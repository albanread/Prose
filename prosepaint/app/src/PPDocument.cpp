#include "PPDocument.h"

#include <Entry.h>
#include <File.h>
#include <OS.h>

#include <zlib.h>

#include <algorithm>
#include <cmath>
#include <cstring>

const char* PPDocument::kDocType = "application/x-vnd.prose.ProsePaint-doc";

// --------------------------------------------------------------- canvas --
static int32
PaperWidthPx(const PPPaper& paper, float dpi)
{
	return (int32)(paper.widthPt * dpi / 72.0f + 0.5f);
}

static int32
PaperHeightPx(const PPPaper& paper, float dpi)
{
	return (int32)(paper.heightPt * dpi / 72.0f + 0.5f);
}

PPDocument::PPDocument()
{
	SetCanvas(PaperWidthPx(fPaper, fDPI), PaperHeightPx(fPaper, fDPI),
		fDPI, fPaper, true);
}

void
PPDocument::SetCanvas(int32 width, int32 height, float dpi,
	const PPPaper& paper, bool whiteBackground)
{
	if (width < 1 || height < 1 || dpi < 12.0f)
		return;
	fWidth = width;
	fHeight = height;
	fDPI = dpi;
	fPaper = paper;
	fLayers.clear();
	fUndo.clear();
	fRedo.clear();
	fNextLayerId = 1;
	int32 id = AddLayer("Background");
	if (whiteBackground) {
		PPLayer* l = LayerById(id);
		uint32 bpr = l->bits->BytesPerRow();
		uint8* bits = (uint8*)l->bits->Bits();
		for (int32 y = 0; y < fHeight; y++) {
			uint8* row = bits + (size_t)y * bpr;
			for (int32 x = 0; x < fWidth; x++) {
				row[x * 4 + 0] = 255;	// B
				row[x * 4 + 1] = 255;	// G
				row[x * 4 + 2] = 255;	// R
				row[x * 4 + 3] = 255;	// A
			}
		}
	}
	fModified = false;
}

// --------------------------------------------------------------- layers --
PPLayer*
PPDocument::LayerAt(int32 index)
{
	if (index < 0 || index >= (int32)fLayers.size())
		return NULL;
	return &fLayers[index];
}

PPLayer*
PPDocument::LayerById(int32 id)
{
	for (PPLayer& l : fLayers)
		if (l.id == id)
			return &l;
	return NULL;
}

int32
PPDocument::IndexOf(int32 id) const
{
	for (size_t i = 0; i < fLayers.size(); i++)
		if (fLayers[i].id == id)
			return (int32)i;
	return -1;
}

int32
PPDocument::AddLayer(const char* name)
{
	PPLayer l;
	l.id = fNextLayerId++;
	if (name != NULL && name[0] != '\0')
		l.name = name;
	else {
		l.name.SetToFormat("Layer %d", (int)fNextLayerId - 1);
		if (fLayers.empty())
			l.name = "Background";
	}
	l.bits = std::make_shared<BBitmap>(BRect(0, 0, fWidth - 1, fHeight - 1),
		B_RGBA32);
	if (!l.bits->IsValid())
		return -1;
	memset(l.bits->Bits(), 0, l.bits->BitsLength());
	fLayers.push_back(l);
	fActiveId = l.id;
	MarkDirty(BRect(0, 0, fWidth - 1, fHeight - 1));
	fModified = true;
	return l.id;
}

void
PPDocument::DeleteLayer(int32 id)
{
	if (fLayers.size() <= 1)
		return;		// a painting keeps at least one layer
	int32 idx = IndexOf(id);
	if (idx < 0)
		return;
	fLayers.erase(fLayers.begin() + idx);
	if (fActiveId == id)
		fActiveId = fLayers.back().id;
	MarkDirty(BRect(0, 0, fWidth - 1, fHeight - 1));
	fModified = true;
}

void
PPDocument::MoveLayer(int32 id, bool up)
{
	int32 idx = IndexOf(id);
	int32 to = up ? idx + 1 : idx - 1;
	if (idx < 0 || to < 0 || to >= (int32)fLayers.size())
		return;
	std::swap(fLayers[idx], fLayers[to]);
	MarkDirty(BRect(0, 0, fWidth - 1, fHeight - 1));
	fModified = true;
}

