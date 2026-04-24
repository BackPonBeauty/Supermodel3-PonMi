#include "SuperAA.h"
#include <string>
#include <algorithm> // for std::max
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

SuperAA::SuperAA(int aaValue, CRTcolor CRTcolors, bool scanLine, int scanlineStrength, int totalXRes, int totalYRes, int ubarrelStrength, const char *gameTitle, bool wideScreen, bool overlay, const char *configFilePath) : m_aa(aaValue),
                                                                                                                                                                                                                              m_crtcolors(CRTcolors),
                                                                                                                                                                                                                              m_scanlineEnable(scanLine),
                                                                                                                                                                                                                              m_scanlineStrength(scanlineStrength / 100.0f),
                                                                                                                                                                                                                              m_barrelEffectEnable(true),
                                                                                                                                                                                                                              m_barrelStrength(ubarrelStrength / 1000.0f),
                                                                                                                                                                                                                              m_totalXRes(totalXRes),
                                                                                                                                                                                                                              m_totalYRes(totalYRes),
                                                                                                                                                                                                                              m_vao(0),
                                                                                                                                                                                                                              m_width(0),
                                                                                                                                                                                                                              m_height(0),
                                                                                                                                                                                                                              m_wideScreen(wideScreen),
                                                                                                                                                                                                                              m_overlay(overlay),
                                                                                                                                                                                                                              m_configFilePath(configFilePath),
                                                                                                                                                                                                                              m_ringBufferIndex(0),
                                                                                                                                                                                                                              m_frameCounter(0),
                                                                                                                                                                                                                              m_mixEnabled(false),
                                                                                                                                                                                                                              m_locOldFrameTex1(-1),
                                                                                                                                                                                                                              // m_locOldFrameTex2(-1),
                                                                                                                                                                                                                              // m_locOldFrameTex3(-1),
                                                                                                                                                                                                                              m_MixStrength(0.8),
                                                                                                                                                                                                                              m_locMixEnabled(-1)

{
    // リングバッファの初期化（テクスチャ生成は Init() で行う）
    for (int i = 0; i < RING_BUFFER_SIZE; i++)
    {
        m_frameRingBuffer[i] = 0;
    }

    if ((m_aa > 1) || (m_crtcolors != CRTcolor::None))
    {
        // =========================
        // Vertex Shader
        // =========================
        static const char *overlayFS = R"glsl(
#version 410 core
in vec2 vTexCoord;
uniform sampler2D uOverlayTex;
out vec4 fragColor;
void main() {
    fragColor = texture(uOverlayTex, vTexCoord);
}
)glsl";

        static const char *vertexShader = R"glsl(
#version 410 core
	
out vec2 vTexCoord; // フラグメントシェーダーへ渡すUV

void main(void)
{
    const vec4 vertices[] = vec4[](
        vec4(-1.0, -1.0, 0.0, 1.0),
        vec4(-1.0,  1.0, 0.0, 1.0),
        vec4( 1.0, -1.0, 0.0, 1.0),
        vec4( 1.0,  1.0, 0.0, 1.0)
    );

    vec4 v = vertices[gl_VertexID % 4];
    gl_Position = v;
    
    // [-1, 1] を [0, 1] に変換してUV座標にする
    vTexCoord = (v.xy + 1.0) * 0.5;
}
)glsl";

        // =========================
        // Fragment Shader header
        // =========================
        static const std::string fragmentShaderVersion = R"glsl(
#version 410 core
)glsl";

        std::string aaString = "const int aa = ";
        aaString += std::to_string(m_aa);
        aaString += ";\n";

        std::string ccString = "#define CRTCOLORS ";
        ccString += std::to_string((int)m_crtcolors);
        ccString += "\n";

        // スキャンライン強度はUniformで制御するため、ここでのconst定義は削除

        std::string uhString = "const int uScreenHeight = ";
        uhString += std::to_string(m_totalYRes);
        uhString += ";\n";

        //  =========================
        //  Fragment Shader body
        //  =========================
        static const std::string fragmentShaderBody = R"glsl(
in vec2 vTexCoord;
uniform sampler2D tex1;
uniform float scanlineStrength;
uniform float barrelStrength;
uniform int barrelEffectEnable;
uniform int scanlineEnable;
uniform float uAspect;
uniform sampler2D uOldFrameTex1;      
//uniform sampler2D uOldFrameTex2;      
//uniform sampler2D uOldFrameTex3;      
uniform int uMixEnabled; 
uniform float mixStrength;            

