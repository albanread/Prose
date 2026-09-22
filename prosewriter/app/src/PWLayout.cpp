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
	bool changed = setup.pageWidth != fSetup.pageWidth
		|| setup.pageHeight != fSetup.pageHeight
		|| setup.marginLeft != fSetup.marginLeft
		|| setup.marginRight != fSetup.marginRight
		|| setup.marginTop != fSetup.marginTop
		|| setup.marginBottom != fSetup.marginBottom;
	fSetup = setup;
	if (changed)
		fSetupEpoch++;	// lines were measured in a different box: re-measure
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
void
PWLayout::ImageSizeAt(int32 para, int32 byteOffset, float* w, float* h) const
{
	*w = *h = 0;
	if (fDoc == NULL || para < 0 || para >= fDoc->CountParagraphs())
		return;
	// Direct table lookup — the old route went through a global-offset
	// Locate() per byte, which made layout quadratic in paragraphs.
	const std::map<int32, PWDocument::PWImage>& imgs =
		fDoc->ParagraphImages(para);
	auto it = imgs.find(byteOffset);
	if (it != imgs.end()) {
		*w = it->second.widthPt;
		*h = it->second.heightPt;
	}
}

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
		// default stops: the next multiple of 36, not 36 from wherever
		// the pen happens to be (successive tabs must land on a grid)
		float target = (floorf(pos / 36.0f) + 1.0f) * 36.0f;
		if (target <= pos)
			target = pos + 36.0f;
		return target - pos;
	}
	if (runOf == NULL)
		return 0;
	float w = 0, h = 0;
	ImageSizeAt(para, at, &w, &h);
	if (w > 0)
		return w;
	BFont f = FontForRun(runs[runOf[at]]);
	return f.StringWidth(text + at, 1);
}

const char PWLayout::kCellSep = 0x1D;

bool
PWLayout::IsTableParagraph(int32 para) const
{
	if (fDoc == NULL || para >= fDoc->CountParagraphs())
		return false;
	return strchr(fDoc->ParagraphText(para), kCellSep) != NULL;
}

const PWLayout::RowLayout*
PWLayout::RowAt(int32 para) const
{
	if (fDoc == NULL)
		return NULL;
	auto it = fRows.find(fDoc->ParaId(para));
	return it == fRows.end() ? NULL : &it->second;
}

float
PWLayout::CellLayout::CellWidthOfByte(int32 byte, const PWLayout* layout,
	const char* text) const
{
	// width of text from the cell start to `byte` on its wrapped line;
	// a byte at/past the content end (e.g. the caret resting on the
	// separator) measures the full content, so the caret stays at the
	// content edge inside the cell — never on the previous cell's rule
	for (const CellLine& cl : lines) {
		if (byte <= cl.startByte + cl.length)
			return layout->TextWidthOfSpan(text, cl.startByte,
				byte < cl.startByte ? cl.startByte : byte);
	}
	if (!lines.empty()) {
		const CellLine& last = lines.back();
		return layout->TextWidthOfSpan(text, last.startByte,
			last.startByte + last.length);
	}
	return 0;
}

