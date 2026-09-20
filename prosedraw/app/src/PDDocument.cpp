#include "PDDocument.h"

#include <Entry.h>
#include <File.h>
#include <Node.h>

#include <algorithm>
#include <cmath>
#include <cstring>

const char* PDDocument::kDocType = "application/x-vnd.prose.ProseDraw-doc";

// ---------------------------------------------------------------- style --
static int32
ColorToInt(const rgb_color& c)
{
	// little-endian arm64: red | green<<8 | blue<<16 | alpha<<24
	return (int32)c.red | ((int32)c.green << 8) | ((int32)c.blue << 16)
		| ((int32)c.alpha << 24);
}

static rgb_color
IntToColor(int32 v)
{
	return rgb_color { (uint8)(v & 0xFF), (uint8)((v >> 8) & 0xFF),
		(uint8)((v >> 16) & 0xFF), (uint8)((v >> 24) & 0xFF) };
}

void
PDStyle::Archive(BMessage* into) const
{
	into->AddBool("fillOn", fillOn);
	into->AddInt32("fill", ColorToInt(fill));
	into->AddInt32("stroke", ColorToInt(stroke));
	into->AddFloat("sw", strokeWidth);
	into->AddBool("dashed", dashed);
	into->AddFloat("ts", textSize);
	into->AddInt32("tc", ColorToInt(textColor));
}

void
PDStyle::Unarchive(const BMessage* from)
{
	from->FindBool("fillOn", &fillOn);
	int32 c = ColorToInt(fill);
	from->FindInt32("fill", &c);
	fill = IntToColor(c);
	c = ColorToInt(stroke);
	from->FindInt32("stroke", &c);
	stroke = IntToColor(c);
	from->FindFloat("sw", &strokeWidth);
	from->FindBool("dashed", &dashed);
	from->FindFloat("ts", &textSize);
	c = ColorToInt(textColor);
	from->FindInt32("tc", &c);
	textColor = IntToColor(c);
}

// -------------------------------------------------------------- document --
PDDocument::PDDocument()
{
}

PDShape*
PDDocument::ShapeById(int32 id)
{
	for (PDShape& s : fShapes)
		if (s.id == id)
			return &s;
	return NULL;
}

int32
PDDocument::IndexOf(int32 id) const
{
	for (size_t i = 0; i < fShapes.size(); i++)
		if (fShapes[i].id == id)
			return (int32)i;
	return -1;
}

BRect
PDDocument::SnapRect(BRect r) const
{
	if (!fSnap)
		return r;
	float g = fGrid;
	return BRect(roundf(r.left / g) * g, roundf(r.top / g) * g,
		roundf(r.right / g) * g, roundf(r.bottom / g) * g);
}

PDShape*
PDDocument::AddShape(PDShapeKind kind, BRect rect, const char* label)
{
	Snapshot();
	PDShape s;
	s.id = fNextId++;
	s.kind = kind;
	s.rect = SnapRect(rect);
	if (kind == PD_TEXT) {
		s.style.fillOn = false;
		s.style.strokeWidth = 0;
	}
	if (label)
		s.label = label;
	fShapes.push_back(s);
	fModified = true;
	return &fShapes.back();
}

void
PDDocument::RemoveShapes(const std::vector<int32>& ids)
{
	Snapshot();
	// connectors dangling off removed shapes go too — enforced here so no
	// caller can leave the model inconsistent
	std::vector<int32> gone(ids);
	for (const PDShape& s : fShapes) {
		if (s.kind == PD_CONNECTOR
			&& (std::find(gone.begin(), gone.end(), s.fromId) != gone.end()
				|| std::find(gone.begin(), gone.end(), s.toId)
					!= gone.end()))
			gone.push_back(s.id);
	}
	fShapes.erase(std::remove_if(fShapes.begin(), fShapes.end(),
		[&gone](const PDShape& s) {
			return std::find(gone.begin(), gone.end(), s.id) != gone.end();
		}), fShapes.end());
	fModified = true;
}