void
PPDocument::RenameLayer(int32 id, const char* name)
{
	PPLayer* l = LayerById(id);
	if (l != NULL && name != NULL && name[0] != '\0') {
		l->name = name;
		fModified = true;
	}
}

void
PPDocument::SetLayerVisible(int32 id, bool visible)
{
	PPLayer* l = LayerById(id);
	if (l != NULL && l->visible != visible) {
		l->visible = visible;
		MarkDirty(BRect(0, 0, fWidth - 1, fHeight - 1));
		fModified = true;
	}
}

void
PPDocument::SetLayerOpacity(int32 id, uint8 opacity)
{
	PPLayer* l = LayerById(id);
	if (l != NULL && l->opacity != opacity) {
		l->opacity = opacity;
		MarkDirty(BRect(0, 0, fWidth - 1, fHeight - 1));
		fModified = true;
	}
}

void
PPDocument::SetActiveLayer(int32 id)
{
	if (LayerById(id) != NULL)
		fActiveId = id;
}

// -------------------------------------------------------------- painting --
void
PPDocument::MarkDirty(const BRect& r)
{
	BRect c = r & BRect(0, 0, fWidth - 1, fHeight - 1);
	if (!c.IsValid())
		return;
	fCompositeDirty = true;
	if (!fDirty.IsValid())
		fDirty = c;
	else
		fDirty = fDirty | c;
}

void
PPDocument::StrokeBegin()
{
	fInStroke = true;
	fDirty = BRect();
	fSmudgeFoot.clear();
	fCurrentStep = UndoStep();
	fCurrentStep.layerId = fActiveId;
	// the pristine layer, for this stroke's "before" bytes
	PPLayer* l = ActiveLayer();
	if (l != NULL && l->bits != NULL) {
		fBeforeSnap = std::make_shared<BBitmap>(l->bits->Bounds(),
			B_RGBA32);
		if (fBeforeSnap->IsValid() && fBeforeSnap->ImportBits(l->bits.get())
				!= B_OK)
			fBeforeSnap.reset();
	} else
		fBeforeSnap.reset();
}

void
PPDocument::CaptureAfter(UndoStep& step)
{
	PPLayer* l = LayerById(step.layerId);
	if (l == NULL || !step.rect.IsValid())
		return;
	int32 x0 = (int32)step.rect.left, y0 = (int32)step.rect.top;
	int32 w = (int32)step.rect.IntegerWidth() + 1;
	int32 h = (int32)step.rect.IntegerHeight() + 1;
	step.after.resize((size_t)w * h * 4);
	uint32 bpr = l->bits->BytesPerRow();
	const uint8* bits = (const uint8*)l->bits->Bits();
	for (int32 y = 0; y < h; y++)
		memcpy(step.after.data() + (size_t)y * w * 4,
			bits + (size_t)(y0 + y) * bpr + (size_t)x0 * 4,
			(size_t)w * 4);
}

void
PPDocument::StrokeEnd()
{
	if (!fInStroke)
		return;
	fInStroke = false;
	if (!fCurrentStep.rect.IsValid()) {
		fBeforeSnap.reset();
		return;		// nothing was painted
	}
	PPLayer* l = LayerById(fCurrentStep.layerId);
	if (l == NULL || fBeforeSnap == NULL) {
		fBeforeSnap.reset();
		return;
	}
	int32 x0 = (int32)fCurrentStep.rect.left, y0 = (int32)fCurrentStep.rect.top;
	int32 w = (int32)fCurrentStep.rect.IntegerWidth() + 1;
	int32 h = (int32)fCurrentStep.rect.IntegerHeight() + 1;
	// before: from the stroke-start snapshot; after: from the layer
	fCurrentStep.before.resize((size_t)w * h * 4);
	uint32 snapBpr = fBeforeSnap->BytesPerRow();
	const uint8* snap = (const uint8*)fBeforeSnap->Bits();
	for (int32 y = 0; y < h; y++)
		memcpy(fCurrentStep.before.data() + (size_t)y * w * 4,
			snap + (size_t)(y0 + y) * snapBpr + (size_t)x0 * 4,
			(size_t)w * 4);
	fBeforeSnap.reset();
	CaptureAfter(fCurrentStep);
	fUndo.push_back(fCurrentStep);
	if (fUndo.size() > 40)
		fUndo.erase(fUndo.begin());
	fRedo.clear();
	fModified = true;
	fCurrentStep = UndoStep();
}

