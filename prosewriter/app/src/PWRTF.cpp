#include "PWRTF.h"

#include "PWDocument.h"

#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

// ------------------------------------------------------------------ write --

static void
AppendFmt(BString* out, const char* fmt, ...)
{
	char buf[128];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	*out << buf;
}
static void
AppendEscaped(BString* out, const char* text, int32 length)
{
	for (int32 i = 0; i < length; i++) {
		unsigned char c = (unsigned char)text[i];
		switch (c) {
			case '\\': *out << "\\\\"; break;
			case '{':  *out << "\\{"; break;
			case '}':  *out << "\\}"; break;
			default:
				if (c < 128)
					*out << (char)c;
				else
					AppendFmt(out, "\\'%02x", c);
				break;
		}
	}
}

status_t
PW_WriteRTF(const PWDocument* doc, BString* out)
{
	out->Truncate(0);
	*out << "{\\rtf1\\ansi\\deff0\n";

	// Font table: 0 is the document default; distinct families after.
	std::map<std::string, int32> fontIndex;
	std::vector<std::string> fontNames;
	std::string defFamily = doc->DefaultFormat().family;
	fontIndex[defFamily] = 0;
	fontNames.push_back(defFamily);
	for (int32 p = 0; p < doc->CountParagraphs(); p++) {
		const std::vector<PWRun>& runs = doc->ParagraphRuns(p);
		for (const PWRun& r : runs) {
			std::string fam = r.format.family[0] ? r.format.family : defFamily;
			if (fam == defFamily || fontIndex.count(fam))
				continue;
			fontIndex[fam] = (int32)fontNames.size();
			fontNames.push_back(fam);
		}
	}
	*out << "{\\fonttbl";
	for (size_t i = 0; i < fontNames.size(); i++)
		*out << "{\\f" << i << " " << fontNames[i].c_str() << ";}";
	*out << "}\n";

	// Colour table: index 0 is auto (black for us); colours start at 1.
	std::vector<rgb_color> colors;
	for (int32 p = 0; p < doc->CountParagraphs(); p++) {
		const std::vector<PWRun>& runs = doc->ParagraphRuns(p);
		for (const PWRun& r : runs) {
			rgb_color c = r.format.color;
			if (c.red == 0 && c.green == 0 && c.blue == 0)
				continue;
			bool seen = false;
			for (size_t i = 0; i < colors.size(); i++)
				if (colors[i].red == c.red && colors[i].green == c.green
					&& colors[i].blue == c.blue) { seen = true; break; }
			if (!seen) colors.push_back(c);
		}
	}
	*out << "{\\colortbl;";
	for (size_t i = 0; i < colors.size(); i++)
		AppendFmt(out, "\\red%d\\green%d\\blue%d;",
			colors[i].red, colors[i].green, colors[i].blue);
	*out << "}\n";

	PWCharFormat prev;
	memset(&prev, 0, sizeof(prev));
	strlcpy(prev.family, defFamily.c_str(), sizeof(font_family));
	prev.size = 12;
	prev.color = rgb_color{0, 0, 0, 255};

	for (int32 p = 0; p < doc->CountParagraphs(); p++) {
		const char* text = doc->ParagraphText(p);
		int32 len = doc->ParagraphLength(p);
		const std::vector<PWRun>& runs = doc->ParagraphRuns(p);
		switch (doc->ParagraphFormat(p).alignment) {
			case PW_ALIGN_CENTER:	*out << "\\qc "; break;
			case PW_ALIGN_RIGHT:	*out << "\\qr "; break;
			case PW_ALIGN_JUSTIFY:	*out << "\\qj "; break;
			default:				*out << "\\ql "; break;
		}
		size_t ri = 0;
		for (int32 at = 0; at < len; ) {
			while (ri + 1 < runs.size()
				&& at >= runs[ri].start + runs[ri].length)
				ri++;
			const PWRun& r = runs[std::min(ri, runs.size() - 1)];
			int32 segEnd = std::min(r.start + r.length, len);
			if (segEnd <= at)
				segEnd = at + 1;

			std::string fam = r.format.family[0] ? r.format.family : defFamily;
			if (strcmp(prev.family, fam.c_str()) != 0)
				AppendFmt(out, "\\f%d ", fontIndex[fam]);
			if (prev.bold != r.format.bold)
				*out << (r.format.bold ? "\\b " : "\\b0 ");
			if (prev.italic != r.format.italic)
				*out << (r.format.italic ? "\\i " : "\\i0 ");
			if (prev.underline != r.format.underline)
				*out << (r.format.underline ? "\\ul " : "\\ulnone ");
			if (prev.size != r.format.size)
				AppendFmt(out, "\\fs%d ", (int)(r.format.size * 2 + 0.5f));
			rgb_color c = r.format.color;
			if (c.red | c.green | c.blue) {
				for (size_t i = 0; i < colors.size(); i++) {
					if (colors[i].red == c.red && colors[i].green == c.green
						&& colors[i].blue == c.blue) {
						AppendFmt(out, "\\cf%d ", (int32)i + 1);
						break;
					}
				}
			}
			prev = r.format;
			strlcpy(prev.family, fam.c_str(), sizeof(font_family));

			AppendEscaped(out, text + at, segEnd - at);
			at = segEnd;
		}
		if (len == 0)
			*out << " ";	// empty paragraphs need something before \par
		if (p + 1 < doc->CountParagraphs())
			*out << "\\par\n";
	}
	*out << "}\n";
	return B_OK;
}

