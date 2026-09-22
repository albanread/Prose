#include "PPBrush.h"

#include <cmath>

PPBrush::PPBrush()
{
	Rebuild();
}

void
PPBrush::SetSize(int32 size)
{
	if (size < 1)
		size = 1;
	if (size > 64)
		size = 64;
	if (size != fSize) {
		fSize = size;
		Rebuild();
	}
}

void
PPBrush::Rebuild()
{
	int32 n = fSize;
	fMaskBpr = n;
	fMask.assign((size_t)n * n, 0);
	float c = (n - 1) / 2.0f;
	float radius = n / 2.0f;
	// hardness as a falloff: 0 = fully soft (linear to the edge),
	// 255 = hard (full inside a pixel of the rim)
	float hard = fHardness / 255.0f;
	for (int32 y = 0; y < n; y++) {
		for (int32 x = 0; x < n; x++) {
			uint8 v = 0;
			switch (fShape) {
				case PP_BRUSH_ROUND:
				{
					// a hard circle: full alpha anywhere inside the rim
					float dx = x - c, dy = y - c;
					if (dx * dx + dy * dy <= radius * radius)
						v = 255;
					break;
				}
				case PP_BRUSH_SOFT:
				{
					float dx = x - c, dy = y - c;
					float d = sqrtf(dx * dx + dy * dy);
					if (d <= radius) {
						float t = d / radius;		// 0 centre, 1 rim
						// full alpha until the hardness core ends,
						// linear falloff to the rim after it
						float core = 1.0f - hard;
						float a = t <= core ? 1.0f
							: 1.0f - (t - core) / (1.0f - core + 1e-6f);
						v = (uint8)std::max(0.0f,
							std::min(1.0f, a) * 255.0f);
					}
					break;
				}
				case PP_BRUSH_SQUARE:
					v = 255;
					break;
				case PP_BRUSH_ANGLED:
				{
					// a 45-degree slanted ellipse: |dx·cos + dy·sin|
					// small across a thin diagonal nib
					const float k = 0.70710678f;
					float dx = x - c, dy = y - c;
					float across = dx * k + dy * k;
					float along = -dx * k + dy * k;
					float thin = radius * 0.35f + 0.5f;
					if (fabsf(across) <= thin && fabsf(along) <= radius)
						v = 255;
					break;
				}
				default:
					break;
			}
			fMask[(size_t)y * fMaskBpr + x] = v;
		}
	}
}

void
PPBrush::StampLine(float x0, float y0, float x1, float y1,
	void (*dab)(void* ctx, int32 x, int32 y), void* ctx) const
{
	float dist = sqrtf((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0));
	float spacing = std::max(1.0f, fSize / 4.0f);
	int32 steps = (int32)(dist / spacing) + 1;
	for (int32 i = 0; i <= steps; i++) {
		float t = steps == 0 ? 0 : (float)i / steps;
		dab(ctx, (int32)(x0 + (x1 - x0) * t + 0.5f),
			(int32)(y0 + (y1 - y0) * t + 0.5f));
	}
}

const char*
PPBrush::ShapeName(PPBrushShape s)
{
	switch (s) {
		case PP_BRUSH_ROUND: return "Round";
		case PP_BRUSH_SOFT: return "Soft";
		case PP_BRUSH_SQUARE: return "Square";
		default: return "Angled";
	}
}

const char*
PPBrush::ToolName(PPTool t)
{
	switch (t) {
		case PP_TOOL_PEN: return "Pen";
		case PP_TOOL_BRUSH: return "Brush";
		case PP_TOOL_AIRBRUSH: return "Airbrush";
		case PP_TOOL_ERASER: return "Eraser";
		case PP_TOOL_SMUDGE: return "Smudge";
		case PP_TOOL_FILL: return "Fill";
		case PP_TOOL_LINE: return "Line";
		case PP_TOOL_RECT: return "Rectangle";
		case PP_TOOL_ELLIPSE: return "Ellipse";
		default: return "Eyedropper";
	}
}