void
PDDocument::SetShapeRect(int32 id, BRect rect)
{
	PDShape* s = ShapeById(id);
	if (s == NULL || s->rect == rect)
		return;
	Snapshot();
	s->rect = SnapRect(rect);
	fModified = true;
}

void
PDDocument::SetShapeLabel(int32 id, const char* label)
{
	PDShape* s = ShapeById(id);
	if (s == NULL || s->label == label)
		return;
	Snapshot();
	s->label = label;
	fModified = true;
}

void
PDDocument::SetShapeStyle(int32 id, const PDStyle& style)
{
	PDShape* s = ShapeById(id);
	if (s == NULL || s->style == style)
		return;
	Snapshot();
	s->style = style;
	fModified = true;
}

void
PDDocument::MoveZ(int32 id, bool toFront)
{
	int32 i = IndexOf(id);
	if (i < 0)
		return;
	if (toFront && i + 1 == (int32)fShapes.size())
		return;
	if (!toFront && i == 0)
		return;
	Snapshot();
	PDShape s = fShapes[i];
	fShapes.erase(fShapes.begin() + i);
	fShapes.insert(toFront ? fShapes.end() : fShapes.begin(), s);
	fModified = true;
}

bool
PDDocument::Align(const std::vector<int32>& ids, PDAlign mode)
{
	if (ids.size() < (mode == PD_DISTRIBE_H || mode == PD_DISTRIBE_V ? 3 : 2))
		return false;
	std::vector<PDShape*> sel;
	for (int32 id : ids) {
		PDShape* s = ShapeById(id);
		if (s != NULL && s->kind != PD_CONNECTOR)
			sel.push_back(s);
	}
	if (sel.size() != ids.size() || sel.size() < 2)
		return false;

	float left = sel[0]->rect.left, right = sel[0]->rect.right;
	float top = sel[0]->rect.top, bottom = sel[0]->rect.bottom;
	for (const PDShape* s : sel) {
		left = std::min(left, s->rect.left);
		right = std::max(right, s->rect.right);
		top = std::min(top, s->rect.top);
		bottom = std::max(bottom, s->rect.bottom);
	}
	float midX = (left + right) / 2, midY = (top + bottom) / 2;

	Snapshot();
	switch (mode) {
		case PD_ALIGN_LEFT:
			for (PDShape* s : sel) s->rect.OffsetBy(left - s->rect.left, 0);
			break;
		case PD_ALIGN_RIGHT:
			for (PDShape* s : sel) s->rect.OffsetBy(right - s->rect.right, 0);
			break;
		case PD_ALIGN_HCENTER:
			for (PDShape* s : sel)
				s->rect.OffsetBy(midX - (s->rect.left + s->rect.right) / 2, 0);
			break;
		case PD_ALIGN_TOP:
			for (PDShape* s : sel) s->rect.OffsetBy(0, top - s->rect.top);
			break;
		case PD_ALIGN_BOTTOM:
			for (PDShape* s : sel) s->rect.OffsetBy(0, bottom - s->rect.bottom);
			break;
		case PD_ALIGN_VCENTER:
			for (PDShape* s : sel)
				s->rect.OffsetBy(0, midY - (s->rect.top + s->rect.bottom) / 2);
			break;
		case PD_DISTRIBE_H:
		case PD_DISTRIBE_V:
		{
			// even gaps between the selection boxes, ordered along the axis
			std::vector<PDShape*> order = sel;
			std::sort(order.begin(), order.end(),
				[mode](const PDShape* a, const PDShape* b) {
					return mode == PD_DISTRIBE_H ? a->rect.left < b->rect.left
						: a->rect.top < b->rect.top;
				});
			if (mode == PD_DISTRIBE_H) {
				float total = 0;
				for (PDShape* s : order) total += s->rect.Width();
				float gap = (right - left - total) / (order.size() - 1);
				float x = left;
				for (PDShape* s : order) {
					s->rect.OffsetBy(x - s->rect.left, 0);
					x += s->rect.Width() + gap;
				}
			} else {
				float total = 0;
				for (PDShape* s : order) total += s->rect.Height();
				float gap = (bottom - top - total) / (order.size() - 1);
				float y = top;
				for (PDShape* s : order) {
					s->rect.OffsetBy(0, y - s->rect.top);
					y += s->rect.Height() + gap;
				}
			}
			break;
		}
	}
	fModified = true;
	return true;
}

