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

#include "TCPSend.h"
#include "OSD/Logger.h"

#if defined(_DEBUG)
#include <stdio.h>
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

// 作業用バッファ（静的に確保して速度を稼ぐ）
static unsigned char last_packet[8192]; // 3072 から 8192 へ
static unsigned char delta_buffer[8192];
static unsigned char s_send_work_buffer[16384]; // 送信用も余裕を持つ
static unsigned char c_buf[4096];

static bool first_run = true;

TCPSend::TCPSend(std::string &ip, int port) : m_ip(ip),
                                              m_port(port),
                                              m_socket(nullptr)
{
    SDLNet_Init();
}

TCPSend::~TCPSend()
{
    if (m_socket)
    {
        SDLNet_TCP_Close(m_socket);
        m_socket = nullptr;
    }

    SDLNet_Quit(); // unload lib (winsock dll for windows)
}

bool TCPSend::Send(const void *data, int length)
{

    int compressed_len = length;
    const void *p_data = data;
    // analyze_packet_diff((unsigned char*)data, length);
    //  1. 圧縮を試みるasdasd
    if (length > 1500)
    {
        int c_res = compress_packet((unsigned char *)data, length, c_buf);
        if (c_res > 0)
        {
            compressed_len = c_res;
            p_data = c_buf;
        }
        else
        {
            // 圧縮に失敗、または逆に大きくなった場合は元のデータを送る
            compressed_len = length;
            p_data = data;
        }
    }

    // 2. 「ヘッダー8バイト + データ」を1つのメモリにパッキング
    // [0-3]: 圧縮後サイズ, [4-7]: 元のサイズ
    memcpy(s_send_work_buffer, &compressed_len, 4);
    memcpy(s_send_work_buffer + 4, &length, 4);
    memcpy(s_send_work_buffer + 8, p_data, compressed_len);

    // 3. 1回のシステムコールで一気に送る
    SDLNet_TCP_Send(m_socket, s_send_work_buffer, 8 + compressed_len);

    return true;
}

bool TCPSend::Connected()
{
    return m_socket != 0;
}

bool TCPSend::Connect()
{
    IPaddress ip;
    int result = SDLNet_ResolveHost(&ip, m_ip.c_str(), m_port);

    if (result == 0)
    {
        m_socket = SDLNet_TCP_Open(&ip);

        // --- ここから最適化 ---
        if (m_socket)
        {
#ifdef _WIN32
            // SDL_netの内部構造体からSOCKET型を直接取り出す
            SOCKET sock = *(SOCKET *)m_socket;
            int one = 1;
            setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof(one));
#else
            int sock = *(int *)m_socket;
            int one = 1;
            setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
#endif
            DPRINTF("TCP_NODELAY enabled on Sender side.\n");
        }
    }

    return Connected();
}

void TCPSend::analyze_packet_diff(unsigned char *current_packet, int length)
{
    if (first_run)
    {
        memcpy(last_packet, current_packet, length);
        first_run = false;
        return;
    }

    int match_count = 0;
    for (int i = 0; i < length; i++)
    {
        if (current_packet[i] == last_packet[i])
        {
            match_count++;
        }
    }

    double match_rate = (double)match_count / length * 100.0;

    // 100フレームに1回、または変化が激しい時だけログに出すなどの調整を
    //printf("Packet Diff: %d/%d bytes matched (%.2f%%)\n", match_count, length, match_rate);

    // 次回のために保存
    memcpy(last_packet, current_packet, length);
}

int TCPSend::compress_packet(unsigned char *current, int length, unsigned char *out_buf)
{
    // 1. XORで差分を0に変換
    for (int i = 0; i < length; i++)
    {
        delta_buffer[i] = current[i] ^ last_packet[i];
    }

    // 2. zlib圧縮
    unsigned long compressed_size = length;
    int res = compress(out_buf, &compressed_size, delta_buffer, length);

    if (res != Z_OK)
        return -1;

    // 次回のために現在のパケットを保存
    memcpy(last_packet, current, length);

    return (int)compressed_size; // 圧縮後のサイズを返す
}
