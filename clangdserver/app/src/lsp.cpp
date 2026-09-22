#include "lsp.h"

#include <Autolock.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>

#include <kernel/OS.h>

// how big one JSON-RPC frame may be before the reader refuses it
static const size_t kMaxFrame = 16u * 1024 * 1024;

LspSession::LspSession()
	: fStdin(-1),
	  fStdout(-1),
	  fChild(-1),
	  fReader(-1),
	  fReaderGone(false),
	  fNextId(0),
	  fNotify(NULL),
	  fCookie(NULL)
{
}

LspSession::~LspSession()
{
	if (fChild > 0) {
		kill(fChild, SIGKILL);
		wait_for_thread(fReader, NULL);
		waitpid(fChild, NULL, 0);
	}
	if (fStdin >= 0) close(fStdin);
	if (fStdout >= 0) close(fStdout);
}

status_t
LspSession::Start(const char* clangdPath)
{
	int inPipe[2], outPipe[2];
	if (pipe(inPipe) != 0 || pipe(outPipe) != 0)
		return B_ERROR;

	// fork(), not vfork(): vfork() here is the bare system call, without the
	// heap's fork hooks, and this process has other threads. If one of them
	// held a malloc lock at that instant, the child -- whose execv()
	// allocates -- would wait for it for ever, holding the pipes open.
	pid_t child = fork();
	if (child < 0) {
		close(inPipe[0]); close(inPipe[1]);
		close(outPipe[0]); close(outPipe[1]);
		return B_ERROR;
	}

	if (child == 0) {
		// the child: clangd reads JSON-RPC from stdin and writes it to stdout
		close(STDIN_FILENO);
		dup(inPipe[0]);
		close(inPipe[1]);
		close(STDOUT_FILENO);
		dup(outPipe[1]);
		close(outPipe[0]);

		// clangd is chatty on stderr by default; the server keeps it quiet
		int nullfd = open("/dev/null", O_WRONLY);
		if (nullfd >= 0) { close(STDERR_FILENO); dup(nullfd); }

		char argLog[] = "--log=error";
		char argIndex[] = "--background-index=false";
		// A question that arrives before clangd has parsed the file -- a
		// document just opened, the first question of a session -- is
		// otherwise answered in its "fallback" mode: words from the text and
		// keywords, no meaning at all, so after "p." it offers "struct". Wait
		// for the parse instead: the answer is a moment later and right.
		char argParse[] = "--completion-parse=always";
		// completions are names, not edits to the file's #include lines
		char argHeaders[] = "--header-insertion=never";
		char* argv[] = { (char*)"clangd", argIndex, argParse, argHeaders,
			argLog, NULL };
		execv(clangdPath, argv);

		// _exit, not exit: we are a fork of a program with other threads and
		// live loopers; exit() would run their destructors here
		_exit(86);
	}

	close(inPipe[0]);
	close(outPipe[1]);
	fStdin = inPipe[1];
	fStdout = outPipe[0];
	fChild = child;
	fReaderGone = false;

	fReader = spawn_thread(ReaderEntry, "clangd reader", B_NORMAL_PRIORITY,
		this);
	if (fReader < 0)
		return B_ERROR;
	resume_thread(fReader);

	// the handshake every LSP server wants before anything else
	JVal caps;
	caps.Set("textDocument", JVal()
		.Set("completion", JVal())
		.Set("definition", JVal())
		.Set("publishDiagnostics", JVal()));
	JVal params;
	params.Set("processId", JVal((double)getpid()));
	params.Set("rootUri", JVal());
	params.Set("capabilities", caps);
	params.Set("clientInfo", JVal()
		.Set("name", JVal("clangd_server")));

	JVal reply;
	status_t err = Request("initialize", params, reply, 20000000);
	if (err == B_OK)
		Notify("initialized", JVal());
	return err;
}

// ------------------------------------------------------------------ output --

