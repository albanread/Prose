/*
 * Copyright 2026, HaikuArmQemu (PROSE fork). All rights reserved.
 * Distributed under the terms of the MIT License.
 */

/*!	afxtest: regression tests for the bugs that the static analysis of
	September 2026 found in the kernel, libroot, the kits, Tracker and the
	servers (Haiku patches 0141-0150, see docs/static-analysis.md).

	Every test runs in a child process of its own, so that a crash or a hang
	fails that test only; a crashing child must be killed at once, which is
	what the debug_server setting "default_action kill" does. Each test
	prints one line, PASS, FAIL (with the reason) or SKIP, and the run ends
	with "SELFTEST PASS n/n".

	Usage: afxtest [--fat <dir>] [--intrusive] [--list] [test...]

	--fat <dir>		a directory on a FAT volume, for writev-zero-length
	--intrusive		also run the tests that change system settings while
					they run (the window decorator and the bold font)
	test...			run only these

	Some tests show their bug only under the guarded heap; run-guest.sh runs
	those a second time with LD_PRELOAD=libroot_debug.so MALLOC_DEBUG=g.
*/


#include <AppMisc.h>
#include <Application.h>
#include <Bitmap.h>
#include <ChannelSlider.h>
#include <ColumnListView.h>
#include <ColumnTypes.h>
#include <DecoratorPrivate.h>
#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <Font.h>
#include <GradientLinear.h>
#include <GradientRadial.h>
#include <Key.h>
#include <MediaEncoder.h>
#include <MediaFile.h>
#include <MediaFormats.h>
#include <MediaTheme.h>
#include <Messenger.h>
#include <Mime.h>
#include <NetEndpoint.h>
#include <NetworkAddress.h>
#include <ParameterWeb.h>
#include <Path.h>
#include <Picture.h>
#include <Query.h>
#include <Roster.h>
#include <Shape.h>
#include <String.h>
#include <StringList.h>
#include <SymLink.h>
#include <TextView.h>
#include <TranslatorRoster.h>
#include <Url.h>
#include <View.h>
#include <Volume.h>
#include <VolumeRoster.h>
#include <Window.h>

#include <driver_settings.h>
#include <errno.h>
#include <fcntl.h>
#include <parsedate.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include <KMessage.h>
#include <AdapterIO.h>

// Tracker's own classes, from libtracker
#include "IconMenuItem.h"
#include "Model.h"
#include "NavMenu.h"
#include "TrackerString.h"
#include "ViewState.h"
#include "WidgetAttributeText.h"


using BPrivate::BNavMenu;
using BPrivate::Model;
using BPrivate::ModelMenuItem;
using BPrivate::TrackerString;
using BPrivate::WidgetAttributeText;

// not in any header; Appearance uses it
void _set_system_font_(const char* which, font_family family,
	font_style style, float size);


static const char* kSignature = "application/x-vnd.prose-afxtest";

enum {
	RESULT_PASS = 0,
	RESULT_FAIL = 1,
	RESULT_SKIP = 77
};

static const char* sFatDirectory = NULL;
static int sReasonFD = -1;


//	#pragma mark - helpers


static int
report(int result, const char* format, va_list args)
{
	char reason[512];
	vsnprintf(reason, sizeof(reason), format, args);
	if (sReasonFD >= 0)
		write(sReasonFD, reason, strlen(reason));
	return result;
}


static int
fail(const char* format, ...)
{
	va_list args;
	va_start(args, format);
	int result = report(RESULT_FAIL, format, args);
	va_end(args);
	return result;
}


static int
skip(const char* format, ...)
{
	va_list args;
	va_start(args, format);
	int result = report(RESULT_SKIP, format, args);
	va_end(args);
	return result;
}


//! The lowest free file descriptor: it grows when descriptors leak.
static int
lowest_free_fd()
{
	int fd = dup(0);
	if (fd >= 0)
		close(fd);
	return fd;
}


//! The memory the team has in RAM, in bytes.
static size_t
resident_size()
{
	size_t size = 0;
	ssize_t cookie = 0;
	area_info info;
	while (get_next_area_info(B_CURRENT_TEAM, &cookie, &info) == B_OK)
		size += info.ram_size;
	return size;
}


static status_t
write_file(const char* path, const void* data, size_t size)
{
	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		return errno;
	ssize_t written = write(fd, data, size);
	close(fd);
	return written == (ssize_t)size ? B_OK : B_IO_ERROR;
}


static team_id
find_team(const char* name)
{
	int32 cookie = 0;
	team_info info;
	while (get_next_team_info(&cookie, &info) == B_OK) {
		const char* base = strrchr(info.args, '/');
		base = base != NULL ? base + 1 : info.args;
		if (strncmp(base, name, strlen(name)) == 0)
			return info.team;
	}
	return -1;
}


/*!	A view in an offscreen bitmap, locked; the bitmap goes with the class.
*/
class OffscreenView {
public:
	OffscreenView(BRect bounds)
		:
		fBitmap(new BBitmap(bounds, B_RGB32, true)),
		fView(NULL)
	{
		if (fBitmap->InitCheck() != B_OK)
			return;
		fView = new BView(bounds, "view", B_FOLLOW_NONE, 0);
		fBitmap->AddChild(fView);
		fBitmap->Lock();
	}

	~OffscreenView()
	{
		if (fView != NULL)
			fBitmap->Unlock();
		delete fBitmap;
	}

	BView* View() const
	{
		return fView;
	}

	//! The pixel at \a x, \a y as 0xRRGGBB.
	uint32 PixelAt(int32 x, int32 y) const
	{
		const uint8* row = (const uint8*)fBitmap->Bits()
			+ y * fBitmap->BytesPerRow();
		const uint8* pixel = row + x * 4;
		return (uint32)pixel[2] << 16 | (uint32)pixel[1] << 8 | pixel[0];
	}

private:
	BBitmap*	fBitmap;
	BView*		fView;
};


//	#pragma mark - kernel (0141, 0149)


