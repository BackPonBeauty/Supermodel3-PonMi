#pragma once

#include "Supermodel.h"
#include "FBO.h"
#include "New3D/GLSLShader.h"

// This class just implements super sampling. Super sampling looks fantastic but is quite expensive.
// 8x and beyond values can start to eat ridiculous amounts of memory / gpu time, for less and less noticable returns
// 4x works and looks great
// values such as 3 are also possible, that works out 9 samples per pixel
// The algorithm is super simple, just add up all samples and divide by the number

class SuperAA
{
public:
	SuperAA(int aaValue, CRTcolor CRTcolors , int scanlineStrength , int totalXRes ,int totalYRes , int barrelStrength, const char* gameTitle);
	~SuperAA();

	void Init(int width, int height);		// width & height are real window dimensions
	void Draw();							// this is a no-op if AA is 1 and CRTcolors 0, since we'll be drawing straight on the back buffer anyway

	GLuint GetTargetID();
	GLint m_locScanlineEnable = -1;
	GLint m_locScanlineStrength = -1;
	void ToggleScanline();
	float BarrelStrength();
	void ToggleBarrelEffect();
    void SetScanlineEnable(bool False);
    bool IsScanlineEnabled() const;
	void LoadOverlayByTitle(const std::string& gameTitle);

private:
	FBO m_fbo;
	FBO m_fbo2;
	GLSLShader m_shader;
	GLSLShader m_overlayShader;
	const int m_aa;
	const CRTcolor m_crtcolors;
	bool m_scanlineEnable;
	float m_scanlineStrength;
	bool m_barrelEffectEnable;
	float m_barrelStrength;
	int m_totalXRes;
	int m_totalYRes;
	GLuint m_vao;
	int m_width;
	int m_height;
	GLuint m_overlayTex = 0;
};
