#include "PWWindow.h"

#include <Alert.h>
#include <Application.h>
#include <Clipboard.h>
#include <Entry.h>
#include <Menu.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <Messenger.h>
#include <Path.h>
#include <ScrollView.h>
#include <StringView.h>
#include <UTF8.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "PWPageView.h"

// ---------------------------------------------------------------- helpers --
static BMenuItem*
item(const char* label, uint32 what, char shortcut = 0, uint32 mod = 0)
{
	BMessage* msg = new BMessage(what);
	if (shortcut)
		return new BMenuItem(label, msg, shortcut, mod);
	return new BMenuItem(label, msg);
}

// ----------------------------------------------------------------- window --
PWWindow::PWWindow(BRect frame, const char* title)
	:
	BWindow(frame, title, B_TITLED_WINDOW,
		B_QUIT_ON_WINDOW_CLOSE | B_ASYNCHRONOUS_CONTROLS),
	fLayout(&fDoc)
{
	fView = new PWPageView(&fDoc, &fLayout);
	fLayout.SetPageSetup(PWPageSetup());
	fLayout.Layout();

	fMenuBar = new BMenuBar(Bounds(), "menubar");
	BuildMenus();
	AddChild(fMenuBar);

	BRect client = Bounds();
	client.top = fMenuBar->Bounds().Height() + 1;
	client.bottom -= 20;
	BScrollView* scroll = new BScrollView("scroll", fView, B_FOLLOW_ALL,
		true, true, B_FANCY_BORDER);
	scroll->MoveTo(client.LeftTop());
	scroll->ResizeTo(client.Width(), client.Height());
	AddChild(scroll);

	fStatusView = new BStringView(
		BRect(0, Bounds().bottom - 18, Bounds().right, Bounds().bottom),
		"status", "", B_FOLLOW_LEFT_RIGHT | B_FOLLOW_BOTTOM);
	fStatusView->SetAlignment(B_ALIGN_RIGHT);
	AddChild(fStatusView);

	fOpenPanel = new BFilePanel(B_OPEN_PANEL, new BMessenger(this), NULL,
		B_FILE_NODE, false, new BMessage(OPEN_PANEL_MSG));
	fSavePanel = new BFilePanel(B_SAVE_PANEL, new BMessenger(this), NULL,
		B_FILE_NODE, false, new BMessage(SAVE_PANEL_MSG));

	SetSizeLimits(420, 4000, 380, 4000);
	UpdateStatusText();
	fView->MakeFocus();
}

void
PWWindow::BuildMenus()
{
	BMenu* menu = new BMenu("File");
	menu->AddItem(item("New", 'pWnw', 'N', B_COMMAND_KEY));
	menu->AddItem(new BSeparatorItem());
	menu->AddItem(item("Open" B_UTF8_ELLIPSIS, OPEN_PANEL_MSG, 'O',
		B_COMMAND_KEY));
	menu->AddItem(fSaveItem = item("Save", 'pWsV', 'S', B_COMMAND_KEY));
	menu->AddItem(item("Save as" B_UTF8_ELLIPSIS, SAVE_PANEL_MSG, 'S',
		B_COMMAND_KEY | B_SHIFT_KEY));
	menu->AddSeparatorItem();
	menu->AddItem(item("Close", B_QUIT_REQUESTED, 'W', B_COMMAND_KEY));
	menu->AddItem(item("Quit", B_QUIT_REQUESTED, 'Q', B_COMMAND_KEY));
	// New is a whole new window, handled by the application
	menu->ItemAt(0)->SetTarget(be_app);
	fMenuBar->AddItem(menu);

	menu = new BMenu("Edit");
	fUndoItem = item("Undo", 'pWud', 'Z', B_COMMAND_KEY);
	fRedoItem = item("Redo", 'pWrd', 'Y', B_COMMAND_KEY);
	menu->AddItem(fUndoItem);
	menu->AddItem(fRedoItem);
	menu->AddSeparatorItem();
	menu->AddItem(item("Cut", 'pWct', 'X', B_COMMAND_KEY));
	menu->AddItem(item("Copy", 'pWcp', 'C', B_COMMAND_KEY));
	menu->AddItem(item("Paste", 'pWps', 'V', B_COMMAND_KEY));
	menu->AddSeparatorItem();
	menu->AddItem(item("Select all", B_SELECT_ALL, 'A', B_COMMAND_KEY));
	fMenuBar->AddItem(menu);

	menu = new BMenu("Text");
	menu->AddItem(item("Bold", TEXT_APPLY_MSG, 'B', B_COMMAND_KEY));
	menu->ItemAt(0)->Message()->AddInt32("what-kind", 0);
	menu->AddItem(item("Italic", TEXT_APPLY_MSG, 'I', B_COMMAND_KEY));
	menu->ItemAt(1)->Message()->AddInt32("what-kind", 1);
	menu->AddItem(item("Underline", TEXT_APPLY_MSG, 'U', B_COMMAND_KEY));
	menu->ItemAt(2)->Message()->AddInt32("what-kind", 2);
	menu->AddSeparatorItem();
	menu->AddItem(item("Bigger", TEXT_APPLY_MSG, '=', B_COMMAND_KEY));
	menu->ItemAt(4)->Message()->AddInt32("what-kind", 3);
	menu->AddItem(item("Smaller", TEXT_APPLY_MSG, '-', B_COMMAND_KEY));
	menu->ItemAt(5)->Message()->AddInt32("what-kind", 4);
	fMenuBar->AddItem(menu);

	menu = new BMenu("Help");
	menu->AddItem(item("About ProseWriter", B_ABOUT_REQUESTED));
	fMenuBar->AddItem(menu);
}

