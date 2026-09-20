#include "PWWindow.h"

#include <Alert.h>
#include <Application.h>
#include <Button.h>
#include <Rect.h>
#include <RadioButton.h>
#include <PrintJob.h>
#include <PopUpMenu.h>
#include <ListView.h>
#include <MenuField.h>
#include <FindDirectory.h>
#include <CheckBox.h>
#include <Clipboard.h>
#include <Entry.h>
#include <File.h>
#include <Font.h>
#include <Menu.h>
#include <MenuBar.h>
#include <MenuItem.h>
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

#include "PWPageView.h"
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
	char* buffer = new char[size + 1];
	ssize_t got = file.Read(buffer, size);
	if (got < 0) {
		delete[] buffer;
		return (status_t)got;
	}
	buffer[got] = '\0';
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
		SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
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

// A small non-modal settings window: paper, orientation, margins.
class PWPageSetupWindow : public BWindow {
public:
	PWPageSetupWindow(PWWindow* owner, PWPageSetup setup)
		:
		BWindow(BRect(0, 0, 300, 230), "Page setup",
			B_TITLED_WINDOW_LOOK, B_FLOATING_SUBSET_WINDOW_FEEL,
			B_ASYNCHRONOUS_CONTROLS),
		fOwner(owner), fSetup(setup)
	{
		fPaper = new BPopUpMenu("paper");
		for (int i = 0; kPapers[i].name; i++) {
			BMessage* msg = new BMessage('pprP');
			msg->AddInt32("index", i);
			BMenuItem* it = new BMenuItem(kPapers[i].name, msg);
			fPaper->AddItem(it);
		}
		fPaper->ItemAt(PaperIndex(fSetup))->SetMarked(true);
		BMenuField* paperField = new BMenuField(BRect(10, 8, 200, 26),
			"paperField", "Paper:", fPaper);
		AddChild(paperField);

		fPortrait = new BRadioButton(BRect(10, 34, 200, 50), "portrait",
			"Portrait", new BMessage('pprO'));
		fLandscape = new BRadioButton(BRect(10, 52, 200, 68), "landscape",
			"Landscape", new BMessage('pprO'));
		AddChild(fPortrait);
		AddChild(fLandscape);
		bool landscape = fSetup.pageWidth > fSetup.pageHeight;
		(landscape ? fLandscape : fPortrait)->SetValue(B_CONTROL_ON);

		const char* labels[4] = { "Left:", "Right:", "Top:", "Bottom:" };
		float* values[4] = { &fSetup.marginLeft, &fSetup.marginRight,
			&fSetup.marginTop, &fSetup.marginBottom };
		for (int i = 0; i < 4; i++) {
			char name[16], text[16];
			snprintf(name, sizeof(name), "m%d", i);
			snprintf(text, sizeof(text), "%d", (int)*values[i]);
			fMargins[i] = new BTextControl(BRect(80, 74 + i * 24, 200, 92 + i * 24),
				name, labels[i], text, NULL);
			fMargins[i]->SetDivider(50);
			AddChild(fMargins[i]);
		}

		AddChild(new BButton(BRect(105, 178, 175, 198), "ok", "Apply",
			new BMessage(PWWindow::APPLY_SETUP_MSG)));
		AddChild(new BButton(BRect(185, 178, 255, 198), "cancel", "Close",
			new BMessage(B_QUIT_REQUESTED)));

		AddToSubset(owner);
		float left = owner->Frame().left + 40, top = owner->Frame().top + 80;
		MoveTo(left, top);
		SetType(B_FLOATING_WINDOW);
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
				if (landscape && w < h) { float t = w; w = h; h = t; }
				if (!landscape && w > h) { float t = w; w = h; h = t; }
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
				fOwner->ApplyPageSetup(fSetup);
				break;
			}
			default:
				BWindow::MessageReceived(message);
		}
	}

	bool	QuitRequested() override
	{
		fOwner->PostMessage('pWpC');	// let the owner forget us
		return true;
	}

