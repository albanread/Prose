#include "PWDocument.h"

#include <Bitmap.h>
#include <File.h>
#include <String.h>

#include <algorithm>
#include <cstring>
#include <utility>

// ---------------------------------------------------------------- format --
void
PWCharFormat::Archive(BMessage* into) const
{
	into->AddString("family", family);
	into->AddFloat("size", size);
	into->AddInt32("color", *(const int32*)&color);
	into->AddBool("bold", bold);
	into->AddBool("italic", italic);
	into->AddBool("underline", underline);
}

void
PWCharFormat::Unarchive(const BMessage* from)
{
	const char* fam = NULL;
	if (from->FindString("family", &fam) == B_OK)
		strlcpy(family, fam, sizeof(font_family));
	else
		family[0] = '\0';
	from->FindFloat("size", &size);
	int32 c = 0;
	if (from->FindInt32("color", &c) == B_OK)
		color = *(const rgb_color*)&c;
	from->FindBool("bold", &bold);
	from->FindBool("italic", &italic);
	from->FindBool("underline", &underline);
}

void
PWParaFormat::Archive(BMessage* into) const
{
	into->AddInt8("align", (int8)alignment);
	into->AddFloat("il", indentLeft);
	into->AddFloat("ir", indentRight);
	into->AddFloat("if", indentFirst);
	into->AddFloat("ls", lineSpacing);
	into->AddFloat("sb", spaceBefore);
	into->AddFloat("sa", spaceAfter);
	into->AddInt8("list", (int8)listKind);
	for (const PWTab& tab : tabs) {
		BMessage tabMsg('pWt&');
		tabMsg.AddFloat("x", tab.x);
		tabMsg.AddInt8("kind", (int8)tab.kind);
		into->AddMessage("tab", &tabMsg);
	}
}

void
PWParaFormat::Unarchive(const BMessage* from)
{
	int8 v = 0;
	if (from->FindInt8("align", &v) == B_OK)
		alignment = (PWAlignment)v;
	from->FindFloat("il", &indentLeft);
	from->FindFloat("ir", &indentRight);
	from->FindFloat("if", &indentFirst);
	from->FindFloat("ls", &lineSpacing);
	from->FindFloat("sb", &spaceBefore);
	from->FindFloat("sa", &spaceAfter);
	if (from->FindInt8("list", &v) == B_OK)
		listKind = (PWListKind)v;
	tabs.clear();
	BMessage tabMsg;
	for (int32 i = 0; from->FindMessage("tab", i, &tabMsg) == B_OK; i++) {
		PWTab tab;
		tabMsg.FindFloat("x", &tab.x);
		int8 k = 0;
		if (tabMsg.FindInt8("kind", &k) == B_OK)
			tab.kind = (PWTabKind)k;
		tabs.push_back(tab);
	}
}

PWCharFormat
PWDocument::MakeDefaultFormat()
{
	PWCharFormat fmt;
	// The system's plain font family is the document default: on this image
	// that is Noto Sans; whatever it is, it exists.
	font_family family;
	font_style style;
	be_plain_font->GetFamilyAndStyle(&family, &style);
	strlcpy(fmt.family, family, sizeof(font_family));
	fmt.size = 12.0f;
	return fmt;
}

// --------------------------------------------------------------- document --
PWDocument::PWDocument()
{
	fDefault = MakeDefaultFormat();
	Para p;
	p.text = "";
	p.runs.push_back(PWRun{ 0, 0, fDefault });
	fParas.push_back(p);
}

void
PWDocument::Locate(int32 offset, int32* para, int32* inPara) const
{
	int32 at = 0;
	for (size_t i = 0; i < fParas.size(); i++) {
		int32 len = (int32)fParas[i].text.size();
		int32 paraEnd = at + len;
		bool last = (i + 1 == fParas.size());
		if (offset < paraEnd || (offset == paraEnd && last)) {
			*para = (int32)i;
			*inPara = offset - at;
			return;
		}
		// offset == paraEnd and not last: it addresses the separator byte,
		// which belongs to the end of this paragraph
		if (offset == paraEnd) {
			*para = (int32)i;
			*inPara = len;
			return;
		}
		at = paraEnd + 1;
	}
	*para = (int32)fParas.size() - 1;
	*inPara = (int32)fParas.back().text.size();
}

