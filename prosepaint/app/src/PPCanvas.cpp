#include "PPCanvas.h"

#include <ScrollView.h>
#include <Window.h>

#include <cmath>
#include <algorithm>

const float PPCanvas::kMargin = 24.0f;

PPCanvas::PPCanvas(PPDocument* doc)
	:
	BView(BRect(0, 0, 400, 400), "canvas", B_FOLLOW_NONE,
		B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE | B_PULSE_NEEDED),
	fDoc(doc)
{
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	SetDrawingMode(B_OP_COPY);
	DocChanged();
}

void
PPCanvas::MakeFocus(bool focus)
{
	BView::MakeFocus(focus);
	Invalidate();
}

void
PPCanvas::SetZoom(float zoom)
{
	if (zoom < 0.25f)
		zoom = 0.25f;
	if (zoom > 4.0f)
		zoom = 4.0f;
	if (zoom == fZoom)
		return;
	fZoom = zoom;
	DocChanged();
}

void
PPCanvas::DocChanged()
{
	ResizeTo((fDoc->Width() + 2 * kMargin) * fZoom,
		(fDoc->Height() + 2 * kMargin) * fZoom);
	UpdateScrollBars();
	Invalidate();
}


/*!	Both scroll bars, always: the range is what makes a bar a bar -- a bar
	whose range was never set (the horizontal one, until now) shows no knob
	and pans nothing, which reads as the canvas having paved over it.
*/
void
PPCanvas::UpdateScrollBars()
{
	BScrollView* sc = dynamic_cast<BScrollView*>(Parent());
	if (sc == NULL)
		return;
	float rangeX = std::max(0.0f, Bounds().Width() - sc->Bounds().Width());
	float rangeY = std::max(0.0f, Bounds().Height() - sc->Bounds().Height());
	if (BScrollBar* bar = sc->ScrollBar(B_HORIZONTAL)) {
		bar->SetRange(0, rangeX);
		bar->SetProportion(sc->Bounds().Width() / Bounds().Width());
	}
	if (BScrollBar* bar = sc->ScrollBar(B_VERTICAL)) {
		bar->SetRange(0, rangeY);
		bar->SetProportion(sc->Bounds().Height() / Bounds().Height());
	}
}

// ------------------------------------------------------------------ draw --
void
PPCanvas::Draw(BRect)
{
	// desk, shadow, page — the Prose suite look
	BRect pageRect(DocToView(BPoint(0, 0)),
		DocToView(BPoint(fDoc->Width(), fDoc->Height())));
	SetHighColor(tint_color(ui_color(B_PANEL_BACKGROUND_COLOR),
		B_DARKEN_2_TINT));
	FillRect(pageRect.OffsetByCopy(3, 3));
	SetHighColor(255, 255, 255, 255);
	FillRect(pageRect);
	const BBitmap* comp = fDoc->Composite();
	if (comp != NULL)
		DrawBitmap(comp, comp->Bounds(), pageRect);
	// transparency reads as the white page under the composite
	// rubber-band preview for the shape tools
	if (fShaping) {
		SetHighColor(0, 100, 255, 255);
		BPoint a = DocToView(fShapeAnchor);
		BPoint b = DocToView(fLastDoc);
		switch (fTool) {
			case PP_TOOL_LINE:
				StrokeLine(a, b);
				break;
			case PP_TOOL_RECT:
				StrokeRect(BRect(a, b));
				break;
			case PP_TOOL_ELLIPSE:
				StrokeEllipse(BRect(a, b));
				break;
			default:
				break;
		}
	}
	SetHighColor(180, 180, 180, 255);
	StrokeRect(pageRect);
}

// ----------------------------------------------------------------- input --
void
PPCanvas::SetOpacityPercent(int32 percent)
{
	if (percent < 0) percent = 0;
	if (percent > 100) percent = 100;
	fOpacity = (uint8)(percent * 255 / 100);
}

