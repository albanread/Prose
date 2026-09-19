// RTF import/export for ProseWriter — a deliberately small RTF 1.x subset.
//
// Written:  \rtf1, one font table, one colour table, \b \i \ul \fsN \fN \cfN,
//           \ql \qc \qr \qj, \par, escaped bytes for non-ASCII.
// Read:     the same, plus \uN unicode, \'xx, \tab, \line; font/colour
//           tables are honoured, unknown destinations are skipped.
#ifndef PW_RTF_H
#define PW_RTF_H

#include <String.h>
#include <SupportDefs.h>

class PWDocument;

status_t PW_WriteRTF(const PWDocument* doc, BString* out);
status_t PW_LoadRTF(PWDocument* doc, const char* rtf);

#endif	// PW_RTF_H
