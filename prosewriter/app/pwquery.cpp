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
	const char* mode = argc > 4 ? argv[4] : "get";
	const char* data = argc > 5 ? argv[5] : NULL;
	BApplication app("application/x-vnd.prose.pwquery");
	BMessenger msgr(sig);
	printf("messenger valid=%d\n", (int)msgr.IsValid());
	if (!msgr.IsValid())
		return 1;
	uint32 what = B_GET_PROPERTY;
	if (!strcasecmp(mode, "set"))
		what = B_SET_PROPERTY;
	else if (!strcasecmp(mode, "do"))
		what = B_EXECUTE_PROPERTY;
	BMessage request(what);
	request.AddSpecifier(prop);
	if (data != NULL)
		request.AddString("data", data);
	// no window specifier: direct to the app
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
