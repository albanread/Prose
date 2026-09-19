// PWDocument — ProseWriter's document model.
//
// A document is paragraphs; a paragraph is UTF-8 text plus styled runs plus
// a paragraph format. All editing goes through PWDocument, which maintains
// a global byte-offset address space (paragraph separators count as one
// byte, like BTextView's model) and an undo stack.
#ifndef PW_DOCUMENT_H
#define PW_DOCUMENT_H

#include <Font.h>
#include <Message.h>
#include <Point.h>
#include <String.h>
#include <SupportDefs.h>

#include <cstring>
#include <string>
#include <vector>

// Character format. The family is kept; bold/italic are face flags resolved
// against the family at layout time (BFont::SetFamilyAndFace). A family
// without a real bold face still gets synthetic emphasis where FreeType can.
struct PWCharFormat {
	font_family	family;			// empty string = inherit document default
	float		size = 12.0f;
	rgb_color	color { 0, 0, 0, 255 };
	bool		bold = false;
	bool		italic = false;
	bool		underline = false;

	bool operator==(const PWCharFormat& o) const
	{
		return !strcmp(family, o.family) && size == o.size
			&& color == o.color && bold == o.bold && italic == o.italic
			&& underline == o.underline;
	}
	bool operator!=(const PWCharFormat& o) const { return !(*this == o); }
	void Archive(BMessage* into) const;
	void Unarchive(const BMessage* from);
};

// Paragraph format (alignment only for sprint 1; tabs/indents come with the
// ruler in sprint 2).
enum PWAlignment : uint8 { PW_ALIGN_LEFT, PW_ALIGN_CENTER, PW_ALIGN_RIGHT,
	PW_ALIGN_JUSTIFY };

struct PWParaFormat {
	PWAlignment	alignment = PW_ALIGN_LEFT;
	bool operator==(const PWParaFormat& o) const
		{ return alignment == o.alignment; }
};

struct PWRun {
	int32			start = 0;		// byte offset within the paragraph text
	int32			length = 0;		// byte length, never crosses paragraph end
	PWCharFormat	format;
};

class PWDocument {
public:
			PWDocument();

	// ---- content
	int32		CountParagraphs() const { return (int32)fParas.size(); }
	const char*	ParagraphText(int32 index) const { return fParas[index].text.c_str(); }
	int32		ParagraphLength(int32 index) const
					{ return (int32)fParas[index].text.size(); }
	const std::vector<PWRun>& ParagraphRuns(int32 index) const
					{ return fParas[index].runs; }
	const PWParaFormat& ParagraphFormat(int32 index) const
					{ return fParas[index].format; }

	const char*	PlainText() const;			// whole document, '\n' separators
	int32		Length() const;				// total bytes incl. separators
	bool		IsModified() const { return fModified; }
	void		SavedClean() { fModified = false; }

	// ---- the editing surface (global byte offsets; a paragraph separator
	// occupies one byte at the end of every paragraph except the last)
	void		GetText(int32 offset, int32 length, BString* out) const;
	PWCharFormat FormatAt(int32 offset) const;	// format of the run covering offset
	void		Insert(int32 offset, const char* text, const PWCharFormat* fmt);
	void		Remove(int32 offset, int32 length);
	void		ApplyFormat(int32 offset, int32 length, const PWCharFormat& fmt);
	void		SetParaFormat(int32 para, const PWParaFormat& fmt);
	void		SplitPara(int32 offset);		// used by Enter key
	void		MergeWithNext(int32 para);	// used by Backspace at para start

	// ---- offset <-> paragraph geometry
	// Returns {paragraphIndex, offsetInParagraph} for a global offset.
	void		Locate(int32 offset, int32* para, int32* inPara) const;
	int32		ParaStart(int32 para) const;	// global offset of paragraph start
	bool		IsSeparatorOffset(int32 offset) const;

	// ---- undo (Alt+Z / Alt+Y)
	bool		CanUndo() const { return !fUndo.empty(); }
	bool		CanRedo() const { return !fRedo.empty(); }
	void		Undo();
	void		Redo();
	void		SetUndoCoalescing(bool on) { fCoalesce = on; }

	// ---- search (over the plain-text view of the document)
	// Returns the match offset or -1; *length gets the match length.
	int32		FindNext(const char* needle, int32 fromOffset,
					bool caseSensitive, bool wrap, int32* length) const;
	// Replaces every match; returns how many, or -1 on bad input.
	int32		ReplaceAll(const char* find, const char* replace,
					bool caseSensitive);

	// ---- persistence. The native format is a flattened BMessage: one
	// 'para' field per paragraph; RTF/plain text live in later sprints.
	status_t	SaveToMessage(BMessage* msg) const;
	status_t	LoadFromMessage(const BMessage* msg);
	status_t	SaveToFile(const char* path) const;
	status_t	LoadFromFile(const char* path);

	const PWCharFormat& DefaultFormat() const { return fDefault; }

	// ---- headers & footers: run text with {page} and {pages} fields,
	// substituted per page at draw time. Empty string = none.
	void		SetHeaderText(const char* text) { fHeader = text; fModified = true; }
	void		SetFooterText(const char* text) { fFooter = text; fModified = true; }
	const char* HeaderText() const { return fHeader.String(); }
	const char* FooterText() const { return fFooter.String(); }
	static BString ComposeHeaderText(const char* pattern, int32 page,
		int32 pageCount);
	void		SetDefaultFormat(const PWCharFormat& fmt) { fDefault = fmt; }

	// Fixed default every new document starts with.
	static PWCharFormat MakeDefaultFormat();

private:
	struct Para {
		std::vector<PWRun> runs;
		std::string		text;
		PWParaFormat	format;
	};

	struct UndoStep {
		enum Kind { INSERT, REMOVE, FORMAT, PARAFORMAT } kind;
		int32		offset = 0;
		int32		length = 0;
		BString		text;				// removed text (REMOVE) or undo target
		std::vector<PWRun> runs;		// formatting to restore
		PWParaFormat paraFormat;
		bool		coalesce = false;	// may merge with the following same-kind step
	};

	void		NormalizeRuns(Para& p);
	void		PushUndo(const UndoStep& step);
	PWCharFormat	FormatForInsert(const Para& p, int32 at) const;

	std::vector<Para>	fParas;
	PWCharFormat		fDefault;
	std::vector<UndoStep>	fUndo, fRedo;
	bool			fModified = false;
	bool			fCoalesce = true;
	BString			fHeader, fFooter;
	bool			fInternalEdit = false;
	mutable BString	fPlainTextCache;
	mutable bool	fPlainTextValid = false;
};

#endif	// PW_DOCUMENT_H
