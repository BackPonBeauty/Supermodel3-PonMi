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

void TCPReceive::ListenFunc()
{
	while (m_running) {

		CThread::Sleep(1);
		if (m_receiveSocket) continue;

		TCPsocket socket = SDLNet_TCP_Accept(m_listenSocket);

        if (socket) {
            // SDL_netのソケットからOS標準のソケット(SOCKET型)を取り出す
            // SDL_netの内部構造は TCPsocket == struct _TCPsocket* なのでキャストで通ります
            #ifdef _WIN32
                SOCKET sock = *(SOCKET*)socket; 
                int one = 1;
                setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof(one));
            #else
                int sock = *(int*)socket;
                int one = 1;
                setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
            #endif

            if (m_receiveSocket) {
                SDLNet_DelSocket(m_socketSet, (SDLNet_GenericSocket)m_receiveSocket.load());
            }

            m_receiveSocket = socket;
            SDLNet_AddSocket(m_socketSet, (SDLNet_GenericSocket)socket);
            DPRINTF("Accepted connection: Optimized with TCP_NODELAY\n");
        }
	}
}

bool TCPReceive::Connected()
{
	return (m_receiveSocket != 0);
}
