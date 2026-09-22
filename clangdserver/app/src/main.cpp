// clangd_server -- the BeOS shape around clangd.
//
// clangd speaks the Language Server Protocol as JSON-RPC over its own stdin
// and stdout, a POSIX pipe contract an editor cannot be talked to in. This
// server owns that: one clangd child per client, the framing and the JSON
// all inside. An editor speaks to it in BMessages, which is how BeOS
// programs talk to servers -- the roster starts this application on demand
// when the first editor addresses its signature, and it stays up.
//
// The protocol. A session is opened first and answered with a token;
// every later message of that editor carries it. There are no synchronous
// replies to wait for: results and diagnostics are pushed to the "notify"
// messenger the editor named when it opened the session, each answer
// echoing the "reqid" the editor put in its request -- an editor sends a
// request from its looper and handles the answer when its looper gets it.
// Positions are 0-based; lines and columns count bytes of the exact text
// the editor sent. (Definition replies are the one exception: their column
// counts UTF-16 units in the target file, which the editor usually has not
// sent; lines are the same either way.)
//
//   sent by the editor                      answered, pushed to notify
//   LSP_SESSION 'LSes'  notify (messenger)  'LSsr': session (int32) --
//                                            the one synchronous reply
//   LSP_OPEN     'LOpn'  session, name, text      -
//   LSP_CHANGE   'LChg'  session, name, text      -
//   LSP_CLOSE    'LCls'  session, name            -
//   LSP_COMPLETE 'LCmp'  session, name, line,
//                        col, reqid          'LCmr': reqid, count,
//                                                label#i, kind#i, detail#i
//   LSP_DEFINITION 'LDef' session, name, line,
//                        col, reqid          'LDfr': reqid, found, path,
//                                                line, col
//   LSP_END      'LEnd'  session                 -
//
//   pushed to the session's notify messenger
//   LSP_DIAGNOSTICS 'LDgn' name, count, line#i, col#i, sev#i, text#i
//
// Modes:
//   clangd_server              run as the server
//   clangd_server --selftest   the codec and arithmetic, no clangd needed
//   clangd_server --selftest-live   a whole session against a real clangd
//   clangd_server --client-probe    talk to the running server, as an editor

#include <Application.h>
#include <Autolock.h>
#include <Locker.h>
#include <Message.h>
#include <Messenger.h>
#include <Roster.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <vector>

#include "json.h"
#include "lsp.h"
#include "lsp_protocol.h"

#define MAX_COMPLETIONS 200

// clangd pads a member's label with a leading space to line it up with
// scoped names -- and the pad is sometimes a non-breaking space, which
// BString::Trim() leaves alone. An editor wants the name and nothing else.
static void
CleanLabel(BString& label)
{
	while (label.Length() > 0) {
		char c = label.ByteAt(0);
		if (c > ' ' && (unsigned char)c < 0x80)
			break;
		label.Remove(0, 1);
	}
}

// ------------------------------------------------------------------ selftest --

struct Case { const char* name; bool ok; };

static int
FinishCases(const Case* first, const Case* end)
{
	int passed = 0, count = (int)(end - first);
	bool all = true;
	for (int i = 0; i < count; i++) {
		printf("  %-46s %s\n", first[i].name, first[i].ok ? "PASS" : "FAIL");
		if (first[i].ok) passed++; else all = false;
	}
	printf("SELFTEST %s %d/%d\n", all ? "PASS" : "FAIL", passed, count);
	return all ? 0 : 1;
}

