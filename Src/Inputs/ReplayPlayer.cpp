#include "ReplayPlayer.h"
#include "Inputs.h"
#include <cstring>
#include <cstdio>
#include <unordered_map>
#include <string>

// ===== フォーマット定義 =====
struct ReplayHeader
{
    char magic[4];
    uint32_t version;
    uint32_t flags;
    uint32_t reserved;
};

#pragma pack(push, 1)
struct ReplayEvent
{
    uint32_t frame;
    char id[32];
    int32_t value;
};
#pragma pack(pop)

// ===== 静的メンバ定義 =====
FILE *ReplayPlayer::s_fp = nullptr;
bool ReplayPlayer::s_playing = false;
uint32_t ReplayPlayer::s_replayFrame = 0;
static std::unordered_map<std::string, int> s_inputState;

std::vector<ReplayEvent> ReplayPlayer::s_frameEvents;
std::vector<ReplayEvent> ReplayPlayer::s_events;
size_t ReplayPlayer::s_cursor = 0;

// ===== API =====
bool ReplayPlayer::Start(const char *filename)
{

    if (s_playing)
        return false;

    s_fp = fopen(filename, "rb");
    if (!s_fp)
    {
        printf("[Replay] Failed to open %s\n", filename);
        return false;
    }

    ReplayHeader h{};
    if (fread(&h, sizeof(h), 1, s_fp) != 1 ||
        memcmp(h.magic, "SMR1", 4) != 0)
    {
        printf("[Replay] Invalid replay file\n");
        fclose(s_fp);
        s_fp = nullptr;
        return false;
    }

    if (h.version != 1)
    {
        printf("[Replay] Unsupported replay version: %u\n", h.version);
        fclose(s_fp);
        s_fp = nullptr;
        return false;
    }

    ReplayEvent ev;
    while (fread(&ev, sizeof(ev), 1, s_fp) == 1)
    {
        s_events.push_back(ev);
    }

    printf("[Replay] Loaded %zu events\n", s_events.size());

    if (!s_events.empty())
    {
        s_replayFrame = s_events[0].frame;
    }
    else
    {
        s_inputState.clear();
        s_replayFrame = 0;
        s_cursor = 0;
    }

    s_playing = true;

    printf("[Replay] Playback started\n");
    return true;
}

bool ReplayPlayer::IsPlaying()
{
    return s_playing;
}

void ReplayPlayer::Stop()
{
    if (!s_playing)
        return;

    fclose(s_fp);
    s_fp = nullptr;
    s_playing = false;

    printf("[Replay] Playback stopped\n");
}

void ReplayPlayer::Tick()
{
    if (!s_playing)
        return;

    ++s_replayFrame;
}
uint32_t ReplayPlayer::GetFrame()
{
    return s_replayFrame;
}
void ReplayPlayer::ProcessEvents(uint32_t emuFrame, std::vector<ReplayEvent> &outEvents)
{
    // ★ まず最初に「もう終わっているか」をチェック
    if (s_cursor >= s_events.size())
    {
        Stop(); // ← 再生終了
        return;
    }
    // 差分イベントを state に反映
    while (s_cursor < s_events.size() &&
           s_events[s_cursor].frame == emuFrame)
    {
        const ReplayEvent &ev = s_events[s_cursor];
        s_inputState[ev.id] = ev.value;
        s_cursor++;
    }

    // ★ 差分反映後に「ここで尽きた」場合も止める
    if (s_cursor >= s_events.size())
    {
        Stop();
        return;
    }

    // 現在の「全状態」を outEvents に詰める
    outEvents.clear();
    for (const auto &it : s_inputState)
    {
        ReplayEvent ev{};
        ev.frame = emuFrame;
        strncpy(ev.id, it.first.c_str(), sizeof(ev.id) - 1);
        ev.id[sizeof(ev.id) - 1] = '\0';
        ev.value = it.second;

        outEvents.push_back(ev);
    }
}

// 特定の入力 ID の現在の値を返す
int ReplayPlayer::GetInputValue(const char *id)
{
    auto it = s_inputState.find(id);
    if (it != s_inputState.end())
    {
        return it->second;
    }
    return 0; // 記録がなければ 0
}

void ReplayPlayer::UpdateState(uint32_t emuFrame)
{
    // cursor が末尾に達するまで、そのフレームのイベントをすべて map に流し込む
    // emuFrame が 100 なら、100番のイベントを全部拾う
    while (s_cursor < s_events.size() && s_events[s_cursor].frame == emuFrame)
    {
        const ReplayEvent &ev = s_events[s_cursor];
        s_inputState[ev.id] = ev.value;
        s_cursor++;
    }

    // 全イベントを読み終えたら終了
    if (s_cursor >= s_events.size())
    {
        Stop();
    }
}
