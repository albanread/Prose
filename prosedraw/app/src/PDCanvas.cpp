#include "PDCanvas.h"

#include <Beep.h>
#include <Font.h>
#include <ScrollView.h>
#include <Window.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

const float PDCanvas::kMargin = 24.0f;

static const rgb_color kSelection = { 0, 100, 255, 255 };
static const pattern kDash = { { 0xcc, 0xcc, 0xcc, 0xcc,
	0xcc, 0xcc, 0xcc, 0xcc } };

PDCanvas::PDCanvas(PDDocument* doc)
	:
	BView(BRect(0, 0, 200, 200), "canvas", B_FOLLOW_NONE,
		B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE | B_PULSE_NEEDED),
	fDoc(doc)
{
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	SetDrawingMode(B_OP_COPY);
}

void
PDCanvas::MakeFocus(bool focus)
{
	BView::MakeFocus(focus);
	Invalidate();
}

void
PDCanvas::DocumentChangedSize()
{
	ResizeTo(fDoc->Page().width + 2 * kMargin,
		fDoc->Page().height + 2 * kMargin);
	BScrollView* sc = dynamic_cast<BScrollView*>(Parent());
	if (sc != NULL && sc->ScrollBar(B_VERTICAL) != NULL)
		sc->ScrollBar(B_VERTICAL)->SetRange(0,
			std::max(0.0f, Bounds().Height() - sc->Bounds().Height()));
	Invalidate();
}

// ------------------------------------------------------------------ draw --
void
PDCanvas::Draw(BRect)
{
	const PDPageSetup& page = fDoc->Page();
	BRect pageRect(kMargin, kMargin, kMargin + page.width,
		kMargin + page.height);

	// desk, shadow, page — the ProseWriter look
	SetHighColor(tint_color(ui_color(B_PANEL_BACKGROUND_COLOR),
		B_DARKEN_2_TINT));
	FillRect(pageRect.OffsetByCopy(3, 3));
	SetHighColor(255, 255, 255, 255);
	FillRect(pageRect);
	if (fDoc->ShowGrid())
		DrawGrid(pageRect);

	for (int32 i = 0; i < fDoc->Count(); i++)
		DrawShape(*fDoc->ShapeAt(i));

	// selection chrome on top of everything
	for (int32 id : fSelection) {
		int32 idx = fDoc->IndexOf(id);
		if (idx < 0)
			continue;
		SetHighColor(kSelection);
		if (fSelection.size() > 1
			|| fDoc->ShapeAt(idx)->kind == PD_CONNECTOR) {
			BRect f = SelectionFrame(id);
			StrokeRect(f.InsetByCopy(-2, -2), kDash);
		}
	}
	if (fSelection.size() == 1) {
		int32 idx = fDoc->IndexOf(fSelection[0]);
		if (idx >= 0 && fDoc->ShapeAt(idx)->kind != PD_CONNECTOR) {
			SetHighColor(kSelection);
			for (int32 hx = -1; hx <= 1; hx++)
				for (int32 hy = -1; hy <= 1; hy++) {
					if (hx == 0 && hy == 0)
						continue;
					BPoint c = HandlePos(fDoc->ShapeAt(idx), hx, hy);
					FillRect(BRect(c.x - 3, c.y - 3, c.x + 3, c.y + 3));
				}
		}
	}

	// rubber feedback
	SetHighColor(kSelection);
	if (fDrag == DRAG_CREATE || fDrag == DRAG_BAND) {
		BRect r = fDoc->SnapRect(BRect(fDragStart, fDragLast));
		StrokeRect(BRect(DocToView(r.LeftTop()), DocToView(r.RightBottom())),
			kDash);
	} else if (fDrag == DRAG_CONNECT) {
		StrokeLine(DocToView(fDragStart), DocToView(fConnectNow));
	}
}


void
PDCanvas::DrawGrid(BRect pageRect)
{
	SetHighColor(tint_color(ui_color(B_PANEL_BACKGROUND_COLOR),
		B_DARKEN_1_TINT));
	// blend white 12% -> light lines on the page only
	SetHighColor(230, 230, 230, 255);
	float g = fDoc->Grid();
	for (float x = pageRect.left + g; x < pageRect.right - 0.5f; x += g)
		StrokeLine(BPoint(x, pageRect.top + 1), BPoint(x, pageRect.bottom - 1));
	for (float y = pageRect.top + g; y < pageRect.bottom - 0.5f; y += g)
		StrokeLine(BPoint(pageRect.left + 1, y),
			BPoint(pageRect.right - 1, y));
}