static int
SelfTest()
{
	Case cases[32];
	Case* c = cases;

	{	// JSON: objects, arrays, nesting, strings with escapes
		const char* src = "{\"id\":7,\"result\":[1,2.5,\"a\\nb\",true,null,"
			"{\"k\\u00e9\":\"\\u65e5\"}],\"err\":{\"x\":-3}}";
		JVal v;
		BString err;
		bool ok = JParse(src, strlen(src), v, &err);
		ok = ok && v.Get("id").AsInt() == 7
			&& v.Get("result").Count() == 6
			&& v.Get("result").At(1).AsNumber() == 2.5
			&& v.Get("result").At(2).AsString() == "a\nb"
			&& v.Get("result").At(3).AsBool()
			&& v.Get("result").At(4).IsNull()
			&& v.Get("result").At(5).Get("k\303\251").AsString()
				== "\346\227\245"
			&& v.Get("err").Get("x").AsInt() == -3;
		*c++ = (Case){ "json: parses and walks a reply", ok };
	}
	{	// JSON: writes what it read, and reads that back
		JVal v;
		v.Set("name", JVal("cla\"ng\\d\n"))
		 .Set("n", JVal((double)-12))
		 .Set("list", JVal().Add(JVal(true)).Add(JVal()));
		BString out = v.ToJson();
		JVal back;
		bool ok = JParse(out.String(), out.Length(), back)
			&& back.Get("name").AsString() == "cla\"ng\\d\n"
			&& back.Get("n").AsInt() == -12
			&& back.Get("list").Count() == 2
			&& back.Get("list").At(0).AsBool();
		if (!ok)
			printf("  (round trip wrote: %s)\n", out.String());
		*c++ = (Case){ "json: round trip through its own writer", ok };
	}
	{	// JSON: refuses broken documents
		const char* bad[] = { "{\"a\":}", "[1,2", "\"unterminated",
			"{\"a\" bad}", "nul", "" };
		bool ok = true;
		for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
			JVal v;
			if (JParse(bad[i], strlen(bad[i]), v))
				ok = false;
		}
		*c++ = (Case){ "json: rejects broken documents", ok };
	}
	{	// columns: LSP counts UTF-16 units, editors count bytes.
		// "a" + e-acute (2 bytes) + three CJK (3 bytes each) + one
		// supplementary character (4 bytes, two UTF-16 units):
		// 16 bytes, 7 UTF-16 units.
		const char* s =
			"a\303\251\346\227\245\346\234\254\350\252\236\360\235\204\236";
		bool ok = strlen(s) == 16
			&& LspSession::Utf16FromByte(s, strlen(s)) == 7
			&& LspSession::Utf16FromByte(s, 1) == 1
			&& LspSession::Utf16FromByte(s, 3) == 2
			&& LspSession::Utf16FromByte(s, 6) == 3
			&& LspSession::ByteFromUtf16(s, 7) == (int32)strlen(s)
			&& LspSession::ByteFromUtf16(s, 2) == 3
			&& LspSession::ByteFromUtf16(s, 4) == 9;
		*c++ = (Case){ "columns: UTF-16 and byte columns convert", ok };
	}
	{	// URIs: spaces and percents survive the trip
		BString path("/boot/home/my file%2.cpp");
		BString uri = LspSession::FileUriOf(path.String());
		BString back;
		bool ok = LspSession::PathOfUri(uri, back)
			&& back == path
			&& uri.FindFirst("%20") >= 0;
		*c++ = (Case){ "uris: paths and file:// round trip", ok };
	}

	return FinishCases(cases, c);
}

// ------------------------------------------------------------ live selftest --

static int32 gDiagCount = -1;

static void
LiveNotify(const JVal& notification, void* cookie)
{
	(void)cookie;
	if (notification.Get("method").AsString()
			!= "textDocument/publishDiagnostics")
		return;
	int32 n = notification.Get("params").Get("diagnostics").Count();
	if (n > gDiagCount)
		gDiagCount = n;
}

