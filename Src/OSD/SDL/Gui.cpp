#include "SDLIncludes.h"
#include <GL/glew.h>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <memory>
#include <thread>
#include <algorithm>
#include "GameLoader.h"
#include "../Pkgs/imgui/imgui.h"
#include "../Pkgs/imgui/imgui_impl_sdl2.h"
#include "../Pkgs/imgui/imgui_impl_opengl3.h"
#include "../Pkgs/imgui/imgui_internal.h"
#include "../Src/Util/NewConfig.h"
#include "Util/ConfigBuilders.h"
#include "../Src/OSD/SDL/SDLInputSystem.h"
#include "../Src/Inputs/Inputs.h"
#include "Main.h"
#include "Font01.h"
#include <ctime>   // time, localtime, strftime 用
#include <fstream> // ofstream 用
#include <SDL.h>
#include <windows.h>
#include <shellapi.h>

// ★画像読み込みライブラリの追加
#include "../Src/Graphics/stb_image.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN // 競合を防ぐための定数
#endif
#include <windows.h>
#include <shlobj.h>  // ★これが無いと BROWSEINFO でエラーになります
#include <objbase.h> // CoTaskMemFree 用
#else
#include <stdio.h> // Linux用 popen 用
#endif

#ifdef _WIN32
#include "../Src/OSD/Windows/DirectInputSystem.h"
#endif

static std::vector<std::string> resolutions;
static int selectedResIndex = 0;
static bool resLoaded = false;
static bool showPreviewWindow = false;
static int previewW = 0, previewH = 0;
static SDL_Window *g_PreviewWindow = nullptr; // プレビュー用の新しい窓
static int previewPosX = 0;
static int previewPosY = 0;
static char bufPosX[16] = "0";
static char bufPosY[16] = "0";
static bool scrollToSelected = true;

// ★画像管理用のグローバル変数
static GLuint g_GameTexture = 0;
static std::string g_LoadedImageName = "";
static int g_ImgWidth = 0;
static int g_ImgHeight = 0;
static int XResolution = 0;
static int YResolution = 0;
static float RefreshRate = 60.0f;
static bool record = false;
static bool replay = false;
static std::string replayFilename = "";
namespace fs = std::filesystem;

static void SaveSupermodelConfig(const std::string &path, std::map<std::string, std::string> &updates)
{
    std::ifstream ifs(path);
    std::vector<std::string> newLines;
    std::string line;
    std::map<std::string, bool> updatedFlags;

    // 1. 既存のファイルを一行ずつ読み込んで、一致するキーがあれば書き換える
    if (ifs.is_open())
    {
        while (std::getline(ifs, line))
        {
            bool matched = false;
            for (auto const &[key, val] : updates)
            {
                // "Key =" または "Key=" で始まる行を探す
                if (line.compare(0, key.length(), key) == 0)
                {
                    // キーの直後が '=' またはスペースであることを確認
                    size_t nextCharPos = key.length();
                    while (nextCharPos < line.length() && line[nextCharPos] == ' ')
                        nextCharPos++;

                    if (nextCharPos < line.length() && line[nextCharPos] == '=')
                    {
                        newLines.push_back(key + " = " + val);
                        updatedFlags[key] = true;
                        matched = true;
                        break;
                    }
                }
            }
            if (!matched)
                newLines.push_back(line); // 一致しなかった行（コメント等）はそのまま保持
        }
        ifs.close();
    }

    // 2. 元ファイルになかった新規項目があれば末尾に追加
    for (auto const &[key, val] : updates)
    {
        if (!updatedFlags[key])
        {
            newLines.push_back(key + " = " + val);
        }
    }

    // 3. ファイルに上書き保存
    std::ofstream ofs(path, std::ios::trunc);
    if (ofs.is_open())
    {
        for (const auto &l : newLines)
        {
            ofs << l << "\n";
        }
        ofs.close();
    }
}

// ★テクスチャ読み込み関数（端折らず実装）
static bool LoadTextureFromFile(const char *filename, GLuint *out_texture)
{
    int width, height, channels;
    unsigned char *data = stbi_load(filename, &width, &height, &channels, 4);
    if (data == NULL)
        return false;

    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    stbi_image_free(data);
    *out_texture = tex;
    return true;
}

static bool LoadTextureFromFile(const char *filename, GLuint *out_texture, int *out_width, int *out_height)
{
    int width, height, channels;
    unsigned char *data = stbi_load(filename, &width, &height, &channels, 4);
    if (data == NULL)
        return false;

    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    stbi_image_free(data);

    *out_texture = tex;
    *out_width = width;   // ★幅を保存
    *out_height = height; // ★高さを保存
    return true;
}

void ClosePreviewWindow()
{
    if (g_PreviewWindow)
    {
        // 閉じる瞬間の座標を取得
        SDL_GetWindowPosition(g_PreviewWindow, &previewPosX, &previewPosY);

        // テキストボックス表示用に文字列へ変換
        snprintf(bufPosX, sizeof(bufPosX), "%d", previewPosX);
        snprintf(bufPosY, sizeof(bufPosY), "%d", previewPosY);

        SDL_DestroyWindow(g_PreviewWindow);
        g_PreviewWindow = nullptr;
    }
}

// --- 起動ロジック ---
static std::string GetRomPath(int selectedGame, const std::map<std::string, Game> &games, Util::Config::Node &config)
{
    if (selectedGame >= 0)
    {
        int index = 0;
        std::string romDir = config["Dir"].ValueAs<std::string>();

        for (auto &pair : games)
        {
            if (selectedGame == index)
            {
                std::string fullPath = (std::filesystem::path(romDir) / (pair.second.name + ".zip")).string();
                printf("[ROM PATH]\n");
                printf("  Dir      : %s\n", romDir.c_str());
                printf("  GameName : %s\n", pair.second.name.c_str());
                printf("  FullPath : %s\n", fullPath.c_str());

                return fullPath;
            }
            index++;
        }
    }
    return "";
}

static bool CheckRomExists(
    int selectedGame,
    const std::map<std::string, Game> &games,
    Util::Config::Node &config)
{
    if (selectedGame < 0)
    {
        printf("[ROM CHECK] No game selected\n");
        return false;
    }

    std::string romDir;

    // ★ Dir の存在チェック（Hasが無いので例外で判定）
    try
    {
        romDir = config["Dir"].ValueAs<std::string>();
    }
    catch (const std::exception &e)
    {
        printf("[ROM CHECK] Dir not found in config (%s)\n", e.what());
        return false;
    }

    std::replace(romDir.begin(), romDir.end(), '\\', '/');

    int index = 0;
    for (const auto &pair : games)
    {
        if (index == selectedGame)
        {

            std::string fullPath =
                (std::filesystem::path(romDir) /
                 (pair.second.name + ".zip"))
                    .string();

            printf("[ROM CHECK]\n");
            printf("  Dir      : %s\n", romDir.c_str());
            printf("  GameName : %s\n", pair.second.name.c_str());
            printf("  FullPath : %s\n", fullPath.c_str());

            if (std::filesystem::exists(fullPath))
            {
                printf("  EXISTS   : YES\n");
                return true;
            }
            else
            {
                printf("  EXISTS   : NO\n");
                return false;
            }
        }
        index++;
    }

    printf("[ROM CHECK] Game index not found\n");
    return false;
}