/*!	file_cache.cpp: do_cache_io() called a function pointer that only its
	loop sets, so a zero-length write to a file system that uses the file
	cache without checking the length itself (FAT, NTFS, NFSv4) panicked the
	kernel. writev() with an empty vector gets there.
*/
static int
test_writev_zero_length()
{
	if (sFatDirectory == NULL)
		return skip("no FAT volume (--fat <dir>)");

	BString path(sFatDirectory);
	path << "/afx-writev";
	int fd = open(path.String(), O_RDWR | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		return fail("cannot create %s: %s", path.String(), strerror(errno));

	char buffer[16] = "";
	struct iovec vector = { buffer, 0 };
	ssize_t written = writev(fd, &vector, 1);
	ssize_t read = readv(fd, &vector, 1);
	ssize_t pwritten = writev_pos(fd, 4096, &vector, 1);
	struct stat stat;
	fstat(fd, &stat);
	close(fd);
	unlink(path.String());

	// POSIX: a write of nothing to a regular file returns 0 and has no
	// other effect (patch 0149 for FAT, which called it an I/O error)
	if (written != 0 || read != 0 || pwritten != 0) {
		return fail("zero-length vectors transferred %zd, %zd, %zd bytes",
			written, read, pwritten);
	}
	if (stat.st_size != 0) {
		return fail("writing nothing at 4096 made the file %" B_PRIdOFF
			" bytes", stat.st_size);
	}
	return RESULT_PASS;
}


/*!	KMessage.cpp: setting a field that already had a value stored the
	element size through a NULL out-parameter. The same source is in the
	kernel, the boot loader, the runtime_loader and libroot.
*/
static int
test_kmessage_set_twice()
{
	BPrivate::KMessage message;
	if (message.SetInt32("value", 1) != B_OK)
		return fail("the first SetInt32() failed");
	if (message.SetInt32("value", 2) != B_OK)
		return fail("setting the field again failed");
	if (message.SetInt64("big", 1) != B_OK || message.SetInt64("big", 3) != B_OK
		|| message.SetBool("flag", false) != B_OK
		|| message.SetBool("flag", true) != B_OK) {
		return fail("setting an int64 or bool field twice failed");
	}

	int32 value = message.GetInt32("value", -1);
	if (value != 2 || message.GetInt64("big", -1) != 3
		|| !message.GetBool("flag", false)) {
		return fail("the fields hold %" B_PRId32 ", %" B_PRId64 ", %d", value,
			message.GetInt64("big", -1), message.GetBool("flag", false));
	}
	return RESULT_PASS;
}


//	#pragma mark - libroot (0143)


static void*
log_long_message(void*)
{
	BString text;
	text.SetTo('y', 200000);
	syslog(LOG_ERR, "%s", text.String());
	return NULL;
}


/*!	syslog.cpp: the '\n' went at the full length of the formatted message,
	past the 2048 byte buffer on the stack, and write_port() sent what lay
	beyond it.
*/
static int
test_syslog_long_message()
{
	char marker[64];
	snprintf(marker, sizeof(marker), "afxtest-%" B_PRId32 "-",
		(int32)getpid());
	BString text(marker);
	text.Append('x', 3000);
	syslog(LOG_ERR, "%s", text.String());

	// in a small stack, the old '\n' lands far past the stack's end
	pthread_attr_t attributes;
	pthread_attr_init(&attributes);
	pthread_attr_setstacksize(&attributes, 64 * 1024);
	pthread_t thread;
	if (pthread_create(&thread, &attributes, &log_long_message, NULL) != 0)
		return fail("cannot start a thread");
	pthread_join(thread, NULL);

	// the daemon gets what fits: 1991 characters in all
	size_t expected = 1991 - strlen(marker);
	for (int attempt = 0; attempt < 30; attempt++) {
		FILE* log = fopen("/var/log/syslog", "r");
		if (log == NULL)
			return skip("no /var/log/syslog");

		char* line = NULL;
		size_t lineSize = 0;
		ssize_t found = -1;
		while (getline(&line, &lineSize, log) > 0) {
			const char* start = strstr(line, marker);
			if (start != NULL)
				found = strspn(start + strlen(marker), "x");
		}
		free(line);
		fclose(log);

		if (found >= 0) {
			if ((size_t)found != expected) {
				return fail("the log has %zd characters of the message, not "
					"%zu", found, expected);
			}
			return RESULT_PASS;
		}
		snooze(100000);
	}
	return fail("the message is not in /var/log/syslog");
}


/*!	pthread.cpp: the main thread's pthread structure is static, but once
	the thread was detached, exit() handed it to free().
*/
static int
test_pthread_detach_main()
{
	if (pthread_detach(pthread_self()) != 0)
		return fail("pthread_detach() failed");
	exit(RESULT_PASS);
}


static void
exit_from_timer(union sigval)
{
	exit(RESULT_PASS);
}


/*!	timer_support.cpp: a SIGEV_THREAD timer's thread keeps its pthread
	structure on its stack, and exit() from the notify function freed it.
*/
static int
test_timer_thread_exit()
{
	struct sigevent event = {};
	event.sigev_notify = SIGEV_THREAD;
	event.sigev_notify_function = &exit_from_timer;

	timer_t timer;
	if (timer_create(CLOCK_MONOTONIC, &event, &timer) != 0)
		return fail("timer_create() failed: %s", strerror(errno));

	struct itimerspec time = {};
	time.it_value.tv_nsec = 10 * 1000 * 1000;
	if (timer_settime(timer, 0, &time, NULL) != 0)
		return fail("timer_settime() failed: %s", strerror(errno));

	sleep(5);
	return fail("the timer did not fire");
}


// mmap()'s arguments for remap_after_fault(); hidden, so that the assembly
// can address it directly
extern "C" {
__attribute__((visibility("hidden"))) uintptr_t gRemapArguments[6];
}


/*!	A SIGSEGV handler whose last act is mmap(), as a tail call: mmap() then
	returns into the commpage's signal trampoline, and names its area after
	the image that holds its return address. In assembly, because GCC 13 has
	no naked functions on arm64.
*/
extern "C" void remap_after_fault(int, siginfo_t*, void*);
asm(
	".text\n"
	".p2align 2\n"
	".type	remap_after_fault, %function\n"
	"remap_after_fault:\n"
	"	adrp	x9, gRemapArguments\n"
	"	add		x9, x9, :lo12:gRemapArguments\n"
	"	ldp		x0, x1, [x9]\n"
	"	ldp		x2, x3, [x9, #16]\n"
	"	ldp		x4, x5, [x9, #32]\n"
	"	b		mmap\n"
	".size	remap_after_fault, . - remap_after_fault\n");


/*!	runtime_loader commpage.cpp: looking up the image of an address in the
	commpage wrote the symbol name through the NULL that callers who want
	only the image pass (mmap(), pthread_create()).
*/
static int
test_commpage_image_lookup()
{
	size_t size = B_PAGE_SIZE;
	void* page = mmap(NULL, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1,
		0);
	if (page == MAP_FAILED)
		return fail("mmap() failed: %s", strerror(errno));

	gRemapArguments[0] = (uintptr_t)page;
	gRemapArguments[1] = size;
	gRemapArguments[2] = PROT_READ | PROT_WRITE;
	gRemapArguments[3] = MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED;
	gRemapArguments[4] = (uintptr_t)-1;
	gRemapArguments[5] = 0;

	struct sigaction action = {};
	action.sa_sigaction = &remap_after_fault;
	action.sa_flags = SA_SIGINFO;
	sigaction(SIGSEGV, &action, NULL);

	// faults once; the handler maps the page, and the store is repeated
	*(volatile int*)page = 42;
	if (*(volatile int*)page != 42)
		return fail("the page does not hold what was stored");
	return RESULT_PASS;
}


static bool
check_date(const char* text, int hour, int minute, BString& error)
{
	time_t date = parsedate(text, time(NULL));
	if (date == -1) {
		error.SetToFormat("parsedate(\"%s\") failed", text);
		return false;
	}

	struct tm tm;
	localtime_r(&date, &tm);
	if (tm.tm_year != 120 || tm.tm_mon != 11 || tm.tm_mday != 25
		|| tm.tm_hour != hour || tm.tm_min != minute) {
		error.SetToFormat("\"%s\" gives %04d-%02d-%02d %02d:%02d", text,
			tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour,
			tm.tm_min);
		return false;
	}
	return true;
}


/*!	parsedate.cpp: a '-' in a format advanced the format but not the
	position, so every field after it was read from the wrong slot.
*/
static int
test_parsedate_dash()
{
	BString error;
	if (!check_date("12-25-2020 10:30", 10, 30, error)
		|| !check_date("12-25-2020, 10:30 pm", 22, 30, error)
		|| !check_date("12-25-2020 10", 10, 0, error)) {
		return fail("%s", error.String());
	}
	return RESULT_PASS;
}


static const driver_parameter*
find_parameter(const driver_parameter* parameters, int count,
	const char* name)
{
	for (int i = 0; i < count; i++) {
		if (strcmp(parameters[i].name, name) == 0)
			return &parameters[i];
	}
	return NULL;
}


/*!	driver_settings.cpp: "key =" with nothing after the '=' dropped the
	parameter and leaked its values.
*/
static int
test_driver_settings_empty_assignment()
{
	void* handle = parse_driver_settings_string("key =");
	if (handle == NULL)
		return fail("\"key =\" does not parse");
	const driver_settings* settings = get_driver_settings(handle);
	const driver_parameter* key = find_parameter(settings->parameters,
		settings->parameter_count, "key");
	bool keyFound = key != NULL && key->value_count == 0;
	unload_driver_settings(handle);
	if (!keyFound)
		return fail("\"key =\" has no parameter \"key\" without values");

	handle = parse_driver_settings_string("block { key = }\nafter 1");
	if (handle == NULL)
		return fail("\"block { key = }\" does not parse");
	settings = get_driver_settings(handle);
	const driver_parameter* block = find_parameter(settings->parameters,
		settings->parameter_count, "block");
	bool found = block != NULL
		&& find_parameter(block->parameters, block->parameter_count, "key")
			!= NULL
		&& find_parameter(settings->parameters, settings->parameter_count,
			"after") != NULL;
	unload_driver_settings(handle);
	if (!found)
		return fail("\"block { key = }\" loses \"key\" or what follows");
	return RESULT_PASS;
}


/*!	utimes.c: utimensat() set the time to the caller's tv_sec even when
	tv_nsec said UTIME_NOW.
*/
static int
test_utimensat_now()
{
	const char* path = "/tmp/afx-utimensat";
	if (write_file(path, "x", 1) != B_OK)
		return fail("cannot create %s", path);

	struct timespec times[2] = { { 1000, 0 }, { 1000, 0 } };
	utimensat(AT_FDCWD, path, times, 0);

	times[0].tv_nsec = UTIME_OMIT;
	times[1].tv_sec = 0;
	times[1].tv_nsec = UTIME_NOW;
	if (utimensat(AT_FDCWD, path, times, 0) != 0)
		return fail("utimensat() failed: %s", strerror(errno));

	struct stat stat;
	::stat(path, &stat);
	unlink(path);

	time_t now = time(NULL);
	if (stat.st_mtime < now - 60 || stat.st_mtime > now + 60) {
		return fail("UTIME_NOW set the time to %" B_PRIdTIME,
			(bigtime_t)stat.st_mtime);
	}
	return RESULT_PASS;
}


//	#pragma mark - app, interface, support kits (0144)


/*!	Application.cpp: Quit() on an unlocked application that never ran
	deleted it, and unlocked it afterwards.
*/
static int
test_application_quit_unlocked()
{
	BApplication* application = new BApplication(kSignature);
	application->Unlock();
	application->Quit();
		// prints that the application must be locked
	exit(RESULT_PASS);
}


/*!	Key.cpp: the BPasswordKey copy constructor did not copy anything.
*/
static int
test_password_key_copy()
{
	BPasswordKey key("secret", B_KEY_PURPOSE_GENERIC, "afxtest");
	BPasswordKey copy(key);
	if (strcmp(copy.Password(), "secret") != 0
		|| strcmp(copy.Identifier(), "afxtest") != 0) {
		return fail("the copy has password \"%s\" and identifier \"%s\"",
			copy.Password(), copy.Identifier());
	}
	return RESULT_PASS;
}


/*!	ChannelControl.cpp: growing the channel count copied that many bytes of
	the values and limits, not that many int32s.
*/
static int
test_channel_count_grow()
{
	BApplication application(kSignature);
	BChannelSlider slider(BRect(0, 0, 100, 100), "slider", "slider", NULL, 2);
	slider.SetLimitsFor(1, 5, 90);
	slider.SetValueFor(0, 7);
	slider.SetValueFor(1, 42);
	if (slider.SetChannelCount(4) != B_OK)
		return fail("SetChannelCount(4) failed");

	int32 minimum = 0;
	int32 maximum = 0;
	slider.GetLimitsFor(1, &minimum, &maximum);
	if (slider.ValueFor(0) != 7 || slider.ValueFor(1) != 42 || minimum != 5
		|| maximum != 90) {
		return fail("after growing: values %" B_PRId32 ", %" B_PRId32
			", limits %" B_PRId32 "..%" B_PRId32, slider.ValueFor(0),
			slider.ValueFor(1), minimum, maximum);
	}
	return RESULT_PASS;
}


/*!	Font.cpp: StringWidth() returned an uninitialized float when the server
	measured nothing (a font of size 0).
*/
static int
test_font_zero_size_width()
{
	BApplication application(kSignature);
	BFont font(be_plain_font);
	font.SetSize(0);
	float width = font.StringWidth("abc");
	if (width != 0)
		return fail("a font of size 0 gives a width of %g", width);
	return RESULT_PASS;
}


//! Shape.cpp: copying an empty shape passed NULL to memcpy().
static int
test_shape_copy_empty()
{
	BShape empty;
	BShape copy(empty);
	BShape assigned;
	assigned = empty;
	if (!(copy == empty) || !(assigned == empty))
		return fail("the copies of an empty shape are not empty");
	return RESULT_PASS;
}


/*!	String.cpp: the in-place CharacterEscape()/CharacterDeescape() read
	their own buffer after _MakeWritable() had moved it, which happens when
	the string has a NUL inside. Under the guarded heap, that read faults.
*/
static int
test_string_escape_in_place()
{
	BString string("a\"b\"c and some text, so that the block is larger");
	string.SetByteAt(3, '\0');
		// Length() stays, strlen() is 3
	string.CharacterEscape("\"", '\\');
	if (string != "a\\\"b")
		return fail("the escaped string is \"%s\"", string.String());

	BString escaped("a\\\"b and some more text, for a larger block");
	escaped.SetByteAt(4, '\0');
	escaped.CharacterDeescape('\\');
	if (escaped != "a\"b")
		return fail("the de-escaped string is \"%s\"", escaped.String());

	BString inside("xx\"yy\"zz and some text for a larger block");
	inside.CharacterEscape(inside.String() + 2, "\"", '\\');
	if (inside != "\\\"yy\\\"zz and some text for a larger block")
		return fail("escaping its own tail gives \"%s\"", inside.String());
	return RESULT_PASS;
}


/*!	Url.cpp: the scheme check counted with an int8, which wrapped at 128
	and read before the string.
*/
static int
test_url_long_scheme()
{
	BString scheme;
	scheme.SetTo('a', 200);
	BString text(scheme);
	text << ":x";
	BUrl url(text.String(), false);
	if (url.Protocol() != scheme)
		return fail("the scheme is not the 200 letters given");
	if (!url.IsValid())
		return fail("a URL with a 200-letter scheme is not valid");
	return RESULT_PASS;
}


/*!	TextView.cpp and ColumnListView.cpp are tested here only for the
	column list: dragging a column title right of the last column moved the
	column to the front, with an uninitialized left edge.
*/
static int
test_column_drag_past_last()
{
	BApplication application(kSignature);
	BWindow* window = new BWindow(BRect(100, 100, 699, 399), "afxtest",
		B_TITLED_WINDOW, B_NOT_ZOOMABLE);
	BColumnListView* list = new BColumnListView(window->Bounds(), "list",
		B_FOLLOW_ALL, B_WILL_DRAW);
	list->SetColumnFlags(B_ALLOW_COLUMN_MOVE);
	const char* titles[] = { "A", "B", "C" };
	for (int32 i = 0; i < 3; i++)
		list->AddColumn(new BStringColumn(titles[i], 100, 50, 200, 0), i);
	window->AddChild(list);
	window->Show();

	BView* title = NULL;
	if (window->Lock()) {
		title = list->FindView("title_view");
		window->Unlock();
	}
	if (title == NULL)
		return fail("the list has no title view");

	BPoint origin;
	if (window->Lock()) {
		origin = title->ConvertToScreen(B_ORIGIN);
		window->Unlock();
	}

	// Columns A, B and C are at 15, 116 and 217, 100 wide each. Press on
	// A, and move fast: the first move starts the drag, the second one is
	// the first to move the column, and it is right of C already.
	float y = origin.y + 5;
	const struct {
		uint32	what;
		float	x;
	} steps[] = {
		{ B_MOUSE_DOWN, 40 },
		{ B_MOUSE_MOVED, 560 },
		{ B_MOUSE_MOVED, 580 },
		{ B_MOUSE_UP, 580 },
	};
	// to the preferred handler: only then does the window find the view by
	// its token, as it does for what app_server sends
	BMessenger messenger(NULL, window);
	for (size_t i = 0; i < sizeof(steps) / sizeof(steps[0]); i++) {
		BMessage message(steps[i].what);
		message.AddPoint("screen_where", BPoint(origin.x + steps[i].x, y));
		message.AddInt64("when", system_time());
		message.AddInt32("buttons",
			steps[i].what == B_MOUSE_UP ? 0 : B_PRIMARY_MOUSE_BUTTON);
		message.AddInt32("modifiers", 0);
		message.AddInt32("clicks", 1);
		message.AddInt32("_view_token", _get_object_token_(title));

		// one at a time: the window must not merge the moves
		BMessage reply;
		messenger.SendMessage(&message, &reply);
	}

	BString order;
	if (window->Lock()) {
		for (int32 i = 0; i < list->CountColumns(); i++) {
			BString name;
			list->ColumnAt(i)->GetColumnName(&name);
			order << name;
		}
		window->Quit();
	}
	if (order != "BCA")
		return fail("after the drag the columns are \"%s\"", order.String());
	return RESULT_PASS;
}


//	#pragma mark - storage, translation, network, media kits (0145)


/*!	Query.cpp: an unquoted '%' without its closing '%' looped forever in
	_ParseDates(), as did an escaped quote before a '%'.
*/
static int
test_query_lone_percent()
{
	BVolume volume;
	BVolumeRoster().GetBootVolume(&volume);

	const char* predicates[] = {
		"name==100%",
		"name==\"*5\\\"%*\"",
		"name==\"*%*\" && size>100%",
	};
	for (size_t i = 0; i < sizeof(predicates) / sizeof(predicates[0]); i++) {
		BQuery query;
		query.SetVolume(&volume);
		query.SetPredicate(predicates[i]);
		query.Fetch();
			// returns at all: an error for these is fine
	}
	return RESULT_PASS;
}


/*!	TranslatorRoster.cpp: every directory of a list but the last lost its
	last character, and a missing directory added garbage to the count.
*/
static int
test_translator_path_list()
{
	BTranslatorRoster roster;
	status_t status = roster.AddTranslators(
		"/boot/system/add-ons/Translators:/nonexistent-afxtest");

	translator_id* translators = NULL;
	int32 count = 0;
	roster.GetAllTranslators(&translators, &count);
	delete[] translators;

	if (count == 0)
		return fail("no translators from the first directory of the list");
	if (status != B_OK)
		return fail("AddTranslators() says %s", strerror(status));
	return RESULT_PASS;
}


/*!	TextSnifferAddon.cpp: the troff check and the keyword scan read the
	UTF-16 buffer past its length. The sniffing is done in the registrar.
*/
static int
test_text_sniffer()
{
	const char* bomOnly = "/tmp/afx-bom";
	const char* dot = "/tmp/afx-dot";
	const char* utf8 = "/tmp/afx-utf8";
	write_file(bomOnly, "\xff\xfe", 2);
	write_file(dot, ".\n", 2);
	const char* text = "Gr\xc3\xbc\xc3\x9f" "e aus K\xc3\xb6ln, \xc3\xa4 \xc3\xb6"
		"\n";
	write_file(utf8, text, strlen(text));

	team_id registrar = find_team("registrar");
	const char* files[] = { bomOnly, dot, utf8 };
	BString types;
	for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
		BMimeType type;
		if (BMimeType::GuessMimeType(files[i], &type) == B_OK)
			types << type.Type() << " ";
		unlink(files[i]);
	}

	if (find_team("registrar") != registrar)
		return fail("the registrar is gone after sniffing");
	if (strstr(types.String(), "troff") != NULL)
		return fail("a text file is troff: %s", types.String());
	return RESULT_PASS;
}