// The shared column structure of the table containing `para`: natural
// width per column is the MAXIMUM across every row (missing cells
// contribute 0), the column count is the widest row's. Columns are a
// property of the whole table — that is what makes it a table; sizing
// a row by its own content made every row's rules land at different x
// and the grid was garbage.
std::vector<float>
PWLayout::TableColumnWidths(int32 firstPara) const
{
	int32 lastPara = firstPara;
	while (IsTableParagraph(lastPara + 1))
		lastPara++;
	const PWParaFormat& fmt = fDoc->ParagraphFormat(firstPara);
	float column = fSetup.pageWidth - fSetup.marginRight - fmt.indentRight
		- (fSetup.marginLeft + fmt.indentLeft);

	std::vector<float> natural;
	for (int32 p = firstPara; p <= lastPara; p++) {
		const char* text = fDoc->ParagraphText(p);
		int32 len = fDoc->ParagraphLength(p);
		const std::vector<PWRun>& runs = fDoc->ParagraphRuns(p);
		int32 from = 0;
		for (int32 i = 0;; i++) {
			int32 to = i;
			for (to = from; to < len && text[to] != kCellSep; to++)
				;
			if ((int32)natural.size() <= i)
				natural.push_back(40.0f);
			if (to > from) {
				BFont font(be_plain_font);
				const PWCharFormat& f = FormatForSpan(runs, from);
				font.SetFamilyAndFace(f.family,
					(uint16)((f.bold ? B_BOLD_FACE : 0)
						| (f.italic ? B_ITALIC_FACE : 0)));
				font.SetSize(f.size);
				float w = font.StringWidth(text + from, to - from);
				natural[i] = std::max(natural[i],
					std::min(w + 16.0f, column * 0.7f));
			}
			if (to >= len)
				break;
			from = to + 1;
		}
	}

	float sum = 0;
	for (float n : natural)
		sum += n;
	std::vector<float> widths(natural.size(), 0);
	for (size_t i = 0; i < natural.size(); i++)
		widths[i] = std::max(30.0f, natural[i] / sum * column);
	// renormalise after the minimum clamp
	sum = 0;
	for (float w : widths)
		sum += w;
	for (float& w : widths)
		w = w / sum * column;
	return widths;
}

// Table row layout: cells wrap inside their column; the row becomes one
// synthesized Line of the row's height, so page flow, fingerprints and
// the rest of the engine treat it like any other line.
void
PWLayout::LayoutTableRow(int32 para)
{
	const char* text = fDoc->ParagraphText(para);
	int32 paraLen = fDoc->ParagraphLength(para);
	const std::vector<PWRun>& runs = fDoc->ParagraphRuns(para);
	const PWParaFormat& fmt = fDoc->ParagraphFormat(para);
	float column = fSetup.pageWidth - fSetup.marginRight - fmt.indentRight
		- (fSetup.marginLeft + fmt.indentLeft);

	int32 tableFirst = para;
	while (tableFirst > 0 && IsTableParagraph(tableFirst - 1))
		tableFirst--;
	std::vector<float> widths = TableColumnWidths(tableFirst);

	// cell boundaries
	std::vector<int32> starts;
	starts.push_back(0);
	for (int32 i = 0; i < paraLen; i++)
		if (text[i] == kCellSep)
			starts.push_back(i + 1);
	int32 cellCount = (int32)starts.size();
	starts.push_back(paraLen + 1);	// sentinel
	// widths come from the table, not this row (TableColumnWidths);
	// a row with fewer cells leaves the trailing columns empty
	if ((int32)widths.size() < cellCount)
		widths.resize(cellCount, 30.0f);

	RowLayout row;
	row.para = para;
	float edge = fSetup.marginLeft + fmt.indentLeft;
	float ascent = 0, descent = 0;
	{
		font_height fh;
		BFont font(be_plain_font);
		const PWCharFormat& f = FormatForSpan(runs, 0);
		font.SetFamilyAndFace(f.family, 0);
		font.SetSize(f.size);
		font.GetHeight(&fh);
		ascent = fh.ascent * fmt.lineSpacing;
		descent = (fh.descent + fh.leading) * fmt.lineSpacing;
	}
	for (int32 c = 0; c < cellCount; c++) {
		CellLayout cell;
		cell.firstByte = starts[c];
		cell.x = edge;
		cell.width = widths[c];
		edge += widths[c];
		// wrap the cell text greedily
		int32 from = starts[c];
		int32 to = c + 1 < cellCount ? starts[c + 1] - 1 : paraLen;
		if (to < from)
			to = from;	// empty cell
		int32 lineStart = from;
		while (lineStart < to || (lineStart == from && to == from)) {
			float w = 0;
			int32 i = lineStart;
			int32 lastGood = lineStart;
			while (i < to) {
				int32 next = UTF8Next(text, i, paraLen);
				float cw = ByteWidthOfSpan(text, i, next, runs, para);
				w += cw;
				if (w > cell.width - 8 && lastGood > lineStart)
					break;
				i = next;
				if (text[i > 0 ? i - 1 : 0] == ' ' || i >= to)
					lastGood = i;
				else if (i < to && text[i] == ' ')
					lastGood = i;
			}
			if (lastGood <= lineStart) {
				// unbreakable overflow: advance at least one byte,
				// never past the cell's end (a phantom line beyond
				// the text made FillSegments walk forever — the
				// insert-table crash of 2026-09-20)
				lastGood = std::min(std::max(i, lineStart + 1), to);
			}
			CellLine cl;
			cl.startByte = lineStart;
			cl.length = lastGood - lineStart;
			cl.baseline = ascent;
			cl.height = ascent + descent;
			cell.lines.push_back(cl);
			if (lastGood >= to)
				break;
			lineStart = lastGood;
			while (lineStart < to && text[lineStart] == ' ')
				lineStart++;
		}
		if (cell.lines.empty())
			cell.lines.push_back(CellLine{ from, 0, ascent,
				ascent + descent });
		row.height = std::max(row.height,
			cell.lines.size() * (ascent + descent));
		row.cells.push_back(cell);
	}
	row.height += 8;	// cell padding
	fRows[fDoc->ParaId(para)] = row;

	Line line;
	line.para = para;
	line.startPara = 0;
	line.length = paraLen;
	line.startAbs = 0;	// CacheAbsoluteStarts fixes this
	line.x = fSetup.marginLeft + fmt.indentLeft;
	line.width = column;
	line.height = row.height;
	line.baseline = ascent;
	line.last = true;
	line.table = true;
	fLines.push_back(line);
}

