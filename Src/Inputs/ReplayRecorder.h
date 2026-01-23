#pragma once

#include <cstdio>
#include <cstdint>

class CInput;

class ReplayRecorder
{
public:
    // 録画開始
    static void Start(const char* filename);

    // 録画中か？
    static bool IsRecording();

    // 終了処理（明示 or atexit 用）
    static void Stop();
	
	static void Capture(uint32_t frame,const char* mapping,int value);
};