void
PPDocument::DabColour(int32 x, int32 y, rgb_color colour,
	const uint8* mask, int32 maskSize, int32 maskBpr, uint8 flow)
{
	PPLayer* l = ActiveLayer();
	if (l == NULL || mask == NULL)
		return;
	int32 half = maskSize / 2;
	int32 x0 = x - half, y0 = y - half;
	// blend colour into the layer under mask*flow, straight alpha
	uint32 bpr = l->bits->BytesPerRow();
	uint8* bits = (uint8*)l->bits->Bits();
	for (int32 my = 0; my < maskSize; my++) {
		int32 py = y0 + my;
		if (py < 0 || py >= fHeight)
			continue;
		for (int32 mx = 0; mx < maskSize; mx++) {
			int32 px = x0 + mx;
			if (px < 0 || px >= fWidth)
				continue;
			uint8 m = (uint8)((uint32)mask[my * maskBpr + mx] * flow / 255);
			if (m == 0)
				continue;
			uint8* d = bits + (size_t)py * bpr + (size_t)px * 4;
			// d is B,G,R,A
			uint32 dstA = d[3];
			uint32 outA = m + dstA * (255 - m) / 255;
			if (outA == 0)
				continue;
			for (int32 c = 0; c < 3; c++) {
				uint32 src = c == 0 ? colour.blue
					: c == 1 ? colour.green : colour.red;
				uint32 dcv = d[c];
				uint32 out = (src * m + dcv * dstA * (255 - m) / 255)
					/ outA;
				d[c] = (uint8)std::min(255u, out);
			}
			d[3] = (uint8)outA;
		}
	}
	MarkDirty(BRect(x0, y0, x0 + maskSize - 1, y0 + maskSize - 1));
	if (fInStroke) {
		if (!fCurrentStep.rect.IsValid())
			fCurrentStep.rect = fDirty;
		else
			fCurrentStep.rect = fCurrentStep.rect | fDirty;
	}
}

void
PPDocument::DabErase(int32 x, int32 y,
	const uint8* mask, int32 maskSize, int32 maskBpr, uint8 flow)
{
	PPLayer* l = ActiveLayer();
	if (l == NULL || mask == NULL)
		return;
	int32 half = maskSize / 2;
	int32 x0 = x - half, y0 = y - half;
	uint32 bpr = l->bits->BytesPerRow();
	uint8* bits = (uint8*)l->bits->Bits();
	for (int32 my = 0; my < maskSize; my++) {
		int32 py = y0 + my;
		if (py < 0 || py >= fHeight)
			continue;
		for (int32 mx = 0; mx < maskSize; mx++) {
			int32 px = x0 + mx;
			if (px < 0 || px >= fWidth)
				continue;
			uint8 m = (uint8)((uint32)mask[my * maskBpr + mx] * flow / 255);
			if (m == 0)
				continue;
			uint8* d = bits + (size_t)py * bpr + (size_t)px * 4;
			uint32 a = d[3] * (255 - m) / 255;
			d[3] = (uint8)a;
			// straight alpha: colour channels scale with alpha so the
			// pixel stays well-formed for the next blend
			d[0] = (uint8)(d[0] * a / 255);
			d[1] = (uint8)(d[1] * a / 255);
			d[2] = (uint8)(d[2] * a / 255);
		}
	}
	MarkDirty(BRect(x0, y0, x0 + maskSize - 1, y0 + maskSize - 1));
	if (fInStroke) {
		if (!fCurrentStep.rect.IsValid())
			fCurrentStep.rect = fDirty;
		else
			fCurrentStep.rect = fCurrentStep.rect | fDirty;
	}
}