void
PWWindow::MenusBeginning()
{
	fUndoItem->SetEnabled(fDoc.CanUndo());
	fRedoItem->SetEnabled(fDoc.CanRedo());
	fSaveItem->SetEnabled(fDoc.IsModified());
}

void
PWWindow::UpdateTitle()
{
	BString title(fFileName.Length() ? fFileName.String() : "Untitled");
	if (fDoc.IsModified())
		title.Prepend("• ");
	SetTitle(title.String());
}

void
PWWindow::UpdateStatusText()
{
	BString text;
	int32 words = 0;
	const char* plain = fDoc.PlainText();
	bool inWord = false;
	for (const char* p = plain; *p; p++) {
		bool word = (*p != ' ' && *p != '\n' && *p != '\t');
		if (word && !inWord) words++;
		inWord = word;
	}
	int32 page = fLayout.PageOfOffset(fView ? fView->CaretOffset() : 0) + 1;
	text.SetToFormat("page %d of %d   ·   %d words", (int)page,
		(int)fLayout.CountPages(), (int)words);
	fStatusView->SetText(text.String());
	UpdateTitle();
}

void
PWWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case DOC_MODIFIED_MSG:
			UpdateStatusText();
			break;
		case OPEN_PANEL_MSG:
			fOpenPanel->Show();
			break;
		case SAVE_PANEL_MSG:
			fSavePanel->Show();
			break;
		case 'pWop': {	// open panel selection
			entry_ref ref;
			if (message->FindRef("refs", &ref) == B_OK)
				OpenFile(ref);
			break;
		}
		case 'pWsv': {	// save panel selection
			entry_ref ref;
			if (message->FindRef("directory", &ref) == B_OK) {
				BPath path(&ref);
				BString name;
				message->FindString("name", &name);
				path.Append(name.String());
				DoSave(BString(path.Path()));
			}
			break;
		}
		case 'pWsV':
			if (fFilePath.Length())
				DoSave(fFilePath);
			else
				fSavePanel->Show();
			break;
		case 'pWud':
			fDoc.Undo();
			fView->Relayout();
			UpdateStatusText();
			break;
		case 'pWrd':
			fDoc.Redo();
			fView->Relayout();
			UpdateStatusText();
			break;
		case 'pWct': {
			if (!fView->HasSelection())
				break;
			be_clipboard->Lock();
			BMessage* clip = be_clipboard->Data();
			clip->MakeEmpty();
			fView->Cut(clip);
			be_clipboard->Commit();
			be_clipboard->Unlock();
			break;
		}
		case 'pWcp': {
			if (!fView->HasSelection())
				break;
			be_clipboard->Lock();
			BMessage* clip = be_clipboard->Data();
			clip->MakeEmpty();
			fView->Copy(clip);
			be_clipboard->Commit();
			be_clipboard->Unlock();
			break;
		}
		case 'pWps':
			if (be_clipboard->Lock()) {
				const BMessage* clip = be_clipboard->Data();
				fView->Paste(clip);
				be_clipboard->Unlock();
			}
			break;
		case TEXT_APPLY_MSG: {
			int32 kind = 0;
			message->FindInt32("what-kind", &kind);
			int32 from, to;
			fView->GetSelection(&from, &to);
			int32 len = to - from;
			if (len <= 0) {
				// no selection: formatting applies to the next typed run —
				// for now the easy path is selecting the caret's word.
				break;
			}
			PWCharFormat fmt = fDoc.FormatAt(from);
			switch (kind) {
				case 0: fmt.bold = !fmt.bold; break;
				case 1: fmt.italic = !fmt.italic; break;
				case 2: fmt.underline = !fmt.underline; break;
				case 3: fmt.size = fmt.size + 1; break;
				case 4: fmt.size = fmt.size > 5 ? fmt.size - 1 : fmt.size; break;
			}
			fDoc.ApplyFormat(from, len, fmt);
			fView->Relayout();
			UpdateStatusText();
			break;
		}
		default:
			BWindow::MessageReceived(message);
	}
}

