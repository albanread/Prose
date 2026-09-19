#include "PWLayout.h"

#include <ctype.h>

#include <algorithm>

const float PWLayout::kPageGap = 18.0f;

PWLayout::PWLayout(const PWDocument* doc)
	: fDoc(doc)
{
}

void
PWLayout::SetPageSetup(const PWPageSetup& setup)
{
	fSetup = setup;
}

BFont
PWLayout::FontForRun(const PWRun& run) const
{
	BFont font(be_plain_font);
	font.SetFamilyAndFace(run.format.family,
		(uint16)((run.format.bold ? B_BOLD_FACE : 0)
			| (run.format.italic ? B_ITALIC_FACE : 0)));
	font.SetSize(run.format.size);
	return font;
}

// UTF-8: advance over one character's bytes.
static inline int32
UTF8Next(const char* s, int32 i, int32 len)
{
	if (i >= len)
		return len;
	unsigned char c = (unsigned char)s[i];
	int32 step = 1;
	if ((c & 0x80) == 0) step = 1;
	else if ((c & 0xE0) == 0xC0) step = 2;
	else if ((c & 0xF0) == 0xE0) step = 3;
	else if ((c & 0xF8) == 0xF0) step = 4;
	while (step > 1 && i + step - 1 < len
		&& ((unsigned char)s[i + step - 1] & 0xC0) == 0x80) {
		// good continuation, keep step
		break;
	}
	if (i + step > len)
		step = len - i;
	return i + step;
}

static bool
IsBreakChar(char c)
{
	return c == ' ' || c == '\t';
}

// Greedy line breaking over styled text. Words are measured whole; a word
// longer than the column is broken byte-wise at the column edge.
void
PWLayout::LayoutParagraph(int32 para)
{
	const char* text = fDoc->ParagraphText(para);
	int32 paraLen = fDoc->ParagraphLength(para);
	const std::vector<PWRun>& runs = fDoc->ParagraphRuns(para);
	float column = fSetup.TextWidth();

	// Precompute which run covers each byte (a byte->run index map).
	std::vector<int32> runOf(paraLen > 0 ? paraLen : 1, 0);
	for (size_t r = 0; r < runs.size(); r++) {
		int32 from = runs[r].start;
		int32 to = from + runs[r].length;
		for (int32 i = std::max((int32)0, from); i < std::min(to, paraLen); i++)
			runOf[i] = (int32)r;
	}

	int32 lineStart = 0;
	while (lineStart < paraLen || (lineStart == 0 && paraLen == 0)) {
		// Measure word by word from lineStart.
		int32 i = lineStart;
		float width = 0;
		int32 lastGood = lineStart;		// end (exclusive) that fits
		int32 lastBreak = -1;			// offset after a break char that fits
		while (i < paraLen) {
			int32 wordStart = i;
			float wordWidth = 0;
			// leading break chars attach to the previous word
			while (i < paraLen && IsBreakChar(text[i])) {
				BFont f = FontForRun(runs[runOf[i]]);
				wordWidth += f.StringWidth(text + i, 1);
				i = UTF8Next(text, i, paraLen);
			}
			while (i < paraLen && !IsBreakChar(text[i])) {
				BFont f = FontForRun(runs[runOf[i]]);
				wordWidth += f.StringWidth(text + i, 1);
				i = UTF8Next(text, i, paraLen);
			}
			if (width + wordWidth <= column || lastGood == lineStart) {
				width += wordWidth;
				lastGood = i;
				if (i > wordStart && IsBreakChar(text[i - 1]))
					lastBreak = i;
			} else
				break;
		}
		if (lastGood == lineStart)
			lastGood = paraLen;	// single overlong word: fill the line

		// Trim trailing break chars from the line (they wrap invisibly).
		int32 lineEnd = lastGood;
		while (lineEnd > lineStart && IsBreakChar(text[lineEnd - 1]))
			lineEnd--;

		// Line metrics: the tallest font on the line rules.
		float ascent = 0, descent = 0;
		for (int32 b = lineStart; b < lineEnd;) {
			const PWRun& r = runs[runOf[b]];
			font_height fh;
			FontForRun(r).GetHeight(&fh);
			ascent = std::max(ascent, fh.ascent);
			descent = std::max(descent, fh.descent + fh.leading);
			b = UTF8Next(text, b, paraLen);
		}
		if (ascent == 0) {	// empty line: use the paragraph's first run
			font_height fh;
			BFont f = FontForRun(runs[0]);
			f.GetHeight(&fh);
			ascent = fh.ascent;
			descent = fh.descent + fh.leading;
		}

		Line line;
		line.para = para;
		line.startPara = lineStart;
		line.length = lineEnd - lineStart;
		// width recomputed for the trimmed end; alignment shifts x
		float lineW = 0;
		for (int32 b = lineStart; b < lineEnd;) {
			const PWRun& r = runs[runOf[b]];
			BFont f = FontForRun(r);
			// measure to the end of this run's span on the line
			int32 segEnd = std::min(r.start + r.length, lineEnd);
			lineW += f.StringWidth(text + b, segEnd - b);
			b = segEnd;
		}
		line.width = lineW;
		line.height = ascent + descent;
		line.baseline = ascent;
		// x/y assigned in AssignLinesToPages; alignment here:
		switch (fDoc->ParagraphFormat(para).alignment) {
			case PW_ALIGN_CENTER:
				line.x = fSetup.marginLeft + (column - lineW) / 2;
				break;
			case PW_ALIGN_RIGHT:
				line.x = fSetup.marginLeft + column - lineW;
				break;
			default:
				line.x = fSetup.marginLeft;
				break;
		}
		fLines.push_back(line);

		if (lineEnd >= paraLen)
			break;
		// Next line starts after the break chars we swallowed.
		int32 next = lastBreak > lineEnd ? lastBreak : lastGood;
		if (next <= lineStart)
			next = lastGood > lineStart ? lastGood : lineEnd;
		lineStart = next;
	}
	// Empty paragraph: one zero-length line.
	if (fLines.empty() || fLines.back().para != (int32)para)
		fLines.push_back(Line{ para, 0, 0, fSetup.marginLeft, 0, 0, 0, 0 });
}