void
PWLayout::LayoutParagraph(int32 para)
{
	const char* text = fDoc->ParagraphText(para);
	int32 paraLen = fDoc->ParagraphLength(para);
	const std::vector<PWRun>& runs = fDoc->ParagraphRuns(para);
	const PWParaFormat& fmt = fDoc->ParagraphFormat(para);

	if (strchr(text, kCellSep)) {
		LayoutTableRow(para);
		return;
	}

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
				float iw = 0, ih = 0;
				ImageSizeAt(para, i, &iw, &ih);
				if (iw > 0)
					wordWidth += iw;	// the whole marker at once
				else {
					BFont f = FontForRun(runs[runOf[i]]);
					wordWidth += f.StringWidth(text + i, 1);
				}
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
			float iw = 0, ih = 0;
			ImageSizeAt(para, b, &iw, &ih);
			if (iw > 0) {
				ascent = std::max(ascent, ih);	// sits on the baseline
				b = UTF8Next(text, b, paraLen);
				continue;
			}
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
			float iw = 0, ih = 0;
			ImageSizeAt(para, b, &iw, &ih);
			if (iw > 0) {
				lineW += iw;
				b = UTF8Next(text, b, paraLen);
				continue;
			}
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
	if (fDoc == NULL) {
		fLines.clear();
		fRows.clear();
		fPrevSummaries.clear();
		fPrevValid = false;
		AssignLinesToPages();
		return;
	}
	if (!fIncremental || !fPrevValid || fPrevEpoch != fSetupEpoch) {
		LayoutFull();
		return;
	}

	// Incremental: a paragraph keeps its measured lines iff its stable id
	// and revision (and the page-setup epoch) match what they were measured
	// under — the document's own change tracking. Y positions and page
	// breaks are recomputed afterwards in bulk: a walk, not a measurement.
	std::vector<Line> oldLines;
	oldLines.swap(fLines);
	fMeasuredParas = 0;

	for (int32 para = 0; para < fDoc->CountParagraphs(); para++) {
		int32 id = fDoc->ParaId(para);
		auto it = fPrevSummaries.find(id);
		if (it != fPrevSummaries.end()
			&& it->second.revision == fDoc->ParaRevision(para)
			&& it->second.firstLine + it->second.lineCount
				<= (int32)oldLines.size()) {
			for (int32 i = 0; i < it->second.lineCount; i++) {
				Line line = oldLines[it->second.firstLine + i];
				line.para = para;
				fLines.push_back(line);
			}
		} else {
			fMeasuredParas++;
			LayoutParagraph(para);
		}
	}
	CacheAbsoluteStarts();
	AssignLinesToPages();
	BuildSummaries();
}