status_t
PWWindow::OpenFile(const entry_ref& ref)
{
	BPath path(&ref);
	status_t err = fDoc.LoadFromFile(path.Path());
	if (err != B_OK) {
		BAlert* alert = new BAlert("ProseWriter",
			"ProseWriter could not open that file.", "OK");
		alert->Go();
		return err;
	}
	fFilePath = path.Path();
	fFileName = path.Leaf();
	fView->SetCaret(0, false);
	fView->Relayout();
	UpdateStatusText();
	return B_OK;
}

void
PWWindow::DoSave(const BString& pathStr)
{
	if (fDoc.SaveToFile(pathStr.String()) == B_OK) {
		fFilePath = pathStr;
		fFileName = BPath(pathStr.String()).Leaf();
		fDoc.SavedClean();
		UpdateTitle();
	}
}

bool
PWWindow::QuitRequested()
{
	if (fDoc.IsModified()) {
		BAlert* alert = new BAlert("ProseWriter",
			"Save changes before closing?", "Cancel", "Don't save", "Save",
			B_WIDTH_AS_USUAL, B_OFFSET_SPACING, B_WARNING_ALERT);
		int32 choice = alert->Go();
		if (choice == 0)
			return false;
		if (choice == 2) {
			if (fFilePath.Length())
				DoSave(fFilePath);
			else {
				fSavePanel->Show();
				return false;
			}
		}
	}
	be_app_messenger.SendMessage('pWwc');	// window closed
	return true;
}

// -------------------------------------------------------------------- app --
class PWApp : public BApplication {
public:
			PWApp()
				:
				BApplication("application/x-vnd.prose.ProseWriter")
			{
			}

	void	ReadyToRun() override
	{
		BRect frame(80, 60, 860, 940);
		fWindow = new PWWindow(frame, "Untitled");
		fWindow->Show();
	}

	void	MessageReceived(BMessage* message) override
	{
		switch (message->what) {
			case 'pWnw': {	// File New
				BRect frame = fWindow ? fWindow->Frame()
					: BRect(80, 60, 860, 940);
				frame.OffsetBy(24, 24);
				PWWindow* w = new PWWindow(frame, "Untitled");
				w->Show();
				fWindow = w;
				break;
			}
			case 'pWwc':
				if (CountWindows() <= 1)
					Quit();	// last document window gone
				break;
			case B_ABOUT_REQUESTED: {
				BAlert* alert = new BAlert("About ProseWriter",
					"ProseWriter\na word processor for Prose\n\n"
					"Built on Haiku; thank you to everyone\n"
					"who has worked on it.", "OK");
				alert->Go();
				break;
			}
			default:
				BApplication::MessageReceived(message);
		}
	}

private:
	PWWindow*	fWindow = NULL;
};

