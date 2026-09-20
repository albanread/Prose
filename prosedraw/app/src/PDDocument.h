// PDDocument — ProseDraw's document model.
//
// A diagram is shapes on one page. Shapes carry geometry, a style and a
// label; connectors reference other shapes by stable id and re-anchor as
// those shapes move. Everything snaps to the grid through the model, so
// scripted and mouse edits land identically. Undo is whole-state
// snapshots — diagrams are small, and correctness beats cleverness.
#ifndef PD_DOCUMENT_H
#define PD_DOCUMENT_H

#include <GraphicsDefs.h>
#include <Message.h>
#include <Rect.h>
#include <String.h>

#include <cstdint>
#include <utility>
#include <vector>

enum PDShapeKind : uint8 {
	PD_RECT, PD_RRECT, PD_ELLIPSE, PD_DIAMOND, PD_TEXT, PD_CONNECTOR
};

enum PDAlign : uint8 {
	PD_ALIGN_LEFT, PD_ALIGN_HCENTER, PD_ALIGN_RIGHT,
	PD_ALIGN_TOP, PD_ALIGN_VCENTER, PD_ALIGN_BOTTOM,
	PD_DISTRIBE_H, PD_DISTRIBE_V
};

struct PDStyle {
	rgb_color	fill { 255, 255, 255, 255 };
	bool		fillOn = true;
	rgb_color	stroke { 0, 0, 0, 255 };
	float		strokeWidth = 1.0f;
	bool		dashed = false;
	float		textSize = 12.0f;
	rgb_color	textColor { 0, 0, 0, 255 };

	bool operator==(const PDStyle& o) const
	{
		return fill == o.fill && fillOn == o.fillOn && stroke == o.stroke
			&& strokeWidth == o.strokeWidth && dashed == o.dashed
			&& textSize == o.textSize && textColor == o.textColor;
	}
	void Archive(BMessage* into) const;
	void Unarchive(const BMessage* from);
};

struct PDShape {
	int32		id = 0;			// stable identity; connectors reference it
	PDShapeKind	kind = PD_RECT;
	BRect		rect;			// doc/page space, points
	BString		label;
	PDStyle		style;
	// connectors only: the shapes joined, and where the arrows are
	int32		fromId = 0;
	int32		toId = 0;
	bool		arrowEnd = true;
	bool		arrowStart = false;
};

// Papers in points at 72 dpi — one table, one truth (layout, canvas,
// and Sprint 2's PDF MediaBox all read it).
struct PDPageSetup {
	float width = 595.0f;		// A4
	float height = 842.0f;
};

class PDDocument {
public:
			PDDocument();

	// ---- shapes
	int32		Count() const { return (int32)fShapes.size(); }
	PDShape*	ShapeAt(int32 index) { return &fShapes[index]; }
	const PDShape* ShapeAt(int32 index) const { return &fShapes[index]; }
	PDShape*	ShapeById(int32 id);
	const PDShape* ShapeById(int32 id) const;
	int32		IndexOf(int32 id) const;

	// Geometry goes through SnapRect: whatever created it — mouse,
	// inspector, scripting — lands on the grid.
	PDShape*	AddShape(PDShapeKind kind, BRect rect,
				const char* label = NULL);
	void		RemoveShapes(const std::vector<int32>& ids);
	void		SetShapeRect(int32 id, BRect rect);
	void		SetShapeLabel(int32 id, const char* label);
	void		SetShapeStyle(int32 id, const PDStyle& style);
	// z-order: front = drawn last = top of the stack
	void		MoveZ(int32 id, bool toFront);

	// ---- alignment over a selection (ids); returns false when the
	// selection can't support it (align needs >= 2, distribute >= 3)
	bool		Align(const std::vector<int32>& ids, PDAlign mode);

	// ---- connectors
	// Where a connector from `from' to `to' attaches on each border —
	// the intersection of the centre-centre line with the shape box.
	static BPoint	AnchorPoint(const PDShape& from, const PDShape& to);
	// id of the topmost non-connector shape whose box contains p
	int32		ShapeAtPoint(BPoint p) const;
	// connector hit test: distance from p to the drawn segment
	int32		ConnectorAtPoint(BPoint p) const;

	// ---- grid
	float		Grid() const { return fGrid; }
	void		SetGrid(float g) { if (g >= 2) fGrid = g; }
	bool		SnapEnabled() const { return fSnap; }
	void		SetSnapEnabled(bool on) { fSnap = on; }
	bool		ShowGrid() const { return fShowGrid; }
	void		SetShowGrid(bool on) { fShowGrid = on; }
	BRect		SnapRect(BRect r) const;

	// ---- drag-style edits: one undo entry for a whole gesture
	// (Mouse-driven moves mutate shapes raw; the gesture opens with
	// PushUndo and closes with Touch)
	void		PushUndo() { Snapshot(); }
	void		Touch() { fModified = true; }

	// ---- page
	PDPageSetup	Page() const { return fPage; }
	void		SetPage(const PDPageSetup& p) { fPage = p; }

	// ---- undo (snapshots of the whole shape list)
	bool		CanUndo() const { return !fUndo.empty(); }
	bool		CanRedo() const { return !fRedo.empty(); }
	void		Undo();
	void		Redo();
	bool		IsModified() const { return fModified; }
	void		SavedClean() { fModified = false; }

	// ---- persistence: flattened BMessage, what-code 'pDd&'
	static const char* kDocType;	// application/x-vnd.prose.ProseDraw-doc
	status_t	SaveToMessage(BMessage* msg) const;
	status_t	LoadFromMessage(const BMessage* msg);
	// atomic (write beside, sync, rename) — a ProseWriter lesson
	status_t	SaveToFile(const char* path) const;
	status_t	LoadFromFile(const char* path);

private:
	void		Snapshot();		// push current state onto the undo stack
	void		Restore(std::vector<PDShape> shapes, int32 nextId);

	std::vector<PDShape>	fShapes;
	std::vector<std::pair<std::vector<PDShape>, int32> > fUndo, fRedo;
	int32		fNextId = 1;
	float		fGrid = 8.0f;
	bool		fSnap = true;
	bool		fShowGrid = true;
	PDPageSetup	fPage;
	bool		fModified = false;
};

// The loader is chosen by content, never by name (ProseWriter's
// extension bug, paid for once): our files start "HMF1" + '&','d','D','p'.
enum PDDocKind { PD_KIND_ERROR, PD_KIND_NATIVE, PD_KIND_OTHER };
PDDocKind PD_SniffDocument(const char* path);

#endif	// PD_DOCUMENT_H
