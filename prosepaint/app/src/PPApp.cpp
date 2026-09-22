// PPApp — ProsePaint's application, window, tool palette, brush
// properties, colour selection, layers panel, scripting, selftest.
#include <Alert.h>
#include <Application.h>
#include <Button.h>
#include <CheckBox.h>
#include <Entry.h>
#include <File.h>
#include <FilePanel.h>
#include <FindDirectory.h>
#include <ListView.h>
#include <Menu.h>
#include <MenuBar.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Message.h>
#include <MimeType.h>
#include <Messenger.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <PropertyInfo.h>
#include <Screen.h>
#include <ScrollView.h>
#include <StringItem.h>
#include <TextControl.h>
#include <Directory.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

#include "PPBrush.h"
#include "PPCanvas.h"
#include "PPDocument.h"
#include "PPPDF.h"
#include "PPRecent.h"

// ---------------------------------------------------------------- helpers --
static bool
HasSuffix(const BString& s, const char* suffix)
{
	int32 n = strlen(suffix);
	return s.Length() >= n
		&& memcmp(s.String() + s.Length() - n, suffix, n) == 0;
}

static BString
WithExtension(BString name, const char* ext)
{
	if (HasSuffix(name, ext))
		return name;
	return name << ext;
}

static status_t
WriteAttrOn(const char* path, const char* attr, const char* value)
{
	BNode node;
	status_t err = node.SetTo(path);
	if (err != B_OK)
		return err;
	BString v(value);
	return node.WriteAttrString(attr, &v);
}

static const struct { const char* name; float w, h; } kPapers[] = {
	{ "A4", 595.0f, 842.0f },
	{ "US Letter", 612.0f, 792.0f },
	{ "US Legal", 612.0f, 1008.0f },
	{ "A5", 420.0f, 595.0f },
	{ "A3", 842.0f, 1191.0f },
	{ NULL, 0, 0 }
};

struct PPColourEntry { const char* name; rgb_color c; };
static const PPColourEntry kColours[] = {
	{ "Black",	{ 0, 0, 0, 255 } },
	{ "White",	{ 255, 255, 255, 255 } },
	{ "Grey",	{ 200, 200, 200, 255 } },
	{ "Red",	{ 216, 40, 40, 255 } },
	{ "Orange",	{ 240, 160, 40, 255 } },
	{ "Yellow",	{ 250, 240, 120, 255 } },
	{ "Green",	{ 70, 170, 70, 255 } },
	{ "Blue",	{ 70, 110, 220, 255 } },
	{ "Purple",	{ 150, 80, 190, 255 } },
};
static const int32 kColourCount = 9;

// ---------------------------------------------------------------- window --
class PPResizeWindow;
class PPWindow : public BWindow {
public:
			PPWindow(BRect frame, const char* title);

	bool	QuitRequested() override;
	void	MessageReceived(BMessage* message) override;
	void	FrameResized(float width, float height) override
	{
		LayoutChildren();
	}

	PPDocument* Document() { return &fDoc; }
	PPCanvas*	Canvas() { return fCanvas; }
	bool	IsQuitting() const { return fQuitting; }

	enum {
		OPEN_PANEL_MSG = 'ppOf', SAVE_PANEL_MSG = 'ppSf',
		PDF_PANEL_MSG = 'ppFf', ZOOM_MSG = 'ppZm',
		TOOL_MSG = 'ppTl', PROP_MSG = 'ppPr', SWATCH_MSG = 'ppSw',
		LAYER_MSG = 'ppLy', STATUS_MSG = 'ppUp'
	};

private:
	void	BuildMenus();
	void	BuildPalette();
	void	BuildProperties();
	void	BuildLayersPanel();
	void	LayoutChildren();
	void	UpdateStatus();
	void	RefreshLayers();
	void	RebuildRecentMenu();
	void	RememberRecent(const BString& path);
	void	DoSave(const BString& path);
	void	DoExportPDF(const BString& path);
	status_t	OpenFile(const entry_ref& ref);
	void	RegisterDocumentType();
	void	SetPaper(int32 index);

	PPDocument	fDoc;
	PPCanvas*	fCanvas = NULL;
	BScrollView*	fScroll = NULL;
	BMenuBar*	fMenuBar = NULL;
	BView*		fPalette = NULL;
	BView*		fProps = NULL;
	BView*		fLayers = NULL;
	BListView*	fLayerList = NULL;
	BFilePanel*	fOpenPanel = NULL;
	BFilePanel*	fSavePanel = NULL;
	BFilePanel*	fPDFPanel = NULL;
	PPRecent	fRecent;
	BMenu*		fRecentMenu = NULL;
	BMenu*		fZoomMenu = NULL;
	BString		fRecentPath;
	PPResizeWindow*	fResizeWin = NULL;
	BString		fFilePath;
	BMenuItem*	fUndoItem = NULL;
	BMenuItem*	fRedoItem = NULL;
	BMenuItem*	fSaveItem = NULL;
	BMenuField*	fShapeField = NULL;
	BMenuField*	fSizeField = NULL;
	BMenuField*	fHardField = NULL;
	BMenuField*	fOpaqField = NULL;
	BTextControl*	fR = NULL, * fG = NULL, * fB = NULL;
	int32		fPaperIndex = 0;
	bool		fRefreshing = false;
	bool		fQuitting = false;
};

// Resize canvas: a small ask-don't-assume panel (dimensions in,
// one message, hide — the Insert-table pattern)
class PPWindow;
class PPResizeWindow : public BWindow {
public:
	PPResizeWindow(PPWindow* owner, int32 w, int32 h)
		:
		BWindow(BRect(0, 0, 220, 130), "Resize canvas",
			B_TITLED_WINDOW_LOOK, B_FLOATING_APP_WINDOW_FEEL,
			B_NOT_ZOOMABLE | B_ASYNCHRONOUS_CONTROLS)
	{
		fW = new BTextControl(BRect(8, 8, 200, 30), "w", "Width px:",
			BString().SetToFormat("%d", (int)w).String(), NULL);
		fH = new BTextControl(BRect(8, 38, 200, 60), "h", "Height px:",
			BString().SetToFormat("%d", (int)h).String(), NULL);
		fW->SetDivider(70);
		fH->SetDivider(70);
		AddChild(fW);
		AddChild(fH);
		BMessage* go = new BMessage('ppGo');
		go->AddPointer("owner", owner);
		BButton* ok = new BButton(BRect(60, 90, 150, 114), "ok",
			"Resize", go);
		AddChild(ok);
		SetDefaultButton(ok);
		fOwner = owner;
	}

	bool QuitRequested() override
	{
		if (fOwner != NULL)
			fOwner->PostMessage('ppRq');	// panel died
		return true;
	}

	void MessageReceived(BMessage* message) override
	{
		if (message->what == 'ppGo') {
			int32 w = atoi(fW->Text());
			int32 h = atoi(fH->Text());
			if (fOwner != NULL && w >= 1 && h >= 1) {
				BMessage m('ppRz');
				m.AddInt32("w", w);
				m.AddInt32("h", h);
				fOwner->PostMessage(&m);
			}
			PostMessage(B_QUIT_REQUESTED);
			return;
		}
		BWindow::MessageReceived(message);
	}

private:
	BTextControl*	fW = NULL;
	BTextControl*	fH = NULL;
	PPWindow*	fOwner = NULL;
};



PPWindow::PPWindow(BRect frame, const char* title)
	:
	BWindow(frame, title, B_TITLED_WINDOW,
		B_QUIT_ON_WINDOW_CLOSE | B_ASYNCHRONOUS_CONTROLS)
{
	RegisterDocumentType();
	// the Open Recent list lives in the user settings directory
	{
		BPath settings;
		if (find_directory(B_USER_SETTINGS_DIRECTORY, &settings) == B_OK) {
			settings.Append("ProsePaint/recent_files");
			fRecentPath = settings.Path();
			BPath parent(settings);
			parent.GetParent(&parent);
			create_directory(parent.Path(), 0755);
			fRecent.Load(fRecentPath.String());
		}
	}
	fCanvas = new PPCanvas(&fDoc);

	fMenuBar = new BMenuBar(BRect(0, 0, 200, 20), "menubar");
	BuildMenus();
	AddChild(fMenuBar);

	BScrollView* scroll = new BScrollView("scroll", fCanvas, B_FOLLOW_NONE,
		true, true, B_FANCY_BORDER);
	fScroll = scroll;
	AddChild(scroll);

	BuildPalette();
	AddChild(fPalette);
	BuildProperties();
	AddChild(fProps);
	BuildLayersPanel();
	AddChild(fLayers);

	SetSizeLimits(700, 4000, 520, 4000);
	LayoutChildren();
	fCanvas->MakeFocus();
	UpdateStatus();
}

void
PPWindow::RegisterDocumentType()
{
	// the Tracker chain, pattern-proven in the sibling apps
	BMimeType docType(PPDocument::kDocType);
	if (docType.InitCheck() != B_OK)
		return;
	docType.Install();
	docType.SetShortDescription("ProsePaint painting");
	docType.SetLongDescription("ProsePaint painting document");
	status_t err = docType.SetSnifferRule(
		"1.0 ([0:3] \"HMF1\") ([4:7] \"&tPp\")");
	if (err != B_OK)
		fprintf(stderr, "ProsePaint: sniffer rule rejected: %s\n",
			strerror(err));
	docType.SetPreferredApp("application/x-vnd.prose.ProsePaint");
}