// --------------------------------------------------------------- selftest --
// Runs the model and layout through their paces in-process and prints
// PASS/FAIL lines (see docs/plan.md, T03–T09). No windows are shown.
static int
SelfTest()
{
	struct Case {
		const char* name;
		bool ok;
	} cases[32];
	int n = 0;
	bool all = true;

	#define CHECK(label, cond) { cases[n].name = label; \
		cases[n].ok = (cond); if (!(cond)) all = false; n++; }

	{
		PWDocument doc;
		CHECK("empty document has one paragraph", doc.CountParagraphs() == 1);
		CHECK("empty document length is 0", doc.Length() == 0);
		doc.Insert(0, "Hello", NULL);
		CHECK("insert grows length", doc.Length() == 5);
		doc.Insert(5, " world", NULL);
		CHECK("append works", strcmp(doc.PlainText(), "Hello world") == 0);
		doc.Remove(0, 6);
		CHECK("remove works", strcmp(doc.PlainText(), "world") == 0);
		doc.Undo();
		CHECK("undo restore text", strcmp(doc.PlainText(), "Hello world") == 0);
		doc.Undo();
		CHECK("second undo", strcmp(doc.PlainText(), "Hello") == 0);
		doc.Redo();
		CHECK("redo", strcmp(doc.PlainText(), "Hello world") == 0);
		doc.Insert(5, "\n", NULL);	// Insert of newline: plain insert
		CHECK("newline via insert", doc.CountParagraphs() == 2);
		doc.Undo();
		CHECK("undo split", doc.CountParagraphs() == 1);

		// Format application and round trip.
		PWCharFormat fmt = doc.FormatAt(0);
		fmt.bold = true;
		doc.ApplyFormat(0, 5, fmt);
		printf("dbg: para0 runs=%d [0]=%d..%d bold=%d len=%d text='%s'\n",
			(int)doc.ParagraphRuns(0).size(),
			doc.ParagraphRuns(0)[0].start, doc.ParagraphRuns(0)[0].length,
			(int)doc.ParagraphRuns(0)[0].format.bold,
			(int)doc.ParagraphLength(0), doc.ParagraphText(0));
		CHECK("format applied", doc.FormatAt(2).bold);
		BMessage saved;
		doc.SaveToMessage(&saved);
		PWDocument doc2;
		doc2.LoadFromMessage(&saved);
		CHECK("round trip text",
			strcmp(doc2.PlainText(), doc.PlainText()) == 0);
		CHECK("round trip bold at 2", doc2.FormatAt(2).bold);
		CHECK("round trip not bold at 6", !doc2.FormatAt(6).bold);

		// A layout smoke test with several paragraphs.
		PWLayout layout(&doc);
		PWPageSetup setup;
		layout.SetPageSetup(setup);
		layout.Layout();
		CHECK("layout makes a page", layout.CountPages() >= 1);
		CHECK("line found for offset 0", layout.LineOfOffset(0) == 0);
		BPoint xy;
		float h;
		CHECK("offset 0 has a caret", layout.OffsetToXY(0, &xy, &h));
		CHECK("caret x at left margin", xy.x == setup.marginLeft);
	}

	{
		// T09 performance: 100 pages of text (~40k words) lays out in time.
		PWDocument doc;
		doc.Insert(0,
			"The quick brown fox jumps over the lazy dog. "
			"Pack my box with five dozen liquor jugs. "
			"How vexingly quick daft zebras jump!", NULL);
		for (int i = 0; i < 4000; i++)
			doc.SplitPara((i * 124) % std::max(1, doc.Length()));
		bigtime_t t0 = system_time();
		PWLayout layout(&doc);
		layout.SetPageSetup(PWPageSetup());
		layout.Layout();
		bigtime_t ms = (system_time() - t0) / 1000;
		printf("perf: %d paragraphs -> %d pages in %lld ms\n",
			(int)doc.CountParagraphs(), (int)layout.CountPages(), (long long)ms);
		CHECK("layout pages produced", layout.CountPages() > 50);
		CHECK("layout under budget (1500 ms)", ms < 1500);
	}

	#undef CHECK

	int passed = 0;
	for (int i = 0; i < n; i++) {
		printf("  %-42s %s\n", cases[i].name, cases[i].ok ? "PASS" : "FAIL");
		if (cases[i].ok) passed++;
	}
	printf("SELFTEST %s %d/%d\n", all ? "PASS" : "FAIL", passed, n);
	return all ? 0 : 1;
}

// -------------------------------------------------------------------- main --
int
main(int argc, char** argv)
{
	if (argc > 1 && strcmp(argv[1], "--selftest") == 0) {
		// BApplication connects to app_server so fonts measure properly;
		// no window is shown and we never enter the run loop.
		PWApp app;
		return SelfTest();
	}
	PWApp app;
	app.Run();
	return 0;
}