out vec4 fragColor;

const float SCANLINE_COUNT = 480.0;

// ===== CRT gamma =====
#if (CRTCOLORS == 0)
const float cgamma = 1.0;
#elif (CRTCOLORS == 1)
const float cgamma = 2.5;
#elif (CRTCOLORS == 2)
const float cgamma = 2.25;
#elif (CRTCOLORS > 2)
const float cgamma = -1.0;
#endif

// ===== CRT color matrices =====
#if (CRTCOLORS == 0)
const mat3 colmatrix = mat3(1.0);
#elif (CRTCOLORS == 1)
const mat3 colmatrix = mat3(
    1.3272128334714093, -0.3802108879412366, -0.1003607696463202,
   -0.0241848600268417,  0.9544506228550125,  0.0807358585208939,
   -0.0239585810555497, -0.0409736005706461,  1.4076563728858242
);
#elif (CRTCOLORS == 2)
const mat3 colmatrix = mat3(
    0.9241392201737613, -0.0690701363506526, -0.0084279079392561,
    0.0231962420140721,  0.9729221778325590,  0.0148832015024337,
   -0.0054875731998806,  0.0098220960740544,  1.3383896683854550
);
#elif (CRTCOLORS == 3)
const mat3 colmatrix = mat3(
    0.7822490684754086,  0.0506168576073398,  0.0137752498011040,
    0.0147968947158740,  0.9741745313046067,  0.0220301953285839,
   -0.0013501205469208, -0.0044076726925543,  1.3484819844991036
);
#elif (CRTCOLORS == 4)
const mat3 colmatrix = mat3(
    0.9395420637732393,  0.0501813568598678,  0.0102765793668928,
    0.0177722231435608,  0.9657928624969044,  0.0164349143595347,
   -0.0016215999431855, -0.0043697496597356,  1.0059913496029214
);
#elif (CRTCOLORS == 5)
const mat3 colmatrix = mat3(
    1.0440432087628346, -0.0440432087628348, 0.0,
    0.0, 1.0000000000000002, 0.0,
    0.0117933782840052, 0.9882066217159947
);
#endif

// ===== sRGB helpers =====
float sRGB(float c)
{
    if (c <= 0.0031308)
        return 12.92 * c;
    else
        return 1.055 * pow(c, 1.0/2.4) - 0.055;
}

float invsRGB(float c)
{
    if (c <= 0.04045)
        return c / 12.92;
    else
        return pow((c + 0.055) / 1.055, 2.4);
}

