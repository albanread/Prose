// PDApp — ProseDraw's application, window, palette, inspector,
// scripting and selftest.
#include <Alert.h>
#include <Application.h>
#include <Box.h>
#include <Button.h>
#include <CheckBox.h>
#include <Clipboard.h>
#include <Entry.h>
#include <Directory.h>
#include <File.h>
#include <FilePanel.h>
#include <FindDirectory.h>
#include <Font.h>
#include <Menu.h>
#include <MenuBar.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Message.h>
#include <MimeType.h>
#include <Messenger.h>
#include <Node.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <PropertyInfo.h>
#include <Screen.h>
#include <ScrollView.h>
#include <StringView.h>
#include <TextControl.h>
#include <Window.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "PDCanvas.h"
#include "PDDocument.h"
#include "PDPDF.h"
#include "PDRecent.h"

// ---------------------------------------------------------------- helpers --
static bool
HasSuffix(const BString& path, const char* suffix)
{
	return path.IEndsWith(suffix);
}

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

static const struct {
	const char* name;
	float width, height;
} kPapers[] = {
	{ "A4",		595.0f, 842.0f },
	{ "US Letter",	612.0f, 792.0f },
	{ "US Legal",	612.0f, 1008.0f },
	{ "A5",		420.0f, 595.0f },
	{ "A3",		842.0f, 1191.0f },
	{ NULL, 0, 0 }
};

static status_t
WriteAttrOn(const char* path, const char* attr, const BString& value)
{
	BNode node(path);
	if (node.InitCheck() != B_OK)
		return node.InitCheck();
	return node.WriteAttrString(attr, &value);
}

// ------------------------------------------------------------ inspector --
class PDStencil;
class PDWindow : public BWindow {
public:
			PDWindow(BRect frame, const char* title);
	bool	QuitRequested() override;
	void	MessageReceived(BMessage* message) override;
	void	MenusBeginning() override;
	// the panels own the whole window: no child follows the frame, the
	// window re-lays everything out on every resize (a B_FOLLOW_ALL
	// scroll view used to stretch over the inspector and the status
	// strip the moment the window changed size)
	void	FrameResized(float width, float height) override
	{
		LayoutChildren();
	}

	PDDocument* Document() { return &fDoc; }
	PDCanvas*	Canvas() { return fCanvas; }
	// UI sync for out-of-window mutations (scripting): title, `*`,
	// status bar all live here
	void	UpdateStatus();
	void	NoteSavedTo(const BString& path);
	void	RememberRecent(const BString& path)
	{
		fRecent.Remember(path);
		RebuildRecentMenu();
		if (fRecentPath.Length() > 0)
			fRecent.Save(fRecentPath.String());
	}

	enum {
		OPEN_PANEL_MSG = 'pdOf', SAVE_PANEL_MSG = 'pdSf',
		PDF_PANEL_MSG = 'pdFf',
		TOOL_MSG = 'pdTl', INSPECTOR_MSG = 'pdIn',
		PAPER_MSG = 'pdPp', ORIENT_MSG = 'pdOr',
		GRID_SHOW_MSG = 'pdGs', GRID_SNAP_MSG = 'pdGn',
		ALIGN_MSG = 'pdAl', ZORDER_MSG = 'pdZo',
		ZOOM_MSG = 'pdZm',
		STATUS_MSG = 'pdUp', FOCUS_LABEL_MSG = 'pdIl',
		FOCUS_CANVAS_MSG = 'pdFc', DROP_HOOK_MSG = 'pdDh'
	};

private:
	void	BuildMenus();
	void	LayoutChildren();
	void	RebuildRecentMenu();
	void	BuildInspector();
	void	RefreshInspector();
	void	ApplyInspector(int32 field);
	void	DoSave(const BString& path);
	void	DoExportPDF(const BString& path);
	status_t	OpenFile(const entry_ref& ref);
	void	SetPaper(int32 index, bool landscape);
	void	RegisterDocumentType();

	PDDocument	fDoc;
	PDCanvas*	fCanvas;
	BMenuBar*	fMenuBar;
	PDStencil*	fPalette;
	BView*		fInspector;
	BStringView*	fStatus;
	BScrollView*	fScroll;
	BTextControl*	fLabel;
	BTextControl*	fX, *fY, *fW, *fH;
	BMenuField*	fFillField;
	BMenuField*	fStrokeField;
	BMenuField*	fWidthField;
	BCheckBox*	fDashed;
	BCheckBox*	fArrowEnd, *fArrowStart, *fElbow;
	BMenuField*	fTextSizeField;
	BMenu*		fZoomMenu = NULL;
	BMenuItem*	fUndoItem, *fRedoItem, *fSaveItem;
	int32		fPaperIndex = 0;
	BString		fFilePath;
	BFilePanel*	fOpenPanel = NULL;
	BFilePanel*	fSavePanel = NULL;
	BFilePanel*	fPDFPanel = NULL;
	PDRecent	fRecent;
	BMenu*		fRecentMenu = NULL;
	BString		fRecentPath;
	bool		fRefreshingInspector = false;

	struct ColourEntry { const char* name; rgb_color c; };
	static const ColourEntry	kColours[];
	static const int32		kColourCount;
};

const PDWindow::ColourEntry PDWindow::kColours[] = {
	{ "White",	{ 255, 255, 255, 255 } },
	{ "Grey",	{ 200, 200, 200, 255 } },
	{ "Black",	{ 0, 0, 0, 255 } },
	{ "Red",	{ 216, 40, 40, 255 } },
	{ "Orange",	{ 240, 160, 40, 255 } },
	{ "Yellow",	{ 250, 240, 120, 255 } },
	{ "Green",	{ 70, 170, 70, 255 } },
	{ "Blue",	{ 70, 110, 220, 255 } },
	{ "Purple",	{ 150, 80, 190, 255 } },
};
const int32 PDWindow::kColourCount = 9;

// -------------------------------------------------------------- stencil --
// The tool palette as a real stencil: click a cell to pick the tool,
// drag a shape cell onto the canvas to create one where it lands.
// Cells 0 (Select) and 6 (Link) are tool-only — they have no payload.
class PDStencil : public BView {
public:
	PDStencil()
		:
		BView(BRect(0, 0, 84, 7 * 30 + 4), "stencil", B_FOLLOW_NONE,
			B_WILL_DRAW)
	{
		SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
		SetDrawingMode(B_OP_COPY);
	}

	void SetSelected(int32 tool)
	{
		fSelected = tool;
		Invalidate();
	}

	void Draw(BRect) override
	{
		SetFont(be_plain_font);
		SetLowColor(ViewColor());
		for (int32 i = 0; i < 7; i++) {
			BRect cell = CellAt(i);
			if (i == fSelected) {
				SetHighColor(tint_color(ViewColor(), B_DARKEN_2_TINT));
				FillRect(cell);
			}
			SetHighColor(0, 0, 0, 255);
			DrawIcon(i, cell);
			font_height fh;
			GetFontHeight(&fh);
			DrawString(kNames[i],
				BPoint(cell.left + 28, cell.top + (cell.Height() + fh.ascent) / 2 - 1));
		}
	}

	void MouseDown(BPoint where) override
	{
		int32 cell = CellAt(where);
		if (cell < 0)
			return;
		if (!Draggable(cell)) {
			Pick(cell);
			return;
		}
		// hold-to-drag: a plain click picks the tool, moving past a few
		// pixels starts a drag that drops a shape on the canvas
		BPoint now = where;
		uint32 buttons = 0;
		GetMouse(&now, &buttons, false);
		while (buttons != 0) {
			if (fabsf(now.x - where.x) > 5 || fabsf(now.y - where.y) > 5) {
				BMessage* drag = new BMessage('pdDg');
				drag->AddInt32("kind", KindOf(cell));
				DragMessage(drag, CellAt(cell), this);
				return;
			}
			snooze(20000);
			GetMouse(&now, &buttons, false);
		}
		Pick(cell);
	}

private:
	static const char* kNames[7];

	BRect CellAt(int32 i) const
	{
		return BRect(2, 2 + i * 30, 82, 2 + i * 30 + 28);
	}
	int32 CellAt(BPoint p) const
	{
		for (int32 i = 0; i < 7; i++)
			if (CellAt(i).Contains(p))
				return i;
		return -1;
	}
	bool Draggable(int32 cell) const { return cell >= 1 && cell <= 5; }
	int32 KindOf(int32 cell) const
	{
		// cell order matches PDTool order
		switch (cell) {
			case 1: return PD_RECT;
			case 2: return PD_RRECT;
			case 3: return PD_ELLIPSE;
			case 4: return PD_DIAMOND;
			default: return PD_TEXT;
		}
	}
	void Pick(int32 cell)
	{
		BMessage m('pdTl');
		m.AddInt32("tool", cell);
		if (Window() != NULL)
			Window()->PostMessage(&m);
	}
	void DrawIcon(int32 i, BRect cell)
	{
		BRect icon(4, cell.top + 5, 24, cell.top + 25);
		switch (i) {
			case 0:	// select: arrow
			{
				BPoint arrow[5] = { icon.LeftTop(), icon.LeftTop()
					+ BPoint(14, 0), icon.LeftTop() + BPoint(14, 5),
					icon.LeftTop() + BPoint(6, 5), icon.LeftTop()
					+ BPoint(6, 16) };
				StrokePolygon(arrow, 5, true);
				break;
			}
			case 1: StrokeRect(icon); break;
			case 2: StrokeRoundRect(icon, 4, 4); break;
			case 3: StrokeEllipse(icon); break;
			case 4:
			{
				BPoint d[4] = { BPoint((icon.left + icon.right) / 2, icon.top),
					BPoint(icon.right, (icon.top + icon.bottom) / 2),
					BPoint((icon.left + icon.right) / 2, icon.bottom),
					BPoint(icon.left, (icon.top + icon.bottom) / 2) };
				StrokePolygon(d, 4, true);
				break;
			}
			case 5:
			{
				BFont font(be_plain_font);
				font.SetSize(15);
				SetFont(&font);
				DrawString("T", BPoint(icon.left + 7, icon.bottom - 3));
				break;
			}
			default:	// link
				StrokeLine(icon.LeftTop() + BPoint(2, 16),
					icon.RightBottom() - BPoint(2, 16));
				break;
		}
	}

