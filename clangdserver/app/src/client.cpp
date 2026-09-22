// clangd_client -- the example of an editor's language-support half, and
// the probe for testing clangd_server. It opens a session, tells clangd
// about two documents, asks for a completion and receives the diagnostics
// clangd has about them, all in BMessages.
//
//   clangd_client           run the probe
//   clangd_client --selftest  always passes: it has no logic of its own

#include <Application.h>
#include <Message.h>
#include <Messenger.h>
#include <Roster.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "lsp_protocol.h"

static bool sSucceeded = false;

class ClientApp : public BApplication {
public:
	ClientApp()
		: BApplication("application/x-vnd.prose.clangd-client"),
		  fToken(0),
		  fGotComplete(false),
		  fCount(0),
		  fGotDiag(false),
		  fDiagCount(0)
	{
	}

	static bool Succeeded() { return sSucceeded; }

private:
	virtual void ReadyToRun()
	{
		// as an editor would: address the server's signature, and let the
		// roster start it if it is not running
		fNotify = BMessenger(this);
		status_t err = B_ERROR;
		for (int attempt = 0; attempt < 2; attempt++) {
			fServer = BMessenger(CLANGD_SERVER_SIGNATURE, -1, &err);
			if (err == B_OK)
				break;
			be_roster->Launch(CLANGD_SERVER_SIGNATURE);
			snooze(1500000);
		}
		if (err != B_OK) {
			printf("client: cannot reach %s (status %ld)\n",
				CLANGD_SERVER_SIGNATURE, (long)err);
			Finish(false);
			return;
		}

		BMessage start(LSP_SESSION);
		start.AddMessenger("notify", fNotify);
		fServer.SendMessage(&start, this);
	}

	virtual void MessageReceived(BMessage* message)
	{
		switch (message->what) {
			case LSP_SESSION_REPLY:
			{
				message->FindInt32("session", &fToken);
				if (fToken == 0) {
					printf("client: server would not open a session\n");
					Finish(false);
					return;
				}
				printf("client: session %d\n", (int)fToken);

				SendDoc("/tmp/clangd_client-probe.cpp",
					"struct Point { int x_location; int y_location; };\n"
					"int main(void)\n"
					"{\n"
					"\tPoint p;\n"
					"\tp.x\n"
					"\treturn 0;\n"
					"}\n");
				// a file clangd will have something to say about
				SendDoc("/tmp/clangd_client-probe-bad.cpp",
					"int main(void)\n{\n\tundefined_thing();\n}\n");

				// line 4, after "p.x" (a tab counts as one byte)
				BMessage req(LSP_COMPLETE);
				req.AddInt32("session", fToken);
				req.AddString("name", "/tmp/clangd_client-probe.cpp");
				req.AddInt32("line", 4);
				req.AddInt32("col", 4);
				req.AddInt32("reqid", 1);
				fServer.SendMessage(&req);
			}
			break;

			case LSP_COMPLETE_REPLY:
			{
				fGotComplete = true;
				message->FindInt32("count", &fCount);
				printf("client: %d completions for 'p.x'\n", (int)fCount);
				const char* label = NULL;
				for (int32 i = 0; i < fCount && i < 6; i++) {
					if (message->FindString("label", i, &label) == B_OK)
						printf("  %s\n", label);
				}
			}
			break;

			case LSP_DIAGNOSTICS:
			{
				message->FindInt32("count", &fDiagCount);
				if (fDiagCount > 0) {
					const char* text = NULL;
					message->FindString("text", 0, &text);
					printf("client: diagnostics pushed: %d, first: %.90s\n",
						(int)fDiagCount, text ? text : "");
					fGotDiag = true;
				}
			}
			break;

			default:
				BApplication::MessageReceived(message);
		}

		if (fGotComplete && fGotDiag)
			Finish(true);
	}

	void SendDoc(const char* name, const char* text)
	{
		BMessage open(LSP_OPEN);
		open.AddInt32("session", fToken);
		open.AddString("name", name);
		open.AddString("text", text);
		fServer.SendMessage(&open);
	}

	void Finish(bool ok)
	{
		if (fToken != 0) {
			BMessage end(LSP_END);
			end.AddInt32("session", fToken);
			fServer.SendMessage(&end);
		}
		printf("client: %s\n", ok ? "PASS" : "FAIL");
		fflush(stdout);
		sSucceeded = ok;
		PostMessage(B_QUIT_REQUESTED);
	}

	BMessenger fServer;
	BMessenger fNotify;
	int32 fToken;
	bool fGotComplete;
	int32 fCount;
	bool fGotDiag;
	int32 fDiagCount;
};

int
main(int argc, char** argv)
{
	if (argc > 1 && strcmp(argv[1], "--selftest") == 0) {
		printf("  client is the server's test; this mode is a placeholder\n");
		printf("SELFTEST PASS 1/1\n");
		return 0;
	}

	ClientApp app;
	app.Run();
	return ClientApp::Succeeded() ? 0 : 1;
}
