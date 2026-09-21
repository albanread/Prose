#include "PDPDF.h"

#include <Entry.h>
#include <File.h>
#include <String.h>

#include <zlib.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

#include "PDDocument.h"

// A minimal single-page PDF 1.4 writer, vector all the way down.
// Object layout (1-based, as PDF demands):
//   1: Catalog, 2: Pages, 3: Page, 4: content stream, 5: Helvetica.
// The content stream is built before anything is written because /Length
// must be exact; each object's byte offset is recorded for the xref.
// Coordinate flip: the model's origin is the page's top-left, PDF's is
// the bottom-left, so every y is H - y.

static status_t
WriteAll(BFile* file, const char* data, ssize_t length)
{
	ssize_t written = 0;
	while (written < length) {
		ssize_t n = file->Write(data + written, length - written);
		if (n <= 0)
			return n < 0 ? (status_t)n : B_ERROR;
		written += n;
	}
	return B_OK;
}

// Whole points print without decimals (595, not 595.00) — the selftest
// and the eyeball both read MediaBoxes more easily that way. (Paid for
// once in PWPDF; the convention carried over.)
static BString
PdfNum(float f)
{
	char buf[32];
	if (fabsf(f - roundf(f)) < 0.01f)
		snprintf(buf, sizeof(buf), "%.0f", f);
	else
		snprintf(buf, sizeof(buf), "%.1f", f);
	return BString(buf);
}

static BString
PdfColor(const rgb_color& c)
{
	BString s;
	s.SetToFormat("%.3f %.3f %.3f", c.red / 255.0f, c.green / 255.0f,
		c.blue / 255.0f);
	return s;
}

// ------------------------------------------------------------- text --
// Helvetica AFM advance widths (units/1000) for WinAnsi codes 32..126.
// Beyond ASCII the table is honest about its limits: the default width
// is Helvetica's em-ish 556, which only mildly off-centres non-ASCII
// labels. Diagram labels in practice are ASCII.
static const int16 kHelveticaWidths[95] = {
	// 32..41   space ! " # $ % & ' ( )
	278, 278, 355, 556, 556, 889, 667, 191, 333, 333,
	// 42..51   * + , - . / 0 1 2 3
	389, 584, 278, 333, 278, 278, 556, 556, 556, 556,
	// 52..61   4 5 6 7 8 9 : ; < =
	556, 556, 556, 556, 556, 556, 278, 278, 584, 584,
	// 62..71   > ? @ A B C D E F G
	584, 556, 1015, 667, 667, 722, 722, 667, 611, 778,
	// 72..81   H I J K L M N O P Q
	722, 278, 500, 667, 556, 833, 722, 778, 667, 778,
	// 82..91   R S T U V W X Y Z [
	722, 667, 611, 722, 667, 944, 667, 667, 611, 278,
	// 92..101  \ ] ^ _ ` a b c d e
	278, 278, 469, 556, 333, 556, 556, 500, 556, 556,
	// 102..111 f g h i j k l m n o
	278, 556, 556, 222, 222, 500, 222, 833, 556, 556,
	// 112..121 p q r s t u v w x y
	556, 556, 333, 500, 278, 556, 500, 722, 500, 500,
	// 122..126 z { | } ~
	500, 334, 260, 334, 584
};

static int16
HelveticaWidth(uint8 winAnsiByte)
{
	if (winAnsiByte >= 32 && winAnsiByte <= 126)
		return kHelveticaWidths[winAnsiByte - 32];
	return 556;
}