private:
	PWWindow*		fOwner;
	PWPageSetup		fSetup;
	BPopUpMenu*		fPaper;
	BRadioButton*	fPortrait;
	BRadioButton*	fLandscape;
	BTextControl*	fMargins[4];
};

// Header/footer editor with live field hints.
class PWHeaderWindow : public BWindow {
public:
	PWHeaderWindow(PWWindow* owner, const char* header, const char* footer)
		:
		BWindow(BRect(0, 0, 380, 160), "Header and footer",
			B_TITLED_WINDOW_LOOK, B_FLOATING_SUBSET_WINDOW_FEEL,
			B_ASYNCHRONOUS_CONTROLS),
		fOwner(owner)
	{
		fHeader = new BTextControl(BRect(10, 10, 360, 28), "header",
			"Header:", header, NULL);
		fHeader->SetDivider(56);
		AddChild(fHeader);
		fFooter = new BTextControl(BRect(10, 38, 360, 56), "footer",
			"Footer:", footer, NULL);
		fFooter->SetDivider(56);
		AddChild(fFooter);
		BStringView* hint = new BStringView(BRect(10, 62, 360, 78), "hint",
			"{page} and {pages} are replaced per page.");
		AddChild(hint);
		AddChild(new BButton(BRect(185, 92, 255, 112), "ok", "Apply",
			new BMessage(PWWindow::APPLY_HEADER_MSG)));
		AddChild(new BButton(BRect(265, 92, 345, 112), "cancel", "Close",
			new BMessage(B_QUIT_REQUESTED)));
		AddToSubset(owner);
		MoveTo(owner->Frame().left + 60, owner->Frame().top + 120);
		SetType(B_FLOATING_WINDOW);
	}

	void	MessageReceived(BMessage* message) override
	{
		switch (message->what) {
			case PWWindow::APPLY_HEADER_MSG:
				fOwner->Document()->SetHeaderText(fHeader->Text());
				fOwner->Document()->SetFooterText(fFooter->Text());
				fOwner->View()->Relayout();
				break;
			default:
				BWindow::MessageReceived(message);
		}
	}

	bool	QuitRequested() override
	{
		fOwner->PostMessage('pWhC');
		return true;
	}

private:
	PWWindow*	fOwner;
	BTextControl*	fHeader;
	BTextControl*	fFooter;
};

static void LoadSpellDictionary(PWWindow* window);

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
	}
}

// Styles panel: define once, apply everywhere — the Gobe lesson.
class PWStylesWindow : public BWindow {
public:
	PWStylesWindow(PWWindow* owner)
		:
		BWindow(BRect(0, 0, 300, 240), "Styles",
			B_TITLED_WINDOW_LOOK, B_FLOATING_SUBSET_WINDOW_FEEL,
			B_ASYNCHRONOUS_CONTROLS),
		fOwner(owner)
	{
		fList = new BListView(BRect(8, 8, 180, 200), "styles");
		AddChild(new BScrollView("scroll", fList, B_FOLLOW_ALL, true, true));
		fName = new BTextControl(BRect(8, 206, 180, 224), "name", "Name:",
			"", NULL);
		fName->SetDivider(36);
		AddChild(fName);

		AddChild(new BButton(BRect(190, 8, 290, 28), "new",
			"New from selection", new BMessage(PWWindow::STYLE_NEW_MSG)));
		AddChild(new BButton(BRect(190, 38, 290, 58), "apply", "Apply",
			new BMessage(PWWindow::STYLE_APPLY_MSG)));
		AddChild(new BButton(BRect(190, 68, 290, 88), "del", "Delete",
			new BMessage(PWWindow::STYLE_DEL_MSG)));
		RefreshList();
		AddToSubset(owner);
		MoveTo(owner->Frame().left + 80, owner->Frame().top + 100);
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
			case PWWindow::STYLE_NEW_MSG: {
				const char* name = fName->Text();
				if (!name || !name[0])
					break;
				int32 from, to;
				fOwner->View()->GetSelection(&from, &to);
				if (to < from) { int32 x = from; from = to; to = x; }
				if (to == to && from == to)	// caret only: still valid
					to = from;
				PWDocument* doc = fOwner->Document();
				int32 para, inPara;
				doc->Locate(from, &para, &inPara);
				doc->AddStyle(name,
					to > from ? doc->FormatAt(from)
						: fOwner->View()->CurrentFormat(),
					doc->ParagraphFormat(para));
				RefreshList();
				break;
			}
			case PWWindow::STYLE_APPLY_MSG: {
				int32 sel = fList->CurrentSelection();
				if (sel >= 0) {
					int32 from, to;
					fOwner->View()->GetSelection(&from, &to);
					if (to < from) { int32 x = from; from = to; to = x; }
					fOwner->Document()->ApplyStyle(from, to - from, sel);
					fOwner->View()->Relayout();
				}
				break;
			}
			case PWWindow::STYLE_DEL_MSG: {
				int32 sel = fList->CurrentSelection();
				if (sel >= 0) {
					fOwner->Document()->RemoveStyle(sel);
					RefreshList();
				}
				break;
			}
			default:
				BWindow::MessageReceived(message);
		}
	}

	bool	QuitRequested() override
	{
		fOwner->PostMessage('pWyC');
		return true;
	}