static int
SelfTestLive(const char* clangdPath)
{
	Case cases[8];
	Case* c = cases;

	LspSession lsp;
	lsp.SetNotifier(LiveNotify, NULL);
	status_t err = lsp.Start(clangdPath);
	*c++ = (Case){ "session: clangd answered initialize", err == B_OK };
	if (err != B_OK) {
		printf("  (clangd did not start; is it on this machine?)\n");
		return FinishCases(cases, c);
	}

	const char* path = "/tmp/clangd_server-selftest.cpp";
	const char* text =
		"struct Point {\n"
		"	int x_location;\n"
		"	int y_location;\n"
		"};\n"
		"int main(void)\n"
		"{\n"
		"	Point p;\n"
		"	p.x\n"
		"	return 0;\n"
		"}\n";
	lsp.DidOpen(path, "text/x-c++src", text);

	// line 7, column 4: just after "p.x"
	JVal params;
	params.Set("textDocument", JVal().Set("uri",
		JVal(LspSession::FileUriOf(path).String())));
	params.Set("position", JVal()
		.Set("line", JVal((double)7))
		.Set("character", JVal((double)LspSession::Utf16FromByte(
			"	p.x", 4))));
	JVal reply;
	bool gotCompletion = false;
	status_t rerr = lsp.Request("textDocument/completion", params, reply,
		60000000);
	if (rerr == B_OK) {
		const JVal& items = reply.Has("items")
			? reply.Get("items") : reply;
		bool hasX = false;
		for (int i = 0; i < items.Count(); i++) {
			BString label = items.At(i).Get("label").AsString();
			CleanLabel(label);
			if (label == "x_location") hasX = true;
		}
		gotCompletion = hasX;
	} else
		printf("  (completion request status 0x%lx)\n", (long)rerr);
	*c++ = (Case){ "session: completes a member after 'p.x'", gotCompletion };

	// a file clangd must complain about, and the complaint delivered
	const char* badPath = "/tmp/clangd_server-selftest-bad.cpp";
	lsp.DidOpen(badPath, "text/x-c++src",
		"int main(void)\n{\n\tundefined_thing();\n}\n");
	for (int i = 0; i < 50 && gDiagCount <= 0; i++)
		snooze(100000);
	*c++ = (Case){ "session: diagnostics pushed for a broken file",
		gDiagCount > 0 };

	return FinishCases(cases, c);
}

// -------------------------------------------------------------------- server --

static BString
ClangdPath()
{
	// where a package puts clangd, and where a loose copy waits its turn
	const char* dirs[] = {
		"/boot/system/non-packaged/bin", "/boot/system/bin" };
	for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
		BString p(dirs[i]);
		p += "/clangd";
		if (access(p.String(), X_OK) == 0)
			return p;
	}
	return BString();
}

struct EditorSession {
	int32 token;
	BMessenger notify;		// where diagnostics are pushed
	LspSession* lsp;
	BLocker docLock;
	std::vector<BString*> docNames;	// the texts we were told, for positions
	std::vector<BString*> docTexts;
};

class ServerApp : public BApplication {
public:
	ServerApp()
		: BApplication(CLANGD_SERVER_SIGNATURE),
		  fNextToken(0)
	{
	}

	virtual void MessageReceived(BMessage* message)
	{
		switch (message->what) {
			case LSP_SESSION:
			case LSP_OPEN:
			case LSP_CHANGE:
			case LSP_CLOSE:
			case LSP_COMPLETE:
			case LSP_DEFINITION:
			case LSP_END:
				Handle(message);
			break;

			default:
				BApplication::MessageReceived(message);
		}
	}

	virtual bool QuitRequested()
	{
		for (size_t i = 0; i < fSessions.size(); i++) {
			for (size_t k = 0; k < fSessions[i]->docNames.size(); k++) {
				delete fSessions[i]->docNames[k];
				delete fSessions[i]->docTexts[k];
			}
			delete fSessions[i]->lsp;
			delete fSessions[i];
		}
		fSessions.clear();
		return true;
	}

private:
	std::vector<EditorSession*> fSessions;
	int32 fNextToken;

	EditorSession* SessionByToken(int32 token)
	{
		for (size_t i = 0; i < fSessions.size(); i++)
			if (fSessions[i]->token == token)
				return fSessions[i];
		return NULL;
	}

	EditorSession* NewSession(BMessage* message)
	{
		BString clangdPath = ClangdPath();
		if (clangdPath.Length() == 0) {
			fprintf(stderr, "clangd_server: no clangd on this machine\n");
			return NULL;
		}

		EditorSession* s = new EditorSession();
		s->lsp = new LspSession();
		s->token = atomic_add(&fNextToken, 1) + 1;
		message->FindMessenger("notify", &s->notify);
		s->lsp->SetNotifier(SessionNotify, s);
		if (s->lsp->Start(clangdPath.String()) != B_OK) {
			fprintf(stderr, "clangd_server: clangd would not start\n");
			delete s->lsp;
			delete s;
			return NULL;
		}
		fSessions.push_back(s);
		printf("clangd_server: session %d opened (%d editors)\n",
			(int)s->token, (int)fSessions.size());
		return s;
	}