int32
PWDocument::ParaStart(int32 para) const
{
	int32 at = 0;
	for (int32 i = 0; i < para; i++)
		at += (int32)fParas[i].text.size() + 1;
	return at;
}

int32
PWDocument::Length() const
{
	int32 at = 0;
	for (size_t i = 0; i < fParas.size(); i++)
		at += (int32)fParas[i].text.size() + 1;
	return at - 1;	// no separator after the last paragraph
}

bool
PWDocument::IsSeparatorOffset(int32 offset) const
{
	int32 para, inPara;
	Locate(offset, &para, &inPara);
	return inPara == (int32)fParas[para].text.size() && para + 1 < (int32)fParas.size();
}

const char*
PWDocument::PlainText() const
{
	if (!fPlainTextValid) {
		fPlainTextCache = "";
		for (size_t i = 0; i < fParas.size(); i++) {
			fPlainTextCache << fParas[i].text.c_str();
			if (i + 1 < fParas.size())
				fPlainTextCache.Append("\n", 1);
		}
		fPlainTextValid = true;
	}
	return fPlainTextCache.String();
}

void
PWDocument::GetText(int32 offset, int32 length, BString* out) const
{
	out->Truncate(0);
	if (length <= 0)
		return;
	int32 at = 0;
	for (size_t i = 0; i < fParas.size() && length > 0; i++) {
		int32 len = (int32)fParas[i].text.size();
		if (offset < at + len) {
			int32 from = offset - at;
			if (from < 0) from = 0;
			int32 take = len - from;
			if (take > length) take = length;
			if (take > 0) {
				out->Append(fParas[i].text.c_str() + from, take);
				length -= take;
				offset = at + from + take;
			}
		}
		if (length > 0 && offset == at + len && i + 1 < fParas.size()) {
			out->Append("\n", 1);
			length--;
			offset = at + len + 1;
		}
		at += len + 1;
	}
}

PWCharFormat
PWDocument::FormatAt(int32 offset) const
{
	int32 para, inPara;
	Locate(offset, &para, &inPara);
	const Para& p = fParas[para];
	for (const PWRun& r : p.runs)
		if (inPara >= r.start && inPara < r.start + r.length)
			return r.format;
	// between runs / empty runs: nearest run to the left, else default
	for (auto it = p.runs.rbegin(); it != p.runs.rend(); ++it)
		if (it->start + it->length <= inPara && it->length > 0)
			return it->format;
	return p.runs.empty() ? fDefault : p.runs[0].format;
}

void
PWDocument::NormalizeRuns(Para& p)
{
	// drop empties, merge same-format neighbours, renumber, keep order
	std::vector<PWRun> merged;
	for (PWRun r : p.runs) {
		if (r.length <= 0)
			continue;
		if (!merged.empty()
			&& merged.back().format == r.format
			&& merged.back().start + merged.back().length == r.start) {
			merged.back().length += r.length;
		} else
			merged.push_back(r);
	}
	if (merged.empty())
		merged.push_back(PWRun{ 0, (int32)p.text.size(), fDefault });
	p.runs.swap(merged);
}

PWCharFormat
PWDocument::FormatForInsert(const Para& p, int32 at) const
{
	// The format of the run covering the byte before the insertion point is
	// what a new character adopts (typing continues the current style).
	for (auto it = p.runs.rbegin(); it != p.runs.rend(); ++it) {
		if (it->length > 0 && at > it->start && at <= it->start + it->length)
			return it->format;
	}
	return p.runs.empty() ? fDefault : p.runs[0].format;
}

void
PWDocument::PushUndo(const UndoStep& step)
{
	fModified = true;
	fPlainTextValid = false;
	if (fInternalEdit)
		return;		// bookkeeping edits made by Undo/Redo are invisible
	fUndo.push_back(step);
	if (fUndo.size() > 512)
		fUndo.erase(fUndo.begin());
	fRedo.clear();
}