BPoint
PDCanvas::HandlePos(const PDShape* s, int32 xAnchor, int32 yAnchor)
{
	BPoint c = DocToView(BPoint(
		xAnchor < 0 ? s->rect.left : xAnchor > 0 ? s->rect.right
			: (s->rect.left + s->rect.right) / 2,
		yAnchor < 0 ? s->rect.top : yAnchor > 0 ? s->rect.bottom
			: (s->rect.top + s->rect.bottom) / 2));
	return c;
}

BRect
PDCanvas::SelectionFrame(int32 id) const
{
	int32 idx = fDoc->IndexOf(id);
	if (idx < 0)
		return BRect();
	const PDShape& s = *fDoc->ShapeAt(idx);
	if (s.kind == PD_CONNECTOR) {
		const PDShape* from = fDoc->ShapeById(s.fromId);
		const PDShape* to = fDoc->ShapeById(s.toId);
		if (from == NULL || to == NULL)
			return BRect();
		BPoint a = PDDocument::AnchorPoint(*from, *to);
		BPoint b = PDDocument::AnchorPoint(*to, *from);
		return BRect(DocToView(a), DocToView(b));
	}
	return BRect(DocToView(s.rect.LeftTop()), DocToView(s.rect.RightBottom()));
}

void
PDCanvas::DrawShape(const PDShape& s)
{
	if (s.kind == PD_CONNECTOR) {
		const PDShape* from = fDoc->ShapeById(s.fromId);
		const PDShape* to = fDoc->ShapeById(s.toId);
		if (from == NULL || to == NULL)
			return;
		BPoint a = DocToView(PDDocument::AnchorPoint(*from, *to));
		BPoint b = DocToView(PDDocument::AnchorPoint(*to, *from));
		SetHighColor(s.style.stroke);
		SetPenSize(s.style.strokeWidth);
		SetDrawingMode(B_OP_COPY);
		StrokeLine(a, b, s.style.dashed ? kDash : B_SOLID_HIGH);
		// arrowhead at the end, along the line
		float ang = atan2f(b.y - a.y, b.x - a.x);
		const float head = 8.0f + s.style.strokeWidth * 2;
		BPoint tip = b;
		BPoint back(tip.x - head * cosf(ang), tip.y - head * sinf(ang));
		BPoint n(head * 0.4f * sinf(ang), -head * 0.4f * cosf(ang));
		BPoint tri[3] = { tip, BPoint(back.x + n.x, back.y + n.y),
			BPoint(back.x - n.x, back.y - n.y) };
		FillPolygon(tri, 3);
		SetPenSize(1.0f);
		return;
	}

	BRect r = BRect(DocToView(s.rect.LeftTop()), DocToView(s.rect.RightBottom()));
	if (s.kind == PD_TEXT) {
		if (s.label.Length() > 0) {
			BFont font(be_plain_font);
			font.SetSize(s.style.textSize);
			SetFont(&font);
			SetHighColor(s.style.textColor);
			DrawString(s.label.String(), r.LeftTop()
				+ BPoint(0, s.style.textSize));
		}
		return;
	}

	SetPenSize(s.style.strokeWidth);
	if (s.style.fillOn) {
		SetHighColor(s.style.fill);
		if (s.kind == PD_RECT)
			FillRect(r);
		else if (s.kind == PD_RRECT)
			FillRoundRect(r, 8, 8);
		else if (s.kind == PD_ELLIPSE)
			FillEllipse(r);
		else if (s.kind == PD_DIAMOND) {
			BPoint pts[4] = {
				BPoint((r.left + r.right) / 2, r.top),
				BPoint(r.right, (r.top + r.bottom) / 2),
				BPoint((r.left + r.right) / 2, r.bottom),
				BPoint(r.left, (r.top + r.bottom) / 2) };
			FillPolygon(pts, 4);
		}
	}
	SetHighColor(s.style.stroke);
	pattern p = s.style.dashed ? kDash : B_SOLID_HIGH;
	if (s.style.strokeWidth > 0) {
		if (s.kind == PD_RECT)
			StrokeRect(r, p);
		else if (s.kind == PD_RRECT)
			StrokeRoundRect(r, 8, 8, p);
		else if (s.kind == PD_ELLIPSE)
			StrokeEllipse(r, p);
		else if (s.kind == PD_DIAMOND) {
			BPoint pts[4] = {
				BPoint((r.left + r.right) / 2, r.top),
				BPoint(r.right, (r.top + r.bottom) / 2),
				BPoint((r.left + r.right) / 2, r.bottom),
				BPoint(r.left, (r.top + r.bottom) / 2) };
			StrokePolygon(pts, 4, true, p);
		}
	}
	SetPenSize(1.0f);

	// the label, centred in the shape
	if (s.label.Length() > 0) {
		BFont font(be_plain_font);
		font.SetSize(s.style.textSize);
		SetFont(&font);
		float w = font.StringWidth(s.label.String());
		font_height fh;
		font.GetHeight(&fh);
		SetHighColor(s.style.textColor);
		DrawString(s.label.String(),
			BPoint((r.left + r.right) / 2 - w / 2,
				(r.top + r.bottom) / 2 + fh.ascent / 2 - fh.descent / 4));
	}
}

