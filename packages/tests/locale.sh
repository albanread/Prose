#!/bin/sh
# The Locale Kit must answer: every call below goes through ICU. The image
# shipped until build 198 used the bootstrap icu74, whose libicudata.so is a
# 135 KB stub that looks for its data where nothing on Haiku puts it, and on
# it every one of these failed -- "Out of memory" from BNumberFormat and
# BDateFormat, "General system error" from the rest -- so sizes, dates,
# durations and plurals came out empty all over the system, and
# ActivityMonitor said "Used memory -- MiB". icu74 74.1-6 (our build, patch
# 0089) links its 31 MB of data into the library.
#
# Compiles a probe with the image's own compiler and runs it the way a
# desktop session would (LC_ALL set: the automation portal sets no locale,
# and ICU then falls back to POSIX, which is correct but not what a user has).
#   packages/boot-test.sh packages/tests/locale.sh
fail=0
say() { echo "$*"; echo "locale probe: $*" > /dev/dprintf 2>/dev/null; }

size=$(ls -l /boot/system/lib/libicudata.so.74.1 2>/dev/null | awk '{print $5}')
if [ -n "$size" ] && [ "$size" -gt 10000000 ]; then
	say "libicudata carries its data: $size bytes"
else
	say "LIBICUDATA IS A STUB: ${size:-missing} bytes"; fail=1
fi

dir=/boot/home/locale-probe
rm -rf $dir; mkdir -p $dir
cat > $dir/probe.cpp <<'CPP'
// What the Locale Kit can do on this machine: every call below goes through
// ICU, and an ICU that cannot find its data answers each one with an error or
// an empty string.
#include <stdio.h>
#include <string.h>
#include <Application.h>
#include <Collator.h>
#include <DateFormat.h>
#include <DurationFormat.h>
#include <Language.h>
#include <LocaleRoster.h>
#include <NumberFormat.h>
#include <String.h>
#include <StringFormat.h>
#include <TimeUnitFormat.h>

static int failures = 0;

static void report(const char* what, status_t status, const BString& out)
{
	bool good = status == B_OK && out.Length() > 0;
	if (!good) failures++;
	printf("%-5s %-26s %s\n", good ? "ok" : "FAIL", what,
		status == B_OK ? (out.Length() ? out.String() : "(empty)") : strerror(status));
}

int main()
{
	BApplication app("application/x-vnd.prose-icu-probe");
	BString out;

	BNumberFormat number;
	out = ""; report("BNumberFormat 1234567.891", number.Format(out, 1234567.891), out);
	out = ""; report("BNumberFormat percent 0.42", number.FormatPercent(out, 0.42), out);

	BDateFormat date;
	out = ""; report("BDateFormat long", date.Format(out, (time_t)1789900000, B_LONG_DATE_FORMAT), out);

	BDurationFormat duration;
	out = ""; report("BDurationFormat 1h2m3s", duration.Format(out, 0, 3723000000LL), out);

	BTimeUnitFormat timeUnit;
	out = ""; report("BTimeUnitFormat 5 hours", timeUnit.Format(out, 5, B_TIME_UNIT_HOUR), out);

	BStringFormat plural("{0, plural, one{# file} other{# files}}");
	out = ""; report("BStringFormat plural 3", plural.Format(out, 3), out);

	BLanguage* german = NULL;
	status_t status = BLocaleRoster::Default()->GetLanguage("de", &german);
	out = "";
	if (status == B_OK && german != NULL) status = german->GetName(out);
	// a stub ICU answers the language's code, not its name
	if (status == B_OK && out == "de") { out = "de (the code, not a name)"; status = B_ERROR; }
	report("language name for de", status, out);
	delete german;

	// an explicit locale: the default one is whatever LC_ALL says, and with
	// none set ICU takes en_US_POSIX, whose collation is plain ASCII order
	BCollator collator("en", B_COLLATE_TERTIARY, true);
	int order = collator.Compare("apple", "Banana");
	printf("%-5s %-26s %d\n", order < 0 ? "ok" : "FAIL", "BCollator apple < Banana", order);
	if (order >= 0) failures++;

	printf("%s\n", failures == 0 ? "PASS" : "FAIL");
	return failures == 0 ? 0 : 1;
}
CPP
if clang++ -O2 -o $dir/probe $dir/probe.cpp -lbe > $dir/build.log 2>&1; then
	LC_ALL=en_US.UTF-8 $dir/probe > $dir/out.txt 2>&1
	status=$?
	while IFS= read -r line; do say "$line"; done < $dir/out.txt
	[ $status = 0 ] || fail=1
else
	say "COULD NOT BUILD THE PROBE: $(tail -c 300 $dir/build.log | tr '\n' ' ')"; fail=1
fi

[ $fail = 0 ] && say PASS || say FAIL