void
PWDocument::Insert(int32 offset, const char* text, const PWCharFormat* fmt)
{
	if (!text || !text[0])
		return;

	// Newlines in the payload are paragraph breaks; insert each segment
	// then split, so the undo trail covers both halves.
	if (strchr(text, '\n')) {
		int32 at = offset;
		const char* p = text;
		while (*p) {
			const char* nl = strchr(p, '\n');
			int32 seg = nl ? (int32)(nl - p) : (int32)strlen(p);
			if (seg > 0) {
				BString piece(p, seg);
				Insert(at, piece.String(), fmt);
				at += seg;
			}
			if (!nl)
				break;
			SplitPara(at);
			at += 1;
			p = nl + 1;
		}
		return;
	}
	int32 paraIdx, inPara;
	Locate(offset, &paraIdx, &inPara);
	Para& p = fParas[paraIdx];
	PWCharFormat f = fmt ? *fmt : FormatForInsert(p, inPara);

	// Split the run covering the insertion, then insert a new run.
	std::vector<PWRun> runs;
	for (const PWRun& r : p.runs) {
		if (inPara > r.start && inPara < r.start + r.length) {
			runs.push_back(PWRun{ r.start, inPara - r.start, r.format });
			runs.push_back(PWRun{ inPara, (int32)strlen(text), f });
			runs.push_back(PWRun{ inPara + (int32)strlen(text),
				r.start + r.length - inPara, r.format });
		} else if (inPara == r.start + r.length && inPara != 0) {
			runs.push_back(r);
			// new run appended after this one; handled by normalize ordering
		} else {
			// shift runs after the insertion point
			PWRun shifted = r;
			if (r.start >= inPara)
				shifted.start += (int32)strlen(text);
			runs.push_back(shifted);
		}
	}
	if (runs.size() == p.runs.size()) {	// no split happened: add the run
		PWRun r{ inPara, (int32)strlen(text), f };
		runs.push_back(r);
		// keep sorted by start
		for (size_t i = runs.size(); i > 1; i--) {
			if (runs[i - 2].start > runs[i - 1].start)
				std::swap(runs[i - 2], runs[i - 1]);
		}
	}
	p.text.insert(inPara, text);
	p.runs.swap(runs);
	NormalizeRuns(p);

	UndoStep s;
	s.kind = UndoStep::INSERT;
	s.offset = offset;
	s.length = (int32)strlen(text);
	s.text = text;
	s.coalesce = fCoalesce;
	// coalesce with a previous single-char insert directly before us
	if (fCoalesce && !fUndo.empty() && fUndo.back().kind == UndoStep::INSERT
		&& fUndo.back().coalesce
		&& fUndo.back().offset + fUndo.back().length == offset
		&& strlen(text) == 1) {
		fUndo.back().length += (int32)strlen(text);
		fModified = true;
		fPlainTextValid = false;
		return;
	}
	PushUndo(s);
}

