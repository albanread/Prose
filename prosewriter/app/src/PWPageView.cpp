#include "PWPageView.h"

#include <Clipboard.h>
#include <Message.h>
#include <ScrollView.h>
#include <Window.h>

#include <cstdio>
#include <cstring>

static const bigtime_t kBlinkInterval = 500000;	// 500 ms

PWPageView::PWPageView(PWDocument* doc, PWLayout* layout)
	:
	BView(BRect(0, 0, 200, 200), "pageView", B_FOLLOW_NONE,
		B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE | B_PULSE_NEEDED),
	fDoc(doc),
	fLayout(layout)
{
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	SetDrawingMode(B_OP_COPY);
	SetEventMask(0);
}

PWPageView::~PWPageView() {}

void
PWPageView::AttachedToWindow()
{
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	fScrollView = dynamic_cast<BScrollView*>(Parent());
	// room for the pages plus a desk border
	ResizeTo(fLayout->PageSetup().pageWidth + 2 * 24,
		fLayout->TotalHeight() + 2 * 24);
}

void
PWPageView::MakeFocus(bool focus)
{
	BView::MakeFocus(focus);
	Invalidate();
}

void
PWPageView::FrameResized(float, float)
{
	// keep the scroll view's data extent in step
	if (fScrollView)
		fScrollView->ScrollBar(B_VERTICAL)->SetRange(0,
			fLayout->TotalHeight() + 48 - Bounds().Height());
}

void
PWPageView::Relayout()
{
	fLayout->Layout();
	Invalidate();
	Window()->PostMessage('pWup');	// tell the window to refresh status text
}

// ------------------------------------------------------------------ draw --
void
PWPageView::Draw(BRect updateRect)
{
	DrawPages(updateRect);
	DrawSelection();
	DrawCaret();
}

void
PWPageView::DrawPages(BRect updateRect)
{
	SetHighColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	FillRect(updateRect);

	for (int32 p = 0; p < fLayout->CountPages(); p++) {
		BRect page = fLayout->PageBounds(p).OffsetByCopy(24, 24);
		if (!page.Intersects(updateRect))
			continue;
		// shadow first, page on top
		SetHighColor(tint_color(ui_color(B_PANEL_BACKGROUND_COLOR), B_DARKEN_2_TINT));
		FillRect(page.OffsetByCopy(3, 3));
		SetHighColor(ui_color(B_DOCUMENT_BACKGROUND_COLOR));
		FillRect(page);

		// Text lines on this page.
		SetLowColor(ui_color(B_DOCUMENT_BACKGROUND_COLOR));
		for (int32 li = 0; li < (int32)fLayout->Lines().size(); li++) {
			const PWLayout::Line& line = fLayout->Lines()[li];
			if (line.y + 24 < updateRect.top - 40
				|| line.y + 24 > updateRect.bottom + 40)
				continue;
			std::vector<PWLayout::Segment> segs;
			fLayout->FillSegments(li, &segs);
			const char* text = fDoc->ParagraphText(line.para);
			for (const PWLayout::Segment& s : segs) {
				BFont font(be_plain_font);
				font.SetFamilyAndFace(s.run->format.family,
					(uint16)((s.run->format.bold ? B_BOLD_FACE : 0)
						| (s.run->format.italic ? B_ITALIC_FACE : 0)));
				font.SetSize(s.run->format.size);
				SetFont(&font);
				SetHighColor(s.run->format.color);
				if (s.length > 0) {
					DrawString(text + s.startPara, s.length,
						BPoint(s.x + 24, s.baseline + 24));
					if (s.run->format.underline) {
						SetHighColor(s.run->format.color);
						StrokeLine(
							BPoint(s.x + 24, s.baseline + 24 + 2),
							BPoint(s.x + 24 + font.StringWidth(text + s.startPara,
								s.length), s.baseline + 24 + 2));
					}
				}
			}
		}
	}
}

