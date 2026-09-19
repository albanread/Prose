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
// Width of one byte at `at`, with tabs advancing to the paragraph's next
// tab stop (absolute from the left text edge) or a default 36 pt step.
float
PWLayout::ByteWidth(int32 para, const std::vector<PWRun>& runs,
	const int32* runOf, const char* text, int32 at, int32 paraLen,
	float lineX, float edge) const
{
	if (text[at] == '\t') {
		const std::vector<PWTab>& tabs = fDoc->ParagraphFormat(para).tabs;
		float pos = lineX + 0.01f - edge;	// x relative to the text edge
		for (const PWTab& tab : tabs)
			if (tab.x > pos)
				return tab.x - pos;
		return 36.0f;
	}
	if (runOf == NULL)
		return 0;
	BFont f = FontForRun(runs[runOf[at]]);
	return f.StringWidth(text + at, 1);
}

void
PWLayout::LayoutParagraph(int32 para)
{
	const char* text = fDoc->ParagraphText(para);
	int32 paraLen = fDoc->ParagraphLength(para);
	const std::vector<PWRun>& runs = fDoc->ParagraphRuns(para);
	const PWParaFormat& fmt = fDoc->ParagraphFormat(para);

	// The paragraph's text box, inside the page margins and indents. Lists
	// hang their marker in the left indent.
	float edge = fSetup.marginLeft + fmt.indentLeft;
	float listIndent = fmt.listKind != PW_LIST_NONE ? 18.0f : 0.0f;
	float column = fSetup.pageWidth - fSetup.marginRight - fmt.indentRight
		- edge - listIndent;

	std::vector<int32> runOf(paraLen > 0 ? paraLen : 1, 0);
	for (size_t r = 0; r < runs.size(); r++) {
		int32 from = runs[r].start;
		int32 to = from + runs[r].length;
		for (int32 i = std::max((int32)0, from); i < std::min(to, paraLen); i++)
			runOf[i] = (int32)r;
	}

	int32 listSeq = 0;
	if (fmt.listKind == PW_LIST_NUMBER) {
		for (int32 p = para - 1; p >= 0; p--) {
			if (fDoc->ParagraphFormat(p).listKind != PW_LIST_NUMBER)
				break;
			listSeq++;
		}
		listSeq++;	// 1-based, restarting at each non-list paragraph
	}

	int32 lineStart = 0;
	while (lineStart < paraLen || (lineStart == 0 && paraLen == 0)) {
		bool first = (lineStart == 0);
		float lineEdge = edge + listIndent + (first ? fmt.indentFirst : 0);

		int32 i = lineStart;
		float width = 0;
		int32 lastGood = lineStart;
		int32 lastBreak = -1;
		while (i < paraLen) {
			int32 wordStart = i;
			float wordWidth = 0;
			while (i < paraLen && IsBreakChar(text[i])) {
				wordWidth += ByteWidth(para, runs, runOf.data(), text, i,
					paraLen, lineEdge + width, edge);
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
			lastGood = paraLen;

		int32 lineEnd = lastGood;
		while (lineEnd > lineStart && IsBreakChar(text[lineEnd - 1]))
			lineEnd--;

		float ascent = 0, descent = 0;
		for (int32 b = lineStart; b < lineEnd;) {
			const PWRun& r = runs[runOf[b]];
			font_height fh;
			FontForRun(r).GetHeight(&fh);
			ascent = std::max(ascent, fh.ascent);
			descent = std::max(descent, fh.descent + fh.leading);
			b = UTF8Next(text, b, paraLen);
		}
		if (ascent == 0) {
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
		float lineW = 0;
		for (int32 b = lineStart; b < lineEnd;) {
			const PWRun& r = runs[runOf[b]];
			BFont f = FontForRun(r);
			int32 segEnd = std::min(r.start + r.length, lineEnd);
			if (text[b] == '\t' && segEnd == b + 1) {
				lineW += ByteWidth(para, runs, runOf.data(), text, b, paraLen,
					lineEdge + lineW, edge);
				b = segEnd;
				continue;
			}
			lineW += f.StringWidth(text + b, segEnd - b);
			b = segEnd;
		}
		line.width = lineW;
		line.height = (ascent + descent) * fmt.lineSpacing;
		line.baseline = ascent * fmt.lineSpacing;
		switch (fmt.alignment) {
			case PW_ALIGN_CENTER:
				line.x = lineEdge + (column - lineW) / 2;
				break;
			case PW_ALIGN_RIGHT:
				line.x = lineEdge + column - lineW;
				break;
			default:
				line.x = lineEdge;
				break;
		}
		line.last = (lineEnd >= paraLen);
		if (first && fmt.listKind != PW_LIST_NONE) {
			line.listMark = fmt.listKind;
			line.listSeq = listSeq;
		}
		fLines.push_back(line);

		if (line.last)
			break;
		int32 next = lastBreak > lineEnd ? lastBreak : lastGood;
		if (next <= lineStart)
			next = lastGood > lineStart ? lastGood : lineEnd;
		lineStart = next;
	}
	if (fLines.empty() || fLines.back().para != (int32)para) {
		Line empty;
		empty.para = para;
		empty.x = edge + listIndent;
		empty.last = true;
		fLines.push_back(empty);
	}
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
			const PWParaFormat& fmt = fDoc->ParagraphFormat(fLines[i].para);
			// vertical spacing around paragraphs, not inside pages' first line
			float before = (fLines[i].startPara == 0 && used > 0)
				? fmt.spaceBefore : 0;
			float h = fLines[i].height > 0 ? fLines[i].height : 14;
			if (used > 0 && used + before + h > columnHeight)
				break;
			used += before;
			fLines[i].y = pageTop + fSetup.marginTop + used;
			used += h;
			if (fLines[i].last)
				used += fmt.spaceAfter;
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
		CacheAbsoluteStarts();
	}
	AssignLinesToPages();
}

void
PWLayout::CacheAbsoluteStarts()
{
	// Lines arrive grouped by paragraph in order; accumulate each
	// paragraph's base offset as its first line is seen.
	int32 paraBase = 0;
	int32 lastPara = -1;
	for (Line& l : fLines) {
		if (l.para != lastPara) {
			paraBase = fDoc->ParaStart(l.para);
			lastPara = l.para;
		}
		l.startAbs = paraBase + l.startPara;
	}
}

void
PWLayout::PageLines(int32 page, int32* firstLine, int32* lineCount) const
{
	if (page < 0 || page >= (int32)fPages.size()) {
		*firstLine = 0;
		*lineCount = 0;
		return;
	}
	*firstLine = fPages[page].firstLine;
	*lineCount = fPages[page].lineCount;
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
	return fLines[lineIndex].startAbs;
}

int32
PWLayout::LineEndAbs(int32 lineIndex) const
{
	if (lineIndex + 1 < (int32)fLines.size())
		return fLines[lineIndex + 1].startAbs;
	return fDoc->Length() + 1;
}

int32
PWLayout::LineEnd(int32 lineIndex) const
{
	return LineEndAbs(lineIndex);
}

int32
PWLayout::LineOfOffset(int32 offset) const
{
	// Binary search over cached absolute starts against contiguous
	// [startAbs, LineEndAbs) ownership: every offset has exactly one line.
	if (fLines.empty())
		return 0;
	int32 lo = 0, hi = (int32)fLines.size() - 1;
	while (lo < hi) {
		int32 mid = (lo + hi) / 2;
		if (offset < fLines[mid].startAbs)
			hi = mid - 1;
		else if (offset >= LineEndAbs(mid))
			lo = mid + 1;
		else
			return mid;
	}
	if (lo < 0)
		lo = 0;
	if (lo >= (int32)fLines.size())
		lo = (int32)fLines.size() - 1;
	return lo;
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
	if (LineIsJustified(line)) {
		int32 spaces = 0;
		for (int32 i = l.startPara; i < caretPara; i++)
			if (text[i] == ' ')
				spaces++;
		x += SlackPerGap(line) * spaces;
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
	// Binary search by y (lines are sorted by y), then settle to the
	// nearest band when the point falls between lines.
	int32 lo = 0, hi = (int32)fLines.size() - 1;
	int32 best = 0;
	while (lo <= hi) {
		int32 mid = (lo + hi) / 2;
		const Line& l = fLines[mid];
		if (p.y < l.y)
			hi = mid - 1;
		else if (p.y > l.y + l.height)
			lo = mid + 1;
		else {
			best = mid;
			break;
		}
	}
	if (lo > hi) {
		// between bands: pick the closer neighbour
		int32 below = hi >= 0 ? hi : 0;
		int32 above = lo < (int32)fLines.size() ? lo : (int32)fLines.size() - 1;
		float dB = p.y < fLines[below].y ? fLines[below].y - p.y : 1e30f;
		float dA = p.y > fLines[above].y + fLines[above].height
			? p.y - (fLines[above].y + fLines[above].height) : 1e30f;
		best = dA < dB ? above : below;
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

bool
PWLayout::LineIsJustified(int32 lineIndex) const
{
	const Line& l = fLines[lineIndex];
	return fDoc->ParagraphFormat(l.para).alignment == PW_ALIGN_JUSTIFY
		&& !l.last;
}

float
PWLayout::SlackPerGap(int32 lineIndex) const
{
	const Line& l = fLines[lineIndex];
	const char* text = fDoc->ParagraphText(l.para);
	int32 gaps = 0;
	for (int32 i = l.startPara; i < l.startPara + l.length; i++)
		if (text[i] == ' ')
			gaps++;
	if (gaps == 0)
		return 0;
	float slack = fSetup.TextWidth() - l.width;
	return slack > 0 ? slack / gaps : 0;
}

void
PWLayout::FillSegments(int32 lineIndex, std::vector<Segment>* out) const
{
	out->clear();
	const Line& l = fLines[lineIndex];
	const std::vector<PWRun>& runs = fDoc->ParagraphRuns(l.para);
	const char* text = fDoc->ParagraphText(l.para);
	int32 paraLen = fDoc->ParagraphLength(l.para);
	bool justify = LineIsJustified(lineIndex);
	float slack = justify ? SlackPerGap(lineIndex) : 0;
	float edge = fSetup.marginLeft
		+ fDoc->ParagraphFormat(l.para).indentLeft;
	float x = l.x;
	int32 b = l.startPara;
	int32 end = l.startPara + l.length;
	auto pushSeg = [&](const PWRun* r, int32 from, int32 to, float& at) {
		Segment s;
		s.run = r;
		s.startPara = from;
		s.length = to - from;
		s.x = at;
		s.baseline = l.y + l.baseline;
		if (to - from == 1 && text[from] == '\t') {
			// tabs advance to the paragraph's next stop, not their glyph
			at += ByteWidth(l.para, runs, NULL, text, from, paraLen, at, edge);
			out->push_back(s);
			return;
		}
		BFont f = FontForRun(*r);
		at += f.StringWidth(text + from, to - from);
		if (justify && to < end && text[to - 1] == ' ')
			at += slack;	// the gap after a trailing space takes the slack
		out->push_back(s);
	};
	while (b < end) {
		const PWRun* r = &runs[0];
		for (const PWRun& rr : runs)
			if (b >= rr.start && b < rr.start + rr.length) { r = &rr; break; }
		int32 segEnd = std::min(r->start + r->length, end);
		if (segEnd <= b)
			segEnd = b + 1;
		if (!justify) {
			while (b < segEnd) {
				if (text[b] == '\t') {
					pushSeg(r, b, b + 1, x);
					b++;
				} else {
					int32 word = b;
					while (word < segEnd && text[word] != '\t')
						word++;
					pushSeg(r, b, word, x);
					b = word;
				}
			}
			continue;
		}
		// justified: split at spaces so each gap can stretch
		while (b < segEnd) {
			int32 word = b;
			while (word < segEnd && text[word] != ' ')
				word++;
			if (word > b)
				pushSeg(r, b, word, x);
			if (word < segEnd) {
				int32 sp = word;
				while (sp < segEnd && text[sp] == ' ')
					sp++;
				pushSeg(r, word, sp, x);
				b = sp;
			} else
				b = word;
		}
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
