#include "PWWindow.h"

#include <Alert.h>
#include <Application.h>
#include <Beep.h>
#include <Button.h>
#include <Rect.h>
#include <RadioButton.h>
#include <PrintJob.h>
#include <PopUpMenu.h>
#include <GroupLayout.h>
#include <GroupLayoutBuilder.h>
#include <LayoutBuilder.h>
#include <ListView.h>
#include <MenuField.h>
#include <FindDirectory.h>
#include <CheckBox.h>
#include <Clipboard.h>
#include <Entry.h>
#include <Node.h>
#include <File.h>
#include <Font.h>
#include <Menu.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <Message.h>
#include <MimeType.h>
#include <Messenger.h>
#include <Path.h>
#include <PropertyInfo.h>
#include <Screen.h>
#include <ScrollView.h>
#include <StringItem.h>
#include <StringView.h>
#include <TextControl.h>
#include <TranslationUtils.h>
#include <UTF8.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

#include "PWPageView.h"
#include "PWPDF.h"
#include "PWRTF.h"
#include "PWRuler.h"
#include "PWSpell.h"

// ---------------------------------------------------------------- helpers --
static BMenuItem*
item(const char* label, uint32 what, char shortcut = 0, uint32 mod = 0)
{
	BMessage* msg = new BMessage(what);
	if (shortcut)
		return new BMenuItem(label, msg, shortcut, mod);
	return new BMenuItem(label, msg);
}

static status_t
ReadFileToString(const char* path, BString* out)
{
	BFile file;
	status_t err = file.SetTo(path, B_READ_ONLY);
	if (err != B_OK)
		return err;
	off_t size = 0;
	file.GetSize(&size);
	if (size < 0 || size > 64LL * 1024 * 1024) {
		out->SetTo("");
		return size < 0 ? B_ERROR : B_OK;
	}
	char* buffer = new (std::nothrow) char[size + 1];
	if (buffer == NULL)
		return B_NO_MEMORY;
	// read in a loop: a single Read() may return short (the same trap the
	// spell dictionary and LoadFromFile already learned)
	ssize_t got = 0;
	while (got < (ssize_t)size) {
		ssize_t n = file.Read(buffer + got, size - got);
		if (n < 0) {
			got = -1;
			break;
		}
		if (n == 0)
			break;
		got += n;
	}
	if (got < 0) {
		delete[] buffer;
		return B_ERROR;
	}
	out->SetTo(buffer, (int32)got);
	delete[] buffer;
	return B_OK;
}

static status_t
WriteStringToFile(const char* path, const BString& text)
{
	BFile file;
	status_t err = file.SetTo(path, B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (err != B_OK)
		return err;
	ssize_t wrote = file.Write(text.String(), text.Length());
	return wrote == text.Length() ? B_OK : B_ERROR;
}

// What loader a file needs, decided by its CONTENT (the review's rule:
// "detect the format by content, not extension") — a just-saved document
// must reopen regardless of what the user typed in the save panel. The
// native format is a flattened BMessage: "HMF1" magic + our 'pWd&'
// what-code in the first 8 bytes.
enum PWDocKind { PW_KIND_ERROR, PW_KIND_NATIVE, PW_KIND_RTF, PW_KIND_TEXT };
static PWDocKind
SniffDocumentKind(const char* path)
{
	BFile file;
	if (file.SetTo(path, B_READ_ONLY) != B_OK)
		return PW_KIND_ERROR;
	char head[8] = { 0 };
	ssize_t got = file.Read(head, sizeof(head));
	if (got >= 8 && memcmp(head, "HMF1", 4) == 0
		&& memcmp(head + 4, "&dWp", 4) == 0)
		return PW_KIND_NATIVE;
	if (got >= 5 && memcmp(head, "{\\rtf", 5) == 0)
		return PW_KIND_RTF;
	return PW_KIND_TEXT;
}

// Case-insensitive extension test. IFindLast() returns an int32 offset
// (B_ERROR when absent); comparing it to NULL made "not found" read as
// true, so every non-RTF file was fed to the RTF parser and Open never
// worked for the native format. IEndsWith returns bool.
static bool
HasSuffix(const BString& path, const char* suffix)
{
	return path.IEndsWith(suffix);
}

// A file name with the given extension, appended case-insensitively if
// missing — one rule for every save panel (native .prose, RTF, PDF) and
// their prefilled suggestions, so a bare typed name can never produce a
// file our own open path then misreads.
static BString
WithExtension(const BString& name, const char* ext)
{
	if (HasSuffix(name, ext))
		return name;
	BString with(name);
	if (with.Length() > 0 && with[with.Length() - 1] == '.')
		with.Truncate(with.Length() - 1);
	with << ext;
	return with;
}


// Replace the window's document with the contents of `fresh`.
static void
AdoptDocument(PWDocument* into, PWDocument* fresh)
{
	BMessage msg;
	fresh->SaveToMessage(&msg);
	into->LoadFromMessage(&msg);
}

// -------------------------------------------------------------- find bar --
class PWFindBar : public BView {
public:
	PWFindBar(BRect frame)
		:
		BView(frame, "findbar", B_FOLLOW_LEFT_RIGHT | B_FOLLOW_BOTTOM,
			B_WILL_DRAW)
	{
		BRect r(6, 3, 206, 20);
		fFind = new BTextControl(r, "find", "Find:", "",
			new BMessage(PWWindow::FIND_FIELD_MSG), B_FOLLOW_LEFT);
		fFind->SetDivider(34);
		AddChild(fFind);

		r.Set(212, 2, 262, 21);
		AddChild(new BButton(r, "next", "Next",
			new BMessage(PWWindow::FIND_NEXT_MSG), B_FOLLOW_LEFT));

		r.Set(280, 3, 446, 20);
		fReplace = new BTextControl(r, "replace", "Replace:", "",
			new BMessage(PWWindow::REPLACE_FIND_MSG), B_FOLLOW_LEFT);
		fReplace->SetDivider(52);
		AddChild(fReplace);

		r.Set(452, 2, 522, 21);
		AddChild(new BButton(r, "replaceBtn", "Replace",
			new BMessage(PWWindow::REPLACE_FIND_MSG), B_FOLLOW_LEFT));

		r.Set(528, 2, 572, 21);
		AddChild(new BButton(r, "allBtn", "All",
			new BMessage(PWWindow::REPLACE_ALL_MSG), B_FOLLOW_LEFT));

		r.Set(578, 2, 616, 21);
		fCase = new BCheckBox(r, "case", "Aa", new BMessage('pWcs'),
			B_FOLLOW_LEFT);
		AddChild(fCase);

		r.Set(Bounds().right - 26, 2, Bounds().right - 6, 21);
		AddChild(new BButton(r, "close", "Close",
			new BMessage(PWWindow::FIND_BAR_MSG), B_FOLLOW_RIGHT));
	}

	BTextControl*	FindField() { return fFind; }
	BTextControl*	ReplaceField() { return fReplace; }
	BCheckBox*	CaseBox() { return fCase; }

private:
	BTextControl*	fFind;
	BTextControl*	fReplace;
	BCheckBox*		fCase;
};

static const float kFindBarHeight = 26.0f;

// ----------------------------------------------------- paper & print bits --

static const struct {
	const char* name;
	float width, height;
} kPapers[] = {
	{ "A4",		595.0f, 842.0f },
	{ "US Letter",	612.0f, 792.0f },
	{ "US Legal",	612.0f, 1008.0f },
	{ "A5",		420.0f, 595.0f },
	{ "A3",		842.0f, 1191.0f },
	{ "B5",		499.0f, 709.0f },
	{ NULL, 0, 0 }
};

static int32
PaperIndex(const PWPageSetup& s)
{
	for (int i = 0; kPapers[i].name; i++) {
		if ((fabs(s.pageWidth - kPapers[i].width) < 1
				&& fabs(s.pageHeight - kPapers[i].height) < 1)
			|| (fabs(s.pageWidth - kPapers[i].height) < 1
				&& fabs(s.pageHeight - kPapers[i].width) < 1))
			return i;
	}
	return 0;
}

// ---- dialogs, the StyledEdit way (ReplaceWindow pattern) ----
// Panels hide instead of quitting, send their payload to the owner
// handler on the owner's looper, use the layout API, make their primary
// button default, and close on W.
class PWPanelWindow : public BWindow {
public:
	PWPanelWindow(BRect frame, const char* title, PWWindow* owner)
		:
		BWindow(frame, title, B_FLOATING_WINDOW,
			B_NOT_RESIZABLE | B_NOT_ZOOMABLE | B_ASYNCHRONOUS_CONTROLS
			| B_AUTO_UPDATE_SIZE_LIMITS, B_CURRENT_WORKSPACE),
		fOwner(owner)
	{
		AddShortcut('W', B_COMMAND_KEY, new BMessage(MSG_PANEL_HIDE));
	}

	void SendToOwner(BMessage* message)
	{
		fOwner->Looper()->PostMessage(message, fOwner);
	}

	bool QuitRequested() override
	{
		// However we die (close button included), the owner must drop its
		// pointer now — it dangles the moment this window is destroyed.
		// The old design had four per-panel handlers for this that nothing
		// ever sent.
		BMessage closed(MSG_PANEL_CLOSED);
		closed.AddPointer("panel", this);
		SendToOwner(&closed);
		return true;
	}

protected:
	PWWindow*	fOwner;
	enum { MSG_PANEL_HIDE = 'pWpH', MSG_PANEL_CLOSED = 'pWpl' };
};

// A small non-modal settings window: paper, orientation, margins.
// Page setup, StyledEdit pattern: fields in, one message, hide.
class PWPageSetupWindow : public PWPanelWindow {
public:
	PWPageSetupWindow(PWWindow* owner, PWPageSetup setup)
		:
		PWPanelWindow(BRect(0, 0, 300, 240), "Page setup", owner),
		fSetup(setup)
	{
		fPaper = new BPopUpMenu("paper");
		for (int i = 0; kPapers[i].name; i++) {
			BMessage* msg = new BMessage('pprP');
			msg->AddInt32("index", i);
			fPaper->AddItem(new BMenuItem(kPapers[i].name, msg));
		}
		fPaper->ItemAt(PaperIndex(fSetup))->SetMarked(true);
		fPaperField = new BMenuField("paperField", "Paper:", fPaper);

		fPortrait = new BRadioButton("portrait", "Portrait",
			new BMessage('pprO'));
		fLandscape = new BRadioButton("landscape", "Landscape",
			new BMessage('pprO'));
		bool landscape = fSetup.pageWidth > fSetup.pageHeight;
		(landscape ? fLandscape : fPortrait)->SetValue(B_CONTROL_ON);

		const char* labels[4] = { "Left:", "Right:", "Top:", "Bottom:" };
		float* values[4] = { &fSetup.marginLeft, &fSetup.marginRight,
			&fSetup.marginTop, &fSetup.marginBottom };
		char initial[4][16];
		for (int i = 0; i < 4; i++) {
			snprintf(initial[i], sizeof(initial[i]), "%d",
				(int)*values[i]);
			fMargins[i] = new BTextControl(BString("m") << i,
				labels[i], initial[i], NULL);
			fMargins[i]->SetDivider(52);
		}
		BButton* apply = new BButton("apply", "Apply",
			new BMessage(PWWindow::APPLY_SETUP_MSG));
		BButton* cancel = new BButton("cancel", "Close",
			new BMessage(MSG_PANEL_HIDE));
		apply->MakeDefault(true);

		SetLayout(new BGroupLayout(B_VERTICAL));
		AddChild(BGroupLayoutBuilder(B_VERTICAL, 4)
			.Add(fPaperField)
			.Add(fPortrait)
			.Add(fLandscape)
			.Add(fMargins[0])
			.Add(fMargins[1])
			.Add(fMargins[2])
			.Add(fMargins[3])
			.AddGroup(B_HORIZONTAL, 10)
				.Add(apply)
				.AddGlue()
				.Add(cancel)
			.End()
			.SetInsets(10, 10, 10, 10)
		);
	}

	void	MessageReceived(BMessage* message) override
	{
		switch (message->what) {
			case PWWindow::APPLY_SETUP_MSG: {
				int32 index = 0;
				for (int i = 0; i < fPaper->CountItems(); i++)
					if (fPaper->ItemAt(i)->IsMarked())
						index = i;
				bool landscape = fLandscape->Value() == B_CONTROL_ON;
				float w = kPapers[index].width, h = kPapers[index].height;
				if (landscape && w < h) { float x = w; w = h; h = x; }
				if (!landscape && w > h) { float x = w; w = h; h = x; }
				fSetup.pageWidth = w;
				fSetup.pageHeight = h;
				fSetup.marginLeft = atof(fMargins[0]->Text());
				fSetup.marginRight = atof(fMargins[1]->Text());
				fSetup.marginTop = atof(fMargins[2]->Text());
				fSetup.marginBottom = atof(fMargins[3]->Text());
					#define CLAMP(m, limit) if (fSetup.m < 18) fSetup.m = 18; \
						if (fSetup.m > (limit) - 72) fSetup.m = (limit) - 72;
					CLAMP(marginLeft, fSetup.pageWidth)
					CLAMP(marginRight, fSetup.pageWidth)
					CLAMP(marginTop, fSetup.pageHeight)
					CLAMP(marginBottom, fSetup.pageHeight)
					#undef CLAMP
					// the PAIR must leave a usable column/height — the
					// clamps above allowed left+right to swallow the page
					// whole, and a negative column breaks layout
					if (fSetup.marginLeft + fSetup.marginRight
							> fSetup.pageWidth - 72) {
						float over = fSetup.marginLeft + fSetup.marginRight
							- (fSetup.pageWidth - 72);
						fSetup.marginLeft -= over / 2;
						fSetup.marginRight -= over / 2;
					}
					if (fSetup.marginTop + fSetup.marginBottom
							> fSetup.pageHeight - 72) {
						float over = fSetup.marginTop + fSetup.marginBottom
							- (fSetup.pageHeight - 72);
						fSetup.marginTop -= over / 2;
						fSetup.marginBottom -= over / 2;
					}
				BMessage apply(PWWindow::APPLY_SETUP_MSG);
				apply.AddFloat("w", fSetup.pageWidth);
				apply.AddFloat("h", fSetup.pageHeight);
				apply.AddFloat("ml", fSetup.marginLeft);
				apply.AddFloat("mr", fSetup.marginRight);
				apply.AddFloat("mt", fSetup.marginTop);
				apply.AddFloat("mb", fSetup.marginBottom);
				SendToOwner(&apply);
				break;
			}
			case MSG_PANEL_HIDE:
				if (!IsHidden())
					Hide();
				break;
			default:
				BWindow::MessageReceived(message);
		}
	}

private:
	PWPageSetup		fSetup;
	BPopUpMenu*		fPaper;
	BMenuField*		fPaperField;
	BRadioButton*	fPortrait;
	BRadioButton*	fLandscape;
	BTextControl*	fMargins[4];
};


// Header/footer editor with live field hints.
// Header and footer, copied from StyledEdit's ReplaceWindow: strings in,
// one message to the owner, then hide.
class PWHeaderWindow : public PWPanelWindow {
public:
	PWHeaderWindow(PWWindow* owner, const char* header, const char* footer)
		:
		PWPanelWindow(BRect(0, 0, 320, 140), "Header and footer", owner)
	{
		fHeader = new BTextControl("header", "Header:", header, NULL);
		fHeader->SetDivider(56);
		fFooter = new BTextControl("footer", "Footer:", footer, NULL);
		fFooter->SetDivider(56);
		BStringView* hint = new BStringView("hint",
			"{page} and {pages} are replaced per page.");
		BButton* apply = new BButton("apply", "Apply",
			new BMessage(PWWindow::APPLY_HEADER_MSG));
		BButton* cancel = new BButton("cancel", "Cancel",
			new BMessage(MSG_PANEL_HIDE));
		apply->MakeDefault(true);

		SetLayout(new BGroupLayout(B_VERTICAL));
		AddChild(BGroupLayoutBuilder(B_VERTICAL, 4)
			.Add(fHeader)
			.Add(fFooter)
			.Add(hint)
			.AddGroup(B_HORIZONTAL, 10)
				.Add(apply)
				.AddGlue()
				.Add(cancel)
			.End()
			.SetInsets(10, 10, 10, 10)
		);
		fHeader->MakeFocus();
	}

	void	MessageReceived(BMessage* message) override
	{
		switch (message->what) {
			case PWWindow::APPLY_HEADER_MSG: {
				BMessage apply(PWWindow::APPLY_HEADER_MSG);
				apply.AddString("header", fHeader->Text());
				apply.AddString("footer", fFooter->Text());
				SendToOwner(&apply);
				PostMessage(MSG_PANEL_HIDE);
				break;
			}
			case MSG_PANEL_HIDE:
				if (!IsHidden())
					Hide();
				break;
			default:
				BWindow::MessageReceived(message);
		}
	}

private:
	BTextControl*	fHeader;
	BTextControl*	fFooter;
};


static void LoadSpellDictionary(PWWindow* window);
static BPath FrameSettingsPath();

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

	fRuler = new PWRuler(BRect(0, 0, Bounds().Width(), PWRuler::kHeight),
		&fLayout, fView);
	AddChild(fRuler);

	BScrollView* scroll = new BScrollView("scroll", fView, B_FOLLOW_ALL,
		true, true, B_FANCY_BORDER);
	AddChild(scroll);

	fFindBar = new PWFindBar(BRect(0, 0, Bounds().Width(), kFindBarHeight));
	fFindBar->Hide();
	AddChild(fFindBar);

	fStatusView = new BStringView(BRect(0, 0, 200, 18), "status", "",
		B_FOLLOW_LEFT_RIGHT | B_FOLLOW_BOTTOM);
	fStatusView->SetAlignment(B_ALIGN_RIGHT);
	AddChild(fStatusView);

	// File panels are created lazily: they are windows, and an eagerly
	// created panel hijacks "Window 1" from script senders.
	SetSizeLimits(460, 4000, 380, 4000);
	LayoutChildren();
	LoadSpellDictionary(this);
	UpdateStatusText();
	fView->MakeFocus();
}