void
PPCanvas::PaintDab(BPoint doc)
{
	PPBrush& b = fBrush;
	switch (fTool) {
		case PP_TOOL_PEN:
		case PP_TOOL_BRUSH:
		case PP_TOOL_LINE:
		case PP_TOOL_RECT:
		case PP_TOOL_ELLIPSE:
			fDoc->DabColour((int32)doc.x, (int32)doc.y, fColour,
				b.Mask(), b.MaskSize(), b.MaskBpr(), 255);
			break;
		case PP_TOOL_AIRBRUSH:
			// low flow: repeated passes (or a held spray via Pulse)
			// build the ink up
			fDoc->DabColour((int32)doc.x, (int32)doc.y, fColour,
				b.Mask(), b.MaskSize(), b.MaskBpr(), 40);
			break;
		case PP_TOOL_ERASER:
			fDoc->DabErase((int32)doc.x, (int32)doc.y,
				b.Mask(), b.MaskSize(), b.MaskBpr(), 255);
			break;
		case PP_TOOL_SMUDGE:
			fDoc->DabSmudge((int32)doc.x, (int32)doc.y,
				b.Mask(), b.MaskSize(), b.MaskBpr(), fSmudgeStrength);
			break;
		default:
			break;
	}
}

// stamp the brush along a shape path (line / rectangle edges /
// ellipse) — the same dab pipeline as freehand strokes
void
PPCanvas::StampPath(float x0, float y0, float x1, float y1)
{
	fBrush.StampLine(x0, y0, x1, y1,
		[](void* ctx, int32 x, int32 y)
		{
			((PPCanvas*)ctx)->PaintDab(BPoint(x, y));
		}, this);
}

void
PPCanvas::CommitShape(BPoint from, BPoint to)
{
	fDoc->StrokeBegin(fOpacity);
	switch (fTool) {
		case PP_TOOL_LINE:
			StampPath(from.x, from.y, to.x, to.y);
			break;
		case PP_TOOL_RECT:
			StampPath(from.x, from.y, to.x, from.y);
			StampPath(to.x, from.y, to.x, to.y);
			StampPath(to.x, to.y, from.x, to.y);
			StampPath(from.x, to.y, from.x, from.y);
			break;
		case PP_TOOL_ELLIPSE:
		{
			float cx = (from.x + to.x) / 2, cy = (from.y + to.y) / 2;
			float rx = fabsf(to.x - from.x) / 2, ry = fabsf(to.y - from.y) / 2;
			float perim = 3.1416f * 1.5f * (rx + ry);
			int32 steps = std::max(24, (int32)(perim
				/ std::max(1.0f, fBrush.Size() / 4.0f)));
			float px = cx + rx, py = cy;
			for (int32 i = 1; i <= steps; i++) {
				float t = (float)i / steps * 2.0f * 3.14159265f;
				float nx = cx + rx * cosf(t), ny = cy + ry * sinf(t);
				StampPath(px, py, nx, ny);
				px = nx; py = ny;
			}
			break;
		}
		default:
			break;
	}
	fDoc->StrokeEnd();
	DocChanged();
	UpdateStatus();
}

void
PPCanvas::ShapeStroke(const char* spec)
{
	if (spec == NULL)
		return;
	float a = 0, b2 = 0, c = 0, d = 0;
	if (sscanf(spec, "line %f %f %f %f", &a, &b2, &c, &d) == 4) {
		PPTool keep = fTool;
		fTool = PP_TOOL_LINE;
		CommitShape(BPoint(a, b2), BPoint(c, d));
		fTool = keep;
	} else if (sscanf(spec, "rect %f %f %f %f", &a, &b2, &c, &d) == 4) {
		PPTool keep = fTool;
		fTool = PP_TOOL_RECT;
		CommitShape(BPoint(a, b2), BPoint(a + c, b2 + d));
		fTool = keep;
	} else if (sscanf(spec, "ellipse %f %f %f %f", &a, &b2, &c, &d) == 4) {
		PPTool keep = fTool;
		fTool = PP_TOOL_ELLIPSE;
		CommitShape(BPoint(a, b2), BPoint(a + c, b2 + d));
		fTool = keep;
	}
}