// UTF-8 label -> WinAnsi bytes (what /Encoding /WinAnsiEncoding expects).
// Latin-1 maps straight through; the handful of punctuation marks
// WinAnsi places in 0x80..0x9F get their code; anything else becomes
// '?' rather than a wrong glyph. Escaping ( ) \ and non-printables as
// octal keeps the string a valid literal.
static BString
PdfText(const char* utf8)
{
	BString out;
	for (const uint8* p = (const uint8*)utf8; *p != '\0'; p++) {
		uint32 cp = 0;
		int len = 1;
		if (*p < 0x80)
			cp = *p;
		else if ((*p & 0xE0) == 0xC0 && p[1] != '\0') {
			cp = ((*p & 0x1F) << 6) | (p[1] & 0x3F); len = 2;
		} else if ((*p & 0xF0) == 0xE0 && p[1] != '\0' && p[2] != '\0') {
			cp = ((*p & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
			len = 3;
		} else
			cp = 0xFFFD;	// truncated or 4-byte: not representable
		p += len - 1;

		uint8 byte = '?';
		if (cp < 0x80 || (cp >= 0xA0 && cp <= 0xFF))
			byte = (uint8)cp;
		else {
			switch (cp) {
				case 0x20AC: byte = 0x80; break;	// €
				case 0x2022: byte = 0x95; break;	// bullet
				case 0x2013: byte = 0x96; break;	// en dash
				case 0x2014: byte = 0x97; break;	// em dash
				case 0x2018: byte = 0x91; break;	// left single quote
				case 0x2019: byte = 0x92; break;	// right single quote
				case 0x201C: byte = 0x93; break;	// left double quote
				case 0x201D: byte = 0x94; break;	// right double quote
				default: byte = '?';
			}
		}

		if (byte == '(' || byte == ')' || byte == '\\')
			out << '\\' << (char)byte;
		else if (byte < 32 || byte > 126) {
			char esc[6];
			snprintf(esc, sizeof(esc), "\\%03o", byte);
			out << esc;
		} else
			out << (char)byte;
	}
	return out;
}

static float
HelveticaStringWidth(const char* utf8, float size)
{
	// width in font units of the *WinAnsi* form — matching what the
	// content stream will actually set
	BString t = PdfText(utf8);
	float units = 0;
	for (int32 i = 0; i < t.Length(); i++)
		units += HelveticaWidth((uint8)t[i]);
	return units / 1000.0f * size;
}

// ------------------------------------------------------------ content --
struct PdfPen {
	BString& s;
	float pageHeight;

	BString N(float f) { return PdfNum(f); }
	// model y (down) -> PDF y (up)
	float Y(float y) { return pageHeight - y; }
	void Op(const char* op) { s << op << "\n"; }
};

static void
EmitLabel(PdfPen& p, const BString& label, const PDStyle& style,
	float xModelBaseline, float yModelBaseline, bool centre)
{
	float x = xModelBaseline;
	float y = yModelBaseline;
	if (centre) {
		// same visual centre rule as the canvas: baseline sits
		// ascent/2 above and descent/4 below the box centre (Helvetica:
		// ascender 718, descender 207 per mille)
		x -= HelveticaStringWidth(label.String(), style.textSize) / 2;
	}
	p.s << "BT /F1 " << p.N(style.textSize) << " Tf "
		<< PdfColor(style.textColor) << " rg "
		<< p.N(x) << " " << p.N(p.Y(y)) << " Td ("
		<< PdfText(label.String()) << ") Tj ET\n";
}

// An arrowhead pointing tip-ward — identical geometry to the canvas
// (head = 8 + 2*penWidth, half-width 0.4*head), so print matches screen.
static void
EmitArrowhead(PdfPen& p, BPoint tail, BPoint tip, const PDStyle& style)
{
	float ang = atan2f(tip.y - tail.y, tip.x - tail.x);
	const float head = 8.0f + style.strokeWidth * 2;
	BPoint back(tip.x - head * cosf(ang), tip.y - head * sinf(ang));
	BPoint n(head * 0.4f * sinf(ang), -head * 0.4f * cosf(ang));
	p.s << "q " << PdfColor(style.stroke) << " rg "
		<< p.N(tip.x) << " " << p.N(p.Y(tip.y)) << " m "
		<< p.N(back.x + n.x) << " " << p.N(p.Y(back.y + n.y)) << " l "
		<< p.N(back.x - n.x) << " " << p.N(p.Y(back.y - n.y)) << " l h f Q\n";
}

static void
EmitConnector(PdfPen& p, const PDShape& s, const PDDocument& doc)
{
	const PDShape* from = doc.ShapeById(s.fromId);
	const PDShape* to = doc.ShapeById(s.toId);
	if (from == NULL || to == NULL)
		return;
	std::vector<BPoint> wps;
	PDDocument::ConnectorWaypoints(*from, *to, s, wps);
	p.s << "q " << PdfColor(s.style.stroke) << " RG "
		<< p.N(s.style.strokeWidth) << " w "
		<< (s.style.dashed ? "[2 2] 0 d" : "[] 0 d") << "\n"
		<< p.N(wps[0].x) << " " << p.N(p.Y(wps[0].y)) << " m";
	for (size_t k = 1; k < wps.size(); k++)
		p.s << " " << p.N(wps[k].x) << " " << p.N(p.Y(wps[k].y)) << " l";
	p.s << " S\nQ\n";
	if (s.arrowEnd)
		EmitArrowhead(p, wps[wps.size() - 2], wps.back(), s.style);
	if (s.arrowStart)
		EmitArrowhead(p, wps[1], wps.front(), s.style);
}

static void
EmitBoxShape(PdfPen& p, const PDShape& s)
{
	const BRect& r = s.rect;
	const float left = r.left;
	const float top = r.top;
	const float w = r.Width();
	const float h = r.Height();

	// the path, by kind (all coords already PDF-space)
	switch (s.kind) {
		case PD_RECT:
			p.s << p.N(left) << " " << p.N(p.Y(r.bottom)) << " "
				<< p.N(w) << " " << p.N(h) << " re\n";
			break;
		case PD_ELLIPSE:
		{
			const float k = 0.552284f;
			float cx = left + w / 2, cy = top + h / 2;
			float rx = w / 2, ry = h / 2, kx = k * rx, ky = k * ry;
			float cyP = p.Y(cy);
			p.s << p.N(cx + rx) << " " << p.N(cyP) << " m\n"
				<< p.N(cx + rx) << " " << p.N(cyP + ky) << " "
				<< p.N(cx + kx) << " " << p.N(cyP + ry) << " "
				<< p.N(cx) << " " << p.N(cyP + ry) << " c\n"
				<< p.N(cx - kx) << " " << p.N(cyP + ry) << " "
				<< p.N(cx - rx) << " " << p.N(cyP + ky) << " "
				<< p.N(cx - rx) << " " << p.N(cyP) << " c\n"
				<< p.N(cx - rx) << " " << p.N(cyP - ky) << " "
				<< p.N(cx - kx) << " " << p.N(cyP - ry) << " "
				<< p.N(cx) << " " << p.N(cyP - ry) << " c\n"
				<< p.N(cx + kx) << " " << p.N(cyP - ry) << " "
				<< p.N(cx + rx) << " " << p.N(cyP - ky) << " "
				<< p.N(cx + rx) << " " << p.N(cyP) << " c\nh\n";
			break;
		}
		case PD_RRECT:
		{
			// 8 pt corners, like the canvas's StrokeRoundRect(r, 8, 8)
			const float rad = 8.0f;
			const float kr = 0.552284f * rad;
			float yb = p.Y(r.bottom), yt = p.Y(r.top);
			p.s << p.N(left + rad) << " " << p.N(yb) << " m\n"
				<< p.N(left + w - rad) << " " << p.N(yb) << " l\n"
				<< p.N(left + w - rad + kr) << " " << p.N(yb) << " "
				<< p.N(left + w) << " " << p.N(yb + rad - kr) << " "
				<< p.N(left + w) << " " << p.N(yb + rad) << " c\n"
				<< p.N(left + w) << " " << p.N(yt - rad) << " l\n"
				<< p.N(left + w) << " " << p.N(yt - rad + kr) << " "
				<< p.N(left + w - rad + kr) << " " << p.N(yt) << " "
				<< p.N(left + w - rad) << " " << p.N(yt) << " c\n"
				<< p.N(left + rad) << " " << p.N(yt) << " l\n"
				<< p.N(left + rad - kr) << " " << p.N(yt) << " "
				<< p.N(left) << " " << p.N(yt - rad + kr) << " "
				<< p.N(left) << " " << p.N(yt - rad) << " c\n"
				<< p.N(left) << " " << p.N(yb + rad) << " l\n"
				<< p.N(left) << " " << p.N(yb + rad - kr) << " "
				<< p.N(left + rad - kr) << " " << p.N(yb) << " "
				<< p.N(left + rad) << " " << p.N(yb) << " c\nh\n";
			break;
		}
		case PD_DIAMOND:
			p.s << p.N(left + w / 2) << " " << p.N(p.Y(top)) << " m\n"
				<< p.N(left + w) << " " << p.N(p.Y(top + h / 2)) << " l\n"
				<< p.N(left + w / 2) << " " << p.N(p.Y(top + h)) << " l\n"
				<< p.N(left) << " " << p.N(p.Y(top + h / 2)) << " l\nh\n";
			break;
		default:
			return;
	}

	// paint: fill and/or stroke, like the canvas's rules
	if (s.style.fillOn)
		p.s << PdfColor(s.style.fill) << " rg ";
	if (s.style.strokeWidth > 0) {
		p.s << PdfColor(s.style.stroke) << " RG "
			<< p.N(s.style.strokeWidth) << " w "
			<< (s.style.dashed ? "[2 2] 0 d" : "[] 0 d") << "\n";
	}
	if (s.style.fillOn && s.style.strokeWidth > 0)
		p.Op("B");
	else if (s.style.fillOn)
		p.Op("f");
	else if (s.style.strokeWidth > 0)
		p.Op("S");
}

static void
EmitShape(PdfPen& p, const PDShape& s, const PDDocument& doc,
	std::map<int32, int32>& imageObject)
{
	if (s.kind == PD_CONNECTOR) {
		EmitConnector(p, s, doc);
		return;
	}
	if (s.kind == PD_TEXT) {
		// top-left anchored, baseline one textSize down — as on screen
		if (s.label.Length() > 0)
			EmitLabel(p, s.label, s.style, s.rect.left,
				s.rect.top + s.style.textSize, false);
		return;
	}
	if (s.kind == PD_IMAGE) {
		// unit square scaled onto the rect; the XObject is registered
		// in imageObject for the Resources dictionary
		const PDImage* img = doc.ImageById(s.imageId);
		if (img == NULL)
			return;
		if (imageObject.count(s.imageId) == 0)
			imageObject[s.imageId] = (int32)imageObject.size();
		int32 n = imageObject[s.imageId];
		p.s << "q " << p.N(s.rect.Width()) << " 0 0 "
			<< p.N(s.rect.Height()) << " " << p.N(s.rect.left) << " "
			<< p.N(p.Y(s.rect.bottom))
			<< " cm /Im" << n << " Do Q\n";
		return;
	}
	p.s << "q\n";
	EmitBoxShape(p, s);
	p.s << "Q\n";
	if (s.label.Length() > 0) {
		const float cx = (s.rect.left + s.rect.right) / 2;
		const float cy = (s.rect.top + s.rect.bottom) / 2;
		EmitLabel(p, s.label, s.style, cx,
			cy + s.style.textSize * (0.718f / 2 - 0.207f / 4), true);
	}
}

// B_RGBA32 rows are B,G,R,A on little-endian arm64; PDF wants RGB.
static bool
FlateRGB(const PDImage& img, std::vector<char>* out)
{
	std::vector<uint8> rgb;
	rgb.resize((size_t)img.width * img.height * 3);
	size_t rowLen = (size_t)img.width * 4;
	for (int32 y = 0; y < img.height; y++) {
		const uint8* src = img.bits.data() + (size_t)y * rowLen;
		uint8* dst = rgb.data() + (size_t)y * img.width * 3;
		for (int32 x = 0; x < img.width; x++) {
			dst[x * 3 + 0] = src[x * 4 + 2];
			dst[x * 3 + 1] = src[x * 4 + 1];
			dst[x * 3 + 2] = src[x * 4 + 0];
		}
	}
	uLongf compressed = compressBound((uLong)rgb.size());
	out->resize(compressed);
	if (compress2((Bytef*)out->data(), &compressed,
			(const Bytef*)rgb.data(), (uLong)rgb.size(), 6) != Z_OK)
		return false;
	out->resize(compressed);
	return true;
}

status_t
PD_WritePDF(const PDDocument& doc, const char* path)
{
	if (path == NULL || path[0] == '\0')
		return B_BAD_VALUE;
	const PDPageSetup& page = doc.Page();

	// first pass: content stream + which images it references
	// (imageObject: image id -> /ImN index)
	std::map<int32, int32> imageObject;
	BString content;
	PdfPen pen = { content, page.height };
	for (int32 i = 0; i < doc.Count(); i++)
		EmitShape(pen, *doc.ShapeAt(i), doc, imageObject);

	// second pass: Flate-compress every referenced raster up front —
	// the image objects need exact stream lengths before writing
	struct ImageBits
	{
		const PDImage*	image;
		std::vector<char> flate;
	};
	std::vector<ImageBits> images;
	for (const auto& entry : imageObject) {
		const PDImage* img = doc.ImageById(entry.first);
		ImageBits bits;
		bits.image = img;
		if (img == NULL || !FlateRGB(*img, &bits.flate))
			return B_ERROR;
		images.push_back(bits);
	}

	BFile file;
	status_t err = file.SetTo(path,
		B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (err != B_OK)
		return err;

	const int32 kFixedObjects = 5;	// catalog, pages, page, content, font
	const int32 objectCount = kFixedObjects + (int32)images.size();
	off_t pos = 0;
	std::vector<off_t> offsets(objectCount + 1);
	auto write = [&](const char* data, ssize_t length) -> status_t {
		status_t e = WriteAll(&file, data, length);
		pos += length;
		return e;
	};
	auto writeStr = [&](const BString& s) -> status_t {
		return write(s.String(), s.Length());
	};

	const char header[] = "%PDF-1.4\n%\xE2\xE3\xCF\xD3\n";
	err = write(header, (ssize_t)strlen(header));

	if (err == B_OK) {
		offsets[0] = pos;
		err = writeStr("1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\n"
			"endobj\n");
	}
	if (err == B_OK) {
		offsets[1] = pos;
		err = writeStr("2 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1 "
			">>\nendobj\n");
	}
	if (err == B_OK) {
		offsets[2] = pos;
		BString xobjects;
		for (size_t i = 0; i < images.size(); i++)
			xobjects << (i > 0 ? " " : "") << "/Im" << i << " "
				<< kFixedObjects + 1 + (int32)i << " 0 R";
		BString pageObj;
		pageObj << "3 0 obj\n<< /Type /Page /Parent 2 0 R\n"
			<< "  /MediaBox [0 0 " << PdfNum(page.width) << " "
			<< PdfNum(page.height) << "]\n"
			<< "  /Resources << /Font << /F1 5 0 R >>";
		if (images.size() > 0)
			pageObj << " /XObject << " << xobjects << " >>";
		pageObj << " >>\n  /Contents 4 0 R >>\nendobj\n";
		err = writeStr(pageObj);
	}
	if (err == B_OK) {
		offsets[3] = pos;
		BString contentObj;
		contentObj << "4 0 obj\n<< /Length " << content.Length()
			<< " >>\nstream\n" << content << "endstream\nendobj\n";
		err = writeStr(contentObj);
	}
	if (err == B_OK) {
		offsets[4] = pos;
		err = writeStr("5 0 obj\n<< /Type /Font /Subtype /Type1 "
			"/BaseFont /Helvetica /Encoding /WinAnsiEncoding >>\n"
			"endobj\n");
	}
	for (size_t i = 0; i < images.size() && err == B_OK; i++) {
		offsets[kFixedObjects + i] = pos;
		BString head;
		head << kFixedObjects + 1 + (int32)i << " 0 obj\n"
			<< "<< /Type /XObject /Subtype /Image /Width "
			<< images[i].image->width << " /Height "
			<< images[i].image->height
			<< " /ColorSpace /DeviceRGB /BitsPerComponent 8"
			<< " /Filter /FlateDecode /Length "
			<< (ssize_t)images[i].flate.size() << " >>\nstream\n";
		err = writeStr(head);
		if (err == B_OK)
			err = write(images[i].flate.data(),
				(ssize_t)images[i].flate.size());
		if (err == B_OK)
			err = writeStr("\nendstream\nendobj\n");
	}
	if (err == B_OK) {
		off_t xrefAt = pos;
		BString xref;
		xref << "xref\n0 " << objectCount + 1
			<< "\n0000000000 65535 f \n";
		char line[24];
		for (int32 i = 0; i < objectCount; i++) {
			snprintf(line, sizeof(line), "%010lld 00000 n \n",
				(long long)offsets[i]);
			xref << line;
		}
		xref << "trailer\n<< /Size " << objectCount + 1
			<< " /Root 1 0 R >>\nstartxref\n"
			<< (long long)xrefAt << "\n%%EOF\n";
		err = writeStr(xref);
	}

	file.Unset();
	if (err != B_OK) {
		// never leave a broken half-PDF behind
		BEntry corpse;
		if (corpse.SetTo(path) == B_OK)
			corpse.Remove();
	}
	return err;
}