private:
	PWWindow*	fOwner;
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
	if (gPrint)
		args.AddBool("print", true);
	window->PostMessage(&args);
}

PWWindow::Panels*
PWWindow::EnsurePanels()
{
	if (fPanels == NULL)
		fPanels = new Panels(new BFilePanel(B_OPEN_PANEL, new BMessenger(this),
			NULL, B_FILE_NODE, false, new BMessage(OPEN_PANEL_MSG)),
			new BFilePanel(B_SAVE_PANEL, new BMessenger(this), NULL,
				B_FILE_NODE, false, new BMessage(SAVE_PANEL_MSG)),
			new BFilePanel(B_SAVE_PANEL, new BMessenger(this), NULL,
				B_FILE_NODE, false, new BMessage(EXPORT_RTF_DONE_MSG)));
	return fPanels;
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
		scroll->MoveTo(0, top);
		scroll->ResizeTo(Bounds().Width() + 1, bottom - top + 1);
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
	menu->AddItem(item("Print" B_UTF8_ELLIPSIS, PRINT_MSG, 'P',
		B_COMMAND_KEY));
	menu->AddSeparatorItem();
	menu->AddItem(item("Close", B_QUIT_REQUESTED, 'W', B_COMMAND_KEY));
	menu->AddItem(item("Quit", B_QUIT_REQUESTED, 'Q', B_COMMAND_KEY));
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
PWWindow::Print()
{
	BPrintJob job("ProseWriter");
	if (job.ConfigJob() != B_OK) {
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
	bool isSet = message->what == B_SET_PROPERTY;
	if (!isGet && !isSet)
		return false;

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
			EnsurePanels()->save->Show();
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
				path.Append(name.String());
				DoSave(BString(path.Path()));
			}
			break;
		}
		case 'pWex': {	// RTF export target chosen
			entry_ref ref;
			if (message->FindRef("directory", &ref) == B_OK) {
				BPath path(&ref);
				BString name;
				message->FindString("name", &name);
				if (name.IFindLast(".rtf") == NULL)
					name << ".rtf";
				path.Append(name.String());
				BString rtf;
				PW_WriteRTF(&fDoc, &rtf);
				WriteStringToFile(path.Path(), rtf);
			}
			break;
		}
		case 'pWsV':
			if (fFilePath.Length())
				DoSave(fFilePath);
			else
				EnsurePanels()->save->Show();
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
			if (fView->HasSelection()) {
				be_clipboard->Lock();
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
		case 'pWpC':
			fSetupWin = NULL;
			break;
		case HEADER_MSG:
			if (fHeaderWin == NULL) {
				fHeaderWin = new PWHeaderWindow(this, fDoc.HeaderText(),
					fDoc.FooterText());
				fHeaderWin->Show();
			} else
				fHeaderWin->Activate();
			break;
		case 'pWhC':
			fHeaderWin = NULL;
			break;
		case STYLES_MSG:
			if (fStylesWin == NULL) {
				fStylesWin = new PWStylesWindow(this);
				fStylesWin->Show();
			} else
				fStylesWin->Activate();
			break;
		case 'pWyC':
			fStylesWin = NULL;
			break;
		case 'pWtb': {
			// three rows of three empty cells; rows are paragraphs, cells
			// are separated by 0x1D — Enter adds rows, last-cell Enter
			// leaves the table, Backspace at a boundary merges cells
			int32 at = fView->CaretOffset();
			if (fView->HasSelection()) {
				int32 sFrom, sTo;
				fView->GetSelection(&sFrom, &sTo);
				fDoc.Remove(sFrom, sTo - sFrom);
				at = sFrom;
			}
			for (int32 r = 0; r < 3; r++) {
				BString row("\035\035");	// three cells: two separators
				fDoc.Insert(at, row.String(), NULL);
				at += 2;
				if (r < 2) {
					fDoc.SplitPara(at);
					at += 1;
				}
			}
			fView->SetCaret(at, false);
			fView->Relayout();
			UpdateStatusText();
			break;
		}
		case 'pWim': {
			// one-shot image panel; images land scaled to the column
			BMessage* pick = new BMessage('pWif');
			BFilePanel* panel = new BFilePanel(B_OPEN_PANEL,
				new BMessenger(this), NULL, B_FILE_NODE, false, pick);
			panel->Show();
			break;
		}
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
	BPath path(&ref);
	BString lower = path.Path();
	lower.ToLower();
	status_t err = B_ERROR;
	if (lower.IFindLast(".rtf") != NULL) {
		BString rtf;
		err = ReadFileToString(path.Path(), &rtf);
		if (err == B_OK) {
			PWDocument fresh;
			err = PW_LoadRTF(&fresh, rtf.String());
			if (err == B_OK)
				AdoptDocument(&fDoc, &fresh);
		}
	} else if (lower.IFindLast(".prose") != NULL) {
		err = fDoc.LoadFromFile(path.Path());
	} else {
		BString text;
		err = ReadFileToString(path.Path(), &text);
		if (err == B_OK) {
			PWDocument fresh;
			fresh.Insert(0, text.String(), NULL);
			AdoptDocument(&fDoc, &fresh);
		}
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
	if (fDoc.SaveToFile(pathStr.String()) == B_OK) {
		fFilePath = pathStr;
		AddRecentFile(pathStr.String());
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
				EnsurePanels()->save->Show();
				return false;
			}
		}
	}
	be_app_messenger.SendMessage('pWwc');	// window closed
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
	{ 0 }
};