// ------------------------------------------------------------------- read --
namespace {

struct RtfState {
	bool bold = false, italic = false, underline = false;
	float size = 12.0f;
	int32 font = 0;
	int32 color = 0;
	PWAlignment align = PW_ALIGN_LEFT;
};

bool
IsDestinationWord(const std::string& w)
{
	static const char* const kDestinations[] = {
		"fonttbl", "colortbl", "stylesheet", "info", "pict", "object",
		"footer", "header", "footnote", "listtable", "listoverridetable",
		"list", "generator", "bkmkstart", "bkmkend", "field", "shp",
		"shppict", "nonshppict", "panose", "fname", "falt", "themecolor",
		NULL
	};
	for (int i = 0; kDestinations[i]; i++)
		if (w == kDestinations[i])
			return true;
	return false;
}

class RtfReader {
public:
	RtfReader(PWDocument* doc) : fDoc(doc) {}

	status_t Parse(const char* rtf)
	{
		fText = rtf;
		fLen = (int32)strlen(rtf);
		fDefault = fDoc->DefaultFormat();
		if (!SkipChar('{'))
			return B_ERROR;
		fDepth = 1;
		while (fDepth > 0 && fPos < fLen) {
			char c = fText[fPos];
			if (c == '{') {
				fPos++; fDepth++;
				fStack.push_back(fState);
			} else if (c == '}') {
				fPos++; fDepth--;
				PopState();
				// leaving a table group ends the table
				if (fDepth <= fTableDepth) {
					fInFontTable = fInColorTable = false;
					fTableDepth = 32000;
				}
			} else if (c == '\\') {
				Control();
			} else if (c == '\n' || c == '\r') {
				fPos++;
			} else if (InTable()) {
				HandleCharInTable(c);
				fPos++;
			} else {
				char one[2] = { c, 0 };
				Emit(one, 1);
				fPos++;
			}
		}
		// The last paragraph has no trailing \par; apply its alignment.
		ApplyAlignment(fDoc->CountParagraphs() - 1);
		return B_OK;
	}

private:
	void PopState()
	{
		if (!fStack.empty()) {
			fState = fStack.back();
			fStack.pop_back();
		}
	}

	bool SkipChar(char c)
	{
		if (fPos < fLen && fText[fPos] == c) { fPos++; return true; }
		return false;
	}

	void ApplyAlignment(int32 para)
	{
		if (para < 0 || para >= fDoc->CountParagraphs())
			return;
		// Only if this paragraph's text is untouched by an earlier \q*
		if (fSeenAlignForPara.find(para) != fSeenAlignForPara.end())
			return;
		PWParaFormat fmt = fDoc->ParagraphFormat(para);
		fmt.alignment = fState.align;
		fDoc->SetParaFormat(para, fmt);
		fSeenAlignForPara.insert(para);
	}