	// the reader thread of one session; publishes the editor's document
	static void SessionNotify(const JVal& notification, void* cookie)
	{
		EditorSession* s = (EditorSession*)cookie;
		if (notification.Get("method").AsString()
				!= "textDocument/publishDiagnostics")
			return;

		const JVal& p = notification.Get("params");
		BString path;
		if (!LspSession::PathOfUri(p.Get("uri").AsString(), path))
			return;

		const JVal& items = p.Get("diagnostics");
		BMessage diag(LSP_DIAGNOSTICS);
		diag.AddString("name", path);
		diag.AddInt32("count", items.Count());
		for (int i = 0; i < items.Count() && i < 200; i++) {
			const JVal& d = items.At(i);
			int32 line = (int32)d.Get("range").Get("start").Get("line").AsInt();
			int32 col16 = (int32)d.Get("range").Get("start").Get("character")
				.AsInt();
			// back to bytes, against the text the editor sent us
			BString lineText;
			LineOf(s, path.String(), line, lineText);
			diag.AddInt32("line", line);
			diag.AddInt32("col",
				LspSession::ByteFromUtf16(lineText.String(), col16));
			diag.AddInt32("sev", (int32)d.Get("severity").AsInt());
			diag.AddString("text", d.Get("message").AsString());
		}
		s->notify.SendMessage(&diag);
	}

	static void LineOf(EditorSession* s, const char* name, int32 line,
		BString& out)
	{
		BAutolock lock(s->docLock);
		for (size_t i = 0; i < s->docNames.size(); i++) {
			if (*(s->docNames[i]) == name) {
				const BString& text = *(s->docTexts[i]);
				int32 pos = 0;
				for (int32 l = 0; l < line; l++) {
					int32 nl = text.FindFirst('\n', pos);
					if (nl < 0) { out = ""; return; }
					pos = nl + 1;
				}
				int32 end = text.FindFirst('\n', pos);
				text.CopyInto(out, pos,
					end < 0 ? text.Length() - pos : end - pos);
				return;
			}
		}
		out = "";
	}

	void RememberText(EditorSession* s, const char* name, const char* text)
	{
		BAutolock lock(s->docLock);
		for (size_t i = 0; i < s->docNames.size(); i++)
			if (*(s->docNames[i]) == name) {
				*(s->docTexts[i]) = text;
				return;
			}
		s->docNames.push_back(new BString(name));
		s->docTexts.push_back(new BString(text));
	}

	void ForgetText(EditorSession* s, const char* name)
	{
		BAutolock lock(s->docLock);
		for (size_t i = 0; i < s->docNames.size(); i++)
			if (*(s->docNames[i]) == name) {
				delete s->docNames[i];
				delete s->docTexts[i];
				s->docNames.erase(s->docNames.begin() + i);
				s->docTexts.erase(s->docTexts.begin() + i);
				return;
			}
	}

	// Requests run on their own thread: the server's looper keeps answering
	// every editor while clangd thinks. The answer is pushed to the
	// session's notify messenger with the editor's own reqid.
	struct Work {
		EditorSession* session;
		uint32 what;
		int32 reqid;
		BString name;
		int32 line;
		int32 col;
	};