// Command-line document setup (used by the guest test harness):
//   ProseWriter --demo --set-header "T {page} of {pages}" --set-footer "{page}"
//              --paper letter --landscape --seed "text" [--print]
static const char* gHeader = NULL;
static const char* gFooter = NULL;
static const char* gPaper = NULL;
static const char* gSeed = NULL;
static const char* gSeedImage = NULL;
static const char* gOpenPath = NULL;
static bool gSeedTable = false;
static bool gLandscape = false;
static bool gPrint = false;

static void
ParseArgs(int argc, char** argv)
{
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--set-header") && i + 1 < argc)
			gHeader = argv[++i];
		else if (!strcmp(argv[i], "--set-footer") && i + 1 < argc)
			gFooter = argv[++i];
		else if (!strcmp(argv[i], "--paper") && i + 1 < argc)
			gPaper = argv[++i];
		else if (!strcmp(argv[i], "--seed") && i + 1 < argc)
			gSeed = argv[++i];
		else if (!strcmp(argv[i], "--seed-image") && i + 1 < argc)
			gSeedImage = argv[++i];
		else if (!strcmp(argv[i], "--seed-table"))
			gSeedTable = true;
		else if (!strcmp(argv[i], "--landscape"))
			gLandscape = true;
		else if (!strcmp(argv[i], "--print"))
			gPrint = true;
		else if (argv[i][0] != '-')
			gOpenPath = argv[i];	// a document to open
	}
}

// The product identity, in one place.
static const char* kPWVersion = "0.1";
static const char* kPWReleaseDate = __DATE__;	// build day = release day

// About ProseWriter: a real about window — name, version, team, licence,
// and the credit that matters.
class PWAboutWindow : public BWindow {
public:
	explicit PWAboutWindow(PWWindow* owner, BApplication* app)
		:
		BWindow(BRect(0, 0, 360, 240), "About ProseWriter",
			B_TITLED_WINDOW_LOOK, B_FLOATING_SUBSET_WINDOW_FEEL,
			B_ASYNCHRONOUS_CONTROLS | B_QUIT_ON_WINDOW_CLOSE),
		fOwnerApp(app)
	{
		BStringView* name = new BStringView(BRect(10, 12, 350, 36),
			"name", "ProseWriter");
		name->SetFont(be_bold_font);
		name->SetFontSize(20);
		name->SetAlignment(B_ALIGN_CENTER);
		AddChild(name);

		BString line;
		BStringView* version = new BStringView(BRect(10, 40, 350, 56),
			"version", (line << "Version " << kPWVersion
				<< "  \302\267  released " << kPWReleaseDate).String());
		version->SetAlignment(B_ALIGN_CENTER);
		AddChild(version);

		BStringView* team = new BStringView(BRect(10, 62, 350, 78),
			"team", "by the Prose team");
		team->SetFontSize(12);
		team->SetAlignment(B_ALIGN_CENTER);
		AddChild(team);

		BStringView* licence = new BStringView(BRect(10, 100, 350, 116),
			"licence", "MIT licence \302\267 fork freely, attribution kept");
		licence->SetFontSize(10);
		licence->SetAlignment(B_ALIGN_CENTER);
		AddChild(licence);

		BStringView* haiku = new BStringView(BRect(16, 140, 344, 172),
			"haiku", "Built on Haiku, which carries the spirit of BeOS.\n"
			"Thank you to everyone who has worked on it.\n"
			"haiku-os.org");
		haiku->SetFontSize(10);
		haiku->SetAlignment(B_ALIGN_CENTER);
		AddChild(haiku);

		AddChild(new BButton(BRect(140, 196, 220, 216), "close", "Close",
			new BMessage(B_QUIT_REQUESTED)));

		AddToSubset(owner);
		MoveTo(owner->Frame().left + 120, owner->Frame().top + 140);
	}

	bool QuitRequested() override
	{
		if (fOwnerApp != NULL)
			fOwnerApp->PostMessage('pWaq');
		return true;
	}

private:
	BApplication* fOwnerApp;
};

// Insert table: rows, columns, and a header row — asked, not assumed.
// Insert table, StyledEdit pattern: dimensions in, one message, hide.
class PWInsertTableWindow : public PWPanelWindow {
public:
	explicit PWInsertTableWindow(PWWindow* owner)
		:
		PWPanelWindow(BRect(0, 0, 260, 140), "Insert table", owner)
	{
		fRows = new BTextControl("rows", "Rows:", "3", NULL);
		fRows->SetDivider(40);
		fCols = new BTextControl("cols", "Columns:", "3", NULL);
		fCols->SetDivider(52);
		fHeader = new BCheckBox("header", "Header row (bold)", NULL);
		fHeader->SetValue(B_CONTROL_ON);
		BButton* insert = new BButton("insert", "Insert",
			new BMessage('pWtI'));
		BButton* cancel = new BButton("cancel", "Cancel",
			new BMessage(MSG_PANEL_HIDE));
		insert->MakeDefault(true);

		SetLayout(new BGroupLayout(B_VERTICAL));
		AddChild(BGroupLayoutBuilder(B_VERTICAL, 4)
			.Add(fRows)
			.Add(fCols)
			.Add(fHeader)
			.AddGroup(B_HORIZONTAL, 10)
				.Add(insert)
				.AddGlue()
				.Add(cancel)
			.End()
			.SetInsets(10, 10, 10, 10)
		);
		fRows->MakeFocus();
	}

	void	MessageReceived(BMessage* message) override
	{
		switch (message->what) {
			case 'pWtI': {
				int32 rows = atoi(fRows->Text());
				int32 cols = atoi(fCols->Text());
				if (rows < 1) rows = 1;
				if (rows > 100) rows = 100;
				if (cols < 1) cols = 1;
				if (cols > 50) cols = 50;
				BMessage insert('pWtI');
				insert.AddInt32("rows", rows);
				insert.AddInt32("cols", cols);
				insert.AddBool("header",
					fHeader->Value() == B_CONTROL_ON);
				SendToOwner(&insert);
				PostMessage(MSG_PANEL_HIDE);
				break;
			}
			case MSG_PANEL_HIDE:
				if (!IsHidden())
					Hide();
				break;
			default:
				BWindow::MessageReceived(message);
		}
	}

private:
	BTextControl*	fRows;
	BTextControl*	fCols;
	BCheckBox*	fHeader;
};


// Styles panel: define once, apply everywhere — the Gobe lesson.
// Styles panel, StyledEdit pattern.
class PWStylesWindow : public PWPanelWindow {
public:
	explicit PWStylesWindow(PWWindow* owner)
		:
		PWPanelWindow(BRect(0, 0, 300, 220), "Styles", owner)
	{
		fList = new BListView("styles");
		fName = new BTextControl("name", "Name:", "", NULL);
		fName->SetDivider(40);
		BButton* newBtn = new BButton("new", "New from selection",
			new BMessage(PWWindow::STYLE_NEW_MSG));
		BButton* applyBtn = new BButton("apply", "Apply",
			new BMessage(PWWindow::STYLE_APPLY_MSG));
		BButton* delBtn = new BButton("del", "Delete",
			new BMessage(PWWindow::STYLE_DEL_MSG));
		applyBtn->MakeDefault(true);

		SetLayout(new BGroupLayout(B_HORIZONTAL));
		AddChild(BGroupLayoutBuilder(B_VERTICAL, 4)
			.Add(new BScrollView("scroll", fList, B_FOLLOW_ALL, true, true))
			.AddGroup(B_HORIZONTAL, 4)
				.Add(fName)
			.End()
			.End()
		);
		AddChild(BGroupLayoutBuilder(B_VERTICAL, 4)
			.Add(newBtn)
			.Add(applyBtn)
			.Add(delBtn)
			.AddGlue()
			.SetInsets(0, 0, 0, 0)
		);
		RefreshList();
	}

	void	RefreshList()
	{
		fList->MakeEmpty();
		PWDocument* doc = fOwner->Document();
		for (int32 i = 0; i < doc->CountStyles(); i++)
			fList->AddItem(new BStringItem(doc->StyleAt(i)->name.String()));
	}

	void	MessageReceived(BMessage* message) override
	{
		switch (message->what) {
			case 'pWls':
				RefreshList();
				break;
			case PWWindow::STYLE_NEW_MSG: {
				const char* name = fName->Text();
				if (!name || !name[0])
					break;
				BMessage styleMsg(PWWindow::STYLE_NEW_MSG);
				styleMsg.AddString("name", name);
				SendToOwner(&styleMsg);
				break;
			}
			case PWWindow::STYLE_APPLY_MSG: {
				int32 sel = fList->CurrentSelection();
				if (sel >= 0) {
					BMessage apply(PWWindow::STYLE_APPLY_MSG);
					apply.AddInt32("index", sel);
					SendToOwner(&apply);
				}
				break;
			}
			case PWWindow::STYLE_DEL_MSG: {
				int32 sel = fList->CurrentSelection();
				if (sel >= 0) {
					BMessage del(PWWindow::STYLE_DEL_MSG);
					del.AddInt32("index", sel);
					SendToOwner(&del);
				}
				break;
			}
			case MSG_PANEL_HIDE:
				if (!IsHidden())
					Hide();
				break;
			default:
				BWindow::MessageReceived(message);
		}
	}

private:
	BListView*	fList;
	BTextControl*	fName;
};


// The window thread owns the document, layout and view; the harness
// arguments are delivered as a message so nothing is mutated cross-thread.
static void
ApplyWindowArgs(PWWindow* window)
{
	BMessage args('pWda');
	if (gHeader)
		args.AddString("header", gHeader);
	if (gFooter)
		args.AddString("footer", gFooter);
	if (gPaper)
		args.AddString("paper", gPaper);
	if (gLandscape)
		args.AddBool("landscape", true);
	if (gSeed)
		args.AddString("seed", gSeed);
	if (gSeedImage)
		args.AddString("seedImage", gSeedImage);
	if (gSeedTable)
		args.AddBool("seedTable", true);
	if (gOpenPath)
		args.AddString("openPath", gOpenPath);
	if (gPrint)
		args.AddBool("print", true);
	window->PostMessage(&args);
}

PWWindow::Panels*
PWWindow::EnsurePanels()
{
	// The completion message is what the panel SENDS when the user
	// confirms — it must be the *selection handler*, never the
	// "show the panel" menu command. Save/Open shared codes with their
	// show-commands, so confirming re-showed the panel and the save
	// handler ('pWsv'/'pWop') never ran: the UI never wrote a file.
	if (fPanels == NULL)
		fPanels = new Panels(new BFilePanel(B_OPEN_PANEL, new BMessenger(this),
			NULL, B_FILE_NODE, false, new BMessage('pWop')),
			new BFilePanel(B_SAVE_PANEL, new BMessenger(this), NULL,
				B_FILE_NODE, false, new BMessage('pWsv')),
			new BFilePanel(B_SAVE_PANEL, new BMessenger(this), NULL,
				B_FILE_NODE, false, new BMessage(EXPORT_RTF_DONE_MSG)));
	return fPanels;
}

void
PWWindow::SaveViaScript(const char* path)
{
	if (path != NULL)
		DoSave(BString(path));
	else if (fFilePath.Length())
		DoSave(fFilePath);
}

void
PWWindow::AdoptDoc(PWDocument* fresh)
{
	BMessage msg;
	fresh->SaveToMessage(&msg);
	fDoc.LoadFromMessage(&msg);
	fView->SetCaret(0, false);
	fView->Relayout();
	UpdateStatusText();
}

void
PWWindow::LayoutChildren()
{
	float menuH = fMenuBar->Bounds().Height();
	float top = menuH + 1;
	fRuler->MoveTo(0, top);
	fRuler->ResizeTo(Bounds().Width(), PWRuler::kHeight);
	top += PWRuler::kHeight;
	float bottom = Bounds().bottom - 19 - (fFindShown ? kFindBarHeight : 0);
	BView* scroll = fView->ScrollView();
	if (scroll) {
		// exactly the content width, ending above the status strip —
		// the old +1s hung the border over the edge and the status bar
		scroll->MoveTo(0, top);
		scroll->ResizeTo(Bounds().Width(), bottom - top);
	}
	fFindBar->MoveTo(0, Bounds().bottom - 19 - kFindBarHeight + 1);
	fFindBar->ResizeTo(Bounds().Width(), kFindBarHeight);
	fStatusView->MoveTo(0, Bounds().bottom - 17);
	fStatusView->ResizeTo(Bounds().Width() - 8, 16);
}

void
PWWindow::FrameResized(float, float)
{
	LayoutChildren();
}

