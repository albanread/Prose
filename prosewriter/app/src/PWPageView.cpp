#include "PWPageView.h"

#include <Clipboard.h>
#include <Message.h>
#include <ScrollView.h>
#include <Window.h>

#include <cstdio>
#include <cstring>
#include <algorithm>

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
	fCurrentFormat = fDoc->DefaultFormat();
	ResizeTo(PagePixelWidth(), PagePixelHeight() + 24);
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

float
PWPageView::PagePixelWidth() const
{
	return fLayout->PageSetup().pageWidth * fZoom + 2 * 24;
}

float
PWPageView::PagePixelHeight() const
{
	return fLayout->TotalHeight() * fZoom + 24;
}

void
PWPageView::SetZoom(float zoom)
{
	if (zoom < 0.4f) zoom = 0.4f;
	if (zoom > 3.0f) zoom = 3.0f;
	if (zoom == fZoom)
		return;
	fZoom = zoom;
	ResizeTo(PagePixelWidth(), PagePixelHeight());
	Invalidate();
	if (Window())
		Window()->PostMessage('pWup');
}

void
PWPageView::ApplyCharFormat(const PWCharFormat& fmt)
{
	fCurrentFormat = fmt;
	fOverrideFormat = true;
	if (HasSelection()) {
		int32 from, to;
		GetSelection(&from, &to);
		fDoc->ApplyFormat(from, to - from, fmt);
		Relayout();
	}
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

	const PWPageSetup& setup = fLayout->PageSetup();
	for (int32 p = 0; p < fLayout->CountPages(); p++) {
		float top = 24 + p * (setup.pageHeight + PWLayout::Gap()) * fZoom;
		BRect page(24, top, 24 + setup.pageWidth * fZoom,
			top + setup.pageHeight * fZoom);
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
			if (24 + line.y * fZoom < updateRect.top - 40
				|| 24 + line.y * fZoom > updateRect.bottom + 40)
				continue;
			std::vector<PWLayout::Segment> segs;
			fLayout->FillSegments(li, &segs);
			const char* text = fDoc->ParagraphText(line.para);
			for (const PWLayout::Segment& s : segs) {
				BFont font(be_plain_font);
				font.SetFamilyAndFace(s.run->format.family,
					(uint16)((s.run->format.bold ? B_BOLD_FACE : 0)
						| (s.run->format.italic ? B_ITALIC_FACE : 0)));
				font.SetSize(s.run->format.size * fZoom);
				SetFont(&font);
				SetHighColor(s.run->format.color);
				if (s.length > 0) {
					DrawString(text + s.startPara, s.length,
						DocToView(BPoint(s.x, s.baseline)));
					if (s.run->format.underline) {
						float ux = DocToView(BPoint(s.x, s.baseline)).x;
						float uy = 24 + s.baseline * fZoom + 2 * fZoom;
						StrokeLine(BPoint(ux, uy),
							BPoint(ux + font.StringWidth(text + s.startPara,
								s.length), uy));
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
		r.left = 24 + a.x * fZoom;
		r.right = (li == endLine) ? 24 + b.x * fZoom
			: 24 + (line.x + line.width) * fZoom;
		r.top = 24 + (line.y + line.baseline - line.height * 0.8f) * fZoom;
		r.bottom = r.top + line.height * fZoom;
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
	StrokeLine(DocToView(BPoint(p.x, p.y - h * 0.75f)),
		DocToView(BPoint(p.x, p.y + h * 0.25f)));
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
			BPoint v = DocToView(p);
			BRect r(v.x - 1, v.y - h * fZoom, v.x + 1, v.y + 2);
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
	fDoc->Insert(fCaret, BString(text, length).String(), &fCurrentFormat);
	fCaret += length;
	fSelAnchor = -1;
	fOverrideFormat = false;	// the format has been used; moves re-sync
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
	if (!fOverrideFormat)
		fCurrentFormat = fDoc->FormatAt(offset);
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

	// Prose styled flavour: the selection's runs, offsets relative to the
	// selection start, so a paste inside ProseWriter keeps character styles.
	int32 from, to;
	GetSelection(&from, &to);
	clip->RemoveName("pWrun");
	int32 para, inPara;
	fDoc->Locate(from, &para, &inPara);
	int32 covered = 0;
	int32 paraLen = fDoc->ParagraphLength(para);
	while (covered < to - from) {
		int32 take = paraLen - inPara;
		if (take > to - from - covered)
			take = to - from - covered;
		const std::vector<PWRun>& runs = fDoc->ParagraphRuns(para);
		for (const PWRun& r : runs) {
			int32 runEnd = r.start + r.length;
			if (runEnd <= inPara || r.start >= inPara + take)
				continue;
			int32 s = std::max(r.start, inPara);
			int32 e = std::min(runEnd, inPara + take);
			BMessage runMsg('pWr&');
			runMsg.AddInt32("start", covered + (s - inPara));
			runMsg.AddInt32("length", e - s);
			r.format.Archive(&runMsg);
			clip->AddMessage("pWrun", &runMsg);
		}
		covered += take;
		if (covered < to - from) {
			covered += 1;	// the paragraph separator
			para++;
			if (para < fDoc->CountParagraphs()) {
				inPara = 0;
				paraLen = fDoc->ParagraphLength(para);
				continue;
			}
			break;
		}
	}
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
	int32 insertedAt = fCaret;
	fDoc->Insert(insertedAt, text.String(), &fCurrentFormat);
	// Prose styled flavour wins when present
	BMessage runMsg;
	for (int32 i = 0; clip->FindMessage("pWrun", i, &runMsg) == B_OK; i++) {
		int32 start = 0, length = 0;
		runMsg.FindInt32("start", &start);
		runMsg.FindInt32("length", &length);
		PWCharFormat fmt;
		fmt.Unarchive(&runMsg);
		if (length > 0 && insertedAt + start + length <= fDoc->Length())
			fDoc->ApplyFormat(insertedAt + start, length, fmt);
	}
	fCaret = insertedAt + text.Length();
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
	BPoint docPoint = ViewToDoc(where);
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
	int32 at = fLayout->XYToOffset(ViewToDoc(point));
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
	BPoint view = DocToView(p);
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