void
PWDocument::Remove(int32 offset, int32 length)
{
	if (length <= 0 || offset < 0 || offset >= Length())
		return;
	if (offset + length > Length())
		length = Length() - offset;

	// Capture what is being removed, as text + runs, for undo.
	BString removed;
	GetText(offset, length, &removed);
	std::vector<PWRun> savedRuns;
	int32 at = offset;
	int32 paraIdx, inPara;
	Locate(offset, &paraIdx, &inPara);
	while (at < offset + length) {
		Para& p = fParas[paraIdx];
		int32 paraLen = (int32)p.text.size();
		int32 take = offset + length - at;
		bool crosses = (inPara + take > paraLen);
		if (crosses)
			take = paraLen - inPara;
		if (take > 0) {
			for (const PWRun& r : p.runs) {
				int32 from = inPara > r.start ? inPara : r.start;
				int32 to = inPara + take < r.start + r.length
					? inPara + take : r.start + r.length;
				if (to > from)
					savedRuns.push_back(PWRun{ at - offset + (from - inPara),
						to - from, r.format });
			}
			// remove bytes [inPara, inPara+take) with run surgery:
			// rebuild the run list around the hole
			std::vector<PWRun> runs;
			for (const PWRun& r : p.runs) {
				int32 rEnd = r.start + r.length;
				if (rEnd <= inPara || r.start >= inPara + take) {
					PWRun kept = r;
					if (kept.start >= inPara + take)
						kept.start -= take;
					runs.push_back(kept);
				} else {
					if (r.start < inPara)
						runs.push_back(PWRun{ r.start, inPara - r.start, r.format });
					if (rEnd > inPara + take)
						runs.push_back(PWRun{ inPara,
							rEnd - inPara - take, r.format });
				}
			}
			// drop images inside the removed range; shift those after
			std::map<int32, PWImage> kept;
			for (auto& kv : p.images) {
				if (kv.first < inPara)
					kept.insert(kv);
				else if (kv.first >= inPara + take)
					kept[kv.first - take] = kv.second;
				else
					delete kv.second.bitmap;
			}
			p.images.swap(kept);
			p.text.erase(inPara, take);
			p.runs.swap(runs);
			NormalizeRuns(p);
		}
		if (crosses && paraIdx + 1 < (int32)fParas.size()) {
			// also swallow the separator: merge the next paragraph in
			Para& next = fParas[paraIdx + 1];
			int32 boundary = (int32)p.text.size();
			for (PWRun& r : next.runs)
				r.start += boundary;
			p.text += next.text;
			p.runs.insert(p.runs.end(), next.runs.begin(), next.runs.end());
			fParas.erase(fParas.begin() + paraIdx + 1);
			inPara = boundary;
			at += take + 1;
		} else {
			at += take;
			inPara += take;
		}
	}
	if (fParas.empty())
		fParas.push_back(Para{ { PWRun{ 0, 0, fDefault } }, "", PWParaFormat() });

	UndoStep s;
	s.kind = UndoStep::REMOVE;
	s.offset = offset;
	s.length = length;
	s.text = removed;
	s.runs = savedRuns;
	PushUndo(s);
}

void
PWDocument::ApplyFormat(int32 offset, int32 length, const PWCharFormat& fmt)
{
	if (length <= 0)
		return;
	// Walk the range splitting runs at range bounds and restyling.
	int32 at = offset;
	while (at < offset + length) {
		int32 paraIdx, inPara;
		Locate(at, &paraIdx, &inPara);
		Para& p = fParas[paraIdx];
		int32 paraLen = (int32)p.text.size();
		int32 avail = paraLen - inPara;
		if (IsSeparatorOffset(at))
			avail = 0;	// skip the separator itself
		if (avail == 0) {
			at += 1;
			continue;
		}
		int32 take = offset + length - at;
		if (take > avail) take = avail;
		if (take > 0) {
			std::vector<PWRun> runs;
			for (const PWRun& r : p.runs) {
				int32 rEnd = r.start + r.length;
				if (rEnd <= inPara || r.start >= inPara + take) {
					runs.push_back(r);
					continue;
				}
				if (r.start < inPara)
					runs.push_back(PWRun{ r.start, inPara - r.start, r.format });
				runs.push_back(PWRun{ inPara > r.start ? inPara : r.start,
					(rEnd < inPara + take ? rEnd : inPara + take)
						- (inPara > r.start ? inPara : r.start), fmt });
				if (rEnd > inPara + take)
					runs.push_back(PWRun{ inPara + take, rEnd - inPara - take,
						r.format });
			}
			p.runs.swap(runs);
			NormalizeRuns(p);
			at += take;
		}
	}
	// A coarse undo: capture is skipped in sprint 1's fast path; format undo
	// arrives with the styles panel in sprint 2.
	fModified = true;
	fPlainTextValid = false;
}

void
PWDocument::SetParaFormat(int32 para, const PWParaFormat& fmt)
{
	if (para < 0 || para >= (int32)fParas.size())
		return;
	UndoStep s;
	s.kind = UndoStep::PARAFORMAT;
	s.offset = para;
	s.paraFormat = fParas[para].format;
	fParas[para].format = fmt;
	PushUndo(s);
}