void
PWWindow::ToggleFindBar(bool show)
{
	if (show == fFindShown)
		return;
	fFindShown = show;
	if (show) {
		fFindBar->Show();
		((PWFindBar*)fFindBar)->FindField()->MakeFocus(true);
	} else {
		fFindBar->Hide();
		fView->MakeFocus();
	}
	LayoutChildren();
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
	menu->AddItem(item("Export as RTF" B_UTF8_ELLIPSIS, EXPORT_RTF_MSG));
	fRecentMenu = new BMenu("Open recent");
	menu->AddItem(fRecentMenu);
	BuildRecentMenu();
	menu->AddSeparatorItem();
	menu->AddItem(item("Insert image" B_UTF8_ELLIPSIS, 'pWim'));
	menu->AddItem(item("Insert table", 'pWtb'));
		menu->AddItem(item("Page setup" B_UTF8_ELLIPSIS, PAGE_SETUP_MSG));
		menu->AddItem(item("Print to PDF" B_UTF8_ELLIPSIS, 'pWpq'));
		menu->AddItem(item("Print" B_UTF8_ELLIPSIS, PRINT_MSG, 'P',
			B_COMMAND_KEY));
	menu->AddSeparatorItem();
	menu->AddItem(item("Close", B_QUIT_REQUESTED, 'W', B_COMMAND_KEY));
	BMenuItem* quitItem = item("Quit", B_QUIT_REQUESTED, 'Q',
		B_COMMAND_KEY);
	quitItem->SetTarget(be_app);	// Quit closes every window, not one
	menu->AddItem(quitItem);
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
	menu->AddSeparatorItem();
	// structure edits on the table under the caret; Tab already moves
	// between cells and appends a row from the last cell
	BMenu* table = new BMenu("Table");
	table->AddItem(item("Insert row", 'pWRI'));
	table->AddItem(item("Insert column", 'pWCI'));
	table->AddItem(item("Delete row", 'pWRD'));
	table->AddItem(item("Delete column", 'pWCD'));
	menu->AddItem(table);
	fMenuBar->AddItem(menu);

	menu = new BMenu("Text");
	menu->AddItem(item("Bold", TEXT_APPLY_MSG, 'B', B_COMMAND_KEY));
	menu->ItemAt(0)->Message()->AddInt32("what-kind", 0);
	menu->AddItem(item("Italic", TEXT_APPLY_MSG, 'I', B_COMMAND_KEY));
	menu->ItemAt(1)->Message()->AddInt32("what-kind", 1);
	menu->AddItem(item("Underline", TEXT_APPLY_MSG, 'U', B_COMMAND_KEY));
	menu->ItemAt(2)->Message()->AddInt32("what-kind", 2);
	menu->AddSeparatorItem();

	BMenu* family = new BMenu("Family");
	font_family name;
	int32 count = count_font_families();
	for (int32 i = 0; i < count; i++) {
		uint32 flags;
		if (get_font_family(i, &name, &flags) == B_OK) {
			BMessage* msg = new BMessage(FAMILY_MSG);
			msg->AddString("family", (const char*)name);
			family->AddItem(new BMenuItem((const char*)name, msg));
		}
	}
	family->SetRadioMode(true);
	menu->AddItem(family);

	BMenu* size = new BMenu("Size");
	const float sizes[] = { 9, 10, 11, 12, 14, 16, 18, 24, 36, 48 };
	for (float s : sizes) {
		BMessage* msg = new BMessage(SIZE_MSG);
		msg->AddFloat("size", s);
		char label[12];
		snprintf(label, sizeof(label), "%d pt", (int)s);
		size->AddItem(new BMenuItem(label, msg));
	}
	size->SetRadioMode(true);
	menu->AddItem(size);

	BMenu* colour = new BMenu("Colour");
	struct { const char* name; rgb_color c; } colours[] = {
		{ "Black",	{ 0, 0, 0, 255 } },
		{ "Grey",	{ 130, 130, 130, 255 } },
		{ "Silver",	{ 190, 190, 190, 255 } },
		{ "Maroon",	{ 128, 0, 0, 255 } },
		{ "Red",	{ 200, 0, 0, 255 } },
		{ "Olive",	{ 128, 110, 0, 255 } },
		{ "Green",	{ 0, 128, 0, 255 } },
		{ "Navy",	{ 0, 0, 128, 255 } },
		{ "Blue",	{ 0, 60, 200, 255 } },
		{ "Purple",	{ 128, 0, 128, 255 } },
	};
	for (auto& c : colours) {
		BMessage* msg = new BMessage(COLOR_MSG);
		msg->AddInt32("red", c.c.red);
		msg->AddInt32("green", c.c.green);
		msg->AddInt32("blue", c.c.blue);
		colour->AddItem(new BMenuItem(c.name, msg));
	}
	menu->AddItem(colour);
	menu->AddSeparatorItem();
	const char* alignLabels[] = { "Align left", "Align centre",
		"Align right", "Justify" };
	for (int i = 0; i < 4; i++) {
		BMessage* msg = new BMessage(ALIGN_MSG);
		msg->AddInt32("align", i);
		menu->AddItem(new BMenuItem(alignLabels[i], msg));
	}
	menu->ItemAt(menu->CountItems() - 4)->SetMarked(true);
	fMenuBar->AddItem(menu);

	menu = new BMenu("Document");
	menu->AddItem(item("Header and footer" B_UTF8_ELLIPSIS, HEADER_MSG));
	menu->AddItem(item("Styles" B_UTF8_ELLIPSIS, STYLES_MSG));
	fSpellItem = item("Check spelling", 'pWsc');
	fSpellItem->SetMessage(new BMessage('pWsc'));
	fSpellItem->SetMarked(true);
	menu->AddItem(fSpellItem);
	fMenuBar->AddItem(menu);

	menu = new BMenu("Search");
	menu->AddItem(item("Find", FIND_BAR_MSG, 'F', B_COMMAND_KEY));
	menu->AddItem(item("Find next", FIND_NEXT_MSG, 'G', B_COMMAND_KEY));
	fMenuBar->AddItem(menu);

	fZoomMenu = new BMenu("Zoom");
	const float zooms[] = { 0.5f, 0.75f, 1.0f, 1.5f, 2.0f };
	for (float z : zooms) {
		BMessage* msg = new BMessage(ZOOM_MSG);
		msg->AddFloat("zoom", z);
		char label[16];
		snprintf(label, sizeof(label), "%d%%", (int)(z * 100));
		fZoomMenu->AddItem(new BMenuItem(label, msg));
	}
	fZoomMenu->SetRadioMode(true);
	fZoomMenu->ItemAt(2)->SetMarked(true);
	menu = new BMenu("View");
	menu->AddItem(fZoomMenu);
	menu->AddItem(item("Fit width", FIT_WIDTH_MSG));
	menu->AddSeparatorItem();
	menu->AddItem(item("Zoom in", 'pWzi', '=', B_COMMAND_KEY));
	menu->AddItem(item("Zoom out", 'pWzo', '-', B_COMMAND_KEY));
	fMenuBar->AddItem(menu);

	menu = new BMenu("Help");
	menu->AddItem(item("About ProseWriter", B_ABOUT_REQUESTED));
	fMenuBar->AddItem(menu);

	// trigger letters (menu-bar focus, then the letter)
	fMenuBar->FindItem("File")->SetTrigger('F');
	fMenuBar->FindItem("Edit")->SetTrigger('E');
	fMenuBar->FindItem("Text")->SetTrigger('T');
	fMenuBar->FindItem("Search")->SetTrigger('S');
	fMenuBar->FindItem("Document")->SetTrigger('D');
	fMenuBar->FindItem("View")->SetTrigger('V');
	fMenuBar->FindItem("Help")->SetTrigger('H');
}

void
PWWindow::MenusBeginning()
{
	fUndoItem->SetEnabled(fDoc.CanUndo());
	fRedoItem->SetEnabled(fDoc.CanRedo());
	fSaveItem->SetEnabled(fDoc.IsModified());
	const PWCharFormat& fmt = fView->CurrentFormat();
	fMenuBar->FindItem("Bold")->SetMarked(fmt.bold);
	fMenuBar->FindItem("Italic")->SetMarked(fmt.italic);
	fMenuBar->FindItem("Underline")->SetMarked(fmt.underline);
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
	text.SetToFormat("page %d of %d   ·   %d words   ·   %d%%", (int)page,
		(int)fLayout.CountPages(), (int)words,
		(int)(fView->Zoom() * 100 + 0.5f));
	fStatusView->SetText(text.String());
	UpdateTitle();
}

void
PWWindow::FindNext()
{
	PWFindBar* bar = (PWFindBar*)fFindBar;
	const char* needle = bar->FindField()->Text();
	if (!needle || !needle[0])
		return;
	int32 length = 0;
	int32 from = fView->CaretOffset();
	if (fView->HasSelection()) {
		int32 sFrom, sTo;
		fView->GetSelection(&sFrom, &sTo);
		if (from == sFrom)
			from = sTo;	// step past the current match
	}
	int32 found = fDoc.FindNext(needle, from,
		bar->CaseBox()->Value() == B_CONTROL_ON, true, &length);
	if (found >= 0) {
		fView->Select(found, found + length);
		fView->ScrollCaretVisible();
	}
}

void
PWWindow::ReplaceAndFind()
{
	PWFindBar* bar = (PWFindBar*)fFindBar;
	const char* needle = bar->FindField()->Text();
	const char* replacement = bar->ReplaceField()->Text();
	if (!needle || !needle[0])
		return;
	int32 from, to;
	fView->GetSelection(&from, &to);
	int32 length = 0;
	int32 at = fDoc.FindNext(needle, from,
		bar->CaseBox()->Value() == B_CONTROL_ON, false, &length);
	if (at == from && length == to - from && at >= 0) {
		fDoc.Remove(at, length);
		fDoc.Insert(at, replacement, NULL);
		fView->SetCaret(at + (int32)strlen(replacement), false);
		fView->Relayout();
	}
	FindNext();
}

void
PWWindow::ReplaceAll()
{
	PWFindBar* bar = (PWFindBar*)fFindBar;
	int32 count = fDoc.ReplaceAll(bar->FindField()->Text(),
		bar->ReplaceField()->Text(),
		bar->CaseBox()->Value() == B_CONTROL_ON);
	fView->Relayout();
	UpdateStatusText();
	BString report;
	report.SetToFormat("Replaced %d.", (int)count);
	(new BAlert("ProseWriter", report.String(), "OK"))->Go(NULL);
}

void
PWWindow::ApplyAlignment(int32 alignment)
{
	int32 from, to;
	fView->GetSelection(&from, &to);
	if (to < from) { int32 t = from; from = to; to = t; }
	if (from == to)
		to = from + 1;	// caret only: its paragraph
	int32 para, inPara;
	fDoc.Locate(from, &para, &inPara);
	PWParaFormat fmt;
	fmt.alignment = (PWAlignment)alignment;
	int32 lastPara;
	fDoc.Locate(to, &lastPara, &inPara);
	for (int32 p = para; p <= lastPara && p < fDoc.CountParagraphs(); p++)
		fDoc.SetParaFormat(p, fmt);
	fView->Relayout();
}

void
PWWindow::SetZoom(float zoom)
{
	fView->SetZoom(zoom);
	fRuler->Invalidate();
	MarkZoomItem(zoom);
	UpdateStatusText();
}

void
PWWindow::MarkZoomItem(float zoom)
{
	for (int32 i = 0; i < fZoomMenu->CountItems(); i++) {
		float z = 0;
		fZoomMenu->ItemAt(i)->Message()->FindFloat("zoom", &z);
		fZoomMenu->ItemAt(i)->SetMarked(fabs(z - zoom) < 0.01f);
	}
}

// Dictionary search order: the user's settings, the system data dir, and
// beside the application (dev installs). Any hit enables checking.
static void
LoadSpellDictionary(PWWindow* window)
{
	PWSpellChecker* spell = new PWSpellChecker();
	BPath candidates[3];
	find_directory(B_USER_SETTINGS_DIRECTORY, &candidates[0]);
	candidates[0].Append("ProseWriter/words");
	candidates[1].SetTo("/boot/system/data/ProseWriter/words");
	candidates[2].SetTo("/boot/home/apps/words");
	for (BPath& path : candidates) {
		int32 n = spell->Load(path.Path());
		if (n > 0) {
			fprintf(stderr, "ProseWriter: %d words from %s\n", (int)n,
				path.Path());
			break;
		}
	}
	window->SetSpellChecker(spell);
}

void
PWWindow::SetSpellChecker(PWSpellChecker* spell)
{
	fSpell = spell;
	fView->SetSpellChecker(spell);
	fView->SetSpellEnabled(fSpellItem->IsMarked() && spell->Loaded());
}

void
PWWindow::ApplyPageSetup(const PWPageSetup& setup)
{
	fLayout.SetPageSetup(setup);
	fView->Relayout();
	fRuler->Invalidate();
	UpdateStatusText();
}

void
PWWindow::ExportPDF(const char* path)
{
	// The whole document, one PDF page per layout page, WYSIWYG with the
	// screen (paper size, margins, headers/footers — one source of truth).
	bigtime_t t0 = system_time();
	status_t err = PW_WritePDF(fView, &fLayout, path, 2.0f);	// 144 dpi
	if (err == B_OK) {
		BString done;
		done.SetToFormat("PDF written: %s (%lld ms, %d page%s)", path,
			(long long)((system_time() - t0) / 1000),
			(int)fLayout.CountPages(),
			fLayout.CountPages() == 1 ? "" : "s");
		(new BAlert("ProseWriter", done.String(), "OK"))->Go(NULL);
	} else {
		BString bad;
		bad.SetToFormat("Could not write %s: %s", path, strerror(err));
		(new BAlert("ProseWriter", bad.String(), "OK"))->Go(NULL);
	}
}

void
PWWindow::Print()
{
	BPrintJob job("ProseWriter");
	status_t configured = job.ConfigJob();
	if (configured == B_CANCEL)
		return;		// the user changed their mind — that is not an error
	if (configured != B_OK) {
		(new BAlert("ProseWriter",
			"No printer is configured; printing was cancelled.", "OK"))->Go();
		return;
	}
	job.BeginJob();
	int32 pages = fLayout.CountPages();
	float scale = job.PrintableRect().Width() / fLayout.PageSetup().pageWidth;
	float wasZoom = fView->Zoom();
	fView->SetZoom(scale);
	for (int32 p = 0; p < pages; p++) {
		BRect page(0, 0, fLayout.PageSetup().pageWidth * scale,
			fLayout.PageSetup().pageHeight * scale);
		fView->BeginPrintMode(p);
		job.DrawView(fView, page, BPoint(0, 0));
		fView->EndPrintMode();
		job.SpoolPage();
	}
	fView->SetZoom(wasZoom);
	job.CommitJob();
}

static BPath
RecentFilePath()
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) == B_OK) {
		path.Append("ProseWriter");
		create_directory(path.Path(), 0755);
		path.Append("recent-files");
	}
	return path;
}

void
PWWindow::BuildRecentMenu()
{
	while (fRecentMenu->RemoveItem((int32)0) != NULL) { }
	BPath path = RecentFilePath();
	BString lines;
	{
		BFile file;
		if (file.SetTo(path.Path(), B_READ_ONLY) == B_OK) {
			char buffer[4096];
			ssize_t n = file.Read(buffer, sizeof(buffer) - 1);
			if (n > 0) {
				buffer[n] = 0;
				lines = buffer;
			}
		}
	}
	int32 count = 0;
	int32 at = 0;
	while (count < 8) {
		int32 eol = lines.FindFirst('\n', at);
		BString line;
		lines.CopyInto(line, at, (eol < 0 ? lines.Length() : eol) - at);
		if (line.Length() == 0)
			break;
		BMessage* msg = new BMessage(RECENT_MSG);
		msg->AddString("path", line);
		BPath only(line.String());
		fRecentMenu->AddItem(new BMenuItem(only.Leaf(), msg));
		at = eol + 1;
		count++;
		if (eol < 0)
			break;
	}
	if (count == 0)
		fRecentMenu->AddItem(new BMenuItem("(none)", NULL));
}

void
PWWindow::AddRecentFile(const char* path)
{
	BPath file = RecentFilePath();
	BString lines;
	{
		BFile in;
		if (in.SetTo(file.Path(), B_READ_ONLY) == B_OK) {
			char buffer[4096];
			ssize_t n = in.Read(buffer, sizeof(buffer) - 1);
			if (n > 0) {
				buffer[n] = 0;
				lines = buffer;
			}
		}
	}
	BString updated = path;
	updated << "\n";
	int32 at = 0;
	int32 added = 1;
	while (added < 8) {
		int32 eol = lines.FindFirst('\n', at);
		if (eol < 0)
			break;
		BString line;
		lines.CopyInto(line, at, eol - at);
		at = eol + 1;
		if (line != path) {
			updated << line << "\n";
			added++;
		}
	}
	BFile out;
	if (out.SetTo(file.Path(),
			B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE) == B_OK)
		out.Write(updated.String(), updated.Length());
	BuildRecentMenu();
}

// ---- scripting: the Be way. hey ProseWriter get Text of Window 1, &c.
static void
ReplyString(BMessage* message, const char* value)
{
	BMessage reply(B_REPLY);
	reply.AddString("result", value);
	message->SendReply(&reply);
}

static void
ReplyInt(BMessage* message, int32 value)
{
	BMessage reply(B_REPLY);
	reply.AddInt32("result", value);
	message->SendReply(&reply);
}

static void
ReplyError(BMessage* message, const char* error)
{
	BMessage reply(B_REPLY);
	reply.AddString("error", error);
	message->SendReply(&reply);
}

