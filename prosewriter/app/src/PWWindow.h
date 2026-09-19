// PWWindow — one ProseWriter document window.
#ifndef PW_WINDOW_H
#define PW_WINDOW_H

#include <FilePanel.h>
#include <String.h>
#include <StringView.h>
#include <Window.h>

#include "PWDocument.h"
#include "PWLayout.h"

class PWPageView;

class PWWindow : public BWindow {
public:
			PWWindow(BRect frame, const char* title);

	bool	QuitRequested() override;
	void	MessageReceived(BMessage* message) override;
	void	MenusBeginning() override;

	status_t	OpenFile(const entry_ref& ref);
	PWDocument* Document() { return &fDoc; }
	PWPageView* View() { return fView; }

	enum {
		DOC_MODIFIED_MSG	= 'pWup',
		OPEN_PANEL_MSG		= 'pWof',
		SAVE_PANEL_MSG		= 'pWsf',
		TEXT_APPLY_MSG		= 'pWta',
	};

private:
	void	BuildMenus();
	void	UpdateTitle();
	void	DoSave(const BString& path);
	void	SaveIfNeeded();			// ask before losing changes
	void	UpdateStatusText();

	PWDocument		fDoc;
	PWLayout		fLayout;
	PWPageView*		fView;
	BMenuBar*		fMenuBar;
	BString			fFilePath;		// empty = never saved
	BString			fFileName;
	BMenuItem*		fUndoItem;
	BMenuItem*		fRedoItem;
	BMenuItem*		fSaveItem;
	BFilePanel*		fOpenPanel;
	BFilePanel*		fSavePanel;
	BStringView*	fStatusView;
};

#endif	// PW_WINDOW_H