void main()
{
    vec2 uv = vTexCoord;
	
    float aspect = 1.33;
    if (uAspect > 0.0) aspect = uAspect;

    // ===== 歪み計算 =====
    vec2 c = uv * 2.0 - 1.0;
    c.x *= aspect;

    if (barrelEffectEnable != 0)
    {
        float r2 = dot(c, c);
        c *= (1.0 + barrelStrength * 1.0 * r2);
    }

    c.x /= aspect;
    uv = (c + 1.0) * 0.5;

    // ===== 範囲外チェック =====
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        fragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    // ===== Fetch & 25-Frame Delay Interpolation =====
    vec3 color = texture(tex1, uv).rgb;
    
    // ★ フレーム遅延 mix
    if (uMixEnabled != 0)
    {
        vec3 oldColor1 = texture(uOldFrameTex1, uv).rgb;  
        color = mix(oldColor1, color, mixStrength);
        //vec3 oldColor2 = texture(uOldFrameTex2, uv).rgb;  
        //color = mix(oldColor2, color, 0.1);
        //vec3 oldColor3 = texture(uOldFrameTex3, uv).rgb;  
        //color = mix(oldColor3, color, 0.1);
    }

	
    // ===== 色補正処理 =====
    color = pow(color, vec3(cgamma));
    color *= colmatrix;
    color = vec3(sRGB(color.r), sRGB(color.g), sRGB(color.b));
	
    // ===== Scanline =====
    if (scanlineEnable != 0)
    {
        float distortedY = uv.y * float(uScreenHeight);
        float pixelsPerLine = max(float(uScreenHeight) / 480.0, 1.0);
        float lineIndex = floor(distortedY / pixelsPerLine);
        
        float mask = mod(lineIndex, 2.0);
        
        color *= mix(1.0 - scanlineStrength, 1.0, mask);
    }

    fragColor = vec4(color, 1.0);
}
)glsl";

        std::string fs = fragmentShaderVersion + aaString + ccString + uhString + fragmentShaderBody;
        // ★ デバッグ：シェーダソースに uOldFrameTex が含まれているか確認
        if (fs.find("uOldFrameTex") != std::string::npos)
        {
            printf("[DEBUG] uOldFrameTex found in shader source\n");
        }
        else
        {
            printf("[ERROR] uOldFrameTex NOT found in shader source!\n");
        }
        // =========================
        // Shader load
        // =========================
        m_shader.LoadShaders(vertexShader, fs.c_str());

        // GetUniformLocation() を直接使用してメンバ変数に保存
        m_locScanlineEnable = m_shader.GetUniformLocation("scanlineEnable");
        m_locScanlineStrength = m_shader.GetUniformLocation("scanlineStrength");
        m_locBarrelEffectEnable = m_shader.GetUniformLocation("barrelEffectEnable");
        m_locBarrelStrength = m_shader.GetUniformLocation("barrelStrength");
        m_locOldFrameTex1 = m_shader.GetUniformLocation("uOldFrameTex1");
        m_locMixEnabled = m_shader.GetUniformLocation("uMixEnabled");
        m_locMixStrength = m_shader.GetUniformLocation("mixStrength");
        m_locTex1 = m_shader.GetUniformLocation("tex1");
        m_locUAspect = m_shader.GetUniformLocation("uAspect");
        /*
                if ((m_aa > 1) || (m_crtcolors != CRTcolor::None))
                {

                    // m_locOldFrameTex2 = m_shader.GetUniformLocation("uOldFrameTex2");
                    // m_locOldFrameTex3 = m_shader.GetUniformLocation("uOldFrameTex3");


                    printf("[SuperAA] Frame Delay Interpolation:\n");
                    printf("  uOldFrameTex1: %d\n", m_locOldFrameTex1);
                    // printf("  uOldFrameTex2: %d\n", m_locOldFrameTex2);
                    // printf("  uOldFrameTex3: %d\n", m_locOldFrameTex3);
                    printf("  uMixEnabled: %d\n", m_locMixEnabled);
                }

                printf("[SuperAA] Uniform locations:\n");
                printf("  tex1: %d\n", m_locTex1);
                printf("  scanlineStrength: %d\n", m_locScanlineStrength);
                printf("  barrelStrength: %d\n", m_locBarrelStrength);
                printf("  barrelEffectEnable: %d\n", m_locBarrelEffectEnable);
                printf("  scanlineEnable: %d\n", m_locScanlineEnable);
                printf("  uAspect: %d\n", m_locUAspect);*/
        m_overlayShader.LoadShaders(vertexShader, overlayFS);
        m_overlayShader.GetUniformLocationMap("uOverlayTex");

        // VAO生成
        glGenVertexArrays(1, &m_vao);

        if (gameTitle != nullptr)
        {
            LoadOverlayByTitle(gameTitle);
        }
    }
}
// =========================
// Destructor
// =========================
SuperAA::~SuperAA()
{
    m_shader.UnloadShaders();
    m_fbo.Destroy();

    if (m_vao)
    {
        glDeleteVertexArrays(1, &m_vao);
        m_vao = 0;
    }
}

// =========================
// Init
// =========================
void SuperAA::Init(int width, int height)
{
    if ((m_aa > 1) || (m_crtcolors != CRTcolor::None))
    {

        // 0以下のサイズを防ぐ
        if (width <= 0 || height <= 0)
            return;
        m_fbo.Destroy();
        m_fbo.Create(width * m_aa, height * m_aa);
        m_fbo2.Create(width * m_aa, height * m_aa);
        m_width = width;
        m_height = height;

        // InitFrameRingBuffer(width, height);
    }
}
// m_fbo.Destroy();