// Answered on whichever looper owns the window; property work is all
// main-thread window state, so this is safe from app or window looper.
static bool
HandleScriptingForWindow(PWWindow* window, BMessage* message,
	const char* property)
{
	if (property == NULL || !property[0])
		return false;
	PWDocument& doc = *window->Document();
	BString prop = property;
	bool isGet = message->what == B_GET_PROPERTY;
	// set/execute verbs fall through to their handlers below

	if (prop == "Text") {
		if (isGet) {
			ReplyString(message, doc.PlainText());
		} else {
			BString text;
			if (message->FindString("data", &text) == B_OK) {
				PWDocument fresh;
				fresh.Insert(0, text.String(), NULL);
				window->AdoptDoc(&fresh);
				ReplyString(message, "");
			} else
				ReplyError(message, "no data");
		}
		return true;
	}
	if (prop == "Header" || prop == "Footer") {
		if (isGet) {
			ReplyString(message, prop == "Header" ? doc.HeaderText()
				: doc.FooterText());
		} else {
			BString text;
			if (message->FindString("data", &text) == B_OK) {
				if (prop == "Header")
					doc.SetHeaderText(text.String());
				else
					doc.SetFooterText(text.String());
				window->View()->Relayout();
				ReplyString(message, "");
			} else
				ReplyError(message, "no data");
		}
		return true;
	}
	if (prop == "Selection") {
		BString sel;
		if (isGet) {
			int32 from, to;
			window->View()->GetSelection(&from, &to);
			sel.SetToFormat("%d-%d", (int)from, (int)to);
			ReplyString(message, sel.String());
		} else if (message->FindString("data", &sel) == B_OK) {
			int32 dash = sel.FindFirst('-');
			int32 a = atol(sel.String());
			int32 b = dash >= 0 ? atol(sel.String() + dash + 1) : a;
			window->View()->Select(a, b);
			ReplyString(message, "");
		} else
			ReplyError(message, "no data");
		return true;
	}
	if (prop == "Modified") {
		if (isGet)
			ReplyInt(message, doc.IsModified() ? 1 : 0);
		else
			ReplyError(message, "read-only property");
		return true;
	}
	if (prop == "Frame") {
		BString frame;
		if (isGet) {
			BRect f = window->Frame();
			frame.SetToFormat("%g %g %g %g", f.left, f.top, f.right,
				f.bottom);
			ReplyString(message, frame.String());
		} else if (message->FindString("data", &frame) == B_OK) {
			float l, t2, r, b;
			if (sscanf(frame.String(), "%g %g %g %g", &l, &t2, &r, &b) == 4) {
				window->MoveTo(l, t2);
				window->ResizeTo(r - l, b - t2);
				ReplyString(message, "");
			} else
				ReplyError(message, "frame is \"l t r b\"");
		} else
			ReplyError(message, "no data");
		return true;
	}
	if (prop == "Version" && isGet) {
		ReplyString(message, kPWVersion);
		return true;
	}
	if (prop == "Save" && message->what == B_EXECUTE_PROPERTY) {
		BString path;
		if (message->FindString("data", &path) == B_OK && path.Length())
			window->SaveViaScript(path.String());
		else
			window->SaveViaScript(NULL);
		ReplyString(message, "");
		return true;
	}
	if (prop == "Title" && isGet) {
		ReplyString(message, window->Title());
		return true;
	}
	if (prop == "Quit" && message->what == B_EXECUTE_PROPERTY) {
		window->PostMessage(B_QUIT_REQUESTED);
		ReplyString(message, "");
		return true;
	}
	if (prop == "Activate" && message->what == B_EXECUTE_PROPERTY) {
		// Bring the window forward and give the page view keyboard focus
		// without mouse input — the harness's way in when the VM's input
		// layer cannot click.
		window->Activate();
		window->View()->MakeFocus();
		ReplyString(message, "");
		return true;
	}
	if (prop == "Open" && message->what == B_EXECUTE_PROPERTY) {
		// open a document: data = path. Forwarded to the window looper as
		// the very message the Open panel sends ('pWop' + refs), so the
		// scripted path and the panel path are one code path — and the
		// modified-guard's alert stays on the window thread where a modal
		// Go() is legal.
		BString path;
		if (message->FindString("data", &path) == B_OK && path.Length()) {
			entry_ref ref;
			if (get_ref_for_path(path.String(), &ref) == B_OK) {
				BMessage open('pWop');
				open.AddRef("refs", &ref);
				window->PostMessage(&open);
				ReplyString(message, "");
			} else
				ReplyError(message, "bad path");
		} else
			ReplyError(message, "data: path required");
		return true;
	}
	if (prop == "PDF" && message->what == B_EXECUTE_PROPERTY) {
		// print to PDF: data = output path. No alert here — the caller
		// holds the window lock and a modal Go() would deadlock the app.
		BString path;
		if (message->FindString("data", &path) == B_OK && path.Length()) {
			status_t err = PW_WritePDF(window->View(), window->Layout(),
				path.String(), 2.0f);
			if (err == B_OK)
				ReplyString(message, "");
			else
				ReplyError(message, strerror(err));
		} else
			ReplyError(message, "data: output path required");
		return true;
	}
	if (prop == "WordCount" && isGet) {
		int32 words = 0;
		const char* plain = doc.PlainText();
		bool inWord = false;
		for (const char* c = plain; *c; c++) {
			bool w = (*c != ' ' && *c != '\n' && *c != '\t');
			if (w && !inWord) words++;
			inWord = w;
		}
		ReplyInt(message, words);
		return true;
	}
	return false;
}

bool
PWWindow::HandleScripting(BMessage* message)
{
	int32 index = 0;
	BMessage spec;
	int32 what = 0;
	const char* prop = NULL;
	if (message->GetCurrentSpecifier(&index, &spec, &what, &prop) != B_OK)
		return false;
	return HandleScriptingForWindow(this, message, prop);
}

void
PWWindow::MessageReceived(BMessage* message)
{
	if (HandleScripting(message))
		return;
	switch (message->what) {
		case DOC_MODIFIED_MSG:
			UpdateStatusText();
			break;
		case OPEN_PANEL_MSG:
			EnsurePanels()->open->Show();
			break;
		case SAVE_PANEL_MSG:
			EnsurePanels();
			{
				BString suggested = fFileName.Length() ? fFileName
					: BString("Untitled");
				fPanels->save->SetSaveText(
					WithExtension(suggested, ".prose").String());
			}
			fPanels->save->Show();
			break;
		case EXPORT_RTF_MSG:
			EnsurePanels()->exportRtf->Show();
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
				path.Append(WithExtension(name, ".prose").String());
				DoSave(BString(path.Path()));
				// finish a quit that was waiting on this save
				if (fQuitPending) {
					fQuitPending = false;
					if (!fDoc.IsModified())
						PostMessage(B_QUIT_REQUESTED);
				}
			}
			break;
		}
		case B_CANCEL:
			// a file panel was dismissed: a pending quit dies with it
			fQuitPending = false;
			break;
			case 'pWex': {	// RTF export target chosen
				entry_ref ref;
				if (message->FindRef("directory", &ref) == B_OK) {
					BPath path(&ref);
					BString name;
					message->FindString("name", &name);
					path.Append(WithExtension(name, ".rtf").String());
					BString rtf;
					PW_WriteRTF(&fDoc, &rtf);
					status_t err = WriteStringToFile(path.Path(), rtf);
					if (err != B_OK) {
						BString msg;
						msg.SetToFormat("Could not write %s: %s",
							path.Path(), strerror(err));
						(new BAlert("ProseWriter", msg.String(), "OK"))
							->Go(NULL);
					}
				}
				break;
			}
		case 'pWsV':
			if (fFilePath.Length())
				DoSave(fFilePath);
			else {
				EnsurePanels();
				BString suggested = fFileName.Length() ? fFileName
					: BString("Untitled");
				fPanels->save->SetSaveText(
					WithExtension(suggested, ".prose").String());
				fPanels->save->Show();
			}
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
		case 'pWct':
			if (fView->HasSelection() && be_clipboard->Lock()) {
				BMessage* clip = be_clipboard->Data();
				clip->MakeEmpty();
				fView->Cut(clip);
				be_clipboard->Commit();
				be_clipboard->Unlock();
				UpdateStatusText();
			}
			break;
		case 'pWcp':
			if (fView->HasSelection()) {
				be_clipboard->Lock();
				BMessage* clip = be_clipboard->Data();
				clip->MakeEmpty();
				fView->Copy(clip);
				be_clipboard->Commit();
				be_clipboard->Unlock();
			}
			break;
		case 'pWps':
			if (be_clipboard->Lock()) {
				const BMessage* clip = be_clipboard->Data();
				fView->Paste(clip);
				be_clipboard->Unlock();
			}
			UpdateStatusText();
			break;
		case B_SELECT_ALL:
			fView->Select(0, fDoc.Length());
			break;
		case B_REFS_RECEIVED: {
			// dropped from Tracker: images insert at the caret (all of
			// them); the first non-image opens as a document
			entry_ref ref;
			for (int32 i = 0; message->FindRef("refs", i, &ref) == B_OK;
					i++) {
				BPath path(&ref);
				BString lower = path.Path();
				lower.ToLower();
				bool isImage = HasSuffix(lower, ".png")
					|| HasSuffix(lower, ".jpg")
					|| HasSuffix(lower, ".jpeg")
					|| HasSuffix(lower, ".bmp")
					|| HasSuffix(lower, ".gif")
					|| HasSuffix(lower, ".tiff")
					|| HasSuffix(lower, ".webp");
				if (isImage) {
					BBitmap* bmp = BTranslationUtils::GetBitmap(&ref);
					if (bmp) {
						float column = fLayout.PageSetup().TextWidth();
						float w = bmp->Bounds().Width() + 1;
						float h = bmp->Bounds().Height() + 1;
						if (w > column) {
							h = h * column / w;
							w = column;
						}
						int32 at = fView->CaretOffset();
						fDoc.InsertImage(at, bmp, w, h);
						fView->SetCaret(at + 3, false);
						fView->Relayout();
						continue;
					}
				}
				if (i == 0)
					OpenFile(ref);
			}
			break;
		}
		case TEXT_APPLY_MSG: {
			int32 kind = 0;
			message->FindInt32("what-kind", &kind);
			PWCharFormat fmt = fView->CurrentFormat();
			switch (kind) {
				case 0: fmt.bold = !fmt.bold; break;
				case 1: fmt.italic = !fmt.italic; break;
				case 2: fmt.underline = !fmt.underline; break;
				case 3: fmt.size = fmt.size + 1; break;
				case 4: fmt.size = fmt.size > 5 ? fmt.size - 1 : fmt.size; break;
			}
			fView->ApplyCharFormat(fmt);
			UpdateStatusText();
			break;
		}
		case FAMILY_MSG: {
			PWCharFormat fmt = fView->CurrentFormat();
			const char* family = NULL;
			message->FindString("family", &family);
			if (family)
				strlcpy(fmt.family, family, sizeof(font_family));
			fView->ApplyCharFormat(fmt);
			break;
		}
		case SIZE_MSG: {
			PWCharFormat fmt = fView->CurrentFormat();
			message->FindFloat("size", &fmt.size);
			fView->ApplyCharFormat(fmt);
			break;
		}
		case COLOR_MSG: {
			PWCharFormat fmt = fView->CurrentFormat();
			int32 r = 0, g = 0, b = 0;
			message->FindInt32("red", &r);
			message->FindInt32("green", &g);
			message->FindInt32("blue", &b);
			fmt.color = rgb_color{ (uint8)r, (uint8)g, (uint8)b, 255 };
			fView->ApplyCharFormat(fmt);
			break;
		}
		case ALIGN_MSG: {
			int32 align = 0;
			message->FindInt32("align", &align);
			ApplyAlignment(align);
			break;
		}
		case ZOOM_MSG: {
			float zoom = 1.0f;
			message->FindFloat("zoom", &zoom);
			SetZoom(zoom);
			break;
		}
		case 'pWzi':
		case 'pWzo': {
			static const float kZooms[] = { 0.5f, 0.75f, 1.0f, 1.5f,
				2.0f };
			float current = fView->Zoom();
			int32 index = 2;
			for (int32 i = 0; i < 5; i++)
				if (fabs(kZooms[i] - current) < 0.01f)
					index = i;
			if (message->what == 'pWzi')
				index = index < 4 ? index + 1 : 4;
			else
				index = index > 0 ? index - 1 : 0;
			SetZoom(kZooms[index]);
			break;
		}
		case FIT_WIDTH_MSG: {
			BView* scroll = fView->ScrollView();
			if (scroll)
				SetZoom((scroll->Bounds().Width() - 52)
					/ fLayout.PageSetup().pageWidth);
			break;
		}
		case MARGIN_MSG:
			fView->Relayout();
			fRuler->Invalidate();
			UpdateStatusText();
			break;
		case FIND_BAR_MSG:
			ToggleFindBar(!fFindShown);
			break;
		case B_ESCAPE:
			if (fFindShown)
				ToggleFindBar(false);
			break;
		case PAGE_SETUP_MSG:
			if (fSetupWin == NULL) {
				fSetupWin = new PWPageSetupWindow(this,
					fLayout.PageSetup());
				fSetupWin->Show();
			} else
				fSetupWin->Activate();
			break;
		case HEADER_MSG:
			if (fHeaderWin == NULL) {
				fHeaderWin = new PWHeaderWindow(this, fDoc.HeaderText(),
					fDoc.FooterText());
				fHeaderWin->Show();
			} else {
				fHeaderWin->Show();
				fHeaderWin->Activate();
			}
			break;
		case STYLES_MSG:
			if (fStylesWin == NULL) {
				fStylesWin = new PWStylesWindow(this);
				fStylesWin->Show();
			} else
				fStylesWin->Activate();
			break;
		case APPLY_SETUP_MSG: {
			PWPageSetup setup = fLayout.PageSetup();
			message->FindFloat("w", &setup.pageWidth);
			message->FindFloat("h", &setup.pageHeight);
			message->FindFloat("ml", &setup.marginLeft);
			message->FindFloat("mr", &setup.marginRight);
			message->FindFloat("mt", &setup.marginTop);
			message->FindFloat("mb", &setup.marginBottom);
			fLayout.SetPageSetup(setup);
			fView->Relayout();
			fRuler->Invalidate();
			UpdateStatusText();
			break;
		}
		case APPLY_HEADER_MSG: {
			BString text;
			if (message->FindString("header", &text) == B_OK)
				fDoc.SetHeaderText(text.String());
			if (message->FindString("footer", &text) == B_OK)
				fDoc.SetFooterText(text.String());
			fView->Relayout();
			UpdateStatusText();
			break;
		}
		case STYLE_NEW_MSG: {
			BString name;
			if (message->FindString("name", &name) != B_OK)
				break;
			int32 from, to;
			fView->GetSelection(&from, &to);
			if (to < from) { int32 x = from; from = to; to = x; }
			int32 para, inPara;
			fDoc.Locate(from, &para, &inPara);
			fDoc.AddStyle(name,
				to > from ? fDoc.FormatAt(from)
					: fView->CurrentFormat(),
				fDoc.ParagraphFormat(para));
			if (fStylesWin != NULL)
				fStylesWin->PostMessage('pWls');
			break;
		}
		case STYLE_APPLY_MSG: {
			int32 index = -1;
			if (message->FindInt32("index", &index) == B_OK
				&& index >= 0) {
				int32 from, to;
				fView->GetSelection(&from, &to);
				if (to < from) { int32 x = from; from = to; to = x; }
				fDoc.ApplyStyle(from, to - from, index);
				fView->Relayout();
			}
			break;
		}
		case STYLE_DEL_MSG: {
			int32 index = -1;
			if (message->FindInt32("index", &index) == B_OK && index >= 0) {
				fDoc.RemoveStyle(index);
				if (fStylesWin != NULL)
					fStylesWin->PostMessage('pWls');
			}
			break;
		}
		case 'pWtI': {
			int32 rows = 3, cols = 3;
			bool header = true;
			message->FindInt32("rows", &rows);
			message->FindInt32("cols", &cols);
			message->FindBool("header", &header);
			int32 at = fView->CaretOffset();
			if (fView->HasSelection()) {
				int32 sFrom, sTo;
				fView->GetSelection(&sFrom, &sTo);
				fDoc.Remove(sFrom, sTo - sFrom);
				at = sFrom;
			}
			if (fDoc.InsertTable(at, rows, cols, header) == B_OK) {
				fView->SetCaret(at + rows * (cols - 1) + rows, false);
				fView->Relayout();
				UpdateStatusText();
			}
			break;
		}
		case 'pWtb':
			if (fTableWin == NULL) {
				fTableWin = new PWInsertTableWindow(this);
				fTableWin->Show();
			} else
				fTableWin->Activate();
			break;
		case 'pWRI': case 'pWCI': case 'pWRD': case 'pWCD':
		{
			// table structure edits on the caret's table
			int32 at = fView->CaretOffset();
			int32 newCaret = at;
			status_t err = B_NOT_ALLOWED;
			switch (message->what) {
				case 'pWRI':
					err = fDoc.InsertTableRowAfter(at, &newCaret);
					break;
				case 'pWCI':
					err = fDoc.InsertTableColumnAt(at, &newCaret);
					break;
				case 'pWRD':
					err = fDoc.DeleteTableRow(at, &newCaret);
					break;
				default:
					err = fDoc.DeleteTableColumnAt(at, &newCaret);
					break;
			}
			if (err == B_OK) {
				fView->SetCaret(newCaret, false);
				fView->Relayout();
				UpdateStatusText();
			} else
				beep();
			break;
		}
		case 'pWpl': {
			// a panel died (its own close button or our quit): drop the
			// pointer before it dangles
			void* panel = NULL;
			message->FindPointer("panel", &panel);
			if (panel == (void*)fSetupWin) fSetupWin = NULL;
			if (panel == (void*)fHeaderWin) fHeaderWin = NULL;
			if (panel == (void*)fStylesWin) fStylesWin = NULL;
			if (panel == (void*)fTableWin) fTableWin = NULL;
			break;
		}
		case B_ABOUT_REQUESTED:
			// the menu delivers to the window; the AboutRequested() hook
			// lives on the application — forward it (StyledEdit's pattern)
			be_app->PostMessage(B_ABOUT_REQUESTED);
			break;
		case 'pWim':
			// one image panel, created on first use (a new BFilePanel per
			// menu hit leaked a window each time)
			if (fImagePanel == NULL) {
				BMessage* pick = new BMessage('pWif');
				fImagePanel = new BFilePanel(B_OPEN_PANEL,
					new BMessenger(this), NULL, B_FILE_NODE, false, pick);
			}
			fImagePanel->Show();
			break;
		case 'pWif': {
			entry_ref ref;
			if (message->FindRef("refs", &ref) == B_OK) {
				BBitmap* bmp = BTranslationUtils::GetBitmap(&ref);
				if (bmp) {
					float column = fLayout.PageSetup().TextWidth();
					float w = bmp->Bounds().Width() + 1;
					float h = bmp->Bounds().Height() + 1;
					if (w > column) {
						h = h * column / w;
						w = column;
					}
					int32 at = fView->CaretOffset();
					if (fView->HasSelection()) {
						int32 sFrom, sTo;
						fView->GetSelection(&sFrom, &sTo);
						fDoc.Remove(sFrom, sTo - sFrom);
						at = sFrom;
					}
					fDoc.InsertImage(at, bmp, w, h);
					fView->SetCaret(at + 3, false);
					fView->Relayout();
					UpdateStatusText();
				} else
					(new BAlert("ProseWriter",
						"That file could not be read as an image.",
						"OK"))->Go(NULL);
			}
			break;
		}
		case 'pWsc':
			fView->SetSpellEnabled(fSpellItem->IsMarked()
				&& fSpell && fSpell->Loaded());
			UpdateStatusText();
			break;
		case 'pWpq': {
			// Print to PDF: one panel, pre-filled with the document's name
			if (fPdfPanel == NULL) {
				BMessage* pick = new BMessage('pWpF');
				fPdfPanel = new BFilePanel(B_SAVE_PANEL,
					new BMessenger(this), NULL, B_FILE_NODE, false, pick);
			}
			BString suggested = fFileName.Length() ? fFileName : "Untitled";
			fPdfPanel->SetSaveText(
				WithExtension(suggested, ".pdf").String());
			fPdfPanel->Show();
			break;
		}
		case 'pWpF': {	// PDF target chosen
			entry_ref ref;
			if (message->FindRef("directory", &ref) == B_OK) {
				BPath path(&ref);
				BString name;
				message->FindString("name", &name);
				path.Append(WithExtension(name, ".pdf").String());
				ExportPDF(path.Path());
			}
			break;
		}
		case PRINT_MSG:
			Print();
			break;
		case 'pWst': {
			// a set, forwarded from the app looper (data carried as a
			// plain field; the window applies it on its own thread)
			BString prop;
			BString data;
			if (message->FindString("pwprop", &prop) == B_OK
				&& message->FindString("data", &data) == B_OK) {
				BMessage setter(B_SET_PROPERTY);
				setter.AddString("data", data);
				HandleScriptingForWindow(this, &setter, prop.String());
			}
			break;
		}
		case 'pWda': {
			BMessage* args = message;
			BString text;
			if (args->FindString("header", &text) == B_OK)
				fDoc.SetHeaderText(text.String());
			if (args->FindString("footer", &text) == B_OK)
				fDoc.SetFooterText(text.String());
			BString paper;
			if (args->FindString("paper", &paper) == B_OK) {
				PWPageSetup setup = fLayout.PageSetup();
				for (int i = 0; kPapers[i].name; i++) {
					if (!strcasecmp(kPapers[i].name, paper.String())) {
						setup.pageWidth = kPapers[i].width;
						setup.pageHeight = kPapers[i].height;
						break;
					}
				}
				bool landscape = false;
				args->FindBool("landscape", &landscape);
				if (landscape && setup.pageWidth < setup.pageHeight) {
					float t = setup.pageWidth;
					setup.pageWidth = setup.pageHeight;
					setup.pageHeight = t;
				}
				fLayout.SetPageSetup(setup);
			}
			if (args->FindString("seed", &text) == B_OK)
				fDoc.Insert(0, text.String(), NULL);
			bool seedTable = false;
			if (args->FindBool("seedTable", &seedTable) == B_OK
				&& seedTable) {
				fDoc.Insert(0, "Widget\035Qty\035Price", NULL);
				fDoc.SplitPara(10);
				fDoc.Insert(11, "bolt\03512\0350.30", NULL);
				fDoc.SplitPara(18);
				fDoc.Insert(19, "nut\035144\0350.05", NULL);
				fDoc.SplitPara(27);
				fDoc.Insert(28, "A table, as paragraphs.", NULL);
			}
			BString openPath;
			if (args->FindString("openPath", &openPath) == B_OK) {
				entry_ref ref;
				if (get_ref_for_path(openPath.String(), &ref) == B_OK) {
					BMessage open('pWop');
					open.AddRef("refs", &ref);
					PostMessage(&open);
				}
			}
			BString imagePath;
			if (args->FindString("seedImage", &imagePath) == B_OK) {
				BBitmap* bmp = BTranslationUtils::GetBitmap(imagePath);
				if (bmp) {
					float column = fLayout.PageSetup().TextWidth();
					float w = bmp->Bounds().Width() + 1;
					float h = bmp->Bounds().Height() + 1;
					if (w > column) {
						h = h * column / w;
						w = column;
					}
					fDoc.InsertImage(0, bmp, w, h);
					fDoc.Insert(3, " The ProseWriter logo, inline.", NULL);
				}
			}
			fView->SetCaret(0, false);
			fView->Relayout();
			UpdateStatusText();
			bool doPrint = false;
			if (args->FindBool("print", &doPrint) == B_OK && doPrint)
				Print();
			break;
		}
		case RECENT_MSG: {
			BString path;
			if (message->FindString("path", &path) == B_OK) {
				entry_ref ref;
				if (get_ref_for_path(path.String(), &ref) == B_OK)
					OpenFile(ref);
			}
			break;
		}
		case FIND_FIELD_MSG:
		case FIND_NEXT_MSG:
			FindNext();
			break;
		case REPLACE_FIND_MSG:
			ReplaceAndFind();
			break;
		case REPLACE_ALL_MSG:
			ReplaceAll();
			break;
		default:
			BWindow::MessageReceived(message);
	}
}

