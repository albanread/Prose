// A JSON reader and writer, as small as the Language Server Protocol needs.
// It exists because Haiku's own JSON classes are private API; an editor's
// language service should not reach into private headers for the sake of a
// data format this small. What it understands is all of JSON: objects,
// arrays, strings with every escape (including \u, into UTF-8), numbers,
// true, false, null. What it does not pretend to do is preserve key order,
// exact number formatting, or anything LSP never sends.
#ifndef PROSE_JSON_H
#define PROSE_JSON_H

#include <String.h>
#include <SupportDefs.h>

#include <vector>

class JVal {
public:
	enum Kind { Null, Bool, Number, String, Array, Object };

	JVal() : fKind(Null) {}
	explicit JVal(bool b) : fKind(Bool), fBool(b) {}
	explicit JVal(double n) : fKind(Number), fNumber(n) {}
	// without this, JVal("text") would rather convert the pointer to bool
	explicit JVal(const char* s) : fKind(String), fString(s) {}
	explicit JVal(const BString& s) : fKind(String), fString(s) {}

	Kind KindOf() const { return fKind; }
	bool IsNull() const { return fKind == Null; }

	bool AsBool() const { return fBool; }
	double AsNumber() const { return fNumber; }
	int64 AsInt() const { return (int64)fNumber; }
	const BString& AsString() const { return fString; }

	// array
	JVal& Add(const JVal& v) { fKind = Array; fArray.push_back(v); return *this; }
	int Count() const { return (int)fArray.size(); }
	const JVal& At(int i) const { return fArray[i]; }

	// object: a flat list of key/value pairs, first key wins on duplicates
	void BecomeObject() { fKind = Object; }
	JVal& Set(const char* key, const JVal& v);
	bool Has(const char* key) const;
	const JVal& Get(const char* key) const;	// missing key: a shared Null

	BString ToJson() const;

private:
	Kind fKind = Null;
	bool fBool = false;
	double fNumber = 0;
	BString fString;
	std::vector<JVal> fArray;
	std::vector<BString> fKeys;

	static const JVal sNull;
};

// Parses one complete JSON document. Returns false and fills `error`
// (best effort) when the text is not one document of valid JSON.
bool JParse(const char* text, size_t length, JVal& out, BString* error = NULL);
BString JEscape(const char* text);

#endif	// PROSE_JSON_H
