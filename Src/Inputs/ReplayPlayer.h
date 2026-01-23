#pragma once

#include <cstdint>
#include <cstdio>
#include <vector>      // ★ 必須
#include <unordered_map>

class CInput;

// イベント構造体は forward 宣言だけ（本体は cpp に書く）
struct ReplayEvent;

class ReplayPlayer
{
public:
    static bool Start(const char* filename);
    static bool IsPlaying();
    static void Stop();
	//static void ProcessEvents();
	static void ProcessEvents(uint32_t emuFrame, std::vector<ReplayEvent>& outEvents);
    static void Reset();
    static void Tick();              // ★ 再生フレームを進める
    static uint32_t GetFrame();       // ★ replayFrame を返す

private:
    static FILE*    s_fp;
    static bool     s_playing;
    static uint32_t s_replayFrame;
    static std::vector<ReplayEvent> s_events; // ★ 宣言
    static size_t   s_cursor;                 // ★ 宣言

    // ★ 1フレームのイベントをすべて保持する
    static std::vector<ReplayEvent> s_frameEvents;
};
