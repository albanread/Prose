#include "PJFractal.h"

#include <cmath>

const double PJFractal::kOrbitRadius = 0.7885;
const int32 PJFractal::kPaletteCount = 4;

namespace {

struct RampPoint { double t; uint8 r, g, b; };

// the palettes: control points across t = 0..1 (the ramp wraps)
const RampPoint kRamps[PJFractal::kPaletteCount][6] = {
	{	// Inferno: black → purple → red → orange → yellow → white
		{ 0.00,   0,   0,   4 },
		{ 0.25,  87,  16, 110 },
		{ 0.50, 188,  55,  84 },
		{ 0.70, 249, 133,  22 },
		{ 0.88, 252, 222, 100 },
		{ 1.00, 255, 255, 255 },
	},
	{	// Ocean: abyss → navy → blue → cyan → foam
		{ 0.00,   2,   6,  24 },
		{ 0.30,  10,  40,  99 },
		{ 0.55,  30,  96, 168 },
		{ 0.78,  90, 180, 222 },
		{ 0.92, 190, 236, 250 },
		{ 1.00, 255, 255, 255 },
	},
	{	// Ember: charcoal → ash → ember → flame
		{ 0.00,  12,  10,  10 },
		{ 0.35,  70,  52,  46 },
		{ 0.60, 178,  78,  44 },
		{ 0.80, 240, 152,  60 },
		{ 0.93, 254, 224, 144 },
		{ 1.00, 255, 255, 236 },
	},
	{	// Ultraviolet: black → violet → magenta → cyan → white
		{ 0.00,   4,   0,  12 },
		{ 0.28,  64,  12, 108 },
		{ 0.52, 156,  36, 168 },
		{ 0.74, 236,  92, 188 },
		{ 0.90, 152, 220, 252 },
		{ 1.00, 255, 255, 255 },
	},
};

uint8
Lerp8(double t, uint8 a, uint8 b)
{
	return (uint8)(a + (b - a) * t + 0.5);
}

}	// namespace

PJFractal::PJFractal()
{
	BuildRamp();
}

void
PJFractal::BuildRamp()
{
	const RampPoint* pts = kRamps[fPalette];
	for (int32 i = 0; i < 256; i++) {
		double t = i / 255.0;
		int32 seg = 0;
		while (seg < 4 && t > pts[seg + 1].t)
			seg++;
		double f = (t - pts[seg].t)
			/ (pts[seg + 1].t - pts[seg].t + 1e-9);
		if (f < 0) f = 0;
		if (f > 1) f = 1;
		fRamp[i].r = Lerp8(f, pts[seg].r, pts[seg + 1].r);
		fRamp[i].g = Lerp8(f, pts[seg].g, pts[seg + 1].g);
		fRamp[i].b = Lerp8(f, pts[seg].b, pts[seg + 1].b);
	}
}

void
PJFractal::SetView(float re, float im, float span)
{
	if (span < 1e-6f || span > 100.0f)
		return;
	fCentreRe = re;
	fCentreIm = im;
	fSpan = span;
}

void
PJFractal::ZoomBy(float factor)
{
	if (factor <= 0)
		return;
	SetView(fCentreRe, fCentreIm, fSpan * factor);
}

void
PJFractal::PanBy(float dRe, float dIm)
{
	fCentreRe += dRe;
	fCentreIm += dIm;
}

void
PJFractal::SetFrame(double thetaDeg)
{
	// keep theta in [0, 360) so Frame get/set round-trips
	fTheta = fmod(thetaDeg, 360.0);
	if (fTheta < 0)
		fTheta += 360.0;
	double rad = fTheta * M_PI / 180.0;
	fCRe = kOrbitRadius * cos(rad);
	fCIm = kOrbitRadius * sin(rad);
}

void
PJFractal::SetIterations(int32 n)
{
	if (n >= 16 && n <= 2048)
		fIterations = n;
}

void
PJFractal::SetPalette(int32 index)
{
	if (index < 0 || index >= kPaletteCount)
		return;
	fPalette = index;
	BuildRamp();
}

const char*
PJFractal::PaletteName(int32 index)
{
	static const char* names[kPaletteCount] = { "Inferno", "Ocean",
		"Ember", "Ultraviolet" };
	if (index < 0 || index >= kPaletteCount)
		return "Inferno";
	return names[index];
}

double
PJFractal::SmoothIter(double zre, double zim) const
{
	// z ← z² + c until |z| escapes radius 2; the smooth count keeps
	// bands out of the colouring (log-log interpolation)
	double re = zre, im = zim;
	for (int32 i = 0; i < fIterations; i++) {
		double nre = re * re - im * im + fCRe;
		double nim = 2.0 * re * im + fCIm;
		re = nre;
		im = nim;
		double m2 = re * re + im * im;
		if (m2 > 4.0) {
			double m = sqrt(m2);
			return i + 1.0 - log(log(m) / log(2.0)) / log(2.0);
		}
	}
	return -1.0;		// inside
}

PJColour
PJFractal::RampColour(double t) const
{
	// t wraps: the ramp is a ring, so iteration bands flow past the
	// high end back into the dark
	double f = fmod(t, 256.0);
	if (f < 0)
		f += 256.0;
	return fRamp[(int32)f & 255];
}

PJColour
PJFractal::PixelColour(int32 x, int32 y, int32 w, int32 h) const
{
	double re = fCentreRe + ((double)x / (w - 1) - 0.5) * fSpan;
	double im = fCentreIm + ((double)y / (h - 1) - 0.5)
		* (fSpan * h / w);
	double n = SmoothIter(re, im);
	if (n < 0)
		return PJColour{ 0, 0, 0 };		// the set itself: black
	// scale the count into the ramp; 32 iterations ≈ one full lap
	return RampColour(n * 8.0);
}

void
PJFractal::Render(uint8* bits, int32 bpr, int32 w, int32 h) const
{
	for (int32 y = 0; y < h; y++) {
		uint8* row = bits + (size_t)y * bpr;
		double im = fCentreIm + ((double)y / (h - 1) - 0.5)
			* (fSpan * h / w);
		for (int32 x = 0; x < w; x++) {
			double re = fCentreRe + ((double)x / (w - 1) - 0.5) * fSpan;
			double n = SmoothIter(re, im);
			uint8* p = row + (size_t)x * 4;
			if (n < 0) {
				p[0] = p[1] = p[2] = 0;
				p[3] = 255;
				continue;
			}
			PJColour c = RampColour(n * 8.0);
			p[0] = c.b;
			p[1] = c.g;
			p[2] = c.r;
			p[3] = 255;
		}
	}
}