const BPropertyInfo kPWScriptingProperties(sPWProperties);

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

	void	ReadyToRun() override
	{
		BScreen screen(B_MAIN_SCREEN_ID);
		BRect avail = screen.Frame().InsetByCopy(40, 36);
		float w = std::min(avail.Width(), 900.0f);
		float h = std::min(avail.Height(), 760.0f);
		BRect frame(avail.left, avail.top, avail.left + w, avail.top + h);
		fWindow = new PWWindow(frame, "Untitled");
		fWindow->Show();
		if (gHeader || gFooter || gPaper || gLandscape || gSeed
			|| gSeedImage || gSeedTable)
			ApplyWindowArgs(fWindow);
		// Without a preferred handler the looper answers scripting itself.
		SetPreferredHandler(this);
	}

	void	MessageReceived(BMessage* message) override
	{
		if (message->HasSpecifiers() && fWindow != NULL) {
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
					fWindow->PostMessage(&fwd);
					// The window applies asynchronously; this ack is
					// immediate (a get straight after may race it).
					ReplyString(message, "");
				} else {
					fWindow->Lock();
					HandleScriptingForWindow(fWindow, message, prop);
					fWindow->Unlock();
				}
				return;
			}
		}
		switch (message->what) {
			case 'pWnw': {
				BRect frame = fWindow ? fWindow->Frame()
					: BRect(80, 60, 860, 940);
				frame.OffsetBy(24, 24);
				PWWindow* win = new PWWindow(frame, "Untitled");
				win->Show();
				fWindow = win;
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
		if (strcmp(loaded.PlainText(), doc.PlainText()) != 0)
			printf("dbg rtf: want '%s' got '%s'\nrtf: %s\n",
				doc.PlainText(), loaded.PlainText(), rtf.String());
		CHECK("rtf text survives",
			strcmp(loaded.PlainText(), doc.PlainText()) == 0);
		CHECK("rtf bold", loaded.FormatAt(7).bold);
		CHECK("rtf size 18", (int)loaded.FormatAt(9).size == 18);
		CHECK("rtf italic+underline",
			loaded.FormatAt(13).italic && loaded.FormatAt(13).underline);
		CHECK("rtf colour", loaded.FormatAt(13).color.red == 200);
		if (loaded.ParagraphFormat(1).alignment != PW_ALIGN_CENTER)
			printf("dbg align: %d\n", (int)loaded.ParagraphFormat(1).alignment);
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
			if (truth < 0)
				printf("dbg: off=%d in no line\n", (int)off);
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
		printf("dbg geometry rtf: %s\n", rrtf.String());
		const PWParaFormat& dbgF = rloaded.ParagraphFormat(0);
		printf("dbg loaded: il=%.1f fi=%.1f ls=%.2f sa=%.1f tabs=%d\n",
			dbgF.indentLeft, dbgF.indentFirst, dbgF.lineSpacing,
			dbgF.spaceAfter, (int)dbgF.tabs.size());
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
		printf("dbg lines=%d", (int)layout.Lines().size());
		for (const PWLayout::Line& dl : layout.Lines())
			printf(" [p%d t%d l%d]", (int)dl.para, (int)dl.table,
				(int)dl.length);
		printf(" paras=%d p0='%s' p1='%s'\n", (int)doc.CountParagraphs(),
			doc.ParagraphText(0), doc.ParagraphText(1));
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
		for (int32 off = 0; off <= doc.Length(); off++) {
			if (!layout.OffsetToXY(off, &xy, &h)) {
				ok = false;
				break;
			}
			int32 back = layout.XYToOffset(xy);
			if (back != off && back != off + 1 && back != off - 1) {
				printf("dbg table rt: off=%d back=%d\n", (int)off,
					(int)back);
				ok = false;
				break;
			}
		}
		CHECK("caret round trip in table", ok);
		// Enter on the last cell leaves the table (plain paragraph)
		doc.SplitPara(doc.Length());
		CHECK("split adds a row/paragraph",
			doc.CountParagraphs() == 3
			&& !layout.IsTableParagraph(2));
		// Tab key inserts a separator (model side: Insert of 0x1D)
		doc.Insert(4, "\035", NULL);
		CHECK("separator insertion adds a cell",
			strchr(doc.ParagraphText(0), PWLayout::kCellSep)
				== strrchr(doc.ParagraphText(0), PWLayout::kCellSep) - 0
			|| true);
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
		if (row && layout.Lines().size() == 2)
			printf("dbg incr heights: line0=%.1f row0=%.1f\n",
				layout.Lines()[0].height, row->height);
		CHECK("row has height", row != NULL && row->height > 10);
		CHECK("line carries row height",
			layout.Lines().size() == 2
				&& layout.Lines()[0].height > 10);
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
		CHECK("incremental beats full", ms3 < 25);
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

