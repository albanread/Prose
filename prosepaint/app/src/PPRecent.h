// PPRecent — the Open Recent list: most-recent-first, deduplicated,
// capped. Pure data (the window gives it a settings path), so the
// ordering and the file round trip are selftestable.
#ifndef PP_RECENT_H
#define PP_RECENT_H

#include <String.h>
#include <SupportDefs.h>

#include <vector>

class PPRecent {
public:
	static const int32	kMax = 8;

	void		Remember(const BString& path);
	const std::vector<BString>& Items() const { return fItems; }

	// one absolute path per line; missing file is not an error state,
	// just an empty list (first launch)
	status_t	Load(const char* path);
	status_t	Save(const char* path) const;

private:
	std::vector<BString>	fItems;
};

#endif	// PP_RECENT_H