void
PWDocument::SplitPara(int32 offset)
{
	int32 paraIdx, inPara;
	Locate(offset, &paraIdx, &inPara);
	Para& p = fParas[paraIdx];
	Para next;
	next.format = p.format;
	next.text = p.text.substr(inPara);
	next.runs.clear();
	for (const PWRun& r : p.runs) {
		int32 rEnd = r.start + r.length;
		if (r.start >= inPara) {
			next.runs.push_back(PWRun{ r.start - inPara, r.length, r.format });
		} else if (rEnd > inPara) {
			next.runs.push_back(PWRun{ 0, rEnd - inPara, r.format });
		}
	}
	next.images.clear();
	for (auto& kv : p.images) {
		if (kv.first >= inPara)
			next.images[kv.first - inPara] = kv.second;
	}
	for (auto it = p.images.begin(); it != p.images.end();) {
		if (it->first >= inPara)
			it = p.images.erase(it);
		else
			++it;
	}
	p.text.resize(inPara);
	p.runs.erase(std::remove_if(p.runs.begin(), p.runs.end(),
		[inPara](const PWRun& r) { return r.start >= inPara; }),
		p.runs.end());
	for (PWRun& r : p.runs)
		if (r.start + r.length > inPara)
			r.length = inPara - r.start;
	NormalizeRuns(p);
	NormalizeRuns(next);
	fParas.insert(fParas.begin() + paraIdx + 1, next);

	UndoStep s;
	s.kind = UndoStep::INSERT;
	s.offset = offset;
	s.length = 1;
	s.text = "\n";
	s.coalesce = false;
	PushUndo(s);
}

void
PWDocument::MergeWithNext(int32 para)
{
	if (para < 0 || para + 1 >= (int32)fParas.size())
		return;
	Para& p = fParas[para];
	Para& next = fParas[para + 1];
	int32 boundary = (int32)p.text.size();
	for (auto& kv : next.images)
		p.images[kv.first + boundary] = kv.second;
	next.images.clear();
	for (PWRun& r : next.runs)
		r.start += boundary;
	p.text += next.text;
	p.runs.insert(p.runs.end(), next.runs.begin(), next.runs.end());
	fParas.erase(fParas.begin() + para + 1);
	NormalizeRuns(p);

	UndoStep s;
	s.kind = UndoStep::REMOVE;
	s.offset = boundary;
	s.length = 1;
	PushUndo(s);
}

// ------------------------------------------------------------------ undo --
// Sprint 1 keeps undo exact for text and approximate for styles: undoing a
// removal re-inserts the text and then re-applies the saved run styles, and
// undo/redo entries created by those internal edits are dropped so the
// user's stack order is preserved.
void
PWDocument::Undo()
{
	if (fUndo.empty())
		return;
	UndoStep s = fUndo.back();
	fUndo.pop_back();
	fRedo.push_back(s);

	size_t depthBefore = fUndo.size();
	fCoalesce = false;
	fInternalEdit = true;
	switch (s.kind) {
		case UndoStep::INSERT:
			Remove(s.offset, s.length);
			break;
		case UndoStep::REMOVE:
		{
			Insert(s.offset, s.text.String(), NULL);
			// Restore the saved styling over the re-inserted span.
			ApplyFormat(s.offset, s.length, FormatAt(s.offset)); // normalize first
			for (const PWRun& r : s.runs) {
				if (r.length > 0)
					ApplyFormat(s.offset + r.start, r.length, r.format);
			}
			break;
		}
		case UndoStep::PARAFORMAT:
			if (s.offset < (int32)fParas.size())
				fParas[s.offset].format = s.paraFormat;
			break;
		default:
			break;
	}
	fCoalesce = true;
	fInternalEdit = false;
	while (fUndo.size() > depthBefore)
		fUndo.pop_back();	// drop the entry the internal edit pushed
	fModified = true;
	fPlainTextValid = false;
}

