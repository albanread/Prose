// PPPDF — ProsePaint's PDF export: the flattened painting as one
// true-size image page. MediaBox is the paper in points, so an A4
// painting prints on A4.
#ifndef PP_PDF_H
#define PP_PDF_H

#include <SupportDefs.h>

class PPDocument;

status_t PP_WritePDF(const PPDocument& doc, const char* path);

#endif	// PP_PDF_H
