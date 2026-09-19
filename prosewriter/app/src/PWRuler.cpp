#include "PWRuler.h"

#include <Message.h>
#include <Window.h>

#include "PWLayout.h"
#include "PWPageView.h"

const float PWRuler::kHeight = 22.0f;

PWRuler::PWRuler(BRect frame, PWLayout* layout, PWPageView* pageView)
	:
	BView(frame, "ruler", B_FOLLOW_LEFT_RIGHT, B_WILL_DRAW),
	fLayout(layout),
	fPageView(pageView)
{
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	SetExplicitMinSize(BSize(100, kHeight));
	SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, kHeight));
}

float
PWRuler::ContentLeftPx() const
{
	return 24 + fLayout->PageSetup().marginLeft * fPageView->Zoom();
}

float
PWRuler::ContentRightPx() const
{
	const PWPageSetup& s = fLayout->PageSetup();
	return 24 + (s.pageWidth - s.marginRight) * fPageView->Zoom();
}

void
PWRuler::SetMargins(float leftDoc, float rightDoc)
{
	PWPageSetup s = fLayout->PageSetup();
	if (leftDoc >= 0) s.marginLeft = leftDoc;
	if (rightDoc >= 0) s.marginRight = rightDoc;
	fLayout->SetPageSetup(s);
	if (Window())
		Window()->PostMessage('pWmg');
}

void
PWRuler::Draw(BRect)
{
	SetHighColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	FillRect(Bounds());
	float zoom = fPageView->Zoom();
	const PWPageSetup& s = fLayout->PageSetup();

	// Ticks over the text column, one every 1/8", labels on the inches.
	float column = s.TextWidth();
	SetHighColor(ui_color(B_CONTROL_TEXT_COLOR));
	SetLowColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	for (float at = 0; at <= column + 0.5f; at += 9.0f) {
		float x = 24 + (s.marginLeft + at) * zoom;
		float h = 4;
		bool inch = fmod(at, 72.0f) < 0.01f;
		if (inch)
			h = 10;
		else if (fmod(at, 36.0f) < 0.01f)
			h = 7;
		StrokeLine(BPoint(x, kHeight - 3 - h), BPoint(x, kHeight - 3));
		if (inch && at > 0 && at < column) {
			char label[8];
			snprintf(label, sizeof(label), "%d", (int)(at / 72.0f));
			SetFont(be_plain_font);
			DrawString(label, BPoint(x + 2, kHeight - 13));
		}
	}

	// Margin handles: a small triangle hanging into the ruler.
	float left = ContentLeftPx();
	float right = ContentRightPx();
	for (float x : { left, right }) {
		BPoint triangle[3] = {
			BPoint(x - 5, 2), BPoint(x + 5, 2), BPoint(x, 10)
		};
		SetHighColor(ui_color(B_CONTROL_HIGHLIGHT_COLOR));
		FillPolygon(triangle, 3);
		SetHighColor(ui_color(B_CONTROL_TEXT_COLOR));
		StrokePolygon(triangle, 3);
	}
}

void
PWRuler::MouseDown(BPoint point)
{
	float left = ContentLeftPx();
	float right = ContentRightPx();
	if (fabs(point.x - left) <= 6)
		fDragging = 1;
	else if (fabs(point.x - right) <= 6)
		fDragging = 2;
	else
		return;
	fDragStart = point;
	fDragLeft = fLayout->PageSetup().marginLeft;
	fDragRight = fLayout->PageSetup().marginRight;
	SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);
}

void
PWRuler::MouseMoved(BPoint point, uint32 transit, const BMessage*)
{
	if (!fDragging || transit != B_INSIDE_VIEW)
		return;
	float zoom = fPageView->Zoom();
	float dx = (point.x - fDragStart.x) / zoom;
	const PWPageSetup& s = fLayout->PageSetup();
	if (fDragging == 1) {
		float left = fDragLeft + dx;
		if (left < 18) left = 18;
		if (left > s.pageWidth - s.marginRight - 72)
			left = s.pageWidth - s.marginRight - 72;
		SetMargins(left, -1);
	} else {
		float right = fDragRight - dx;
		if (right < 18) right = 18;
		if (right > s.pageWidth - s.marginLeft - 72)
			right = s.pageWidth - s.marginLeft - 72;
		SetMargins(-1, right);
	}
	Invalidate();
	fPageView->Relayout();
}

void
PWRuler::MouseUp(BPoint)
{
	fDragging = 0;
}