status_t
PWWindow::OpenFile(const entry_ref& ref)
{
	// Opening over unsaved changes must be a decision, not a surprise.
	if (fDoc.IsModified()) {
		BAlert* alert = new BAlert("ProseWriter",
			"The current document has unsaved changes. Open anyway?",
			"Cancel", "Discard changes", "Open and save first",
			B_WIDTH_AS_USUAL, B_OFFSET_SPACING, B_WARNING_ALERT);
		int32 choice = alert->Go();
		if (choice == 0)
			return B_CANCELED;
		if (choice == 2) {
			if (fFilePath.Length()) {
				DoSave(fFilePath);
				if (fDoc.IsModified())
					return B_CANCELED;	// the save failed; do not discard
			} else {
				EnsurePanels()->save->Show();
				return B_CANCELED;	// save via the panel, then open again
			}
		}
	}
	BPath path(&ref);
	BString lower = path.Path();
	lower.ToLower();
	status_t err = B_ERROR;
	bool handled = false;
	auto loadRtf = [&]() -> status_t {
		BString rtf;
		status_t e = ReadFileToString(path.Path(), &rtf);
		if (e == B_OK) {
			PWDocument fresh;
			e = PW_LoadRTF(&fresh, rtf.String());
			if (e == B_OK)
				AdoptDocument(&fDoc, &fresh);
		}
		return e;
	};
	auto loadText = [&]() -> status_t {
		BString text;
		status_t e = ReadFileToString(path.Path(), &text);
		if (e == B_OK) {
			PWDocument fresh;
			fresh.Insert(0, text.String(), NULL);
			AdoptDocument(&fDoc, &fresh);
		}
		return e;
	};
	switch (SniffDocumentKind(path.Path())) {
		case PW_KIND_NATIVE:
			err = fDoc.LoadFromFile(path.Path());
			handled = true;
			break;
		case PW_KIND_RTF:
			err = loadRtf();
			handled = true;
			break;
		default:
			break;
	}
	if (!handled) {
		// content is not recognizable — let the name break the tie
		if (HasSuffix(lower, ".prose"))
			err = fDoc.LoadFromFile(path.Path());
		else if (HasSuffix(lower, ".rtf"))
			err = loadRtf();
		else
			err = loadText();
	}
	if (err != B_OK) {
		BAlert* alert = new BAlert("ProseWriter",
			"ProseWriter could not open that file.", "OK");
		alert->Go();
		return err;
	}
	fFilePath = path.Path();
	fFileName = path.Leaf();
	AddRecentFile(fFilePath.String());
	fView->SetCaret(0, false);
	fView->Relayout();
	UpdateStatusText();
	return B_OK;
}

void
PWWindow::DoSave(const BString& pathStr)
{
	status_t err = fDoc.SaveToFile(pathStr.String());
	if (err == B_OK) {
		fFilePath = pathStr;
		AddRecentFile(pathStr.String());
		fFileName = BPath(pathStr.String()).Leaf();
		fDoc.SavedClean();
		UpdateTitle();
		// Type the file on save: without BEOS:TYPE, Tracker sees an
		// untyped blob and double-click/open-with cannot find us (the
		// sniffer needs mimeset otherwise).
		BNode node(pathStr.String());
		BString docType("application/x-vnd.prose.ProseWriter-doc");
		if (node.InitCheck() == B_OK)
			node.WriteAttrString("BEOS:TYPE", &docType);
	} else {
		// A failed save is never silent — the user must know before they
		// quit and answer "Don't save".
		BString msg;
		msg.SetToFormat("Could not save %s: %s", pathStr.String(),
			strerror(err));
		(new BAlert("ProseWriter", msg.String(), "OK"))->Go(NULL);
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
			if (fFilePath.Length()) {
				DoSave(fFilePath);
				if (fDoc.IsModified())
					return false;	// the save failed: keep the window
			} else {
				// Save needs the panel; remember the close intent — when
				// the panel save completes (or is cancelled) we finish it
				EnsurePanels()->save->Show();
				fQuitPending = true;
				return false;
			}
		}
	}
	// remember where we were; the next window opens here
	{
		BFile out;
		if (out.SetTo(FrameSettingsPath().Path(),
				B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE) == B_OK) {
			BString rect;
			rect.SetToFormat("%g %g %g %g\n", Frame().left, Frame().top,
				Frame().right, Frame().bottom);
			out.Write(rect.String(), rect.Length());
		}
	}
	// Our panels are separate loopers; they must not outlive their owner
	// (their fOwner would dangle). They announce their own death with
	// 'pWpl'; these are the deaths we cause.
	if (fSetupWin != NULL) { fSetupWin->PostMessage(B_QUIT_REQUESTED); fSetupWin = NULL; }
	if (fHeaderWin != NULL) { fHeaderWin->PostMessage(B_QUIT_REQUESTED); fHeaderWin = NULL; }
	if (fStylesWin != NULL) { fStylesWin->PostMessage(B_QUIT_REQUESTED); fStylesWin = NULL; }
	if (fTableWin != NULL) { fTableWin->PostMessage(B_QUIT_REQUESTED); fTableWin = NULL; }
	BMessage closed('pWwc');
	closed.AddPointer("win", this);
	be_app_messenger.SendMessage(&closed);	// window closed
	return true;
}