// ----------------------------------------------------------- connectors --
BPoint
PDDocument::AnchorPoint(const PDShape& from, const PDShape& to)
{
	// Where the centre-to-centre line leaves `from's box: scale the
	// direction to the first border it crosses.
	BPoint fc((from.rect.left + from.rect.right) / 2,
		(from.rect.top + from.rect.bottom) / 2);
	BPoint tc((to.rect.left + to.rect.right) / 2,
		(to.rect.top + to.rect.bottom) / 2);
	float dx = tc.x - fc.x, dy = tc.y - fc.y;
	if (fabsf(dx) < 0.01f && fabsf(dy) < 0.01f)
		return BPoint(fc.x, from.rect.top);	// stacked: leave north
	float hw = from.rect.Width() / 2, hh = from.rect.Height() / 2;
	float kx = fabsf(dx) < 0.01f ? 1e9f : hw / fabsf(dx);
	float ky = fabsf(dy) < 0.01f ? 1e9f : hh / fabsf(dy);
	float k = std::min(kx, ky);
	return BPoint(fc.x + dx * k, fc.y + dy * k);
}

int32
PDDocument::ShapeAtPoint(BPoint p) const
{
	for (int32 i = (int32)fShapes.size() - 1; i >= 0; i--) {
		const PDShape& s = fShapes[i];
		if (s.kind != PD_CONNECTOR && s.rect.Contains(p))
			return s.id;
	}
	return 0;
}

int32
PDDocument::ConnectorAtPoint(BPoint p) const
{
	for (int32 i = (int32)fShapes.size() - 1; i >= 0; i--) {
		const PDShape& c = fShapes[i];
		if (c.kind != PD_CONNECTOR)
			continue;
		const PDShape* from = const_cast<PDDocument*>(this)->ShapeById(
			c.fromId);
		const PDShape* to = const_cast<PDDocument*>(this)->ShapeById(c.toId);
		if (from == NULL || to == NULL)
			continue;
		BPoint a = AnchorPoint(*from, *to);
		BPoint b = AnchorPoint(*to, *from);
		// distance from p to segment ab
		float abx = b.x - a.x, aby = b.y - a.y;
		float len2 = abx * abx + aby * aby;
		float t = len2 > 0 ? ((p.x - a.x) * abx + (p.y - a.y) * aby) / len2
			: 0;
		t = std::max(0.0f, std::min(1.0f, t));
		BPoint q(a.x + abx * t, a.y + aby * t);
		if (fabsf(p.x - q.x) + fabsf(p.y - q.y) < 6)
			return c.id;
	}
	return 0;
}

// ------------------------------------------------------------------ undo --
void
PDDocument::Snapshot()
{
	fUndo.push_back(std::make_pair(fShapes, fNextId));
	if (fUndo.size() > 200)
		fUndo.erase(fUndo.begin());
	fRedo.clear();
	fModified = true;
}

void
PDDocument::Restore(std::vector<PDShape> shapes, int32 nextId)
{
	fRedo.push_back(std::make_pair(fShapes, fNextId));
	fShapes.swap(shapes);
	fNextId = nextId;
	fModified = true;
}

void
PDDocument::Undo()
{
	if (fUndo.empty())
		return;
	std::pair<std::vector<PDShape>, int32> s = fUndo.back();
	fUndo.pop_back();
	Restore(s.first, s.second);
}