// =========================
// Draw
// =========================
void SuperAA::Draw()
{

    // ★ 初回初期化（リングバッファ）
    static bool ringBufferInitialized = false;
    if (!ringBufferInitialized && m_width > 0 && m_height > 0)
    {
        InitFrameRingBuffer(m_width, m_height);
        ringBufferInitialized = true;
    }

    // --- 1. ポストエフェクト（AA/CRT）の描画 ---
    if ((m_aa > 1) || (m_crtcolors != CRTcolor::None))
    {
        if (m_width > 0 && m_height > 0)
        {
            m_shader.EnableShader();

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, m_fbo.GetTextureID());

            if (m_locTex1 >= 0)
                glUniform1i(m_locTex1, 0);

            if (m_locScanlineEnable >= 0)
            {
                glUniform1i(m_locScanlineEnable, m_scanlineEnable ? 1 : 0);
            }

            if (m_locScanlineStrength >= 0)
            {
                glUniform1f(m_locScanlineStrength, m_scanlineStrength);
            }

            // barrelEffect 関連を送信
            if (m_locBarrelEffectEnable >= 0)
                glUniform1i(m_locBarrelEffectEnable, m_barrelEffectEnable ? 1 : 0);

            // barrelStrength を送信（動的な値を反映）
            if (m_locBarrelStrength >= 0)
                glUniform1f(m_locBarrelStrength, m_barrelStrength);
            
            if (m_locMixStrength >= 0)
                glUniform1f(m_locMixStrength, m_MixStrength);
            

            if (m_mixEnabled)
            {

                // 1フレーム前
                int oldFrameIndex1 = (m_ringBufferIndex - 1 + RING_BUFFER_SIZE) % RING_BUFFER_SIZE;
                // 2フレーム前
                // int oldFrameIndex2 = (m_ringBufferIndex - 2 + RING_BUFFER_SIZE) % RING_BUFFER_SIZE;
                // 3フレーム前
                // int oldFrameIndex3 = (m_ringBufferIndex - 3 + RING_BUFFER_SIZE) % RING_BUFFER_SIZE;

                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, m_frameRingBuffer[oldFrameIndex1]);
                glUniform1i(m_locOldFrameTex1, 1);
                glActiveTexture(GL_TEXTURE0);

                glUniform1i(m_locMixEnabled, 1);
            }
            else
            {
                glUniform1i(m_locMixEnabled, 0);
            }

            // uAspect も送信
            if (m_locUAspect >= 0)
                glUniform1f(m_locUAspect, (float)m_width / (float)m_height);

            glBindVertexArray(m_vao);
            glViewport(0, 0, m_width, m_height);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            glBindVertexArray(0);
            m_shader.DisableShader();
        }
    }

    if (m_overlayTex != 0 && m_wideScreen && m_overlay)
    {

        GLint last_viewport[4];
        glGetIntegerv(GL_VIEWPORT, last_viewport);

        glViewport(0, 0, m_totalXRes, m_totalYRes);

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_DEPTH_TEST);

        m_overlayShader.EnableShader();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_overlayTex);

        if (m_overlayShader.uniformLocMap["uOverlayTex"] >= 0)
        {
            glUniform1i(m_overlayShader.uniformLocMap["uOverlayTex"], 0);
        }

        glBindVertexArray(m_vao);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        m_overlayShader.DisableShader();
        glDisable(GL_BLEND);

        glViewport(last_viewport[0], last_viewport[1], last_viewport[2], last_viewport[3]);
    }

    glBindVertexArray(0);

    // ★ 最後に古いフレームバッファを更新
    UpdateFrameRingBuffer();
}

void SuperAA::ToggleScanline()
{
    m_scanlineEnable = !m_scanlineEnable;
    printf("[SuperAA] Scanline %s\n", m_scanlineEnable ? "ON" : "OFF");
}

void SuperAA::ToggleBarrelEffect()
{
    m_barrelEffectEnable = !m_barrelEffectEnable;
    printf("[SuperAA] Barrel Effect %s\n", m_barrelEffectEnable ? "ON" : "OFF");
}
void SuperAA::ToggleMixEffect()
{
    m_mixEnabled = !m_mixEnabled;
    printf("[SuperAA] Mix Effect %s\n", m_mixEnabled ? "ON" : "OFF");
}
void SuperAA::ToggleOverlay()
{
    m_overlay = !m_overlay;
    printf("[SuperAA] Overlay %s\n", m_overlay ? "ON" : "OFF");
}