void
PWLayout::AssignLinesToPages()
{
	fPages.clear();
	float pageTop = 0;
	float columnHeight = fSetup.TextHeight();
	int32 i = 0;
	while (i < (int32)fLines.size()) {
		PageSpan span;
		span.firstLine = i;
		float used = 0;
		while (i < (int32)fLines.size()) {
			float h = fLines[i].height > 0 ? fLines[i].height : 14;
			if (used > 0 && used + h > columnHeight)
				break;
			fLines[i].y = pageTop + fSetup.marginTop + used;
			used += h;
			i++;
		}
		span.lineCount = i - span.firstLine;
		if (span.lineCount == 0) {	// safety: never loop empty
			span.lineCount = 1;
			fLines[i].y = pageTop + fSetup.marginTop;
			i++;
		}
		fPages.push_back(span);
		pageTop += fSetup.pageHeight + kPageGap;
	}
	if (fPages.empty())
		fPages.push_back(PageSpan{ 0, 0 });
}

void
PWLayout::Layout()
{
	fLines.clear();
	if (fDoc != NULL) {
		for (int32 p = 0; p < fDoc->CountParagraphs(); p++)
			LayoutParagraph(p);
	}
	AssignLinesToPages();
}

BRect
PWLayout::PageBounds(int32 page) const
{
	float top = page * (fSetup.pageHeight + kPageGap);
	return BRect(0, top, fSetup.pageWidth, top + fSetup.pageHeight);
}

int32
PWLayout::LineStart(int32 lineIndex) const
{
	const Line& l = fLines[lineIndex];
	return fDoc->ParaStart(l.para) + l.startPara;
}

int32
PWLayout::LineEnd(int32 lineIndex) const
{
	const Line& l = fLines[lineIndex];
	int32 paraLen = fDoc->ParagraphLength(l.para);
	int32 end = l.startPara + l.length;
	// The line "owns" the separator when the paragraph wraps to its end.
	if (end >= paraLen && l.para + 1 < fDoc->CountParagraphs())
		return fDoc->ParaStart(l.para) + paraLen + 1;
	return fDoc->ParaStart(l.para) + end;
}

int32
PWLayout::LineOfOffset(int32 offset) const
{
	// Linear scan; fine for sprint 1, becomes a binary search when profiled.
	for (int32 i = 0; i < (int32)fLines.size(); i++) {
		if (LineStart(i) <= offset && offset < LineEnd(i))
			return i;
	}
	return (int32)fLines.size() - 1;
}

int32
PWLayout::NextLineStart(int32 offset) const
{
	int32 line = LineOfOffset(offset);
	if (line + 1 < (int32)fLines.size())
		return LineStart(line + 1);
	return LineEnd(line);
}

int32
PWLayout::PrevLineStart(int32 offset) const
{
	int32 line = LineOfOffset(offset);
	if (line > 0)
		return LineStart(line - 1);
	return LineStart(0);
}

