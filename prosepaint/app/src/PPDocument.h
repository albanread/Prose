// PPDocument — ProsePaint's document model.
//
// A painting is layers of RGBA pixels over a paper-sized canvas.
// All blending is plain arithmetic on straight (non-premultiplied)
// RGBA bytes — no drawing modes, no kit magic — so the canvas, the
// PDF exporter and the tests all share one truth. Undo is per-stroke
// dirty rectangles: only the touched pixels are remembered.
#ifndef PP_DOCUMENT_H
#define PP_DOCUMENT_H

#include <Bitmap.h>
#include <GraphicsDefs.h>
#include <Message.h>
#include <Rect.h>
#include <String.h>

#include <cstdint>
#include <memory>
#include <vector>

struct PPLayer {
	int32	id = 0;
	BString	name;
	bool	visible = true;
	uint8	opacity = 255;		// layer opacity, 0..255
	std::shared_ptr<BBitmap>	bits;	// B_RGBA32, canvas-sized
};

// Paper presets in points at 72 dpi -> pixels at the doc's dpi.
struct PPPaper {
	const char*	name = "A4";
	float	widthPt = 595.0f;
	float	heightPt = 842.0f;
};

class PPDocument {
public:
			PPDocument();

	// ---- canvas
	int32	Width() const { return fWidth; }
	int32	Height() const { return fHeight; }
	float	DPI() const { return fDPI; }
	// paper for the PDF MediaBox
	PPPaper	Paper() const { return fPaper; }
	void	SetCanvas(int32 width, int32 height, float dpi,
				const PPPaper& paper, bool whiteBackground);

	// ---- layers (index 0 = bottom)
	int32		CountLayers() const { return (int32)fLayers.size(); }
	PPLayer*	LayerAt(int32 index);
	PPLayer*	LayerById(int32 id);
	int32		IndexOf(int32 id) const;
	// returns the new layer's id
	int32		AddLayer(const char* name = NULL);
	void		DeleteLayer(int32 id);
	void		MoveLayer(int32 id, bool up);
	void		RenameLayer(int32 id, const char* name);
	void		SetLayerVisible(int32 id, bool visible);
	void		SetLayerOpacity(int32 id, uint8 opacity);
	// the painting target: topmost layer by convention (the UI keeps
	// it that way); painting goes through StrokeAt/EraseAt so the
	// dirty rectangle is tracked
	void		SetActiveLayer(int32 id);
	PPLayer*	ActiveLayer() { return fActiveId ? LayerById(fActiveId) : NULL; }
	// flood fill the active layer from (x,y): pixels within tolerance
	// (per channel, on the active layer) take the colour; one stroke
	int32		FillAt(int32 x, int32 y, rgb_color colour, uint8 tolerance);
	// the composite colour at a pixel (what the eyedropper sees)
	rgb_color	PickColour(int32 x, int32 y) const;

	// ---- painting (brush dab onto the active layer at canvas px)
	// dab: blend colour into the layer under mask*a; erase: alpha-out
	// under mask*a. Both record the dirty rect and one undo step per
	// stroke (StrokeBegin ... dabs ... StrokeEnd).
	void		StrokeBegin();
	void		StrokeEnd();
	void		DabColour(int32 x, int32 y, rgb_color colour,
				const uint8* mask, int32 maskSize, int32 maskBpr,
				uint8 flow);
	void		DabErase(int32 x, int32 y,
				const uint8* mask, int32 maskSize, int32 maskBpr,
				uint8 flow);
	// smudge: drag the ink. The footprint buffer (the last stamp's
	// picked-up colour) lerps toward what's under the brush each step
	// and is written back — the classic mixing behaviour.
	void		DabSmudge(int32 x, int32 y,
				const uint8* mask, int32 maskSize, int32 maskBpr,
				uint8 strength);
	BRect		DirtyRect() const { return fDirty; }
	void		SetModified() { fModified = true; }

	// ---- composite (over-operator, layer visibility + opacity,
	// onto the given target bitmap; transparent where nothing paints)
	// The display cache: recomputed only while fCompositeDirty.
	const BBitmap*	Composite();

	// ---- undo (per-stroke dirty rectangles)
	bool	CanUndo() const { return !fUndo.empty(); }
	bool	CanRedo() const { return !fRedo.empty(); }
	void	Undo();
	void	Redo();

	bool	IsModified() const { return fModified; }
	void	SavedClean() { fModified = false; }

	// ---- persistence: flattened BMessage, what-code 'pPt&'
	static const char* kDocType;	// application/x-vnd.prose.ProsePaint-doc
	status_t	SaveToMessage(BMessage* msg) const;
	status_t	LoadFromMessage(const BMessage* msg);
	// atomic (write beside, sync, rename)
	status_t	SaveToFile(const char* path) const;
	status_t	LoadFromFile(const char* path);

private:
	struct UndoStep
	{
		int32	layerId = 0;
		BRect	rect;				// integer canvas pixels
		std::vector<uint8>	before;	// rect's bytes, before
		std::vector<uint8>	after;	// rect's bytes, after
	};
	void	CaptureAfter(UndoStep& step);
	void	ApplyStep(const UndoStep& step, bool forward);
	void	MarkDirty(const BRect& r);
	int32	fWidth = 794;			// A4 @ 96 dpi
	int32	fHeight = 1123;
	float	fDPI = 96.0f;
	PPPaper	fPaper;

	std::vector<PPLayer>	fLayers;
	int32	fNextLayerId = 1;
	int32	fActiveId = 0;

	std::shared_ptr<BBitmap>	fComposite;
	bool	fCompositeDirty = true;

	// stroke state
	BRect			fDirty;			// this stroke's dirty rect
	bool			fInStroke = false;
	UndoStep		fCurrentStep;
	// the layer as it was before the stroke: a full copy taken at
	// StrokeBegin (the dirty rect only grows as dabs land, so the
	// "before" bytes cannot be captured piecemeal)
	std::shared_ptr<BBitmap>	fBeforeSnap;
	std::vector<uint8>	fSmudgeFoot;	// footprint, maskSize²·4 bytes
	std::vector<UndoStep>	fUndo, fRedo;
	bool	fModified = false;
};

// The loader is chosen by content, never by name: our files start
// "HMF1" + 't','P','p' (the what-code 'pPt&', little-endian).
enum PPDocKind { PP_KIND_ERROR, PP_KIND_NATIVE, PP_KIND_OTHER };
PPDocKind PP_SniffDocument(const char* path);

#endif	// PP_DOCUMENT_H
