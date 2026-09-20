#include "PWSpell.h"

#include <File.h>
#include <algorithm>
#include <cctype>
#include <cstring>

int32
PWSpellChecker::Load(const char* path)
{
	BFile file;
	if (file.SetTo(path, B_READ_ONLY) != B_OK)
		return 0;
	off_t size = 0;
	file.GetSize(&size);
	char* buffer = new char[size + 1];
	ssize_t got = file.Read(buffer, size);
	if (got < 0)
		got = 0;
	buffer[got] = '\0';
	char* p = buffer;
	while (*p) {
		char* eol = strpbrk(p, "\r\n");
		int32 len = eol ? (int32)(eol - p) : (int32)strlen(p);
		if (len > 0 && len < 48) {
			std::string word(p, len);
			for (auto& c : word)
				c = tolower((unsigned char)c);
			fWords.push_back(word);
		}
		if (!eol)
			break;
		p = eol + 1;
	}
	delete[] buffer;
	std::sort(fWords.begin(), fWords.end());
	fWords.erase(std::unique(fWords.begin(), fWords.end()), fWords.end());
	return (int32)fWords.size();
}

void
PWSpellChecker::AddWord(const char* word)
{
	std::string w(word);
	for (auto& c : w)
		c = tolower((unsigned char)c);
	fWords.push_back(w);
	std::sort(fWords.begin(), fWords.end());
}

bool
PWSpellChecker::Found(const std::string& word) const
{
	return std::binary_search(fWords.begin(), fWords.end(), word);
}

bool
PWSpellChecker::IsCorrect(const char* word) const
{
	if (!word || !word[0])
		return true;
	if (fWords.empty())
		return true;	// no dictionary: nothing is wrong

	std::string w(word);
	for (auto& c : w)
		c = tolower((unsigned char)c);
	if (w.size() <= 1)
		return true;	// lone letters are names, variables, "a", "I"
	// digits mean codes, urls, dates — accepted as typed
	for (char c : w)
		if (isdigit((unsigned char)c))
			return true;
	if (Found(w))
		return true;
	// possessives and contractions against the bare stem
	size_t apo = w.find('\'');
	if (apo != std::string::npos && apo > 1) {
		std::string stem = w.substr(0, apo);
		if (Found(stem))
			return true;
		if (stem.size() > 1 && stem[stem.size() - 1] == 's'
			&& Found(stem.substr(0, stem.size() - 1)))
			return true;
	}
	// hyphenated compounds: every part must check
	if (w.find('-') != std::string::npos) {
		bool allOk = true;
		size_t at = 0;
		while (at <= w.size()) {
			size_t dash = w.find('-', at);
			std::string part = w.substr(at,
				dash == std::string::npos ? std::string::npos : dash - at);
			if (!part.empty() && !Found(part)) {
				allOk = false;
				break;
			}
			if (dash == std::string::npos)
				break;
			at = dash + 1;
		}
		if (allOk)
			return true;
	}
	return false;
}

void
PWSpellChecker::ScanParagraph(const char* text,
	std::vector<std::pair<int32, int32>>* ranges) const
{
	ranges->clear();
	if (fWords.empty() || !text)
		return;
	int32 len = (int32)strlen(text);
	int32 i = 0;
	while (i < len) {
		// skip separators
		while (i < len && !isalnum((unsigned char)text[i])
			&& text[i] != '\'' && text[i] != '-')
			i++;
		if (i >= len)
			break;
		int32 start = i;
		bool hasDigit = false;
		bool hasNonAscii = false;
		while (i < len && (isalnum((unsigned char)text[i])
				|| text[i] == '\'' || text[i] == '-')) {
			if (isdigit((unsigned char)text[i]))
				hasDigit = true;
			if ((unsigned char)text[i] >= 0x80)
				hasNonAscii = true;
			i++;
		}
		// trailing separators stay out of the marked range
		int32 end = i;
		while (end > start && (text[end - 1] == '\''
				|| text[end - 1] == '-'))
			end--;
		if (end <= start || hasDigit || hasNonAscii || end - start == 1)
			continue;
		BString word(text + start, end - start);
		if (!IsCorrect(word.String()))
			ranges->push_back(std::make_pair(start, end - start));
	}
}


void
PWSpellChecker::CaretWordRange(const char* text, int32 offset,
	int32* start, int32* length)
{
	*start = offset;
	*length = 0;
	if (!text)
		return;
	int32 len = (int32)strlen(text);
	if (offset < 0 || offset > len)
		return;
	auto isWord = [](char c) {
		return isalnum((unsigned char)c) || c == '\'' || c == '-';
	};
	int32 s = offset;
	while (s > 0 && isWord(text[s - 1]))
		s--;
	int32 e = offset;
	while (e < len && isWord(text[e]))
		e++;
	// trailing separators stay out (mirrors ScanParagraph)
	while (e > s && (text[e - 1] == '\'' || text[e - 1] == '-'))
		e--;
	if (e < s)
		e = s;
	*start = s;
	*length = e - s;
}
