// PJFractal — the Julia math, free of UI: view, the animated c-path,
// escape iteration with smooth colouring, and palettes. Pure and
// deterministic so the selftest and the pixels on screen agree.
#ifndef PJ_FRACTAL_H
#define PJ_FRACTAL_H

#include <SupportDefs.h>

#include <cstdint>
#include <cstring>

struct PJColour {
	uint8	r = 0, g = 0, b = 0;
};

class PJFractal {
public:
			PJFractal();

	// ---- view (complex plane): centre + horizontal span
	float	CentreRe() const { return fCentreRe; }
	float	CentreIm() const { return fCentreIm; }
	float	Span() const { return fSpan; }
	void	SetView(float re, float im, float span);
	void	ZoomBy(float factor);		// <1 zooms in, clamped
	void	PanBy(float dRe, float dIm);

	// ---- c: the animated parameter, on the classic 0.7885·e^{iθ}
	// orbit (θ in degrees — the animation frame)
	double	Frame() const { return fTheta; }
	void	SetFrame(double thetaDeg);
	double	CRe() const { return fCRe; }
	double	CIm() const { return fCIm; }
	static const double	kOrbitRadius;

	// ---- quality
	int32	Iterations() const { return fIterations; }
	void	SetIterations(int32 n);		// 16..2048

	// ---- palettes (256-entry ramps, built from control points)
	int32	Palette() const { return fPalette; }
	void	SetPalette(int32 index);
	static const int32	kPaletteCount;
	static const char*	PaletteName(int32 index);

	// ---- render: B_RGBA32 rows (B,G,R,A) into caller memory
	void	Render(uint8* bits, int32 bpr, int32 w, int32 h) const;
	// one pixel's colour — the test surface (same math as Render)
	PJColour PixelColour(int32 x, int32 y, int32 w, int32 h) const;
	// raw escape data: smooth iteration count, or -1 for inside
	double	SmoothIter(double zre, double zim) const;

private:
	PJColour RampColour(double t) const;	// t wraps at 256
	void	BuildRamp();

	float	fCentreRe = 0.0f;
	float	fCentreIm = 0.0f;
	float	fSpan = 3.0f;
	double	fTheta = 0.0;			// degrees
	double	fCRe = kOrbitRadius;
	double	fCIm = 0.0;
	int32	fIterations = 200;
	int32	fPalette = 0;
	PJColour	fRamp[256];
};

#endif	// PJ_FRACTAL_H