void
PWPageView::DrawSelection()
{
	if (!HasSelection())
		return;
	int32 from, to;
	GetSelection(&from, &to);
	// Selection spans lines; invert the run rectangles per line.
	SetDrawingMode(B_OP_INVERT);
	int32 startLine = fLayout->LineOfOffset(from);
	int32 endLine = fLayout->LineOfOffset(to - 1 >= from ? to - 1 : from);
	for (int32 li = startLine; li <= endLine && li >= 0; li++) {
		const PWLayout::Line& line = fLayout->Lines()[li];
		BPoint a, b;
		float ha, hb;
		int32 lineS = fLayout->LineStart(li);
		int32 lineE = fLayout->LineEnd(li);
		if (!fLayout->OffsetToXY(std::max(from, lineS), &a, &ha))
			continue;
		if (!fLayout->OffsetToXY(std::min(to, lineE), &b, &hb))
			continue;
		BRect r;
		r.left = a.x + 24;
		r.right = (li == endLine) ? b.x + 24
			: line.x + line.width + 24;
		r.top = line.y + 24 + line.baseline - line.height * 0.8f;
		r.bottom = r.top + line.height;
		if (r.right < r.left)
			continue;
		FillRect(r);
	}
	SetDrawingMode(B_OP_COPY);
}

void
PWPageView::DrawCaret()
{
	if (!fCaretVisible || !Window()->IsActive())
		return;
	BPoint p;
	float h;
	if (!fLayout->OffsetToXY(fCaret, &p, &h))
		return;
	SetDrawingMode(B_OP_INVERT);
	StrokeLine(BPoint(p.x + 24, p.y - h * 0.75f + 24),
		BPoint(p.x + 24, p.y + h * 0.25f + 24));
	SetDrawingMode(B_OP_COPY);
}

void
PWPageView::Pulse()
{
	bigtime_t now = system_time();
	if (now - fLastCaretBlink >= kBlinkInterval) {
		fLastCaretBlink = now;
		fCaretVisible = !fCaretVisible;
		BPoint p;
		float h;
		if (fLayout->OffsetToXY(fCaret, &p, &h)) {
			BRect r(p.x + 24 - 1, p.y - h + 24, p.x + 24 + 1, p.y + 24 + 2);
			Invalidate(r);
		}
	}
}

void
PWPageView::WindowActivated(bool active)
{
	BView::WindowActivated(active);
	fCaretVisible = active;
	Invalidate();
}

// ----------------------------------------------------------------- input --
void
PWPageView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case B_SELECT_ALL:
			Select(0, fDoc->Length());
			break;
		default:
			BView::MessageReceived(message);
	}
}

void
PWPageView::KeyDown(const char* bytes, int32 numBytes)
{
	uint32 mods = modifiers();
	if (numBytes == 1 && (bytes[0] & 0x80) == 0 && bytes[0] >= 32
		&& !(mods & (B_COMMAND_KEY | B_CONTROL_KEY | B_OPTION_KEY))) {
		HandlePrintableChar(bytes, numBytes);
		return;
	}
	HandleNavigationKey(bytes, mods);
}

void
PWPageView::HandlePrintableChar(const char* bytes, int32 numBytes)
{
	if (HasSelection())
		DeleteSelection();
	InsertText(bytes, numBytes);
}

void
PWPageView::InsertText(const char* text, int32 length)
{
	fDoc->Insert(fCaret, BString(text, length).String(), NULL);
	fCaret += length;
	fSelAnchor = -1;
	Relayout();
	ScrollCaretVisible();
}

void
PWPageView::DeleteSelection()
{
	if (!HasSelection())
		return;
	int32 from, to;
	GetSelection(&from, &to);
	fDoc->Remove(from, to - from);
	fCaret = from;
	fSelAnchor = -1;
	Relayout();
	ScrollCaretVisible();
}

