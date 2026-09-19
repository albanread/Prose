// PWLayout — paragraphs to lines to pages.
//
// Works in 72-dpi points. Page size and margins come from PWPageSetup.
// Lines record the paragraph they came from, their start offset within it,
// their length in bytes, and their height/baseline from the tallest font on
// the line. Pages are just line ranges. The view scales and offsets these.
#ifndef PW_LAYOUT_H
#define PW_LAYOUT_H

#include <Font.h>
#include <Point.h>
#include <Rect.h>

#include <vector>

#include "PWDocument.h"

struct PWPageSetup {
	float	pageWidth = 595.0f;	// A4 at 72 dpi
	float	pageHeight = 842.0f;
	float	marginLeft = 72.0f;
	float	marginRight = 72.0f;
	float	marginTop = 72.0f;
	float	marginBottom = 72.0f;

	float	TextWidth() const { return pageWidth - marginLeft - marginRight; }
	float	TextHeight() const { return pageHeight - marginTop - marginBottom; }
};

class PWLayout {
public:
	explicit PWLayout(const PWDocument* doc);

	void	SetPageSetup(const PWPageSetup& setup);
	const PWPageSetup& PageSetup() const { return fSetup; }

	// Recompute everything. Cheap enough for now (see plan's perf budget);
	// incremental relayout from a paragraph index when it starts to matter.
	void	Layout();

	int32		CountPages() const { return (int32)fPages.size(); }
	BRect		PageBounds(int32 page) const;		// document space, stacked
	int32		PageOfOffset(int32 offset) const;

	// Document-space point of the caret's hot spot (between characters),
	// and its line height. Returns false if the offset is out of range.
	bool	OffsetToXY(int32 offset, BPoint* xy, float* caretHeight) const;
	// Hit-test. The returned offset is a valid caret position (UTF-8
	// character boundary or paragraph end).
	int32	XYToOffset(BPoint p) const;

	struct Line {
		int32	para = 0;
		int32	startPara = 0;		// byte offset within the paragraph
		int32	length = 0;		// bytes on this line (no separator)
		int32	startAbs = 0;		// document offset of startPara (cached)
		float	x = 0, y = 0;		// origin of the line's first byte
		float	width = 0;		// total advance (natural, un-justified)
		float	height = 0;		// line height
		float	baseline = 0;	// baseline offset within the line
		bool	last = false;	// last line of its paragraph
		uint8	listMark = 0;	// PWListKind of the paragraph, first line only
		int32	listSeq = 0;	// 1-based ordinal for numbered lists
	};
	const std::vector<Line>& Lines() const { return fLines; }
	// The lines of one page, for per-page drawing (see PWPageView).
	void	PageLines(int32 page, int32* firstLine, int32* lineCount) const;

	struct Segment {			// one styled piece of one line
		const PWRun* run = NULL;
		int32	startPara = 0;	// paragraph-relative byte offset
		int32	length = 0;
		float	x = 0;			// document-space x
		float	baseline = 0;	// document-space baseline y
	};
	// Segments of a line, ready to draw.
	void	FillSegments(int32 lineIndex, std::vector<Segment>* out) const;

	// The offset of the first byte after the last byte of a line — handles
	// paragraph separators, so caret motion walks lines correctly.
	int32	LineStart(int32 lineIndex) const;
	int32	LineEnd(int32 lineIndex) const;
	int32	LineOfOffset(int32 offset) const;
	int32	NextLineStart(int32 offset) const;
	int32	PrevLineStart(int32 offset) const;

	int32	TotalHeight() const;	// document space, pages + gaps

	// The document may be null-sized; Layout() must still produce one page.
	void	SetDocument(const PWDocument* doc) { fDoc = doc; }

private:
	void	LayoutParagraph(int32 para);
	void	AssignLinesToPages();
	void	CacheAbsoluteStarts();
	BFont	FontForRun(const PWRun& run) const;
	// Contiguous ownership: line i owns [startAbs, startAbs of line i+1);
	// the last line owns through the document end. Trimmed trailing spaces
	// and paragraph separators therefore always have exactly one owner.
	int32	LineEndAbs(int32 lineIndex) const;
	float	ByteWidth(int32 para, const std::vector<PWRun>& runs,
			const int32* runOf, const char* text, int32 at, int32 paraLen,
			float lineX, float edge) const;
	bool	LineIsJustified(int32 lineIndex) const;
	float	SlackPerGap(int32 lineIndex) const;

	const PWDocument*	fDoc = NULL;
	PWPageSetup			fSetup;
	std::vector<Line>	fLines;
	// per page: first line index and count; page y origin computed on demand
	struct PageSpan { int32 firstLine = 0, lineCount = 0; };
	std::vector<PageSpan>	fPages;

	static const float kPageGap;	// grey gap between pages, points
public:
	static float Gap() { return kPageGap; }
private:
};

#endif	// PW_LAYOUT_H
