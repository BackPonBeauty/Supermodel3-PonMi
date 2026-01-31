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

#include "TCPReceive.h"
#include "OSD/Logger.h"
#include "OSD/Thread.h"

#if defined(_DEBUG)
#include <cstdio>
#define DPRINTF DebugLog
#else
#define DPRINTF(a, ...)
#endif

// --- ファイル冒頭に追加 ---
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h> // TCP_NODELAY や IPPROTO_TCP のために必要
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <netinet/tcp.h>
#endif
#include <zlib.h>
static unsigned char last_packet[8192] = {0};


TCPReceive::TCPReceive(int port) :
	m_listenSocket(nullptr),
	m_receiveSocket(nullptr),
	m_socketSet(nullptr)
{
	SDLNet_Init();

	m_socketSet = SDLNet_AllocSocketSet(1);

	IPaddress ip;
	int result = SDLNet_ResolveHost(&ip, nullptr, port);

	if (result == 0) {
		m_listenSocket = SDLNet_TCP_Open(&ip);
		if (m_listenSocket) {
			m_running = true;
			m_listenThread = std::thread(&TCPReceive::ListenFunc, this);
		}
	}
}

TCPReceive::~TCPReceive()
{
	m_running = false;

	if (m_listenThread.joinable()) {
		m_listenThread.join();
	}

	if (m_listenSocket) {
		SDLNet_TCP_Close(m_listenSocket);
		m_listenSocket = nullptr;
	}

	if (m_receiveSocket) {
		SDLNet_TCP_Close(m_receiveSocket);
		m_receiveSocket = nullptr;
	}

	if (m_socketSet) {
		SDLNet_FreeSocketSet(m_socketSet);
		m_socketSet = nullptr;
	}

	SDLNet_Quit();
}

bool TCPReceive::CheckDataAvailable(int timeoutMS)
{
	if (!m_receiveSocket) {
		return false;
	}

	return SDLNet_CheckSockets(m_socketSet, timeoutMS) > 0;
}

std::vector<char>& TCPReceive::Receive() {
    if (!m_receiveSocket) return m_recBuffer;

    int sizes[2]; // [0]: compressed_size, [1]: original_size
    
    // 1. 常に8バイト(int x 2)のヘッダーを確実に読み切るasdasd
    int h_received = 0;
    while (h_received < 8) {
        int r = SDLNet_TCP_Recv(m_receiveSocket, (char*)sizes + h_received, 8 - h_received);
        if (r <= 0) return m_recBuffer; 
        h_received += r;
    }
    
    int compressed_size = sizes[0];
    int original_size = sizes[1];

    // 2. 受信用バッファの準備（不足している時だけ拡張）
    if (m_tempBuffer.size() < (size_t)compressed_size) {
        m_tempBuffer.resize(compressed_size + 1024); 
    }

    // 3. データ本体の受信（分割されて届く可能性を考慮したループ）
    int body_received = 0;
    while (body_received < compressed_size) {
        int r = SDLNet_TCP_Recv(m_receiveSocket, m_tempBuffer.data() + body_received, compressed_size - body_received);
        if (r <= 0) break;
        body_received += r;
    }

    // 4. 展開とバッファ管理
    if (compressed_size < original_size) {
        // 圧縮されている場合：展開先を確保してデコンプレス
        if (m_recBuffer.size() < (size_t)original_size) {
            m_recBuffer.resize(original_size);
        }
        // decompress_packet 内で last_packet の更新も行われます
        decompress_packet((unsigned char*)m_tempBuffer.data(), compressed_size, (unsigned char*)m_recBuffer.data(), original_size);
        //printf("Decompressed: %d -> %d bytes\n", compressed_size, original_size);
        // ベクトルのサイズを論理的に切り詰める（不要なコピーを避ける）
        if (m_recBuffer.size() != (size_t)original_size) {
            m_recBuffer.resize(original_size);
        }
    } else {
        // 圧縮されていない場合：tempからrecへコピー（または直接recで受けても良いですが、安全策で）
        m_recBuffer.assign(m_tempBuffer.begin(), m_tempBuffer.begin() + compressed_size);
    }

    return m_recBuffer;
}
/*
std::vector<char>& TCPReceive::Receive()
{
	if (!m_receiveSocket) {
		DPRINTF("Can't receive because no socket.\n");
		m_recBuffer.clear();
		return m_recBuffer;
	}

	int size = 0;
	int result = SDLNet_TCP_Recv(m_receiveSocket, &size, sizeof(int));
	DPRINTF("Received %i bytes\n", result);
	if (result <= 0) {
		SDLNet_TCP_Close(m_receiveSocket);
		m_receiveSocket = nullptr;
	}

	// reserve our space
	m_recBuffer.resize(size);

	while (size) {

		result = SDLNet_TCP_Recv(m_receiveSocket, m_recBuffer.data() + (m_recBuffer.size() - size), size);
		DPRINTF("Received %i bytes\n", result);
		if (result <= 0) {
			SDLNet_TCP_Close(m_receiveSocket);
			m_receiveSocket = nullptr;
			break;
		}

		size -= result;
	}

	return m_recBuffer;
}
*/
void TCPReceive::ListenFunc()
{
    while (m_running) {
        // すでに接続されている場合は、接続が切れるまでお休みする
        if (m_receiveSocket) {
            CThread::Sleep(100); // 接続中はチェック頻度を下げてCPUを休ませる
            continue;
        }

        TCPsocket socket = SDLNet_TCP_Accept(m_listenSocket);

        if (socket) {
            // --- TCP_NODELAYの設定（ここは今のままで完璧です） ---
            #ifdef _WIN32
                SOCKET sock = *(SOCKET*)socket; 
                int one = 1;
                setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof(one));
            #else
                int sock = *(int*)socket;
                int one = 1;
                setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
            #endif

            m_receiveSocket = socket;
            SDLNet_AddSocket(m_socketSet, (SDLNet_GenericSocket)socket);
            DPRINTF("Accepted connection: Optimized with TCP_NODELAY\n");
        } else {
            // 接続が来ていない時だけ、少しだけ休む
            CThread::Sleep(16); 
        }
    }
}

bool TCPReceive::Connected()
{
	return (m_receiveSocket != 0);
}

void TCPReceive::decompress_packet(unsigned char* compressed_data, int compressed_len, unsigned char* out_frame, int original_len) {
    unsigned char delta_buffer[original_len];
    unsigned long dest_len = original_len;

    // 1. zlib展開
    uncompress(delta_buffer, &dest_len, compressed_data, compressed_len);

    // 2. XORで復元（last_packetに対して差分を適用）
    for (int i = 0; i < original_len; i++) {
        out_frame[i] = delta_buffer[i] ^ last_packet[i];
        // last_packetを今回のデータに更新
        last_packet[i] = out_frame[i];
    }
}