void
PPDocument::DabSmudge(int32 x, int32 y,
	const uint8* mask, int32 maskSize, int32 maskBpr, uint8 strength)
{
	PPLayer* l = ActiveLayer();
	if (l == NULL || mask == NULL)
		return;
	int32 half = maskSize / 2;
	int32 x0 = x - half, y0 = y - half;
	uint32 bpr = l->bits->BytesPerRow();
	uint8* bits = (uint8*)l->bits->Bits();
	if ((int32)fSmudgeFoot.size() != maskSize * maskSize * 4) {
		// first touch: just pick up what's under the brush
		fSmudgeFoot.resize((size_t)maskSize * maskSize * 4, 0);
		for (int32 my = 0; my < maskSize; my++) {
			int32 py = y0 + my;
			for (int32 mx = 0; mx < maskSize; mx++) {
				int32 px = x0 + mx;
				uint8* foot = fSmudgeFoot.data()
					+ ((size_t)my * maskSize + mx) * 4;
				if (px < 0 || px >= fWidth || py < 0 || py >= fHeight) {
					foot[3] = 0;	// outside: transparent
					continue;
				}
				const uint8* d = bits + (size_t)py * bpr + (size_t)px * 4;
				foot[0] = d[0]; foot[1] = d[1];
				foot[2] = d[2]; foot[3] = d[3];
			}
		}
		return;
	}
	// mix: strength is how much of the carried footprint SURVIVES —
	// the rest becomes what is under the brush — and the result is
	// written back. Full strength drags everything along the path
	// (the ink follows the stroke); half strength smears gently.
	for (int32 my = 0; my < maskSize; my++) {
		int32 py = y0 + my;
		if (py < 0 || py >= fHeight)
			continue;
		for (int32 mx = 0; mx < maskSize; mx++) {
			int32 px = x0 + mx;
			if (px < 0 || px >= fWidth)
				continue;
			if (mask[my * maskBpr + mx] == 0)
				continue;
			uint8* d = bits + (size_t)py * bpr + (size_t)px * 4;
			uint8* foot = fSmudgeFoot.data()
				+ ((size_t)my * maskSize + mx) * 4;
			for (int32 c = 0; c < 4; c++)
				foot[c] = (uint8)(((uint32)foot[c] * strength
					+ (uint32)d[c] * (255 - strength)) / 255);
			d[0] = foot[0]; d[1] = foot[1];
			d[2] = foot[2]; d[3] = foot[3];
		}
	}
	MarkDirty(BRect(x0, y0, x0 + maskSize - 1, y0 + maskSize - 1));
	if (fInStroke) {
		if (!fCurrentStep.rect.IsValid())
			fCurrentStep.rect = fDirty;
		else
			fCurrentStep.rect = fCurrentStep.rect | fDirty;
	}
}

// -------------------------------------------------------------- composite --
int32
PPDocument::FillAt(int32 x, int32 y, rgb_color colour, uint8 tolerance)
{
	PPLayer* l = ActiveLayer();
	if (l == NULL || x < 0 || x >= fWidth || y < 0 || y >= fHeight)
		return 0;
	uint32 bpr = l->bits->BytesPerRow();
	uint8* bits = (uint8*)l->bits->Bits();
	const uint8* seedPtr = bits + (size_t)y * bpr + (size_t)x * 4;
	// a COPY: the seed pixel gets painted by the very first run, and
	// comparing through the pointer would then match nothing
	uint8 seed[4] = { seedPtr[0], seedPtr[1], seedPtr[2], seedPtr[3] };
	auto near = [&](const uint8* p) -> bool
	{
		if (tolerance == 0)
			return p[0] == seed[0] && p[1] == seed[1]
				&& p[2] == seed[2] && p[3] == seed[3];
		for (int32 c = 0; c < 4; c++)
			if ((uint8)std::abs((int)p[c] - (int)seed[c]) > tolerance)
				return false;
		return true;
	};
	// scanline flood fill with a visited bitmap
	std::vector<uint8> visited((size_t)fWidth * fHeight, 0);
	std::vector<int32> stack;
	stack.push_back(y * fWidth + x);
	int32 filled = 0;
	while (!stack.empty()) {
		int32 idx = stack.back();
		stack.pop_back();
		int32 px = idx % fWidth, py = idx / fWidth;
		if (visited[idx])
			continue;
		// walk the run left/right
		int32 left = px;
		while (left > 0 && !visited[py * fWidth + left - 1]
			&& near(bits + (size_t)py * bpr + (size_t)(left - 1) * 4))
			left--;
		int32 right = px;
		while (right < fWidth - 1
			&& !visited[py * fWidth + right + 1]
			&& near(bits + (size_t)py * bpr + (size_t)(right + 1) * 4))
			right++;
		for (int32 fx = left; fx <= right; fx++) {
			visited[py * fWidth + fx] = 1;
			uint8* d = bits + (size_t)py * bpr + (size_t)fx * 4;
			d[0] = colour.blue; d[1] = colour.green;
			d[2] = colour.red; d[3] = 255;
			filled++;
		}
		// seed the rows above and below
		for (int32 fx = left; fx <= right; fx++) {
			if (py > 0) {
				int32 i2 = (py - 1) * fWidth + fx;
				if (!visited[i2] && near(bits + (size_t)(py - 1) * bpr
						+ (size_t)fx * 4))
					stack.push_back(i2);
			}
			if (py < fHeight - 1) {
				int32 i2 = (py + 1) * fWidth + fx;
				if (!visited[i2] && near(bits + (size_t)(py + 1) * bpr
						+ (size_t)fx * 4))
					stack.push_back(i2);
			}
		}
	}
	MarkDirty(BRect(0, 0, fWidth - 1, fHeight - 1));
	// fill is one undo step: capture the whole layer cheaply via the
	// stroke machinery with a full-canvas rect
	if (fInStroke) {
		if (!fCurrentStep.rect.IsValid())
			fCurrentStep.rect = BRect(0, 0, fWidth - 1, fHeight - 1);
		else
			fCurrentStep.rect = fCurrentStep.rect
				| BRect(0, 0, fWidth - 1, fHeight - 1);
	}
	fModified = true;
	return filled;
}