/*!	NetEndpoint.cpp: IsDataPending() on a closed endpoint did FD_SET(-1).
*/
static int
test_net_endpoint_closed()
{
	BNetEndpoint endpoint;
	endpoint.Close();
	if (endpoint.IsDataPending(0))
		return fail("a closed endpoint has data pending");
	BNetEndpoint* client = endpoint.Accept(10);
	if (client != NULL)
		return fail("a closed endpoint accepted a connection");
	return RESULT_PASS;
}


//! NetworkAddress.cpp: IPv6 prefix lengths were rounded down to bytes.
static int
test_ipv6_prefix_length()
{
	const uint32 lengths[] = { 0, 8, 60, 64, 97, 127, 128 };
	for (size_t i = 0; i < sizeof(lengths) / sizeof(lengths[0]); i++) {
		BNetworkAddress mask;
		if (mask.SetToMask(AF_INET6, lengths[i]) != B_OK)
			return fail("SetToMask(AF_INET6, %" B_PRIu32 ") failed", lengths[i]);
		if (mask.PrefixLength() != (ssize_t)lengths[i]) {
			return fail("a /%" B_PRIu32 " mask has a prefix length of %zd",
				lengths[i], mask.PrefixLength());
		}
	}
	return RESULT_PASS;
}


/*!	MediaFile.cpp: a file that no reader takes, or a writer without a
	format, leaked the BFile and its descriptor.
*/
static int
test_media_file_descriptors()
{
	BApplication application(kSignature);
	const char* textPath = "/tmp/afx-notmedia.txt";
	const char* outPath = "/tmp/afx-out.media";
	write_file(textPath, "not a media file\n", 17);

	entry_ref textRef;
	get_ref_for_path(textPath, &textRef);
	BEntry(outPath).Remove();
	write_file(outPath, "", 0);
	entry_ref outRef;
	get_ref_for_path(outPath, &outRef);

	int before = lowest_free_fd();
	for (int i = 0; i < 20; i++) {
		BMediaFile reader(&textRef);
		BMediaFile writer(&outRef, (const media_file_format*)NULL);
	}
	int after = lowest_free_fd();
	unlink(textPath);
	unlink(outPath);

	if (after != before)
		return fail("40 files leaked %d descriptors", after - before);
	return RESULT_PASS;
}


