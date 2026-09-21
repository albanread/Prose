// PDCanvas — the drawing surface: the page on the desk, the grid, the
// shapes, the selection, and the tool state machine.
#ifndef PD_CANVAS_H
#define PD_CANVAS_H

#include <View.h>

#include <vector>

#include "PDDocument.h"

enum PDTool : uint8 {
	PD_TOOL_SELECT, PD_TOOL_RECT, PD_TOOL_RRECT, PD_TOOL_ELLIPSE,
	PD_TOOL_DIAMOND, PD_TOOL_TEXT, PD_TOOL_CONNECTOR
};

class PDCanvas : public BView {
public:
			PDCanvas(PDDocument* doc);

	void	Draw(BRect updateRect) override;
	void	MouseDown(BPoint point) override;
	void	MouseMoved(BPoint point, uint32 transit,
				const BMessage* drag) override;
	void	MouseUp(BPoint point) override;
	void	KeyDown(const char* bytes, int32 numBytes) override;
	void	MakeFocus(bool focus = true) override;

	void	SetTool(PDTool tool) { fTool = tool; }
	PDTool	Tool() const { return fTool; }

	// zoom: 0.25..4, one place maps doc<->view, every interaction and
	// the whole render scale with it (PDF export is unaffected — it
	// reads doc space)
	void	SetZoom(float zoom);
	float	Zoom() const { return fZoom; }

	// the current selection (ids); empty = none
	const std::vector<int32>&	Selection() const { return fSelection; }
	void	Select(const std::vector<int32>& ids);
	void	SelectAll();
	void	DeleteSelection();
	void	NudgeSelection(float dx, float dy);
	void	DuplicateSelection();

	// geometry shared with the inspector: the selection's union, snapped
	BRect	SelectionBounds() const;
	void	SetSelectionRect(BRect rect);		// single selection only
	void	QueueConnector(bool elbow = false);	// scripted: connect last two

	// the page grew or shrank: resize the data extent
	void	DocumentChangedSize();

	BPoint	DocToView(BPoint p) const
	{
		return BPoint((kMargin + p.x) * fZoom,
			(kMargin + p.y) * fZoom);
	}
	BPoint	ViewToDoc(BPoint p) const
	{
		return BPoint(p.x / fZoom - kMargin, p.y / fZoom - kMargin);
	}
	static const float	kMargin;	// desk margin around the page, px

private:
	BPoint		HandlePos(const PDShape* s, int32 xAnchor, int32 yAnchor);
	enum DragMode {
		DRAG_NONE, DRAG_MOVE, DRAG_RESIZE, DRAG_CREATE, DRAG_BAND,
		DRAG_CONNECT
	};
	void		DrawShape(const PDShape& s);
	void		DrawGrid(BRect pageRect);
	void		UpdateStatus();
	int32		HandleAt(BPoint viewPoint, int32* xAnchor, int32* yAnchor);
	BRect		SelectionFrame(int32 id) const;	// view-space

	PDDocument*	fDoc;
	PDTool		fTool = PD_TOOL_SELECT;
	float		fZoom = 1.0f;
	std::vector<int32>	fSelection;
	DragMode	fDrag = DRAG_NONE;
	BPoint		fDragStart;		// doc space
	BPoint		fDragLast;		// doc space
	BRect		fDragOriginal;	// single-shape move/resize base
	int32		fResizeX = 0, fResizeY = 0;	// -1/0/+1 per axis
	int32		fConnectFrom = 0;			// connector tool's first shape
	BPoint		fConnectNow;				// rubber line's live end
	std::vector<BRect>	fMoveBase;	// multi-move originals, aligned to fSelection
};

#endif	// PD_CANVAS_H