void
PPWindow::BuildMenus()
{
	BMenu* menu = new BMenu("File");
	menu->AddItem(new BMenuItem("New", new BMessage('ppNw'), 'N',
		B_COMMAND_KEY));
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem("Open" B_UTF8_ELLIPSIS,
		new BMessage(OPEN_PANEL_MSG), 'O', B_COMMAND_KEY));
	fSaveItem = new BMenuItem("Save", new BMessage('ppSv'), 'S',
		B_COMMAND_KEY);
	menu->AddItem(fSaveItem);
	menu->AddItem(new BMenuItem("Save as" B_UTF8_ELLIPSIS,
		new BMessage(SAVE_PANEL_MSG), 'S', B_COMMAND_KEY | B_SHIFT_KEY));
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem("Print to PDF" B_UTF8_ELLIPSIS,
		new BMessage(PDF_PANEL_MSG), 'P', B_COMMAND_KEY));
	menu->AddSeparatorItem();
	fRecentMenu = new BMenu("Open Recent");
	menu->AddItem(fRecentMenu);
	RebuildRecentMenu();
	BMenuItem* quit = new BMenuItem("Quit", new BMessage(B_QUIT_REQUESTED),
		'Q', B_COMMAND_KEY);
	quit->SetTarget(be_app);
	menu->AddItem(quit);
	menu->ItemAt(0)->SetTarget(be_app);
	fMenuBar->AddItem(menu);

	menu = new BMenu("Edit");
	fUndoItem = new BMenuItem("Undo", new BMessage('ppUd'), 'Z',
		B_COMMAND_KEY);
	fRedoItem = new BMenuItem("Redo", new BMessage('ppRd'), 'Y',
		B_COMMAND_KEY);
	menu->AddItem(fUndoItem);
	menu->AddItem(fRedoItem);
	fMenuBar->AddItem(menu);

	menu = new BMenu("Canvas");
	for (int32 i = 0; kPapers[i].name != NULL; i++) {
		BMessage* m = new BMessage('ppPp');
		m->AddInt32("index", i);
		menu->AddItem(new BMenuItem(kPapers[i].name, m));
	}
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem("Resize" B_UTF8_ELLIPSIS,
		new BMessage('ppRs')));
	fMenuBar->AddItem(menu);

	menu = new BMenu("Layer");
	menu->AddItem(new BMenuItem("Add layer", new BMessage('ppLa')));
	menu->AddItem(new BMenuItem("Delete layer", new BMessage('ppLd')));
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem("Move up", new BMessage('ppLu')));
	menu->AddItem(new BMenuItem("Move down", new BMessage('ppLw')));
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem("Toggle visible",
		new BMessage('ppLv')));
	fMenuBar->AddItem(menu);

	menu = new BMenu("View");
	fZoomMenu = new BMenu("Zoom");
	const float zooms[5] = { 0.5f, 0.75f, 1.0f, 1.5f, 2.0f };
	for (int32 i = 0; i < 5; i++) {
		BMessage* m = new BMessage(ZOOM_MSG);
		m->AddFloat("zoom", zooms[i]);
		char label[12];
		snprintf(label, sizeof(label), "%.0f%%", zooms[i] * 100);
		BMenuItem* it = new BMenuItem(label, m);
		if (zooms[i] == 1.0f) {
			it->SetMarked(true);
			it->SetShortcut('0', B_COMMAND_KEY);
		}
		fZoomMenu->AddItem(it);
	}
	fZoomMenu->SetRadioMode(true);
	menu->AddItem(fZoomMenu);
	fMenuBar->AddItem(menu);
}

void
PPWindow::BuildPalette()
{
	fPalette = new BView(BRect(0, 0, 84, PP_TOOL_COUNT * 30 + 4),
		"palette", B_FOLLOW_NONE, B_WILL_DRAW);
	fPalette->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	fPalette->SetDrawingMode(B_OP_COPY);
	const char* names[PP_TOOL_COUNT] = { "Pen", "Brush", "Spray",
		"Eraser", "Smudge", "Fill", "Pick", "Line", "Rect", "Oval" };
	for (int32 i = 0; i < PP_TOOL_COUNT; i++) {
		BMessage* m = new BMessage(TOOL_MSG);
		m->AddInt32("tool", i);
		BButton* b = new BButton(
			BRect(4, 4 + i * 30, 80, 28 + i * 30), "tool", names[i], m);
		if (i == PP_TOOL_BRUSH)
			b->SetValue(B_CONTROL_ON);
		fPalette->AddChild(b);
	}
}

void
PPWindow::BuildProperties()
{
	fProps = new BView(BRect(0, 0, 220, 320), "props", B_FOLLOW_NONE,
		B_WILL_DRAW);
	fProps->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

	// brush shape
	BPopUpMenu* shapeMenu = new BPopUpMenu("shape");
	for (int32 i = 0; i < PP_BRUSH_COUNT; i++) {
		BMessage* m = new BMessage(PROP_MSG);
		m->AddInt32("field", 0);
		m->AddInt32("index", i);
		shapeMenu->AddItem(new BMenuItem(PPBrush::ShapeName((PPBrushShape)i),
			m));
	}
	shapeMenu->ItemAt(PP_BRUSH_SOFT)->SetMarked(true);
	fShapeField = new BMenuField(BRect(8, 8, 212, 30), "shape", "Shape:",
		shapeMenu);
	fShapeField->SetDivider(52);
	fProps->AddChild(fShapeField);

	// size 1..64 via a menu of common sizes
	BPopUpMenu* sizeMenu = new BPopUpMenu("size");
	const int32 sizes[] = { 1, 2, 4, 8, 12, 16, 24, 32, 48, 64 };
	for (int32 i = 0; i < 10; i++) {
		BMessage* m = new BMessage(PROP_MSG);
		m->AddInt32("field", 1);
		m->AddInt32("size", sizes[i]);
		char label[8];
		snprintf(label, sizeof(label), "%d px", (int)sizes[i]);
		BMenuItem* it = new BMenuItem(label, m);
		if (sizes[i] == 8)
			it->SetMarked(true);
		sizeMenu->AddItem(it);
	}
	fSizeField = new BMenuField(BRect(8, 34, 212, 56), "size", "Size:",
		sizeMenu);
	fSizeField->SetDivider(52);
	fProps->AddChild(fSizeField);

	// hardness
	BPopUpMenu* hardMenu = new BPopUpMenu("hard");
	const char* hardLabels[] = { "Hard", "Firm", "Soft", "Fuzzy" };
	const int32 hardVals[] = { 255, 200, 120, 40 };
	for (int32 i = 0; i < 4; i++) {
		BMessage* m = new BMessage(PROP_MSG);
		m->AddInt32("field", 2);
		m->AddInt32("hard", hardVals[i]);
		hardMenu->AddItem(new BMenuItem(hardLabels[i], m));
	}
	hardMenu->ItemAt(1)->SetMarked(true);
	fHardField = new BMenuField(BRect(8, 60, 212, 82), "hard", "Edge:",
		hardMenu);
	fHardField->SetDivider(52);
	fProps->AddChild(fHardField);

	// stroke opacity — the stroke-buffer contract: stamps within one
	// stroke never build up, each pass lands at this strength
	BPopUpMenu* opaqMenu = new BPopUpMenu("opaq");
	const int32 opaqVals[] = { 100, 75, 50, 25 };
	for (int32 i = 0; i < 4; i++) {
		BMessage* m = new BMessage(PROP_MSG);
		m->AddInt32("field", 14);
		m->AddInt32("opaq", opaqVals[i]);
		char label[8];
		snprintf(label, sizeof(label), "%d%%", (int)opaqVals[i]);
		BMenuItem* it = new BMenuItem(label, m);
		if (opaqVals[i] == 100)
			it->SetMarked(true);
		opaqMenu->AddItem(it);
	}
	fOpaqField = new BMenuField(BRect(8, 86, 212, 108), "opaq", "Ink:",
		opaqMenu);
	fOpaqField->SetDivider(52);
	fProps->AddChild(fOpaqField);

	// colour swatches: a plain view that reports clicks (BView has no
	// invocation; the window is right there)
	class PPSwatch : public BView {
	public:
		PPSwatch(BRect frame, rgb_color c, int32 index, BWindow* owner)
			:
			BView(frame, "swatch", B_FOLLOW_NONE, B_WILL_DRAW),
			fColour(c)
		{
			SetViewColor(c);
			fMsg.what = PPWindow::SWATCH_MSG;
			fMsg.AddInt32("index", index);
			fOwner = owner;
		}
		void MouseDown(BPoint) override
		{
			BMessage m(fMsg);
			BMessenger(fOwner).SendMessage(&m);
		}
	private:
		rgb_color	fColour;
		BMessage	fMsg;
		BWindow*	fOwner;
	};
	for (int32 i = 0; i < kColourCount; i++)
		fProps->AddChild(new PPSwatch(
			BRect(10 + i * 23, 126, 10 + i * 23 + 19, 145),
			kColours[i].c, i, this));

	// custom RGB
	fR = new BTextControl(BRect(8, 154, 100, 176), "r", "R:", "0", NULL);
	fG = new BTextControl(BRect(108, 154, 200, 176), "g", "G:", "0", NULL);
	fB = new BTextControl(BRect(8, 180, 100, 202), "b", "B:", "0", NULL);
	BTextControl* rgb[3] = { fR, fG, fB };
	for (int32 i = 0; i < 3; i++) {
		rgb[i]->SetDivider(16);
		BMessage* m = new BMessage(PROP_MSG);
		m->AddInt32("field", 10 + i);
		rgb[i]->SetMessage(m);
		rgb[i]->SetTarget(this);
		fProps->AddChild(rgb[i]);
	}
}

