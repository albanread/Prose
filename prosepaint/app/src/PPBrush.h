// PPBrush — the stamp engine. A brush is a precomputed alpha mask:
// shape × size × hardness. Stamps are blended along the stroke path
// with spacing; the model owns the blend math, this owns the shape.
#ifndef PP_BRUSH_H
#define PP_BRUSH_H

#include <GraphicsDefs.h>
#include <Rect.h>

#include <cstdint>
#include <vector>

enum PPBrushShape : uint8 {
	PP_BRUSH_ROUND = 0,		// hard-edged circle
	PP_BRUSH_SOFT,			// soft-edged circle (hardness falloff)
	PP_BRUSH_SQUARE,		// hard square
	PP_BRUSH_ANGLED,		// 45-degree calligraphic slant
	PP_BRUSH_COUNT
};

enum PPTool : uint8 {
	PP_TOOL_PEN = 0,		// hard round, tiny sizes
	PP_TOOL_BRUSH,
	PP_TOOL_AIRBRUSH,		// low flow; sprays while held
	PP_TOOL_ERASER,
	PP_TOOL_SMUDGE,
	PP_TOOL_FILL,
	PP_TOOL_EYEDROPPER,
	PP_TOOL_LINE,			// shape strokes, rubber-band preview
	PP_TOOL_RECT,
	PP_TOOL_ELLIPSE,
	PP_TOOL_COUNT
};

class PPBrush {
public:
			PPBrush();

	PPBrushShape	Shape() const { return fShape; }
	void		SetShape(PPBrushShape shape) { fShape = shape; Rebuild(); }
	int32		Size() const { return fSize; }
	void		SetSize(int32 size);			// 1..64 px
	uint8		Hardness() const { return fHardness; }
	void		SetHardness(uint8 h) { fHardness = h; Rebuild(); }

	// the stamp mask: size×size bytes of 0..255, row stride maskBpr
	const uint8*	Mask() const { return fMask.data(); }
	int32		MaskSize() const { return fSize; }
	int32		MaskBpr() const { return fMaskBpr; }
	BRect		MaskBounds() const
					{ return BRect(0, 0, fSize - 1, fSize - 1); }

	// stamp the mask along the line (x0,y0)-(x1,y1), invoking dab at
	// each spaced position; spacing = size/4, at least 1 px
	void		StampLine(float x0, float y0, float x1, float y1,
				void (*dab)(void* ctx, int32 x, int32 y),
				void* ctx) const;

	static const char*	ShapeName(PPBrushShape s);
	static const char*	ToolName(PPTool t);

private:
	void		Rebuild();

	PPBrushShape	fShape = PP_BRUSH_ROUND;
	int32		fSize = 8;
	uint8		fHardness = 200;
	int32		fMaskBpr = 8;
	std::vector<uint8>	fMask;
};

#endif	// PP_BRUSH_H