	int32	fSelected = 0;
};

const char* PDStencil::kNames[7] = { "Select", "Box", "Round", "Oval",
	"Diamond", "Text", "Link" };

// --------------------------------------------------------------- swatch --
// An inspector colour chip: drag it onto a shape to fill it, onto a
// connector to colour its line.
class PDSwatch : public BView {
public:
	PDSwatch(BRect frame, rgb_color c)
		:
		BView(frame, "swatch", B_FOLLOW_NONE, B_WILL_DRAW),
		fColour(c)
	{
		SetViewColor(c);
	}

	void MouseDown(BPoint) override
	{
		BMessage* drag = new BMessage('pdDc');
		drag->AddInt32("red", fColour.red);
		drag->AddInt32("green", fColour.green);
		drag->AddInt32("blue", fColour.blue);
		DragMessage(drag, Bounds(), this);
	}

private:
	rgb_color	fColour;
};


PDWindow::PDWindow(BRect frame, const char* title)
	:
	BWindow(frame, title, B_TITLED_WINDOW,
		B_QUIT_ON_WINDOW_CLOSE | B_ASYNCHRONOUS_CONTROLS),
	fDoc(),
	fCanvas(new PDCanvas(&fDoc))
{
	RegisterDocumentType();
	// the Open Recent list lives in the user settings directory
	{
		BPath settings;
		if (find_directory(B_USER_SETTINGS_DIRECTORY, &settings) == B_OK) {
			settings.Append("ProseDraw/recent_files");
			fRecentPath = settings.Path();
			BPath parent(settings);
			parent.GetParent(&parent);
			create_directory(parent.Path(), 0755);
			fRecent.Load(fRecentPath.String());
		}
	}
	fMenuBar = new BMenuBar(Bounds(), "menubar");
	BuildMenus();
	AddChild(fMenuBar);

	fScroll = new BScrollView("scroll", fCanvas, B_FOLLOW_NONE, true, true,
		B_FANCY_BORDER);
	AddChild(fScroll);

	fPalette = new PDStencil();
	AddChild(fPalette);

	BuildInspector();
	AddChild(fInspector);

	fStatus = new BStringView(BRect(0, 0, 300, 18), "status", "",
		B_FOLLOW_NONE);
	AddChild(fStatus);

	SetSizeLimits(640, 4000, 480, 4000);
	LayoutChildren();
	fCanvas->DocumentChangedSize();
	fCanvas->MakeFocus();
	UpdateStatus();
}

void
PDWindow::RegisterDocumentType()
{
	// The whole Tracker chain from day one: type, sniffer (HMF1 + our
	// 'pDd&' what-code, little-endian), preferred app — patterns ported
	// from ProseWriter, where their absence cost a sprint.
	BMimeType docType(PDDocument::kDocType);
	if (docType.InitCheck() != B_OK)
		return;
	docType.Install();
	docType.SetShortDescription("ProseDraw diagram");
	docType.SetLongDescription("ProseDraw diagram document");
	status_t err = docType.SetSnifferRule(
		"1.0 ([0:3] \"HMF1\") ([4:7] \"&dDp\")");
	if (err != B_OK)
		fprintf(stderr, "ProseDraw: sniffer rule rejected: %s\n",
			strerror(err));
	docType.SetPreferredApp("application/x-vnd.prose.ProseDraw");
}

void
PDWindow::BuildMenus()
{
	BMenu* menu = new BMenu("File");
	menu->AddItem(new BMenuItem("New",
		new BMessage('pdNw'), 'N', B_COMMAND_KEY));
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem("Open" B_UTF8_ELLIPSIS,
		new BMessage(OPEN_PANEL_MSG), 'O', B_COMMAND_KEY));
	fRecentMenu = new BMenu("Open Recent");
	menu->AddItem(fRecentMenu);
	RebuildRecentMenu();
	menu->AddItem(fSaveItem = new BMenuItem("Save",
		new BMessage('pdSv'), 'S', B_COMMAND_KEY));
	menu->AddItem(new BMenuItem("Save as" B_UTF8_ELLIPSIS,
		new BMessage(SAVE_PANEL_MSG), 'S', B_COMMAND_KEY | B_SHIFT_KEY));
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem("Print to PDF" B_UTF8_ELLIPSIS,
		new BMessage(PDF_PANEL_MSG), 'P', B_COMMAND_KEY));
	menu->AddSeparatorItem();
	BMenuItem* quit = new BMenuItem("Quit", new BMessage(B_QUIT_REQUESTED),
		'Q', B_COMMAND_KEY);
	quit->SetTarget(be_app);
	menu->AddItem(quit);
	menu->ItemAt(0)->SetTarget(be_app);
	fMenuBar->AddItem(menu);

	menu = new BMenu("Edit");
	fUndoItem = new BMenuItem("Undo", new BMessage('pdUd'), 'Z',
		B_COMMAND_KEY);
	fRedoItem = new BMenuItem("Redo", new BMessage('pdRd'), 'Y',
		B_COMMAND_KEY);
	menu->AddItem(fUndoItem);
	menu->AddItem(fRedoItem);
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem("Select all", new BMessage(B_SELECT_ALL),
		'A', B_COMMAND_KEY));
	menu->AddItem(new BMenuItem("Duplicate", new BMessage('pdDp'), 'D',
		B_COMMAND_KEY));
	menu->AddItem(new BMenuItem("Delete", new BMessage('pdDl'),
		B_BACKSPACE, B_COMMAND_KEY));
	fMenuBar->AddItem(menu);

	menu = new BMenu("Shape");
	{
		BMessage* front = new BMessage(ZORDER_MSG);
		front->AddInt32("front", 1);
		menu->AddItem(new BMenuItem("Bring to front", front));
		BMessage* back = new BMessage(ZORDER_MSG);
		back->AddInt32("front", 0);
		menu->AddItem(new BMenuItem("Send to back", back));
	}
	fMenuBar->AddItem(menu);

	menu = new BMenu("Align");
	const char* aligns[] = { "Left", "Centre", "Right", "Top", "Middle",
		"Bottom", "Distribute horizontally", "Distribute vertically" };
	for (int32 i = 0; i < 8; i++) {
		BMessage* m = new BMessage(ALIGN_MSG);
		m->AddInt32("align", i);
		menu->AddItem(new BMenuItem(aligns[i], m));
	}
	fMenuBar->AddItem(menu);

	menu = new BMenu("View");
	BMenu* paper = new BMenu("Paper");
	for (int32 i = 0; kPapers[i].name != NULL; i++) {
		BMessage* m = new BMessage(PAPER_MSG);
		m->AddInt32("index", i);
		paper->AddItem(new BMenuItem(kPapers[i].name, m));
	}
	paper->SetRadioMode(true);
	paper->ItemAt(0)->SetMarked(true);
	menu->AddItem(paper);
	BMenu* orient = new BMenu("Orientation");
	BMessage* po = new BMessage(ORIENT_MSG);
	po->AddInt32("landscape", 0);
	BMenuItem* portrait = new BMenuItem("Portrait", po);
	BMessage* lo = new BMessage(ORIENT_MSG);
	lo->AddInt32("landscape", 1);
	BMenuItem* landscape = new BMenuItem("Landscape", lo);
	orient->AddItem(portrait);
	orient->AddItem(landscape);
	orient->SetRadioMode(true);
	portrait->SetMarked(true);
	menu->AddItem(orient);
	menu->AddSeparatorItem();
	BMenuItem* grid = new BMenuItem("Show grid",
		new BMessage(GRID_SHOW_MSG));
	grid->SetMarked(true);
	menu->AddItem(grid);
	BMenuItem* snap = new BMenuItem("Snap to grid",
		new BMessage(GRID_SNAP_MSG));
	snap->SetMarked(true);
	menu->AddItem(snap);
	menu->AddSeparatorItem();
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
PDWindow::BuildInspector()
{
	fInspector = new BView(BRect(0, 0, 208, 300), "inspector",
		B_FOLLOW_NONE, B_WILL_DRAW);
	fInspector->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

	fLabel = new BTextControl(BRect(8, 8, 200, 28), "label", "Label:", "",
		new BMessage(INSPECTOR_MSG));
	fLabel->SetDivider(42);
	fLabel->Message()->AddInt32("field", 0);
	fInspector->AddChild(fLabel);

	const char* names[4] = { "X:", "Y:", "W:", "H:" };
	BTextControl** fields[4] = { &fX, &fY, &fW, &fH };
	for (int32 i = 0; i < 4; i++) {
		BMessage* m = new BMessage(INSPECTOR_MSG);
		m->AddInt32("field", 1 + i);
		*fields[i] = new BTextControl(BRect(8, 36 + i * 26, 200,
			56 + i * 26), names[i], names[i], "0", m);
		(*fields[i])->SetDivider(42);
		fInspector->AddChild(*fields[i]);
	}

	BPopUpMenu* fillMenu = new BPopUpMenu("fill");
	for (int32 i = 0; i < kColourCount + 1; i++) {
		BMessage* m = new BMessage(INSPECTOR_MSG);
		m->AddInt32("field", 5);
		m->AddInt32("index", i);
		const char* n = i == 0 ? "None"
			: kColours[i - 1].name;
		fillMenu->AddItem(new BMenuItem(n, m));
	}
	fillMenu->ItemAt(1)->SetMarked(true);
	fFillField = new BMenuField(BRect(8, 144, 200, 168), "fill", "Fill:",
		fillMenu);
	fFillField->SetDivider(42);
	fInspector->AddChild(fFillField);

	BPopUpMenu* strokeMenu = new BPopUpMenu("stroke");
	for (int32 i = 0; i < kColourCount; i++) {
		BMessage* m = new BMessage(INSPECTOR_MSG);
		m->AddInt32("field", 6);
		m->AddInt32("index", i);
		strokeMenu->AddItem(new BMenuItem(kColours[i].name, m));
	}
	strokeMenu->ItemAt(2)->SetMarked(true);
	fStrokeField = new BMenuField(BRect(8, 172, 200, 196), "stroke",
		"Stroke:", strokeMenu);
	fStrokeField->SetDivider(42);
	fInspector->AddChild(fStrokeField);

	BPopUpMenu* widthMenu = new BPopUpMenu("width");
	const float widths[] = { 1, 2, 3, 4 };
	for (int32 i = 0; i < 4; i++) {
		BMessage* m = new BMessage(INSPECTOR_MSG);
		m->AddInt32("field", 7);
		m->AddInt32("index", i);
		char label[8];
		snprintf(label, sizeof(label), "%d pt", (int)widths[i]);
		widthMenu->AddItem(new BMenuItem(label, m));
	}
	widthMenu->ItemAt(0)->SetMarked(true);
	fWidthField = new BMenuField(BRect(8, 200, 200, 224), "width",
		"Line:", widthMenu);
	fWidthField->SetDivider(42);
	fInspector->AddChild(fWidthField);

	BMessage* dashMsg = new BMessage(INSPECTOR_MSG);
	dashMsg->AddInt32("field", 8);
	fDashed = new BCheckBox(BRect(8, 230, 200, 250), "dashed", "Dashed",
		dashMsg);
	fInspector->AddChild(fDashed);

	BPopUpMenu* sizeMenu = new BPopUpMenu("textsize");
	const float sizes[] = { 9, 10, 12, 14, 18, 24 };
	for (int32 i = 0; i < 6; i++) {
		BMessage* m = new BMessage(INSPECTOR_MSG);
		m->AddInt32("field", 9);
		m->AddInt32("index", i);
		char label[12];
		snprintf(label, sizeof(label), "%d pt", (int)sizes[i]);
		sizeMenu->AddItem(new BMenuItem(label, m));
	}
	sizeMenu->ItemAt(2)->SetMarked(true);
	fTextSizeField = new BMenuField(BRect(8, 254, 200, 278), "textsize",
		"Text:", sizeMenu);
	fTextSizeField->SetDivider(42);
	fInspector->AddChild(fTextSizeField);

	// connector-only controls; RefreshInspector enables them when the
	// selection actually holds a connector
	const char* checks[3] = { "Arrow at end", "Arrow at start",
		"Elbow route" };
	BCheckBox** boxes[3] = { &fArrowEnd, &fArrowStart, &fElbow };
	for (int32 i = 0; i < 3; i++) {
		BMessage* m = new BMessage(INSPECTOR_MSG);
		m->AddInt32("field", 10 + i);
		*boxes[i] = new BCheckBox(BRect(8, 286 + i * 22, 200, 306 + i * 22),
			"conn", checks[i], m);
		fInspector->AddChild(*boxes[i]);
	}
	// colour chips: drag one onto a shape (fill) or connector (stroke)
	for (int32 i = 0; i < kColourCount; i++) {
		fInspector->AddChild(new PDSwatch(
			BRect(8 + i * 22, 362, 8 + i * 22 + 18, 380),
			kColours[i].c));
	}
	fInspector->ResizeTo(208, 392);
}

