#include "ReplayRecorder.h"
#include "Inputs/Input.h"

#include <cstdio>
#include <cstring>

// ===== フォーマット定義 =====
struct ReplayHeader
{
    char magic[4];     // "SMR1"
    uint32_t version;  // 1
    uint32_t flags;    // 0
    uint32_t reserved; // 0
};

#pragma pack(push, 1)
struct ReplayEvent
{
    uint32_t frame;
    char id[32];
    int32_t value;
};
#pragma pack(pop)

// ===== 内部状態 =====
static FILE *g_fp = nullptr;
static bool g_recording = false;

// ===== API =====
void ReplayRecorder::Start(const char *filename)
{
    if (g_recording)
        return;

    g_fp = fopen(filename, "wb");
    if (!g_fp)
    {
        printf("[Replay] Failed to open %s\n", filename);
        return;
    }

    // ヘッダ書き込み
    ReplayHeader h{};
    memcpy(h.magic, "SMR1", 4);
    h.version = 1;
    h.flags = 0;
    h.reserved = 0;

    fwrite(&h, sizeof(h), 1, g_fp);
    fflush(g_fp);

    g_recording = true;
    printf("[Replay] Recording started: %s\n", filename);
}

bool ReplayRecorder::IsRecording()
{
    return g_recording;
}

void ReplayRecorder::Stop()
{
    if (!g_recording)
        return;

    fflush(g_fp);
    fclose(g_fp);
    g_fp = nullptr;
    g_recording = false;

    printf("[Replay] Recording stopped\n");
}

void ReplayRecorder::Capture(uint32_t frame, const char *id, int value)
{
    if (!g_recording || !g_fp)
        return;

    ReplayEvent ev{};
    ev.frame = frame + 1;

    // 安全なコピー（32バイトのバッファなら、最大31文字 + 終端ヌル）
    strncpy(ev.id, id, sizeof(ev.id) - 1);
    ev.id[sizeof(ev.id) - 1] = '\0';

    ev.value = value;

    fwrite(&ev, sizeof(ev), 1, g_fp);
}