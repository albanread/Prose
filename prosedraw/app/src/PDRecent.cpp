#include "PDRecent.h"

#include <File.h>

#include <new>

void
PDRecent::Remember(const BString& path)
{
	if (path.Length() == 0)
		return;
	for (size_t i = 0; i < fItems.size(); i++) {
		if (fItems[i] == path) {
			fItems.erase(fItems.begin() + i);
			break;
		}
	}
	fItems.insert(fItems.begin(), path);
	if ((int32)fItems.size() > kMax)
		fItems.resize(kMax);
}

status_t
PDRecent::Load(const char* path)
{
	fItems.clear();
	BFile file;
	status_t err = file.SetTo(path, B_READ_ONLY);
	if (err != B_OK)
		return err;
	off_t size = 0;
	file.GetSize(&size);
	if (size <= 0 || size > 64LL * 1024)
		return B_BAD_VALUE;
	char* buffer = new (std::nothrow) char[size + 1];
	if (buffer == NULL)
		return B_NO_MEMORY;
	ssize_t got = 0;
	while (got < (ssize_t)size) {
		ssize_t n = file.Read(buffer + got, size - got);
		if (n < 0) {
			got = -1;
			break;
		}
		if (n == 0)
			break;
		got += n;
	}
	if (got < 0) {
		delete[] buffer;
		return B_ERROR;
	}
	buffer[got] = '\0';
	// full-read loop, then split — a single Read() may come back short
	BString all(buffer, got);
	delete[] buffer;
	int32 start = 0;
	while (start <= all.Length()) {
		int32 end = all.FindFirst('\n', start);
		if (end < 0)
			end = all.Length();
		BString line;
		all.CopyInto(line, start, end - start);
		line.Trim();
		if (line.Length() > 0)
			fItems.push_back(line);
		if (end >= all.Length())
			break;
		start = end + 1;
	}
	return B_OK;
}

status_t
PDRecent::Save(const char* path) const
{
	BFile file;
	status_t err = file.SetTo(path,
		B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (err != B_OK)
		return err;
	for (size_t i = 0; i < fItems.size(); i++) {
		BString line = fItems[i];
		line << "\n";
		ssize_t len = line.Length();
		ssize_t written = 0;
		while (written < len) {
			ssize_t n = file.Write(line.String() + written, len - written);
			if (n <= 0)
				return n < 0 ? (status_t)n : B_ERROR;
			written += n;
		}
	}
	return B_OK;
}