void
PPWindow::BuildLayersPanel()
{
	fLayers = new BView(BRect(0, 0, 220, 260), "layers", B_FOLLOW_NONE,
		B_WILL_DRAW);
	fLayers->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

	fLayerList = new BListView(BRect(8, 8, 212, 190), "layers");
	BScrollView* layerScroller = new BScrollView("layerscroll", fLayerList,
		B_FOLLOW_NONE, false, true);
	fLayerList->SetSelectionMessage(new BMessage(LAYER_MSG));
	fLayers->AddChild(layerScroller);

	const char* labels[4] = { "Add", "Del", "Up", "Down" };
	uint32 whats[4] = { 'ppLa', 'ppLd', 'ppLu', 'ppLw' };
	for (int32 i = 0; i < 4; i++) {
		BButton* b = new BButton(BRect(8 + i * 52, 200, 8 + i * 52 + 48,
			222), "lyr", labels[i], new BMessage(whats[i]));
		fLayers->AddChild(b);
	}
	RefreshLayers();
}

void
PPWindow::RefreshLayers()
{
	if (fLayerList == NULL)
		return;
	fRefreshing = true;
	fLayerList->MakeEmpty();
	// topmost first in the list (painters think top-down)
	for (int32 i = fDoc.CountLayers() - 1; i >= 0; i--) {
		PPLayer* l = fDoc.LayerAt(i);
		BString label;
		label.SetToFormat("%s%s  %d%%", l->name.String(),
			l->visible ? "" : " (hidden)", (int)l->opacity * 100 / 255);
		fLayerList->AddItem(new BStringItem(label.String()));
	}
	int32 active = fDoc.IndexOf(fDoc.ActiveLayer() ? fDoc.ActiveLayer()->id
		: -1);
	if (active >= 0)
		fLayerList->Select(fDoc.CountLayers() - 1 - active);
	fRefreshing = false;
}

void
PPWindow::LayoutChildren()
{
	// the panels own the whole window; nothing follows the frame
	float menuH = fMenuBar->Bounds().Height() + 1;
	fMenuBar->ResizeTo(Bounds().Width(), menuH - 1);
	fPalette->MoveTo(0, menuH);
	fProps->MoveTo(Bounds().right - 220, menuH);
	fLayers->MoveTo(Bounds().right - 220, menuH + 330);
	fScroll->MoveTo(84, menuH);
	// exactly to the window's bottom edge: this app has no status strip (the
	// status lives in the title), and the +1 that ProseDraw once grew past
	// its chrome by is not repeated here
	fScroll->ResizeTo(Bounds().right - 220 - 84, Bounds().bottom - menuH);
	if (fCanvas != NULL) {
		// the bars' ranges span the scroll view, which just changed size
		fCanvas->UpdateScrollBars();
		fCanvas->Invalidate();
	}
}

void
PPWindow::UpdateStatus()
{
	BString s;
	s.SetToFormat("%dx%d px  %s @ %.0f dpi   %d layers   %s",
		(int)fDoc.Width(), (int)fDoc.Height(), fDoc.Paper().name,
		fDoc.DPI(), (int)fDoc.CountLayers(),
		fDoc.IsModified() ? "modified" : "clean");
	SetTitle(BString(fDoc.IsModified() ? "* " : "")
		.Append(fFilePath.Length() ? BString(BPath(fFilePath.String()).Leaf())
			: BString("Untitled"))
		.String());
	// status lives in the window title's tail for v1 (no bar yet)
	BString title = Title();
	title << "   —   " << s;
	SetTitle(title.String());
	if (fUndoItem != NULL)
		fUndoItem->SetEnabled(fDoc.CanUndo());
	if (fRedoItem != NULL)
		fRedoItem->SetEnabled(fDoc.CanRedo());
	if (fSaveItem != NULL)
		fSaveItem->SetEnabled(fDoc.IsModified() || !fFilePath.Length());
}

void
PPWindow::RebuildRecentMenu()
{
	if (fRecentMenu == NULL)
		return;
	while (fRecentMenu->CountItems() > 0)
		delete fRecentMenu->RemoveItem((int32)0);
	if (fRecent.Items().empty()) {
		BMenuItem* none = new BMenuItem("(no recent files)", NULL);
		none->SetEnabled(false);
		fRecentMenu->AddItem(none);
		return;
	}
	for (const BString& path : fRecent.Items()) {
		BMessage* m = new BMessage('ppRc');
		m->AddString("path", path);
		fRecentMenu->AddItem(
			new BMenuItem(BPath(path.String()).Leaf(), m));
	}
}

void
PPWindow::RememberRecent(const BString& path)
{
	fRecent.Remember(path);
	RebuildRecentMenu();
	if (fRecentPath.Length() > 0)
		fRecent.Save(fRecentPath.String());
}

void
PPWindow::DoExportPDF(const BString& pathStr)
{
	// exports never touch document state
	status_t err = PP_WritePDF(fDoc, pathStr.String());
	if (err == B_OK) {
		WriteAttrOn(pathStr.String(), "BEOS:TYPE", "application/pdf");
		UpdateStatus();
	} else {
		BString msg;
		msg.SetToFormat("Could not write %s: %s", pathStr.String(),
			strerror(err));
		(new BAlert("ProsePaint", msg.String(), "OK"))->Go(NULL);
	}
}

void
PPWindow::DoSave(const BString& pathStr)
{
	status_t err = fDoc.SaveToFile(pathStr.String());
	if (err == B_OK) {
		fFilePath = pathStr;
		fDoc.SavedClean();
		WriteAttrOn(pathStr.String(), "BEOS:TYPE", PPDocument::kDocType);
		RememberRecent(pathStr);
	} else {
		BString msg;
		msg.SetToFormat("Could not save %s: %s", pathStr.String(),
			strerror(err));
		(new BAlert("ProsePaint", msg.String(), "OK"))->Go(NULL);
	}
	UpdateStatus();
}

status_t
PPWindow::OpenFile(const entry_ref& ref)
{
	BPath path(&ref);
	PPDocKind kind = PP_SniffDocument(path.Path());
	if (kind != PP_KIND_NATIVE) {
		BString msg;
		msg.SetToFormat("ProsePaint could not open %s (kind %d): "
			"not a painting", path.Path(), (int)kind);
		(new BAlert("ProsePaint", msg.String(), "OK"))->Go(NULL);
		return B_BAD_TYPE;
	}
	status_t err = fDoc.LoadFromFile(path.Path());
	if (err != B_OK) {
		BString msg;
		msg.SetToFormat("ProsePaint could not open %s: %s", path.Path(),
			strerror(err));
		(new BAlert("ProsePaint", msg.String(), "OK"))->Go(NULL);
		return err;
	}
	fFilePath = path.Path();
	RememberRecent(fFilePath);
	fCanvas->DocChanged();
	RefreshLayers();
	UpdateStatus();
	return B_OK;
}

void
PPWindow::SetPaper(int32 index)
{
	if (index < 0 || kPapers[index].name == NULL)
		return;
	fPaperIndex = index;
	PPPaper paper;
	paper.name = kPapers[index].name;
	paper.widthPt = kPapers[index].w;
	paper.heightPt = kPapers[index].h;
	fDoc.SetCanvas(
		(int32)(paper.widthPt * fDoc.DPI() / 72.0f + 0.5f),
		(int32)(paper.heightPt * fDoc.DPI() / 72.0f + 0.5f),
		fDoc.DPI(), paper, true);
	fCanvas->DocChanged();
	RefreshLayers();
	UpdateStatus();
}

bool
PPWindow::QuitRequested()
{
	if (fDoc.IsModified()) {
		BAlert* alert = new BAlert("ProsePaint",
			"Save changes before closing?", "Cancel", "Don't save", "Save",
			B_WIDTH_AS_USUAL, B_OFFSET_SPACING, B_WARNING_ALERT);
		int32 choice = alert->Go();
		if (choice == 0)
			return false;
		if (choice == 2) {
			if (fFilePath.Length()) {
				DoSave(fFilePath);
				if (fDoc.IsModified())
					return false;
			} else
				return false;
		}
	}
	fQuitting = true;
	return true;
}