void
PWPageView::HandleNavigationKey(const char* bytes, int32 mods)
{
	bool select = (mods & B_SHIFT_KEY) != 0;
	bool wordWise = (mods & B_COMMAND_KEY) != 0;	// Alt = word-wise
	int32 docLen = fDoc->Length();
	int32 newCaret = fCaret;
	const char* text = fDoc->PlainText();

	switch (bytes[0]) {
		case B_LEFT_ARROW: {
			if (wordWise) {
				newCaret = fCaret;
				while (newCaret > 0 && text[newCaret - 1] == ' ') newCaret--;
				while (newCaret > 0 && text[newCaret - 1] != ' '
					&& text[newCaret - 1] != '\n') newCaret--;
			} else {
				newCaret = fCaret;
				// step back one UTF-8 character
				do { newCaret--; } while (newCaret > 0
					&& (text[newCaret] & 0xC0) == 0x80);
			}
			break;
		}
		case B_RIGHT_ARROW: {
			if (wordWise) {
				newCaret = fCaret;
				while (newCaret < docLen && text[newCaret] == ' ') newCaret++;
				while (newCaret < docLen && text[newCaret] != ' '
					&& text[newCaret] != '\n') newCaret++;
			} else {
				newCaret = fCaret;
				do { newCaret++; } while (newCaret < docLen
					&& (text[newCaret] & 0xC0) == 0x80);
			}
			break;
		}
		case B_UP_ARROW:
			newCaret = fLayout->PrevLineStart(fCaret);
			break;
		case B_DOWN_ARROW:
			newCaret = fLayout->NextLineStart(fCaret);
			break;
		case B_HOME:
			newCaret = wordWise ? 0 : fLayout->LineStart(fLayout->LineOfOffset(fCaret));
			break;
		case B_END:
			newCaret = wordWise ? docLen : fLayout->LineEnd(fLayout->LineOfOffset(fCaret)) - 1;
			if (newCaret < 0) newCaret = 0;
			break;
		case B_PAGE_UP: {
			BPoint p; float h;
			fLayout->OffsetToXY(fCaret, &p, &h);
			newCaret = fLayout->XYToOffset(BPoint(p.x,
				p.y - fLayout->PageSetup().TextHeight()));
			break;
		}
		case B_PAGE_DOWN: {
			BPoint p; float h;
			fLayout->OffsetToXY(fCaret, &p, &h);
			newCaret = fLayout->XYToOffset(BPoint(p.x,
				p.y + fLayout->PageSetup().TextHeight()));
			break;
		}
		case B_BACKSPACE:
			if (HasSelection()) { DeleteSelection(); return; }
			if (fCaret > 0) {
				int32 back = fCaret - 1;
				const char* t = fDoc->PlainText();
				while (back > 0 && (t[back] & 0xC0) == 0x80) back--;
				fDoc->Remove(back, fCaret - back);
				fCaret = back;
				Relayout();
				ScrollCaretVisible();
			}
			return;
		case B_DELETE:
			if (HasSelection()) { DeleteSelection(); return; }
			if (fCaret < docLen) {
				int32 fwd = fCaret + 1;
				const char* t = fDoc->PlainText();
				while (fwd < docLen && (t[fwd] & 0xC0) == 0x80) fwd++;
				fDoc->Remove(fCaret, fwd - fCaret);
				Relayout();
			}
			return;
		case B_ENTER:
			if (HasSelection()) DeleteSelection();
			fDoc->SplitPara(fCaret);
			fCaret += 1;
			fSelAnchor = -1;
			Relayout();
			ScrollCaretVisible();
			return;
		case B_TAB:
			if (HasSelection()) DeleteSelection();
			// A soft tab: four spaces until the ruler arrives in sprint 2.
			InsertText("    ", 4);
			return;
		case B_ESCAPE:
			fSelAnchor = -1;
			Invalidate();
			return;
		default:
			BView::KeyDown(bytes, mods);
			return;
	}

	if (newCaret < 0) newCaret = 0;
	if (newCaret > docLen) newCaret = docLen;
	SetCaret(newCaret, select);
}

void
PWPageView::SetCaret(int32 offset, bool extend)
{
	int32 docLen = fDoc->Length();
	if (offset < 0) offset = 0;
	if (offset > docLen) offset = docLen;
	bool hadSelection = HasSelection();
	fCaret = offset;
	if (extend) {
		if (fSelAnchor < 0)
			fSelAnchor = offset == 0 ? 0 : fCaret;
	} else
		fSelAnchor = -1;
	fCaretVisible = true;
	fLastCaretBlink = system_time();
	Invalidate();
	ScrollCaretVisible();
	if (hadSelection || HasSelection())
		Window()->PostMessage('pWup');
	else
		Window()->PostMessage('pWup');
}

void
PWPageView::Select(int32 from, int32 to)
{
	if (from > to) { int32 t = from; from = to; to = t; }
	fSelAnchor = from;
	fCaret = to;
	Invalidate();
	Window()->PostMessage('pWup');
}

void
PWPageView::GetSelection(int32* from, int32* to) const
{
	if (fSelAnchor < 0 || fSelAnchor == fCaret) {
		*from = *to = fCaret;
		return;
	}
	if (fSelAnchor < fCaret) { *from = fSelAnchor; *to = fCaret; }
	else { *from = fCaret; *to = fSelAnchor; }
}

