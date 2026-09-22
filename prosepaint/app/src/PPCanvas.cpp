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
	BScrollView* sc = dynamic_cast<BScrollView*>(Parent());
	if (sc != NULL && sc->ScrollBar(B_VERTICAL) != NULL)
		sc->ScrollBar(B_VERTICAL)->SetRange(0,
			std::max(0.0f, Bounds().Height() - sc->Bounds().Height()));
	Invalidate();
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
	SetHighColor(180, 180, 180, 255);
	StrokeRect(pageRect);
}

// ----------------------------------------------------------------- input --
void
PPCanvas::PaintDab(BPoint doc)
{
	PPBrush& b = fBrush;
	switch (fTool) {
		case PP_TOOL_PEN:
		case PP_TOOL_BRUSH:
			fDoc->DabColour((int32)doc.x, (int32)doc.y, fColour,
				b.Mask(), b.MaskSize(), b.MaskBpr(), 255);
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
			fDoc->StrokeBegin();
			fDoc->FillAt((int32)doc.x, (int32)doc.y, fColour,
				fFillTolerance);
			fDoc->StrokeEnd();
			DocChanged();
			UpdateStatus();
			return;
		case PP_TOOL_EYEDROPPER:
			PickAt(doc);
			return;
		default:
			break;
	}
	fDoc->StrokeBegin();
	fPainting = true;
	fLastDoc = doc;
	PaintDab(doc);
	Invalidate();
	SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);
}

void
PPCanvas::MouseMoved(BPoint point, uint32 transit, const BMessage*)
{
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
	if (!fPainting)
		return;
	fPainting = false;
	fDoc->StrokeEnd();
	Invalidate();
	UpdateStatus();
}

void
PPCanvas::StrokeSegment(float x0, float y0, float x1, float y1)
{
	if (fTool == PP_TOOL_FILL) {
		fDoc->StrokeBegin();
		fDoc->FillAt((int32)x0, (int32)y0, fColour, fFillTolerance);
		fDoc->StrokeEnd();
		DocChanged();
		return;
	}
	if (fTool == PP_TOOL_EYEDROPPER) {
		PickAt(BPoint(x0, y0));
		return;
	}
	fDoc->StrokeBegin();
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