// ------------------------------------------------------------- selection --
void
PDCanvas::Select(const std::vector<int32>& ids)
{
	fSelection = ids;
	Invalidate();
	UpdateStatus();
}

void
PDCanvas::SelectAll()
{
	std::vector<int32> ids;
	for (int32 i = 0; i < fDoc->Count(); i++) {
		const PDShape* s = fDoc->ShapeAt(i);
		if (s->kind != PD_CONNECTOR)
			ids.push_back(s->id);
	}
	Select(ids);
}

void
PDCanvas::DeleteSelection()
{
	if (fSelection.empty())
		return;
	fDoc->RemoveShapes(fSelection);
	fSelection.clear();
	Invalidate();
	UpdateStatus();
}

void
PDCanvas::NudgeSelection(float dx, float dy)
{
	if (fSelection.empty())
		return;
	for (int32 id : fSelection) {
		PDShape* s = fDoc->ShapeById(id);
		if (s != NULL && s->kind != PD_CONNECTOR)
			fDoc->SetShapeRect(id, s->rect.OffsetByCopy(dx, dy));
	}
	Invalidate();
	UpdateStatus();
}

void
PDCanvas::DuplicateSelection()
{
	if (fSelection.empty())
		return;
	std::vector<int32> fresh;
	// copy the shapes, then the connectors among them, offset one grid
	std::map<int32, int32> remap;
	for (int32 id : fSelection) {
		PDShape* s = fDoc->ShapeById(id);
		if (s == NULL || s->kind == PD_CONNECTOR)
			continue;
		PDShape n = *s;
		n.rect.OffsetBy(fDoc->Grid(), fDoc->Grid());
		PDShape* added = fDoc->AddShape(n.kind, n.rect, n.label.String());
		added->style = n.style;
		remap[id] = added->id;
		fresh.push_back(added->id);
	}
	for (int32 id : fSelection) {
		PDShape* s = fDoc->ShapeById(id);
		if (s == NULL || s->kind != PD_CONNECTOR)
			continue;
		if (remap.count(s->fromId) && remap.count(s->toId)) {
			PDShape* c = fDoc->AddShape(PD_CONNECTOR, BRect(0, 0, 0, 0));
			c->fromId = remap[s->fromId];
			c->toId = remap[s->toId];
			c->style = s->style;
			c->arrowEnd = s->arrowEnd;
			fresh.push_back(c->id);
		}
	}
	Select(fresh);
}

BRect
PDCanvas::SelectionBounds() const
{
	BRect b;
	bool any = false;
	for (int32 id : fSelection) {
		PDShape* s = fDoc->ShapeById(id);
		if (s == NULL || s->kind == PD_CONNECTOR)
			continue;
		b = any ? (b | s->rect) : s->rect;
		any = true;
	}
	return any ? b : BRect(0, 0, 0, 0);
}

void
PDCanvas::SetSelectionRect(BRect rect)
{
	if (fSelection.size() != 1)
		return;
	PDShape* s = fDoc->ShapeById(fSelection[0]);
	if (s != NULL && s->kind != PD_CONNECTOR)
		fDoc->SetShapeRect(fSelection[0], rect);
	Invalidate();
}

void
PDCanvas::QueueConnector()
{
	// scripted: connect the two most recently added non-connector shapes
	std::vector<int32> ids;
	for (int32 i = fDoc->Count() - 1; i >= 0 && ids.size() < 2; i--) {
		const PDShape* s = fDoc->ShapeAt(i);
		if (s->kind != PD_CONNECTOR)
			ids.push_back(s->id);
	}
	if (ids.size() == 2) {
		PDShape* c = fDoc->AddShape(PD_CONNECTOR, BRect(0, 0, 0, 0));
		c->fromId = ids[1];
		c->toId = ids[0];
		fSelection.clear();
		fSelection.push_back(c->id);
		Invalidate();
		UpdateStatus();
	}
}

