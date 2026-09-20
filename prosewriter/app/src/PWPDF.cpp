#include "PWPDF.h"

#include <Bitmap.h>
#include <Entry.h>
#include <File.h>
#include <String.h>

#include <zlib.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "PWLayout.h"
#include "PWPageView.h"

// A minimal PDF 1.4 writer. Object layout (1-based, as PDF demands):
//   1: Catalog, 2: Pages; then per page i (0-based):
//   3 + 3*i: Page, 4 + 3*i: content stream, 5 + 3*i: image XObject.
// Everything is written incrementally; each object's byte offset is
// recorded for the xref table.

static status_t
WriteAll(BPositionIO* file, const char* data, ssize_t length)
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
// and the eyeball both read MediaBoxes more easily that way.
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

// B_RGB32 on little-endian arm64 is B,G,R,A in memory; PDF wants RGB rows.
static bool
FlateRGB(const BBitmap* bmp, std::vector<char>* out)
{
	int32 w = bmp->Bounds().IntegerWidth() + 1;
	int32 h = bmp->Bounds().IntegerHeight() + 1;
	uint32 bpr = bmp->BytesPerRow();
	const uint8* bits = (const uint8*)bmp->Bits();
	std::vector<uint8> rgb;
	rgb.resize((size_t)w * h * 3);
	for (int32 y = 0; y < h; y++) {
		const uint8* row = bits + (size_t)y * bpr;
		uint8* dst = rgb.data() + (size_t)y * w * 3;
		for (int32 x = 0; x < w; x++) {
			dst[x * 3 + 0] = row[x * 4 + 2];
			dst[x * 3 + 1] = row[x * 4 + 1];
			dst[x * 3 + 2] = row[x * 4 + 0];
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
PW_WritePDF(PWPageView* view, PWLayout* layout, const char* path,
	float dpiScale)
{
	if (view == NULL || layout == NULL || path == NULL || path[0] == '\0'
		|| dpiScale <= 0)
		return B_BAD_VALUE;

	int32 pages = layout->CountPages();
	if (pages < 1)
		return B_BAD_VALUE;
	const float pageWidth = layout->PageSetup().pageWidth;
	const float pageHeight = layout->PageSetup().pageHeight;

	BFile file;
	status_t err = file.SetTo(path, B_WRITE_ONLY | B_CREATE_FILE
		| B_ERASE_FILE);
	if (err != B_OK)
		return err;

	off_t pos = 0;
	std::vector<off_t> offsets;		// offsets[n] = byte offset of object n+1
	auto mark = [&pos, &offsets](int32 id) {
		if ((size_t)id > offsets.size())
			offsets.resize(id);
		offsets[id - 1] = pos;
	};
	auto write = [&](const char* data, ssize_t length) -> status_t {
		status_t e = WriteAll(&file, data, length);
		pos += length;
		return e;
	};
	auto writeStr = [&](const BString& s) -> status_t {
		return write(s.String(), s.Length());
	};

	err = writeStr("%PDF-1.4\n%\xE2\xE3\xCF\xD3\n");

	// Render and compress every page up front — the PDF structures below
	// need the exact stream lengths before anything can be written.
	struct PageBits {
		std::vector<char> flate;
		int32 width = 0;
		int32 height = 0;
	};
	std::vector<PageBits> pageBits(pages);
	for (int32 p = 0; p < pages && err == B_OK; p++) {
		BBitmap* bmp = view->RenderPageBitmap(p, dpiScale);
		if (bmp == NULL) {
			err = B_NO_MEMORY;
			break;
		}
		pageBits[p].width = bmp->Bounds().IntegerWidth() + 1;
		pageBits[p].height = bmp->Bounds().IntegerHeight() + 1;
		bool ok = FlateRGB(bmp, &pageBits[p].flate);
		delete bmp;
		if (!ok)
			err = B_ERROR;
	}

	if (err == B_OK) {
		mark(1);
		err = writeStr("1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");

		BString kids;
		for (int32 p = 0; p < pages; p++)
			kids << (p > 0 ? " " : "") << 3 + 3 * p << " 0 R";
		mark(2);
		BString pagesObj;
		pagesObj << "2 0 obj\n<< /Type /Pages /Kids [" << kids
			<< "] /Count " << pages << " >>\nendobj\n";
		err = writeStr(pagesObj);
	}

	for (int32 p = 0; p < pages && err == B_OK; p++) {
		const PageBits& pb = pageBits[p];
		int32 pageId = 3 + 3 * p;
		int32 contentId = pageId + 1;
		int32 imageId = pageId + 2;

		mark(pageId);
		BString pageObj;
		pageObj << pageId << " 0 obj\n<< /Type /Page /Parent 2 0 R\n"
			<< "  /MediaBox [0 0 " << PdfNum(pageWidth) << " "
			<< PdfNum(pageHeight) << "]\n"
			<< "  /Resources << /XObject << /Im0 " << imageId
			<< " 0 R >> >>\n"
			<< "  /Contents " << contentId << " 0 R >>\nendobj\n";
		err = writeStr(pageObj);
		if (err != B_OK)
			break;

		// one image, painted edge to edge
		BString content;
		content << "q\n" << PdfNum(pageWidth) << " 0 0 "
			<< PdfNum(pageHeight) << " 0 0 cm\n/Im0 Do\nQ\n";
		mark(contentId);
		BString contentObj;
		contentObj << contentId << " 0 obj\n<< /Length "
			<< content.Length() << " >>\nstream\n" << content
			<< "endstream\nendobj\n";
		err = writeStr(contentObj);
		if (err != B_OK)
			break;

		mark(imageId);
		BString imageHead;
		imageHead << imageId << " 0 obj\n<< /Type /XObject /Subtype /Image"
			<< " /Width " << pb.width << " /Height " << pb.height
			<< " /ColorSpace /DeviceRGB /BitsPerComponent 8"
			<< " /Filter /FlateDecode /Length "
			<< (ssize_t)pb.flate.size() << " >>\nstream\n";
		err = writeStr(imageHead);
		if (err == B_OK)
			err = write(pb.flate.data(), (ssize_t)pb.flate.size());
		if (err == B_OK)
			err = writeStr("\nendstream\nendobj\n");
	}

	if (err == B_OK) {
		off_t xrefAt = pos;
		int32 count = (int32)offsets.size();
		BString xref;
		xref << "xref\n0 " << count + 1 << "\n0000000000 65535 f \n";
		char line[24];
		for (int32 i = 0; i < count; i++) {
			snprintf(line, sizeof(line), "%010lld 00000 n \n",
				(long long)offsets[i]);
			xref << line;
		}
		xref << "trailer\n<< /Size " << count + 1
			<< " /Root 1 0 R >>\nstartxref\n" << (long long)xrefAt
			<< "\n%%EOF\n";
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