void
PDDocument::Redo()
{
	if (fRedo.empty())
		return;
	std::pair<std::vector<PDShape>, int32> s = fRedo.back();
	fRedo.pop_back();
	fUndo.push_back(std::make_pair(fShapes, fNextId));
	fShapes.swap(s.first);
	fNextId = s.second;
	fModified = true;
}

// ----------------------------------------------------------- persistence --
status_t
PDDocument::SaveToMessage(BMessage* msg) const
{
	msg->AddFloat("pw", fPage.width);
	msg->AddFloat("ph", fPage.height);
	msg->AddFloat("grid", fGrid);
	for (const PDShape& s : fShapes) {
		BMessage m('pDs&');
		m.AddInt32("id", s.id);
		m.AddInt8("kind", (int8)s.kind);
		m.AddFloat("l", s.rect.left);
		m.AddFloat("t", s.rect.top);
		m.AddFloat("r", s.rect.right);
		m.AddFloat("b", s.rect.bottom);
		if (s.label.Length() > 0)
			m.AddString("label", s.label);
		s.style.Archive(&m);
		m.AddInt32("from", s.fromId);
		m.AddInt32("to", s.toId);
		m.AddBool("ae", s.arrowEnd);
		m.AddBool("as", s.arrowStart);
		msg->AddMessage("shape", &m);
	}
	return B_OK;
}

status_t
PDDocument::LoadFromMessage(const BMessage* msg)
{
	fShapes.clear();
	fUndo.clear();
	fRedo.clear();
	fNextId = 1;
	msg->FindFloat("pw", &fPage.width);
	msg->FindFloat("ph", &fPage.height);
	msg->FindFloat("grid", &fGrid);
	BMessage m;
	for (int32 i = 0; msg->FindMessage("shape", i, &m) == B_OK; i++) {
		PDShape s;
		m.FindInt32("id", &s.id);
		int8 k = PD_RECT;
		m.FindInt8("kind", &k);
		s.kind = (PDShapeKind)k;
		float l = 0, t = 0, r = 0, b = 0;
		m.FindFloat("l", &l);
		m.FindFloat("t", &t);
		m.FindFloat("r", &r);
		m.FindFloat("b", &b);
		s.rect.Set(l, t, r, b);
		const char* label = NULL;
		if (m.FindString("label", &label) == B_OK)
			s.label = label;
		s.style.Unarchive(&m);
		m.FindInt32("from", &s.fromId);
		m.FindInt32("to", &s.toId);
		m.FindBool("ae", &s.arrowEnd);
		m.FindBool("as", &s.arrowStart);
		fShapes.push_back(s);
		fNextId = std::max(fNextId, s.id + 1);
	}
	fModified = false;
	return B_OK;
}

status_t
PDDocument::SaveToFile(const char* path) const
{
	BMessage msg('pDd&');
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
	// atomic: write beside, flush, rename — never clobber the user's
	// diagram with a half-written file
	BString tmpPath(path);
	tmpPath << ".pdtmp";
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
PDDocument::LoadFromFile(const char* path)
{
	BFile file;
	status_t err = file.SetTo(path, B_READ_ONLY);
	if (err != B_OK)
		return err;
	off_t size = 0;
	file.GetSize(&size);
	if (size <= 0 || size > 64LL * 1024 * 1024)
		return B_BAD_VALUE;
	char* buffer = new (std::nothrow) char[size];
	if (buffer == NULL)
		return B_NO_MEMORY;
	// read in a loop: a single Read() may return short
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

PDDocKind
PD_SniffDocument(const char* path)
{
	BFile file;
	if (file.SetTo(path, B_READ_ONLY) != B_OK)
		return PD_KIND_ERROR;
	char head[8] = { 0 };
	ssize_t got = file.Read(head, sizeof(head));
	if (got >= 8 && memcmp(head, "HMF1", 4) == 0
		&& memcmp(head + 4, "&dDp", 4) == 0)
		return PD_KIND_NATIVE;
	return PD_KIND_OTHER;
}