void SuperAA::IncreaseScanlineStrength()
{
    const float STEP = 0.01f; // 5%ずつ変更
    m_scanlineStrength += STEP;
    if (m_scanlineStrength > 1.0f)
        m_scanlineStrength = 1.0f;
    printf("[SuperAA] Scanline Strength Increased: %.2f\n", m_scanlineStrength);
    SaveToINI();
}

void SuperAA::DecreaseScanlineStrength()
{
    const float STEP = 0.01f;
    m_scanlineStrength -= STEP;
    if (m_scanlineStrength < 0.0f)
        m_scanlineStrength = 0.0f;
    printf("[SuperAA] Scanline Strength Decreased: %.2f\n", m_scanlineStrength);
    SaveToINI();
}

void SuperAA::IncreaseBarrelStrength()
{
    const float STEP = 0.001f; // 0.1%ずつ変更
    m_barrelStrength += STEP;
    if (m_barrelStrength > 0.100f)
        m_barrelStrength = 0.100f;
    printf("[SuperAA] Barrel Strength Decrease: %.3f\n", m_barrelStrength);
    SaveToINI();
}

void SuperAA::DecreaseBarrelStrength()
{
    const float STEP = 0.001f;
    m_barrelStrength -= STEP;
    if (m_barrelStrength < 0.000f)
        m_barrelStrength = 0.000f;
    printf("[SuperAA] Barrel Strength Increase: %.3f\n", m_barrelStrength);
    SaveToINI();
}

void SuperAA::IncreaseMixStrength()
{
    const float STEP = 0.1f; // 0.1%ずつ変更
    m_MixStrength += STEP;
    if (m_MixStrength > 1.0f)
        m_MixStrength = 1.0f;
    printf("[SuperAA] Mix Strength Decrease: %.3f\n", m_MixStrength);
}

void SuperAA::DecreaseMixStrength()
{
    const float STEP = 0.1f;
    m_MixStrength -= STEP;
    if (m_MixStrength < 0.0f)
        m_MixStrength = 0.0f;
    printf("[SuperAA] Mix Strength Increase: %.3f\n", m_MixStrength);
}


// ============================================================================
// ★ 新規追加：ini ファイル即座保存関数
// ============================================================================
void SuperAA::SaveToINI()
{
    if (m_configFilePath.empty())
    {
        printf("[SuperAA] ERROR: ini file path not set\n");
        return;
    }

    // 現在の値を 0～100 の形式に変換
    // ScanlineStrength: 内部は (1 - value) なので逆算
    int scanlineValue = static_cast<int>(m_scanlineStrength * 100.0f);
    if (scanlineValue < 0)
        scanlineValue = 0;
    if (scanlineValue > 100)
        scanlineValue = 100;

    // BarrelStrength: 内部は value / 100.0f なので 100を掛ける
    int barrelValue = static_cast<int>(m_barrelStrength * 1000.0f);
    if (barrelValue < 0)
        barrelValue = 0;
    if (barrelValue > 100)
        barrelValue = 100;

    printf("[SuperAA] Saving to ini: ScanlineStrength=%d, BarrelStrength=%d\n",
           scanlineValue, barrelValue);

    // ini ファイルを読み込む
    std::ifstream infile(m_configFilePath);
    if (!infile.is_open())
    {
        printf("[SuperAA] ERROR: Cannot open ini file: %s\n", m_configFilePath.c_str());
        return;
    }

    std::stringstream buffer;
    std::string line;

    // ファイルの内容を処理
    while (std::getline(infile, line))
    {
        // ScanlineStrength を検索・置換
        if (line.find("ScanlineStrength") != std::string::npos && line.find('=') != std::string::npos)
        {
            buffer << "ScanlineStrength = " << scanlineValue << '\n';
        }
        // BarrelStrength を検索・置換
        else if (line.find("BarrelStrength") != std::string::npos && line.find('=') != std::string::npos)
        {
            buffer << "BarrelStrength = " << barrelValue << '\n';
        }
        else
        {
            buffer << line << '\n';
        }
    }
    infile.close();

    // ファイルに書き込む
    std::ofstream outfile(m_configFilePath);
    if (!outfile.is_open())
    {
        printf("[SuperAA] ERROR: Cannot write to ini file: %s\n", m_configFilePath.c_str());
        return;
    }

    outfile << buffer.str();
    outfile.close();

    printf("[SuperAA] ini file updated successfully\n");
}

