// PWPDF — ProseWriter's PDF export.
//
// One PDF page per layout page, paginated exactly like the screen: the
// writer asks PWLayout for the page count and renders each page through
// PWPageView's print-mode path (see docs/plan.md, Sprint 10). Pages are
// raster (Flate-compressed RGB images) — WYSIWYG by construction; text
// is not selectable in v1.
#ifndef PW_PDF_H
#define PW_PDF_H

#include <SupportDefs.h>

class PWLayout;
class PWPageView;

// dpiScale = pixels per point (2.0 = 144 dpi). The PDF's MediaBox always
// carries the document's paper size in points (A4 stays a true A4).
status_t PW_WritePDF(PWPageView* view, PWLayout* layout, const char* path,
	float dpiScale = 2.0f);

#endif	// PW_PDF_H