class NullEncoder : public BMediaEncoder {
public:
	NullEncoder(const media_codec_info* info)
		:
		BMediaEncoder(info)
	{
	}

protected:
	virtual status_t WriteChunk(const void*, size_t, media_encode_info*)
	{
		return B_OK;
	}
};


/*!	MediaEncoder.cpp: SetFormat() with an output format that no encoder
	takes released the encoder, and then used it.
*/
static int
test_media_encoder_bad_format()
{
	BApplication application(kSignature);
	int32 cookie = 0;
	media_codec_info info;
	if (get_next_encoder(&cookie, &info) != B_OK)
		return skip("no encoders installed");

	NullEncoder encoder(&info);
	if (encoder.InitCheck() != B_OK)
		return skip("the encoder \"%s\" does not initialize", info.pretty_name);

	media_format input;
	media_format output;
	input.type = B_MEDIA_RAW_AUDIO;
	output.type = B_MEDIA_UNKNOWN_TYPE;
	if (encoder.SetFormat(&input, &output) == B_OK)
		return fail("an encoder took the unknown media type");
	return RESULT_PASS;
}


/*!	DefaultMediaTheme.cpp: a parameter web without groups (what a node
	without controls gives) made the theme use a tab view it never created.
*/
static int
test_media_theme_empty_web()
{
	BApplication application(kSignature);
	BParameterWeb* web = new BParameterWeb();
	BView* view = BMediaTheme::ViewFor(web);
	if (view != NULL)
		return fail("the theme made a view for a web without controls");
	return RESULT_PASS;
}