status_t
LspSession::Send(const BString& body)
{
	BAutolock lock(fLock);
	if (fStdin < 0) return B_ERROR;

	BString head;
	head << "Content-Length: " << (int64)body.Length() << "\r\n\r\n";

	const char* data = head.String();
	size_t left = head.Length();
	while (left > 0) {
		ssize_t n = write(fStdin, data, left);
		if (n <= 0) return B_ERROR;
		data += n;
		left -= n;
	}
	data = body.String();
	left = body.Length();
	while (left > 0) {
		ssize_t n = write(fStdin, data, left);
		if (n <= 0) return B_ERROR;
		data += n;
		left -= n;
	}
	return B_OK;
}

void
LspSession::Notify(const char* method, const JVal& params)
{
	JVal msg;
	msg.Set("jsonrpc", JVal("2.0"));
	msg.Set("method", JVal(method));
	msg.Set("params", params);
	if (Send(msg.ToJson()) != B_OK)
		fprintf(stderr, "clangd_server: %s: write to clangd failed\n",
			method);
}

status_t
LspSession::Request(const char* method, const JVal& params, JVal& reply,
	bigtime_t timeout)
{
	int64 id;
	{
		BAutolock lock(fLock);
		id = ++fNextId;
	}
	JVal msg;
	msg.Set("jsonrpc", JVal("2.0"));
	msg.Set("id", JVal((double)id));
	msg.Set("method", JVal(method));
	msg.Set("params", params);
	status_t err = Send(msg.ToJson());
	if (err != B_OK) return err;

	bigtime_t deadline = system_time() + timeout;
	for (;;) {
		{
			BAutolock lock(fLock);
			for (size_t i = 0; i < fReplies.size(); i++) {
				if (fReplies[i].id == id) {
					reply = fReplies[i].value;
					bool wasError = fReplies[i].error;
					fReplies.erase(fReplies.begin() + i);
					return wasError ? B_ERROR : B_OK;
				}
			}
			if (fReaderGone)
				return B_ERROR;
		}
		if (system_time() >= deadline)
			return B_TIMED_OUT;
		snooze(20000);
	}
}

// ------------------------------------------------------------------- input --

int32
LspSession::ReaderEntry(void* arg)
{
	return ((LspSession*)arg)->ReaderLoop();
}

int32
LspSession::ReaderLoop()
{
	char buf[16384];
	for (;;) {
		ssize_t n = read(fStdout, buf, sizeof(buf));
		if (n <= 0)
			break;
		fInBuf.Append(buf, n);
		if (fInBuf.Length() > (int32)kMaxFrame) {
			fprintf(stderr, "clangd_server: frame over %u bytes dropped\n",
				(unsigned)kMaxFrame);
			break;
		}
		PumpReader();
	}

	BAutolock lock(fLock);
	fReaderGone = true;
	return 0;
}

// cuts complete frames out of the accumulator and hands each to the session
void
LspSession::PumpReader()
{
	for (;;) {
		int32 headEnd = fInBuf.FindFirst("\r\n\r\n");
		if (headEnd < 0) {
			// the whole header has not arrived yet
			if (fInBuf.Length() > 16384) return;	// and never will
			return;
		}

		// "Content-Length: NNN" -- clangd sends exactly this one field
		int32 lineEnd = fInBuf.FindFirst("\r\n");
		BString head;
		fInBuf.CopyInto(head, 0, lineEnd);
		int64 length = -1;
		if (strncasecmp(head.String(), "Content-Length:", 15) == 0)
			length = atoll(head.String() + 15);
		if (length < 0 || length > (int64)kMaxFrame) {
			fprintf(stderr, "clangd_server: bad frame header '%s'\n",
				head.String());
			fInBuf = "";
			return;
		}

		int32 bodyStart = headEnd + 4;
		if (fInBuf.Length() < bodyStart + (int32)length)
			return;		// the body has not arrived in full yet

		BString body;
		fInBuf.CopyInto(body, bodyStart, (int32)length);
		fInBuf.Remove(0, bodyStart + (int32)length);
		HandleDocument(body.String(), body.Length());
	}
}

void
LspSession::HandleDocument(const char* body, size_t length)
{
	JVal msg;
	BString err;
	if (!JParse(body, length, msg, &err)) {
		fprintf(stderr, "clangd_server: unreadable JSON from clangd: %s\n",
			err.String());
		return;
	}
	Deliver(msg);
}