void
PWDocument::Redo()
{
	if (fRedo.empty())
		return;
	UndoStep s = fRedo.back();
	fRedo.pop_back();

	size_t depthBefore = fUndo.size();
	fCoalesce = false;
	fInternalEdit = true;
	switch (s.kind) {
		case UndoStep::INSERT:
			Insert(s.offset, s.text.String(), NULL);
			break;
		case UndoStep::REMOVE:
			Remove(s.offset, s.length);
			break;
		case UndoStep::PARAFORMAT:
			if (s.offset < (int32)fParas.size()) {
				PWParaFormat cur = fParas[s.offset].format;
				fParas[s.offset].format = s.paraFormat;
				s.paraFormat = cur;
			}
			break;
		default:
			break;
	}
	fCoalesce = true;
	fInternalEdit = false;
	fUndo.push_back(s);		// the undo entry for this redo, in order
	while (fUndo.size() > depthBefore + 1)
		fUndo.erase(fUndo.begin() + depthBefore);
	fModified = true;
	fPlainTextValid = false;
}

// ------------------------------------------------------------ persistence --
status_t
PWDocument::SaveToMessage(BMessage* msg) const
{
	msg->AddString("header", fHeader);
	msg->AddString("footer", fFooter);
	for (const PWStyle& s : fStyles) {
		BMessage styleMsg('pWs&');
		styleMsg.AddString("name", s.name);
		s.chr.Archive(&styleMsg);
		s.para.Archive(&styleMsg);
		msg->AddMessage("style", &styleMsg);
	}
	status_t err = B_OK;
	for (size_t i = 0; i < fParas.size() && err == B_OK; i++) {
		const Para& p = fParas[i];
		BMessage paraMsg('pWp&');
		paraMsg.AddString("text", p.text.c_str());
		p.format.Archive(&paraMsg);
		for (auto& kv : p.images) {
			BMessage imgMsg('pWi&');
			imgMsg.AddInt32("offset", kv.first);
			imgMsg.AddFloat("w", kv.second.widthPt);
			imgMsg.AddFloat("h", kv.second.heightPt);
			if (kv.second.bitmap) {
				BRect bounds = kv.second.bitmap->Bounds();
				imgMsg.AddFloat("bw", bounds.Width() + 1);
				imgMsg.AddFloat("bh", bounds.Height() + 1);
				uint32 size = (uint32)(bounds.IntegerWidth() + 1)
					* (uint32)(bounds.IntegerHeight() + 1) * 4;
				uint8* bits = (uint8*)kv.second.bitmap->Bits();
				imgMsg.AddData("bits", B_RAW_TYPE, bits, size);
			}
			paraMsg.AddMessage("image", &imgMsg);
		}
		for (const PWRun& r : p.runs) {
			BMessage runMsg('pWr&');
			runMsg.AddInt32("start", r.start);
			runMsg.AddInt32("length", r.length);
			r.format.Archive(&runMsg);
			paraMsg.AddMessage("run", &runMsg);
		}
		err = msg->AddMessage("para", &paraMsg);
	}
	return err;
}