rgb_color
PPDocument::PickColour(int32 x, int32 y) const
{
	rgb_color out = { 0, 0, 0, 0 };
	if (x < 0 || x >= fWidth || y < 0 || y >= fHeight)
		return out;
	const BBitmap* comp = const_cast<PPDocument*>(this)->Composite();
	if (comp == NULL)
		return out;
	uint32 bpr = comp->BytesPerRow();
	const uint8* p = (const uint8*)comp->Bits()
		+ (size_t)y * bpr + (size_t)x * 4;
	out.blue = p[0]; out.green = p[1];
	out.red = p[2]; out.alpha = p[3];
	return out;
}

// -------------------------------------------------------------- composite --
const BBitmap*
PPDocument::Composite()
{
	if (!fCompositeDirty && fComposite != NULL)
		return fComposite.get();
	if (fComposite == NULL || fComposite->Bounds().Width() != fWidth - 1
		|| fComposite->Bounds().Height() != fHeight - 1) {
		fComposite = std::make_shared<BBitmap>(
			BRect(0, 0, fWidth - 1, fHeight - 1), B_RGBA32);
	}
	memset(fComposite->Bits(), 0, fComposite->BitsLength());
	uint32 dstBpr = fComposite->BytesPerRow();
	uint8* dst = (uint8*)fComposite->Bits();
	for (const PPLayer& l : fLayers) {
		if (!l.visible || l.opacity == 0 || l.bits == NULL)
			continue;
		uint32 srcBpr = l.bits->BytesPerRow();
		const uint8* src = (const uint8*)l.bits->Bits();
		for (int32 y = 0; y < fHeight; y++) {
			uint8* drow = dst + (size_t)y * dstBpr;
			const uint8* srow = src + (size_t)y * srcBpr;
			for (int32 x = 0; x < fWidth; x++) {
				uint8* d = drow + x * 4;
				const uint8* s = srow + x * 4;
				uint32 layerA = (uint32)s[3] * l.opacity / 255;
				uint32 dstA = d[3];
				uint32 outA = layerA + dstA * (255 - layerA) / 255;
				if (outA == 0) {
					d[0] = d[1] = d[2] = d[3] = 0;
					continue;
				}
				for (int32 c = 0; c < 3; c++) {
					uint32 out = ((uint32)s[c] * layerA
						+ (uint32)d[c] * dstA * (255 - layerA) / 255)
						/ outA;
					d[c] = (uint8)std::min(255u, out);
				}
				d[3] = (uint8)outA;
			}
		}
	}
	fCompositeDirty = false;
	return fComposite.get();
}