/*!	AdapterIO.cpp: FlushBefore() leaked the new buffer whenever it did not
	adopt it (nothing to flush, or an error).
*/
static int
test_adapter_io_flush()
{
	BAdapterIO io(B_MEDIA_STREAMING | B_MEDIA_SEEKABLE, B_INFINITE_TIMEOUT);
	for (int i = 0; i < 1000; i++)
		io.FlushBefore(-1);

	size_t before = resident_size();
	for (int i = 0; i < 200000; i++)
		io.FlushBefore(-1);
	size_t after = resident_size();

	if (after > before + 4 * 1024 * 1024) {
		return fail("200000 flushes grew the team by %zu KB",
			(after - before) / 1024);
	}
	return RESULT_PASS;
}


//	#pragma mark - Tracker (0146)


/*!	TrackerString.cpp: '?' and a negated bracket matched the end of the
	string and went on past it; the 101st '*' was stored past the arrays.
*/
static int
test_tracker_glob()
{
	const struct {
		const char*	string;
		const char*	pattern;
		bool		matches;
	} cases[] = {
		{ "ab", "??", true },
		{ "ab", "???", false },
		{ "ab", "????????????????????????????????????????", false },
		{ "a", "a[!x]", false },
		{ "ay", "a[!x]", true },
		{ "abc", "*?", true },
		{ "abc", "*b?", true },
		{ "abc", "*b??", false },
	};
	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		TrackerString string(cases[i].string);
		if (string.MatchesGlob(cases[i].pattern, true) != cases[i].matches) {
			return fail("\"%s\" %s \"%s\"", cases[i].string,
				cases[i].matches ? "does not match" : "matches",
				cases[i].pattern);
		}
	}

	BString many;
	for (int i = 0; i < 101; i++)
		many << "*a";
	BString as;
	as.SetTo('a', 101);
	if (TrackerString(as.String()).MatchesGlob(many.String(), true))
		return fail("101 wildcards match, beyond the 100 the matcher holds");
	return RESULT_PASS;
}


