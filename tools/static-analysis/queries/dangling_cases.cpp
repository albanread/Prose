// Cases for dangling.query: each "BAD" line must be reported, no "OK" line.
#include <string.h>
#include <stdlib.h>

class Str {
public:
	Str(const char* s) : fData(strdup(s)) {}
	Str(const Str& other) : fData(strdup(other.fData)) {}
	~Str() { free(fData); }
	const char* String() const { return fData; }
	operator const char*() const { return fData; }
private:
	char* fData;
};

Str Name();
void Use(const char*);
char* Copy(const char* s) { return strdup(s); }

struct Holder {
	const char* fName;
	void Set() { fName = Name().String(); }			// BAD: member assigned
};

const char* Returned() { return Name().String(); }	// BAD: returned

void Cases()
{
	const char* a = Name();				// BAD: conversion operator
	const char* b = Name().String();		// BAD: String()
	const char* c;
	c = Name().String();				// BAD: assigned
	Use(Name().String());				// OK: used within the statement
	Str kept = Name();
	const char* d = kept.String();			// OK: the Str lives on
	char* e = Copy(Name().String());		// OK: copied within the statement
	Use(a); Use(b); Use(c); Use(d); free(e);
}
