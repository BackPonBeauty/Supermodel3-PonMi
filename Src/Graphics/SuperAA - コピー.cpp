#include "SuperAA.h"
#include <string>

SuperAA::SuperAA(int aaValue, CRTcolor CRTcolors, float scanlineStrength) :
    m_aa(aaValue),
    m_crtcolors(CRTcolors),
    m_scanlineEnable(true),
    m_scanlineStrength(scanlineStrength),
    m_vao(0),
    m_width(0),
    m_height(0)
{
    //if ((m_aa > 1) || (m_crtcolors != CRTcolor::None))
	if (true)
    {
    	//float m_scanlineStrength = 0.85f;
        // =========================
        // Vertex Shader
        // =========================
        static const char* vertexShader = R"glsl(
#version 410 core

void main(void)
{
    const vec4 vertices[] = vec4[](
        vec4(-1.0, -1.0, 0.0, 1.0),
        vec4(-1.0,  1.0, 0.0, 1.0),
        vec4( 1.0, -1.0, 0.0, 1.0),
        vec4( 1.0,  1.0, 0.0, 1.0)
    );

    gl_Position = vertices[gl_VertexID % 4];
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
    	
    	std::string scString = "const float SCANLINE_STRENGTH = ";
		scString += std::to_string(m_scanlineStrength);
		scString += ";\n";

        // =========================
        // Fragment Shader body                              uniform float scanlineStrength;
        // =========================
        static const std::string fragmentShader = R"glsl(

uniform sampler2D tex1;
uniform bool scanlineEnable;
uniform float scanlineStrength;
out vec4 fragColor;

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

// ===== AA resolve =====
vec3 GetTextureValue(sampler2D s)
{
    ivec2 base = ivec2(gl_FragCoord.xy) * aa;
    vec3 col = vec3(0.0);

    for (int y = 0; y < aa; y++)
        for (int x = 0; x < aa; x++)
            col += texelFetch(s, base + ivec2(x, y), 0).rgb;

    return col / float(aa * aa);
}

void main()
{
    vec3 color = GetTextureValue(tex1);

#if (CRTCOLORS != 0)
    if (cgamma != -1.0)
        color = pow(color, vec3(cgamma));
    else
        color = vec3(invsRGB(color.r), invsRGB(color.g), invsRGB(color.b));

    color *= colmatrix;

    color = vec3(sRGB(color.r), sRGB(color.g), sRGB(color.b));
#endif

    // ===== Scanline (AA independent) =====
	if (scanlineEnable)
	{
		float scan = mod(floor(gl_FragCoord.y), 2.0);
		float mask = step(0.5, scan);
		color *= mix(SCANLINE_STRENGTH, 1.0, mask);
	}
    fragColor = vec4(color, 1.0);
}
)glsl";

        std::string fs = fragmentShaderVersion + aaString + ccString + scString + fragmentShader;

        // =========================
        // Shader load
        // =========================
        m_shader.LoadShaders(vertexShader, fs.c_str());
        m_shader.GetUniformLocationMap("tex1");
	    m_shader.GetUniformLocationMap("scanlineEnable");
    	m_shader.GetUniformLocationMap("scanlineStrength");

        m_shader.EnableShader();

		if (m_shader.attribLocMap["tex1"] >= 0)
    	{
    		glUniform1i(m_shader.attribLocMap["tex1"], 0);
    	}

		if (m_shader.attribLocMap["scanlineEnable"] >= 0)
    	{
			glUniform1i(m_shader.attribLocMap["scanlineEnable"],m_scanlineEnable ? 1 : 0);
    		glUniform1f(m_shader.attribLocMap["scanlineStrength"],m_scanlineStrength);
    	}


		m_shader.DisableShader();

        // =========================
        // VAO
        // =========================
        glGenVertexArrays(1, &m_vao);
        glBindVertexArray(m_vao);
        glBindVertexArray(0);
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
    m_fbo.Destroy();
    m_fbo.Create(width * m_aa, height * m_aa);

    m_width  = width;
    m_height = height;
}

// =========================
// Draw
// =========================
void SuperAA::Draw()
{
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_fbo.GetTextureID());

    glBindVertexArray(m_vao);
    glViewport(0, 0, m_width, m_height);

    m_shader.EnableShader();
	
	
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    m_shader.DisableShader();

    glBindVertexArray(0);
}
void SuperAA::ToggleScanline()
{
    m_scanlineEnable = !m_scanlineEnable;

    m_shader.EnableShader();
    if (m_shader.attribLocMap["scanlineEnable"] >= 0)
    {
        glUniform1i(
            m_shader.attribLocMap["scanlineEnable"],
            m_scanlineEnable ? 1 : 0
        );
    	/*
        glUniform1f(
            m_shader.attribLocMap["scanlineStrength"],
            m_scanlineStrength
        );*/
    }
	printf("Scanline strength = %.2f\n", m_scanlineStrength);
    m_shader.DisableShader();
}

void SuperAA::SetScanlineEnable(bool enable)
{
    m_scanlineEnable = enable;
}

bool SuperAA::IsScanlineEnabled() const
{
    return m_scanlineEnable;
}

// =========================
// Target FBO
// =========================
GLuint SuperAA::GetTargetID()
{
    return m_fbo.GetFBOID();
}

