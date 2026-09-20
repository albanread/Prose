// PDPDF — ProseDraw's vector PDF writer.
//
// Diagrams are vector primitives, so the PDF is too: path operators per
// shape (rect / Bézier ellipse / Bézier rounded-rect / diamond), the
// connector lines and their arrowheads, and labels set in the Helvetica
// base-14 font (no embedding needed, metrics known offline). The page is
// the paper — MediaBox comes straight from PDPageSetup, so an A4
// document prints on A4, landscape included.
#ifndef PD_PDF_H
#define PD_PDF_H

#include <InterfaceDefs.h>

class PDDocument;

status_t PD_WritePDF(const PDDocument& doc, const char* path);

#endif	// PD_PDF_H