GLuint SuperAA::GetTargetID()
{
    return m_fbo.GetFBOID();
}
GLuint LoadPNGTexture(const char *filename)
{
    int width, height, channels;
    // 上下反転が必要な場合は true に（OpenGLは左下が原点のため）
    stbi_set_flip_vertically_on_load(true);

    unsigned char *data = stbi_load(filename, &width, &height, &channels, 4); // 強制的にRGBAで読み込む

    if (!data)
    {
        // printf("Failed to load texture: %s\n", filename);
        return 0;
    }

    GLuint textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);

    // PNGテクスチャの設定（RGBAを指定）
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);

    // フィルタリング設定（オーバーレイなので綺麗に見えるよう線形補間）
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // 画面端の処理
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    stbi_image_free(data);
    return textureID;
}
// --- ヘルパー関数: メモリから読み込む ---
GLuint LoadTextureFromMemory(const unsigned char *data, int len)
{
    int width, height, channels;
    stbi_set_flip_vertically_on_load(true);
    unsigned char *pixels = stbi_load_from_memory(data, len, &width, &height, &channels, 4);

    if (!pixels)
        return 0;

    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    stbi_image_free(pixels);
    return tex;
}
void SuperAA::LoadOverlayByTitle(const std::string &gameTitle)
{
    // 1. タイトル文字列の整形 (空白をハイフンに)
    std::string processedTitle = gameTitle;
    std::replace(processedTitle.begin(), processedTitle.end(), ' ', '-');

    // 2. 古いテクスチャの破棄
    if (m_overlayTex != 0)
    {
        glDeleteTextures(1, &m_overlayTex);
        m_overlayTex = 0;
    }

    // 3. まずファイルを探す
    std::string path = "image/" + processedTitle + ".png";
    m_overlayTex = LoadPNGTexture(path.c_str());

    // 4. ファイルがなければ、埋め込み画像を読み込む
    if (m_overlayTex == 0)
    {
        printf("[SuperAA] Overlay file not found.\n");
        // m_overlayTex = LoadTextureFromMemory(warning_text_png, warning_text_png_len);
    }
    else
    {
        printf("[SuperAA] Loaded overlay: %s\n", path.c_str());
    }
}

void SuperAA::InitFrameRingBuffer(int width, int height)
{
    printf("[SuperAA] InitFrameRingBuffer: %d x %d\n", width, height);

    // 既存のテクスチャを削除
    for (int i = 0; i < RING_BUFFER_SIZE; i++)
    {
        if (m_frameRingBuffer[i] != 0)
        {
            glDeleteTextures(1, &m_frameRingBuffer[i]);
            m_frameRingBuffer[i] = 0;
        }
    }

    // 新しいテクスチャを生成
    for (int i = 0; i < RING_BUFFER_SIZE; i++)
    {
        glGenTextures(1, &m_frameRingBuffer[i]);
        glBindTexture(GL_TEXTURE_2D, m_frameRingBuffer[i]);

        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    glBindTexture(GL_TEXTURE_2D, 0);

    m_ringBufferIndex = 0;

    printf("[SuperAA] FrameRingBuffer initialized with %d textures\n", RING_BUFFER_SIZE);
}
void SuperAA::UpdateFrameRingBuffer()
{

    if (m_ringBufferIndex < 0 || m_ringBufferIndex >= RING_BUFFER_SIZE)
    {
        printf("[EARLY RETURN] m_ringBufferIndex=%d, RING_BUFFER_SIZE=%d\n", m_ringBufferIndex, RING_BUFFER_SIZE);
        return;
    }

    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0); // ★ 一度リセット
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_fbo.GetFBOID());

    glReadBuffer(GL_COLOR_ATTACHMENT0); // ★ 読み込み対象を明示的に指定

    glBindTexture(GL_TEXTURE_2D, m_frameRingBuffer[m_ringBufferIndex]);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, m_width, m_height);

    GLenum err = glGetError();
    if (err != GL_NO_ERROR)
    {
        printf("[ERROR] glCopyTexSubImage2D failed: 0x%x\n", err);
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);

    m_ringBufferIndex = (m_ringBufferIndex + 1) % RING_BUFFER_SIZE;
}