void
PDWindow::LayoutChildren()
{
	// the client area lives strictly between stencil and inspector,
	// above the status strip — never under either (scrollbars own
	// their own strips inside the scroll view; the view is clipped)
	float menuH = fMenuBar->Bounds().Height() + 1;
	fMenuBar->ResizeTo(Bounds().Width(), menuH - 1);
	fPalette->MoveTo(0, menuH);
	float right = Bounds().right;
	fInspector->MoveTo(right - 208, menuH);
	fScroll->MoveTo(84, menuH);
	fScroll->ResizeTo(right - 208 - 84, Bounds().bottom - 18 - menuH + 1);
	fStatus->MoveTo(8, Bounds().bottom - 17);
	fStatus->ResizeTo(Bounds().Width() - 16, 16);
	// repositioned views do not all invalidate themselves (the canvas
	// stayed black after a scripted resize once); repaint explicitly
	if (fCanvas != NULL)
		fCanvas->Invalidate();
	if (fInspector != NULL)
		fInspector->Invalidate();
	if (fPalette != NULL)
		fPalette->Invalidate();
}

void
PDWindow::MenusBeginning()
{
	fUndoItem->SetEnabled(fDoc.CanUndo());
	fRedoItem->SetEnabled(fDoc.CanRedo());
	fSaveItem->SetEnabled(fDoc.IsModified());
}

// Every path the document takes on — open, save, save-as, scripted
// save — funnels through here: one place sets the title, the recent
// list and the status line.
void
PDWindow::NoteSavedTo(const BString& path)
{
	fFilePath = path;
	RememberRecent(path);
	UpdateStatus();
}

void
PDWindow::RebuildRecentMenu()
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
		BMessage* m = new BMessage('pdRc');
		m->AddString("path", path);
		fRecentMenu->AddItem(
			new BMenuItem(BPath(path.String()).Leaf(), m));
	}
}

void
PDWindow::UpdateStatus()
{
	const PDPageSetup& p = fDoc.Page();
	BString s;
	const char* paperName = "Custom";
	for (int32 i = 0; kPapers[i].name != NULL; i++)
		if (kPapers[i].width == p.width && kPapers[i].height == p.height)
			paperName = kPapers[i].name;
	bool landscape = p.width > p.height;
	s.SetToFormat("%d shapes, %d selected   %s %s (%.0fx%.0f)   "
		"grid %.0f pt%s   zoom %.0f%%", (int)fDoc.Count(),
		(int)fCanvas->Selection().size(), paperName,
		landscape ? "landscape" : "portrait", p.width, p.height,
		fDoc.Grid(), fDoc.SnapEnabled() ? "" : ", snap off",
		fCanvas->Zoom() * 100);
	fStatus->SetText(s.String());
	BString name("Untitled");
	if (fFilePath.Length()) {
		BPath leaf(fFilePath.String());
		if (leaf.InitCheck() == B_OK && leaf.Leaf() != NULL
			&& leaf.Leaf()[0] != '\0')
			name = leaf.Leaf();
	}
	SetTitle(BString(fDoc.IsModified() ? "* " : "").Append(name).String());
}

void
PDWindow::RefreshInspector()
{
	if (fRefreshingInspector)
		return;
	fRefreshingInspector = true;
	const std::vector<int32>& sel = fCanvas->Selection();
	BRect b = fCanvas->SelectionBounds();
	char buf[16];
	snprintf(buf, sizeof(buf), "%.0f", b.left);
	fX->SetText(sel.empty() ? "" : buf);
	snprintf(buf, sizeof(buf), "%.0f", b.top);
	fY->SetText(sel.empty() ? "" : buf);
	snprintf(buf, sizeof(buf), "%.0f", b.Width());
	fW->SetText(sel.empty() ? "" : buf);
	snprintf(buf, sizeof(buf), "%.0f", b.Height());
	fH->SetText(sel.empty() ? "" : buf);
	fLabel->SetText("");
	if (sel.size() == 1) {
		PDShape* s = fDoc.ShapeById(sel[0]);
		if (s != NULL)
			fLabel->SetText(s->label.String());
	}
	// connector flags: read from the first selected connector
	const PDShape* conn = NULL;
	for (int32 id : sel) {
		const PDShape* s = fDoc.ShapeById(id);
		if (s != NULL && s->kind == PD_CONNECTOR) {
			conn = s;
			break;
		}
	}
	fArrowEnd->SetEnabled(conn != NULL);
	fArrowStart->SetEnabled(conn != NULL);
	fElbow->SetEnabled(conn != NULL);
	fArrowEnd->SetValue(conn != NULL && conn->arrowEnd
		? B_CONTROL_ON : B_CONTROL_OFF);
	fArrowStart->SetValue(conn != NULL && conn->arrowStart
		? B_CONTROL_ON : B_CONTROL_OFF);
	fElbow->SetValue(conn != NULL && conn->orthogonal
		? B_CONTROL_ON : B_CONTROL_OFF);
	fRefreshingInspector = false;
}

