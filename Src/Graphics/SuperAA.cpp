#include "SuperAA.h"
#include <string>
#include <algorithm> // for std::max

SuperAA::SuperAA(int aaValue, CRTcolor CRTcolors, float scanlineStrength, int totalYRes, float ubarrelStrength) :
    m_aa(aaValue),
    m_crtcolors(CRTcolors),
    m_scanlineEnable(true),
    m_scanlineStrength(scanlineStrength),
    m_barrelEffectEnable(true),
    m_barrelStrength(ubarrelStrength),
    m_totalYRes(totalYRes),
    m_vao(0),
    m_width(0),
    m_height(0)
{
	if ((m_aa > 1) || (m_crtcolors != CRTcolor::None)) {
    // =========================
    // Vertex Shader
    // =========================
    static const char* vertexShader = R"glsl(
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
	
	std::string scString = "const float SCANLINE_STRENGTH = "; 
	scString += std::to_string(m_scanlineStrength);
	scString += ";\n";
    
	std::string bsString = "const float BARREL_STRENGTH = "; 
	bsString += std::to_string(m_barrelStrength);
	bsString += ";\n";
    // =========================
    // Fragment Shader body
    // =========================
    static const std::string fragmentShaderBody = R"glsl(
in vec2 vTexCoord; // 頂点シェーダーから受け取る
uniform sampler2D tex1;
uniform int scanlineEnable;
uniform float scanlineStrength; // 定数ではなくUniformを使用
uniform int barrelEffectEnable;
uniform float uAspect;          // width / height
uniform float BarrelStrength;  // 0.0 = OFF
out vec4 fragColor;

const float SCANLINE_COUNT = 480.0; // 必要に応じて uScreenHeight / 2.0 などに

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
	
	//float strength = 0.01; // かなり強い歪み
    float aspect = 1.33;  // アスペクト比も仮固定 (4:3)
    if (uAspect > 0.0) aspect = uAspect; // もしC++から来ていれば使う

    // ===== 歪み計算 =====
    vec2 c = uv * 2.0 - 1.0; // [0,1] -> [-1,1]
    c.x *= aspect;           // アスペクト比考慮

    float r2 = dot(c, c);    // 中心からの距離の2乗
    c *= (1.0 + BARREL_STRENGTH * r2); // 歪ませる

    c.x /= aspect;           // アスペクト比戻す
    uv = (c + 1.0) * 0.5;    // [-1,1] -> [0,1]

    // ===== デバッグ：範囲外を赤くする =====
    // これで「赤い枠」が見えれば、歪み計算は成功しています。
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        fragColor = vec4(0.0, 0.0, 0.0, 1.0); // 赤
        return;
    }

    // ===== Fetch =====

    vec3 color = texture(tex1, uv).rgb;
	
	
    // (色補正処理...)
    color = pow(color, vec3(cgamma));
    color *= colmatrix;
    color = vec3(sRGB(color.r), sRGB(color.g), sRGB(color.b));
	
	
	    // ===== Scanline =====
   if (scanlineEnable != 0)
    {
        float distortedY = uv.y * float(uScreenHeight);
        float pixelsPerLine = max(float(uScreenHeight) / 480.0, 1.0);
        float lineIndex = floor(distortedY / pixelsPerLine);
        
        // 偶数行か奇数行か (0.0 or 1.0)
        float mask = mod(lineIndex, 2.0);
        
        color *= mix(SCANLINE_STRENGTH, 1.0, mask);
    }
	
	




    fragColor = vec4(color, 1.0);
}
)glsl";

    std::string fs = fragmentShaderVersion + aaString + ccString + uhString + scString + bsString +fragmentShaderBody;

    // =========================
    // Shader load
    // =========================
    m_shader.LoadShaders(vertexShader, fs.c_str());
    m_shader.GetUniformLocationMap("tex1");
	m_shader.GetUniformLocationMap("barrelEffectEnable");
	m_shader.GetUniformLocationMap("BarrelStrength");
    m_shader.GetUniformLocationMap("scanlineEnable");
	m_shader.GetUniformLocationMap("scanlineStrength");
    m_shader.GetUniformLocationMap("uAspect");
    

    // VAO生成
    glGenVertexArrays(1, &m_vao);
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
	if ((m_aa > 1) || (m_crtcolors != CRTcolor::None)) {
		
		 // 0以下のサイズを防ぐ
    	if (width <= 0 || height <= 0) return;
    		m_fbo.Destroy();
    		m_fbo.Create(width * m_aa, height * m_aa);

    		m_width  = width;
    		m_height = height;
		}
		
	}
    //m_fbo.Destroy();
   

// =========================
// Draw
// =========================
void SuperAA::Draw()
{
	if ((m_aa > 1) || (m_crtcolors != CRTcolor::None)) {
		
	
		
	
	
    // 初期化されていない、またはサイズが無効な場合は描画しない
    if (m_width <= 0 || m_height <= 0) return;

    glDisable(GL_DEPTH_TEST);
    //glDisable(GL_STENCIL_TEST);
    //glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);

    m_shader.EnableShader();

    // テクスチャバインド (Unit 0)
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_fbo.GetTextureID());
    if (m_shader.attribLocMap["tex1"] >= 0)
    {
        glUniform1i(m_shader.attribLocMap["tex1"], 0);
    }
			
	//glUniform1i(m_shader.attribLocMap["barrelEffectEnable"],m_barrelEffectEnable ? 1 : 0);
		
		
    glUniform1i(m_shader.attribLocMap["scanlineEnable"],m_scanlineEnable ? 1 : 0);
/*
    if (m_barrelEffectEnable)
	{
		glUniform1f(m_shader.attribLocMap["BarrelStrength"],m_barrelStrength);
	}
	else
	{
		glUniform1f(m_shader.attribLocMap["BarrelStrength"],0.0f);
	}
*/
	
	// Aspect / Barrel（今はデバッグ固定でもOK）
    if (m_shader.attribLocMap["uAspect"] >= 0)
        glUniform1f(m_shader.attribLocMap["uAspect"],
                    float(m_width) / float(m_height));

    //if (m_shader.attribLocMap["BarrelStrength"] >= 0)
        //glUniform1f(m_shader.attribLocMap["BarrelStrength"], 0.01f);
	
    glBindVertexArray(m_vao);
    glViewport(0, 0, m_width, m_height);
    
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    
    glBindVertexArray(0);
    m_shader.DisableShader();
	}
	
}

void SuperAA::ToggleScanline()
{
    m_scanlineEnable = !m_scanlineEnable;
	printf("[SuperAA] Scanline %s\n", m_scanlineEnable ? "ON" : "OFF");
	
}
void SuperAA::ToggleBarrelEffect()
{
	m_barrelEffectEnable = !m_barrelEffectEnable;
	printf("[SuperAA] barrelEffect %s\n", m_barrelEffectEnable ? "ON" : "OFF");
	
}

//float SuperAA::BarrelStrength()
//{
//	m_barrelStrength = ubarrelStrength;
//}
/*
void SuperAA::SetScanlineEnable(bool enable)
{
    //m_scanlineEnable = true;
}

bool SuperAA::IsScanlineEnabled() const
{
    return m_scanlineEnable;
}
*/
// =========================
// Target FBO
// =========================
GLuint SuperAA::GetTargetID()
{
    return m_fbo.GetFBOID();
}

