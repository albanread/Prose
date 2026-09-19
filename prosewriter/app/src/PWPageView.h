// PWPageView — renders the pages of a PWLayout and takes text input.
#ifndef PW_PAGEVIEW_H
#define PW_PAGEVIEW_H

#include <ScrollView.h>
#include <View.h>

#include <vector>

#include "PWLayout.h"


class PWPageView : public BView {
public:
			PWPageView(PWDocument* doc, PWLayout* layout);

	void	AttachedToWindow() override;
	void	Draw(BRect updateRect) override;
	void	KeyDown(const char* bytes, int32 numBytes) override;
	void	MouseDown(BPoint point) override;
	void	MouseMoved(BPoint point, uint32 transit, const BMessage* drag) override;
	void	MouseUp(BPoint point) override;
	void	Pulse() override;
	void	WindowActivated(bool active) override;
	void	FrameResized(float width, float height) override;
	void	MessageReceived(BMessage* message) override;
	void	MakeFocus(bool focus = true) override;

	int32	CaretOffset() const { return fCaret; }
	void	SetCaret(int32 offset, bool select);
	void	Select(int32 from, int32 to);
	bool	HasSelection() const
				{ return fSelAnchor >= 0 && fSelAnchor != fCaret; }
	void	GetSelection(int32* from, int32* to) const;
	void	GetSelectionText(BString* out) const;

	void	Cut(BMessage* intoClipboard);
	void	Copy(BMessage* intoClipboard);
	void	Paste(const BMessage* fromClipboard);

	void	ScrollCaretVisible();
	void	Relayout();			// document changed: relayout + repaint

protected:
			PWPageView(BMessage* archive);
			~PWPageView();

private:
	void	HandlePrintableChar(const char* bytes, int32 numBytes);
	void	HandleNavigationKey(const char* bytes, int32 modifiers);
	void	InsertText(const char* text, int32 length);
	void	DeleteSelection();
	void	DrawPages(BRect updateRect);
	void	DrawSelection();
	void	DrawCaret();
	void	ClickCycled(BPoint where, int32 clicks);

	PWDocument*	fDoc;
	BScrollView*	fScrollView = NULL;
	PWLayout*	fLayout;
	int32		fCaret = 0;
	int32		fSelAnchor = -1;	// -1: no selection
	bool		fCaretVisible = true;
	bigtime_t	fLastCaretBlink = 0;
	BPoint		fLastClick;
	int32		fClickCount = 0;
	bigtime_t	fLastClickTime = 0;
	bool		fMouseSelecting = false;
};

#endif	// PW_PAGEVIEW_H