void
LspSession::Deliver(const JVal& msg)
{
	// a response carries the id of a request we made
	if (msg.Has("id") && (msg.Has("result") || msg.Has("error"))) {
		Reply r;
		r.id = msg.Get("id").AsInt();
		r.error = msg.Has("error");
		r.value = r.error ? msg.Get("error") : msg.Get("result");
		BAutolock lock(fLock);
		fReplies.push_back(r);
		return;
	}

	// a notification from the server
	if (msg.Has("method") && fNotify != NULL)
		fNotify(msg, fCookie);
}

// --------------------------------------------------------------- documents --

static JVal
VersionedText(const char* uri, int32 version, const char* text)
{
	return JVal()
		.Set("uri", JVal(uri))
		.Set("languageId", JVal("cpp"))
		.Set("version", JVal((double)version))
		.Set("text", JVal(text));
}

void
LspSession::DidOpen(const char* path, const char* mime, const char* text)
{
	(void)mime;	// clangd picks the language from the file name
	JVal doc = VersionedText(FileUriOf(path).String(), 1, text);
	Notify("textDocument/didOpen", JVal().Set("textDocument", doc));
}

void
LspSession::DidChange(const char* path, const char* text)
{
	JVal doc = JVal()
		.Set("uri", JVal(FileUriOf(path).String()))
		.Set("version", JVal((double)2));
	JVal change = JVal().Set("text", JVal(text));
	JVal event = JVal().Set("textDocument", doc);
	event.Set("contentChanges", JVal().Add(change));
	Notify("textDocument/didChange", event);
}

void
LspSession::DidClose(const char* path)
{
	Notify("textDocument/didClose",
		JVal().Set("textDocument",
			JVal().Set("uri", JVal(FileUriOf(path).String()))));
}

// ----------------------------------------------------------------- columns --

int32
LspSession::Utf16FromByte(const char* s, int32 byteCol)
{
	int32 units = 0, i = 0;
	while (s[i] != 0 && i < byteCol) {
		unsigned char c = s[i];
		if (c < 0x80) i += 1;
		else if ((c & 0xE0) == 0xC0) i += 2;
		else if ((c & 0xF0) == 0xE0) i += 3;
		else if ((c & 0xF8) == 0xF0) { i += 4; units++; }	// surrogate pair
		else i += 1;	// a stray continuation byte counts as itself
		if (i > byteCol) break;		// the column lands inside a character
		units++;
	}
	return units;
}

int32
LspSession::ByteFromUtf16(const char* s, int32 utf16Col)
{
	int32 units = 0, i = 0;
	while (s[i] != 0 && units < utf16Col) {
		unsigned char c = s[i];
		int32 bytes = 1;
		if (c < 0x80) bytes = 1;
		else if ((c & 0xE0) == 0xC0) bytes = 2;
		else if ((c & 0xF0) == 0xE0) bytes = 3;
		else if ((c & 0xF8) == 0xF0) { bytes = 4; units++; }
		i += bytes;
		units++;
	}
	return i;
}

// -------------------------------------------------------------------- URIs --

static bool
UriSafe(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
		|| (c >= '0' && c <= '9') || c == '/' || c == '-' || c == '_'
		|| c == '.' || c == '~' || c == '+' || c == ':';
}

BString
LspSession::FileUriOf(const char* path)
{
	BString uri("file://");
	for (const char* p = path; *p; p++) {
		if (UriSafe(*p))
			uri += *p;
		else {
			char buf[4];
			snprintf(buf, sizeof(buf), "%%%02X", (unsigned char)*p);
			uri += buf;
		}
	}
	return uri;
}

bool
LspSession::PathOfUri(const BString& uri, BString& path)
{
	if (uri.FindFirst("file://") != 0)
		return false;
	BString out;
	const char* p = uri.String() + 7;
	while (*p) {
		if (*p == '%' && p[1] != 0 && p[2] != 0) {
			char hex[3] = { p[1], p[2], 0 };
			out += (char)strtol(hex, NULL, 16);
			p += 3;
		} else {
			out += *p;
			p++;
		}
	}
	path = out;
	return true;
}
