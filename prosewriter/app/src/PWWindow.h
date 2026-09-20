// PWWindow — one ProseWriter document window.
#ifndef PW_WINDOW_H
#define PW_WINDOW_H

#include <FilePanel.h>
#include <String.h>
#include <StringView.h>
#include <Window.h>

#include "PWDocument.h"
#include "PWLayout.h"
#include "PWSpell.h"

class BCheckBox;
class PWHeaderWindow;
class PWStylesWindow;
class PWPageSetupWindow;
class BMenu;
class BTextControl;
class PWPageView;
class PWRuler;

class PWWindow : public BWindow {
public:
	struct Panels;
			PWWindow(BRect frame, const char* title);

	bool	QuitRequested() override;
	void	MessageReceived(BMessage* message) override;
	void	MenusBeginning() override;
	void	FrameResized(float w, float h) override;

	status_t	OpenFile(const entry_ref& ref);
	PWDocument* Document() { return &fDoc; }
	PWPageView* View() { return fView; }
	PWLayout* Layout() { return &fLayout; }

	enum {
		DOC_MODIFIED_MSG	= 'pWup',
		OPEN_PANEL_MSG		= 'pWof',
		SAVE_PANEL_MSG		= 'pWsf',
		TEXT_APPLY_MSG		= 'pWta',
		FIND_BAR_MSG		= 'pWfd',
		FIND_NEXT_MSG		= 'pWfn',
		FIND_FIELD_MSG		= 'pWff',
		REPLACE_FIND_MSG	= 'pWrF',
		REPLACE_ALL_MSG	= 'pWrA',
		FAMILY_MSG		= 'pWfm',
		SIZE_MSG		= 'pWsz',
		COLOR_MSG		= 'pWcl',
		ALIGN_MSG		= 'pWal',
		ZOOM_MSG		= 'pWzm',
		FIT_WIDTH_MSG		= 'pWzw',
		MARGIN_MSG		= 'pWmg',
		PAGE_SETUP_MSG		= 'pWpg',
		PRINT_MSG		= 'pWpr',
		HEADER_MSG		= 'pWhd',
		APPLY_SETUP_MSG	= 'pWpA',
		APPLY_HEADER_MSG	= 'pWhA',
		RECENT_MSG		= 'pWrc',
		STYLES_MSG		= 'pWsy',
		STYLE_APPLY_MSG	= 'pWsa',
		STYLE_NEW_MSG		= 'pWsn',
		STYLE_DEL_MSG		= 'pWsd',
		EXPORT_RTF_MSG		= 'pWer',
		EXPORT_RTF_DONE_MSG	= 'pWex',
	};

private:
	void	BuildMenus();
	void	UpdateTitle();
	void	DoSave(const BString& path);
	void	UpdateStatusText();
	void	LayoutChildren();
	void	ToggleFindBar(bool show);
	void	FindNext();
	void	ReplaceAndFind();
	void	ReplaceAll();
	void	ApplyAlignment(int32 alignment);
public:
	void	ApplyPageSetup(const PWPageSetup& setup);
	void	AdoptDoc(PWDocument* fresh);
public:
	void	SetSpellChecker(PWSpellChecker* spell);
	Panels*	EnsurePanels();
private:
	void	SetZoom(float zoom);
	void	MarkZoomItem(float zoom);
	bool	HandleScripting(BMessage* message);
	void	Print();
	void	AddRecentFile(const char* path);
	void	BuildRecentMenu();

	PWDocument		fDoc;
	PWLayout		fLayout;
	PWPageView*		fView;
	PWRuler*		fRuler;
	BMenuBar*		fMenuBar;
	BView*			fFindBar;
	BTextControl*	fFindText;
	BTextControl*	fReplaceText;
	BCheckBox*		fCaseBox;
	bool			fFindShown = false;
	BString			fFilePath;		// empty = never saved
	BString			fFileName;
	BMenuItem*		fUndoItem;
	BMenuItem*		fRedoItem;
	BMenuItem*		fSaveItem;
	// File panels are created on first use; they are windows, and an
	// eagerly created one hijacks "Window 1" from script senders.
public:
	struct Panels {
		BFilePanel* open;
		BFilePanel* save;
		BFilePanel* exportRtf;
		Panels(BFilePanel* o, BFilePanel* s, BFilePanel* e)
			: open(o), save(s), exportRtf(e) {}
	};
	Panels*		fPanels = NULL;

private:
	BStringView*	fStatusView;
	BMenu*			fZoomMenu;
	BMenu*			fRecentMenu;
	PWSpellChecker*	fSpell;
	BMenuItem*		fSpellItem;
	PWPageSetupWindow*	fSetupWin = NULL;
	PWHeaderWindow*	fHeaderWin = NULL;
	PWStylesWindow*	fStylesWin = NULL;
};

#endif	// PW_WINDOW_H
