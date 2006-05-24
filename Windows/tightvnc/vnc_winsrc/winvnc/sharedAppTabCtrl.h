//
// sharedAppTabCtrl
//
// Object implementing the Window selection dialog for WinVNC.
//

class sharedAppTabCtrl;

#if (!defined(_WINVNC_sharedAppTabCtrl))
#define _WINVNC_sharedAppTabCtrl

// Includes
#include "stdhdrs.h"
#include "sharedapp.h"

#define SHAP_TAB_WINDOWS 0
#define SHAP_TAB_CLIENTS 1

// The sharedAppTabCtrl class itself
class sharedAppTabCtrl
{
public:
	// Constructor/destructor
	sharedAppTabCtrl(SharedAppVnc *_shapp);
	~sharedAppTabCtrl();

	// Initialisation
	BOOL Init();

	// The dialog box window proc
	static BOOL CALLBACK winListDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
	static BOOL CALLBACK connDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
	static BOOL CALLBACK modeDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);

	// General
	HWND CreatePropertySheet(HWND hwndOwner);
	void Show(BOOL show);
	void SetTab(int tab);
	void refreshWindowList(HWND hDlg);

	// Implementation
	BOOL m_dlgvisible;

private:
	SharedAppVnc *m_shapp;
	HWND hPropSht;
	HWND hWinListDlg;
	HWND hClientDlg;
	HWND hModeDlg;
};

#endif // _WINVNC_sharedAppTabCtrl