	void Emit(const char* bytes, int32 count)
	{
		if (count <= 0)
			return;
		PWCharFormat fmt = fDefault;
		auto it = fFonts.find(fState.font);
		if (it != fFonts.end() && !it->second.empty())
			strlcpy(fmt.family, it->second.c_str(), sizeof(font_family));
		fmt.size = fState.size > 0 ? fState.size : 12.0f;
		fmt.bold = fState.bold;
		fmt.italic = fState.italic;
		fmt.underline = fState.underline;
		if (fState.color >= 1 && fState.color <= (int32)fColors.size())
			fmt.color = fColors[fState.color - 1];
		fDoc->Insert(fDoc->Length(), BString(bytes, count).String(), &fmt);
	}

	void EmitUnicode(int32 cp)
	{
		if (cp < 0)
			cp += 65536;
		char utf8[5];
		int32 n = 0;
		if (cp < 0x80) {
			utf8[n++] = (char)cp;
		} else if (cp < 0x800) {
			utf8[n++] = (char)(0xC0 | (cp >> 6));
			utf8[n++] = (char)(0x80 | (cp & 0x3F));
		} else {
			utf8[n++] = (char)(0xE0 | (cp >> 12));
			utf8[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
			utf8[n++] = (char)(0x80 | (cp & 0x3F));
		}
		Emit(utf8, n);
	}

	int Hex2()
	{
		int v = 0;
		for (int i = 0; i < 2 && fPos < fLen; i++) {
			char c = fText[fPos++];
			v <<= 4;
			if (c >= '0' && c <= '9') v |= c - '0';
			else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
			else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
		}
		return v;
	}

	// Consume through the end of the current group (the '{' was consumed).
	void SkipGroup()
	{
		int depth = 0;
		while (fPos < fLen) {
			char c = fText[fPos];
			if (c == '{') { depth++; fPos++; }
			else if (c == '}') {
				fPos++;
				if (depth == 0)
					return;
				depth--;
			} else if (c == '\\') {
				fPos++;
				if (fPos < fLen && fText[fPos] == '\'') { fPos += 3; }
				else if (fPos < fLen && fText[fPos] == '\\') fPos++;
				else
					while (fPos < fLen && isalpha((unsigned char)fText[fPos]))
						fPos++;
			}
			else
				fPos++;
		}
	}

	void EndParagraph()
	{
		fDoc->Insert(fDoc->Length(), "\n", NULL);
		ApplyAlignment(fDoc->CountParagraphs() - 2);
	}

	void Control()
	{
		fPos++;	// backslash
		if (fPos >= fLen) return;
		char c = fText[fPos];
		if (c == '\\' || c == '{' || c == '}') { Emit(&c, 1); fPos++; return; }
		if (c == '\'') {
			fPos++;
			int b = Hex2();
			char ch = (char)b;
			Emit(&ch, 1);
			return;
		}
		bool starred = false;
		if (c == '*') { starred = true; fPos++; if (fPos >= fLen) return; }

		std::string word;
		while (fPos < fLen && isalpha((unsigned char)fText[fPos]))
			word += fText[fPos++];
		int32 param = 0;
		bool hasParam = false;
		if (fPos < fLen && (isdigit((unsigned char)fText[fPos])
				|| fText[fPos] == '-')) {
			hasParam = true;
			bool neg = fText[fPos] == '-';
			if (neg) fPos++;
			while (fPos < fLen && isdigit((unsigned char)fText[fPos]))
				param = param * 10 + (fText[fPos++] - '0');
			if (neg) param = -param;
		}
		if (fPos < fLen && fText[fPos] == ' ')
			fPos++;

		if (starred) { SkipGroup(); return; }
		if (word == "fonttbl") {
			fInFontTable = true;
			fTableDepth = fDepth;
			return;
		}
		if (word == "colortbl") {
			fInColorTable = true;
			fSeenAutoColor = false;
			fTableDepth = fDepth;
			return;
		}
		if (fInFontTable || fInColorTable) {
			TableControl(word, hasParam, param);
			return;
		}
		if (IsDestinationWord(word)) { SkipGroup(); return; }

		if (word == "par" || word == "line") { EndParagraph(); return; }
		if (word == "tab") { Emit("\t", 1); return; }
		if (word == "b") fState.bold = !hasParam || param != 0;
		else if (word == "i") fState.italic = !hasParam || param != 0;
		else if (word == "ul") fState.underline = !hasParam || param != 0;
		else if (word == "ulnone") fState.underline = false;
		else if (word == "fs") fState.size = hasParam ? param / 2.0f : 12.0f;
		else if (word == "f") fState.font = hasParam ? param : 0;
		else if (word == "cf") fState.color = hasParam ? param : 0;
		else if (word == "ql") fState.align = PW_ALIGN_LEFT;
		else if (word == "qc") fState.align = PW_ALIGN_CENTER;
		else if (word == "qr") fState.align = PW_ALIGN_RIGHT;
		else if (word == "qj") fState.align = PW_ALIGN_JUSTIFY;
		else if (word == "u" && hasParam) {
			EmitUnicode(param);
			// skip the substitute character (default \uc1)
			if (fPos < fLen && fText[fPos] == '?') fPos++;
			else if (fPos + 1 < fLen && fText[fPos] == '\\'
				&& fText[fPos + 1] == '\'') {
				fPos += 3;
			}
		}
		else if (word == "plain") {
			fState.bold = fState.italic = fState.underline = false;
			fState.size = 12.0f;
			fState.color = 0;
		}
		// everything else: ignored
	}

	void TableControl(const std::string& word, bool hasParam, int32 param)
	{
		if (word == "f" && hasParam && fInFontTable) {
			if (fPendingFont >= 0 && !fCurrentName.empty())
				fFonts[fPendingFont] = Trimmed(fCurrentName);
			fPendingFont = param;
			fCurrentName.clear();
		} else if (word == "red" && hasParam)
			fPendingColor.red = (uint8)param;
		else if (word == "green" && hasParam)
			fPendingColor.green = (uint8)param;
		else if (word == "blue" && hasParam)
			fPendingColor.blue = (uint8)param;
		else if (word == "fcharset" || word == "fmodern" || word == "fswiss"
			|| word == "froman" || word == "fnil" || word == "fdecor"
			|| word == "fscript" || word == "ftech")
			;	// face metadata, not part of the name
	}

	std::string Trimmed(const std::string& s)
	{
		size_t a = s.find_first_not_of(" \t\r\n");
		if (a == std::string::npos) return "";
		size_t b = s.find_last_not_of(" \t\r\n");
		return s.substr(a, b - a + 1);
	}

	// Plain characters inside a table group accumulate as the font name.
	void TableText(char c)
	{
		if (fInFontTable)
			fCurrentName += c;
	}

public:
	void HandleCharInTable(char c)
	{
		if (c == ';') {
			if (fInFontTable && fPendingFont >= 0 && !fCurrentName.empty())
				fFonts[fPendingFont] = Trimmed(fCurrentName);
			if (fInColorTable) {
				if (fSeenAutoColor) {
					fColors.push_back(fPendingColor);
					fPendingColor = rgb_color{0, 0, 0, 255};
				}
				fSeenAutoColor = true;	// the table's first ; is "auto"
			}
			fCurrentName.clear();
			return;
		}
		TableText(c);
	}

	bool InTable() const { return fInFontTable || fInColorTable; }

private:
	PWDocument*	fDoc;
	const char*	fText = "";
	int32		fLen = 0, fPos = 0;
	int			fDepth = 0;
	RtfState	fState;
	std::vector<RtfState> fStack;
	PWCharFormat fDefault;
	bool		fInFontTable = false, fInColorTable = false;
	bool		fSeenAutoColor = false;
	std::map<int32, std::string> fFonts;
	std::vector<rgb_color> fColors;
	std::string	fCurrentName;
	int32		fPendingFont = -1;
	rgb_color	fPendingColor { 0, 0, 0, 255 };
	int			fTableDepth = 32000;
	std::set<int32> fSeenAlignForPara;
};

}	// namespace

status_t
PW_LoadRTF(PWDocument* doc, const char* rtf)
{
	RtfReader reader(doc);
	return reader.Parse(rtf);
}
