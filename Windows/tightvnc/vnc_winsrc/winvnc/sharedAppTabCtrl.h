/*
 * sharedapp.c
 *
 * Copyright (C) 2005 Grant Wallace, Princeton University. All Rights Reserved.
 *
 *  This is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This software is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this software; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307,
 *  USA.
 */


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