// ----------------------------------------------------------------- input --
int32
PDCanvas::HandleAt(BPoint viewPoint, int32* xAnchor, int32* yAnchor)
{
	if (fSelection.size() != 1)
		return 0;
	int32 idx = fDoc->IndexOf(fSelection[0]);
	if (idx < 0)
		return 0;
	const PDShape* s = fDoc->ShapeAt(idx);
	if (s->kind == PD_CONNECTOR)
		return 0;
	for (int32 hx = -1; hx <= 1; hx++)
		for (int32 hy = -1; hy <= 1; hy++) {
			if (hx == 0 && hy == 0)
				continue;
			BPoint c = HandlePos(s, hx, hy);
			if (fabsf(viewPoint.x - c.x) <= 5
				&& fabsf(viewPoint.y - c.y) <= 5) {
				*xAnchor = hx;
				*yAnchor = hy;
				return s->id;
			}
		}
	return 0;
}

void
PDCanvas::MouseDown(BPoint where)
{
	MakeFocus();
	BPoint p = ViewToDoc(where);
	int32 xAnchor = 0, yAnchor = 0;

	switch (fTool) {
		case PD_TOOL_SELECT:
		{
			int32 handleId = HandleAt(where, &xAnchor, &yAnchor);
			if (handleId != 0) {
				fDoc->PushUndo();
				fDrag = DRAG_RESIZE;
				fResizeX = xAnchor;
				fResizeY = yAnchor;
				fDragOriginal = fDoc->ShapeById(handleId)->rect;
				fDragStart = fDragLast = p;
				return;
			}
			int32 hit = fDoc->ConnectorAtPoint(p);
			if (hit == 0)
				hit = fDoc->ShapeAtPoint(p);
			if (hit != 0) {
				bool additive = (modifiers() & B_SHIFT_KEY) != 0;
				std::vector<int32> sel = fSelection;
				if (additive) {
					auto it = std::find(sel.begin(), sel.end(), hit);
					if (it != sel.end())
						sel.erase(it);
					else
						sel.push_back(hit);
				} else if (std::find(sel.begin(), sel.end(), hit)
					== sel.end()) {
					sel.clear();
					sel.push_back(hit);
				}
				Select(sel);
				// moving starts from every selected box (connectors ride)
				fMoveBase.clear();
				for (int32 id : fSelection) {
					PDShape* s = fDoc->ShapeById(id);
					if (s != NULL && s->kind != PD_CONNECTOR)
						fMoveBase.push_back(s->rect);
				}
				fDoc->PushUndo();
				fDrag = DRAG_MOVE;
				fDragStart = fDragLast = p;
				return;
			}
			if (!(modifiers() & B_SHIFT_KEY))
				Select(std::vector<int32>());
			fDrag = DRAG_BAND;
			fDragStart = fDragLast = p;
			return;
		}
		case PD_TOOL_CONNECTOR:
		{
			int32 hit = fDoc->ShapeAtPoint(p);
			if (hit != 0 && fConnectFrom == 0) {
				fConnectFrom = hit;
				fDragStart = p;
				fConnectNow = p;
				fDrag = DRAG_CONNECT;
				SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);
				return;
			}
			if (hit != 0 && hit != fConnectFrom) {
				PDShape* c = fDoc->AddShape(PD_CONNECTOR, BRect(0, 0, 0, 0));
				c->fromId = fConnectFrom;
				c->toId = hit;
				fConnectFrom = 0;
				fDrag = DRAG_NONE;
				Select(std::vector<int32>(1, c->id));
			} else if (fConnectFrom != 0) {
				fConnectFrom = 0;
				fDrag = DRAG_NONE;
				Invalidate();
			}
			return;
		}
		case PD_TOOL_TEXT:
		{
			BRect r(p, p + BPoint(160, 24));
			PDShape* s = fDoc->AddShape(PD_TEXT, r, "Text");
			Select(std::vector<int32>(1, s->id));
			return;
		}
		default:
			// shape creation tools: drag out the box
			fDrag = DRAG_CREATE;
			fDragStart = fDragLast = p;
			SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);
			return;
	}
}