// Tracker's PoseInfo attribute, as it is on disk
struct PoseInfoAttribute {
	bool	invisible;
	int64	initedDirectory;
	float	x;
	float	y;
};


/*!	NavMenu.cpp: a link to an invisible folder deleted the resolved Model
	while a BModelOpener still held it. Under the guarded heap, the opener's
	destructor faults.
*/
static int
test_nav_menu_hidden_link()
{
	BApplication application(kSignature);
	const char* directory = "/tmp/afx-nav";
	BString hidden(directory);
	hidden << "/hidden";
	BString link(directory);
	link << "/link";

	create_directory(hidden.String(), 0755);
	PoseInfoAttribute info = { true, -1, 0, 0 };
	BNode node(hidden.String());
	if (node.WriteAttr("_trk/pinfo_le", B_RAW_TYPE, 0, &info, sizeof(info))
			!= (ssize_t)sizeof(info)) {
		return fail("cannot write the pose info of %s", hidden.String());
	}
	unlink(link.String());
	if (symlink(hidden.String(), link.String()) != 0)
		return fail("cannot create the link %s", link.String());

	entry_ref ref;
	get_ref_for_path(link.String(), &ref);
	Model model(&ref);
	if (model.InitCheck() != B_OK)
		return fail("no model for %s", link.String());

	BMessage message(B_REFS_RECEIVED);
	ModelMenuItem* item = BNavMenu::NewModelItem(&model, &message,
		BMessenger());
	delete item;

	unlink(link.String());
	rmdir(hidden.String());
	rmdir(directory);
	if (item != NULL)
		return fail("the menu lists a link to an invisible folder");
	return RESULT_PASS;
}


/*!	WidgetAttributeText.cpp: starting to edit a rating passed no pose view
	to the text fitting, which asked that view for its font when the number
	did not fit its column.
*/
static int
test_rating_edit_without_view()
{
	BApplication application(kSignature);
	const char* path = "/tmp/afx-rating";
	write_file(path, "", 0);
	int32 rating = 1234567890;
	BNode(path).WriteAttr("Media:Rating", B_INT32_TYPE, 0, &rating,
		sizeof(rating));

	entry_ref ref;
	get_ref_for_path(path, &ref);
	Model model(&ref, true, true);
	BPrivate::BColumn column("Rating", 20, B_ALIGN_LEFT, "Media:Rating",
		B_INT32_TYPE, "rating", false, true);
	WidgetAttributeText* text = WidgetAttributeText::NewWidgetText(&model,
		&column, NULL);
	BTextView textView(BRect(0, 0, 100, 20), "text", BRect(0, 0, 100, 20),
		B_FOLLOW_NONE, B_WILL_DRAW);
	text->SetupEditing(&textView);

	BString edited(textView.Text());
	delete text;
	unlink(path);
	if (edited != "1234567890")
		return fail("editing starts with \"%s\"", edited.String());
	return RESULT_PASS;
}


