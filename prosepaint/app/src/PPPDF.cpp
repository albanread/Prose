#include "PPPDF.h"

#include <Entry.h>
#include <File.h>
#include <String.h>

#include <zlib.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

#include "PPDocument.h"

// A minimal one-page PDF 1.4 writer: catalog, pages, page, content,
// one Flate-compressed DeviceRGB image XObject painted edge to edge.
// Object layout is fixed (1..5); every byte offset is recorded for
// the xref. The pattern is the one ProseWriter/ProseDraw proved.

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

status_t
PP_WritePDF(const PPDocument& doc, const char* path)
{
	if (path == NULL || path[0] == '\0')
		return B_BAD_VALUE;
	const BBitmap* comp = const_cast<PPDocument&>(doc).Composite();
	if (comp == NULL)
		return B_ERROR;
	int32 w = doc.Width();
	int32 h = doc.Height();
	if (w < 1 || h < 1)
		return B_BAD_VALUE;

	// composite over white: print output is opaque, and transparent
	// canvas pixels read as the paper
	uint32 bpr = comp->BytesPerRow();
	const uint8* bits = (const uint8*)comp->Bits();
	std::vector<uint8> rgb((size_t)w * h * 3);
	for (int32 y = 0; y < h; y++) {
		const uint8* src = bits + (size_t)y * bpr;
		uint8* dst = rgb.data() + (size_t)y * w * 3;
		for (int32 x = 0; x < w; x++) {
			const uint8* p = src + (size_t)x * 4;	// B,G,R,A
			uint32 a = p[3];
			dst[x * 3 + 0] = (uint8)((p[2] * a + 255 * (255 - a)) / 255);
			dst[x * 3 + 1] = (uint8)((p[1] * a + 255 * (255 - a)) / 255);
			dst[x * 3 + 2] = (uint8)((p[0] * a + 255 * (255 - a)) / 255);
		}
	}
	uLongf flateLen = compressBound((uLong)rgb.size());
	std::vector<uint8> flate(flateLen);
	if (compress2((Bytef*)flate.data(), &flateLen, (const Bytef*)rgb.data(),
			(uLong)rgb.size(), 6) != Z_OK)
		return B_ERROR;
	flate.resize(flateLen);

	BFile file;
	status_t err = file.SetTo(path,
		B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (err != B_OK)
		return err;

	off_t pos = 0;
	off_t offsets[5];
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
		BString pageObj;
		pageObj << "3 0 obj\n<< /Type /Page /Parent 2 0 R\n"
			<< "  /MediaBox [0 0 " << PdfNum(doc.Paper().widthPt) << " "
			<< PdfNum(doc.Paper().heightPt) << "]\n"
			<< "  /Resources << /XObject << /Im0 5 0 R >> >>\n"
			<< "  /Contents 4 0 R >>\nendobj\n";
		err = writeStr(pageObj);
	}
	if (err == B_OK) {
		offsets[3] = pos;
		// one image, painted edge to edge in page units
		BString content = "q ";
		content << PdfNum(doc.Paper().widthPt) << " 0 0 "
			<< PdfNum(doc.Paper().heightPt) << " 0 0 cm /Im0 Do Q\n";
		BString contentObj;
		contentObj << "4 0 obj\n<< /Length " << content.Length()
			<< " >>\nstream\n" << content << "endstream\nendobj\n";
		err = writeStr(contentObj);
	}
	if (err == B_OK) {
		offsets[4] = pos;
		BString head;
		head << "5 0 obj\n<< /Type /XObject /Subtype /Image /Width " << w
			<< " /Height " << h
			<< " /ColorSpace /DeviceRGB /BitsPerComponent 8"
			<< " /Filter /FlateDecode /Length " << (ssize_t)flate.size()
			<< " >>\nstream\n";
		err = writeStr(head);
		if (err == B_OK)
			err = write((const char*)flate.data(),
				(ssize_t)flate.size());
		if (err == B_OK)
			err = writeStr("\nendstream\nendobj\n");
	}
	if (err == B_OK) {
		off_t xrefAt = pos;
		BString xref;
		xref << "xref\n0 6\n0000000000 65535 f \n";
		char line[24];
		for (int i = 0; i < 5; i++) {
			snprintf(line, sizeof(line), "%010lld 00000 n \n",
				(long long)offsets[i]);
			xref << line;
		}
		xref << "trailer\n<< /Size 6 /Root 1 0 R >>\nstartxref\n"
			<< (long long)xrefAt << "\n%%EOF\n";
		err = writeStr(xref);
	}

	file.Unset();
	if (err != B_OK) {
		BEntry corpse;
		if (corpse.SetTo(path) == B_OK)
			corpse.Remove();
	}
	return err;
}
