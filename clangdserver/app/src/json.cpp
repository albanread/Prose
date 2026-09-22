#include "json.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

const JVal JVal::sNull;

JVal&
JVal::Set(const char* key, const JVal& v)
{
	fKind = Object;
	for (size_t i = 0; i < fKeys.size(); i++) {
		if (fKeys[i] == key) {
			fArray[i] = v;
			return *this;
		}
	}
	fKeys.push_back(key);
	fArray.push_back(v);
	return *this;
}

bool
JVal::Has(const char* key) const
{
	for (size_t i = 0; i < fKeys.size(); i++)
		if (fKeys[i] == key) return true;
	return false;
}

const JVal&
JVal::Get(const char* key) const
{
	for (size_t i = 0; i < fKeys.size(); i++)
		if (fKeys[i] == key) return fArray[i];
	return sNull;
}

BString
JVal::ToJson() const
{
	switch (fKind) {
		case Null: return "null";
		case Bool: return fBool ? "true" : "false";
		case Number:
		{
			// LSP only ever carries integers and the occasional simple
			// decimal; this prints both without dust
			if (fNumber == (double)(int64)fNumber)
				return BString() << (int64)fNumber;
			return BString() << fNumber;
		}
		case String: return JEscape(fString.String());
		case Array:
		{
			BString out("[");
			for (size_t i = 0; i < fArray.size(); i++) {
				if (i) out += ",";
				out += fArray[i].ToJson();
			}
			out += "]";
			return out;
		}
		case Object:
		{
			BString out("{");
			for (size_t i = 0; i < fArray.size(); i++) {
				if (i) out += ",";
				out += JEscape(fKeys[i].String());
				out += ":";
				out += fArray[i].ToJson();
			}
			out += "}";
			return out;
		}
	}
	return "null";
}

BString
JEscape(const char* text)
{
	BString out("\"");
	for (const char* p = text; *p; p++) {
		unsigned char c = *p;
		switch (c) {
			case '"': out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\n': out += "\\n"; break;
			case '\r': out += "\\r"; break;
			case '\t': out += "\\t"; break;
			default:
				// control characters become \u00XX; bytes 0x80 and up are
				// already UTF-8 and JSON carries them as they are
				if (c < 0x20) {
					char buf[8];
					snprintf(buf, sizeof(buf), "\\u%04x", c);
					out += buf;
				} else
					out += (char)c;
		}
	}
	out += "\"";
	return out;
}

// ------------------------------------------------------------------ reader --

namespace {

struct Reader {
	const char* p;
	const char* end;
	BString error;

	bool Fail(const char* why)
	{
		if (error.Length() == 0) error = why;
		return false;
	}

	void SkipSpace()
	{
		while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
			p++;
	}

	bool ParseValue(JVal& out, int depth)
	{
		if (depth > 64)
			return Fail("nesting deeper than 64");
		SkipSpace();
		if (p >= end) return Fail("value expected at end of text");

		switch (*p) {
			case '{': return ParseObject(out, depth);
			case '[': return ParseArray(out, depth);
			case '"':
			{
				BString s;
				if (!ParseString(s)) return false;
				out = JVal(s);
				return true;
			}
			case 't':
				if (end - p >= 4 && memcmp(p, "true", 4) == 0) {
					p += 4; out = JVal(true); return true;
				}
				return Fail("bad literal");
			case 'f':
				if (end - p >= 5 && memcmp(p, "false", 5) == 0) {
					p += 5; out = JVal(false); return true;
				}
				return Fail("bad literal");
			case 'n':
				if (end - p >= 4 && memcmp(p, "null", 4) == 0) {
					p += 4; out = JVal(); return true;
				}
				return Fail("bad literal");
			default: return ParseNumber(out);
		}
	}

	bool ParseObject(JVal& out, int depth)
	{
		p++;	// '{'
		out.BecomeObject();
		SkipSpace();
		if (p < end && *p == '}') { p++; return true; }
		for (;;) {
			SkipSpace();
			if (p >= end || *p != '"') return Fail("object key expected");
			BString key;
			if (!ParseString(key)) return false;
			SkipSpace();
			if (p >= end || *p != ':') return Fail("':' expected");
			p++;
			JVal v;
			if (!ParseValue(v, depth + 1)) return false;
			out.Set(key.String(), v);
			SkipSpace();
			if (p < end && *p == ',') { p++; continue; }
			if (p < end && *p == '}') { p++; return true; }
			return Fail("',' or '}' expected");
		}
	}

