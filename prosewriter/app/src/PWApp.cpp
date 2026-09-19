#include "PWWindow.h"

#include <Alert.h>
#include <Application.h>
#include <Button.h>
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
#include <Screen.h>
#include <ScrollView.h>
#include <StringView.h>
#include <TextControl.h>
#include <UTF8.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "PWPageView.h"
#include "PWRTF.h"
#include "PWRuler.h"

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

	fOpenPanel = new BFilePanel(B_OPEN_PANEL, new BMessenger(this), NULL,
		B_FILE_NODE, false, new BMessage(OPEN_PANEL_MSG));
	fSavePanel = new BFilePanel(B_SAVE_PANEL, new BMessenger(this), NULL,
		B_FILE_NODE, false, new BMessage(SAVE_PANEL_MSG));
	fExportPanel = new BFilePanel(B_SAVE_PANEL, new BMessenger(this), NULL,
		B_FILE_NODE, false, new BMessage(EXPORT_RTF_DONE_MSG));

	SetSizeLimits(460, 4000, 380, 4000);
	LayoutChildren();
	UpdateStatusText();
	fView->MakeFocus();
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
		case EXPORT_RTF_MSG:
			fExportPanel->Show();
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
		BScreen screen(B_MAIN_SCREEN_ID);
		BRect avail = screen.Frame().InsetByCopy(40, 36);
		float w = std::min(avail.Width(), 900.0f);
		float h = std::min(avail.Height(), 760.0f);
		BRect frame(avail.left, avail.top, avail.left + w, avail.top + h);
		fWindow = new PWWindow(frame, "Untitled");
		fWindow->Show();
	}

	void	MessageReceived(BMessage* message) override
	{
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
	} cases[40];
	int n = 0;
	bool all = true;

	#define CHECK(label, cond) { cases[n].name = label; \
		bool ok_ = (cond); cases[n].ok = ok_; \
		if (!ok_) all = false; n++; }

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
		PWApp app;
		return SelfTest();
	}
	PWApp app;
	app.Run();
	return 0;
}
