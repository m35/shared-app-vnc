//  Copyright (C) 2002 Alkit Communications
//

// vncWinList.cpp

// Implementation of the WinList dialog

#include "stdhdrs.h"

#include "WinVNC.h"
#include "vncWinList.h"

// Constructor/destructor
vncWinList::vncWinList(SharedAppVnc *_shapp)
{
	m_dlgvisible = FALSE;
	m_shapp = _shapp;
}

vncWinList::~vncWinList()
{
}

// Initialisation
BOOL
vncWinList::Init()
{
	return TRUE;
}

// Dialog box handling functions
void
vncWinList::Show(BOOL show)
{
	if (show)
	{
		if (!m_dlgvisible)
		{
			DialogBoxParam(hAppInstance,
				MAKEINTRESOURCE(IDD_SELECT_WIN), 
				NULL,
				(DLGPROC) DialogProc,
				(LONG) this);
		}
	}
}

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM hDlg)
{
  char name[1000];
  HWND hList;
  //wchar_t name_lpcstr[1000];
  int listitem, n;

  if(hDlg == NULL) {
	hList = GetDlgItem(hwnd, IDC_WIN_LIST);
    listitem = SendMessage(hList, LB_ADDSTRING, 0, 
                                 (LPARAM)(LPCTSTR) "Full screen");
	SendMessage(hList, LB_SETITEMDATA,      
                     (WPARAM) listitem, (LPARAM) NULL);
	return TRUE;
  }
  n = GetWindowText(hwnd, name, 999);

  if(IsWindowVisible(hwnd) /*&& !IsIconic(hwnd)*/ && n>0) {
    hList = GetDlgItem((HWND)hDlg, IDC_WIN_LIST);
    /*mbstowcs(name_lpcstr, name, n+1);
	listitem = SendMessage(hList, LB_ADDSTRING, 0, 
                                 (LPARAM)(LPCTSTR) name_lpcstr);*/
	listitem = SendMessage(hList, LB_ADDSTRING, 0, 
                                 (LPARAM)(LPCTSTR) name);
	SendMessage(hList, LB_SETITEMDATA,      
                     (WPARAM) listitem, (LPARAM) hwnd);
    //MessageBox(NULL, name, name, MB_OK);
  }

  return TRUE; 
}

BOOL CALLBACK
vncWinList::DialogProc(HWND hwnd,
					 UINT uMsg,
					 WPARAM wParam,
					 LPARAM lParam )
{
	HWND hList;
	int curr_item;
	HWND sel_hwnd;

	// We use the dialog-box's USERDATA to store a _this pointer
	// This is set only once WM_INITDIALOG has been recieved, though!
	vncWinList *_this = (vncWinList *) GetWindowLong(hwnd, GWL_USERDATA);

	switch (uMsg)
	{

	case WM_INITDIALOG:
		{
			// Retrieve the Dialog box parameter and use it as a pointer
			// to the calling vncProperties object
			SetWindowLong(hwnd, GWL_USERDATA, lParam);
			_this = (vncWinList *) lParam;

			// Show the dialog
			SetForegroundWindow(hwnd);

			_this->m_dlgvisible = TRUE;

			// Fill in list box with the desktop's window's names
			EnumDesktopWindows(NULL, EnumWindowsProc, (long)hwnd);
			// Add the 'Full screen' list entry
            EnumWindowsProc(hwnd, NULL);
			return TRUE;
		}

	case WM_COMMAND:
		switch (LOWORD(wParam))
		{

		case IDCANCEL:
			// Close the dialog
			EndDialog(hwnd, TRUE);

			_this->m_dlgvisible = FALSE;

			return TRUE;

		break;

		case IDC_RADIO_NOSHARING:
			_this->m_shapp->bEnabled = TRUE;
			_this->m_shapp->bOn = FALSE;
			return TRUE;
		case IDC_RADIO_DESKTOP:
			_this->m_shapp->bEnabled = FALSE;
			return TRUE;
		case IDC_RADIO_WINDOWS:
			_this->m_shapp->bEnabled = TRUE;
			_this->m_shapp->bOn = TRUE;
			return TRUE;

		case ID_SHARE_WIN:
			// Get selected window's hwnd
			hList = GetDlgItem(hwnd, IDC_WIN_LIST);
			if ((curr_item = SendMessage(hList, LB_GETCURSEL, 0, 0)) != LB_ERR) {
				sel_hwnd = (HWND) SendMessage(hList, LB_GETITEMDATA, curr_item, 0);

				if(IsIconic(sel_hwnd))
					OpenIcon(sel_hwnd);
				BringWindowToTop(sel_hwnd);
				//shared_window = sel_hwnd;
				_this->m_shapp->AddWindow(sel_hwnd, 0);
			}
			// Close the dialog
			//EndDialog(hwnd, TRUE);
			//_this->m_dlgvisible = FALSE;

			return TRUE;

		case ID_HIDE_WIN:
			// Get selected window's hwnd
			hList = GetDlgItem(hwnd, IDC_WIN_LIST);
			if ((curr_item = SendMessage(hList, LB_GETCURSEL, 0, 0)) != LB_ERR) {
				sel_hwnd = (HWND) SendMessage(hList, LB_GETITEMDATA, curr_item, 0);
				_this->m_shapp->RemoveWindow(sel_hwnd);
			}
			return TRUE;
		}

		break;
	case WM_DESTROY:
		EndDialog(hwnd, FALSE);
		_this->m_dlgvisible = FALSE;
		return TRUE;
	}
	return 0;
}