	bool ParseArray(JVal& out, int depth)
	{
		p++;	// '['
		out = JVal();
		SkipSpace();
		if (p < end && *p == ']') { p++; return true; }
		for (;;) {
			JVal v;
			if (!ParseValue(v, depth + 1)) return false;
			out.Add(v);
			SkipSpace();
			if (p < end && *p == ',') { p++; continue; }
			if (p < end && *p == ']') { p++; return true; }
			return Fail("',' or ']' expected");
		}
	}

	bool ParseString(BString& out)
	{
		p++;	// '"'
		while (p < end) {
			unsigned char c = *p;
			if (c == '"') { p++; return true; }
			if (c == '\\') {
				p++;
				if (p >= end) return Fail("escape at end of string");
				char e = *p++;
				switch (e) {
					case '"': out += '"'; break;
					case '\\': out += '\\'; break;
					case '/': out += '/'; break;
					case 'b': out += '\b'; break;
					case 'f': out += '\f'; break;
					case 'n': out += '\n'; break;
					case 'r': out += '\r'; break;
					case 't': out += '\t'; break;
					case 'u':
					{
						uint32 cp;
						if (!ParseUnicode(cp)) return false;
						if (cp >= 0xD800 && cp <= 0xDBFF && end - p >= 6
							&& p[0] == '\\' && p[1] == 'u') {
							// surrogate pair
							p += 2;
							uint32 lo;
							if (!ParseUnicode(lo))
								return false;
							if (lo >= 0xDC00 && lo <= 0xDFFF)
								cp = 0x10000
									+ ((cp - 0xD800) << 10) + (lo - 0xDC00);
							else
								return Fail("bad low surrogate");
						}
						if (!AppendUtf8(out, cp))
							return Fail("bad code point");
						break;
					}
					default: return Fail("unknown escape");
				}
			} else if (c < 0x20) {
				return Fail("control character in string");
			} else {
				out += (char)c;
				p++;
			}
		}
		return Fail("string not closed");
	}

	bool ParseUnicode(uint32& cp)
	{
		if (end - p < 4) return Fail("short \\u escape");
		uint32 v = 0;
		for (int i = 0; i < 4; i++) {
			char h = p[i];
			v <<= 4;
			if (h >= '0' && h <= '9') v += h - '0';
			else if (h >= 'a' && h <= 'f') v += h - 'a' + 10;
			else if (h >= 'A' && h <= 'F') v += h - 'A' + 10;
			else return Fail("bad hex digit in \\u escape");
		}
		p += 4;
		if (v >= 0xDC00 && v <= 0xDFFF)
			return Fail("lone low surrogate");
		cp = v;
		return true;
	}

	static bool AppendUtf8(BString& out, uint32 cp)
	{
		if (cp > 0x10FFFF) return false;
		char buf[4];
		if (cp < 0x80) {
			buf[0] = cp; out.Append(buf, 1);
		} else if (cp < 0x800) {
			buf[0] = 0xC0 | (cp >> 6);
			buf[1] = 0x80 | (cp & 0x3F);
			out.Append(buf, 2);
		} else if (cp < 0x10000) {
			buf[0] = 0xE0 | (cp >> 12);
			buf[1] = 0x80 | ((cp >> 6) & 0x3F);
			buf[2] = 0x80 | (cp & 0x3F);
			out.Append(buf, 3);
		} else {
			buf[0] = 0xF0 | (cp >> 18);
			buf[1] = 0x80 | ((cp >> 12) & 0x3F);
			buf[2] = 0x80 | ((cp >> 6) & 0x3F);
			buf[3] = 0x80 | (cp & 0x3F);
			out.Append(buf, 4);
		}
		return true;
	}

	bool ParseNumber(JVal& out)
	{
		const char* start = p;
		if (p < end && (*p == '-' || *p == '+')) p++;
		bool digits = false;
		while (p < end && *p >= '0' && *p <= '9') { p++; digits = true; }
		if (p < end && *p == '.') {
			p++;
			while (p < end && *p >= '0' && *p <= '9') { p++; digits = true; }
		}
		if (p < end && (*p == 'e' || *p == 'E')) {
			p++;
			if (p < end && (*p == '-' || *p == '+')) p++;
			while (p < end && *p >= '0' && *p <= '9') p++;
		}
		if (!digits) return Fail("not a number");
		char buf[64];
		size_t len = p - start;
		if (len >= sizeof(buf)) return Fail("number too long");
		memcpy(buf, start, len);
		buf[len] = 0;
		out = JVal(strtod(buf, NULL));
		return true;
	}
};

} // namespace

bool
JParse(const char* text, size_t length, JVal& out, BString* error)
{
	Reader r = { text, text + length, BString() };
	if (!r.ParseValue(out, 0)) {
		if (error) *error = r.error;
		return false;
	}
	r.SkipSpace();
	if (r.p != r.end) {
		if (error) *error = "text after the document";
		return false;
	}
	return true;
}