void
PPWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case TOOL_MSG:
		{
			int32 tool = 0;
			message->FindInt32("tool", &tool);
			fCanvas->SetTool((PPTool)tool);
			for (int32 i = 0; i < PP_TOOL_COUNT; i++) {
				BButton* b = dynamic_cast<BButton*>(fPalette->ChildAt(i));
				if (b != NULL)
					b->SetValue(i == tool ? B_CONTROL_ON : B_CONTROL_OFF);
			}
			UpdateStatus();
			break;
		}
		case PROP_MSG:
		{
			int32 field = 0;
			message->FindInt32("field", &field);
			if (field == 0) {
				int32 index = PP_BRUSH_SOFT;
				message->FindInt32("index", &index);
				fCanvas->Brush().SetShape((PPBrushShape)index);
			} else if (field == 1) {
				int32 size = 8;
				message->FindInt32("size", &size);
				fCanvas->Brush().SetSize(size);
			} else if (field == 2) {
				int32 hard = 200;
				message->FindInt32("hard", &hard);
				fCanvas->Brush().SetHardness((uint8)hard);
			} else if (field == 14) {
				// stroke opacity percent
				int32 v = 100;
				message->FindInt32("opaq", &v);
				fCanvas->SetOpacityPercent(v);
			} else if (field >= 10 && field <= 12) {
				rgb_color c = fCanvas->Colour();
				int32 v = atoi((field == 10 ? fR : field == 11 ? fG : fB)
					->Text());
				if (v < 0) v = 0;
				if (v > 255) v = 255;
				if (field == 10) c.red = (uint8)v;
				else if (field == 11) c.green = (uint8)v;
				else c.blue = (uint8)v;
				fCanvas->SetColour(c);
			} else if (field == 13) {
				// scripted colour: all three channels at once
				rgb_color c = fCanvas->Colour();
				int32 v = 0;
				if (message->FindInt32("red", &v) == B_OK)
					c.red = (uint8)std::max(0, std::min(255, v));
				if (message->FindInt32("green", &v) == B_OK)
					c.green = (uint8)std::max(0, std::min(255, v));
				if (message->FindInt32("blue", &v) == B_OK)
					c.blue = (uint8)std::max(0, std::min(255, v));
				fCanvas->SetColour(c);
				fR->SetText(BString().SetToFormat("%d", (int)c.red).String());
				fG->SetText(BString().SetToFormat("%d", (int)c.green).String());
				fB->SetText(BString().SetToFormat("%d", (int)c.blue).String());
			}
			break;
		}
		case SWATCH_MSG:
		{
			int32 index = 0;
			message->FindInt32("index", &index);
			if (index >= 0 && index < kColourCount) {
				fCanvas->SetColour(kColours[index].c);
				fR->SetText(BString().SetToFormat("%d",
					(int)kColours[index].c.red).String());
				fG->SetText(BString().SetToFormat("%d",
					(int)kColours[index].c.green).String());
				fB->SetText(BString().SetToFormat("%d",
					(int)kColours[index].c.blue).String());
			}
			break;
		}
		case LAYER_MSG:
		{
			if (fRefreshing)
				break;
			int32 sel = fLayerList->CurrentSelection();
			if (sel >= 0)
				fDoc.SetActiveLayer(
					fDoc.LayerAt(fDoc.CountLayers() - 1 - sel)->id);
			UpdateStatus();
			break;
		}
		case 'ppLa':
		{
			fDoc.AddLayer();
			RefreshLayers();
			fCanvas->DocChanged();
			UpdateStatus();
			break;
		}
		case 'ppLd':
		{
			PPLayer* active = fDoc.ActiveLayer();
			if (active != NULL)
				fDoc.DeleteLayer(active->id);
			RefreshLayers();
			fCanvas->DocChanged();
			UpdateStatus();
			break;
		}
		case 'ppLu': case 'ppLw':
		{
			PPLayer* active = fDoc.ActiveLayer();
			if (active != NULL)
				fDoc.MoveLayer(active->id, message->what == 'ppLu');
			RefreshLayers();
			fCanvas->Invalidate();
			UpdateStatus();
			break;
		}
		case 'ppLv':
		{
			PPLayer* active = fDoc.ActiveLayer();
			if (active != NULL)
				fDoc.SetLayerVisible(active->id, !active->visible);
			RefreshLayers();
			fCanvas->Invalidate();
			UpdateStatus();
			break;
		}
		case 'ppUd':
			fDoc.Undo();
			fCanvas->Invalidate();
			UpdateStatus();
			break;
		case 'ppRd':
			fDoc.Redo();
			fCanvas->Invalidate();
			UpdateStatus();
			break;
		case 'ppPp':
		{
			int32 index = 0;
			message->FindInt32("index", &index);
			SetPaper(index);
			break;
		}
		case 'ppNw':
			SetPaper(fPaperIndex);
			break;
		case 'ppSv':
			if (fFilePath.Length())
				DoSave(fFilePath);
			else
				PostMessage(SAVE_PANEL_MSG);
			break;
		case OPEN_PANEL_MSG:
			if (fOpenPanel == NULL)
				fOpenPanel = new BFilePanel(B_OPEN_PANEL,
					new BMessenger(this), NULL, B_FILE_NODE, false,
					new BMessage('ppOo'));
			fOpenPanel->Show();
			break;
		case 'ppOo':
		{
			entry_ref ref;
			if (message->FindRef("refs", &ref) == B_OK)
				OpenFile(ref);
			break;
		}
		case SAVE_PANEL_MSG:
			if (fSavePanel == NULL)
				fSavePanel = new BFilePanel(B_SAVE_PANEL,
					new BMessenger(this), NULL, B_FILE_NODE, false,
					new BMessage('ppSo'));
			{
				BString suggested = fFilePath.Length()
					? BString(BPath(fFilePath.String()).Leaf())
					: BString("Untitled");
				fSavePanel->SetSaveText(
					WithExtension(suggested, ".paint").String());
			}
			fSavePanel->Show();
			break;
		case 'ppSo':
		{
			entry_ref ref;
			if (message->FindRef("directory", &ref) == B_OK) {
				BPath path(&ref);
				BString name;
				message->FindString("name", &name);
				path.Append(WithExtension(name, ".paint").String());
				DoSave(BString(path.Path()));
			}
			break;
		}
		case STATUS_MSG:
			UpdateStatus();
			break;
		case 'ppSt':
		{
			// scripted stroke: the harness's brush
			float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
			message->FindFloat("x0", &x0);
			message->FindFloat("y0", &y0);
			message->FindFloat("x1", &x1);
			message->FindFloat("y1", &y1);
			fCanvas->StrokeSegment(x0, y0, x1, y1);
			UpdateStatus();
			break;
		}
		case 'ppDb':
		{
			float x = 0, y = 0;
			message->FindFloat("x", &x);
			message->FindFloat("y", &y);
			fCanvas->DabAt(x, y);
			UpdateStatus();
			break;
		}
		case 'ppLx':
		{
			BString op;
			if (message->FindString("op", &op) != B_OK)
				break;
			PPLayer* active = fDoc.ActiveLayer();
			if (op == "add") {
				fDoc.AddLayer();
			} else if (active != NULL && op == "delete") {
				fDoc.DeleteLayer(active->id);
			} else if (active != NULL && op == "up") {
				fDoc.MoveLayer(active->id, true);
			} else if (active != NULL && op == "down") {
				fDoc.MoveLayer(active->id, false);
			} else if (active != NULL && op.IStartsWith("visible")) {
				int32 v = 1;
				sscanf(op.String(), "%*s %d", &v);
				fDoc.SetLayerVisible(active->id, v != 0);
			} else if (active != NULL && op.IStartsWith("opacity")) {
				int32 v = 100;
				sscanf(op.String(), "%*s %d", &v);
				if (v < 0) v = 0;
				if (v > 100) v = 100;
				fDoc.SetLayerOpacity(active->id,
					(uint8)(v * 255 / 100));
			} else if (active != NULL && op.IStartsWith("rename")) {
				const char* name = strstr(op.String(), " ");
				if (name != NULL && name[1] != '\0')
					fDoc.RenameLayer(active->id, name + 1);
			}
			RefreshLayers();
			fCanvas->DocChanged();
			UpdateStatus();
			break;
		}
		case 'ppAd':
		{
			// a scripted save adopted this path
			message->FindString("path", &fFilePath);
			BString adopted = fFilePath;
			if (adopted.Length() > 0)
				RememberRecent(adopted);
			UpdateStatus();
			break;
		}
		case 'ppRc':
		{
			// Open Recent selection
			BString path;
			if (message->FindString("path", &path) == B_OK) {
				entry_ref ref;
				if (get_ref_for_path(path.String(), &ref) == B_OK)
					OpenFile(ref);
			}
			break;
		}
		case 'ppRs':
			if (fResizeWin == NULL) {
				fResizeWin = new PPResizeWindow(this, fDoc.Width(),
					fDoc.Height());
				fResizeWin->Show();
			} else
				fResizeWin->Activate();
			break;
		case 'ppRz':
		{
			int32 w = 0, h = 0;
			message->FindInt32("w", &w);
			message->FindInt32("h", &h);
			fDoc.Resize(w, h);
			fCanvas->DocChanged();
			UpdateStatus();
			break;
		}
		case 'ppRq':
			fResizeWin = NULL;
			break;
		case 'ppSh':
		{
			BString spec;
			if (message->FindString("spec", &spec) == B_OK)
				fCanvas->ShapeStroke(spec.String());
			break;
		}
		case 'ppOp':
		{
			int32 v = 100;
			message->FindInt32("opaq", &v);
			fCanvas->SetOpacityPercent(v);
			// keep the menu field in step
			if (fOpaqField != NULL) {
				int32 best = 0;
				int32 bestDelta = 999;
				for (int32 i = 0; i < fOpaqField->Menu()->CountItems();
						i++) {
					int32 val = 100;
					fOpaqField->Menu()->ItemAt(i)->Message()
						->FindInt32("opaq", &val);
					int32 delta = abs(val - v);
					if (delta < bestDelta) {
						bestDelta = delta;
						best = i;
					}
				}
				fOpaqField->Menu()->ItemAt(best)->SetMarked(true);
			}
			break;
		}
		case PDF_PANEL_MSG:
			if (fPDFPanel == NULL)
				fPDFPanel = new BFilePanel(B_SAVE_PANEL,
					new BMessenger(this), NULL, B_FILE_NODE, false,
					new BMessage('ppPf'));
			{
				BString suggested = fFilePath.Length()
					? BString(BPath(fFilePath.String()).Leaf())
					: BString("Untitled");
				fPDFPanel->SetSaveText(
					WithExtension(suggested, ".pdf").String());
			}
			fPDFPanel->Show();
			break;
		case 'ppPf':
		{
			entry_ref ref;
			if (message->FindRef("directory", &ref) == B_OK) {
				BPath path(&ref);
				BString name;
				message->FindString("name", &name);
				path.Append(WithExtension(name, ".pdf").String());
				DoExportPDF(BString(path.Path()));
			}
			break;
		}
		case 'ppPx':
		{
			// scripted PDF export — runs on the window thread
			BString path;
			if (message->FindString("path", &path) == B_OK
				&& path.Length())
				DoExportPDF(path);
			break;
		}
		case ZOOM_MSG:
		{
			float z = 1.0f;
			message->FindFloat("zoom", &z);
			fCanvas->SetZoom(z);
			if (fZoomMenu != NULL) {
				for (int32 i = 0; i < fZoomMenu->CountItems(); i++) {
					float itemZoom = 0;
					fZoomMenu->ItemAt(i)->Message()
						->FindFloat("zoom", &itemZoom);
					fZoomMenu->ItemAt(i)->SetMarked(
						itemZoom == fCanvas->Zoom());
				}
			}
			UpdateStatus();
			break;
		}
		case B_REFS_RECEIVED:
		{
			entry_ref ref;
			if (message->FindRef("refs", &ref) == B_OK)
				OpenFile(ref);
			break;
		}
		default:
			BWindow::MessageReceived(message);
	}
}