void
PDWindow::ApplyInspector(int32 field)
{
	const std::vector<int32>& sel = fCanvas->Selection();
	if (sel.empty())
		return;
	if (field == 0) {
		for (int32 id : sel)
			fDoc.SetShapeLabel(id, fLabel->Text());
	} else if (field >= 1 && field <= 4) {
		BRect b = fCanvas->SelectionBounds();
		float v = atof(field == 1 ? fX->Text() : field == 2 ? fY->Text()
			: field == 3 ? fW->Text() : fH->Text());
		if (field == 1) b.OffsetBy(v - b.left, 0);
		if (field == 2) b.OffsetBy(0, v - b.top);
		if (field == 3) b.right = b.left + v;
		if (field == 4) b.bottom = b.top + v;
		fCanvas->SetSelectionRect(b);
	} else if (field >= 10 && field <= 12) {
		// connector flags apply to every selected connector
		bool end = fArrowEnd->Value() == B_CONTROL_ON;
		bool start = fArrowStart->Value() == B_CONTROL_ON;
		bool elbow = fElbow->Value() == B_CONTROL_ON;
		for (int32 id : sel)
			fDoc.SetShapeFlags(id, end, start, elbow);
	} else if (field >= 5) {
		// style fields apply the shared style of the first selection
		PDShape* s = fDoc.ShapeById(sel[0]);
		if (s == NULL)
			return;
		PDStyle st = s->style;
		switch (field) {
			case 5:
			{
				int32 index = 0;
				fFillField->Menu()->FindMarked()->Message()
					->FindInt32("index", &index);
				st.fillOn = index != 0;
				if (index > 0)
					st.fill = kColours[index - 1].c;
				break;
			}
			case 6:
			{
				int32 index = 2;
				fStrokeField->Menu()->FindMarked()->Message()
					->FindInt32("index", &index);
				st.stroke = kColours[index].c;
				break;
			}
			case 7:
			{
				int32 index = 0;
				fWidthField->Menu()->FindMarked()->Message()
					->FindInt32("index", &index);
				st.strokeWidth = (float)(index + 1);
				break;
			}
			case 8:
				st.dashed = fDashed->Value() == B_CONTROL_ON;
				break;
			case 9:
			{
				int32 index = 2;
				const float sizes[] = { 9, 10, 12, 14, 18, 24 };
				fTextSizeField->Menu()->FindMarked()->Message()
					->FindInt32("index", &index);
				st.textSize = sizes[index];
				break;
			}
		}
		for (int32 id : sel)
			fDoc.SetShapeStyle(id, st);
	}
	fCanvas->Invalidate();
	UpdateStatus();
}

void
PDWindow::SetPaper(int32 index, bool landscape)
{
	if (index < 0 || kPapers[index].name == NULL)
		return;
	fPaperIndex = index;
	float w = kPapers[index].width, h = kPapers[index].height;
	if (landscape && w < h) { float t = w; w = h; h = t; }
	if (!landscape && w > h) { float t = w; w = h; h = t; }
	PDPageSetup p;
	p.width = w;
	p.height = h;
	fDoc.SetPage(p);
	fCanvas->DocumentChangedSize();
	UpdateStatus();
}

void
PDWindow::DoSave(const BString& pathStr)
{
	status_t err = fDoc.SaveToFile(pathStr.String());
	if (err == B_OK) {
		fDoc.SavedClean();
		// typed saves: Tracker can find us without anyone running mimeset
		WriteAttrOn(pathStr.String(), "BEOS:TYPE", PDDocument::kDocType);
		NoteSavedTo(pathStr);
		return;
	}
	BString msg;
	msg.SetToFormat("Could not save %s: %s", pathStr.String(),
		strerror(err));
	(new BAlert("ProseDraw", msg.String(), "OK"))->Go(NULL);
	UpdateStatus();
}

void
PDWindow::DoExportPDF(const BString& pathStr)
{
	// exports never touch document state — no undo, no modified flag
	status_t err = PD_WritePDF(fDoc, pathStr.String());
	if (err == B_OK) {
		WriteAttrOn(pathStr.String(), "BEOS:TYPE", "application/pdf");
		UpdateStatus();
	} else {
		BString msg;
		msg.SetToFormat("Could not write %s: %s", pathStr.String(),
			strerror(err));
		(new BAlert("ProseDraw", msg.String(), "OK"))->Go(NULL);
	}
}

status_t
PDWindow::OpenFile(const entry_ref& ref)
{
	if (fDoc.IsModified()) {
		BAlert* alert = new BAlert("ProseDraw",
			"The current diagram has unsaved changes. Open anyway?",
			"Cancel", "Discard changes", "Open and save first",
			B_WIDTH_AS_USUAL, B_OFFSET_SPACING, B_WARNING_ALERT);
		int32 choice = alert->Go();
		if (choice == 0)
			return B_CANCELED;
		if (choice == 2) {
			if (fFilePath.Length()) {
				DoSave(fFilePath);
				if (fDoc.IsModified())
					return B_CANCELED;
			} else
				return B_CANCELED;
		}
	}
	BPath path(&ref);
	status_t err = B_ERROR;
	PDDocKind kind = PD_SniffDocument(path.Path());
	if (kind == PD_KIND_NATIVE) {
		err = fDoc.LoadFromFile(path.Path());
	} else {
		// not ours and not anything we read — say so
		err = B_BAD_TYPE;
	}
	if (err != B_OK) {
		BString msg;
		msg.SetToFormat("ProseDraw could not open %s (kind %d): %s",
			path.Path(), (int)kind, strerror(err));
		(new BAlert("ProseDraw", msg.String(), "OK"))->Go(NULL);
		return err;
	}
	fCanvas->Select(std::vector<int32>());
	fCanvas->DocumentChangedSize();
	NoteSavedTo(path.Path());
	return B_OK;
}

bool
PDWindow::QuitRequested()
{
	if (fDoc.IsModified()) {
		BAlert* alert = new BAlert("ProseDraw",
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
			} else {
				if (fSavePanel == NULL)
					fSavePanel = new BFilePanel(B_SAVE_PANEL,
						new BMessenger(this), NULL, B_FILE_NODE, false,
						new BMessage('pdSv'));
				fSavePanel->Show();
				return false;
			}
		}
	}
	BMessage closed('pdWc');
	closed.AddPointer("win", this);
	be_app_messenger.SendMessage(&closed);
	return true;
}

void
PDWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case TOOL_MSG:
		{
			int32 tool = 0;
			message->FindInt32("tool", &tool);
			fCanvas->SetTool((PDTool)tool);
			fPalette->SetSelected(tool);
			UpdateStatus();
			break;
		}
		case STATUS_MSG:
			RefreshInspector();
			UpdateStatus();
			break;
		case INSPECTOR_MSG:
		{
			int32 field = 0;
			message->FindInt32("field", &field);
			ApplyInspector(field);
			break;
		}
		case 'pdUd':
			fDoc.Undo();
			fCanvas->Invalidate();
			UpdateStatus();
			break;
		case 'pdRd':
			fDoc.Redo();
			fCanvas->Invalidate();
			UpdateStatus();
			break;
		case B_SELECT_ALL:
			fCanvas->SelectAll();
			break;
		case FOCUS_CANVAS_MSG:
			fCanvas->MakeFocus();
			break;
		case DROP_HOOK_MSG:
		{
			// harness drop: doc point -> the same core a real drop runs
			float x = 0, y = 0;
			message->FindFloat("x", &x);
			message->FindFloat("y", &y);
			BPoint viewPoint = fCanvas->DocToView(BPoint(x, y));
			int32 kind = -1;
			if (message->FindInt32("kind", &kind) == B_OK)
				fCanvas->DropCreateAt((PDShapeKind)kind, viewPoint);
			else {
				int32 r = 0, g2 = 0, b = 0;
				message->FindInt32("red", &r);
				message->FindInt32("green", &g2);
				message->FindInt32("blue", &b);
				rgb_color c = { (uint8)r, (uint8)g2, (uint8)b, 255 };
				fCanvas->DropColourAt(c, viewPoint);
			}
			break;
		}
		case 'pdDp':
			fCanvas->DuplicateSelection();
			break;
		case 'pdDl':
			fCanvas->DeleteSelection();
			break;
		case ALIGN_MSG:
		{
			int32 align = 0;
			message->FindInt32("align", &align);
			fDoc.Align(fCanvas->Selection(), (PDAlign)align);
			fCanvas->Invalidate();
			UpdateStatus();
			break;
		}
		case ZORDER_MSG:
		{
			int32 front = 1;
			message->FindInt32("front", &front);
			for (int32 id : fCanvas->Selection())
				fDoc.MoveZ(id, front != 0);
			fCanvas->Invalidate();
			break;
		}
		case PAPER_MSG:
		{
			int32 index = 0;
			message->FindInt32("index", &index);
			SetPaper(index, fDoc.Page().width > fDoc.Page().height);
			break;
		}
		case ORIENT_MSG:
		{
			int32 landscape = 0;
			message->FindInt32("landscape", &landscape);
			SetPaper(fPaperIndex, landscape != 0);
			break;
		}
		case GRID_SHOW_MSG:
		{
			BMenuItem* item = fMenuBar->FindItem("Show grid");
			if (item != NULL) {
				fDoc.SetShowGrid(!fDoc.ShowGrid());
				item->SetMarked(fDoc.ShowGrid());
				fCanvas->Invalidate();
			}
			break;
		}
		case GRID_SNAP_MSG:
		{
			BMenuItem* item = fMenuBar->FindItem("Snap to grid");
			if (item != NULL) {
				fDoc.SetSnapEnabled(!fDoc.SnapEnabled());
				item->SetMarked(fDoc.SnapEnabled());
			}
			break;
		}
		case ZOOM_MSG:
		{
			float z = 1.0f;
			message->FindFloat("zoom", &z);
			fCanvas->SetZoom(z);
			// radio marks follow; a scripted zoom between presets
			// legitimately marks nothing
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
		case OPEN_PANEL_MSG:
			if (fOpenPanel == NULL)
				fOpenPanel = new BFilePanel(B_OPEN_PANEL,
					new BMessenger(this), NULL, B_FILE_NODE, false,
					new BMessage('pdOo'));
			fOpenPanel->Show();
			break;
		case 'pdOo':
		{
			entry_ref ref;
			if (message->FindRef("refs", &ref) == B_OK)
				OpenFile(ref);
			break;
		}
		case 'pdRc':
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
		case B_REFS_RECEIVED:
		{
			// Tracker double-click, launch refs (replayed after
			// ReadyToRun), or a file dropped on the canvas — all the
			// same open
			entry_ref ref;
			if (message->FindRef("refs", &ref) == B_OK)
				OpenFile(ref);
			break;
		}
		case 'pdSv':
		{
			// save panel selection, or a scripted save
			entry_ref ref;
			if (message->FindRef("directory", &ref) == B_OK) {
				BPath path(&ref);
				BString name;
				message->FindString("name", &name);
				path.Append(WithExtension(name, ".draw").String());
				DoSave(BString(path.Path()));
			} else if (fFilePath.Length()) {
				DoSave(fFilePath);
			} else {
				PostMessage(SAVE_PANEL_MSG);
			}
			break;
		}
		case SAVE_PANEL_MSG:
			if (fSavePanel == NULL)
				fSavePanel = new BFilePanel(B_SAVE_PANEL,
					new BMessenger(this), NULL, B_FILE_NODE, false,
					new BMessage('pdSv'));
			{
				BString suggested = fFilePath.Length()
					? BString(BPath(fFilePath.String()).Leaf())
					: BString("Untitled");
				fSavePanel->SetSaveText(
					WithExtension(suggested, ".draw").String());
			}
			fSavePanel->Show();
			break;
		case PDF_PANEL_MSG:
			if (fPDFPanel == NULL)
				fPDFPanel = new BFilePanel(B_SAVE_PANEL,
					new BMessenger(this), NULL, B_FILE_NODE, false,
					new BMessage('pdPf'));
			{
				BString suggested = fFilePath.Length()
					? BString(BPath(fFilePath.String()).Leaf())
					: BString("Untitled");
				fPDFPanel->SetSaveText(
					WithExtension(suggested, ".pdf").String());
			}
			fPDFPanel->Show();
			break;
		case 'pdPf':
		{
			// panel selection: export the diagram as vector PDF
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
		default:
			BWindow::MessageReceived(message);
	}
}