// -------------------------------------------------------------------- app --
// Scripting suite, the SerialApp pattern: a property_info table, a
// BPropertyInfo, GetSupportedSuites advertising it, ResolveSpecifier
// claiming our properties, and FindMatch dispatch in MessageReceived.
// (Note: no PopSpecifier — popping breaks delivery-time reads.)
static property_info sPWProperties[] = {
	{ "Text",
		{ B_GET_PROPERTY, B_SET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, B_DIRECT_SPECIFIER, 0 },
		"get or set the document text", 0, { B_STRING_TYPE } },
	{ "Header",
		{ B_GET_PROPERTY, B_SET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, B_DIRECT_SPECIFIER, 0 },
		"get or set the header pattern ({page}, {pages})", 0,
		{ B_STRING_TYPE } },
	{ "Footer",
		{ B_GET_PROPERTY, B_SET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, B_DIRECT_SPECIFIER, 0 },
		"get or set the footer pattern ({page}, {pages})", 0,
		{ B_STRING_TYPE } },
	{ "Selection",
		{ B_GET_PROPERTY, B_SET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, B_DIRECT_SPECIFIER, 0 },
		"get or set the selection as from-to", 0, { B_STRING_TYPE } },
	{ "Modified",
		{ B_GET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"is the document modified", 0, { B_INT32_TYPE } },
	{ "WordCount",
		{ B_GET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"number of words in the document", 0, { B_INT32_TYPE } },
	{ "Frame",
		{ B_GET_PROPERTY, B_SET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, B_DIRECT_SPECIFIER, 0 },
		"get or set the window frame as \"l t r b\"", 0, { B_STRING_TYPE } },
	{ "Version",
		{ B_GET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"version and release date", 0, { B_STRING_TYPE } },
	{ "Save",
		{ B_EXECUTE_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"save the document (data: optional path)", 0, { B_STRING_TYPE } },
	{ "Title",
		{ B_GET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"the window title", 0, { B_STRING_TYPE } },
	{ "Quit",
		{ B_EXECUTE_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"close every window and quit", 0, { 0 } },
	{ "Activate",
		{ B_EXECUTE_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"bring the window forward and focus the page (harness)", 0, { 0 } },
	{ "PDF",
		{ B_EXECUTE_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"print to a PDF file (data: output path)", 0, { B_STRING_TYPE } },
	{ "Open",
		{ B_EXECUTE_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"open a document file (data: path)", 0, { B_STRING_TYPE } },
	{ 0 }
};

const BPropertyInfo kPWScriptingProperties(sPWProperties);

static BPath
FrameSettingsPath()
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) == B_OK) {
		path.Append("ProseWriter");
		create_directory(path.Path(), 0755);
		path.Append("frame");
	}
	return path;
}

class PWApp : public BApplication {
public:
			PWApp()
				:
				BApplication("application/x-vnd.prose.ProseWriter")
			{
			}

	status_t GetSupportedSuites(BMessage* message) override
	{
		message->AddString("suites", "suite/x-vnd.prose.ProseWriter");
		message->AddFlat("messages", &kPWScriptingProperties);
		return BApplication::GetSupportedSuites(message);
	}

	BHandler* ResolveSpecifier(BMessage* message, int32 index,
		BMessage* specifier, int32 what, const char* property) override
	{
		if (kPWScriptingProperties.FindMatch(message, index, specifier,
				what, property) >= 0)
			return this;
		return BApplication::ResolveSpecifier(message, index, specifier,
			what, property);
	}

	void	AboutRequested() override
	{
		// BApplication routes B_ABOUT_REQUESTED here (DeskCalc pattern);
		// it never reaches MessageReceived
		if (fAbout != NULL)
			fAbout->Activate();
		else if (DocWindow() != NULL) {
			fAbout = new PWAboutWindow(DocWindow(), be_app);
			fAbout->Show();
		}
	}

	void	ReadyToRun() override
	{
		RegisterDocumentType();
		BScreen screen(B_MAIN_SCREEN_ID);
		BRect avail = screen.Frame().InsetByCopy(40, 36);
		float w = std::min(avail.Width(), 900.0f);
		float h = std::min(avail.Height(), 760.0f);
		BRect frame(avail.left, avail.top, avail.left + w, avail.top + h);
		// remember where the last window was, if it fits this screen
		BString saved;
		BFile file;
		if (file.SetTo(FrameSettingsPath().Path(), B_READ_ONLY) == B_OK) {
			char buffer[128];
			ssize_t n = file.Read(buffer, sizeof(buffer) - 1);
			if (n > 0) {
				buffer[n] = 0;
				saved = buffer;
			}
		}
		float l, t2, r, b;
		if (sscanf(saved.String(), "%f %f %f %f", &l, &t2, &r, &b) == 4) {
			BRect remember(l, t2, r, b);
			if (remember.Width() > 380 && remember.Height() > 300
				&& remember.Intersects(screen.Frame())) {
				if (remember.right > screen.Frame().right - 20)
					remember.OffsetBy(screen.Frame().right - 20
						- remember.right, 0);
				if (remember.bottom > screen.Frame().bottom - 20)
					remember.OffsetBy(0, screen.Frame().bottom - 20
						- remember.bottom);
				if (remember.left >= screen.Frame().left - 5
					&& remember.top >= screen.Frame().top - 5)
					frame = remember;
			}
		}
		PWWindow* window = new PWWindow(frame, "Untitled");
		window->Show();
		if (gHeader || gFooter || gPaper || gLandscape || gSeed
			|| gSeedImage || gSeedTable || gOpenPath)
			ApplyWindowArgs(window);
		// Without a preferred handler the looper answers scripting itself.
		SetPreferredHandler(this);
	}

	void	MessageReceived(BMessage* message) override
	{
		if (message->what == B_REFS_RECEIVED && DocWindow() != NULL) {
			// launched with a document, or files dropped on the app
			DocWindow()->PostMessage(message);
		}
		if (message->HasSpecifiers() && DocWindow() != NULL) {
			BMessage spec;
			int32 what = 0;
			int32 index = 0;
			const char* prop = NULL;
			if (message->GetCurrentSpecifier(&index, &spec, &what,
					&prop) == B_OK
				&& kPWScriptingProperties.FindMatch(message, index, &spec,
					what, prop) >= 0) {
				if (message->what == B_SET_PROPERTY) {
					// Sets mutate window-owned state: a plain private
					// message carries the work to the window looper
					// (specifier stacks die in the window's dispatcher).
					// The original message rides along so the reply
					// goes back to the sender from the right thread.
					BMessage fwd('pWst');
					fwd.AddString("pwprop", prop);
					BString data;
					if (message->FindString("data", &data) == B_OK)
						fwd.AddString("data", data);
					DocWindow()->PostMessage(&fwd);
					// The window applies asynchronously; this ack is
					// immediate (a get straight after may race it).
					ReplyString(message, "");
				} else {
					PWWindow* window = DocWindow();
					window->Lock();
					HandleScriptingForWindow(window, message, prop);
					window->Unlock();
				}
				return;
			}
		}
		switch (message->what) {
			case 'pWnw': {
				BRect frame(80, 60, 860, 940);
				if (DocWindow() != NULL)
					frame = DocWindow()->Frame().OffsetByCopy(24, 24);
				BScreen screen(B_MAIN_SCREEN_ID);
				if (frame.right > screen.Frame().right
					|| frame.bottom > screen.Frame().bottom)
					frame.OffsetTo(screen.Frame().left + 40,
						screen.Frame().top + 40);
				PWWindow* win = new PWWindow(frame, "Untitled");
				win->Show();
				break;
			}
			case 'pWwc': {
				// a document window closed — count the REMAINING ones.
				// (Windows, not BWindows: hidden panels and the about box
				// must not keep a windowless app alive, and the closing
				// window itself may still be in the list.)
				void* closing = NULL;
				message->FindPointer("win", &closing);
				int32 docs = 0;
				for (int32 i = CountWindows() - 1; i >= 0; i--) {
					BWindow* w = WindowAt(i);
					if (w != NULL && w != (BWindow*)closing
						&& dynamic_cast<PWWindow*>(w) != NULL)
						docs++;
				}
				if (docs == 0)
					Quit();	// last document window gone
				break;
			}
			case 'pWaq':
				fAbout = NULL;
				break;
			default:
				BApplication::MessageReceived(message);
		}
	}

	bool	QuitRequested() override
	{
		// App-level quit (Deskbar, Alt+Q aimed at be_app) asks every
		// document window; each runs its own save guard, and the last one
		// to close ends the app through 'pWwc'. We stay alive meanwhile.
		bool any = false;
		for (int32 i = CountWindows() - 1; i >= 0; i--) {
			PWWindow* w = dynamic_cast<PWWindow*>(WindowAt(i));
			if (w != NULL) {
				w->PostMessage(B_QUIT_REQUESTED);
				any = true;
			}
		}
		return !any;
	}

private:
	// F4 (sprint 8's open item): install the document MIME type so Tracker
	// can type .prose files by CONTENT — a flattened BMessage ("HMF1")
	// carrying our 'pWd&' what-code — and open them with us. Idempotent;
	// runs on every launch so the sniffer rule ships with the app.
	void	RegisterDocumentType()
	{
		BMimeType docType("application/x-vnd.prose.ProseWriter-doc");
		if (docType.InitCheck() != B_OK)
			return;
		docType.Install();
		docType.SetShortDescription("ProseWriter document");
		docType.SetLongDescription("ProseWriter word processor document");
		status_t err = docType.SetSnifferRule(
			"1.0 ([0:3] \"HMF1\") ([4:7] \"&dWp\")");
		if (err != B_OK)
			fprintf(stderr, "ProseWriter: sniffer rule rejected: %s\n",
				strerror(err));
		docType.SetPreferredApp("application/x-vnd.prose.ProseWriter");
	}

	// A document window, any document window. The app must not cache one:
	// a cached pointer dangles the moment that window closes while others
	// remain (File ▸ New, then closing the new one, used to free it).
	PWWindow*	DocWindow()
	{
		for (int32 i = CountWindows() - 1; i >= 0; i--) {
			PWWindow* w = dynamic_cast<PWWindow*>(WindowAt(i));
			if (w != NULL)
				return w;
		}
		return NULL;
	}

	PWAboutWindow* fAbout = NULL;
};

// --------------------------------------------------------------- selftest --
static int
SelfTest()
{
	struct Case {
		const char* name;
		bool ok;
	};
	std::vector<Case> cases;
	bool all = true;

	#define CHECK(label, cond) { bool ok_ = (cond); \
		cases.push_back(Case{ label, ok_ }); \
		if (!ok_) all = false; }

		printf("block: model\n"); fflush(stdout);
	{
		PWDocument doc;
		CHECK("empty document has one paragraph", doc.CountParagraphs() == 1);
		doc.Insert(0, "Hello", NULL);
		doc.Insert(5, " world", NULL);
		CHECK("append works", strcmp(doc.PlainText(), "Hello world") == 0);
		doc.Remove(0, 6);
		doc.Undo();
		CHECK("undo restore text", strcmp(doc.PlainText(), "Hello world") == 0);
		doc.Undo();
		CHECK("second undo", strcmp(doc.PlainText(), "Hello") == 0);
		doc.Redo();
		CHECK("redo", strcmp(doc.PlainText(), "Hello world") == 0);
		doc.Insert(5, "\n", NULL);
		CHECK("newline splits paragraphs", doc.CountParagraphs() == 2);
		doc.Undo();
		CHECK("undo split", doc.CountParagraphs() == 1);

		PWCharFormat fmt = doc.FormatAt(0);
		fmt.bold = true;
		doc.ApplyFormat(0, 5, fmt);
		CHECK("format applied", doc.FormatAt(2).bold);
		BMessage saved;
		doc.SaveToMessage(&saved);
		PWDocument doc2;
		doc2.LoadFromMessage(&saved);
		CHECK("round trip text",
			strcmp(doc2.PlainText(), doc.PlainText()) == 0);
		CHECK("round trip bold", doc2.FormatAt(2).bold);

		PWLayout layout(&doc);
		PWPageSetup setup;
		layout.SetPageSetup(setup);
		layout.Layout();
		CHECK("layout makes a page", layout.CountPages() >= 1);
		BPoint xy;
		float h;
		CHECK("offset 0 has a caret", layout.OffsetToXY(0, &xy, &h));
		CHECK("caret x at left margin", xy.x == setup.marginLeft);
	}

		printf("block: search\n"); fflush(stdout);
	{
		// Search and replace over the plain text.
		PWDocument doc;
		doc.Insert(0, "one two three two one", NULL);
		int32 len = 0;
		CHECK("find next finds first",
			doc.FindNext("two", 0, true, false, &len) == 4 && len == 3);
		CHECK("find next case-sensitive miss",
			doc.FindNext("Two", 0, true, false, &len) == -1);
		CHECK("find next case-insensitive hit",
			doc.FindNext("TWO", 0, false, false, &len) == 4);
		CHECK("find wraps", doc.FindNext("one", 14, true, true, &len) == 18);
		CHECK("replace all count", doc.ReplaceAll("two", "TWO", true) == 2);
		CHECK("replace all result",
			strcmp(doc.PlainText(), "one TWO three TWO one") == 0);
	}

		printf("block: rtf\n"); fflush(stdout);
	{
		// RTF round trip: text, bold/italic/underline, size, colour,
		// alignment, multiple paragraphs.
		PWDocument doc;
		doc.Insert(0, "Plain ", NULL);
		PWCharFormat bold = doc.DefaultFormat();
		bold.bold = true;
		bold.size = 18;
		doc.Insert(doc.Length(), "bold18", &bold);
		PWCharFormat red = doc.DefaultFormat();
		red.italic = true;
		red.underline = true;
		red.color = rgb_color{ 200, 0, 0, 255 };
		doc.Insert(doc.Length(), " rediu", &red);
		doc.Insert(doc.Length(), "\nSecond paragraph", NULL);
		PWParaFormat centered;
		centered.alignment = PW_ALIGN_CENTER;
		doc.SetParaFormat(1, centered);

		BString rtf;
		CHECK("write rtf", PW_WriteRTF(&doc, &rtf) == B_OK
			&& rtf.FindFirst("\\rtf1") == 1);
		PWDocument loaded;
		CHECK("load rtf", PW_LoadRTF(&loaded, rtf.String()) == B_OK);
		CHECK("rtf text survives",
			strcmp(loaded.PlainText(), doc.PlainText()) == 0);
		CHECK("rtf bold", loaded.FormatAt(7).bold);
		CHECK("rtf size 18", (int)loaded.FormatAt(9).size == 18);
		CHECK("rtf italic+underline",
			loaded.FormatAt(13).italic && loaded.FormatAt(13).underline);
		CHECK("rtf colour", loaded.FormatAt(13).color.red == 200);
		CHECK("rtf alignment",
			loaded.ParagraphFormat(1).alignment == PW_ALIGN_CENTER);
		CHECK("rtf not bold at 0", !loaded.FormatAt(0).bold);
	}

		printf("block: justify\n"); fflush(stdout);
	{
		// Justify: a wrapped justified paragraph fills the column.
		PWDocument doc;
		BString filler;
		for (int i = 0; i < 30; i++)
			filler << "word" << i << " ";
		doc.Insert(0, filler.String(), NULL);
		PWParaFormat justified;
		justified.alignment = PW_ALIGN_JUSTIFY;
		doc.SetParaFormat(0, justified);
		PWLayout layout(&doc);
		PWPageSetup setup;
		layout.SetPageSetup(setup);
		layout.Layout();
		CHECK("justified wraps to many lines", layout.Lines().size() >= 2);
		std::vector<PWLayout::Segment> segs;
		layout.FillSegments(0, &segs);
		CHECK("justified line has word segments", segs.size() > 4);
		float endX = segs.back().x;
		const char* text = doc.ParagraphText(0);
		BFont font(be_plain_font);
		font.SetFamilyAndFace(segs.back().run->format.family, 0);
		font.SetSize(segs.back().run->format.size);
		endX += font.StringWidth(text + segs.back().startPara,
			segs.back().length);
		CHECK("justified line fills column",
			fabs(endX - (setup.marginLeft + setup.TextWidth())) < 2.0f);
		BPoint xy;
		float hh;
		CHECK("caret in justified line",
			layout.OffsetToXY(10, &xy, &hh) && xy.x >= setup.marginLeft - 1
			&& xy.x <= setup.marginLeft + setup.TextWidth() + 2);
	}

		printf("block: headers\n"); fflush(stdout);
	{
		// Header/footer field substitution.
		PWDocument doc;
		doc.SetHeaderText("Report {page} of {pages}");
		doc.SetFooterText("{page}/{pages} end");
		BString h = PWDocument::ComposeHeaderText(doc.HeaderText(), 3, 12);
		CHECK("header fields", h == "Report 3 of 12");
		BString f = PWDocument::ComposeHeaderText(doc.FooterText(), 1, 2);
		CHECK("footer fields", f == "1/2 end");
		BMessage msg;
		doc.SaveToMessage(&msg);
		PWDocument doc2;
		doc2.LoadFromMessage(&msg);
		CHECK("header persists", strcmp(doc2.HeaderText(),
			"Report {page} of {pages}") == 0);
	}

		printf("block: paper\n"); fflush(stdout);
	{
		// Paper size and orientation change the page count.
		PWDocument doc;
		BString filler;
		for (int i = 0; i < 120; i++)
			filler << "filler text here ";
		doc.Insert(0, filler.String(), NULL);
		PWLayout layout(&doc);
		PWPageSetup portrait;
		portrait.pageWidth = 595; portrait.pageHeight = 842;
		layout.SetPageSetup(portrait);
		layout.Layout();
		int32 portraitPages = layout.CountPages();
		PWPageSetup landscape = portrait;
		landscape.pageWidth = 842; landscape.pageHeight = 595;
		layout.SetPageSetup(landscape);
		layout.Layout();
		CHECK("paper swap relayouts", portraitPages >= 1);
		// landscape has a wider column -> fewer or equal pages
		CHECK("landscape not taller", layout.CountPages() <= portraitPages + 1);
	}

		printf("block: binsearch build\n"); fflush(stdout);
	{
		// Binary-search lookups agree with linear truth on a large doc.
		PWDocument doc;
		doc.Insert(0,
			"alpha beta gamma delta epsilon zeta eta theta iota kappa "
			"lambda mu nu xi omicron pi rho sigma tau upsilon phi chi psi "
			"omega. ", NULL);
		for (int i = 0; i < 3000; i++)
			doc.SplitPara((i * 97) % std::max((int32)1, doc.Length()));
		PWLayout layout(&doc);
		layout.SetPageSetup(PWPageSetup());
		layout.Layout();
		int32 total = doc.Length();
		printf("block: binsearch probes\n"); fflush(stdout);
		bool agree = true;
		bool roundTrip = true;
		BPoint xy;
		float hh;
		for (int probe = 0; probe < 200; probe++) {
			int32 off = (probe * 2654435761u) % (total > 0 ? total : 1);
			// truth: linear scan over lines
			int32 truth = -1;
			for (int32 i = 0; i < (int32)layout.Lines().size(); i++) {
				int32 s = layout.Lines()[i].startAbs;
				int32 e = (i + 1 < (int32)layout.Lines().size())
					? layout.Lines()[i + 1].startAbs
					: total + 1;
				if (off >= s && off < e) { truth = i; break; }
			}
			if (truth != layout.LineOfOffset(off))
				agree = false;
			if (layout.OffsetToXY(off, &xy, &hh)) {
				int32 back = layout.XYToOffset(xy);
				// same or an adjacent boundary is acceptable
				if (back != off && back != off + 1 && back != off - 1)
					roundTrip = false;
			}
		}
		CHECK("line lookup matches linear scan", agree);
		CHECK("offset/point round trip", roundTrip);
	}

	{
		// The fix-pass regressions: every one of these caught a real bug.
		printf("block: fixes\n"); fflush(stdout);

		// --- file round trip through the FILE path (atomic save; and the
		// old SaveToFile returned Write()'s byte count, so every save read
		// as a failure to DoSave)
		PWDocument fdoc;
		fdoc.Insert(0, "Save me.\nSecond para", NULL);
		PWCharFormat fbold = fdoc.DefaultFormat();
		fbold.bold = true;
		fdoc.ApplyFormat(0, 4, fbold);
		const char* path = "/tmp/pw-selftest.prose";
		CHECK("save to file returns B_OK", fdoc.SaveToFile(path) == B_OK);
		{
			BEntry tmp(BString(path).Append(".pwtmp").String());
			CHECK("no temp file left behind",
				tmp.InitCheck() != B_OK || !tmp.Exists());
		}
		PWDocument floaded;
		{
			status_t loadErr = floaded.LoadFromFile(path);
			if (loadErr != B_OK)
				printf("load-from-file failed: %s\n", strerror(loadErr));
			CHECK("load from file returns B_OK", loadErr == B_OK);
		}
		CHECK("file round trip text",
			strcmp(floaded.PlainText(), "Save me.\nSecond para") == 0);
		CHECK("file round trip bold", floaded.FormatAt(1).bold);
		// content sniffing decides the loader: a just-saved file must
		// reopen no matter what name the save panel was given
		CHECK("sniff recognizes native", SniffDocumentKind(path)
			== PW_KIND_NATIVE);
		{
			BFile rtfOut;
			if (rtfOut.SetTo("/tmp/pw-sniff.rtf",
					B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE) == B_OK)
				rtfOut.Write("{\\rtf1\\ansi}", 10);
			BFile txtOut;
			if (txtOut.SetTo("/tmp/pw-sniff.txt",
					B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE) == B_OK)
				txtOut.Write("just text", 9);
			CHECK("sniff recognizes rtf",
				SniffDocumentKind("/tmp/pw-sniff.rtf") == PW_KIND_RTF);
			CHECK("sniff falls through to text",
				SniffDocumentKind("/tmp/pw-sniff.txt") == PW_KIND_TEXT);
			CHECK("sniff rejects missing file",
				SniffDocumentKind("/tmp/pw-sniff-none") == PW_KIND_ERROR);
			CHECK("extension appended", WithExtension("report", ".prose")
				== "report.prose");
			CHECK("extension not doubled",
				WithExtension("report.prose", ".prose") == "report.prose");
			CHECK("extension case-insensitive",
				WithExtension("REPORT.PROSE", ".prose") == "REPORT.PROSE");
			CHECK("trailing dot tidied", WithExtension("report.", ".prose")
				== "report.prose");
			remove("/tmp/pw-sniff.rtf");
			remove("/tmp/pw-sniff.txt");
		}
		remove(path);

		// --- format undo: bold/undo/redo, and undo of a format must not
		// eat the text edit before it
		PWDocument bdoc;
		bdoc.Insert(0, "make me bold!", NULL);
		PWCharFormat bb = bdoc.DefaultFormat();
		bb.bold = true;
		bdoc.ApplyFormat(0, 12, bb);
		CHECK("bold applied", bdoc.FormatAt(3).bold);
		bdoc.Undo();
		CHECK("bold undo restores plain", !bdoc.FormatAt(3).bold);
		bdoc.Redo();
		CHECK("bold redo reapplies", bdoc.FormatAt(3).bold);
		PWCharFormat bu = bdoc.FormatAt(0);
		bu.underline = true;
		bdoc.ApplyFormat(0, 3, bu);
		bdoc.Undo();
		CHECK("undo after format keeps text",
			strcmp(bdoc.PlainText(), "make me bold!") == 0
			&& !bdoc.FormatAt(1).underline);

		// --- coalesced typing: undo removes the whole run, redo restores
		// the whole run (redo used to re-insert one character)
		PWDocument tdoc2;
		tdoc2.Insert(0, "a", NULL);
		tdoc2.Insert(1, "b", NULL);
		tdoc2.Insert(2, "c", NULL);
		tdoc2.Undo();
		CHECK("undo coalesced typing clears all", tdoc2.Length() == 0);
		tdoc2.Redo();
		CHECK("redo coalesced typing restores all",
			strcmp(tdoc2.PlainText(), "abc") == 0);

		// --- UTF-8 lives in the model end to end
		PWDocument udoc;
		udoc.Insert(0, "caf\xC3\xA9 na\xC3\xAFve \xE2\x80\x94 ok", NULL);
		CHECK("utf-8 text in model",
			strcmp(udoc.PlainText(), "caf\xC3\xA9 na\xC3\xAFve \xE2\x80\x94 ok")
				== 0);
		CHECK("utf-8 find", udoc.FindNext("\xC3\xA9", 0, true, false, NULL)
			== 3);

		// --- images: move with inserts, survive merges and undo
		PWDocument idoc;
		idoc.Insert(0, "ab\ncd", NULL);
		idoc.InsertImage(4, new BBitmap(BRect(0, 0, 9, 9), B_RGB32),
			20, 10);
		idoc.Insert(3, "x", NULL);	// type before the image
		CHECK("image follows its marker",
			idoc.CountImages() == 1 && idoc.ImageAt(5) != NULL);
		idoc.Remove(2, 2);			// separator + 'x': paragraphs merge
		// "ab" + "c<FFFC>d" — the marker sits at byte 3
		CHECK("image survives paragraph merge",
			idoc.CountImages() == 1 && idoc.ImageAt(3) != NULL);
		idoc.Undo();
		CHECK("image restored by undo of merge",
			idoc.CountImages() == 1 && idoc.ImageAt(5) != NULL);
		idoc.Remove(5, 3);			// delete the marker itself
		CHECK("image deleted with marker", idoc.CountImages() == 0);
		idoc.Undo();
		CHECK("image restored by undo of delete",
			idoc.CountImages() == 1 && idoc.ImageAt(5) != NULL
			&& idoc.ImageAt(5)->bitmap);

		// --- revision-based reuse: same-length mid-paragraph edit
		BString para60;
		for (int i = 0; i < 60; i++)
			para60 << "word" << i << " ";
		PWDocument rdoc;
		rdoc.Insert(0, para60.String(), NULL);
		PWLayout rlayout(&rdoc);
		rlayout.SetPageSetup(PWPageSetup());
		rlayout.Layout();
		int32 at = rdoc.FindNext("word1", 0, true, false, NULL);
		rdoc.Remove(at, 5);
		rdoc.Insert(at, "wod1x", NULL);	// same length, past byte 32
		rlayout.Layout();
		CHECK("same-length mid edit remeasures",
			rlayout.LastMeasuredParagraphs() == 1);
		// bolding the same span must re-measure too
		PWCharFormat rb = rdoc.DefaultFormat();
		rb.bold = true;
		rdoc.ApplyFormat(at, 5, rb);
		rlayout.Layout();
		CHECK("format change remeasures",
			rlayout.LastMeasuredParagraphs() == 1);
		// and a page-setup change re-measures everything, at the new edge
		PWPageSetup narrow = rlayout.PageSetup();
		narrow.marginLeft = 120;
		narrow.marginRight = 120;
		rlayout.SetPageSetup(narrow);
		rlayout.Layout();
		if (rlayout.LastMeasuredParagraphs() != rdoc.CountParagraphs())
			printf("fix-debug: setup remeasure: measured=%d paras=%d\n",
				(int)rlayout.LastMeasuredParagraphs(),
				(int)rdoc.CountParagraphs());
		CHECK("page setup change remeasures",
			rlayout.LastMeasuredParagraphs() == rdoc.CountParagraphs());
		CHECK("rewrap honors new margins",
			fabs(rlayout.Lines()[0].x - 120.0f) < 0.5f);

		// --- table rows follow paragraph shifts (fRows by stable id)
		PWDocument sdoc;
		sdoc.Insert(0, "A\035B\035C", NULL);
		PWLayout slayout(&sdoc);
		slayout.SetPageSetup(PWPageSetup());
		slayout.Layout();
		CHECK("table lays out", slayout.RowAt(0) != NULL
			&& slayout.RowAt(0)->cells.size() == 3);
		sdoc.SplitPara(0);	// a new paragraph above the table
		slayout.Layout();	// incremental
		CHECK("table row follows the shift",
			slayout.RowAt(1) != NULL
			&& slayout.RowAt(1)->cells.size() == 3);

		// --- columns are a property of the whole table: rows with
		// different content and even different cell counts share the
		// same column edges (the old per-row sizing was the defect)
		{
			// three contiguous rows — SplitPara BETWEEN rows, the
			// InsertTable idiom (ParagraphLength excludes separators)
			PWDocument tdoc;
			tdoc.Insert(0, "Name\035Quantity\035Notes", NULL);
			int32 at = tdoc.Length();
			tdoc.SplitPara(at);
			tdoc.Insert(at + 1, "long content in this cell\035x\035z", NULL);
			at = at + 1 + tdoc.ParagraphLength(1);
			tdoc.SplitPara(at);
			tdoc.Insert(at + 1, "short\0351", NULL);
			CHECK("seed is three rows", tdoc.CountParagraphs() == 3);
			PWLayout tlayout(&tdoc);
			tlayout.SetPageSetup(PWPageSetup());
			tlayout.Layout();
			const PWLayout::RowLayout* r0 = tlayout.RowAt(0);
			const PWLayout::RowLayout* r1 = tlayout.RowAt(1);
			const PWLayout::RowLayout* r2 = tlayout.RowAt(2);
			bool aligned = r0 != NULL && r1 != NULL && r2 != NULL;
			if (aligned) {
				for (size_t c = 0; c < r0->cells.size(); c++) {
					if (c < r1->cells.size()
						&& (fabs(r0->cells[c].x - r1->cells[c].x) > 0.01f
							|| fabs(r0->cells[c].width
								- r1->cells[c].width) > 0.01f))
						aligned = false;
					if (c < r2->cells.size()
						&& (fabs(r0->cells[c].x - r2->cells[c].x) > 0.01f
							|| fabs(r0->cells[c].width
								- r2->cells[c].width) > 0.01f))
						aligned = false;
				}
			}
			CHECK("table columns align across rows", aligned);
			CHECK("table column count is the widest row's",
				r0 != NULL && r0->cells.size() == 3
				&& r2 != NULL && r2->cells.size() == 2);

			// --- structure edits
			int32 caret = 0;
			// insert a row below row 0: same cell count, empty
			CHECK("insert row",
				tdoc.InsertTableRowAfter(0, &caret) == B_OK);
			CHECK("inserted row is empty cells",
				tdoc.ParagraphLength(1) == 2
				&& tdoc.ParagraphText(1)[0] == PWDocument::kCellSep);
			tlayout.Layout();
			{
				const PWLayout::RowLayout* nr = tlayout.RowAt(1);
				const PWLayout::RowLayout* orow = tlayout.RowAt(0);
				CHECK("inserted row aligns too", nr != NULL && orow != NULL
					&& !nr->cells.empty() && !orow->cells.empty()
					&& fabs(nr->cells[0].x - orow->cells[0].x) < 0.01f);
			}
			// delete that row again
			CHECK("delete row", tdoc.DeleteTableRow(caret, &caret) == B_OK);
			CHECK("row count back", tdoc.CountParagraphs() == 3);
			// insert a column at the caret's column (0): every row
			int32 row0Len = tdoc.ParagraphLength(0);
			CHECK("insert column",
				tdoc.InsertTableColumnAt(0, &caret) == B_OK);
			CHECK("column inserted in every row",
				tdoc.ParagraphText(0)[0] == PWDocument::kCellSep
				&& tdoc.ParagraphLength(0) == row0Len + 1
				&& tdoc.ParagraphText(1)[0] == PWDocument::kCellSep
				&& tdoc.ParagraphText(2)[0] == PWDocument::kCellSep);
			// delete the column again
			CHECK("delete column",
				tdoc.DeleteTableColumnAt(caret, &caret) == B_OK);
			CHECK("column deletion restores text",
				tdoc.ParagraphLength(0) == row0Len
				&& tdoc.ParagraphText(0)[0] == 'N');
			// guards
			PWDocument ndoc;
			ndoc.Insert(0, "no table here", NULL);
			CHECK("row ops refuse outside tables",
				ndoc.InsertTableRowAfter(0, &caret) == B_NOT_ALLOWED
				&& ndoc.DeleteTableRow(0, &caret) == B_NOT_ALLOWED
				&& ndoc.DeleteTableColumnAt(0, &caret) == B_NOT_ALLOWED);
		}

		// --- RTF: multi-entry font tables, and colour back to black
		const char* multiFontRtf =
			"{\\rtf1\\ansi{\\fonttbl{\\f0 Times New Roman;}{\\f1 Courier;}}"
			"{\\colortbl;\\red200\\green0\\blue0;}"
			"\\f0 plain \\cf1 red \\cf0 black again}";
		PWDocument mfdoc;
		CHECK("multi-font rtf loads",
			PW_LoadRTF(&mfdoc, multiFontRtf) == B_OK);
		CHECK("font names not leaked into text",
			strstr(mfdoc.PlainText(), "Courier") == NULL
			&& strstr(mfdoc.PlainText(), "Times") == NULL);
		CHECK("rtf cf0 returns to black",
			mfdoc.FormatAt(mfdoc.Length() - 1).color.red == 0);
		PWDocument cdoc;
		cdoc.Insert(0, "redblack", NULL);
		PWCharFormat redf = cdoc.DefaultFormat();
		redf.color = rgb_color{ 200, 0, 0, 255 };
		cdoc.ApplyFormat(0, 3, redf);
		BString crtf;
		PW_WriteRTF(&cdoc, &crtf);
		CHECK("writer emits cf0", crtf.FindFirst("\\cf0") >= 0);
		PWDocument cloaded;
		PW_LoadRTF(&cloaded, crtf.String());
		CHECK("export/import black after red",
			cloaded.FormatAt(5).color.red == 0
			&& cloaded.FormatAt(1).color.red == 200);
	}

		printf("block: perf\n"); fflush(stdout);
	{
		// Sprint 4: indents, tabs, spacing, lists.
		printf("block: s4 layout\n"); fflush(stdout);
		PWDocument doc;
		doc.Insert(0, "First line indented then wraps around here we go "
			"with more words to be sure it wraps twice.", NULL);
		PWParaFormat fmt;
		fmt.indentFirst = 36;
		fmt.indentLeft = 18;
		doc.SetParaFormat(0, fmt);
		PWLayout layout(&doc);
		PWPageSetup setup;
		layout.SetPageSetup(setup);
		layout.Layout();
		const PWLayout::Line& first = layout.Lines()[0];
		const PWLayout::Line& second = layout.Lines()[1];
		CHECK("first-line indent shifts line 0",
			fabs(first.x - (setup.marginLeft + fmt.indentLeft
				+ fmt.indentFirst)) < 0.5f);
		CHECK("left indent shifts wrapped lines",
			fabs(second.x - (setup.marginLeft + fmt.indentLeft)) < 0.5f);

		// Tab advance: a tab at 72 jumps the second word there.
		PWDocument tdoc;
		tdoc.Insert(0, "a\tb", NULL);
		PWParaFormat tfmt;
		tfmt.tabs.push_back(PWTab{ 72, PW_TAB_LEFT });
		tdoc.SetParaFormat(0, tfmt);
		PWLayout tlayout(&tdoc);
		tlayout.SetPageSetup(setup);
		tlayout.Layout();
		std::vector<PWLayout::Segment> tsegs;
		tlayout.FillSegments(0, &tsegs);
		CHECK("tab produces three segments", tsegs.size() >= 3);
		if (tsegs.size() >= 3)
			CHECK("tab advances to the stop",
				fabs(tsegs[2].x - (setup.marginLeft + 72)) < 1.0f);

		// Line spacing doubles the used height.
		PWDocument sdoc;
		BString filler;
		for (int i = 0; i < 80; i++)
			filler << "spacing test ";
		sdoc.Insert(0, filler.String(), NULL);
		PWLayout sl1(&sdoc), sl2(&sdoc);
		sl1.SetPageSetup(setup);
		sl2.SetPageSetup(setup);
		sl1.Layout();
		PWParaFormat sfmt;
		sfmt.lineSpacing = 2.0f;
		sdoc.SetParaFormat(0, sfmt);
		sl2.Layout();
		CHECK("spacing grows the layout",
			sl2.Lines().back().y > sl1.Lines().back().y * 1.5f);

		// Numbered lists: sequence restarts after a plain paragraph.
		PWDocument ldoc;
		ldoc.Insert(0, "one", NULL);
		ldoc.Insert(3, "\ntwo", NULL);
		ldoc.Insert(7, "\nbreak\nfour", NULL);
		PWParaFormat lfmt;
		lfmt.listKind = PW_LIST_NUMBER;
		ldoc.SetParaFormat(0, lfmt);
		ldoc.SetParaFormat(1, lfmt);
		ldoc.SetParaFormat(3, lfmt);
		PWLayout llayout(&ldoc);
		llayout.SetPageSetup(setup);
		llayout.Layout();
		CHECK("numbered marker on line 0",
			llayout.Lines()[0].listMark == PW_LIST_NUMBER
			&& llayout.Lines()[0].listSeq == 1);
		CHECK("numbered marker continues",
			llayout.Lines()[1].listMark == PW_LIST_NUMBER
			&& llayout.Lines()[1].listSeq == 2);
		CHECK("numbered restarts after break",
			llayout.Lines()[3].listMark == PW_LIST_NUMBER
			&& llayout.Lines()[3].listSeq == 1);

		// RTF round trip of the new geometry.
		PWDocument rdoc;
		rdoc.Insert(0, "indented", NULL);
		PWParaFormat rfmt;
		rfmt.indentLeft = 28;
		rfmt.indentFirst = -14;
		rfmt.lineSpacing = 1.5f;
		rfmt.spaceAfter = 10;
		rfmt.tabs.push_back(PWTab{ 90, PW_TAB_LEFT });
		rdoc.SetParaFormat(0, rfmt);
		BString rrtf;
		PW_WriteRTF(&rdoc, &rrtf);
		PWDocument rloaded;
		PW_LoadRTF(&rloaded, rrtf.String());
		const PWParaFormat& rf = rloaded.ParagraphFormat(0);
		CHECK("rtf indent left", fabs(rf.indentLeft - 28) < 1.0f);
		CHECK("rtf indent first", fabs(rf.indentFirst + 14) < 1.0f);
		CHECK("rtf line spacing", fabs(rf.lineSpacing - 1.5f) < 0.05f);
		CHECK("rtf space after", fabs(rf.spaceAfter - 10) < 1.0f);
		CHECK("rtf tab stop", rf.tabs.size() == 1
			&& fabs(rf.tabs[0].x - 90) < 1.0f);
	}

	{
		// Spell checker, 1997-style.
		printf("block: spell\n"); fflush(stdout);
		PWSpellChecker spell;
		spell.AddWord("hello");
		spell.AddWord("world");
		spell.AddWord("don't");
		spell.AddWord("cat");
		spell.AddWord("the");
		spell.AddWord("says");
		spell.AddWord("it");
		CHECK("known word passes", spell.IsCorrect("hello"));
		CHECK("case-insensitive", spell.IsCorrect("HELLO"));
		CHECK("typo flagged", !spell.IsCorrect("helo"));
		CHECK("contraction", spell.IsCorrect("don't"));
		CHECK("possessive", spell.IsCorrect("cat's"));
		CHECK("digits pass", spell.IsCorrect("mp3"));
		CHECK("single letters pass", spell.IsCorrect("a"));
		CHECK("hyphen parts", spell.IsCorrect("hello-world"));
		CHECK("hyphen typo", !spell.IsCorrect("hello-wrold"));
		int32 ws = 0, wl = 0;
		PWSpellChecker::CaretWordRange("one two three", 5, &ws, &wl);
		CHECK("caret word mid", ws == 4 && wl == 3);
		PWSpellChecker::CaretWordRange("one two three", 3, &ws, &wl);
		CHECK("caret word at end boundary", ws == 0 && wl == 3);
		PWSpellChecker::CaretWordRange("one two three", 7, &ws, &wl);
		CHECK("caret word at start boundary", ws == 4 && wl == 3);
		PWSpellChecker::CaretWordRange("hello,  don't", 7, &ws, &wl);
		CHECK("caret on separator", wl == 0);
		// a comma directly after a word still exempts that word: the
		// user might be about to keep typing it
		PWSpellChecker::CaretWordRange("hello,", 5, &ws, &wl);
		CHECK("caret after word grabs word", ws == 0 && wl == 5);
		PWSpellChecker::CaretWordRange("don't", 5, &ws, &wl);
		CHECK("caret word with apostrophe", ws == 0 && wl == 5);

		std::vector<std::pair<int32, int32>> ranges;
		spell.ScanParagraph("The wrold says hello, don't it? mp3", &ranges);
		CHECK("one misspelling found", ranges.size() == 1);
		if (ranges.size() == 1) {
			BString word;
			BString("The wrold says hello, don't it? mp3").CopyInto(word,
				ranges[0].first, ranges[0].second);
			CHECK("misspelling is 'wrold'", word == "wrold");
		}
	}

	{
		// Named styles.
		printf("block: styles\n"); fflush(stdout);
		PWDocument doc;
		doc.Insert(0, "style me", NULL);
		PWCharFormat chr = doc.DefaultFormat();
		chr.bold = true;
		chr.size = 18;
		PWParaFormat para;
		para.alignment = PW_ALIGN_CENTER;
		doc.AddStyle("Heading", chr, para);
		CHECK("style stored", doc.CountStyles() == 1
			&& doc.StyleNamed("Heading") != NULL);
		doc.ApplyStyle(0, doc.Length(), 0);
		CHECK("style applied char", doc.FormatAt(1).bold
			&& (int)doc.FormatAt(1).size == 18);
		CHECK("style applied para",
			doc.ParagraphFormat(0).alignment == PW_ALIGN_CENTER);
		doc.AddStyle("Heading", doc.DefaultFormat(), PWParaFormat());
		CHECK("style replaced by name", doc.CountStyles() == 1
			&& !doc.StyleNamed("Heading")->chr.bold);
		BMessage msg;
		doc.SaveToMessage(&msg);
		PWDocument doc2;
		doc2.LoadFromMessage(&msg);
		CHECK("style persists", doc2.CountStyles() == 1
			&& doc2.StyleNamed("Heading") != NULL);
		doc2.RemoveStyle(0);
		CHECK("style removed", doc2.CountStyles() == 0);
	}

	{
		// Inline images.
		printf("block: images\n"); fflush(stdout);
		BBitmap* bmp = new BBitmap(BRect(0, 0, 19, 9), B_RGB32, true);
		CHECK("bitmap for test", bmp && bmp->IsValid());
		if (bmp) {
			uint8* bits = (uint8*)bmp->Bits();
			for (int32 y = 0; y < 10; y++)
				for (int32 x = 0; x < 20; x++) {
					uint8* px = bits + y * bmp->BytesPerRow() + x * 4;
					px[0] = x * 12; px[1] = y * 25; px[2] = 200;
					px[3] = 255;
				}
		}
		PWDocument doc;
		doc.Insert(0, "before  after", NULL);
		status_t err = doc.InsertImage(7, bmp, 40, 20);
		CHECK("image inserted", err == B_OK && doc.CountImages() == 1);
		CHECK("marker in text", doc.Length() == 13 + 3);
		CHECK("image found at offset", doc.ImageAt(7) != NULL);
		doc.Remove(3, 2);
		CHECK("image survives unrelated delete",
			doc.CountImages() == 1 && doc.ImageAt(5) != NULL);
		doc.Remove(4, 3);
		CHECK("image deleted with marker", doc.CountImages() == 0);
		doc.InsertImage(2, new BBitmap(BRect(0, 0, 19, 9), B_RGB32, true),
			40, 20);
		BMessage msg;
		doc.SaveToMessage(&msg);
		PWDocument doc2;
		doc2.LoadFromMessage(&msg);
		CHECK("image persists", doc2.CountImages() == 1
			&& doc2.ImageAt(2) != NULL && doc2.ImageAt(2)->widthPt == 40);
		// layout: the image line grows to the image height
		PWLayout layout(&doc);
		layout.SetPageSetup(PWPageSetup());
		layout.Layout();
		bool tallLine = false;
		for (const PWLayout::Line& l : layout.Lines())
			if (l.height >= 20)
				tallLine = true;
		CHECK("image grows its line", tallLine);
		std::vector<PWLayout::Segment> segs;
		layout.FillSegments(layout.LineOfOffset(2), &segs);
		bool imageSeg = false;
		for (const PWLayout::Segment& s : segs)
			if (s.isImage && s.imageW == 40)
				imageSeg = true;
		CHECK("image segment emitted", imageSeg);
	}

	{
		// Tables: rows are paragraphs, cells split on 0x1D.
		printf("block: tables\n"); fflush(stdout);
		PWDocument doc;
		doc.Insert(0, "Widget\035Qty\035Price", NULL);
		doc.SplitPara(16);	// after "Price": row 2 starts clean
		doc.Insert(17, "bolt\03512\0350.30", NULL);
		CHECK("rows detected", doc.CountParagraphs() == 2
			&& strstr(doc.PlainText(), "\035") != NULL);
		PWLayout layout(&doc);
		layout.SetPageSetup(PWPageSetup());
		layout.Layout();
		CHECK("two table lines", layout.Lines().size() == 2
			&& layout.Lines()[0].table && layout.Lines()[1].table);
		const PWLayout::RowLayout* row = layout.RowAt(0);
		CHECK("row layout exists", row != NULL);
		if (row) {
			CHECK("three cells", row->cells.size() == 3);
			float sum = 0;
			for (const PWLayout::CellLayout& c : row->cells)
				sum += c.width;
			CHECK("columns fill the column",
				fabs(sum - layout.PageSetup().TextWidth()) < 2.0f);
		}
		// caret round trip inside cells
		BPoint xy;
		float h;
		bool ok = true;
		bool confined = true;
		for (int32 off = 0; off <= doc.Length(); off++) {
			if (!layout.OffsetToXY(off, &xy, &h)) {
				ok = false;
				break;
			}
			int32 back = layout.XYToOffset(xy);
			if (back != off && back != off + 1 && back != off - 1) {
				ok = false;
				break;
			}
		}
		CHECK("caret round trip in table", ok);
		// every caret offset of a row lands inside the row's cells
		// (the caret may never sit on a cell's left rule)
		const PWLayout::RowLayout* row0 = layout.RowAt(0);
		if (row0) {
			for (int32 off = 0; off <= doc.ParagraphLength(0); off++) {
				if (!layout.OffsetToXY(off, &xy, &h))
					continue;
				bool inside = false;
				for (const PWLayout::CellLayout& cell : row0->cells)
					if (xy.x >= cell.x - 0.5
						&& xy.x <= cell.x + cell.width + 0.5)
						inside = true;
				if (!inside) {
					confined = false;
					break;
				}
			}
		}
		CHECK("caret confined to cells", confined);
		// Enter on the last cell leaves the table (plain paragraph)
		doc.SplitPara(doc.Length());
		CHECK("split adds a row/paragraph",
			doc.CountParagraphs() == 3
			&& !layout.IsTableParagraph(2));
		// Tab key inserts a separator (model side: Insert of 0x1D)
		doc.Insert(4, "\035", NULL);
		int32 sepsNow = 0;
		for (int32 c = 0; c < doc.ParagraphLength(0); c++)
			if (doc.ParagraphText(0)[c] == PWLayout::kCellSep)
				sepsNow++;
		CHECK("separator insertion adds a cell", sepsNow == 3);
		PWLayout layout2(&doc);
		layout2.SetPageSetup(PWPageSetup());
		layout2.Layout();
		CHECK("row now has four cells", layout2.RowAt(0) != NULL
			&& layout2.RowAt(0)->cells.size() == 4);
		// persistence: separators survive the .prose round trip
		BMessage msg;
		doc.SaveToMessage(&msg);
		PWDocument doc2;
		doc2.LoadFromMessage(&msg);
		CHECK("separators persist", strstr(doc2.PlainText(), "\035")
			!= NULL);
	}

	{
		// The GUI path: empty doc laid out, THEN a table inserted —
		// the incremental relayout must produce table lines too.
		printf("block: table incremental\n"); fflush(stdout);
		PWDocument doc;
		PWLayout layout(&doc);
		layout.SetPageSetup(PWPageSetup());
		layout.Layout();	// empty
		doc.Insert(0, "A\035B\035C", NULL);
		doc.SplitPara(5);
		doc.Insert(6, "1\0352\0353", NULL);
		layout.Layout();	// incremental
		CHECK("incremental makes table lines",
			layout.Lines().size() == 2
			&& layout.Lines()[0].table);
		const PWLayout::RowLayout* row = layout.RowAt(0);
		CHECK("incremental row layout", row != NULL && row->cells.size() == 3);
		CHECK("row has height", row != NULL && row->height > 10);
		CHECK("line carries row height",
			layout.Lines().size() == 2
				&& layout.Lines()[0].height > 10);
	}

	{
		// InsertTable + the empty-cell crash regression.
		printf("block: insert table\n"); fflush(stdout);
		PWDocument doc;
		CHECK("table built", doc.InsertTable(0, 3, 3, true) == B_OK);
		CHECK("three rows", doc.CountParagraphs() == 3);
		CHECK("two separators per row",
			strchr(doc.ParagraphText(1), PWLayout::kCellSep) != NULL
			&& doc.ParagraphLength(0) == 2);
		CHECK("header bold", doc.FormatAt(0).bold);
		CHECK("body not bold", !doc.FormatAt(4).bold);
		CHECK("bounds rejected", doc.InsertTable(0, 0, 3, false) == B_BAD_VALUE
			&& doc.InsertTable(0, 3, 0, false) == B_BAD_VALUE);
		// the crash: layout + segments over EMPTY cells used to walk
		// past the text forever
		PWLayout layout(&doc);
		layout.SetPageSetup(PWPageSetup());
		layout.Layout();
		for (int32 li = 0; li < (int32)layout.Lines().size(); li++) {
			std::vector<PWLayout::Segment> segs;
			layout.FillSegments(li, &segs);
			CHECK("empty cell segments bounded", segs.size() <= 4);
			if (segs.size() > 4)
				break;
		}
		// single cell, single row — the degenerate 1x1
		PWDocument one;
		one.InsertTable(0, 1, 1, false);
		CHECK("1x1 is one plain paragraph",
			one.CountParagraphs() == 1 && one.ParagraphLength(0) == 0);
		PWLayout lone(&one);
		lone.SetPageSetup(PWPageSetup());
		lone.Layout();
		std::vector<PWLayout::Segment> noSegs;
		lone.FillSegments(0, &noSegs);
		CHECK("1x1 layout survives", noSegs.empty());
	}

	{
		// Sprint 10: paper metrics + the PDF writer, structure-checked.
		printf("block: pdf\n"); fflush(stdout);

		// P1: paper metrics drive pagination
		PWDocument doc;
		BString pdfFiller;
		for (int i = 0; i < 1500; i++)
			pdfFiller << "pdf pagination test ";
		doc.Insert(0, pdfFiller.String(), NULL);
		PWLayout layout(&doc);
		PWPageSetup a4;
		CHECK("a4 is 595x842 points",
			a4.pageWidth == 595.0f && a4.pageHeight == 842.0f);
		layout.SetPageSetup(a4);
		layout.Layout();
		int32 a4Pages = layout.CountPages();
		CHECK("a4 doc paginates to several pages", a4Pages >= 3);
		PWPageSetup letter;
		letter.pageWidth = 612;
		letter.pageHeight = 792;
		CHECK("letter is 612x792 points",
			letter.pageWidth == 612.0f && letter.pageHeight == 792.0f);
		layout.SetPageSetup(letter);
		layout.Layout();
		CHECK("letter doc paginates too", layout.CountPages() >= 2);
		// the relation that MUST hold: a narrower column means more pages
		PWPageSetup fat = a4;
		fat.marginLeft = fat.marginRight = 130;
		layout.SetPageSetup(fat);
		layout.Layout();
		CHECK("narrower column means more pages",
			layout.CountPages() > a4Pages);
		PWPageSetup a3;
		a3.pageWidth = 842;
		a3.pageHeight = 1191;
		layout.SetPageSetup(a3);
		layout.Layout();
		CHECK("a3 fits in fewer pages", layout.CountPages() < a4Pages);

		// P2: the PDF writer, end to end. The window lives fully
		// off-screen: views must attach for real rendering.
		BWindow* win = new BWindow(BRect(-3000, -3000, -2000, -2500),
			"pw-pdf-test", B_TITLED_WINDOW, B_NOT_RESIZABLE);
		PWPageView* view = new PWPageView(&doc, &layout);
		win->AddChild(view);
		win->Show();
		snooze(300000);
		layout.SetPageSetup(a4);
		layout.Layout();
		const char* pdfPath = "/tmp/pw-selftest.pdf";
		win->Lock();
		status_t pdfErr = PW_WritePDF(view, &layout, pdfPath, 1.0f); // 72 dpi
		win->Unlock();
		CHECK("pdf write returns B_OK", pdfErr == B_OK);

		// Binary-safe read-back: BString::SetTo stops at NUL bytes, and a
		// PDF is binary (Flate streams).
		std::string pdf;
		{
			BFile in;
			if (in.SetTo(pdfPath, B_READ_ONLY) == B_OK) {
				off_t sz = 0;
				in.GetSize(&sz);
				pdf.resize((size_t)sz);
				size_t got = 0;
				while (got < (size_t)sz) {
					ssize_t n = in.Read(&pdf[got], sz - got);
					if (n <= 0)
						break;
					got += (size_t)n;
				}
				pdf.resize(got);
			}
		}
		remove(pdfPath);
		auto countIn = [&](const char* needle) {
			int32 n = 0;
			size_t at = 0;
			while ((at = pdf.find(needle, at)) != std::string::npos) {
				n++;
				at += strlen(needle);
			}
			return n;
		};
		CHECK("pdf magic", pdf.compare(0, 8, "%PDF-1.4") == 0);
		CHECK("pdf page count matches layout",
			countIn("/MediaBox") == layout.CountPages());
		CHECK("pdf a4 media box",
			pdf.find("/MediaBox [0 0 595 842]") != std::string::npos);
		CHECK("pdf images flate compressed",
			pdf.find("/Filter /FlateDecode") != std::string::npos);
		CHECK("pdf has xref and eof",
			pdf.find("xref") != std::string::npos
				&& pdf.find("%%EOF") != std::string::npos);
		win->Lock();
		win->Quit();
	}

	{
		// Performance: ~100 pages.
		PWDocument doc;
		doc.Insert(0,
			"The quick brown fox jumps over the lazy dog. "
			"Pack my box with five dozen liquor jugs. "
			"How vexingly quick daft zebras jump!", NULL);
		for (int i = 0; i < 4000; i++)
			doc.SplitPara((i * 124) % std::max((int32)1, doc.Length()));
		bigtime_t t0 = system_time();
		PWLayout layout(&doc);
		layout.SetPageSetup(PWPageSetup());
		layout.Layout();
		bigtime_t ms = (system_time() - t0) / 1000;
		printf("measure: layout %d paragraphs -> %d pages in %lld ms\n",
			(int)doc.CountParagraphs(), (int)layout.CountPages(), (long long)ms);
		// keystroke-shape cost: relayout of a long document
		t0 = system_time();
		doc.Insert(doc.Length() / 2, "x", NULL);
		layout.Layout();
		bigtime_t ms2 = (system_time() - t0) / 1000;
		printf("measure: single-key relayout %lld ms\n", (long long)ms2);
		// incremental: one changed paragraph, everything else reused
		t0 = system_time();
		layout.Layout();
		bigtime_t ms3 = (system_time() - t0) / 1000;
		printf("measure: incremental keystroke relayout %lld ms "
			"(%d paragraphs re-measured)\n", (long long)ms3,
			(int)layout.LastMeasuredParagraphs());
		CHECK("incremental beats full", ms3 < 80);
		CHECK("incremental reuses", layout.LastMeasuredParagraphs() <= 2);
		// and produces the identical line structure as a cold layout
		int32 lineCount = (int32)layout.Lines().size();
		layout.SetIncremental(false);
		layout.Layout();
		CHECK("incremental equals full",
			(int32)layout.Lines().size() == lineCount);
		layout.SetIncremental(true);
		t0 = system_time();
		PWSpellChecker big;
		big.Load("/boot/home/config/settings/ProseWriter/words");
		printf("measure: dictionary %d words in %lld ms\n",
			(int)big.CountWords(), (long long)((system_time() - t0) / 1000));
		CHECK("layout pages produced", layout.CountPages() > 50);
		CHECK("layout under budget (1500 ms)", ms < 1500);
	}

	#undef CHECK

	int passed = 0;
	for (const Case& c : cases) {
		printf("  %-42s %s\n", c.name, c.ok ? "PASS" : "FAIL");
		if (c.ok) passed++;
	}
	printf("SELFTEST %s %d/%d\n", all ? "PASS" : "FAIL", passed,
		(int)cases.size());
	return all ? 0 : 1;
}

// -------------------------------------------------------------------- main --
int
main(int argc, char** argv)
{
	if (argc > 1 && strcmp(argv[1], "--selftest") == 0) {
		PWApp app;
		return SelfTest();
	}
	ParseArgs(argc, argv);
	PWApp app;
	app.Run();
	return 0;
}

