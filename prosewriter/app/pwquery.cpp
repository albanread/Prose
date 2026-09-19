// pwquery — definitive scripting probe: sends with a timeout, prints the
// reply. Usage: pwquery [signature] [property]
#include <Application.h>
#include <Message.h>
#include <Messenger.h>
#include <String.h>
#include <cstdio>
#include <cstring>

int
main(int argc, char** argv)
{
	const char* sig = argc > 1 ? argv[1]
		: "application/x-vnd.prose.ProseWriter";
	const char* prop = argc > 2 ? argv[2] : "Title";
	BApplication app("application/x-vnd.prose.pwquery");
	BMessenger msgr(sig);
	printf("messenger valid=%d\n", (int)msgr.IsValid());
	if (!msgr.IsValid())
		return 1;
	BMessage request(B_GET_PROPERTY);
	request.AddSpecifier(prop);
	// argv[3]: "-nowin" (direct to app) or a window NAME; default index 1
	if (argc > 3 && !strcmp(argv[3], "-nowin")) {
		// no window specifier
	} else if (argc > 3) {
		request.AddSpecifier("Window", argv[3]);
	} else {
		request.AddSpecifier("Window", (int32)1);
	}
	BMessage reply;
	status_t err = msgr.SendMessage(&request, &reply, 5000000LL, 5000000LL);
	printf("send err=%s reply.what='%c%c%c%c'\n", strerror(err),
		(char)(reply.what >> 24), (char)(reply.what >> 16),
		(char)(reply.what >> 8), (char)reply.what);
	BString result;
	if (reply.FindString("result", &result) == B_OK)
		printf("result: %s\n", result.String());
	else if (reply.FindString("error", &result) == B_OK)
		printf("error: %s\n", result.String());
	return 0;
}