//	#pragma mark - app_server (0147)


/*!	Painter.cpp: a gradient without color stops painted what the stack or
	an uninitialized array held.
*/
static int
test_gradient_without_stops()
{
	BApplication application(kSignature);
	OffscreenView offscreen(BRect(0, 0, 63, 63));
	BView* view = offscreen.View();
	if (view == NULL)
		return fail("no offscreen view");

	view->SetHighColor(255, 255, 255);
	view->FillRect(view->Bounds());
	BGradientLinear linear;
		// start and end at the origin: the vertical fast path
	view->FillRect(BRect(0, 0, 31, 63), linear);
	BGradientRadial radial(BPoint(48, 32), 16);
	view->FillRect(BRect(32, 0, 63, 63), radial);
	view->Sync();

	for (int32 y = 0; y < 64; y++) {
		for (int32 x = 0; x < 64; x++) {
			uint32 pixel = offscreen.PixelAt(x, y);
			if (pixel != 0xffffff) {
				return fail("a gradient without colors painted %06" B_PRIx32
					" at %" B_PRId32 ",%" B_PRId32, pixel, x, y);
			}
		}
	}
	return RESULT_PASS;
}


/*!	ServerPicture.cpp: a copy of a picture that draws another one lost the
	list of nested pictures, and drawing the copy crashed app_server.
*/
static int
test_picture_clone_nested()
{
	BApplication application(kSignature);
	OffscreenView offscreen(BRect(0, 0, 63, 63));
	BView* view = offscreen.View();
	if (view == NULL)
		return fail("no offscreen view");

	view->BeginPicture(new BPicture);
	view->SetHighColor(255, 0, 0);
	view->FillRect(BRect(0, 0, 63, 63));
	BPicture* square = view->EndPicture();

	// app_server nests a copy of the square in this picture ...
	view->BeginPicture(new BPicture);
	view->DrawPicture(square, B_ORIGIN);
	BPicture* outer = view->EndPicture();

	// ... and copies both for this one (AS_CLONE_PICTURE)
	BPicture clone(*outer);

	view->SetHighColor(255, 255, 255);
	view->FillRect(view->Bounds());
	view->DrawPicture(&clone, B_ORIGIN);
	view->Sync();

	uint32 pixel = offscreen.PixelAt(10, 10);
	delete square;
	delete outer;
	if (pixel != 0xff0000)
		return fail("the copy drew %06" B_PRIx32 ", not red", pixel);
	return RESULT_PASS;
}


/*!	ClientMemoryAllocator.cpp, ServerBitmap.h: rows of 1 GB overflowed the
	bitmap's length to 0, and app_server handed out a block of no size,
	which Free() later merged with itself.
*/
static int
test_bitmap_length_overflow()
{
	BApplication application(kSignature);
	for (int i = 0; i < 2; i++) {
		BBitmap bitmap(BRect(0, 0, 0, 3), 0, B_RGB32, 1 << 30);
		if (bitmap.InitCheck() == B_OK)
			return fail("app_server made a bitmap of 4 rows of 1 GB");
	}
	BBitmap normal(BRect(0, 0, 15, 15), B_RGB32);
	if (normal.InitCheck() != B_OK)
		return fail("a normal bitmap fails after the oversized ones");
	return RESULT_PASS;
}


/*!	DefaultDecorator.cpp: int8 loops over the border width never ended once
	a large bold font made the border 128 pixels wide, and app_server hung.
	Intrusive: switches to the default decorator and a 400 point bold font
	while it runs.
*/
static int
test_decorator_wide_border()
{
	BApplication application(kSignature);
	BString decorator;
	BPrivate::get_decorator(decorator);
	font_family family;
	font_style style;
	be_bold_font->GetFamilyAndStyle(&family, &style);
	float size = be_bold_font->Size();

	if (BPrivate::set_decorator("Default") != B_OK)
		return skip("cannot switch to the default decorator");
	_set_system_font_("bold", family, style, 400);

	BWindow* window = new BWindow(BRect(100, 100, 499, 399), "afxtest",
		B_TITLED_WINDOW, 0);
	window->Show();
	snooze(500000);

	// app_server drew the border if the window answers
	bool answered = false;
	if (window->Lock()) {
		window->Sync();
		answered = true;
		window->Quit();
	}

	_set_system_font_("bold", family, style, size);
	BPrivate::set_decorator(decorator);
	if (!answered)
		return fail("the window does not answer");
	return RESULT_PASS;
}


//	#pragma mark - servers and apps (0148)


/*!	TRoster.cpp: GetRecentDocuments() copied the file type and signature
	into 240 byte buffers without a bound, in the registrar. 64 KB of them
	take the registrar down; a few hundred bytes corrupt its heap quietly.
*/
static int
test_registrar_long_type()
{
	team_id registrar = find_team("registrar");
	BString type("text/");
	type.Append('x', 64 * 1024);
	BString signature("application/x-vnd.");
	signature.Append('y', 64 * 1024);

	BMessage refs;
	be_roster->GetRecentDocuments(&refs, 10, type.String(), NULL);
	be_roster->GetRecentDocuments(&refs, 10, NULL, signature.String());
	be_roster->GetRecentFolders(&refs, 10, signature.String());

	// the registrar still answers, and is still the same team
	BMessage normal;
	be_roster->GetRecentDocuments(&normal, 10, "text/plain", NULL);
	if (find_team("registrar") != registrar)
		return fail("the registrar is gone");
	return RESULT_PASS;
}


//	#pragma mark - the runner


