//
//  Copyright (c) 2002 Alkit Communications
//
//
// vncWinList
//
// Object implementing the Window selection dialog for WinVNC.
//

class vncWinList;

#if (!defined(_WINVNC_VNCWINLIST))
#define _WINVNC_VNCWINLIST

// Includes
#include "stdhdrs.h"

// The vncWinList class itself
class vncWinList
{
public:
	// Constructor/destructor
	vncWinList(SharedAppVnc *_shapp);
	~vncWinList();

	// Initialisation
	BOOL Init();

	// The dialog box window proc
	static BOOL CALLBACK DialogProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

	// General
	void Show(BOOL show);

	// Implementation
	BOOL m_dlgvisible;

private:
	SharedAppVnc *m_shapp;
};

#endif // _WINVNC_VNCPROPERTIES
