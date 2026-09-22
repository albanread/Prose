// One clangd child process: the stdio framing, the request bookkeeping and
// the notification dispatch. Kept clear of BApplication on purpose -- the
// server application is a thin shell of BMessages around this, and the
// selftest drives it directly.
#ifndef PROSE_LSP_H
#define PROSE_LSP_H

#include <OS.h>
#include <Locker.h>
#include <String.h>
#include <SupportDefs.h>

#include "json.h"

#include <vector>

class LspSession {
public:
	// called on the reader thread for every server notification
	// (publishDiagnostics and friends)
	typedef void (*NotifyFn)(const JVal& notification, void* cookie);

	LspSession();
	~LspSession();

	status_t Start(const char* clangdPath);
	bool IsAlive() const { return fChild > 0; }

	void SetNotifier(NotifyFn fn, void* cookie) { fNotify = fn; fCookie = cookie; }

	// a request with an id; waits for the answer, `timeout` in microseconds.
	// B_OK: `reply` holds the result object. B_TIMED_OUT, B_ERROR: no answer.
	status_t Request(const char* method, const JVal& params, JVal& reply,
		bigtime_t timeout);
	// a notification: sent, never answered
	void Notify(const char* method, const JVal& params);

	void DidOpen(const char* path, const char* mime, const char* text);
	void DidChange(const char* path, const char* text);
	void DidClose(const char* path);

	// LSP measures a column in UTF-16 code units and an editor in bytes;
	// these walk one line's text both ways. A column inside a multi-byte
	// character snaps to its first byte.
	static int32 Utf16FromByte(const char* lineText, int32 byteCol);
	static int32 ByteFromUtf16(const char* lineText, int32 utf16Col);

	static BString FileUriOf(const char* path);
	static bool PathOfUri(const BString& uri, BString& path);

private:
	struct Reply {
		int64 id;
		bool error;
		JVal value;		// the "result", or the error object
	};

	status_t Send(const BString& body);
	void PumpReader();
	void HandleDocument(const char* body, size_t length);
	void Deliver(const JVal& message);

	static int32 ReaderEntry(void* arg);
	int32 ReaderLoop();

	int fStdin;
	int fStdout;
	pid_t fChild;
	thread_id fReader;
	bool fReaderGone;

	BLocker fLock;		// guards fReplies, fNextId, fWrite
	int64 fNextId;
	std::vector<Reply> fReplies;
	BString fInBuf;		// the reader's accumulator

	NotifyFn fNotify;
	void* fCookie;
};

#endif	// PROSE_LSP_H
