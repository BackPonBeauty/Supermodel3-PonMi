/**
 ** Supermodel
 ** A Sega Model 3 Arcade Emulator.
 ** Copyright 2011-2020 Bart Trzynadlowski, Nik Henson, Ian Curtis,
 **                     Harry Tuttle, and Spindizzi
 **
 ** This file is part of Supermodel.
 **
 ** Supermodel is free software: you can redistribute it and/or modify it under
 ** the terms of the GNU General Public License as published by the Free
 ** Software Foundation, either version 3 of the License, or (at your option)
 ** any later version.
 **
 ** Supermodel is distributed in the hope that it will be useful, but WITHOUT
 ** ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 ** FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 ** more details.
 **
 ** You should have received a copy of the GNU General Public License along
 ** with Supermodel.  If not, see <http://www.gnu.org/licenses/>.
 **/

#include "TCPSendAsync.h"
#include "OSD/Logger.h"
#include <utility>

#if defined(_DEBUG)
#include <stdio.h>
#define DPRINTF DebugLog
#else
#define DPRINTF(a, ...)
#endif

static const int RETRY_COUNT = 10; // shrugs

TCPSendAsync::TCPSendAsync(std::string &ip, int port) : m_ip(ip),
														m_port(port),
														m_socket(nullptr),
														m_hasData(false)
{
	SDLNet_Init();

	m_sendThread = std::thread(&TCPSendAsync::SendThread, this);
}

TCPSendAsync::~TCPSendAsync()
{
	if (m_socket)
	{
		SDLNet_TCP_Close(m_socket);
		m_socket = nullptr;
	}

	{
		std::unique_lock<std::mutex> lock(m_mutex);
		m_dataBuffers.clear();
		m_hasData = true;  // must set data ready in case of spurious wake up
		m_cv.notify_one(); // tell locked thread it can wake up
	}

	if (m_sendThread.joinable())
	{
		m_sendThread.join();
	}

	SDLNet_Quit(); // unload lib (winsock dll for windows)
}
bool TCPSendAsync::Send(const void *data, int length)
{
	if (!Connected())
	{
		DPRINTF("Not connected\n");
		return false;
	}

	if (!length)
		return true;

	// --- 圧縮処理の追加 ---
	static unsigned char compressed_payload[4096];
	int final_length = length;
	const void *data_to_queue = data;

	// 3072バイトの場合のみ圧縮
	if (length == 3072)
	{
		// ※TCPSend.cppで定義した compress_packet を呼び出せるようにするか、
		// 同様のロジックをここに配置してください。
		int c_len = compress_packet((unsigned char *)data, length, compressed_payload);
		if (c_len > 0)
		{
			data_to_queue = compressed_payload;
			final_length = c_len;
		}
	}

	// キューに入れるバッファを確保（サイズ用4バイト + データ本体）
	auto dataBuffer = std::unique_ptr<char[]>(new char[final_length + 4]);

	*((int32_t *)dataBuffer.get()) = final_length;			   // 圧縮後のサイズをセット
	memcpy(dataBuffer.get() + 4, data_to_queue, final_length); // データをコピー

	{
		std::unique_lock<std::mutex> lock(m_mutex);
		m_dataBuffers.emplace_back(std::move(dataBuffer));
		m_hasData = true;
		m_cv.notify_one();
	}

	return true;
}

bool TCPSendAsync::Connected()
{
	return m_socket != 0;
}

void TCPSendAsync::SendThread()
{
	while (true)
	{

		std::unique_ptr<char[]> sendData;

		{
			std::unique_lock<std::mutex> lock(m_mutex);
			m_cv.wait(lock, [this]
					  { return m_hasData.load(); });

			if (m_dataBuffers.empty())
			{
				return; // if we have woken up with no data assume we need to exit the thread
			}

			auto front = m_dataBuffers.begin();
			sendData = std::move(*front);
			m_dataBuffers.erase(front);
			m_hasData = false; // potentially we could still have data in pipe, we'll set this at the bottom

			// unlock mutex now so we don't block whilst sending
		}

		if (sendData == nullptr)
		{
			break; // shouldn't be able to get here
		}

		// get send size (which is packed at the start of the data
		auto sendSize = *((int32_t *)sendData.get()) + 4; // send size doesn't include 'header'

		int sent = SDLNet_TCP_Send(m_socket, sendData.get(), sendSize); // pack the length at the start of transmission.
		if (sent < sendSize)
		{
			SDLNet_TCP_Close(m_socket);
			m_socket = nullptr;
			break;
		}

		// we have finished with this buffer so release the data
		sendData = nullptr;

		// check if we still have data in the pipe, if so set ready state again
		{
			std::unique_lock<std::mutex> lock(m_mutex);
			if (m_dataBuffers.size())
			{
				m_hasData = true;
				m_cv.notify_one();
			}
		}
	}
}

bool TCPSendAsync::Connect()
{
	IPaddress ip;
	int result = SDLNet_ResolveHost(&ip, m_ip.c_str(), m_port);

	if (result == 0)
	{
		m_socket = SDLNet_TCP_Open(&ip);
		// --- 修正箇所: 送信側にもTCP_NODELAYを適用 ---
		if (m_socket)
		{
			int sock = (int)(intptr_t)SDLNet_TCP_GetSocket(m_socket);
			int one = 1;
#ifdef _WIN32
			setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof(one));
#else
			setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
#endif
		}
	}

	return Connected();
}