void
PPCanvas::PickAt(BPoint doc)
{
	rgb_color c = fDoc->PickColour((int32)doc.x, (int32)doc.y);
	fColour = c;
	UpdateStatus();
}

void
PPCanvas::MouseDown(BPoint where)
{
	MakeFocus();
	BPoint doc = ViewToDoc(where);
	switch (fTool) {
		case PP_TOOL_FILL:
			fDoc->StrokeBegin(fOpacity);
			fDoc->FillAt((int32)doc.x, (int32)doc.y, fColour,
				fFillTolerance);
			fDoc->StrokeEnd();
			DocChanged();
			UpdateStatus();
			return;
		case PP_TOOL_EYEDROPPER:
			PickAt(doc);
			return;
		case PP_TOOL_LINE:
		case PP_TOOL_RECT:
		case PP_TOOL_ELLIPSE:
			fShaping = true;
			fShapeAnchor = doc;
			fLastDoc = doc;
			SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);
			return;
		default:
			break;
	}
	fDoc->StrokeBegin(fOpacity);
	fPainting = true;
	fLastDoc = doc;
	PaintDab(doc);
	Invalidate();
	SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);
}

void
PPCanvas::MouseMoved(BPoint point, uint32 transit, const BMessage*)
{
	if (fShaping) {
		if (transit != B_INSIDE_VIEW && transit != B_OUTSIDE_VIEW)
			return;
		fLastDoc = ViewToDoc(point);
		Invalidate();	// rubber-band preview
		return;
	}
	if (!fPainting || transit != B_INSIDE_VIEW)
		return;
	BPoint doc = ViewToDoc(point);
	float dist = sqrtf((doc.x - fLastDoc.x) * (doc.x - fLastDoc.x)
		+ (doc.y - fLastDoc.y) * (doc.y - fLastDoc.y));
	if (dist < std::max(1.0f, fBrush.Size() / 8.0f))
		return;
	// stamp along the segment, then remember where we got to
	fBrush.StampLine(fLastDoc.x, fLastDoc.y, doc.x, doc.y,
		[](void* ctx, int32 x, int32 y)
		{
			((PPCanvas*)ctx)->PaintDab(BPoint(x, y));
		}, this);
	fLastDoc = doc;
	Invalidate();
}

void
PPCanvas::MouseUp(BPoint)
{
	if (fShaping) {
		fShaping = false;
		CommitShape(fShapeAnchor, fLastDoc);
		return;
	}
	if (!fPainting)
		return;
	fPainting = false;
	fDoc->StrokeEnd();
	Invalidate();
	UpdateStatus();
}

void
PPCanvas::Pulse()
{
	// the airbrush sprays while the button is held
	if (fPainting && fTool == PP_TOOL_AIRBRUSH) {
		PaintDab(fLastDoc);
		Invalidate();
	}
}

void
PPCanvas::StrokeSegment(float x0, float y0, float x1, float y1)
{
	if (fTool == PP_TOOL_FILL) {
		fDoc->StrokeBegin(fOpacity);
		fDoc->FillAt((int32)x0, (int32)y0, fColour, fFillTolerance);
		fDoc->StrokeEnd();
		DocChanged();
		return;
	}
	if (fTool == PP_TOOL_EYEDROPPER) {
		PickAt(BPoint(x0, y0));
		return;
	}
	fDoc->StrokeBegin(fOpacity);
	fBrush.StampLine(x0, y0, x1, y1,
		[](void* ctx, int32 x, int32 y)
		{
			((PPCanvas*)ctx)->PaintDab(BPoint(x, y));
		}, this);
	fDoc->StrokeEnd();
	DocChanged();
	UpdateStatus();
}

void
PPCanvas::DabAt(float x, float y)
{
	if (fTool == PP_TOOL_EYEDROPPER) {
		PickAt(BPoint(x, y));
		return;
	}
	fDoc->StrokeBegin();
	PaintDab(BPoint(x, y));
	fDoc->StrokeEnd();
	DocChanged();
	UpdateStatus();
}

void
PPCanvas::UpdateStatus()
{
	if (Window() != NULL)
		Window()->PostMessage('ppUp');
}