void
PDCanvas::MouseMoved(BPoint point, uint32 transit, const BMessage*)
{
	if (fDrag == DRAG_NONE)
		return;
	if (transit != B_INSIDE_VIEW && transit != B_OUTSIDE_VIEW)
		return;
	fDragLast = ViewToDoc(point);
	if (fDrag == DRAG_CONNECT)
		fConnectNow = fDragLast;
	else if (fDrag == DRAG_MOVE && !fMoveBase.empty()) {
		// the whole selection moves by the grid-snapped delta of its
		// first box; one undo entry covers the gesture (PushUndo at
		// MouseDown, raw rects during, Touch at MouseUp)
		float g = fDoc->Grid();
		auto snap = [g](float v) { return roundf(v / g) * g; };
		float dx = fDragLast.x - fDragStart.x;
		float dy = fDragLast.y - fDragStart.y;
		float sdx = snap(fMoveBase[0].left + dx) - snap(fMoveBase[0].left);
		float sdy = snap(fMoveBase[0].top + dy) - snap(fMoveBase[0].top);
		for (size_t i = 0; i < fSelection.size() && i < fMoveBase.size();
				i++) {
			PDShape* s = fDoc->ShapeById(fSelection[i]);
			if (s != NULL && s->kind != PD_CONNECTOR)
				s->rect = fMoveBase[i].OffsetByCopy(sdx, sdy);
		}
	} else if (fDrag == DRAG_RESIZE) {
		BRect r = fDragOriginal;
		if (fResizeX < 0) r.left = std::min(r.right, fDragLast.x);
		if (fResizeX > 0) r.right = std::max(r.left, fDragLast.x);
		if (fResizeY < 0) r.top = std::min(r.bottom, fDragLast.y);
		if (fResizeY > 0) r.bottom = std::max(r.top, fDragLast.y);
		PDShape* s = fDoc->ShapeById(fSelection[0]);
		if (s != NULL)
			s->rect = fDoc->SnapRect(r);
	}
	Invalidate();
}

void
PDCanvas::MouseUp(BPoint)
{
	if (fDrag == DRAG_CREATE) {
		BRect r(fDragStart, fDragLast);
		if (r.Width() < 2 && r.Height() < 2)
			r = BRect(fDragStart, fDragStart + BPoint(96, 64)); // a click
		PDShapeKind kind = fTool == PD_TOOL_RECT ? PD_RECT
			: fTool == PD_TOOL_RRECT ? PD_RRECT
			: fTool == PD_TOOL_ELLIPSE ? PD_ELLIPSE : PD_DIAMOND;
		PDShape* s = fDoc->AddShape(kind, r);
		Select(std::vector<int32>(1, s->id));
	} else if (fDrag == DRAG_BAND) {
		BRect band = fDoc->SnapRect(BRect(fDragStart, fDragLast));
		std::vector<int32> sel = (modifiers() & B_SHIFT_KEY) != 0
			? fSelection : std::vector<int32>();
		for (int32 i = 0; i < fDoc->Count(); i++) {
			const PDShape* s = fDoc->ShapeAt(i);
			if (s->kind != PD_CONNECTOR && band.Contains(
					BPoint((s->rect.left + s->rect.right) / 2,
						(s->rect.top + s->rect.bottom) / 2))
				&& std::find(sel.begin(), sel.end(), s->id) == sel.end())
				sel.push_back(s->id);
		}
		Select(sel);
	} else if (fDrag == DRAG_MOVE || fDrag == DRAG_RESIZE) {
		fDoc->Touch();
		UpdateStatus();
	}
	fDrag = DRAG_NONE;
	Invalidate();
}

void
PDCanvas::KeyDown(const char* bytes, int32 numBytes)
{
	if (numBytes != 1) {
		BView::KeyDown(bytes, numBytes);
		return;
	}
	float g = fDoc->Grid();
	switch (bytes[0]) {
		case B_DELETE: case B_BACKSPACE:
			DeleteSelection();
			return;
		case B_LEFT_ARROW: NudgeSelection(-g, 0); return;
		case B_RIGHT_ARROW: NudgeSelection(g, 0); return;
		case B_UP_ARROW: NudgeSelection(0, -g); return;
		case B_DOWN_ARROW: NudgeSelection(0, g); return;
		case B_ESCAPE: Select(std::vector<int32>()); return;
		case B_ENTER:
			// hand the label to the inspector
			if (Window() != NULL)
				Window()->PostMessage('pdIl');
			return;
		default:
			BView::KeyDown(bytes, numBytes);
	}
}

void
PDCanvas::UpdateStatus()
{
	if (Window() != NULL)
		Window()->PostMessage('pdUp');
}