status_t
PWDocument::LoadFromMessage(const BMessage* msg)
{
	fParas.clear();
	fUndo.clear();
	fRedo.clear();
	fHeader = fFooter = "";
	fStyles.clear();
	PWCharFormat def = MakeDefaultFormat();
	BMessage paraMsg;
	for (int32 i = 0; msg->FindMessage("para", i, &paraMsg) == B_OK; i++) {
		Para p;
		const char* text = NULL;
		if (paraMsg.FindString("text", &text) == B_OK)
			p.text = text;
		p.format.Unarchive(&paraMsg);
		BMessage runMsg;
		for (int32 j = 0; paraMsg.FindMessage("run", j, &runMsg) == B_OK; j++) {
			PWRun r;
			runMsg.FindInt32("start", &r.start);
			runMsg.FindInt32("length", &r.length);
			r.format.Unarchive(&runMsg);
			if (r.format.family[0] == '\0')
				r.format = def;
			p.runs.push_back(r);
		}
		if (p.runs.empty())
			p.runs.push_back(PWRun{ 0, (int32)p.text.size(), def });
		NormalizeRuns(p);
		BMessage imgMsg;
		for (int32 k = 0; paraMsg.FindMessage("image", k, &imgMsg) == B_OK;
				k++) {
			int32 offset = 0;
			imgMsg.FindInt32("offset", &offset);
			PWImage img;
			imgMsg.FindFloat("w", &img.widthPt);
			imgMsg.FindFloat("h", &img.heightPt);
			float bw = 1, bh = 1;
			imgMsg.FindFloat("bw", &bw);
			imgMsg.FindFloat("bh", &bh);
			const void* bits = NULL;
			ssize_t size = 0;
			if (imgMsg.FindData("bits", B_RAW_TYPE, &bits, &size) == B_OK) {
				BBitmap* bmp = new BBitmap(BRect(0, 0, bw - 1, bh - 1),
					B_RGB32);
				if (bmp && bmp->IsValid()
					&& bmp->ImportBits(bits, (int32)size,
						(int32)(bw * 4), 0, B_RGB32) == B_OK)
					img.bitmap = bmp;
				else
					delete bmp;
			}
			p.images[offset] = img;
		}
		fParas.push_back(p);
	}
	if (fParas.empty())
		fParas.push_back(Para{ { PWRun{ 0, 0, def } }, "", PWParaFormat() });
	msg->FindString("header", &fHeader);
	msg->FindString("footer", &fFooter);
	BMessage styleMsg;
	for (int32 i = 0; msg->FindMessage("style", i, &styleMsg) == B_OK; i++) {
		BString name;
		styleMsg.FindString("name", &name);
		PWCharFormat chr;
		chr.Unarchive(&styleMsg);
		PWParaFormat para;
		para.Unarchive(&styleMsg);
		fStyles.push_back(PWStyle{ name, chr, para });
	}
	fModified = false;
	fPlainTextValid = false;
	return B_OK;
}

const char* PWDocument::kObjectChar = "\357\277\274";

status_t
PWDocument::InsertImage(int32 offset, BBitmap* bitmap, float widthPt,
	float heightPt)
{
	if (!bitmap)
		return B_BAD_VALUE;
	int32 para, inPara;
	Locate(offset, &para, &inPara);
	// register first, then insert the marker so the map key stays valid
	PWImage image;
	image.bitmap = bitmap;
	image.widthPt = widthPt;
	image.heightPt = heightPt;
	fParas[para].images[inPara] = image;
	Insert(offset, kObjectChar, NULL);
	fModified = true;
	fPlainTextValid = false;
	return B_OK;
}

PWDocument::PWImage*
PWDocument::ImageAt(int32 offset)
{
	int32 para, inPara;
	Locate(offset, &para, &inPara);
	auto it = fParas[para].images.find(inPara);
	return it == fParas[para].images.end() ? NULL : &it->second;
}

int32
PWDocument::CountImages() const
{
	int32 n = 0;
	for (const Para& p : fParas)
		n += (int32)p.images.size();
	return n;
}

const PWDocument::PWStyle*
PWDocument::StyleAt(int32 index) const
{
	if (index < 0 || index >= (int32)fStyles.size())
		return NULL;
	return &fStyles[index];
}

const PWDocument::PWStyle*
PWDocument::StyleNamed(const char* name) const
{
	for (const PWStyle& s : fStyles)
		if (s.name == name)
			return &s;
	return NULL;
}

void
PWDocument::AddStyle(const char* name, const PWCharFormat& chr,
	const PWParaFormat& para)
{
	for (PWStyle& s : fStyles) {
		if (s.name == name) {
			s.chr = chr;
			s.para = para;
			fModified = true;
			return;
		}
	}
	PWStyle s;
	s.name = name;
	s.chr = chr;
	s.para = para;
	fStyles.push_back(s);
	fModified = true;
}

void
PWDocument::RemoveStyle(int32 index)
{
	if (index >= 0 && index < (int32)fStyles.size()) {
		fStyles.erase(fStyles.begin() + index);
		fModified = true;
	}
}

void
PWDocument::ClearStyles()
{
	fStyles.clear();
}