struct Test {
	const char*	name;
	int			(*function)();
	int			timeout;
	bool		intrusive;
};

static const Test kTests[] = {
	{ "writev-zero-length", &test_writev_zero_length, 10, false },
	{ "kmessage-set-twice", &test_kmessage_set_twice, 10, false },
	{ "syslog-long-message", &test_syslog_long_message, 20, false },
	{ "pthread-detach-main", &test_pthread_detach_main, 10, false },
	{ "timer-thread-exit", &test_timer_thread_exit, 10, false },
	{ "commpage-image-lookup", &test_commpage_image_lookup, 10, false },
	{ "parsedate-dash", &test_parsedate_dash, 10, false },
	{ "driver-settings-empty-assignment",
		&test_driver_settings_empty_assignment, 10, false },
	{ "utimensat-now", &test_utimensat_now, 10, false },
	{ "application-quit-unlocked", &test_application_quit_unlocked, 20,
		false },
	{ "password-key-copy", &test_password_key_copy, 10, false },
	{ "channel-count-grow", &test_channel_count_grow, 20, false },
	{ "font-zero-size-width", &test_font_zero_size_width, 20, false },
	{ "shape-copy-empty", &test_shape_copy_empty, 10, false },
	{ "string-escape-in-place", &test_string_escape_in_place, 10, false },
	{ "url-long-scheme", &test_url_long_scheme, 10, false },
	{ "column-drag-past-last", &test_column_drag_past_last, 30, false },
	{ "query-lone-percent", &test_query_lone_percent, 20, false },
	{ "translator-path-list", &test_translator_path_list, 30, false },
	{ "text-sniffer", &test_text_sniffer, 20, false },
	{ "net-endpoint-closed", &test_net_endpoint_closed, 10, false },
	{ "ipv6-prefix-length", &test_ipv6_prefix_length, 10, false },
	{ "media-file-descriptors", &test_media_file_descriptors, 60, false },
	{ "media-encoder-bad-format", &test_media_encoder_bad_format, 30,
		false },
	{ "media-theme-empty-web", &test_media_theme_empty_web, 30, false },
	{ "adapter-io-flush", &test_adapter_io_flush, 60, false },
	{ "tracker-glob", &test_tracker_glob, 10, false },
	{ "nav-menu-hidden-link", &test_nav_menu_hidden_link, 20, false },
	{ "rating-edit-without-view", &test_rating_edit_without_view, 20,
		false },
	{ "gradient-without-stops", &test_gradient_without_stops, 20, false },
	{ "bitmap-length-overflow", &test_bitmap_length_overflow, 20, false },
	{ "registrar-long-type", &test_registrar_long_type, 20, false },
	// last: without patch 0147, app_server dies of it
	{ "picture-clone-nested", &test_picture_clone_nested, 20, false },
	{ "decorator-wide-border", &test_decorator_wide_border, 30, true },
};


static int
run_test(const Test& test, BString& reason)
{
	int reasonPipe[2];
	if (pipe(reasonPipe) != 0) {
		reason = "no pipe";
		return RESULT_FAIL;
	}

	fflush(stdout);
	pid_t child = fork();
	if (child < 0) {
		reason.SetToFormat("fork() failed: %s", strerror(errno));
		return RESULT_FAIL;
	}
	if (child == 0) {
		close(reasonPipe[0]);
		sReasonFD = reasonPipe[1];
		alarm(test.timeout);
		exit(test.function());
	}

	close(reasonPipe[1]);
	int status;
	while (waitpid(child, &status, 0) < 0 && errno == EINTR)
		;

	char buffer[512];
	ssize_t length = read(reasonPipe[0], buffer, sizeof(buffer) - 1);
	close(reasonPipe[0]);
	reason.SetTo(buffer, length > 0 ? length : 0);

	if (WIFSIGNALED(status)) {
		if (WTERMSIG(status) == SIGALRM) {
			reason.SetToFormat("did not finish within %d seconds",
				test.timeout);
		} else if (WTERMSIG(status) == SIGKILLTHR)
			reason = "crashed (debug_server killed it)";
		else
			reason.SetToFormat("died of signal %d", WTERMSIG(status));
		return RESULT_FAIL;
	}
	int result = WEXITSTATUS(status);
	if (result != RESULT_PASS && result != RESULT_SKIP) {
		if (reason.IsEmpty())
			reason.SetToFormat("exited with %d", result);
		return RESULT_FAIL;
	}
	return result;
}


int
main(int argc, char** argv)
{
	bool intrusive = false;
	BStringList names;
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--fat") == 0 && i + 1 < argc)
			sFatDirectory = argv[++i];
		else if (strcmp(argv[i], "--intrusive") == 0)
			intrusive = true;
		else if (strcmp(argv[i], "--list") == 0) {
			for (size_t j = 0; j < sizeof(kTests) / sizeof(kTests[0]); j++)
				printf("%s\n", kTests[j].name);
			return 0;
		} else
			names.Add(argv[i]);
	}

	int passed = 0;
	int failed = 0;
	int skipped = 0;
	for (size_t i = 0; i < sizeof(kTests) / sizeof(kTests[0]); i++) {
		const Test& test = kTests[i];
		if (!names.IsEmpty() ? !names.HasString(test.name)
				: test.intrusive && !intrusive) {
			continue;
		}

		BString reason;
		int result = run_test(test, reason);
		if (result == RESULT_PASS) {
			printf("PASS %s\n", test.name);
			passed++;
		} else if (result == RESULT_SKIP) {
			printf("SKIP %s: %s\n", test.name, reason.String());
			skipped++;
		} else {
			printf("FAIL %s: %s\n", test.name, reason.String());
			failed++;
		}
		fflush(stdout);
	}

	printf("SELFTEST %s %d/%d", failed == 0 ? "PASS" : "FAIL", passed,
		passed + failed);
	if (skipped > 0)
		printf(" (%d skipped)", skipped);
	printf("\n");
	return failed == 0 ? 0 : 1;
}