int32
PWLayout::PageOfOffset(int32 offset) const
{
	int32 line = LineOfOffset(offset);
	for (int32 p = 0; p < (int32)fPages.size(); p++) {
		if (line >= fPages[p].firstLine
			&& line < fPages[p].firstLine + fPages[p].lineCount)
			return p;
	}
	return 0;
}

bool
PWLayout::OffsetToXY(int32 offset, BPoint* xy, float* caretHeight) const
{
	if (fLines.empty())
		return false;
	int32 line = LineOfOffset(offset);
	const Line& l = fLines[line];
	int32 para, inPara;
	fDoc->Locate(offset, &para, &inPara);
	int32 caretPara = inPara - l.startPara;
	if (caretPara < 0) caretPara = 0;
	const char* text = fDoc->ParagraphText(l.para);
	const std::vector<PWRun>& runs = fDoc->ParagraphRuns(l.para);

	float x = l.x;
	int32 b = l.startPara;
	while (b < l.startPara + l.length && b < caretPara) {
		const PWRun* r = &runs[0];
		for (const PWRun& rr : runs)
			if (b >= rr.start && b < rr.start + rr.length) { r = &rr; break; }
		int32 segEnd = std::min(r->start + r->length,
			std::min(caretPara, l.startPara + l.length));
		if (segEnd > b) {
			BFont f = FontForRun(*r);
			x += f.StringWidth(text + b, segEnd - b);
			b = segEnd;
		} else
			b = UTF8Next(text, b, fDoc->ParagraphLength(l.para));
	}
	xy->x = x;
	xy->y = l.y + l.baseline;
	*caretHeight = l.height;
	return true;
}

int32
PWLayout::XYToOffset(BPoint p) const
{
	if (fLines.empty())
		return 0;
	// Find the line whose y band contains p (or nearest).
	int32 best = 0;
	float bestDist = 1e30f;
	for (int32 i = 0; i < (int32)fLines.size(); i++) {
		float d;
		if (p.y < fLines[i].y)
			d = fLines[i].y - p.y;
		else if (p.y > fLines[i].y + fLines[i].height)
			d = p.y - (fLines[i].y + fLines[i].height);
		else
			d = -1;	// inside
		if (d < 0) { best = i; break; }
		if (d < bestDist) { bestDist = d; best = i; }
	}
	const Line& l = fLines[best];
	const char* text = fDoc->ParagraphText(l.para);
	const std::vector<PWRun>& runs = fDoc->ParagraphRuns(l.para);
	int32 paraLen = fDoc->ParagraphLength(l.para);

	// Walk the line accumulating width until we pass p.x.
	float x = l.x;
	int32 b = l.startPara;
	int32 lastBoundary = b;
	while (b < l.startPara + l.length) {
		lastBoundary = b;
		const PWRun* r = &runs[0];
		for (const PWRun& rr : runs)
			if (b >= rr.start && b < rr.start + rr.length) { r = &rr; break; }
		BFont f = FontForRun(*r);
		// one UTF-8 character at a time; stop half a char past the point
		int32 next = UTF8Next(text, b, paraLen);
		if (next > l.startPara + l.length)
			next = l.startPara + l.length;
		float w = f.StringWidth(text + b, next - b);
		if (x + w / 2 > p.x)
			break;
		x += w;
		b = next;
	}
	return fDoc->ParaStart(l.para) + b;
}

void
PWLayout::FillSegments(int32 lineIndex, std::vector<Segment>* out) const
{
	out->clear();
	const Line& l = fLines[lineIndex];
	const std::vector<PWRun>& runs = fDoc->ParagraphRuns(l.para);
	const char* text = fDoc->ParagraphText(l.para);
	float x = l.x;
	int32 b = l.startPara;
	int32 end = l.startPara + l.length;
	while (b < end) {
		const PWRun* r = &runs[0];
		for (const PWRun& rr : runs)
			if (b >= rr.start && b < rr.start + rr.length) { r = &rr; break; }
		int32 segEnd = std::min(r->start + r->length, end);
		if (segEnd <= b)
			segEnd = b + 1;
		Segment s;
		s.run = r;
		s.startPara = b;
		s.length = segEnd - b;
		s.x = x;
		s.baseline = l.y + l.baseline;
		BFont f = FontForRun(*r);
		x += f.StringWidth(text + b, segEnd - b);
		out->push_back(s);
		b = segEnd;
	}
}

int32
PWLayout::TotalHeight() const
{
	if (fPages.empty())
		return 0;
	return (int32)((fPages.size() - 1) * (fSetup.pageHeight + kPageGap)
		+ fSetup.pageHeight);
}