// ------------------------------------------------------------------ undo --
void
PPDocument::ApplyStep(const UndoStep& step, bool forward)
{
	PPLayer* l = LayerById(step.layerId);
	if (l == NULL)
		return;
	int32 x0 = (int32)step.rect.left, y0 = (int32)step.rect.top;
	int32 w = (int32)step.rect.IntegerWidth() + 1;
	int32 h = (int32)step.rect.IntegerHeight() + 1;
	const std::vector<uint8>& bytes = forward ? step.after : step.before;
	uint32 bpr = l->bits->BytesPerRow();
	uint8* bits = (uint8*)l->bits->Bits();
	for (int32 y = 0; y < h; y++)
		memcpy(bits + (size_t)(y0 + y) * bpr + (size_t)x0 * 4,
			bytes.data() + (size_t)y * w * 4, (size_t)w * 4);
	MarkDirty(step.rect);
	fModified = true;
}

void
PPDocument::Undo()
{
	if (fUndo.empty())
		return;
	UndoStep step = fUndo.back();
	fUndo.pop_back();
	// redo needs the same step; before/after already captured
	ApplyStep(step, false);
	fRedo.push_back(step);
}

void
PPDocument::Redo()
{
	if (fRedo.empty())
		return;
	UndoStep step = fRedo.back();
	fRedo.pop_back();
	ApplyStep(step, true);
	fUndo.push_back(step);
}

// ------------------------------------------------------------ persistence --
static bool
Flate(const uint8* src, size_t len, std::vector<uint8>* out)
{
	uLongf compressed = compressBound((uLong)len);
	out->resize(compressed);
	if (compress2((Bytef*)out->data(), &compressed, (const Bytef*)src,
			(uLong)len, 6) != Z_OK)
		return false;
	out->resize(compressed);
	return true;
}

static bool
Unflate(const uint8* src, size_t srcLen, uint8* dst, size_t dstLen)
{
	uLongf dstL = dstLen;
	return uncompress((Bytef*)dst, &dstL, (const Bytef*)src,
		(uLong)srcLen) == Z_OK && dstL == dstLen;
}

status_t
PPDocument::SaveToMessage(BMessage* msg) const
{
	msg->what = 'pPt&';
	msg->AddInt32("w", fWidth);
	msg->AddInt32("h", fHeight);
	msg->AddFloat("dpi", fDPI);
	msg->AddString("paper", fPaper.name);
	msg->AddFloat("pw", fPaper.widthPt);
	msg->AddFloat("ph", fPaper.heightPt);
	for (const PPLayer& l : fLayers) {
		BMessage m;
		m.AddInt32("id", l.id);
		m.AddString("name", l.name.String());
		m.AddBool("vis", l.visible);
		m.AddInt32("opa", l.opacity);
		std::vector<uint8> flat;
		size_t raw = (size_t)l.bits->BytesPerRow() * fHeight;
		if (!Flate((const uint8*)l.bits->Bits(), raw, &flat))
			return B_ERROR;
		m.AddData("bits", B_RAW_TYPE, flat.data(), (ssize_t)flat.size());
		m.AddInt32("bpr", l.bits->BytesPerRow());
		msg->AddMessage("layer", &m);
	}
	return B_OK;
}