void
PWLayout::LayoutFull()
{
	fLines.clear();
	fRows.clear();
	fMeasuredParas = 0;
	for (int32 p = 0; p < fDoc->CountParagraphs(); p++) {
		fMeasuredParas++;
		LayoutParagraph(p);
	}
	CacheAbsoluteStarts();
	AssignLinesToPages();
	BuildSummaries();
	fPrevValid = true;
}

void
PWLayout::BuildSummaries()
{
	fPrevSummaries.clear();
	fLiveParaIds.clear();
	int32 i = 0;
	while (i < (int32)fLines.size()) {
		ParaSummary s;
		int32 para = fLines[i].para;
		s.paraId = fDoc->ParaId(para);
		s.revision = fDoc->ParaRevision(para);
		s.firstLine = i;
		while (i < (int32)fLines.size() && fLines[i].para == para)
			i++;
		s.lineCount = i - s.firstLine;
		fPrevSummaries[s.paraId] = s;
		fLiveParaIds.insert(s.paraId);
	}
	fPrevEpoch = fSetupEpoch;
	PruneRowCache();
}

void
PWLayout::PruneRowCache()
{
	// Row geometry for paragraphs that no longer exist (their ids never
	// come back) must not linger: the map would grow forever and — worse —
	// a stale entry could be served for an index that shifted onto a table.
	for (auto it = fRows.begin(); it != fRows.end();) {
		if (fLiveParaIds.find(it->first) == fLiveParaIds.end())
			it = fRows.erase(it);
		else
			++it;
	}
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

float
PWLayout::TextWidthOfSpan(const char* text, int32 from, int32 to) const
{
	if (to <= from)
		return 0;
	// measure across runs for the paragraph of the current walk; used by
	// cell geometry — runs come from the caller's paragraph context via
	// fMeasurePara
	return MeasureWithRuns(text, from, to);
}

PWCharFormat
PWLayout::FormatForSpan(const std::vector<PWRun>& runs, int32 at) const
{
	for (const PWRun& r : runs)
		if (at >= r.start && at < r.start + r.length)
			return r.format;
	return runs.empty() ? PWDocument::MakeDefaultFormat() : runs[0].format;
}

float
PWLayout::MeasureWithRuns(const char* text, int32 from, int32 to) const
{
	if (!fMeasureRuns || to <= from)
		return 0;
	float w = 0;
	int32 b = from;
	while (b < to) {
		const PWRun* r = &fMeasureRuns->at(0);
		for (const PWRun& rr : *fMeasureRuns)
			if (b >= rr.start && b < rr.start + rr.length) { r = &rr; break; }
		int32 segEnd = std::min(r->start + r->length, to);
		if (segEnd <= b)
			segEnd = b + 1;
		BFont f = FontForRun(*r);
		w += f.StringWidth(text + b, segEnd - b);
		b = segEnd;
	}
	return w;
}

float
PWLayout::ByteWidthOfSpan(const char* text, int32 from, int32 to,
	const std::vector<PWRun>& runs, int32 para) const
{
	float iw = 0, ih = 0;
	ImageSizeAt(para, from, &iw, &ih);
	if (iw > 0)
		return iw;
	if (text[from] == kCellSep)
		return 0;
	fMeasureRuns = &runs;
	float w = MeasureWithRuns(text, from, to);
	fMeasureRuns = NULL;
	return w;
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

	if (l.table) {
		const RowLayout* row = RowAt(l.para);
		if (row) {
			const char* text = fDoc->ParagraphText(l.para);
			for (const CellLayout& cell : row->cells) {
				int32 cellEnd = &cell == &row->cells.back()
					? fDoc->ParagraphLength(l.para) : -1;
				if (cellEnd < 0) {
					// next cell's firstByte - 1 (the separator)
					size_t idx = &cell - &row->cells[0];
					cellEnd = row->cells[idx + 1].firstByte - 1;
				}
				if (inPara < cell.firstByte || inPara > cellEnd)
					continue;
				const std::vector<PWRun>& runs =
					fDoc->ParagraphRuns(l.para);
				fMeasureRuns = &runs;
				float w = cell.CellWidthOfByte(inPara, this, text);
				fMeasureRuns = NULL;
				// vertical: the wrapped line containing the byte
				float y = l.y;
				float lineH = row->cells.size()
					? row->cells[0].lines.empty() ? l.height
						: row->cells[0].lines[0].height : l.height;
				for (const CellLine& cl : cell.lines)
					if (inPara >= cl.startByte
						&& inPara <= cl.startByte + cl.length) {
						y = l.y + cl.baseline;
						lineH = cl.height;
						break;
					}
				xy->x = cell.x + w;
				xy->y = y;
				*caretHeight = lineH;
				return true;
			}
		}
		xy->x = l.x;
		xy->y = l.y + l.baseline;
		*caretHeight = l.height;
		return true;
	}
	// inPara, l.startPara and the PWRun offsets below are all
	// paragraph-absolute. This must NOT be line-relative: on a wrapped
	// continuation line (startPara > 0) a line-relative caret byte made
	// the measure loop below never run, parking the caret at l.x — the
	// first glyph of the wrapped line — no matter where typing was.
	int32 caretPara = inPara;
	if (caretPara < l.startPara) caretPara = l.startPara;
	const char* text = fDoc->ParagraphText(l.para);
	const std::vector<PWRun>& runs = fDoc->ParagraphRuns(l.para);

	float x = l.x;
	int32 b = l.startPara;
	// measure every byte before the caret — NOT just the line's
	// counted length: trailing spaces don't count toward a line's
	// wrap length, and a caret right after a typed space sits beyond
	// it (the caret used to freeze on the last glyph until the next
	// visible character was typed)
	while (b < caretPara) {
		const PWRun* r = &runs[0];
		for (const PWRun& rr : runs)
			if (b >= rr.start && b < rr.start + rr.length) { r = &rr; break; }
		int32 segEnd = std::min(r->start + r->length, caretPara);
		if (segEnd > b) {
			BFont f = FontForRun(*r);
			x += f.StringWidth(text + b, segEnd - b);
			b = segEnd;
		} else
			b = UTF8Next(text, b, fDoc->ParagraphLength(l.para));
	}
	if (LineIsJustified(line)) {
		// gaps live in the counted line only: trailing spaces past
		// l.length are not gaps, and must not collect slack
		int32 spaces = 0;
		int32 counted = std::min(caretPara, l.startPara + l.length);
		for (int32 i = l.startPara; i < counted; i++)
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

	if (l.table) {
		const RowLayout* row = RowAt(l.para);
		if (row) {
			for (size_t ci = 0; ci < row->cells.size(); ci++) {
				const CellLayout& cell = row->cells[ci];
				bool last = ci + 1 == row->cells.size();
				if (p.x < cell.x
					|| p.x > cell.x + cell.width
					|| (p.x == cell.x + cell.width && !last))
					continue;
				// vertical: pick the wrapped line
				int32 lineIdx = 0;
				for (size_t li = 0; li < cell.lines.size(); li++)
					if (p.y < l.y + (li + 1) * cell.lines[li].height) {
						lineIdx = (int32)li;
						break;
					} else
						lineIdx = (int32)li;
				const CellLine& cl = cell.lines[lineIdx];
				// horizontal within the cell line
				float x = cell.x;
				int32 b = cl.startByte;
				while (b < cl.startByte + cl.length) {
					int32 next = UTF8Next(text, b, paraLen);
					float w = ByteWidthOfSpan(text, b, next, runs, l.para);
					if (x + w / 2 > p.x)
						break;
					x += w;
					b = next;
				}
				return fDoc->ParaStart(l.para) + b;
			}
			return fDoc->ParaStart(l.para);
		}
	}

	// Walk the line accumulating width until we pass p.x.
	float x = l.x;
	int32 b = l.startPara;
	while (b < l.startPara + l.length) {
		const PWRun* r = &runs[0];
		for (const PWRun& rr : runs)
			if (b >= rr.start && b < rr.start + rr.length) { r = &rr; break; }
		BFont f = FontForRun(*r);
		// one UTF-8 character at a time; stop half a char past the point
		int32 next = UTF8Next(text, b, paraLen);
		if (next > l.startPara + l.length)
			next = l.startPara + l.length;
		float iw = 0, ih = 0;
		ImageSizeAt(l.para, b, &iw, &ih);
		float w = iw > 0 ? iw : f.StringWidth(text + b, next - b);
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
	// The slack is against THIS paragraph's column — indents and the list
	// hang shrink it; the full text width made indented justified text
	// run past the right edge.
	const PWParaFormat& fmt = fDoc->ParagraphFormat(l.para);
	float column = fSetup.TextWidth() - fmt.indentLeft - fmt.indentRight
		- (fmt.listKind != PW_LIST_NONE ? 18.0f : 0.0f);
	float slack = column - l.width;
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

	if (l.table) {
		const RowLayout* row = RowAt(l.para);
		if (row) {
			for (const CellLayout& cell : row->cells) {
				float cellTop = l.y;
				for (const CellLine& cl : cell.lines) {
					int32 b = cl.startByte;
					int32 end = std::min(cl.startByte + cl.length,
						paraLen);
					while (b < end) {
						const PWRun* rr = &runs[0];
						for (const PWRun& cand : runs)
							if (b >= cand.start
								&& b < cand.start + cand.length) {
								rr = &cand;
								break;
							}
						int32 next = UTF8Next(text, b, paraLen);
						float iw = 0, ih = 0;
						ImageSizeAt(l.para, b, &iw, &ih);
						Segment s;
						s.run = rr;
						s.startPara = b;
						s.length = next - b;
						s.isImage = iw > 0;
						s.imageW = iw;
						s.imageH = ih;
						s.baseline = cellTop + cl.baseline;
						fMeasureRuns = &runs;
						s.x = cell.x + MeasureWithRuns(text,
							cl.startByte, b);
						fMeasureRuns = NULL;
						out->push_back(s);
						if (next <= b)
							break;	// never stall the walk
						b = next;
					}
					cellTop += cl.height;
				}
			}
		}
		return;
	}

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
		float iw = 0, ih = 0;
		ImageSizeAt(l.para, from, &iw, &ih);
		if (iw > 0) {
			s.isImage = true;
			s.imageW = iw;
			s.imageH = ih;
			at += iw;
			out->push_back(s);
			return;
		}
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
				float iw = 0, ih = 0;
				ImageSizeAt(l.para, b, &iw, &ih);
				if (iw > 0) {
					pushSeg(r, b, b + 3, x);	// the marker is 3 bytes
					b += 3;
				} else if (text[b] == '\t') {
					pushSeg(r, b, b + 1, x);
					b++;
				} else {
					int32 word = b;
					while (word < segEnd && text[word] != '\t') {
						float w2 = 0, h2 = 0;
						ImageSizeAt(l.para, word, &w2, &h2);
						if (w2 > 0)
							break;
						word++;
					}
					pushSeg(r, b, word, x);
					b = word;
				}
			}
			continue;
		}
		// justified: split at spaces so each gap can stretch; images are
		// their own segments (they never stretch, and their marker's bytes
		// are not text to measure)
		while (b < segEnd) {
			float iw = 0, ih = 0;
			ImageSizeAt(l.para, b, &iw, &ih);
			if (iw > 0) {
				pushSeg(r, b, b + 3, x);	// the marker is 3 bytes
				b += 3;
				continue;
			}
			int32 word = b;
			while (word < segEnd && text[word] != ' ') {
				float w2 = 0, h2 = 0;
				ImageSizeAt(l.para, word, &w2, &h2);
				if (w2 > 0)
					break;	// an image ends the word run
				word++;
			}
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