void
PWDocument::ApplyStyle(int32 offset, int32 length, int32 styleIndex)
{
	const PWStyle* s = StyleAt(styleIndex);
	if (!s)
		return;
	if (length > 0)
		ApplyFormat(offset, length, s->chr);
	else
		length = 1;	// caret only: its paragraph takes the paragraph half
	int32 para, inPara;
	Locate(offset, &para, &inPara);
	int32 lastPara;
	Locate(offset + length, &lastPara, &inPara);
	for (int32 p = para; p <= lastPara && p < CountParagraphs(); p++)
		SetParaFormat(p, s->para);
}

BString
PWDocument::ComposeHeaderText(const char* pattern, int32 page, int32 pageCount)
{
	BString out;
	if (!pattern)
		return out;
	for (const char* p = pattern; *p; ) {
		if (strncmp(p, "{page}", 6) == 0) {
			out << (int)page;
			p += 6;
		} else if (strncmp(p, "{pages}", 7) == 0) {
			out << (int)pageCount;
			p += 7;
		} else {
			out.Append(p, 1);
			p++;
		}
	}
	return out;
}

status_t
PWDocument::SaveToFile(const char* path) const
{
	BMessage msg('pWd&');
	status_t err = SaveToMessage(&msg);
	if (err != B_OK)
		return err;
	BFile file;
	err = file.SetTo(path, B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (err != B_OK)
		return err;
	ssize_t size = msg.FlattenedSize();
	char* buffer = new char[size];
	if (msg.Flatten(buffer, size) == B_OK)
		err = file.Write(buffer, size);
	else
		err = B_ERROR;
	delete[] buffer;
	return err;
}

status_t
PWDocument::LoadFromFile(const char* path)
{
	BFile file;
	status_t err = file.SetTo(path, B_READ_ONLY);
	if (err != B_OK)
		return err;
	// A flattened BMessage starts with its magic; read whole and unflatten
	off_t size = 0;
	file.GetSize(&size);
	char* buffer = new char[size];
	ssize_t got = file.Read(buffer, size);
	if (got < 4) {
		delete[] buffer;
		return B_ERROR;
	}
	BMessage msg;
	err = msg.Unflatten(buffer);
	delete[] buffer;
	if (err != B_OK)
		return err;
	return LoadFromMessage(&msg);
}

// ----------------------------------------------------------------- search --
static char
FoldChar(char c, bool caseSensitive)
{
	if (caseSensitive)
		return c;
	return (c >= 'A' && c <= 'Z') ? c + 32 : c;
}

static bool
MatchAt(const char* text, int32 at, const char* needle, bool caseSensitive)
{
	for (int32 i = 0; needle[i]; i++)
		if (FoldChar(text[at + i], caseSensitive) != FoldChar(needle[i], caseSensitive))
			return false;
	return true;
}

int32
PWDocument::FindNext(const char* needle, int32 fromOffset, bool caseSensitive,
	bool wrap, int32* length) const
{
	if (length)
		*length = 0;
	if (!needle || !needle[0])
		return -1;
	const char* text = PlainText();
	int32 docLen = Length();
	int32 needleLen = (int32)strlen(needle);
	if (fromOffset < 0)
		fromOffset = 0;
	for (int32 at = fromOffset; at + needleLen <= docLen; at++)
		if (MatchAt(text, at, needle, caseSensitive)) {
			if (length)
				*length = needleLen;
			return at;
		}
	if (wrap && fromOffset > 0)
		return FindNext(needle, 0, caseSensitive, false, length);
	return -1;
}

int32
PWDocument::ReplaceAll(const char* find, const char* replace,
	bool caseSensitive)
{
	if (!find || !find[0] || strcmp(find, replace) == 0)
		return -1;
	int32 count = 0;
	int32 at = 0;
	int32 len = 0;
	int32 replaceLen = (int32)strlen(replace);
	while (true) {
		int32 found = FindNext(find, at, caseSensitive, false, &len);
		if (found < 0)
			break;
		Remove(found, len);
		Insert(found, replace, NULL);
		at = found + replaceLen;
		count++;
	}
	return count;
}

PWDocument::~PWDocument()
{
	for (Para& p : fParas)
		for (auto& kv : p.images)
			delete kv.second.bitmap;
}