void
PWPageView::GetSelectionText(BString* out) const
{
	int32 from, to;
	GetSelection(&from, &to);
	fDoc->GetText(from, to - from, out);
}

// -------------------------------------------------------------- clipboard --
void
PWPageView::Cut(BMessage* clip)
{
	Copy(clip);
	DeleteSelection();
}

void
PWPageView::Copy(BMessage* clip)
{
	if (!HasSelection())
		return;
	BString text;
	GetSelectionText(&text);
	clip->what = B_MIME_DATA;
	clip->RemoveName("text/plain");
	clip->AddData("text/plain", B_MIME_TYPE, text.String(), text.Length());
}

void
PWPageView::Paste(const BMessage* clip)
{
	const void* data = NULL;
	ssize_t size = 0;
	if (clip->FindData("text/plain", B_MIME_TYPE, &data, &size) != B_OK)
		return;
	if (HasSelection())
		DeleteSelection();
	BString text((const char*)data, (int32)size);
	fDoc->Insert(fCaret, text.String(), NULL);
	fCaret += text.Length();
	fSelAnchor = -1;
	Relayout();
	ScrollCaretVisible();
}

// ------------------------------------------------------------------ mouse --
void
PWPageView::MouseDown(BPoint where)
{
	MakeFocus();
	int32 clicks = 0;
	bigtime_t now = system_time();
	if (Window()->CurrentMessage()->FindInt32("clicks", &clicks) == B_OK
		&& where == fLastClick && now - fLastClickTime < 400000)
		fClickCount++;
	else
		fClickCount = 1;
	fLastClick = where;
	fLastClickTime = now;
	ClickCycled(where, fClickCount);
	SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);
	fMouseSelecting = true;
}

void
PWPageView::ClickCycled(BPoint where, int32 clicks)
{
	BPoint docPoint(where.x - 24, where.y - 24);
	switch (clicks) {
		case 1:
			SetCaret(fLayout->XYToOffset(docPoint),
				(modifiers() & B_SHIFT_KEY) != 0);
			break;
		case 2: {
			// word: expand around the clicked offset
			int32 at = fLayout->XYToOffset(docPoint);
			const char* text = fDoc->PlainText();
			int32 docLen = fDoc->Length();
			int32 from = at, to = at;
			while (from > 0 && text[from - 1] != ' ' && text[from - 1] != '\n')
				from--;
			while (to < docLen && text[to] != ' ' && text[to] != '\n')
				to++;
			Select(from, to);
			break;
		}
		case 3: {
			// paragraph
			int32 at = fLayout->XYToOffset(docPoint);
			int32 from = at, to = at;
			const char* text = fDoc->PlainText();
			while (from > 0 && text[from - 1] != '\n') from--;
			while (to < fDoc->Length() && text[to] != '\n') to++;
			Select(from, to);
			break;
		}
		default:
			SetCaret(fLayout->XYToOffset(docPoint), false);
			break;
	}
}

void
PWPageView::MouseMoved(BPoint point, uint32 transit, const BMessage*)
{
	if (!fMouseSelecting || transit != B_INSIDE_VIEW)
		return;
	int32 at = fLayout->XYToOffset(BPoint(point.x - 24, point.y - 24));
	if (at != fCaret)
		SetCaret(at, true);
}

void
PWPageView::MouseUp(BPoint)
{
	fMouseSelecting = false;
}

void
PWPageView::ScrollCaretVisible()
{
	BPoint p;
	float h;
	if (!fLayout->OffsetToXY(fCaret, &p, &h))
		return;
	BPoint view(p.x + 24, p.y + 24);
	if (fScrollView) {
		BScrollView* sc = fScrollView;
		BScrollBar* v = sc->ScrollBar(B_VERTICAL);
		BScrollBar* hz = sc->ScrollBar(B_HORIZONTAL);
		if (v) {
			float val = v->Value();
			float height = Bounds().Height();
			if (view.y - h < val + 8)
				v->SetValue(std::max(0.0f, view.y - h - 16));
			else if (view.y > val + height - 24)
				v->SetValue(view.y - height + 48);
		}
		if (hz) {
			float val = hz->Value();
			float width = Bounds().Width();
			if (view.x < val + 8)
				hz->SetValue(std::max(0.0f, view.x - 24));
			else if (view.x > val + width - 24)
				hz->SetValue(view.x - width + 48);
		}
	}
}
