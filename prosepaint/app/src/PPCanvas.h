// PPCanvas — the painting surface: the composited image on the page
// (desk + shadow, the Prose suite look), stroke capture for every
// tool, all input through one doc<->view mapping (zoom-ready).
#ifndef PP_CANVAS_H
#define PP_CANVAS_H

#include <View.h>

#include "PPBrush.h"
#include "PPDocument.h"

class PPCanvas : public BView {
public:
			PPCanvas(PPDocument* doc);

	void	Draw(BRect updateRect) override;
	void	MouseDown(BPoint point) override;
	void	MouseMoved(BPoint point, uint32 transit,
				const BMessage* drag) override;
	void	MouseUp(BPoint point) override;
	void	Pulse() override;
	void	MakeFocus(bool focus = true) override;

	PPTool	Tool() const { return fTool; }
	void	SetTool(PPTool tool) { fTool = tool; }
	PPBrush&	Brush() { return fBrush; }
	void	SetColour(rgb_color c) { fColour = c; }
	rgb_color	Colour() const { return fColour; }
	void	SetSmudgeStrength(uint8 s) { fSmudgeStrength = s; }
	void	SetFillTolerance(uint8 t) { fFillTolerance = t; }
	// stroke opacity percent (0..100) — the stroke-buffer contract
	void	SetOpacityPercent(int32 percent);
	int32	OpacityPercent() const { return (int32)fOpacity * 100 / 255; }

	// zoom: one mapping point (DocToView/ViewToDoc below); the render
	// scales, the composite stays 1:1 data
	void	SetZoom(float zoom);
	float	Zoom() const { return fZoom; }

	// scripted painting: one full stroke along a segment (the test
	// workhorse — the guest's pointer is dead, the scripting IS the
	// brush)
	void	StrokeSegment(float x0, float y0, float x1, float y1);
	// a single dab at a point
	void	DabAt(float x, float y);
	// scripted shape stroke: "line x0 y0 x1 y1", "rect x y w h",
	// "ellipse x y w h" — stroked with the current brush
	void	ShapeStroke(const char* spec);

	// the composite changed: repaint (and only what changed)
	void	DocChanged();

	BPoint	DocToView(BPoint p) const
	{
		return BPoint((kMargin + p.x) * fZoom, (kMargin + p.y) * fZoom);
	}
	BPoint	ViewToDoc(BPoint p) const
	{
		return BPoint(p.x / fZoom - kMargin, p.y / fZoom - kMargin);
	}
	static const float	kMargin;

private:
	void	PaintDab(BPoint doc);
	void	PickAt(BPoint doc);
	void	StampPath(float x0, float y0, float x1, float y1);
	void	CommitShape(BPoint from, BPoint to);
	void	UpdateStatus();

	PPDocument*	fDoc;
	PPTool		fTool = PP_TOOL_BRUSH;
	PPBrush		fBrush;
	rgb_color	fColour { 0, 0, 0, 255 };
	uint8		fOpacity = 255;
	uint8		fSmudgeStrength = 128;
	uint8		fFillTolerance = 32;
	bool		fPainting = false;
	BPoint		fLastDoc;
	// shape tools: anchor + live preview
	BPoint		fShapeAnchor;
	bool		fShaping = false;
	float		fZoom = 1.0f;
};

#endif	// PP_CANVAS_H
