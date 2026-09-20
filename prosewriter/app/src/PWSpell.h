// PWSpell — spell checking, 1997-style: a sorted word list and a scanner.
// As-you-type marking happens per paragraph; the view draws the squiggle.
#ifndef PW_SPELL_H
#define PW_SPELL_H

#include <String.h>
#include <SupportDefs.h>

#include <string>
#include <utility>
#include <vector>

class PWSpellChecker {
public:
	// Loads a word list (one word per line). Returns words loaded; 0 when
	// the file is missing (checking then passes everything).
	int32		Load(const char* path);
	void		AddWord(const char* word);

	bool		IsCorrect(const char* word) const;
	bool		Loaded() const { return !fWords.empty(); }
	int32		CountWords() const { return (int32)fWords.size(); }

	// Word byte-ranges within a paragraph's text that fail the check.
	// A word is letters, digits, apostrophes and hyphens; anything with a
	// digit or a non-ASCII byte is accepted without checking.
	void		ScanParagraph(const char* text,
				std::vector<std::pair<int32, int32>>* ranges) const;

	// The word (letters/digits/'/-) containing byte `offset` in `text`;
	// zero length when the caret sits between words. Used to exempt the
	// word being typed from squiggling until the caret moves on.
	static void	CaretWordRange(const char* text, int32 offset,
				int32* start, int32* length);

private:
	bool		Found(const std::string& word) const;

	std::vector<std::string> fWords;
};

#endif	// PW_SPELL_H