// --- GUIレイアウト (修正点: 関数定義を正しく追加) ---
// static void GUI(const ImGuiIO &io, Util::Config::Node &config, const std::map<std::string, Game> &games, int &selectedGameIndex, bool &exit, bool &exitLaunch, bool &saveSettings, SDL_Window *window , int &selectedResIndex)
void GUI(ImGuiIO &io, Util::Config::Node &config,
         const std::map<std::string, Game> &games, int &selectedGameIndex,
         bool &exit, bool &exitLaunch, bool &saveSettings, SDL_Window *window,
         int &selectedResIndex, int &engineSelection, bool &vVsync, bool &vQuadRendering,
         bool &vGPUMultiThreaded, bool &vMultiThreaded, bool &vMultiTexture,
         bool &vBorderless, bool &vTrueAR, bool &vFullScreen, bool &vWideScreen,
         bool &vWideBackground, bool &vStretch, bool &vShowFrameRate, bool &vThrottle,
         bool &vNoWhiteFlash, bool &vTrueHz, int &superSampling, int &selectedCRT, int &selectedUpscale,
         int &ppcFreq, int &WindowXPosition, int &WindowYPosition, int &Scanline, int &Barrel,
         int &musicVol, int &sfxVol, int &balance, bool &vEmulateSound,
         bool &vEmulateDSB, bool &vFlipStereo, bool &vLegacySoundDSP,
         int &selectedInputType, int &selectedCrosshair, int &selectedStyle,
         bool &vForceFeedback, bool &vNetwork, bool &vSimulateNet,
         char *bufPortIn, char *bufPortOut, char *bufAddressOut)
{
    // 基本スケールの計算
    float scale = io.DisplaySize.y / 600.0f;
    if (scale < 0.5f)
        scale = 0.5f;

    if (!games.empty())
    {
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
        {
            if (selectedGameIndex > 0)
            {
                selectedGameIndex--;
            }
            else
            {
                selectedGameIndex = (int)games.size() - 1; // 一番下へループ
            }
            scrollToSelected = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
        {
            if (selectedGameIndex < (int)games.size() - 1)
            {
                selectedGameIndex++;
            }
            else
            {
                selectedGameIndex = 0; // 一番上へループ
            }
            scrollToSelected = true;
        }
    }

    // ボタンの色をピンクに設定（通常、ホバー、アクティブ）
    // Japan Blue (サムライブルー) 系の配色
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.00f, 0.20f, 0.45f, 1.0f));        // 通常：深い紺色 (Japan Blue)
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.00f, 0.35f, 0.70f, 1.0f)); // ホバー：鮮やかな青
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.00f, 0.15f, 0.35f, 1.0f));  // クリック：さらに深い紺色

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("LauncherCanvas", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

    // ウィンドウ全体の基本スケール
    ImGui::SetWindowFontScale(scale);
    ImGui::TextColored(ImVec4(0.0f, 0.8f, 1.0f, 1.0f), "SEGA MODEL3 UI v2");
    ImGui::Separator();

    // 高さの計算
    float footerHeight = 20.0f * scale;

    // 左カラムの幅をウィンドウの50%に自動計算（追従）
    float leftPaneWidth = io.DisplaySize.x * 0.35f;
    float splitterWidth = 8.0f;
    float imageAreaHeight = 200.0f * scale;
    float upperContentHeight = ImGui::GetContentRegionAvail().y - imageAreaHeight - footerHeight - (20.0f * scale);
    float listAreaHeight = upperContentHeight - imageAreaHeight - ImGui::GetStyle().ItemSpacing.y;
    float optionsHeight = upperContentHeight - ImGui::GetStyle().ItemSpacing.y + imageAreaHeight;
    ImGui::BeginGroup(); // ★ここから左側のセット
    {
        if (ImGui::BeginChild("ImageArea", ImVec2(leftPaneWidth, imageAreaHeight), true))
        {
            ImGui::SetWindowFontScale(scale); // スケール再適用

            // ★画像表示ロジックの埋め込み
            if (selectedGameIndex >= 0)
            {
                int idx = 0;
                for (auto const &pair : games)
                {
                    if (idx == selectedGameIndex)
                    {
                        if (g_LoadedImageName != pair.second.name)
                        {
                            if (g_GameTexture)
                            {
                                glDeleteTextures(1, &g_GameTexture);
                                g_GameTexture = 0;
                            }
                            std::string imgPath = "Snaps/" + pair.second.name + ".png";
                            if (!LoadTextureFromFile(imgPath.c_str(), &g_GameTexture, &g_ImgWidth, &g_ImgHeight))
                            {
                                imgPath = "Snaps/" + pair.second.name + ".jpg";
                                LoadTextureFromFile(imgPath.c_str(), &g_GameTexture, &g_ImgWidth, &g_ImgHeight);
                            }
                            g_LoadedImageName = pair.second.name;
                        }
                        break;
                    }
                    idx++;
                }
            }

            if (g_GameTexture && g_ImgWidth > 0 && g_ImgHeight > 0)
            {
                float availW = ImGui::GetContentRegionAvail().x;
                float availH = ImGui::GetContentRegionAvail().y;

                // --- アスペクト比維持の計算 ---
                float ratioW = availW / (float)g_ImgWidth;
                float ratioH = availH / (float)g_ImgHeight;
                float ratio = (ratioW < ratioH) ? ratioW : ratioH; // 小さい方の倍率を採用

                float drawW = (float)g_ImgWidth * ratio;
                float drawH = (float)g_ImgHeight * ratio;

                // 中央寄せにするためのオフセット計算
                float offsetX = (availW - drawW) * 0.5f;
                float offsetY = (availH - drawH) * 0.5f;
                ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + offsetX, ImGui::GetCursorPosY() + offsetY));

                ImGui::Image((void *)(intptr_t)g_GameTexture, ImVec2(drawW, drawH));
            }
            else
            {
                ImGui::TextDisabled("(NO IMAGE)");
            }
        }
        ImGui::EndChild();

        // --- 左側: ゲームリスト ---
        if (ImGui::BeginChild("GameList", ImVec2(leftPaneWidth, upperContentHeight), true))
        {
            ImGui::SetWindowFontScale(scale);

            static ImGuiTableFlags tableFlags = ImGuiTableFlags_BordersV | ImGuiTableFlags_BordersOuterH | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;

            if (ImGui::BeginTable("GameTable", 2, tableFlags))
            {
                ImGui::TableSetupColumn("Game Title", ImGuiTableColumnFlags_WidthStretch, 0.7f);
                ImGui::TableSetupColumn("ROM", ImGuiTableColumnFlags_WidthStretch, 0.3f);
                ImGui::TableHeadersRow();

                int i = 0;
                for (auto const &pair : games)
                {
                    const bool isSelected = (selectedGameIndex == i);
                    const Game &game = pair.second;
                    ImGui::PushID(game.name.c_str());

                    ImGui::TableNextRow(ImGuiTableRowFlags_None, 16.0f * scale);

                    ImGui::TableSetColumnIndex(0);
                    ImGui::SetWindowFontScale(scale);

                    std::string displayName = game.title.empty() ? game.name : game.title;

                    displayName.erase(std::remove(displayName.begin(), displayName.end(), '\n'), displayName.end());
                    displayName.erase(std::remove(displayName.begin(), displayName.end(), '\r'), displayName.end());

                    if (ImGui::Selectable(displayName.c_str(), selectedGameIndex == i,
                                          ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap))
                    {
                        selectedGameIndex = i;
                    }
                    if (isSelected && scrollToSelected)
                    {
                        ImGui::SetScrollHereY(0.5f); // 0.5f は画面の真ん中に持ってくる指定（0.0fなら一番上、1.0fなら一番下）
                        scrollToSelected = false;    // 一度実行したらフラグを下ろす
                    }

                    ImGui::TableSetColumnIndex(1);
                    ImGui::SetWindowFontScale(scale);
                    // 改行を防ぐため TextUnformatted を使用
                    std::string romName = game.name;
                    romName.erase(std::remove(romName.begin(), romName.end(), '\n'), romName.end());
                    romName.erase(std::remove(romName.begin(), romName.end(), '\r'), romName.end());

                    ImGui::TextUnformatted(game.name.c_str());

                    ImGui::PopID();
                    i++;
                }
                ImGui::EndTable();
            }
        }
        ImGui::EndChild();
    }
    ImGui::EndGroup();

    ImGui::SameLine();

    ImGui::BeginGroup();
    {
        // 右下: オプション
        if (ImGui::BeginChild("RightOptions", ImVec2(0, optionsHeight), true))
        {
            ImGui::SetWindowFontScale(scale); // スケール再適用
            if (ImGui::BeginTabBar("Tabs"))
            {
                if (ImGui::BeginTabItem("Video"))
                {
                    ImGui::Text("Video Settings");

                    // メインGUIの右側カラムの下部付近
                    ImGui::Spacing();
                    ImGui::Separator();
                    // ImGui::Text("Last Preview Position:");

                    // --- new3D / Legacy ラジオボタン ---
                    ImGui::Text("Graphics Engine");
                    // static int engineSelection = 0; // 0: new3D, 1: Legacy (実際はconfigから読み込む)
                    if (ImGui::RadioButton("new3D", &engineSelection, 0))
                    { /* config更新 */
                    }
                    ImGui::SameLine();
                    if (ImGui::RadioButton("Legacy", &engineSelection, 1))
                    { /* config更新 */
                    }

                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();

                    // --- 2列のチェックボックスレイアウト ---
                    ImGui::Columns(2, "VideoSettingsColumns", false); // 2列作成、境界線なし

                    // --- 1列目 ---
                    {
                        if (ImGui::Checkbox("Vsync", &vVsync))
                        {
                            saveSettings = true;
                        }
                        if (ImGui::Checkbox("QuadRendering", &vQuadRendering))
                        {
                            saveSettings = true;
                        }
                        if (ImGui::Checkbox("GPUMultiThreaded", &vGPUMultiThreaded))
                        {
                            saveSettings = true;
                        }
                        if (ImGui::Checkbox("MultiThreaded", &vMultiThreaded))
                        {
                            saveSettings = true;
                        }
                        if (ImGui::Checkbox("MultiTexture", &vMultiTexture))
                        {
                            saveSettings = true;
                        }
                        if (ImGui::Checkbox("Borderless", &vBorderless))
                        {
                            saveSettings = true;
                        }
                        if (ImGui::Checkbox("True-Hz", &vTrueHz))
                        {
                            saveSettings = true;
                        }
                        if (ImGui::Checkbox("True-AR", &vTrueAR))
                        {
                            saveSettings = true;
                        }
                    }

                    ImGui::NextColumn(); // 2列目へ移動

                    // --- 2列目 ---
                    {
                        if (ImGui::Checkbox("FullScreen", &vFullScreen))
                        {
                            saveSettings = true;
                        }
                        if (ImGui::Checkbox("WideScreen", &vWideScreen))
                        {
                            saveSettings = true;
                        }
                        if (ImGui::Checkbox("WideBackground", &vWideBackground))
                        {
                            saveSettings = true;
                        }
                        if (ImGui::Checkbox("Stretch", &vStretch))
                        {
                            saveSettings = true;
                        }
                        if (ImGui::Checkbox("ShowFrameRate", &vShowFrameRate))
                        {
                            saveSettings = true;
                        }
                        if (ImGui::Checkbox("Throttle", &vThrottle))
                        {
                            saveSettings = true;
                        }
                        if (ImGui::Checkbox("NoWhiteFlash", &vNoWhiteFlash))
                        {
                            saveSettings = true;
                        }
                    }
                    ImGui::Columns(1);
                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();
                    int w, h;
                    static bool isFirstFrame = true;
                    if (isFirstFrame)
                    {
                        snprintf(bufPosX, sizeof(bufPosX), "%d", WindowXPosition);
                        snprintf(bufPosY, sizeof(bufPosY), "%d", WindowYPosition);
                        isFirstFrame = false;
                    }

                    if (ImGui::Button("Position##Button"))
                    {

                        if (g_PreviewWindow)
                        {
                            SDL_DestroyWindow(g_PreviewWindow);
                            g_PreviewWindow = nullptr;
                        }

                        if (sscanf(resolutions[selectedResIndex].c_str(), "%dx%d", &w, &h) == 2)
                        {

                            // 1. メインウィンドウが現在いるディスプレイのインデックスを取得
                            // window は RunGUI 内で定義されたメインウィンドウのポインタです
                            int displayIndex = SDL_GetWindowDisplayIndex(window);
                            if (displayIndex < 0)
                                displayIndex = 0; // エラー時は 0番 を参照

                            // 2. そのディスプレイの領域（座標とサイズ）を取得
                            SDL_Rect rect;
                            if (SDL_GetDisplayBounds(displayIndex, &rect) == 0)
                            {

                                // 3. そのディスプレイ内での中央位置を計算
                                // rect.x, rect.y がそのモニターの左上の開始地点
                                int posX = rect.x + (rect.w - w) / 2;
                                int posY = rect.y + (rect.h - h) / 2;

                                // 4. SDLで新しいウィンドウを生成
                                g_PreviewWindow = SDL_CreateWindow(
                                    "Resolution Preview",
                                    posX, posY, w, h,
                                    SDL_WINDOW_SHOWN);
                            }
                        }
                    }

                    float availableWidth = ImGui::GetContentRegionAvail().x - (150.0f * scale); // ラベル分を引いた残り
                    float inputWidth = (availableWidth * 0.5f);                                 // 間隔を引いて半分に

                    ImGui::SameLine(150.0f * scale);

                    // --- X座標 ---
                    ImGui::PushItemWidth(inputWidth);
                    if (ImGui::InputText("##PosX", bufPosX, sizeof(bufPosX)))
                    {
                        saveSettings = true;
                    }
                    ImGui::PopItemWidth();

                    ImGui::SameLine();

                    // --- Y座標 ---
                    ImGui::PushItemWidth(inputWidth);
                    if (ImGui::InputText("##PosY", bufPosY, sizeof(bufPosY)))
                    {
                        saveSettings = true;
                    }
                    ImGui::PopItemWidth();

                    ImGui::Text("Resolution");
                    ImGui::SameLine(150.0f * scale);
                    ImGui::PushItemWidth(-1);
                    // 現在の選択値を文字列で表示するための処理
                    const char *previewValue = resolutions.empty() ? "" : resolutions[selectedResIndex].c_str();

                    if (ImGui::BeginCombo("##Resolution", resolutions[selectedResIndex].c_str()))
                    {
                        ImGui::SetWindowFontScale(scale);
                        for (int i = 0; i < (int)resolutions.size(); i++)
                        {
                            const bool isSelected = (selectedResIndex == i);
                            if (ImGui::Selectable(resolutions[i].c_str(), isSelected))
                            {
                                selectedResIndex = i;
                                /*

                                // --- ここでiniに保存する値をパース（例：1920x1080 -> X=1920, Y=1080） ---
                                int w, h;
                                if (sscanf(resolutions[i].c_str(), "%dx%d", &w, &h) == 2)
                                {
                                    XResolution = w;
                                    YResolution = h;
                                    saveSettings = true; // 保存フラグを立てる
                                }
                                    */
                            }

                            // 初期フォーカス
                            if (isSelected)
                                ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }

                    // --- SuperSampling スライダー (1～8) ---
                    // static int superSampling = 1; // 実際はconfigから読み込む
                    ImGui::Text("Super Sampling");
                    ImGui::SameLine(150.0f * scale);
                    ImGui::PushItemWidth(-1); // 右端までスライダーを広げる
                    if (ImGui::SliderInt("##SS", &superSampling, 1, 8))
                    {
                        saveSettings = true;
                    }
                    ImGui::PopItemWidth();

                    // --- CRTColor ドロップダウン ---
                    const char *crtItems[] = {
                        "0=none",
                        "1=ARI/D93 (recommended for all JP developed games)",
                        "2=PVM_20M2U/D93",
                        "3=BT601_525/D93",
                        "4=BT601_525/D65 (recommended for all US developed games)",
                        "5=BT601_625/D65 (recommended for all EUR/AUS developed games)"};

                    // static は消して、引数で渡ってきた selectedCRT を使う
                    ImGui::Text("CRT Color");
                    ImGui::SameLine(150.0f * scale);
                    ImGui::PushItemWidth(-1);

                    // 現在選択されている番号の「文字列」をプレビューに表示
                    if (ImGui::BeginCombo("##CRTColor", crtItems[selectedCRT]))
                    {
                        ImGui::SetWindowFontScale(scale);
                        for (int n = 0; n < IM_ARRAYSIZE(crtItems); n++)
                        {
                            // 今のループ回数(n)が、選択中の番号(selectedCRT)と一致するか
                            bool is_selected = (selectedCRT == n);
                            if (ImGui::Selectable(crtItems[n], is_selected))
                            {
                                // 選んだら、その番号(n)を selectedCRT に代入
                                selectedCRT = n;
                                saveSettings = true;
                            }
                            if (is_selected)
                                ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::PopItemWidth();

                    // --- UpscaleMode ドロップダウン (1～3) ---
                    const char *upscaleItems[] = {"0=none/sharp pixels", "1=biquintic", "2=bilinear", "3=bicubic"};
                    // static int selectedUpscale = 0;
                    ImGui::Text("Upscale Mode");
                    ImGui::SameLine(150.0f * scale);
                    ImGui::PushItemWidth(-1);
                    if (ImGui::BeginCombo("##UpscaleMode", upscaleItems[selectedUpscale]))
                    {
                        ImGui::SetWindowFontScale(scale);
                        for (int n = 0; n < IM_ARRAYSIZE(upscaleItems); n++)
                        {
                            bool is_selected = (selectedUpscale == n);
                            if (ImGui::Selectable(upscaleItems[n], is_selected))
                            {
                                selectedUpscale = n;
                                saveSettings = true;
                            }
                            if (is_selected)
                                ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::PopItemWidth();
                    // static int ppcFreq = 57; // デフォルト値（configから読み込む変数に置き換えてください）
                    ImGui::Text("PPC Frequency");
                    ImGui::SameLine(150.0f * scale);
                    ImGui::PushItemWidth(-1);
                    if (ImGui::SliderInt("##PPCFreq", &ppcFreq, 0, 200))
                    {
                        // config.Set("PowerPCFrequency", (int64_t)ppcFreq); // 必要に応じてconfigへ
                        saveSettings = true;
                    }
                    ImGui::PopItemWidth();
                    // static int Scanline = 1;
                    ImGui::Text("Scanline Strength");
                    ImGui::SameLine(150.0f * scale);
                    ImGui::PushItemWidth(-1);
                    if (ImGui::SliderInt("##Scanline", &Scanline, 1, 10))
                    {
                        // config.Set("PowerPCFrequency", (int64_t)ppcFreq); // 必要に応じてconfigへ
                        saveSettings = true;
                    }
                    ImGui::PopItemWidth();
                    // static int Barrel = 1;
                    ImGui::Text("Barrel Strength");
                    ImGui::SameLine(150.0f * scale);
                    ImGui::PushItemWidth(-1);
                    if (ImGui::SliderInt("##Barrel", &Barrel, 0, 10))
                    {
                        // config.Set("PowerPCFrequency", (int64_t)ppcFreq); // 必要に応じてconfigへ
                        saveSettings = true;
                    }
                    ImGui::PopItemWidth();
                    ImGui::EndTabItem();
                }
                // --- Audio タブ ---
                if (ImGui::BeginTabItem("Sound"))
                {
                    ImGui::SetWindowFontScale(scale);
                    ImGui::Text("Audio Settings");
                    ImGui::Separator();
                    ImGui::Spacing();

                    // スライダー項目
                    // static int musicVol = 100;
                    // static int sfxVol = 100;
                    // static int balance = 0;

                    float labelWidth = 140.0f * scale; // ラベルの幅を揃える

                    // Music
                    ImGui::Text("Music");
                    ImGui::SameLine(labelWidth);
                    ImGui::PushItemWidth(-1);
                    if (ImGui::SliderInt("##Music", &musicVol, 0, 100))
                    {
                        saveSettings = true;
                    }
                    ImGui::PopItemWidth();

                    // Balance
                    ImGui::Text("Balance");
                    ImGui::SameLine(labelWidth);
                    ImGui::PushItemWidth(-1);
                    if (ImGui::SliderInt("##Balance", &balance, -100, 100))
                    {
                        saveSettings = true;
                    }
                    ImGui::PopItemWidth();

                    // Sound
                    ImGui::Text("Sound");
                    ImGui::SameLine(labelWidth);
                    ImGui::PushItemWidth(-1);
                    if (ImGui::SliderInt("##Sound", &sfxVol, 0, 100))
                    {
                        saveSettings = true;
                    }
                    ImGui::PopItemWidth();

                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();

                    // --- チェックボックス項目 ---
                    // 実際には config["EmulateSound"].ValueAs<bool>() 等と連動させると良いです
                    // static bool vEmulateSound = true;
                    // static bool vEmulateDSB = true;
                    // static bool vFlipStereo = false;
                    // static bool vLegacySoundDSP = false;

                    if (ImGui::Checkbox("EmulateSound", &vEmulateSound))
                    {
                        saveSettings = true;
                    }
                    if (ImGui::Checkbox("EmulateDSB", &vEmulateDSB))
                    {
                        saveSettings = true;
                    }
                    if (ImGui::Checkbox("FlipStereo", &vFlipStereo))
                    {
                        saveSettings = true;
                    }
                    if (ImGui::Checkbox("LegacySoundDSP", &vLegacySoundDSP))
                    {
                        saveSettings = true;
                    }

                    ImGui::EndTabItem();
                }

                // --- Control タブ ---
                if (ImGui::BeginTabItem("Control"))
                {
                    ImGui::SetWindowFontScale(scale);
                    ImGui::Text("Control Settings");
                    ImGui::Separator();
                    ImGui::Spacing();
                    float labelWidth = 150.0f * scale; // 他のタブと統一

                    // --- Input Type ---
                    const char *inputTypes[] = {"Xinput", "Dinput", "Rawinput"};
                    // static int selectedInputType = 0; // config連動を想定
                    ImGui::Text("Input Type");
                    ImGui::SameLine(labelWidth);
                    ImGui::PushItemWidth(-1);
                    if (ImGui::BeginCombo("##InputType", inputTypes[selectedInputType]))
                    {
                        ImGui::SetWindowFontScale(scale);
                        for (int n = 0; n < IM_ARRAYSIZE(inputTypes); n++)
                        {
                            if (ImGui::Selectable(inputTypes[n], selectedInputType == n))
                                selectedInputType = n;
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::PopItemWidth();

                    // --- CrossHairs ---
                    const char *crosshairTypes[] = {"Disable", "Player1", "Player2", "2Players"};
                    // static int selectedCrosshair = 0;
                    ImGui::Text("CrossHairs");
                    ImGui::SameLine(labelWidth);
                    ImGui::PushItemWidth(-1);
                    if (ImGui::BeginCombo("##CrossHairs", crosshairTypes[selectedCrosshair]))
                    {
                        ImGui::SetWindowFontScale(scale);
                        for (int n = 0; n < IM_ARRAYSIZE(crosshairTypes); n++)
                        {
                            if (ImGui::Selectable(crosshairTypes[n], selectedCrosshair == n))
                                selectedCrosshair = n;
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::PopItemWidth();

                    // --- Style (Vector / bmp) ---
                    const char *styleTypes[] = {"vector", "bmp"};
                    // static int selectedStyle = 0;
                    ImGui::Text("Style");
                    ImGui::SameLine(labelWidth);
                    ImGui::PushItemWidth(-1);
                    if (ImGui::BeginCombo("##Style", styleTypes[selectedStyle]))
                    {
                        ImGui::SetWindowFontScale(scale);
                        for (int n = 0; n < IM_ARRAYSIZE(styleTypes); n++)
                        {
                            if (ImGui::Selectable(styleTypes[n], selectedStyle == n))
                                selectedStyle = n;
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::PopItemWidth();

                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();

                    // --- ForceFeedback ---
                    // static bool vForceFeedback = false;
                    if (ImGui::Checkbox("Force Feedback", &vForceFeedback))
                    {
                        saveSettings = true;
                    }

                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();

                    // --- Config ボタン ---
                    if (ImGui::Button("CONFIG", ImVec2(-1, 40.0f * scale)))
                    {
                        ShellExecuteA(NULL, "open", "Supermodel.exe", "-config-inputs", NULL, SW_SHOWNORMAL);

                        exitLaunch = false;
                        exit = true;
                        saveSettings = true;
                    }

                    ImGui::EndTabItem();
                }

                // --- Network タブ ---
                if (ImGui::BeginTabItem("Network"))
                {
                    ImGui::SetWindowFontScale(scale);
                    ImGui::Text("Network Setting");
                    ImGui::Separator();
                    ImGui::Spacing();
                    float labelWidth = 150.0f * scale; // 他のタブと統一

                    // --- チェックボックス項目 ---
                    // static bool vNetwork = false;
                    // static bool vSimulateNet = false;

                    if (ImGui::Checkbox("Network", &vNetwork))
                    {
                        saveSettings = true;
                    }
                    if (ImGui::Checkbox("SimulateNet", &vSimulateNet))
                    {
                        saveSettings = true;
                    }

                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();

                    // --- テキスト入力項目 ---
                    /*
                    static char bufPortIn[8] = "1970";
                    static char bufPortOut[8] = "1971";
                    static char bufAddressOut[128] = "127.0.0.1";
                    */
                    // PortIn
                    ImGui::Text("PortIn");
                    ImGui::SameLine(labelWidth);
                    ImGui::PushItemWidth(100.0f * scale); // ポート番号用なので少し短めに
                    if (ImGui::InputText("##PortIn", bufPortIn, sizeof(bufPortIn), ImGuiInputTextFlags_CharsDecimal))
                    {
                        saveSettings = true;
                    }
                    ImGui::PopItemWidth();

                    // PortOut
                    ImGui::Text("PortOut");
                    ImGui::SameLine(labelWidth);
                    ImGui::PushItemWidth(100.0f * scale);
                    if (ImGui::InputText("##PortOut", bufPortOut, sizeof(bufPortOut), ImGuiInputTextFlags_CharsDecimal))
                    {
                        saveSettings = true;
                    }
                    ImGui::PopItemWidth();

                    // AddressOut
                    ImGui::Text("AddressOut");
                    ImGui::SameLine(labelWidth);
                    ImGui::PushItemWidth(-1); // アドレスは長い可能性があるので右端まで
                    if (ImGui::InputText("##AddressOut", bufAddressOut, sizeof(bufAddressOut)))
                    {
                        saveSettings = true;
                    }
                    ImGui::PopItemWidth();

                    ImGui::EndTabItem();
                }

                // --- Other タブ ---
                if (ImGui::BeginTabItem("Other"))
                {
                    ImGui::SetWindowFontScale(scale);

                    // --- Replay Control Section ---
                    ImGui::Text("Replay System");
                    ImGui::Separator();
                    ImGui::Spacing();

                    // Replay用のChild Windowでひとまとめにする
                    ImGui::BeginChild("ReplayControl", ImVec2(0, 150 * scale), true); // 少し高さを広げたぜ
                    {
                        ImGui::SetWindowFontScale(scale);

                        // 1. .recファイルをスキャン
                        static std::vector<std::string> replayFiles;
                        static int selectedFileIdx = -1;
                        static float lastScanTime = 0;

                        // 5秒ごとにフォルダを再スキャン
                        float currentTime = (float)ImGui::GetTime();
                        if (currentTime - lastScanTime > 5.0f || (replayFiles.empty() && lastScanTime == 0))
                        {
                            std::string folderPath = "Replays";

                            // 1. フォルダがなければ作成しておく（親切設計）
                            if (!fs::exists(folderPath))
                            {
                                fs::create_directory(folderPath);
                            }

                            // 今選択しているファイル名を一時保存（スキャン後に位置を復元するため）
                            std::string currentSelectedName = (selectedFileIdx >= 0 && selectedFileIdx < (int)replayFiles.size())
                                                                  ? replayFiles[selectedFileIdx]
                                                                  : "";

                            replayFiles.clear();

                            // 2. "Replays" フォルダ内をスキャン
                            for (const auto &entry : fs::directory_iterator(folderPath))
                            {
                                if (entry.path().extension() == ".rec")
                                {
                                    replayFiles.push_back(entry.path().string());
                                }
                            }

                            // 3. 降順ソート（新しい日付＝数字が大きい方が上に来るようにする）

                            std::sort(replayFiles.rbegin(), replayFiles.rend());

                            // 4. 選択位置の復元
                            selectedFileIdx = -1;
                            for (int i = 0; i < (int)replayFiles.size(); i++)
                            {
                                if (replayFiles[i] == currentSelectedName)
                                {
                                    selectedFileIdx = i;
                                    break;
                                }
                            }

                            lastScanTime = currentTime;
                        }

                        // 2. コンボボックスでファイルを選択
                        const char *preview = (selectedFileIdx >= 0) ? replayFiles[selectedFileIdx].c_str() : "Select Replay...";
                        if (ImGui::BeginCombo("Files", preview))
                        {
                            ImGui::SetWindowFontScale(scale);
                            for (int i = 0; i < replayFiles.size(); i++)
                            {
                                bool isSelected = (selectedFileIdx == i);
                                if (ImGui::Selectable(replayFiles[i].c_str(), isSelected))
                                {
                                    selectedFileIdx = i;
                                    replayFilename = replayFiles[i];
                                }
                                if (isSelected)
                                {
                                    ImGui::SetItemDefaultFocus();
                                }
                            }
                            ImGui::EndCombo();
                        }
                        ImGui::SameLine();

                        // 1. 削除ボタンの見た目を少し赤っぽくして警告感を出す（任意）
                        ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor::HSV(0.0f, 0.6f, 0.6f));
                        if (ImGui::Button("Delete", ImVec2(80 * scale, 0)))
                        {
                            if (selectedFileIdx >= 0 && selectedFileIdx < (int)replayFiles.size())
                            {
                                // ポップアップを開くフラグを立てる
                                ImGui::SetWindowFontScale(scale);
                                ImGui::OpenPopup("Delete Confirmation");
                            }
                        }
                        ImGui::PopStyleColor();

                        // 2. ポップアップの中身
                        if (ImGui::BeginPopupModal("Delete Confirmation", NULL, ImGuiWindowFlags_AlwaysAutoResize))
                        {
                            ImGui::SetWindowFontScale(scale);
                            ImGui::Text("Are you sure you want to delete this replay?\nThis cannot be undone!\n\n");
                            ImGui::Separator();

                            if (ImGui::Button("OK", ImVec2(120 * scale, 0)))
                            {
                                // ここで物理削除とリスト更新
                                if (std::remove(replayFiles[selectedFileIdx].c_str()) == 0)
                                {
                                    replayFiles.erase(replayFiles.begin() + selectedFileIdx);
                                    selectedFileIdx = -1;
                                    replayFilename = "";
                                }
                                ImGui::CloseCurrentPopup();
                            }

                            ImGui::SameLine();

                            if (ImGui::Button("Cancel", ImVec2(120 * scale, 0)))
                            {
                                ImGui::CloseCurrentPopup();
                            }

                            ImGui::EndPopup();
                        }

                        ImGui::Spacing();

                        // 3. アクションボタン
                        if (ImGui::Button("Record New", ImVec2(120 * scale, 0)))
                        {
                            std::string currentRomName = "";

                            // 1. gamesマップを回して、selectedGameIndex 番目の名前を特定する
                            int idx = 0;
                            // ここで pair を宣言してループを回すぜ
                            for (auto const &p : games)
                            {
                                if (idx == selectedGameIndex)
                                {
                                    currentRomName = p.second.name; // ここでROM名をゲット！
                                    break;
                                }
                                idx++;
                            }

                            if (!currentRomName.empty())
                            {
                                // 2. 時刻を取得 (YYYYMMDDhhmmss)
                                time_t now = time(nullptr);
                                struct tm *tm_now = localtime(&now);
                                char timeStr[20];
                                strftime(timeStr, sizeof(timeStr), "%Y%m%d%H%M%S", tm_now);

                                // 3. ファイル名生成
                                std::string replayFolder = "Replays/";
                                std::string newReplayFile = replayFolder + currentRomName + "@" + timeStr + ".rec";

                                CreateDirectoryA(replayFolder.c_str(), NULL);

                                // 4. 空ファイル作成
                                std::ofstream ofs(newReplayFile);
                                ofs.close();

                                // 5. プロセス起動処理へ（CreateProcessAなど）
                                char szExePath[MAX_PATH];
                                GetModuleFileNameA(NULL, szExePath, MAX_PATH);

                                std::string cmd = "\"" + std::string(szExePath) + "\"";
                                cmd += " -record \"" + newReplayFile + "\"";
                                cmd += " \"roms/" + currentRomName + ".zip\"";

                                STARTUPINFOA si = {sizeof(si)};
                                PROCESS_INFORMATION pi;
                                if (CreateProcessA(NULL, (LPSTR)cmd.c_str(), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
                                {
                                    CloseHandle(pi.hProcess);
                                    CloseHandle(pi.hThread);
                                    exitLaunch = false;
                                    exit = true;
                                    saveSettings = true;
                                }
                            }
                        }
                        ImGui::SameLine();

                        // ファイルが選択されている時だけPlayボタンを有効にする
                        bool hasSelection = (selectedFileIdx >= 0 && selectedFileIdx < replayFiles.size());
                        if (!hasSelection)
                            ImGui::BeginDisabled();

                        if (ImGui::Button("Play Selected", ImVec2(120 * scale, 0)))
                        {
                            if (selectedFileIdx >= 0 && !replayFilename.empty())
                            {
                                char szExePath[MAX_PATH];
                                GetModuleFileNameA(NULL, szExePath, MAX_PATH);

                                // 1. replayFilename には既に "Replays/rom@time.rec" が入っている
                                std::string fullPath = replayFilename;

                                // 2. ROM名を取り出すときは、"Replays/" の後ろから "@" の前までを抜く
                                // まず最後の '/' を探して、ファイル名部分だけにする
                                size_t lastSlash = fullPath.find_last_of("/\\");
                                std::string pureFilename = (lastSlash != std::string::npos) ? fullPath.substr(lastSlash + 1) : fullPath;

                                // その後、今までのやり方で "@" を探す
                                size_t pos = pureFilename.find('@');
                                std::string romName = (pos != std::string::npos) ? pureFilename.substr(0, pos) : "";

                                if (!romName.empty())
                                {
                                    // 3. コマンドライン組み立て
                                    // fullPath が "Replays/xxx.rec" なので、そのままぶち込む！
                                    std::string cmd = "\"" + std::string(szExePath) + "\"";
                                    cmd += " -play \"" + fullPath + "\"";
                                    cmd += " \"roms/" + romName + ".zip\"";

                                    // 4. プロセス起動
                                    STARTUPINFOA si = {sizeof(si)};
                                    PROCESS_INFORMATION pi;
                                    if (CreateProcessA(NULL, (LPSTR)cmd.c_str(), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
                                    {
                                        CloseHandle(pi.hProcess);
                                        CloseHandle(pi.hThread);
                                        exitLaunch = false;
                                        exit = true;
                                        saveSettings = true;
                                    }
                                }
                            }
                        }

                        if (!hasSelection)
                            ImGui::EndDisabled();

                        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Note: Replay files must be located in the 'Replays' folder.");
                    }
                    ImGui::EndChild();

                    ImGui::Spacing();
                    ImGui::Spacing();

                    // --- System Settings Section ---
                    ImGui::Text("System");
                    ImGui::Separator();
                    ImGui::Spacing();
                    ImGui::SetWindowFontScale(scale);
                    ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor::HSV(0.0f, 0.6f, 0.6f));
                    if (ImGui::Button("Reset All Settings", ImVec2(240 * scale, 0)))
                    {
                        // まずは「本当にいいんだな？」と聞くためのポップアップを開く
                        ImGui::OpenPopup("Reset Confirmation");
                    }
                    ImGui::PopStyleColor();
                    // モーダルポップアップの設定
                    if (ImGui::BeginPopupModal("Reset Confirmation", NULL, ImGuiWindowFlags_AlwaysAutoResize))
                    {
                        ImGui::SetWindowFontScale(scale);
                        ImGui::Text("This will delete your 'Config/Supermodel.ini' and close the app.\n"
                                    "All your preferences will be lost. Are you sure?\n\n");
                        ImGui::Separator();

                        // 「はい」に相当するボタン
                        if (ImGui::Button("YES, Reset Everything", ImVec2(180 * scale, 0)))
                        {
                            // 物理ファイルの削除（Config/Supermodel.ini）
                            // 戻り値を気にしすぎず、とりあえず削除を試みるぜ
                            std::remove("Config/Supermodel.ini");

                            // 実行中の設定を保存せずに終了（これで初期化される）
                            exitLaunch = false;
                            exit = true;
                            saveSettings = false;

                            ImGui::CloseCurrentPopup();
                        }

                        ImGui::SameLine();

                        // 「いいえ」に相当するボタン
                        if (ImGui::Button("Cancel", ImVec2(100 * scale, 0)))
                        {
                            ImGui::CloseCurrentPopup();
                        }

                        ImGui::EndPopup();
                    }
                    ImGui::TextDisabled("Warning: This will delete Supermodel.ini and close the app.");
                    // SettingsタブやAboutタブの最後に配置

                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("About"))
                {
                    ImGui::SetWindowFontScale(scale);
                    float windowWidth = ImGui::GetContentRegionAvail().x;

                    // 上部に少し余裕を持たせる
                    ImGui::Dummy(ImVec2(0, 20.0f * scale));

                    // --- 1. タイトルをセンター揃え ---
                    const char *title = "Supermodel-PonMi-Edition";
                    float titleWidth = ImGui::CalcTextSize(title).x;
                    ImGui::SetCursorPosX((windowWidth - titleWidth) * 0.5f);
                    ImGui::Text(title);
                
                    // --- 2. バージョンをセンター揃え ---
                    const char *ver = "ver. 2026.02.06";
                    float verWidth = ImGui::CalcTextSize(ver).x;
                    ImGui::SetCursorPosX((windowWidth - verWidth) * 0.5f);
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), ver);

                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();

                    // --- 3. メッセージをセンター揃え ---
                    const char *credit = "Developed by BackPonBeauty";
                    float creditWidth = ImGui::CalcTextSize(credit).x;
                    ImGui::SetCursorPosX((windowWidth - creditWidth) * 0.5f);
                    ImGui::Text(credit);

                    // --- 4. ボタンをセンターに置きたい場合 ---
                    float buttonWidth = 240.0f * scale;
                    ImGui::SetCursorPosX((windowWidth - buttonWidth) * 0.5f);
                    if (ImGui::Button("Visit GitHub Repository", ImVec2(buttonWidth, 40 * scale)))
                    {
                        ShellExecuteA(NULL, "open", "https://github.com/BackPonBeauty", NULL, NULL, SW_SHOWNORMAL);
                    }
                    // --- Aboutタブ内の支援セクション ---
                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();

                    // 1. 利用可能な横幅を取得
                    float availWidth = ImGui::GetContentRegionAvail().x;

                    // 2. ラベルテキストをセンター揃え
                    const char *supportLabel = "Support the Developer:";
                    float labelSize = ImGui::CalcTextSize(supportLabel).x;
                    ImGui::SetCursorPosX((availWidth - labelSize) * 0.5f);
                    ImGui::Text(supportLabel);

                    // 3. 説明文（TextWrapped）をセンターっぽく見せるためのダミー余白
                    // TextWrappedは左詰め固定なので、ChildWindowを使って中央に寄せるのが一番綺麗だぜ
                    float wrapWidth = 400.0f * scale; // 説明文の最大幅を指定
                    if (availWidth > wrapWidth)
                    {
                        ImGui::SetCursorPosX((availWidth - wrapWidth) * 0.5f);
                    }

                    ImGui::Spacing();

                    // 4. GitHubボタンをセンター揃え
                    float btnWidth = 240.0f * scale;
                    ImGui::SetCursorPosX((availWidth - btnWidth) * 0.5f);

                    ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor(36, 41, 46, 255)); // GitHub Black
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (ImVec4)ImColor(50, 55, 60, 255));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, (ImVec4)ImColor(20, 25, 30, 255));
                    ImGui::PushStyleColor(ImGuiCol_Text, (ImVec4)ImColor(255, 255, 255, 255));

                    if (ImGui::Button("Sponsor on GitHub", ImVec2(btnWidth, 40 * scale)))
                    {
                        ShellExecuteA(NULL, "open", "https://github.com/sponsors/BackPonBeauty", NULL, NULL, SW_SHOWNORMAL);
                    }

                    ImGui::PopStyleColor(4);

                    // ツールチップ
                    if (ImGui::IsItemHovered())
                    {
                        // ツールチップ自体の描画を開始
                        ImGui::BeginTooltip();

                        // ツールチップ内のフォントサイズをスケールに合わせて調整
                        ImGui::SetWindowFontScale(scale);

                        // メッセージを表示
                        ImGui::Text("Fuel my development with some coffee!");

                        // ツールチップの描画を終了
                        ImGui::EndTooltip();
                    }
                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();
                    // 1. セクションタイトル
                    const char *thanksTitle = "[ Special Thanks ]";
                    float thanksTitleWidth = ImGui::CalcTextSize(thanksTitle).x;
                    ImGui::SetCursorPosX((availWidth - thanksTitleWidth) * 0.5f);
                    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f), thanksTitle);

                                // 2. 感謝のリスト（構造体や配列で回すと綺麗だぜ）
                    struct Gratitude
                    {
                        const char *category;
                        const char *name;
                    };
                    Gratitude thanksList[] = {
                        {"Official Supermodel3 Team", "Bart Trzynadlowski, Nik Henson"},
                        {"All Supermodel Developers", "and Fellow Fork Developers"},
                        {"Most Test Played", "@mygirl"},
                        {"Barrel Effect Inspired by", "@ikarugaginkei5744"},
                        // 香港のSpikeout同好会の皆さん
                        {"Network Tester", "Spikeout Community in Hong Kong"},
                        {"Special Shout-out to", "all the 'Anti-PonMi' folks"}};

                    for (const auto &item : thanksList)
                    {
                        // カテゴリ（少し小さめ or 色変え）
                        ImGui::SetWindowFontScale(scale);
                        float catWidth = ImGui::CalcTextSize(item.category).x;
                        ImGui::SetCursorPosX((availWidth - catWidth) * 0.5f);
                        //ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), item.category);
                        ImGui::Text(item.category);

                        // 名前（通常サイズ）
                        ImGui::SetWindowFontScale(scale);
                        float nameWidth = ImGui::CalcTextSize(item.name).x;
                        ImGui::SetCursorPosX((availWidth - nameWidth) * 0.5f);
                        ImGui::Text(item.name);

                        ImGui::Spacing();
                    }

                    ImGui::Dummy(ImVec2(0, 10.0f * scale));

                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
        }
        ImGui::EndChild();
    }
    ImGui::EndGroup();

    // --- 下段: 操作パネル ---
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 5.0f * scale));
    if (ImGui::BeginChild("FooterBar", ImVec2(0, 0), false))
    {
        ImGui::SetWindowFontScale(scale); // スケール再適用

        float btnWidth = 220.0f * scale;
        float btnHeight = -1.0f; // 高さいっぱいに広げる
        float spacing = 10.0f * scale;

        if (ImGui::Button("LAUNCH", ImVec2(btnWidth, btnHeight)))
        {
            if (CheckRomExists(selectedGameIndex, games, config))
            {
                exitLaunch = true;
                exit = true;
                saveSettings = true;
            }
            else
            {
                printf("[LAUNCH] ROM not found, launch canceled\n");
            }
        }
        ImGui::SameLine(0, spacing);

        // --- 追加：ROMパス設定エリア ---
        // 2. フォルダ選択ボタン
        if (ImGui::Button("DIR...", ImVec2(80.0f * scale, -1)))
        {
            std::string pickedPath = "";

#ifdef _WIN32
            // --- Windows用処理 ---
            BROWSEINFO bi = {0};
            bi.lpszTitle = "Select ROM Directory";
            bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
            LPITEMIDLIST pidl = SHBrowseForFolder(&bi);
            if (pidl != nullptr)
            {
                char path[MAX_PATH];
                if (SHGetPathFromIDList(pidl, path))
                    pickedPath = path;
                CoTaskMemFree(pidl);
            }
            /*
    #else

            // --- Linux用処理 (zenityを使用) ---
            // zenityはほとんどのLinux配布版で標準搭載されているダイアログツールです
            FILE* f = popen("zenity --file-selection --directory --title=\"Select ROM Directory\"", "r");
            if (f) {
                char buffer[1024];
                if (fgets(buffer, sizeof(buffer), f)) {
                    pickedPath = buffer;
                    // 末尾の改行コードを削除
                    pickedPath.erase(pickedPath.find_last_not_of("\n\r") + 1);
                }
                pclose(f);
            }
            */
#endif

            if (!pickedPath.empty())
            {
                // 文字化け・区切り文字対策：すべて '/' に変換
                for (auto &c : pickedPath)
                {
                    if (c == '\\')
                        c = '/';
                }
                config.Set("Dir", pickedPath);
            }
        }

        ImGui::SameLine(0, spacing);

        std::string Dir = config["Dir"].ValueAs<std::string>();
        // std::string Dir = config["Dir"].ValueAs<std::string>();
        // std::string Dir = config.Get("Supermodel3UI").Get("Dir").ValueAs<std::string>();
        std::replace(Dir.begin(), Dir.end(), '\\', '/');

        // ImGuiで編集するための作業用バッファ
        char pathBuf[512];
        strncpy(pathBuf, Dir.c_str(), sizeof(pathBuf));
        pathBuf[sizeof(pathBuf) - 1] = '\0';

        // テキストボックスの横幅を、残りのスペースに合わせて自動計算
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - btnWidth - spacing - 10.0f);

        // 第2引数には std::string ではなく char配列(pathBuf) を渡す
        if (ImGui::InputText("##RomPathStr", pathBuf, sizeof(pathBuf)))
        {
            std::string newPath = std::string(pathBuf);
            // ユーザーが文字を入力した瞬間に config の "Dir" を更新
            std::replace(newPath.begin(), newPath.end(), '\\', '/');
            config.Set("Dir", std::string(pathBuf));
        }
        // ----------------------------

        ImGui::SameLine(ImGui::GetWindowWidth() - btnWidth - 10.0f);

        if (ImGui::Button("EXIT", ImVec2(btnWidth, btnHeight)))
        {
            exitLaunch = false;
            exit = true;
            saveSettings = true;
        }
    }
    ImGui::EndChild();

    // GUIのメインループ（毎フレーム通る場所）に記述
    if (g_PreviewWindow)
    {
        // プレビューウィンドウのIDを取得
        uint32_t previewID = SDL_GetWindowID(g_PreviewWindow);
        // 現在フォーカスされているウィンドウを取得
        SDL_Window *focusWin = SDL_GetMouseFocus();

        // マウスの左クリック状態を取得
        if (SDL_GetMouseState(NULL, NULL) & SDL_BUTTON(SDL_BUTTON_LEFT))
        {
            // もしマウスがプレビューウィンドウの上にある、
            // もしくは「どのウィンドウでもいいからクリックされたら消す」なら判定を緩くする
            if (focusWin == g_PreviewWindow)
            {
                ClosePreviewWindow();
                // SDL_DestroyWindow(g_PreviewWindow);
                // g_PreviewWindow = nullptr;
            }
        }
    }

    const uint8_t *state = SDL_GetKeyboardState(NULL);

    if (state[SDL_SCANCODE_ESCAPE])
    {

        exitLaunch = false;

        exit = true;

        saveSettings = true;
    }

    ImGui::End();
    // ★追加：設定した色を元に戻す（Pushした数だけPopする）
    ImGui::PopStyleColor(3);
}

// --- エントリポイント ---
std::vector<std::string> RunGUI(const std::string &configPath, Util::Config::Node &config)
{
    if (!resLoaded)
    {
        std::ifstream file("Resolution.txt");
        std::string line;
        if (file.is_open())
        {
            while (std::getline(file, line))
            {
                if (!line.empty())
                {
                    resolutions.push_back(line);
                }
            }
            file.close();
        }
        else
        {
            // ファイルがない場合のフォールバック
            resolutions.push_back("640x480");
            resolutions.push_back("1920x1080");
        }
        resLoaded = true;
    }
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) < 0)
        return {};

    // --- [1] Supermodel.ini からの読み込み (ループ前) ---

    // Graphics
    int engineSelection = config["New3DEngine"].ValueAs<bool>() ? 0 : 1;
    bool vVsync = config["VSync"].ValueAs<bool>();
    bool vQuadRendering = config["QuadRendering"].ValueAs<bool>();
    bool vGPUMultiThreaded = config["GPUMultiThreaded"].ValueAs<bool>();
    bool vMultiThreaded = config["MultiThreaded"].ValueAs<bool>();
    bool vMultiTexture = config["MultiTexture"].ValueAs<bool>();
    bool vBorderless = config["BorderlessWindow"].ValueAs<bool>();
    bool vTrueAR = config["true-ar"].ValueAs<bool>();
    bool vFullScreen = config["FullScreen"].ValueAs<bool>();
    bool vWideScreen = config["WideScreen"].ValueAs<bool>();
    bool vWideBackground = config["WideBackground"].ValueAs<bool>();
    bool vStretch = config["Stretch"].ValueAs<bool>();
    bool vShowFrameRate = config["ShowFrameRate"].ValueAs<bool>();
    bool vThrottle = config["Throttle"].ValueAs<bool>();
    bool vNoWhiteFlash = config["NoWhiteFlash"].ValueAs<bool>();
    float vRefreshRate = config["RefreshRate"].ValueAs<float>();
    bool vTrueHz;
    if (vRefreshRate == 60.0f)
    {
        vTrueHz = false;
    }
    else
    {
        vTrueHz = true;
    }

    int superSampling = (int)config["Supersampling"].ValueAs<int64_t>();
    int selectedCRT = (int)config["CRTcolors"].ValueAs<int64_t>();
    int selectedUpscale = (int)config["UpscaleMode"].ValueAs<int64_t>();
    int ppcFreq = (int)config["PowerPCFrequency"].ValueAs<int64_t>();
    int WindowXPosition = (int)config["WindowXPosition"].ValueAs<int64_t>();
    int WindowYPosition = (int)config["WindowYPosition"].ValueAs<int64_t>();
    int Scanline = (int)config["ScanlineStrength"].ValueAs<int64_t>();
    int Barrel = (int)config["BarrelStrength"].ValueAs<int64_t>();

    // Sound
    int musicVol = (int)config["MusicVolume"].ValueAs<int64_t>();
    int sfxVol = (int)config["SoundVolume"].ValueAs<int64_t>();
    int balance = (int)config["Balance"].ValueAs<int64_t>();
    bool vEmulateSound = config["EmulateSound"].ValueAs<bool>();
    bool vEmulateDSB = config["EmulateDSB"].ValueAs<bool>();
    bool vFlipStereo = config["FlipStereo"].ValueAs<bool>();
    bool vLegacySoundDSP = config["LegacySoundDSP"].ValueAs<bool>();

    // Control (文字列比較でインデックスを決定)
    std::string inSys = config["InputSystem"].ValueAs<std::string>();
    int selectedInputType = (inSys == "xinput") ? 0 : (inSys == "dinput") ? 1
                                                  : (inSys == "rawinput") ? 2
                                                                          : 2;

    int selectedCrosshair = (int)config["Crosshairs"].ValueAs<int64_t>();

    std::string styleStr = config["CrosshairStyle"].ValueAs<std::string>();
    int selectedStyle = (styleStr == "vector") ? 0 : 1;
    bool vForceFeedback = config["ForceFeedback"].ValueAs<bool>();

    // Network
    bool vNetwork = config["Network"].ValueAs<bool>();
    bool vSimulateNet = config["SimulateNet"].ValueAs<bool>();

    // Network 文字列（char配列へコピー）
    char bufPortIn[16], bufPortOut[16], bufAddressOut[128];
    strncpy(bufPortIn, config["PortIn"].ValueAs<std::string>().c_str(), sizeof(bufPortIn));
    strncpy(bufPortOut, config["PortOut"].ValueAs<std::string>().c_str(), sizeof(bufPortOut));
    strncpy(bufAddressOut, config["AddressOut"].ValueAs<std::string>().c_str(), sizeof(bufAddressOut));

    struct GuiSettings
    {
        int engineSelection;
        bool vVsync, vQuadRendering, vGPUMultiThreaded, vMultiThreaded;
        bool vMultiTexture, vBorderless, vTrueAR, vFullScreen, vWideScreen;
        bool vWideBackground, vStretch, vShowFrameRate, vThrottle, vNoWhiteFlash, vTrueHz;
        int superSampling, selectedCRT, selectedUpscale, ppcFreq;
        int WindowXPosition, WindowYPosition, Scanline, Barrel;
        int musicVol, sfxVol, balance;
        bool vEmulateSound, vEmulateDSB, vFlipStereo, vLegacySoundDSP;
        int selectedInputType, selectedCrosshair, selectedStyle;
        bool vForceFeedback, vNetwork, vSimulateNet;
        char bufPortIn[16], bufPortOut[16], bufAddressOut[128];
        int selectedResIndex; // これもここに入れる
    };

    SDL_Window *window = SDL_CreateWindow("Supermodel-PonMi-Edition", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1024, 600, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_HIDDEN);
    if (!window)
        return {};

    SDL_GLContext glContext = SDL_GL_CreateContext(window);
    glewExperimental = GL_TRUE;
    glewInit();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();

    ImFontConfig font_cfg;

    // --- ここから追加：SelectedGameIdx を自力で読み出す ---
    int selectedGame = -1;
    bool startMaximized = false;
    float fontSize =16.0f;
    {
        std::ifstream ifs("ponmi.ini");
        if (ifs.is_open())
        {
            std::string line;
            while (std::getline(ifs, line))
            {
                // スペースを考慮せず、単純に "キー=値" を探す
                size_t sep = line.find('=');
                if (sep == std::string::npos)
                    continue;

                std::string key = line.substr(0, sep);
                std::string val = line.substr(sep + 1);

                if (key == "SelectedGameIdx")
                {
                    selectedGame = std::stoi(val);
                }
                else if (key == "Maximized")
                {
                    startMaximized = (val == "1"); // boolは 0 か 1 で保存されているため
                }
                else if (key == "FontSize")
                {
                    fontSize = std::stof(val);
                }
            }
        }
        ifs.close();
    }


    font_cfg.FontDataOwnedByAtlas = false;
    io.Fonts->AddFontFromMemoryTTF((void *)Font01_data, sizeof(Font01_data), fontSize, &font_cfg);

    ImGui_ImplSDL2_InitForOpenGL(window, glContext);
    ImGui_ImplOpenGL3_Init("#version 150");


    
    uint32_t window_flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
    if (startMaximized)
    {
        SDL_MaximizeWindow(window);
    }

    // --- ここから追加：iniからサイズと位置を復元 ---
    ImGui::LoadIniSettingsFromDisk(io.IniFilename);

    // LauncherCanvas の ID を直接計算して設定を探す
    ImGuiID canvasID = ImHashStr("LauncherCanvas");
    ImGuiWindowSettings *settings = ImGui::FindWindowSettingsByID(canvasID);

    if (settings)
    {
        int savedW = (int)settings->Size.x;
        int savedH = (int)settings->Size.y;
        int savedX = (int)settings->Pos.x;
        int savedY = (int)settings->Pos.y;

        // サイズの適用
        if (savedW > 0 && savedH > 0)
        {
            SDL_SetWindowSize(window, savedW, savedH);
        }

        // 位置の適用（マルチモニターで見失わないよう、ある程度妥当な値かチェック）
        if (savedX != 0 || savedY != 0)
        {
            SDL_SetWindowPosition(window, savedX, savedY);
        }
        else
        {
            SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
        }
    }
    // --- ここまで ---

    GameLoader loader(config["GameXMLFile"].ValueAs<std::string>());
    auto &games = loader.GetGames();

    bool saveSettings = true, running = true, exit = false, exitLaunch = false;

    SDL_ShowWindow(window);

    int selectedResIndex = 0;
    // configから現在の解像度を "1280x720" のような形式の文字列にする
    std::string currentRes = std::to_string(config["XResolution"].ValueAs<int64_t>()) + "x" +
                             std::to_string(config["YResolution"].ValueAs<int64_t>());

    // resolutionsリストの中から一致するものを探す
    for (int i = 0; i < (int)resolutions.size(); i++)
    {
        if (resolutions[i] == currentRes)
        {
            selectedResIndex = i;
            break;
        }
    }

    while (running)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT)
            {
                running = false;
                exit = false;
                saveSettings = false;
            }
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        // GUI(io, config, games, selectedGame, exit, exitLaunch, saveSettings, window,selectedResIndex);
        GUI(io, config, games, selectedGame, exit, exitLaunch, saveSettings, window,
            selectedResIndex, engineSelection, vVsync, vQuadRendering, vGPUMultiThreaded,
            vMultiThreaded, vMultiTexture, vBorderless, vTrueAR, vFullScreen,
            vWideScreen, vWideBackground, vStretch, vShowFrameRate, vThrottle,
            vNoWhiteFlash, vTrueHz, superSampling, selectedCRT, selectedUpscale, ppcFreq, WindowXPosition, WindowYPosition, Scanline, Barrel,
            musicVol, sfxVol, balance, vEmulateSound, vEmulateDSB, vFlipStereo,
            vLegacySoundDSP, selectedInputType, selectedCrosshair, selectedStyle,
            vForceFeedback, vNetwork, vSimulateNet, bufPortIn, bufPortOut, bufAddressOut);
        if (exit)
        {
            running = false;
        }

        ImGui::Render();
        glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
        glClearColor(0.02f, 0.03f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // --- 終了後の判定 ---
    std::vector<std::string> roms;

    // LAUNCHボタン（exitLaunch）が押されている場合のみROMパスを取得して返り値にする
    if (exitLaunch)
    {
        // 通常のROMパスを取得（フォールバック用）
        std::string path = GetRomPath(selectedGame, games, config);
        if (!path.empty())
        {
            roms.emplace_back(path);
        }
    }

    // 後片付け（テクスチャの破棄を追加）
    if (g_GameTexture)
        glDeleteTextures(1, &g_GameTexture);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();

    std::string iniData;
    if (saveSettings)
    {
        std::map<std::string, std::string> u;

        // --- Video 設定 ---
        u["New3DEngine"] = (engineSelection == 0 ? "1" : "0");
        u["VSync"] = (vVsync ? "1" : "0");
        u["QuadRendering"] = (vQuadRendering ? "1" : "0");
        u["GPUMultiThreaded"] = (vGPUMultiThreaded ? "1" : "0");
        u["MultiThreaded"] = (vMultiThreaded ? "1" : "0");
        u["MultiTexture"] = (vMultiTexture ? "1" : "0");
        u["BorderlessWindow"] = (vBorderless ? "1" : "0");
        u["FullScreen"] = (vFullScreen ? "1" : "0");
        u["WideScreen"] = (vWideScreen ? "1" : "0");
        u["WideBackground"] = (vWideBackground ? "1" : "0");
        u["Stretch"] = (vStretch ? "1" : "0");
        u["ShowFrameRate"] = (vShowFrameRate ? "1" : "0");
        u["Throttle"] = (vThrottle ? "1" : "0");
        u["NoWhiteFlash"] = (vNoWhiteFlash ? "1" : "0");
        u["true-ar"] = (vTrueAR ? "1" : "0");

        // リフレッシュレート（精度指定が必要なため stringstream を使用）
        std::stringstream ss;
        ss << std::fixed << std::setprecision(6) << (vTrueHz ? 57.524158 : 60.0);
        u["RefreshRate"] = ss.str();

        // --- 数値系 ---
        // 1. 現在選択されている解像度文字列を取得 (例: "1920x1080")
        std::string resStr = resolutions[selectedResIndex];
        int resW = 0, resH = 0;

        // 2. 文字列を w と h に分解
        if (sscanf(resStr.c_str(), "%dx%d", &resW, &resH) == 2)
        {
            // 分解成功。これを保存用マップに入れる
            u["XResolution"] = std::to_string(resW);
            u["YResolution"] = std::to_string(resH);
        }
        else
        {
            // 万が一パースに失敗した時のバックアップ（元の変数をそのまま使う）
            u["XResolution"] = std::to_string(XResolution);
            u["YResolution"] = std::to_string(YResolution);
        }

        u["WindowXPosition"] = bufPosX; // char[] はそのまま代入可
        u["WindowYPosition"] = bufPosY;
        u["Supersampling"] = std::to_string(superSampling);
        u["CRTcolors"] = std::to_string(selectedCRT);
        u["UpscaleMode"] = std::to_string(selectedUpscale);
        u["PowerPCFrequency"] = std::to_string(ppcFreq);
        u["ScanlineStrength"] = std::to_string(Scanline);
        u["BarrelStrength"] = std::to_string(Barrel);

        // --- Sound 設定 ---
        u["MusicVolume"] = std::to_string(musicVol);
        u["SoundVolume"] = std::to_string(sfxVol);
        u["Balance"] = std::to_string(balance);
        u["EmulateSound"] = (vEmulateSound ? "1" : "0");
        u["EmulateDSB"] = (vEmulateDSB ? "1" : "0");
        u["FlipStereo"] = (vFlipStereo ? "1" : "0");
        u["LegacySoundDSP"] = (vLegacySoundDSP ? "1" : "0");

        // --- Control / Network ---
        u["InputSystem"] = (selectedInputType == 0) ? "xinput" : (selectedInputType == 1) ? "dinput"
                                                                                          : "rawinput";

        u["Crosshairs"] = std::to_string(selectedCrosshair);
        u["CrosshairStyle"] = (selectedStyle == 0) ? "vector" : "bmp";
        u["ForceFeedback"] = (vForceFeedback ? "1" : "0");
        u["Network"] = (vNetwork ? "1" : "0");
        u["SimulateNet"] = (vSimulateNet ? "1" : "0");
        u["PortIn"] = bufPortIn;
        u["PortOut"] = bufPortOut;
        u["AddressOut"] = bufAddressOut;

        // 最後に自作の更新関数を呼ぶ！
        SaveSupermodelConfig(configPath, u);
    }

    ImGui::DestroyContext();
    // 設定保存の判定
    // --- 終了後の判定内 ---
    if (saveSettings)
    {
        // ImGui標準設定は今まで通り imgui.ini へ（ImGuiが勝手にやります）
        // それとは別に自分用の ponmi.ini を作成
        std::ofstream ofs("ponmi.ini", std::ios::trunc);
        uint32_t flags = SDL_GetWindowFlags(window);
        bool isMaximized = (flags & SDL_WINDOW_MAXIMIZED);
        if (ofs.is_open())
        {
            ofs << "[Settings]\n";
            ofs << "SelectedGameIdx=" << selectedGame << "\n";
            ofs << "Maximized=" << isMaximized << "\n";
            ofs << "FontSize=" << fontSize << "\n";
            // 今後、他にも保存したいものがあればここに追記できる
            ofs.close();
            saveSettings = false;
        }
    }

    SDL_GL_DeleteContext(glContext);

    SDL_DestroyWindow(window);
    SDL_Quit();

    return roms;
}