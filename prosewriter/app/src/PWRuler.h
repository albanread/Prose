// PWRuler — the top ruler: inch ticks, and draggable page margins.
#ifndef PW_RULER_H
#define PW_RULER_H

#include <View.h>

class PWLayout;
class PWPageView;

class PWRuler : public BView {
public:
			PWRuler(BRect frame, PWLayout* layout, PWPageView* pageView);

	void	Draw(BRect updateRect) override;
	void	MouseDown(BPoint point) override;
	void	MouseMoved(BPoint point, uint32 transit, const BMessage* msg) override;
	void	MouseUp(BPoint point) override;

	static const float kHeight;

private:
	float	ContentLeftPx() const;	// view x of the text column's left edge
	float	ContentRightPx() const;
	void	SetMargins(float leftDoc, float rightDoc);

	PWLayout*		fLayout;
	PWPageView*		fPageView;
	int8			fDragging = 0;	// 0 none, 1 left margin, 2 right margin
	BPoint			fDragStart;
	float			fDragLeft = 0, fDragRight = 0;
};

#endif	// PW_RULER_H