// ------------------------------------------------------------------ app --
// Scripting: the ProseWriter set from day one — Activate first (nothing
// keyboardable works on this guest without it), plus the diagram verbs
// the harness needs to build and check documents headlessly.
static property_info sPDProperties[] = {
	{ "ShapeCount",
		{ B_GET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"number of shapes in the diagram", 0, { B_INT32_TYPE } },
	{ "Zoom",
		{ B_GET_PROPERTY, B_SET_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"canvas zoom percent (set: 50..400)", 0,
		{ B_INT32_TYPE, B_INT32_TYPE } },
	{ "Activate",
		{ B_EXECUTE_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"bring the window forward and focus the canvas (harness)", 0, { 0 } },
	{ "AddShape",
		{ B_EXECUTE_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"add a shape: \"rect|rrect|ellipse|diamond|text x y w h|label\", "
		"or \"connect\" to join the last two", 0, { B_STRING_TYPE } },
	{ "Save",
		{ B_EXECUTE_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"save the diagram (data: path)", 0, { B_STRING_TYPE } },
	{ "PDF",
		{ B_EXECUTE_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"export the diagram as vector PDF (data: path)", 0,
		{ B_STRING_TYPE } },
	{ "Drop",
		{ B_EXECUTE_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"run the drop core at a doc point: \"kind x y\" or "
		"\"colour r g b x y\" (harness)", 0, { B_STRING_TYPE } },
	{ "Open",
		{ B_EXECUTE_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"open a diagram (data: path)", 0, { B_STRING_TYPE } },
	{ "Quit",
		{ B_EXECUTE_PROPERTY, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"close every window and quit", 0, { 0 } },
	{ 0 }
};

const BPropertyInfo kPDScriptingProperties(sPDProperties);

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

// Answered with the window lock held (gets) or entirely by forwarding
// (Open) — the ProseWriter threading rules.
static bool
HandleScriptingForWindow(PDWindow* window, BMessage* message,
	const char* property)
{
	if (property == NULL || !property[0])
		return false;
	PDDocument& doc = *window->Document();
	BString prop = property;
	bool isGet = message->what == B_GET_PROPERTY;
	bool isSet = message->what == B_SET_PROPERTY;
	bool isExec = message->what == B_EXECUTE_PROPERTY;

	if (prop == "ShapeCount" && isGet) {
		ReplyInt(message, doc.Count());
		return true;
	}
	if (prop == "Zoom" && isGet) {
		ReplyInt(message, (int32)roundf(window->Canvas()->Zoom() * 100));
		return true;
	}
	if (prop == "Zoom" && isSet) {
		// one value, two conventions: a factor (2, 0.5) or a percent
		// (200). hey sends ints, pwquery strings — take whichever
		// field arrives and disambiguate by range.
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
		BMessage zoom(PDWindow::ZOOM_MSG);
		zoom.AddFloat("zoom", z);
		window->PostMessage(&zoom);
		z = fminf(4.0f, fmaxf(0.25f, z));
		ReplyInt(message, (int32)roundf(z * 100));
		return true;
	}
	if (prop == "Activate" && isExec) {
		// scripting runs on the app looper; window calls need the lock
		// (paid for here: a lock-less Activate() from the app thread is
		// silently ignored and the window never takes keyboard focus)
		if (window->Lock()) {
			window->Activate();
			window->Canvas()->MakeFocus();
			window->Unlock();
		}
		// activation restores the previously focused view — an
		// inspector text field, typically — so re-focus the canvas
		// once more AFTER the activation has settled
		window->PostMessage(PDWindow::FOCUS_CANVAS_MSG);
		ReplyString(message, "");
		return true;
	}
	if (prop == "AddShape" && isExec) {
		BString data;
		if (message->FindString("data", &data) != B_OK || !data.Length()) {
			ReplyError(message, "data: shape spec required");
			return true;
		}
		if (data.IStartsWith("connect")) {
			window->Canvas()->QueueConnector(data.IFindFirst("elbow") >= 0);
			ReplyString(message, "");
			return true;
		}
		PDShapeKind kind = PD_RECT;
		if (data.IStartsWith("rrect")) kind = PD_RRECT;
		else if (data.IStartsWith("ellipse")) kind = PD_ELLIPSE;
		else if (data.IStartsWith("diamond")) kind = PD_DIAMOND;
		else if (data.IStartsWith("text")) kind = PD_TEXT;
		else if (data.IStartsWith("image")) kind = PD_IMAGE;
		else if (!data.IStartsWith("rect")) {
			ReplyError(message, "kind must be rect/rrect/ellipse/diamond/text/image");
			return true;
		}
		float x = 0, y = 0, w = 96, h = 64;
		int32 bar = data.FindFirst('|');
		BString spec = bar >= 0 ? BString(data, bar) : data;
		sscanf(spec.String() + (kind == PD_RECT ? 4 : kind == PD_TEXT ? 4
			: kind == PD_RRECT || kind == PD_IMAGE ? 5
			: kind == PD_ELLIPSE ? 7 : 7),
			" %f %f %f %f", &x, &y, &w, &h);
		BString label;
		if (bar >= 0)
			data.CopyInto(label, bar + 1, data.Length() - bar - 1);
		// images: the text after '|' is the file to load and embed
		int32 imageId = -1;
		if (kind == PD_IMAGE) {
			if (!label.Length()) {
				ReplyError(message, "data: image needs |path");
				return true;
			}
			status_t ierr = B_OK;
			imageId = doc.AddImageFile(label.String(), &ierr);
			if (imageId < 0) {
				ReplyError(message, strerror(ierr));
				return true;
			}
		}
		PDShape* s = doc.AddShape(kind, BRect(x, y, x + w, y + h),
			kind != PD_IMAGE && label.Length() ? label.String() : NULL);
		if (kind == PD_IMAGE)
			s->imageId = imageId;
		window->Canvas()->Invalidate();
		window->UpdateStatus();
		ReplyInt(message, s->id);
		return true;
	}
	if (prop == "Save" && isExec) {
		BString path;
		if (message->FindString("data", &path) == B_OK && path.Length()) {
			// quiet: no modal alerts off the app looper (PW lesson)
			status_t err = doc.SaveToFile(path.String());
			if (err == B_OK) {
				doc.SavedClean();
				WriteAttrOn(path.String(), "BEOS:TYPE",
					PDDocument::kDocType);
				window->NoteSavedTo(path);
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
		if (message->FindString("data", &path) == B_OK && path.Length()) {
			// quiet, and pure output: no document state changes
			status_t err = PD_WritePDF(doc, path.String());
			if (err == B_OK)
				WriteAttrOn(path.String(), "BEOS:TYPE", "application/pdf");
			if (err == B_OK)
				ReplyString(message, "");
			else
				ReplyError(message, strerror(err));
		} else
			ReplyError(message, "data: path required");
		return true;
	}
	if (prop == "Drop" && isExec) {
		// harness: drive the drop core at a doc point — the identical
		// code a real stencil/swatch drop runs, minus the app_server
		// drag itself (this guest's mouse cannot deliver drags; the
		// physical gesture stays human-owed)
		BString data;
		if (message->FindString("data", &data) == B_OK && data.Length()) {
			float x = 0, y = 0;
			if (sscanf(data.String(), "colour %*d %*d %*d %f %f", &x, &y)
					== 2
				|| sscanf(data.String(), "color %*d %*d %*d %f %f",
					&x, &y) == 2) {
				int32 r = 0, g2 = 0, b = 0;
				sscanf(data.String(), "%*s %d %d %d", &r, &g2, &b);
				BMessage drop(PDWindow::DROP_HOOK_MSG);
				drop.AddInt32("red", r);
				drop.AddInt32("green", g2);
				drop.AddInt32("blue", b);
				drop.AddFloat("x", x);
				drop.AddFloat("y", y);
				window->PostMessage(&drop);
				ReplyString(message, "");
				return true;
			}
			PDShapeKind kind = PD_RECT;
			if (data.IStartsWith("rrect")) kind = PD_RRECT;
			else if (data.IStartsWith("ellipse")) kind = PD_ELLIPSE;
			else if (data.IStartsWith("diamond")) kind = PD_DIAMOND;
			else if (data.IStartsWith("text")) kind = PD_TEXT;
			else if (!data.IStartsWith("rect")) {
				ReplyError(message,
					"kind must be rect/rrect/ellipse/diamond/text/colour");
				return true;
			}
			char head[16] = { 0 };
			sscanf(data.String(), "%15s %f %f", head, &x, &y);
			BMessage drop(PDWindow::DROP_HOOK_MSG);
			drop.AddInt32("kind", (int32)kind);
			drop.AddFloat("x", x);
			drop.AddFloat("y", y);
			window->PostMessage(&drop);
			ReplyString(message, "");
		} else
			ReplyError(message, "data: drop spec required");
		return true;
	}
	if (prop == "Open" && isExec) {
		BString path;
		if (message->FindString("data", &path) == B_OK && path.Length()) {
			entry_ref ref;
			if (get_ref_for_path(path.String(), &ref) == B_OK) {
				// forward as the open panel's exact message: one code
				// path, alerts stay on the window thread
				BMessage open('pdOo');
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

// documents given on the command line (the roster may also deliver them
// as B_REFS_RECEIVED before ReadyToRun — too early for a window)
static std::vector<BString> gOpenPaths;

class PDApp : public BApplication {
public:
			PDApp()
				:
				BApplication("application/x-vnd.prose.ProseDraw")
			{
			}

	status_t GetSupportedSuites(BMessage* message) override
	{
		message->AddString("suites", "suite/x-vnd.prose.ProseDraw");
		message->AddFlat("messages", &kPDScriptingProperties);
		return BApplication::GetSupportedSuites(message);
	}

	BHandler* ResolveSpecifier(BMessage* message, int32 index,
		BMessage* specifier, int32 what, const char* property) override
	{
		if (kPDScriptingProperties.FindMatch(message, index, specifier,
				what, property) >= 0)
			return this;
		return BApplication::ResolveSpecifier(message, index, specifier,
			what, property);
	}

	void	ReadyToRun() override
	{
		BScreen screen(B_MAIN_SCREEN_ID);
		BRect avail = screen.Frame().InsetByCopy(40, 36);
		PDWindow* window = new PDWindow(
			BRect(avail.left, avail.top, avail.left + 860,
				avail.top + 680), "Untitled");
		window->Show();
		for (const BString& path : gOpenPaths) {
			entry_ref ref;
			if (get_ref_for_path(path.String(), &ref) == B_OK) {
				BMessage open('pdOo');
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
		bool any = false;
		for (int32 i = CountWindows() - 1; i >= 0; i--) {
			PDWindow* w = dynamic_cast<PDWindow*>(WindowAt(i));
			if (w != NULL) {
				w->PostMessage(B_QUIT_REQUESTED);
				any = true;
			}
		}
		return !any;
	}

	void	MessageReceived(BMessage* message) override
	{
		if (message->what == B_REFS_RECEIVED) {
			if (DocWindow() != NULL)
				DocWindow()->PostMessage(message);
			else
				fPendingRefs = *message;	// replayed after ReadyToRun
		}
		if (message->HasSpecifiers() && DocWindow() != NULL) {
			BMessage spec;
			int32 what = 0;
			int32 index = 0;
			const char* prop = NULL;
			if (message->GetCurrentSpecifier(&index, &spec, &what,
					&prop) == B_OK
				&& kPDScriptingProperties.FindMatch(message, index, &spec,
					what, prop) >= 0) {
				PDWindow* window = DocWindow();
				window->Lock();
				HandleScriptingForWindow(window, message, prop);
				window->Unlock();
				return;
			}
		}
		switch (message->what) {
			case 'pdNw':
			{
				BRect frame(80, 60, 940, 740);
				if (DocWindow() != NULL)
					frame = DocWindow()->Frame().OffsetByCopy(24, 24);
				BScreen screen(B_MAIN_SCREEN_ID);
				if (frame.right > screen.Frame().right
					|| frame.bottom > screen.Frame().bottom)
					frame.OffsetTo(screen.Frame().left + 40,
						screen.Frame().top + 40);
				PDWindow* win = new PDWindow(frame, "Untitled");
				win->Show();
				break;
			}
			case 'pdWc':
			{
				void* closing = NULL;
				message->FindPointer("win", &closing);
				int32 docs = 0;
				for (int32 i = CountWindows() - 1; i >= 0; i--) {
					BWindow* w = WindowAt(i);
					if (w != NULL && w != (BWindow*)closing
						&& dynamic_cast<PDWindow*>(w) != NULL)
						docs++;
				}
				if (docs == 0)
					Quit();
				break;
			}
			default:
				BApplication::MessageReceived(message);
		}
	}

private:
	BMessage	fPendingRefs;

	PDWindow*	DocWindow()
	{
		for (int32 i = CountWindows() - 1; i >= 0; i--) {
			PDWindow* w = dynamic_cast<PDWindow*>(WindowAt(i));
			if (w != NULL)
				return w;
		}
		return NULL;
	}
};

// --------------------------------------------------------------- selftest --
static int
SelfTest()
{
	struct Case { const char* name; bool ok; };
	std::vector<Case> cases;
	bool all = true;
	#define CHECK(label, cond) { bool ok_ = (cond); \
		cases.push_back(Case{ label, ok_ }); \
		if (!ok_) all = false; }

	printf("block: model\n"); fflush(stdout);
	{
		PDDocument doc;
		CHECK("a4 default page", doc.Page().width == 595.0f
			&& doc.Page().height == 842.0f);
		PDPageSetup letter;
		letter.width = 612;
		letter.height = 792;
		doc.SetPage(letter);
		CHECK("letter page set", doc.Page().width == 612.0f);
		PDShape* r = doc.AddShape(PD_RECT, BRect(63.5, 63.5, 160.2, 120.7),
			"Box");
		CHECK("shape snaps to grid on create",
			r->rect.left == 64.0f && r->rect.top == 64.0f
				&& r->rect.right == 160.0f && r->rect.bottom == 120.0f);
		CHECK("label stored", r->label == "Box");
		CHECK("ids are 1-based", r->id == 1);
		doc.SetShapeRect(r->id, BRect(80, 80, 200, 160));
		CHECK("move snaps", doc.ShapeById(r->id)->rect == BRect(80, 80,
			200, 160));
		doc.Undo();
		CHECK("undo restores move", doc.ShapeById(r->id)->rect.left
			== 64.0f);
		doc.Redo();
		CHECK("redo reapplies move", doc.ShapeById(r->id)->rect.left
			== 80.0f);
		doc.RemoveShapes(std::vector<int32>(1, r->id));
		CHECK("delete empties", doc.Count() == 0);
		doc.Undo();
		CHECK("undo restores delete", doc.Count() == 1
			&& doc.ShapeAt(0)->label == "Box");

		// ids, not pointers: AddShape/Undo reallocate the vector, and a
		// held pointer into it is a dangling read
		int32 ia = doc.AddShape(PD_RECT, BRect(64, 64, 160, 128))->id;
		int32 ib = doc.AddShape(PD_RECT, BRect(200, 96, 320, 176))->id;
		int32 ic = doc.AddShape(PD_RECT, BRect(400, 128, 480, 200))->id;
		CHECK("align left", doc.Align(std::vector<int32> {ia, ib, ic},
			PD_ALIGN_LEFT)
			&& doc.ShapeById(ib)->rect.left == 64.0f
			&& doc.ShapeById(ic)->rect.left == 64.0f);
		doc.Undo();
		CHECK("align undoes", doc.ShapeById(ib)->rect.left == 200.0f);
		doc.Align(std::vector<int32> {ia, ib, ic}, PD_DISTRIBE_H);
		float gap1 = doc.ShapeById(ib)->rect.left
			- doc.ShapeById(ia)->rect.right;
		float gap2 = doc.ShapeById(ic)->rect.left
			- doc.ShapeById(ib)->rect.right;
		CHECK("distribute makes even gaps", fabsf(gap1 - gap2) < 0.01f
			&& gap1 > 0);
		CHECK("distribute needs three", !doc.Align(
			std::vector<int32> {ia, ib}, PD_DISTRIBE_H));
		doc.MoveZ(ia, true);
		CHECK("move to front reorders", doc.ShapeAt(doc.Count() - 1)->id
			== ia);
	}

	printf("block: connectors\n"); fflush(stdout);
	{
		PDDocument doc;
		int32 ia = doc.AddShape(PD_RECT, BRect(64, 64, 160, 128))->id;
		int32 ib = doc.AddShape(PD_RECT, BRect(256, 64, 352, 128))->id;
		{
			const PDShape* a = doc.ShapeById(ia);
			const PDShape* b = doc.ShapeById(ib);
			BPoint pa = PDDocument::AnchorPoint(*a, *b);
			BPoint pb = PDDocument::AnchorPoint(*b, *a);
			CHECK("anchor leaves east border", fabsf(pa.x - 160.0f) < 0.01f
				&& fabsf(pa.y - 96.0f) < 0.01f);
			CHECK("anchor arrives at west border", fabsf(pb.x - 256.0f)
				< 0.01f);
		}
		int32 icn = doc.AddShape(PD_CONNECTOR, BRect(0, 0, 0, 0))->id;
		doc.ShapeById(icn)->fromId = ia;
		doc.ShapeById(icn)->toId = ib;
		CHECK("connector hit mid-line", doc.ConnectorAtPoint(
			BPoint(208, 96)) == icn);
		CHECK("connector miss off-line", doc.ConnectorAtPoint(
			BPoint(208, 40)) == 0);
		doc.SetShapeRect(ib, BRect(256, 256, 352, 320));
		BPoint pb2 = PDDocument::AnchorPoint(*doc.ShapeById(ib),
			*doc.ShapeById(ia));
		CHECK("connector follows move", fabsf(pb2.y - 256.0f) < 0.01f);
		doc.RemoveShapes(std::vector<int32>(1, ia));
		CHECK("removing an endpoint removes the connector",
			doc.ShapeById(icn) == NULL);
	}

	printf("block: persistence\n"); fflush(stdout);
	{
		PDDocument doc;
		doc.AddShape(PD_RECT, BRect(64, 64, 200, 140), "Start");
		PDShape* e = doc.AddShape(PD_ELLIPSE, BRect(256, 64, 384, 140),
			"End");
		e->style.fill = rgb_color { 70, 170, 70, 255 };
		e->style.dashed = true;
		e->style.strokeWidth = 2;
		PDShape* cn = doc.AddShape(PD_CONNECTOR, BRect(0, 0, 0, 0));
		cn->fromId = 1;
		cn->toId = 2;
		BMessage msg;
		doc.SaveToMessage(&msg);
		PDDocument loaded;
		loaded.LoadFromMessage(&msg);
		CHECK("round trip count", loaded.Count() == 3);
		CHECK("round trip label", loaded.ShapeAt(0)->label == "Start");
		CHECK("round trip style", loaded.ShapeAt(1)->style.fill.green
			== 170 && loaded.ShapeAt(1)->style.dashed);
		CHECK("round trip connector ids",
			loaded.ShapeAt(2)->fromId == 1 && loaded.ShapeAt(2)->toId == 2);
		CHECK("loaded is clean", !loaded.IsModified());

		const char* path = "/tmp/pd-selftest.draw";
		CHECK("file save", doc.SaveToFile(path) == B_OK);
		CHECK("no temp left", !BEntry(BString(path).Append(".pdtmp")
			.String()).Exists());
		PDDocument fromFile;
		CHECK("file load", fromFile.LoadFromFile(path) == B_OK);
		CHECK("file round trip", fromFile.Count() == 3
			&& fromFile.ShapeAt(0)->label == "Start");
		CHECK("sniff native", PD_SniffDocument(path) == PD_KIND_NATIVE);
		BFile txt;
		if (txt.SetTo("/tmp/pd-selftest.txt",
				B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE) == B_OK)
			txt.Write("just text", 9);
		CHECK("sniff other", PD_SniffDocument("/tmp/pd-selftest.txt")
			== PD_KIND_OTHER);
		CHECK("sniff missing", PD_SniffDocument("/tmp/pd-none")
			== PD_KIND_ERROR);
		remove(path);
		remove("/tmp/pd-selftest.txt");

		CHECK("extension appended", WithExtension("diagram", ".draw")
			== "diagram.draw");
		CHECK("extension not doubled", WithExtension("a.DRAW", ".draw")
			== "a.DRAW");
	}

	printf("block: pdf\n"); fflush(stdout);
	{
		PDDocument doc;
		PDShape* box = doc.AddShape(PD_RECT, BRect(64, 64, 200, 140), "Box");
		PDShape* ell = doc.AddShape(PD_ELLIPSE, BRect(256, 64, 384, 140),
			"a(b)c");
		ell->style.dashed = true;
		ell->style.strokeWidth = 2;
		doc.AddShape(PD_RRECT, BRect(64, 200, 200, 280), "round");
		doc.AddShape(PD_DIAMOND, BRect(256, 200, 384, 280), "why");
		doc.AddShape(PD_TEXT, BRect(64, 320, 300, 360), "Note");
		PDShape* cn = doc.AddShape(PD_CONNECTOR, BRect(0, 0, 0, 0));
		cn->fromId = box->id;
		cn->toId = ell->id;

		CHECK("bad path rejected", PD_WritePDF(doc, "") == B_BAD_VALUE);

		const char* pdfPath = "/tmp/pd-selftest.pdf";
		CHECK("pdf write", PD_WritePDF(doc, pdfPath) == B_OK);
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
		CHECK("pdf read back", pdf.size() > 100);
		CHECK("pdf magic", pdf.compare(0, 8, "%PDF-1.4") == 0);
		CHECK("pdf a4 mediabox",
			pdf.find("/MediaBox [0 0 595 842]") != std::string::npos);
		CHECK("pdf one page", pdf.find("/Type /Page /Parent")
			!= std::string::npos && pdf.find("/Count 1") != std::string::npos);
		CHECK("pdf helvetica",
			pdf.find("/BaseFont /Helvetica") != std::string::npos);
		CHECK("pdf rect op", pdf.find(" re\n") != std::string::npos);
		CHECK("pdf bezier op", pdf.find(" c\n") != std::string::npos);
		CHECK("pdf label", pdf.find("(Box) Tj") != std::string::npos);
		CHECK("pdf label escapes parens",
			pdf.find("(a\\(b\\)c) Tj") != std::string::npos);
		CHECK("pdf dash pattern", pdf.find("[2 2] 0 d") != std::string::npos);
		CHECK("pdf arrowhead", pdf.find(" l h f Q") != std::string::npos);
		{
			// xref offsets must point at real objects: the byte at
			// startxref must be the 'x' of "xref"
			size_t at = pdf.rfind("startxref\n");
			bool ok = at != std::string::npos;
			if (ok) {
				long long off = atoll(pdf.c_str() + at + 10);
				ok = off > 0 && off < (long long)pdf.size()
					&& pdf[off] == 'x';
			}
			CHECK("pdf xref offset", ok);
		}

		doc.SetPage(PDPageSetup { 612.0f, 792.0f });		// US Letter
		CHECK("pdf letter rewrite", PD_WritePDF(doc, pdfPath) == B_OK);
		{
			std::string letter;
			BFile f;
			if (f.SetTo(pdfPath, B_READ_ONLY) == B_OK) {
				char buf[4096];
				ssize_t n;
				while ((n = f.Read(buf, sizeof(buf))) > 0)
					letter.append(buf, n);
			}
			CHECK("pdf letter mediabox",
				letter.find("/MediaBox [0 0 612 792]") != std::string::npos);
		}
		doc.SetPage(PDPageSetup { 842.0f, 595.0f });	// A4 landscape
		CHECK("pdf landscape rewrite", PD_WritePDF(doc, pdfPath) == B_OK);
		{
			std::string land;
			BFile f;
			if (f.SetTo(pdfPath, B_READ_ONLY) == B_OK) {
				char buf[4096];
				ssize_t n;
				while ((n = f.Read(buf, sizeof(buf))) > 0)
					land.append(buf, n);
			}
			CHECK("pdf landscape mediabox",
				land.find("/MediaBox [0 0 842 595]") != std::string::npos);
		}
		remove(pdfPath);
	}

	printf("block: recent\n"); fflush(stdout);
	{
		PDRecent r;
		r.Remember("/tmp/a.draw");
		r.Remember("/tmp/b.draw");
		r.Remember("/tmp/c.draw");
		CHECK("recent order", r.Items().size() == 3
			&& r.Items()[0] == "/tmp/c.draw");
		r.Remember("/tmp/a.draw");
		CHECK("recent dedupe to front", r.Items().size() == 3
			&& r.Items()[0] == "/tmp/a.draw"
			&& r.Items()[1] == "/tmp/c.draw");
		for (int i = 0; i < 12; i++) {
			BString p;
			p.SetToFormat("/tmp/many%d.draw", i);
			r.Remember(p);
		}
		CHECK("recent capped", (int32)r.Items().size() == PDRecent::kMax);
		CHECK("recent oldest dropped", r.Items().back() == "/tmp/many4.draw");

		const char* rp = "/tmp/pd-recent-test";
		CHECK("recent save", r.Save(rp) == B_OK);
		PDRecent q;
		CHECK("recent load", q.Load(rp) == B_OK);
		CHECK("recent round trip", q.Items().size() == r.Items().size()
			&& q.Items()[0] == r.Items()[0]
			&& q.Items().back() == r.Items().back());
		PDRecent none;
		CHECK("recent missing file", none.Load("/tmp/pd-none-recent")
			!= B_OK && none.Items().empty());
		remove(rp);
	}

	printf("block: routing\n"); fflush(stdout);
	{
		// ids, never PDShape*: AddShape reallocs the vector, so a
		// pointer from an earlier add dangles (the Sprint 1 lesson,
		// paid for once more here — NULL->kind through a stale id)
		PDDocument doc;
		int32 aId = doc.AddShape(PD_RECT, BRect(0, 0, 100, 50), "A")->id;
		int32 bId = doc.AddShape(PD_RECT, BRect(200, 80, 300, 130), "B")->id;
		int32 cId = doc.AddShape(PD_CONNECTOR, BRect(0, 0, 0, 0))->id;
		doc.ShapeById(cId)->fromId = aId;
		doc.ShapeById(cId)->toId = bId;

		std::vector<BPoint> wps;
		PDDocument::ConnectorWaypoints(*doc.ShapeById(aId),
			*doc.ShapeById(bId), *doc.ShapeById(cId), wps);
		CHECK("straight is two points", wps.size() == 2);
		doc.ShapeById(cId)->orthogonal = true;
		PDDocument::ConnectorWaypoints(*doc.ShapeById(aId),
			*doc.ShapeById(bId), *doc.ShapeById(cId), wps);
		CHECK("elbow is four points", wps.size() == 4);
		CHECK("elbow segments are axis-aligned",
			((wps[0].x == wps[1].x) || (wps[0].y == wps[1].y))
			&& ((wps[1].x == wps[2].x) || (wps[1].y == wps[2].y))
			&& ((wps[2].x == wps[3].x) || (wps[2].y == wps[3].y)));
		CHECK("elbow endpoints are the anchors",
			wps.front() == PDDocument::AnchorPoint(*doc.ShapeById(aId),
				*doc.ShapeById(bId))
			&& wps.back() == PDDocument::AnchorPoint(*doc.ShapeById(bId),
				*doc.ShapeById(aId)));

		// a point on the elbow's mid-segment, far off the straight
		// diagonal — only the polyline hit test finds the connector
		BPoint mid((wps[1].x + wps[2].x) / 2, (wps[1].y + wps[2].y) / 2);
		CHECK("elbow hit test follows the route",
			doc.ConnectorAtPoint(mid) == cId);

		// flags persist
		doc.ShapeById(cId)->arrowStart = true;
		BMessage msg;
		doc.SaveToMessage(&msg);
		PDDocument loaded;
		loaded.LoadFromMessage(&msg);
		const PDShape* lc = loaded.Count() == 3 ? loaded.ShapeAt(2) : NULL;
		CHECK("elbow persists", lc != NULL && lc->orthogonal);
		CHECK("arrow flags persist",
			lc != NULL && lc->arrowStart && lc->arrowEnd);

		// SetShapeFlags snapshots once, ignores non-connectors
		doc.SetShapeFlags(aId, false, true, false);
		CHECK("flags ignored on boxes",
			doc.ShapeById(aId)->kind == PD_RECT);
		// clear the raw-set flags: a real change, so this snapshot is
		// the one Undo takes (AddShape snapshots too — undoing the add
		// would remove the connector; that's correct, not for this test)
		doc.SetShapeFlags(cId, false, false, false);
		CHECK("flags applied", !doc.ShapeById(cId)->orthogonal
			&& !doc.ShapeById(cId)->arrowStart);
		doc.Undo();
		const PDShape* undone = doc.ShapeById(cId);
		CHECK("flags undo", undone != NULL && undone->orthogonal
			&& undone->arrowStart && doc.Count() == 3);
	}

	printf("block: images\n"); fflush(stdout);
	{
		// a 2x2 RGBA raster with distinct bytes
		uint8 rgba[16];
		for (int i = 0; i < 16; i++)
			rgba[i] = (uint8)(i * 16 + 8);
		PDDocument doc;
		int32 imgId = doc.AddImageRGBA(2, 2, rgba);
		CHECK("image added", imgId >= 0 && doc.CountImages() == 1);
		const PDImage* img = doc.ImageById(imgId);
		CHECK("image stored", img != NULL && img->width == 2
			&& img->height == 2 && img->bits.size() == 16
			&& memcmp(img->bits.data(), rgba, 16) == 0);
		CHECK("image bad rejected",
			doc.AddImageRGBA(0, 2, rgba) == -1);
		CHECK("image missing file", doc.AddImageFile("/tmp/pd-no-such")
			== -1);

		int32 aId = doc.AddShape(PD_IMAGE, BRect(10, 10, 110, 110))->id;
		doc.ShapeById(aId)->imageId = imgId;

		// persistence: rasters and their references round trip
		BMessage msg;
		doc.SaveToMessage(&msg);
		PDDocument loaded;
		loaded.LoadFromMessage(&msg);
		CHECK("image round trip count", loaded.CountImages() == 1);
		const PDImage* limg = loaded.ImageById(imgId);
		CHECK("image round trip bits", limg != NULL
			&& limg->bits.size() == 16
			&& memcmp(limg->bits.data(), rgba, 16) == 0);
		CHECK("image shape reference",
			loaded.ShapeById(aId)->kind == PD_IMAGE
			&& loaded.ShapeById(aId)->imageId == imgId);

		// a real BMP through the Translation Kit: 8x8 solid red, 24bpp
		{
			const int32 W = 8, H = 8;
			const int32 rowBytes = (W * 3 + 3) & ~3;
			uint8 bmp[54 + rowBytes * H];
			memset(bmp, 0, sizeof(bmp));
			bmp[0] = 'B'; bmp[1] = 'M';
			uint32 fileSize = 54 + rowBytes * H;
			memcpy(bmp + 2, &fileSize, 4);
			uint32 dataOffset = 54;
			memcpy(bmp + 10, &dataOffset, 4);
			uint32 headerSize = 40;
			memcpy(bmp + 14, &headerSize, 4);
			int32 w32 = W, h32 = H;
			memcpy(bmp + 18, &w32, 4);
			memcpy(bmp + 22, &h32, 4);
			uint16 planes = 1, bpp = 24;
			memcpy(bmp + 26, &planes, 2);
			memcpy(bmp + 28, &bpp, 2);
			for (int32 y = 0; y < H; y++) {
				uint8* row = bmp + 54 + (H - 1 - y) * rowBytes; // bottom-up
				for (int32 x = 0; x < W; x++) {
					row[x * 3 + 0] = 40;	// B
					row[x * 3 + 1] = 40;	// G
					row[x * 3 + 2] = 216;	// R
				}
			}
			BFile f;
			if (f.SetTo("/tmp/pd-selftest.bmp", B_WRITE_ONLY
					| B_CREATE_FILE | B_ERASE_FILE) == B_OK) {
				f.Write(bmp, sizeof(bmp));
				f.Unset();
			}
			status_t ierr = B_OK;
			int32 bmpId = doc.AddImageFile("/tmp/pd-selftest.bmp", &ierr);
			CHECK("bmp loads through translators",
				bmpId >= 0 && ierr == B_OK);
			const PDImage* bimg = doc.ImageById(bmpId);
			CHECK("bmp dimensions", bimg != NULL && bimg->width == 8
				&& bimg->height == 8 && bimg->bits.size() == 8 * 8 * 4);
			CHECK("bmp pixels", bimg != NULL
				&& bimg->bits.data()[0] == 40		// B
				&& bimg->bits.data()[1] == 40		// G
				&& bimg->bits.data()[2] == 216);	// R
			int32 bShape = doc.AddShape(PD_IMAGE, BRect(130, 10, 230, 110),
				NULL)->id;
			doc.ShapeById(bShape)->imageId = bmpId;

			// and the PDF embeds it as a Flate image XObject
			const char* pdfPath = "/tmp/pd-imgtest.pdf";
			CHECK("image pdf write", PD_WritePDF(doc, pdfPath) == B_OK);
			std::string pdf;
			BFile pf;
			if (pf.SetTo(pdfPath, B_READ_ONLY) == B_OK) {
				char buf[4096];
				ssize_t n;
				while ((n = pf.Read(buf, sizeof(buf))) > 0)
					pdf.append(buf, n);
			}
			CHECK("pdf image xobject",
				pdf.find("/Subtype /Image") != std::string::npos
				&& pdf.find("/Filter /FlateDecode")
					!= std::string::npos
				&& pdf.find("/Width 8") != std::string::npos
				&& pdf.find("/Width 2") != std::string::npos);
			CHECK("pdf images drawn",
				pdf.find("/Im0 Do") != std::string::npos
				&& pdf.find("/Im1 Do") != std::string::npos);
			remove(pdfPath);
			remove("/tmp/pd-selftest.bmp");
		}
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
		PDApp app;
		return SelfTest();
	}
	for (int i = 1; i < argc; i++)
		if (argv[i][0] != '-')
			gOpenPaths.push_back(argv[i]);
	PDApp app;
	app.Run();
	return 0;
}