	static int32 WorkerEntry(void* arg)
	{
		Work* w = (Work*)arg;
		EditorSession* s = w->session;

		BString lineText;
		LineOf(s, w->name.String(), w->line, lineText);

		JVal params;
		params.Set("textDocument", JVal().Set("uri",
			JVal(LspSession::FileUriOf(w->name.String()).String())));
		params.Set("position", JVal()
			.Set("line", JVal((double)w->line))
			.Set("character", JVal((double)LspSession::Utf16FromByte(
				lineText.String(), w->col))));

		if (w->what == LSP_COMPLETE) {
			BMessage out(LSP_COMPLETE_REPLY);
			out.AddInt32("reqid", w->reqid);
			JVal reply;
			status_t rerr = s->lsp->Request("textDocument/completion",
				params, reply, 90000000);
			int n = 0;
			if (rerr == B_OK) {
				const JVal& items = reply.Has("items")
					? reply.Get("items") : reply;
				n = items.Count();
				if (n > MAX_COMPLETIONS) n = MAX_COMPLETIONS;
				out.AddInt32("count", n);
				for (int i = 0; i < n; i++) {
					const JVal& it = items.At(i);
					BString label = it.Get("label").AsString();
					CleanLabel(label);
					out.AddString("label", label);
					out.AddInt32("kind", (int32)it.Get("kind").AsInt());
					out.AddString("detail", it.Get("detail").AsString());
				}
			} else
				out.AddInt32("count", 0);
			s->notify.SendMessage(&out);
		} else if (w->what == LSP_DEFINITION) {
			BMessage out(LSP_DEFINITION_REPLY);
			out.AddInt32("reqid", w->reqid);
			JVal reply;
			if (s->lsp->Request("textDocument/definition", params, reply,
					30000000) == B_OK && reply.Count() > 0) {
				const JVal& loc = reply.At(0);
				BString path;
				out.AddBool("found",
					LspSession::PathOfUri(loc.Get("uri").AsString(), path));
				out.AddString("path", path);
				out.AddInt32("line",
					(int32)loc.Get("range").Get("start").Get("line").AsInt());
				out.AddInt32("col",
					(int32)loc.Get("range").Get("start").Get("character")
					.AsInt());
			} else
				out.AddBool("found", false);
			s->notify.SendMessage(&out);
		}

		delete w;
		return 0;
	}

	void Handle(BMessage* message)
	{
		if (message->what == LSP_SESSION) {
			EditorSession* s = NewSession(message);
			BMessage out(LSP_SESSION_REPLY);
			out.AddInt32("session", s ? s->token : 0);
			message->SendReply(&out);
			return;
		}

		int32 token = 0;
		message->FindInt32("session", &token);
		EditorSession* s = SessionByToken(token);
		if (s == NULL) {
			fprintf(stderr, "clangd_server: message 0x%lx for unknown session"
				" %d\n", (long)message->what, (int)token);
			return;
		}

		const char* name = NULL, *text = NULL;
		message->FindString("name", &name);

		switch (message->what) {
			case LSP_OPEN:
				if (name && message->FindString("text", &text) == B_OK) {
					s->lsp->DidOpen(name, "text/x-c++src", text);
					RememberText(s, name, text);
				}
			break;

			case LSP_CHANGE:
				if (name && message->FindString("text", &text) == B_OK) {
					s->lsp->DidChange(name, text);
					RememberText(s, name, text);
				}
			break;

			case LSP_CLOSE:
				if (name) {
					s->lsp->DidClose(name);
					ForgetText(s, name);
				}
			break;

			case LSP_END:
			{
				for (size_t i = 0; i < fSessions.size(); i++)
					if (fSessions[i] == s) {
						for (size_t k = 0; k < s->docNames.size(); k++) {
							delete s->docNames[k];
							delete s->docTexts[k];
						}
						delete s->lsp;
						delete s;
						fSessions.erase(fSessions.begin() + i);
						break;
					}
				printf("clangd_server: session %d closed (%d editors)\n",
					(int)token, (int)fSessions.size());
			}
			break;

			case LSP_COMPLETE:
			case LSP_DEFINITION:
			{
				Work* w = new Work();
				w->session = s;
				w->what = message->what;
				w->name = name ? name : "";
				w->reqid = 0;
				message->FindInt32("reqid", &w->reqid);
				message->FindInt32("line", &w->line);
				message->FindInt32("col", &w->col);
				thread_id t = spawn_thread(WorkerEntry, "lsp request",
					B_NORMAL_PRIORITY, w);
				if (t >= 0)
					resume_thread(t);
				else
					delete w;
			}
			break;
		}
	}
};

// ------------------------------------------------------------ client probe --

// ---------------------------------------------------------------------- main --

int
main(int argc, char** argv)
{
	// a service writes to wherever it was started from; when that is a
	// file, its reader should not wait for a buffer to fill
	setvbuf(stdout, NULL, _IOLBF, 0);

	if (argc > 1) {
		if (strcmp(argv[1], "--selftest") == 0)
			return SelfTest();
		if (strcmp(argv[1], "--selftest-live") == 0) {
			BString path = ClangdPath();
			if (path.Length() == 0) {
				printf("  no clangd on this machine\n");
				printf("SELFTEST FAIL 0/1\n");
				return 1;
			}
			return SelfTestLive(path.String());
		}
	}

	ServerApp app;
	app.Run();
	return 0;
}