status_t
PPDocument::LoadFromMessage(const BMessage* msg)
{
	int32 w = 0, h = 0;
	if (msg->FindInt32("w", &w) != B_OK || msg->FindInt32("h", &h) != B_OK
		|| w < 1 || h < 1 || w > 8192 || h > 8192)
		return B_BAD_VALUE;
	fWidth = w;
	fHeight = h;
	msg->FindFloat("dpi", &fDPI);
	if (fDPI < 12.0f)
		fDPI = 96.0f;
	const char* paperName = NULL;
	if (msg->FindString("paper", &paperName) == B_OK)
		fPaper.name = paperName;
	msg->FindFloat("pw", &fPaper.widthPt);
	msg->FindFloat("ph", &fPaper.heightPt);
	if (fPaper.widthPt <= 0)
		fPaper.widthPt = w * 72.0f / fDPI;
	if (fPaper.heightPt <= 0)
		fPaper.heightPt = h * 72.0f / fDPI;

	fLayers.clear();
	fUndo.clear();
	fRedo.clear();
	fNextLayerId = 1;
	BMessage m;
	for (int32 i = 0; msg->FindMessage("layer", i, &m) == B_OK; i++) {
		PPLayer l;
		m.FindInt32("id", &l.id);
		const char* name = NULL;
		if (m.FindString("name", &name) == B_OK)
			l.name = name;
		m.FindBool("vis", &l.visible);
		int32 opa = 255;
		m.FindInt32("opa", &opa);
		l.opacity = (uint8)opa;
		int32 bpr = 0;
		m.FindInt32("bpr", &bpr);
		const void* data = NULL;
		ssize_t size = 0;
		if (m.FindData("bits", B_RAW_TYPE, &data, &size) != B_OK
			|| bpr < w * 4) {
			continue;
		}
		l.bits = std::make_shared<BBitmap>(BRect(0, 0, w - 1, h - 1),
			B_RGBA32);
		if (!l.bits->IsValid()
			|| (size_t)l.bits->BytesPerRow() * h
				> (size_t)l.bits->BitsLength()) {
			continue;
		}
		if (!Unflate((const uint8*)data, size, (uint8*)l.bits->Bits(),
				(size_t)l.bits->BytesPerRow() * h)) {
			continue;
		}
		fLayers.push_back(l);
		fNextLayerId = std::max(fNextLayerId, l.id + 1);
	}
	if (fLayers.empty())
		return B_ERROR;
	fActiveId = fLayers.back().id;
	MarkDirty(BRect(0, 0, fWidth - 1, fHeight - 1));
	fModified = false;
	return B_OK;
}

status_t
PPDocument::SaveToFile(const char* path) const
{
	BMessage msg;
	status_t err = SaveToMessage(&msg);
	if (err != B_OK)
		return err;
	ssize_t size = msg.FlattenedSize();
	char* buffer = new (std::nothrow) char[size];
	if (buffer == NULL)
		return B_NO_MEMORY;
	err = msg.Flatten(buffer, size);
	if (err != B_OK) {
		delete[] buffer;
		return err;
	}
	// atomic: write beside, flush, rename (a Prose suite invariant)
	BString tmpPath(path);
	tmpPath << ".pptmp";
	{
		BFile file;
		err = file.SetTo(tmpPath.String(),
			B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
		if (err == B_OK) {
			ssize_t written = 0;
			while (written < size) {
				ssize_t n = file.Write(buffer + written, size - written);
				if (n <= 0) {
					err = n < 0 ? (status_t)n : B_ERROR;
					break;
				}
				written += n;
			}
			if (err == B_OK)
				err = file.Sync();
		}
	}
	if (err == B_OK) {
		BEntry entry;
		err = entry.SetTo(tmpPath.String());
		if (err == B_OK)
			err = entry.Rename(path, true);
	}
	if (err != B_OK) {
		BEntry corpse;
		if (corpse.SetTo(tmpPath.String()) == B_OK)
			corpse.Remove();
	}
	delete[] buffer;
	return err;
}

status_t
PPDocument::LoadFromFile(const char* path)
{
	BFile file;
	status_t err = file.SetTo(path, B_READ_ONLY);
	if (err != B_OK)
		return err;
	off_t size = 0;
	file.GetSize(&size);
	if (size <= 0 || size > 256LL * 1024 * 1024)
		return B_BAD_VALUE;
	char* buffer = new (std::nothrow) char[size];
	if (buffer == NULL)
		return B_NO_MEMORY;
	ssize_t got = 0;
	while (got < (ssize_t)size) {
		ssize_t n = file.Read(buffer + got, size - got);
		if (n < 0) {
			got = -1;
			break;
		}
		if (n == 0)
			break;
		got += n;
	}
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

PPDocKind
PP_SniffDocument(const char* path)
{
	BFile file;
	if (file.SetTo(path, B_READ_ONLY) != B_OK)
		return PP_KIND_ERROR;
	char head[8] = { 0 };
	ssize_t got = file.Read(head, sizeof(head));
	if (got >= 8 && memcmp(head, "HMF1", 4) == 0
		&& memcmp(head + 4, "&tPp", 4) == 0)
		return PP_KIND_NATIVE;
	return PP_KIND_OTHER;
}