// -------------------------------------------------------------------- app --
static property_info sPPProperties[] = {
	{ "LayerCount",
		{ B_GET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"number of layers", 0, { B_INT32_TYPE } },
	{ "Zoom",
		{ B_GET_PROPERTY, B_SET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"canvas zoom percent (set: 25..400)", 0,
		{ B_INT32_TYPE, B_INT32_TYPE } },
	{ "Pixel",
		{ B_GET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"composite colour at \"x y\" (data), as \"r g b a\"", 0,
		{ B_STRING_TYPE } },
	{ "Activate",
		{ B_EXECUTE_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"bring the window forward and focus the canvas (harness)", 0,
		{ 0 } },
	{ "Tool",
		{ B_SET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"set the tool: pen/brush/eraser/smudge/fill/picker", 0,
		{ B_STRING_TYPE } },
	{ "Brush",
		{ B_SET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"set the brush: \"shape size hardness\" (round/soft/square/"
		"angled)", 0, { B_STRING_TYPE } },
	{ "Colour",
		{ B_SET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"set the ink colour: \"r g b\"", 0, { B_STRING_TYPE } },
	{ "Opacity",
		{ B_SET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"stroke opacity percent (strokes land once, at this strength)",
		0, { B_STRING_TYPE } },
	{ "Shape",
		{ B_EXECUTE_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"shape stroke: \"line x0 y0 x1 y1\" / \"rect x y w h\" / "
		"\"ellipse x y w h\"", 0, { B_STRING_TYPE } },
	{ "Canvas",
		{ B_SET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"resize the canvas: \"w h\" pixels (undo history clears)", 0,
		{ B_STRING_TYPE } },
	{ "StrokeLine",
		{ B_EXECUTE_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"paint a stroke: \"x0 y0 x1 y1\" in canvas pixels", 0,
		{ B_STRING_TYPE } },
	{ "Dab",
		{ B_EXECUTE_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"one brush dab: \"x y\" in canvas pixels", 0, { B_STRING_TYPE } },
	{ "Layer",
		{ B_EXECUTE_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"layer ops: add/delete/up/down/visible n/opacity n/rename s", 0,
		{ B_STRING_TYPE } },
	{ "Save",
		{ B_EXECUTE_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"save the painting (data: path)", 0, { B_STRING_TYPE } },
	{ "PDF",
		{ B_EXECUTE_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"export the painting as a PDF page (data: path)", 0,
		{ B_STRING_TYPE } },
	{ "Open",
		{ B_EXECUTE_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"open a painting (data: path)", 0, { B_STRING_TYPE } },
	{ "Quit",
		{ B_EXECUTE_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"close every window and quit", 0, { 0 } },
	{ 0 }
};

const BPropertyInfo kPPScriptingProperties(sPPProperties);

static void
ReplyString(BMessage* message, const char* value)
{
	BMessage reply(B_REPLY);
	reply.AddString("result", value);
	message->SendReply(&reply);
}

static void
ReplyError(BMessage* message, const char* error)
{
	BMessage reply(B_ERROR);
	reply.AddString("error", error);
	message->SendReply(&reply);
}

static void
ReplyInt(BMessage* message, int32 value)
{
	BMessage reply(B_REPLY);
	reply.AddInt32("result", value);
	message->SendReply(&reply);
}

// documents given on the command line (refs may arrive before the
// window exists — the ProseDraw lesson)
static std::vector<BString> gOpenPaths;

class PPApp : public BApplication {
public:
			PPApp()
				:
				BApplication("application/x-vnd.prose.ProsePaint")
			{
			}

	status_t GetSupportedSuites(BMessage* message) override
	{
		message->AddString("suites", "suite/x-vnd.prose.ProsePaint");
		message->AddFlat("messages", &kPPScriptingProperties);
		return BApplication::GetSupportedSuites(message);
	}

	BHandler* ResolveSpecifier(BMessage* message, int32 index,
		BMessage* specifier, int32 what, const char* property) override
	{
		if (kPPScriptingProperties.FindMatch(message, index, specifier,
				what, property) >= 0)
			return this;
		return BApplication::ResolveSpecifier(message, index, specifier,
			what, property);
	}

	void	ReadyToRun() override
	{
		BScreen screen(B_MAIN_SCREEN_ID);
		BRect avail = screen.Frame().InsetByCopy(40, 36);
		PPWindow* window = new PPWindow(
			BRect(avail.left, avail.top, avail.left + 940,
				avail.top + 700), "Untitled");
		window->Show();
		for (const BString& path : gOpenPaths) {
			entry_ref ref;
			if (get_ref_for_path(path.String(), &ref) == B_OK) {
				BMessage open('ppOo');
				open.AddRef("refs", &ref);
				window->PostMessage(&open);
			}
		}
		gOpenPaths.clear();
		if (fPendingRefs.what == B_REFS_RECEIVED) {
			window->PostMessage(&fPendingRefs);
			fPendingRefs.what = 0;
		}
		SetPreferredHandler(this);
	}

	bool	QuitRequested() override
	{
		// the ProseDraw lesson: when the last window closes, the auto
		// quit-request can arrive while the window is still in the
		// list — re-posting to it and returning false leaves the app
		// alive with zero windows. Only windows that are NOT already
		// quitting hold the app open.
		bool any = false;
		for (int32 i = CountWindows() - 1; i >= 0; i--) {
			PPWindow* w = dynamic_cast<PPWindow*>(WindowAt(i));
			if (w == NULL)
				continue;
			bool quitting = true;
			if (w->Lock()) {
				quitting = w->IsQuitting();
				w->Unlock();
			}
			if (!quitting) {
				w->PostMessage(B_QUIT_REQUESTED);
				any = true;
			}
		}
		return !any;
	}

	void	MessageReceived(BMessage* message) override;

	BMessage	fPendingRefs;
};

static bool
HandleScripting(PPWindow* window, BMessage* message, const char* property)
{
	if (property == NULL || !property[0])
		return false;
	PPDocument& doc = *window->Document();
	BString prop = property;
	bool isGet = message->what == B_GET_PROPERTY;
	bool isSet = message->what == B_SET_PROPERTY;
	bool isExec = message->what == B_EXECUTE_PROPERTY;

	if (prop == "LayerCount" && isGet) {
		ReplyInt(message, doc.CountLayers());
		return true;
	}
	if (prop == "Zoom" && isGet) {
		ReplyInt(message, (int32)roundf(window->Canvas()->Zoom() * 100));
		return true;
	}
	if (prop == "Zoom" && isSet) {
		// factor or percent, hey sends ints and pwquery strings
		float z = -1.0f;
		float f = 0.0f;
		int32 i = 0;
		BString s;
		if (message->FindFloat("data", &f) == B_OK)
			z = f;
		else if (message->FindInt32("data", &i) == B_OK)
			z = i;
		else if (message->FindString("data", &s) == B_OK)
			z = atof(s.String());
		if (z > 4.0f && z <= 400.0f)
			z /= 100.0f;	// percent
		BMessage zoom(PPWindow::ZOOM_MSG);
		zoom.AddFloat("zoom", z);
		window->PostMessage(&zoom);
		z = fminf(4.0f, fmaxf(0.25f, z));
		ReplyInt(message, (int32)roundf(z * 100));
		return true;
	}
	if (prop == "Pixel" && isGet) {
		BString data;
		if (message->FindString("data", &data) != B_OK) {
			ReplyError(message, "data: \"x y\" required");
			return true;
		}
		int32 x = -1, y = -1;
		if (sscanf(data.String(), "%d %d", &x, &y) != 2) {
			ReplyError(message, "data: \"x y\" required");
			return true;
		}
		rgb_color c = doc.PickColour(x, y);
		BString out;
		out.SetToFormat("%d %d %d %d", (int)c.red, (int)c.green,
			(int)c.blue, (int)c.alpha);
		ReplyString(message, out.String());
		return true;
	}
	if (prop == "Activate" && isExec) {
		// the ProseDraw lesson: lock, activate, then re-focus after
		// activation settles
		if (window->Lock()) {
			window->Activate();
			window->Canvas()->MakeFocus();
			window->Unlock();
		}
		window->PostMessage(PPWindow::STATUS_MSG);
		ReplyString(message, "");
		return true;
	}
	if (prop == "Tool" && isSet) {
		BString data;
		message->FindString("data", &data);
		PPTool tool = PP_TOOL_BRUSH;
		if (data.IStartsWith("pen")) tool = PP_TOOL_PEN;
		else if (data.IStartsWith("brush")) tool = PP_TOOL_BRUSH;
		else if (data.IStartsWith("air") || data.IStartsWith("spray"))
			tool = PP_TOOL_AIRBRUSH;
		else if (data.IStartsWith("eraser")) tool = PP_TOOL_ERASER;
		else if (data.IStartsWith("smudge")) tool = PP_TOOL_SMUDGE;
		else if (data.IStartsWith("fill")) tool = PP_TOOL_FILL;
		else if (data.IStartsWith("pick") || data.IStartsWith("eyedrop"))
			tool = PP_TOOL_EYEDROPPER;
		else if (data.IStartsWith("line")) tool = PP_TOOL_LINE;
		else if (data.IStartsWith("rect")) tool = PP_TOOL_RECT;
		else if (data.IStartsWith("oval") || data.IStartsWith("ellipse"))
			tool = PP_TOOL_ELLIPSE;
		else {
			ReplyError(message,
				"tool: pen/brush/airbrush/eraser/smudge/fill/picker/"
				"line/rect/ellipse");
			return true;
		}
		BMessage m(PPWindow::TOOL_MSG);
		m.AddInt32("tool", (int32)tool);
		window->PostMessage(&m);
		ReplyString(message, "");
		return true;
	}
	if (prop == "Brush" && isSet) {
		BString data;
		if (message->FindString("data", &data) != B_OK
				|| !data.Length()) {
			ReplyError(message, "data: \"shape size hardness\" required");
			return true;
		}
		char shapeWord[16] = "soft";
		int32 size = 8, hard = 200;
		sscanf(data.String(), "%15s %d %d", shapeWord, &size, &hard);
		PPBrushShape shape = PP_BRUSH_SOFT;
		if (!strcasecmp(shapeWord, "round")) shape = PP_BRUSH_ROUND;
		else if (!strcasecmp(shapeWord, "square")) shape = PP_BRUSH_SQUARE;
		else if (!strcasecmp(shapeWord, "angled")
			|| !strcasecmp(shapeWord, "callig"))
				shape = PP_BRUSH_ANGLED;
		// all three properties in one post; the window applies them in
		// message order (shape, size, edge)
		BMessage shapeMsg(PPWindow::PROP_MSG);
		shapeMsg.AddInt32("field", 0);
		shapeMsg.AddInt32("index", (int32)shape);
		window->PostMessage(&shapeMsg);
		BMessage sizeMsg(PPWindow::PROP_MSG);
		sizeMsg.AddInt32("field", 1);
		sizeMsg.AddInt32("size", size);
		window->PostMessage(&sizeMsg);
		BMessage hardMsg(PPWindow::PROP_MSG);
		hardMsg.AddInt32("field", 2);
		hardMsg.AddInt32("hard", hard);
		window->PostMessage(&hardMsg);
		ReplyString(message, "");
		return true;
	}
	if (prop == "Colour" && isSet) {
		BString data;
		int32 r = 0, g2 = 0, b = 0;
		if (message->FindString("data", &data) != B_OK
			|| sscanf(data.String(), "%d %d %d", &r, &g2, &b) != 3) {
			ReplyError(message, "data: \"r g b\" required");
			return true;
		}
		BMessage mc(PPWindow::PROP_MSG);
		mc.AddInt32("field", 13);
		mc.AddInt32("red", r);
		mc.AddInt32("green", g2);
		mc.AddInt32("blue", b);
		window->PostMessage(&mc);
		ReplyString(message, "");
		return true;
	}
	if (prop == "Opacity" && isSet) {
		BString data;
		int32 v = 100;
		if (message->FindString("data", &data) != B_OK
			|| sscanf(data.String(), "%d", &v) != 1) {
			ReplyError(message, "data: percent required");
			return true;
		}
		if (v < 0) v = 0;
		if (v > 100) v = 100;
		BMessage m('ppOp');
		m.AddInt32("opaq", v);
		window->PostMessage(&m);
		ReplyString(message, "");
		return true;
	}
	if (prop == "Shape" && isExec) {
		BString data;
		if (message->FindString("data", &data) != B_OK || !data.Length()) {
			ReplyError(message,
				"data: \"line x0 y0 x1 y1\" / \"rect|ellipse x y w h\"");
			return true;
		}
		BMessage m('ppSh');
		m.AddString("spec", data);
		window->PostMessage(&m);
		ReplyString(message, "");
		return true;
	}
	if (prop == "Canvas" && isSet) {
		BString data;
		int32 w = 0, h = 0;
		if (message->FindString("data", &data) != B_OK
			|| sscanf(data.String(), "%d %d", &w, &h) != 2
			|| w < 1 || h < 1) {
			ReplyError(message, "data: \"w h\" pixels required");
			return true;
		}
		BMessage m('ppRz');
		m.AddInt32("w", w);
		m.AddInt32("h", h);
		window->PostMessage(&m);
		ReplyString(message, "");
		return true;
	}
	if ((prop == "StrokeLine" || prop == "Dab") && isExec) {
		BString data;
		if (message->FindString("data", &data) != B_OK || !data.Length()) {
			ReplyError(message, "data: coordinates required");
			return true;
		}
		if (prop == "Dab") {
			float x = 0, y = 0;
			if (sscanf(data.String(), "%f %f", &x, &y) != 2) {
				ReplyError(message, "data: \"x y\" required");
				return true;
			}
			BMessage m('ppDb');
			m.AddFloat("x", x);
			m.AddFloat("y", y);
			window->PostMessage(&m);
		} else {
			float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
			if (sscanf(data.String(), "%f %f %f %f", &x0, &y0, &x1, &y1)
					!= 4) {
				ReplyError(message, "data: \"x0 y0 x1 y1\" required");
				return true;
			}
			BMessage m('ppSt');
			m.AddFloat("x0", x0);
			m.AddFloat("y0", y0);
			m.AddFloat("x1", x1);
			m.AddFloat("y1", y1);
			window->PostMessage(&m);
		}
		ReplyString(message, "");
		return true;
	}
	if (prop == "Layer" && isExec) {
		BString data;
		if (message->FindString("data", &data) != B_OK || !data.Length()) {
			ReplyError(message, "data: layer op required");
			return true;
		}
		BMessage m('ppLx');
		m.AddString("op", data);
		window->PostMessage(&m);
		ReplyString(message, "");
		return true;
	}
	if (prop == "Save" && isExec) {
		BString path;
		if (message->FindString("data", &path) == B_OK
			&& path.Length()) {
			// quiet, off the app looper: save the model directly
			status_t err = doc.SaveToFile(path.String());
			if (err == B_OK) {
				doc.SavedClean();
				WriteAttrOn(path.String(), "BEOS:TYPE",
					PPDocument::kDocType);
				BMessage adopt('ppAd');
				adopt.AddString("path", path);
				window->PostMessage(&adopt);
			}
			if (err == B_OK)
				ReplyString(message, "");
			else
				ReplyError(message, strerror(err));
		} else
			ReplyError(message, "data: path required");
		return true;
	}
	if (prop == "PDF" && isExec) {
		BString path;
		if (message->FindString("data", &path) == B_OK
			&& path.Length()) {
			// pure output: no document state changes. The export
			// itself runs on the window thread via the panel path's
			// message so the composite read is thread-safe.
			BMessage m('ppPx');
			m.AddString("path", path);
			window->PostMessage(&m);
			ReplyString(message, "");
		} else
			ReplyError(message, "data: path required");
		return true;
	}
	if (prop == "Open" && isExec) {
		BString path;
		if (message->FindString("data", &path) == B_OK
			&& path.Length()) {
			entry_ref ref;
			if (get_ref_for_path(path.String(), &ref) == B_OK) {
				BMessage open('ppOo');
				open.AddRef("refs", &ref);
				window->PostMessage(&open);
				ReplyString(message, "");
			} else
				ReplyError(message, "bad path");
		} else
			ReplyError(message, "data: path required");
		return true;
	}
	if (prop == "Quit" && isExec) {
		window->PostMessage(B_QUIT_REQUESTED);
		ReplyString(message, "");
		return true;
	}
	return false;
}

void
PPApp::MessageReceived(BMessage* message)
{
	if (message->what == B_REFS_RECEIVED) {
		PPWindow* w = dynamic_cast<PPWindow*>(WindowAt(0));
		if (w != NULL)
			w->PostMessage(message);
		else
			fPendingRefs = *message;
	}
	if (message->HasSpecifiers()) {
		PPWindow* w = dynamic_cast<PPWindow*>(WindowAt(0));
		if (w != NULL) {
			BMessage spec;
			int32 what = 0;
			int32 index = 0;
			const char* prop = NULL;
			if (message->GetCurrentSpecifier(&index, &spec, &what,
					&prop) == B_OK
				&& kPPScriptingProperties.FindMatch(message, index, &spec,
					what, prop) >= 0) {
				if (HandleScripting(w, message, prop))
					return;
			}
		}
	}
	BApplication::MessageReceived(message);
}

// --------------------------------------------------------------- selftest --
struct SelfCase { const char* name; bool ok; };
static std::vector<SelfCase> sCases;
static bool sAll = true;
#define CHECK(label, cond) { bool ok_ = (cond); \
	sCases.push_back(SelfCase{ label, ok_ }); if (!ok_) sAll = false; }

static int
SelfTest()
{
	printf("block: canvas\n"); fflush(stdout);
	{
		PPDocument doc;
		CHECK("A4@96 default width", doc.Width() == 793);
		CHECK("A4@96 default height", doc.Height() == 1123);
		CHECK("default background layer", doc.CountLayers() == 1
			&& doc.LayerAt(0) != NULL && doc.LayerAt(0)->name == "Background");
		rgb_color c = doc.PickColour(10, 10);
		CHECK("background is white", c.red == 255 && c.green == 255
			&& c.blue == 255 && c.alpha == 255);
		// custom canvas
		PPPaper p;
		doc.SetCanvas(64, 48, 96.0f, p, false);
		CHECK("custom canvas size", doc.Width() == 64 && doc.Height() == 48);
		CHECK("empty canvas transparent",
			doc.PickColour(5, 5).alpha == 0);
	}

	printf("block: brush\n"); fflush(stdout);
	{
		PPBrush b;
		b.SetShape(PP_BRUSH_ROUND);
		b.SetSize(9);
		CHECK("round mask centre full", b.Mask()[4 * b.MaskBpr() + 4] == 255);
		CHECK("round mask corner empty",
			b.Mask()[0] == 0 && b.Mask()[8 * b.MaskBpr() + 8] == 0);
		CHECK("round mask symmetric",
			b.Mask()[2 * b.MaskBpr() + 2] == b.Mask()[6 * b.MaskBpr() + 6]);
		b.SetShape(PP_BRUSH_SQUARE);
		CHECK("square mask corner full", b.Mask()[0] == 255);
		b.SetShape(PP_BRUSH_SOFT);
		b.SetHardness(255);
		CHECK("soft at full hardness is hard",
			b.Mask()[4 * b.MaskBpr() + 4] == 255);
		b.SetHardness(40);
		uint8 centre = b.Mask()[4 * b.MaskBpr() + 4];
		uint8 mid = b.Mask()[4 * b.MaskBpr() + 6];
		uint8 rim = b.Mask()[4 * b.MaskBpr() + 8];
		CHECK("soft mask falls off outward", centre >= mid && mid > rim);
		b.SetShape(PP_BRUSH_ANGLED);
		b.SetSize(15);
		CHECK("angled mask is thin",
			b.Mask()[7 * b.MaskBpr() + 7] == 255
			&& b.Mask()[2 * b.MaskBpr() + 2] == 0);
		// StampLine stamps along the path
		struct Counter { int n; } counter = { 0 };
		b.SetShape(PP_BRUSH_ROUND);
		b.SetSize(8);
		b.StampLine(0, 0, 100, 0,
			[](void* ctx, int32, int32) { ((Counter*)ctx)->n++; }, &counter);
		CHECK("stampline spaces along the path", counter.n > 10);
		b.SetSize(999);
		CHECK("brush size clamped above", b.Size() == 64);
		b.SetSize(1);
		CHECK("brush size clamped below", b.Size() == 1);
	}

	printf("block: paint\n"); fflush(stdout);
	{
		PPDocument doc;
		PPPaper p;
		doc.SetCanvas(128, 128, 96.0f, p, false);
		PPBrush b;
		b.SetShape(PP_BRUSH_ROUND);
		b.SetSize(9);
		rgb_color red = { 216, 40, 40, 255 };
		doc.StrokeBegin();
		doc.DabColour(64, 64, red, b.Mask(), b.MaskSize(), b.MaskBpr(), 255);
		doc.StrokeEnd();
		rgb_color got = doc.PickColour(64, 64);
		CHECK("dab paints its colour", got.red == 216 && got.green == 40
			&& got.blue == 40 && got.alpha == 255);
		CHECK("dab stays inside the mask",
			doc.PickColour(64 - 20, 64).alpha == 0);
		// undo restores, redo reapplies
		CHECK("stroke is undoable", doc.CanUndo());
		doc.Undo();
		CHECK("undo clears the dab", doc.PickColour(64, 64).alpha == 0);
		doc.Redo();
		CHECK("redo paints again", doc.PickColour(64, 64).red == 216);
		// eraser
		doc.StrokeBegin();
		doc.DabErase(64, 64, b.Mask(), b.MaskSize(), b.MaskBpr(), 255);
		doc.StrokeEnd();
		CHECK("eraser removes ink", doc.PickColour(64, 64).alpha == 0);
		doc.Undo();
		CHECK("erase undoes", doc.PickColour(64, 64).alpha == 255);
		// fill
		doc.StrokeBegin();
		int32 filled = doc.FillAt(0, 0, red, 8);
		doc.StrokeEnd();
		CHECK("fill covers the canvas", filled >= 128 * 128 - 400);
		CHECK("fill colour lands", doc.PickColour(3, 3).red == 216);
		doc.Undo();
		CHECK("fill undoes", doc.PickColour(3, 3).alpha == 0);
	}

	printf("block: smudge\n"); fflush(stdout);
	{
		PPDocument doc;
		PPPaper p;
		doc.SetCanvas(96, 32, 96.0f, p, false);
		// hard left half red, right half blue: exact pixels, one stroke
		rgb_color red = { 216, 40, 40, 255 };
		rgb_color blue = { 40, 40, 216, 255 };
		uint8 one[1] = { 255 };
		doc.StrokeBegin();
		for (int32 y = 0; y < 32; y++) {
			for (int32 x = 0; x < 48; x++)
				doc.DabColour(x, y, red, one, 1, 1, 255);
			for (int32 x = 48; x < 96; x++)
				doc.DabColour(x, y, blue, one, 1, 1, 255);
		}
		doc.StrokeEnd();
		CHECK("left is red", doc.PickColour(4, 16).red == 216);
		CHECK("right is blue", doc.PickColour(92, 16).blue == 216);
		// smudge across the boundary: a realistic strength carries the
		// red while picking up blue on the way — full 255 would drag
		// the original pickup unchanged (also useful, but no mixing)
		PPBrush b;
		b.SetShape(PP_BRUSH_ROUND);
		b.SetSize(9);
		doc.StrokeBegin();
		for (int32 x = 44; x <= 56; x++)
			doc.DabSmudge(x, 16, b.Mask(), b.MaskSize(), b.MaskBpr(), 200);
		doc.StrokeEnd();
		// the mixing happens at the stroke's flanks (the centre line
		// keeps reading its own trail): SOME pixel in the band must
		// now hold both inks
		bool mixed = false;
		for (int32 y = 12; y <= 20 && !mixed; y++)
			for (int32 x = 48; x <= 58; x++) {
				rgb_color c = doc.PickColour(x, y);
				if (c.red > 60 && c.blue > 60) {
					mixed = true;
					break;
				}
			}
		CHECK("smudge drags red into blue", mixed);
		CHECK("smudge is undoable", doc.CanUndo());
	}

	printf("block: layers\n"); fflush(stdout);
	{
		PPDocument doc;
		PPPaper p;
		doc.SetCanvas(16, 16, 96.0f, p, false);
		int32 bg = doc.LayerAt(0)->id;
		int32 top = doc.AddLayer("Top");
		CHECK("layer added above", doc.CountLayers() == 2
			&& doc.IndexOf(top) == 1);
		doc.MoveLayer(top, false);
		CHECK("layer moved below", doc.IndexOf(top) == 0);
		doc.MoveLayer(top, true);
		doc.SetActiveLayer(top);
		rgb_color red = { 216, 40, 40, 255 };
		// paint exact pixels: set (8,8) with a 1px mask
		uint8 one[1] = { 255 };
		doc.StrokeBegin();
		doc.DabColour(8, 8, red, one, 1, 1, 255);
		doc.StrokeEnd();
		CHECK("top layer paints", doc.PickColour(8, 8).red == 216);
		doc.SetLayerOpacity(top, 128);
		rgb_color c = doc.PickColour(8, 8);
		CHECK("layer opacity halves alpha",
			c.red == 216 && c.alpha == 128);
		doc.SetLayerVisible(top, false);
		CHECK("hidden layer composites away",
			doc.PickColour(8, 8).alpha == 0);
		doc.SetLayerVisible(top, true);
		doc.DeleteLayer(top);
		CHECK("layer delete", doc.CountLayers() == 1
			&& doc.LayerAt(0)->id == bg);
		CHECK("last layer never deletes",
			(doc.DeleteLayer(bg), doc.CountLayers() == 1));
	}

	printf("block: pdf\n"); fflush(stdout);
	{
		PPDocument doc;
		PPPaper p;
		p.name = "A4";
		p.widthPt = 595.0f;
		p.heightPt = 842.0f;
		doc.SetCanvas(40, 30, 96.0f, p, true);
		rgb_color red = { 216, 40, 40, 255 };
		uint8 one[1] = { 255 };
		doc.StrokeBegin();
		doc.DabColour(10, 10, red, one, 1, 1, 255);
		doc.StrokeEnd();
		CHECK("bad pdf path rejected", PP_WritePDF(doc, "") == B_BAD_VALUE);
		const char* pdfPath = "/tmp/pp-selftest.pdf";
		CHECK("pdf write", PP_WritePDF(doc, pdfPath) == B_OK);
		std::string pdf;
		{
			BFile f;
			if (f.SetTo(pdfPath, B_READ_ONLY) == B_OK) {
				char buf[4096];
				ssize_t n;
				while ((n = f.Read(buf, sizeof(buf))) > 0)
					pdf.append(buf, n);
			}
		}
		CHECK("pdf magic", pdf.compare(0, 8, "%PDF-1.4") == 0);
		CHECK("pdf a4 mediabox",
			pdf.find("/MediaBox [0 0 595 842]") != std::string::npos);
		CHECK("pdf image xobject",
			pdf.find("/Subtype /Image") != std::string::npos
			&& pdf.find("/Filter /FlateDecode") != std::string::npos
			&& pdf.find("/Width 40") != std::string::npos
			&& pdf.find("/Height 30") != std::string::npos);
		CHECK("pdf image painted",
			pdf.find("/Im0 Do") != std::string::npos);
		{
			size_t at = pdf.rfind("startxref\n");
			bool ok = at != std::string::npos;
			if (ok) {
				long long off = atoll(pdf.c_str() + at + 10);
				ok = off > 0 && off < (long long)pdf.size()
					&& pdf[off] == 'x';
			}
			CHECK("pdf xref offset", ok);
		}
		remove(pdfPath);
	}

	printf("block: recent\n"); fflush(stdout);
	{
		PPRecent r;
		r.Remember("/tmp/a.paint");
		r.Remember("/tmp/b.paint");
		r.Remember("/tmp/a.paint");
		CHECK("recent order", r.Items().size() == 2
			&& r.Items()[0] == "/tmp/a.paint");
		for (int i = 0; i < 12; i++) {
			BString p2;
			p2.SetToFormat("/tmp/many%d.paint", i);
			r.Remember(p2);
		}
		CHECK("recent capped", (int32)r.Items().size() == PPRecent::kMax);
		const char* rp = "/tmp/pp-recent-test";
		CHECK("recent save", r.Save(rp) == B_OK);
		PPRecent q;
		CHECK("recent load", q.Load(rp) == B_OK);
		CHECK("recent round trip", q.Items().size() == r.Items().size()
			&& q.Items()[0] == r.Items()[0]);
		remove(rp);
	}

	printf("block: persist\n"); fflush(stdout);
	{		PPDocument doc;
		PPPaper p;
		doc.SetCanvas(40, 30, 96.0f, p, false);
		int32 l2 = doc.AddLayer("Art");
		doc.SetActiveLayer(l2);
		rgb_color red = { 216, 40, 40, 255 };
		uint8 one[1] = { 255 };
		doc.StrokeBegin();
		doc.DabColour(10, 10, red, one, 1, 1, 255);
		doc.StrokeEnd();
		doc.SetLayerOpacity(l2, 200);
		BMessage msg;
		CHECK("save to message", doc.SaveToMessage(&msg) == B_OK);
		PPDocument loaded;
		CHECK("load from message", loaded.LoadFromMessage(&msg) == B_OK);
		CHECK("layers round trip", loaded.CountLayers() == 2
			&& loaded.LayerAt(1)->name == "Art"
			&& loaded.LayerAt(1)->opacity == 200);
		CHECK("pixels round trip",
			loaded.PickColour(10, 10).red == 216);
		const char* path = "/tmp/pp-selftest.paint";
		CHECK("file save", doc.SaveToFile(path) == B_OK);
		PPDocument fromFile;
		CHECK("file load", fromFile.LoadFromFile(path) == B_OK);
		CHECK("file round trip", fromFile.CountLayers() == 2
			&& fromFile.PickColour(10, 10).red == 216);
		CHECK("sniff native", PP_SniffDocument(path) == PP_KIND_NATIVE);
		CHECK("sniff other", PP_SniffDocument("/tmp/pp-none-x")
			== PP_KIND_ERROR);
		remove(path);
	}

	printf("block: opacity\n"); fflush(stdout);
	{
		PPDocument doc;
		PPPaper p;
		doc.SetCanvas(64, 32, 96.0f, p, false);
		PPBrush b;
		b.SetShape(PP_BRUSH_ROUND);
		b.SetSize(3);
		rgb_color red = { 216, 40, 40, 255 };
		// ONE stroke, many overlapping dabs, 50% opacity: the dabs
		// must NOT build up — the stroke lands once at half strength
		doc.StrokeBegin(128);
		for (int32 i = 0; i < 10; i++)
			doc.DabColour(20, 16, red, b.Mask(), b.MaskSize(), b.MaskBpr(),
				255);
		doc.StrokeEnd();
		rgb_color one = doc.PickColour(20, 16);
		CHECK("strokes land once at opacity",
			one.alpha == 128 && one.red == 216);
		// a SECOND stroke over it builds toward full
		doc.StrokeBegin(128);
		doc.DabColour(20, 16, red, b.Mask(), b.MaskSize(), b.MaskBpr(), 255);
		doc.StrokeEnd();
		rgb_color two = doc.PickColour(20, 16);
		CHECK("separate strokes build", two.alpha > 150 && two.alpha < 255);
		// erase at half opacity halves what is there
		doc.StrokeBegin(128);
		doc.DabErase(20, 16, b.Mask(), b.MaskSize(), b.MaskBpr(), 255);
		doc.StrokeEnd();
		CHECK("erase stroke lands at opacity",
			doc.PickColour(20, 16).alpha < two.alpha);
		// airbrush flow: a single low-flow dab is faint, repeats build
		doc.StrokeBegin(255);
		doc.DabColour(40, 16, red, b.Mask(), b.MaskSize(), b.MaskBpr(), 40);
		doc.StrokeEnd();
		uint8 faint = doc.PickColour(40, 16).alpha;
		doc.StrokeBegin(255);
		for (int32 i = 0; i < 6; i++)
			doc.DabColour(40, 16, red, b.Mask(), b.MaskSize(), b.MaskBpr(),
				40);
		doc.StrokeEnd();
		uint8 built = doc.PickColour(40, 16).alpha;
		CHECK("airbrush flow builds", faint < 80 && built > faint
			&& built <= 255);
	}

	printf("block: shapes\n"); fflush(stdout);
	{
		PPDocument doc;
		PPPaper p;
		doc.SetCanvas(96, 64, 96.0f, p, false);
		// emulate the canvas's shape commits at the model level: the
		// same dab pipeline, paths the canvas walks
		PPBrush b;
		b.SetShape(PP_BRUSH_ROUND);
		b.SetSize(3);
		rgb_color red = { 216, 40, 40, 255 };
		auto dab = [&](int32 x, int32 y)
		{
			doc.DabColour(x, y, red, b.Mask(), b.MaskSize(), b.MaskBpr(),
				255);
		};
		// a rectangle outline: edges hit, centre empty
		doc.StrokeBegin(255);
		b.StampLine(10, 10, 50, 10, [](void* c, int32 x, int32 y)
			{ ((decltype(dab)*)c)->operator()(x, y); }, &dab);
		b.StampLine(50, 10, 50, 40, [](void* c, int32 x, int32 y)
			{ ((decltype(dab)*)c)->operator()(x, y); }, &dab);
		b.StampLine(50, 40, 10, 40, [](void* c, int32 x, int32 y)
			{ ((decltype(dab)*)c)->operator()(x, y); }, &dab);
		b.StampLine(10, 40, 10, 10, [](void* c, int32 x, int32 y)
			{ ((decltype(dab)*)c)->operator()(x, y); }, &dab);
		doc.StrokeEnd();
		CHECK("rect edges painted", doc.PickColour(30, 10).alpha == 255
			&& doc.PickColour(10, 25).alpha == 255
			&& doc.PickColour(50, 25).alpha == 255
			&& doc.PickColour(30, 40).alpha == 255);
		CHECK("rect interior empty", doc.PickColour(30, 25).alpha == 0);
	}

	printf("block: resize\n"); fflush(stdout);
	{
		PPDocument doc;
		PPPaper p;
		doc.SetCanvas(32, 32, 96.0f, p, false);
		rgb_color red = { 216, 40, 40, 255 };
		uint8 one[1] = { 255 };
		doc.StrokeBegin();
		doc.DabColour(10, 10, red, one, 1, 1, 255);
		doc.StrokeEnd();
		CHECK("undo exists before resize", doc.CanUndo());
		doc.Resize(64, 48);
		CHECK("resize grows", doc.Width() == 64 && doc.Height() == 48);
		CHECK("content anchored", doc.PickColour(10, 10).red == 216);
		CHECK("new area transparent", doc.PickColour(60, 40).alpha == 0);
		CHECK("resize clears undo", !doc.CanUndo());
		doc.Resize(16, 16);
		CHECK("shrink crops", doc.Width() == 16
			&& doc.PickColour(10, 10).red == 216);
		doc.Resize(0, 0);
		CHECK("bad resize refused", doc.Width() == 16);
	}

	int passed = 0;
	for (const SelfCase& c : sCases) {
		printf("  %-42s %s\n", c.name, c.ok ? "PASS" : "FAIL");
		if (c.ok) passed++;
	}
	printf("SELFTEST %s %d/%d\n", sAll ? "PASS" : "FAIL", passed,
		(int)sCases.size());
	return sAll ? 0 : 1;
}

// -------------------------------------------------------------------- main --
int
main(int argc, char** argv)
{
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--selftest") == 0) {
			PPApp app;
			return SelfTest();
		}
		if (argv[i][0] != '-')
			gOpenPaths.push_back(argv[i]);
	}
	PPApp app;
	app.Run();
	return 0;
}
