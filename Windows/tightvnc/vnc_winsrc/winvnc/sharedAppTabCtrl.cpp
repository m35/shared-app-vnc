// sharedAppTabCtrl.cpp

// Implementation of the WinList dialog

#include "stdhdrs.h"

#include "WinVNC.h"
#include "sharedAppTabCtrl.h"
#include "vncServer.h"
#include "VSocket.h"
#include <Psapi.h>

int CreatePropertySheet();

// Constructor/destructor
sharedAppTabCtrl::sharedAppTabCtrl(SharedAppVnc *_shapp)
{
	m_dlgvisible = FALSE;
	m_shapp = _shapp;
}

sharedAppTabCtrl::~sharedAppTabCtrl()
{
}


// Initialisation
BOOL
sharedAppTabCtrl::Init()
{
	return TRUE;
}

// Dialog box handling functions
void
sharedAppTabCtrl::Show(BOOL show)
{
	if (show)
	{
		if (!IsWindow(hPropSht))
		{
			CreatePropertySheet(NULL);
		} else {
			SetForegroundWindow(hPropSht);
		}
	}
}

void sharedAppTabCtrl::SetTab(int tab)
{
	HWND hTab;
	switch (tab)
	{
	case 0:
		hTab = hWinListDlg;
		break;
	case 1:
		hTab = hClientDlg;
		break;
	case 2:
		hTab = hModeDlg;
		break;
	}
	PropSheet_SetCurSel(hPropSht, hTab, tab);
}

int GetProcessName(HWND hwnd, char* name, UINT bufferSize)
{
	HANDLE  m_hProcess;
	DWORD pid;
	int res = 0;

	*name='\0';

	GetWindowThreadProcessId(hwnd, &pid);
	m_hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,  FALSE, pid);
	if (m_hProcess) {
		HMODULE m_hModules[1024];
		DWORD m_count;
		EnumProcessModules(m_hProcess, m_hModules,  sizeof(m_hModules), &m_count);
		if (m_count > 0) 
		{
			if (m_hModules[0]) {
				GetModuleBaseName(m_hProcess, m_hModules[0], name, bufferSize);
				if (*name)
				{
					char *strpos;

					if (strpos = strchr(name, '.')) *strpos = NULL;
					strcat(name, " - \"");

					res = GetWindowText(hwnd, name+strlen(name), bufferSize-strlen(name));
					strcat(name, "\"");
				}
			}
		}
		CloseHandle(m_hProcess);
	}
	return res;
}

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM hDlg)
{
	char name[256];
	int n;

	n = GetProcessName(hwnd, name, sizeof(name));
	
	if(IsWindowVisible(hwnd) && n>0) {
		HWND hShared = GetDlgItem((HWND)hDlg, IDC_SHARED_LIST);
		int listitem = SendMessage(hShared, LB_FINDSTRING, 0, (LPARAM)(LPCTSTR) name);
		if (listitem == LB_ERR)
		{
			HWND hList = GetDlgItem((HWND)hDlg, IDC_WIN_LIST);
			listitem = SendMessage(hList, LB_ADDSTRING, 0, 
				(LPARAM)(LPCTSTR) name);
			SendMessage(hList, LB_SETITEMDATA,      
				(WPARAM) listitem, (LPARAM) hwnd);
		}
	}

	return TRUE; 
}



void sharedAppTabCtrl::refreshWindowList(HWND hDlg)
{
	HWND hList, hShared;
	char name[1024];
	WindowList::iterator iter;
	WindowList sharedAppList = m_shapp->SharedAppList();

	hList = GetDlgItem(hDlg, IDC_WIN_LIST);
	SendMessage(hList, LB_RESETCONTENT, 0, 0);

	hShared = GetDlgItem(hDlg, IDC_SHARED_LIST);
	SendMessage(hShared, LB_RESETCONTENT, 0, 0);

	// Add the shared window listings
	{	omni_mutex_lock l(m_shapp->m_sharedAppLock);
	for (iter = sharedAppList.begin(); iter != sharedAppList.end(); iter++)
	{
		HWND winHwnd = *iter;
		if (GetProcessName(winHwnd, name, sizeof(name)))
		{
			int listitem = SendMessage(hShared, LB_ADDSTRING, 0, (LPARAM)(LPCTSTR) name);
			SendMessage(hShared, LB_SETITEMDATA, (WPARAM) listitem, (LPARAM) winHwnd);
		}
	}
	}

	// Fill in list box with the desktop's windows' names
	EnumDesktopWindows(NULL, EnumWindowsProc, (long)hDlg);
}



BOOL CALLBACK
sharedAppTabCtrl::winListDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam )
{
	HWND hList, hShared;
	int curr_item, listitem, nItems;
	HWND sel_hwnd;
	char name[1024];


	// We use the dialog-box's USERDATA to store a _this pointer
	// This is set only once WM_INITDIALOG has been recieved, though!
	sharedAppTabCtrl *_this = (sharedAppTabCtrl *) GetWindowLong(hDlg, GWL_USERDATA);

	switch (uMsg)
	{

	case WM_INITDIALOG:
		{
			// Retrieve the Dialog box parameter and use it as a pointer
			// to the calling sharedAppTabCtrl object
			PROPSHEETPAGE *ps = (PROPSHEETPAGE *)lParam;
			SetWindowLong(hDlg, GWL_USERDATA, ps->lParam);
			_this = (sharedAppTabCtrl *) ps->lParam;

			_this->hWinListDlg = hDlg;
			_this->hPropSht = GetParent(hDlg);		

			// Load button icons
			HWND hButton = GetDlgItem(hDlg, ID_SHARE_WIN);
			HICON hIcon = LoadIcon(hAppInstance, MAKEINTRESOURCE(IDI_DOWN_ARROW));
			SendMessage(hButton, BM_SETIMAGE, IMAGE_ICON, (LPARAM) (DWORD) hIcon);

			hButton = GetDlgItem(hDlg, ID_HIDE_WIN);
			hIcon = LoadIcon(hAppInstance, MAKEINTRESOURCE(IDI_UP_ARROW));
			SendMessage(hButton, BM_SETIMAGE, IMAGE_ICON, (LPARAM) (DWORD) hIcon);

			hButton = GetDlgItem(hDlg, ID_HIDE_ALL);
			hIcon = LoadIcon(hAppInstance, MAKEINTRESOURCE(IDI_UPALL_ARROW));
			SendMessage(hButton, BM_SETIMAGE, IMAGE_ICON, (LPARAM) (DWORD) hIcon);

			hButton = GetDlgItem(hDlg, ID_WIN_REFRESH);
			hIcon = LoadIcon(hAppInstance, MAKEINTRESOURCE(IDI_REFRESH));
			SendMessage(hButton, BM_SETIMAGE, IMAGE_ICON, (LPARAM) (DWORD) hIcon);

			// Show the dialog
			SetForegroundWindow(hDlg);

			return TRUE;
		}

	case WM_NOTIFY:
		switch (((NMHDR FAR *) lParam)->code) 
		{

		case PSN_SETACTIVE:
			// initialize the controls
			_this->refreshWindowList(hDlg);			
			break;

		case PSN_KILLACTIVE:
			SetWindowLong(hDlg,	DWL_MSGRESULT, FALSE);
			return 1;
			break;

		case PSN_APPLY: // OK or Apply button
			SetWindowLong(hDlg,	DWL_MSGRESULT, TRUE);
			EndDialog(_this->hPropSht, IDOK);
			_this->hPropSht = NULL;
			break;

		case PSN_RESET: // Cancel button
			// rest to the original values
			SetWindowLong(hDlg,	DWL_MSGRESULT, FALSE);
			EndDialog(_this->hPropSht, IDCANCEL);
			_this->hPropSht = NULL;
			break;
		}
		return FALSE;

	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case ID_SHARE_WIN:
			// Get selected window's hwnd
			hList = GetDlgItem(hDlg, IDC_WIN_LIST);
			hShared = GetDlgItem(hDlg, IDC_SHARED_LIST);
			if ((curr_item = SendMessage(hList, LB_GETCURSEL, 0, 0)) != LB_ERR) {
				// Get Name and Data
				SendMessage(hList, LB_GETTEXT, curr_item, (LPARAM)(LPCTSTR) name);
				sel_hwnd = (HWND) SendMessage(hList, LB_GETITEMDATA, curr_item, 0);

				// add to shared_win list box
				listitem = SendMessage(hShared, LB_ADDSTRING, 0, (LPARAM)(LPCTSTR) name);
				SendMessage(hShared, LB_SETITEMDATA, (WPARAM) listitem, (LPARAM) sel_hwnd);

				// remove from select list box
				SendMessage(hList, LB_DELETESTRING, curr_item, 0);

				if(IsIconic(sel_hwnd))
					OpenIcon(sel_hwnd);
				BringWindowToTop(sel_hwnd);
				//shared_window = sel_hwnd;
				_this->m_shapp->AddWindow(sel_hwnd, 0);
				vnclog.Print(LL_SHAREDAPP, "Share Window %x\n", sel_hwnd);
			}
			return TRUE;

		case ID_HIDE_WIN:
			// Get selected window's hwnd
			hList = GetDlgItem(hDlg, IDC_WIN_LIST);
			hShared = GetDlgItem(hDlg, IDC_SHARED_LIST);
			if ((curr_item = SendMessage(hShared, LB_GETCURSEL, 0, 0)) != LB_ERR) {
				// Get Name and Data
				SendMessage(hShared, LB_GETTEXT, curr_item, (LPARAM)(LPCTSTR) name);
				sel_hwnd = (HWND) SendMessage(hShared, LB_GETITEMDATA, curr_item, 0);

				// add to select list box
				listitem = SendMessage(hList, LB_ADDSTRING, 0, (LPARAM)(LPCTSTR) name);
				SendMessage(hList, LB_SETITEMDATA, (WPARAM) listitem, (LPARAM) sel_hwnd);

				// remove from shared list box
				SendMessage(hShared, LB_DELETESTRING, curr_item, 0);

				_this->m_shapp->RemoveWindow(sel_hwnd, true);
			}
			return TRUE;

		case ID_HIDE_ALL:
			// Get selected window's hwnd
			hList = GetDlgItem(hDlg, IDC_WIN_LIST);
			hShared = GetDlgItem(hDlg, IDC_SHARED_LIST);
			nItems = SendMessage(hShared, LB_GETCOUNT, 0, 0);
			for (curr_item=0; curr_item < nItems; curr_item++)
			{
				// Get Name and Data
				SendMessage(hShared, LB_GETTEXT, curr_item, (LPARAM)(LPCTSTR) name);
				sel_hwnd = (HWND) SendMessage(hShared, LB_GETITEMDATA, curr_item, 0);

				// add to select list box
				listitem = SendMessage(hList, LB_ADDSTRING, 0, (LPARAM)(LPCTSTR) name);
				SendMessage(hList, LB_SETITEMDATA, (WPARAM) listitem, (LPARAM) sel_hwnd);

				_this->m_shapp->RemoveWindow(sel_hwnd, true);
			}			
			// remove from shared list box
			SendMessage(hShared, LB_RESETCONTENT, 0, 0);

			//_this->m_shapp->RemoveAllWindows();

			return TRUE;
		case ID_WIN_REFRESH:
			_this->refreshWindowList(hDlg);
			return TRUE;
		}
		break;
	}
	return FALSE;

}


// Callback function - handles messages sent to the dialog box

BOOL CALLBACK sharedAppTabCtrl::connDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	// This is a static method, so we don't know which instantiation we're 
	// dealing with. But we can get a pseudo-this from the parameter to 
	// WM_INITDIALOG, which we therafter store with the window and retrieve
	// as follows:

	sharedAppTabCtrl *_this = (sharedAppTabCtrl *) GetWindowLong(hDlg, GWL_USERDATA);
	HWND hList;
	int curr_item;
	vncClientId clientId;

	switch (uMsg) {

		// Dialog has just been created
	case WM_INITDIALOG:
		{
			// Save the lParam into our user data so that subsequent calls have
			// access to the parent C++ object
			PROPSHEETPAGE *ps = (PROPSHEETPAGE *)lParam;
			SetWindowLong(hDlg, GWL_USERDATA, ps->lParam);
			_this = (sharedAppTabCtrl *) ps->lParam;

			_this->hClientDlg = hDlg;
			_this->hPropSht = GetParent(hDlg);
			
			// load the user settings recent client
			HWND hEdit = GetDlgItem(hDlg, IDC_HOSTNAME_EDIT);
			SendMessage(hEdit, WM_SETTEXT, 0, (LPARAM)(LPCTSTR)_this->m_shapp->GetLastClient());
			CheckDlgButton(hDlg, IDC_DISABLE_INCOMING, BST_UNCHECKED);
			// Return success!
			return TRUE;
		}

	case WM_NOTIFY:
		switch (((NMHDR FAR *) lParam)->code) 
		{

		case PSN_SETACTIVE:
			// initialize the controls

			// add any new incoming clients to the list box
			{
				vncClientList::iterator i;
				vncClientList clients = _this->m_shapp->server->ClientList();

				hList = GetDlgItem(hDlg, IDC_CLIENT_LIST);
				SendMessage(hList, LB_RESETCONTENT, 0, 0);

				// Post this update to all the connected clients
				for (i = clients.begin(); i != clients.end(); i++)
				{
					vncClient *client = _this->m_shapp->server->GetClient(*i);
					const char *clientName = client->GetClientPrettyName();
					if (! clientName) clientName = client->GetClientName();
					int listitem = SendMessage(hList, LB_ADDSTRING, 0, (LPARAM)(LPCTSTR) clientName);
					SendMessage(hList, LB_SETITEMDATA, (WPARAM) listitem, (LPARAM) client->GetClientId());
				}
			}
			break;

		case PSN_KILLACTIVE:
			SetWindowLong(hDlg,	DWL_MSGRESULT, FALSE);
			return 1;
			break;

		case PSN_APPLY: // OK or Apply button
			SetWindowLong(hDlg,	DWL_MSGRESULT, TRUE);
			EndDialog(_this->hPropSht, IDOK);
			_this->hPropSht = NULL;
			break;

		case PSN_RESET: // Cancel button
			// rest to the original values
			SetWindowLong(hDlg,	DWL_MSGRESULT, FALSE);
			EndDialog(_this->hPropSht, IDCANCEL);
			_this->hPropSht = NULL;
			break;
		}
		return FALSE;

		// Dialog has just received a command
	case WM_COMMAND:
		switch (LOWORD(wParam)) 
		{
			// User clicked OK or pressed return
		case ID_CONN:
			char hostname[_MAX_PATH];
			char name[_MAX_PATH];
			char *portp;
			int port;

			// Get the hostname of the VNCviewer
			GetDlgItemText(hDlg, IDC_HOSTNAME_EDIT, hostname, _MAX_PATH);
			strcpy(name, hostname);

			// Calculate the Display and Port offset.
			port = INCOMING_PORT_OFFSET;
			portp = strchr(hostname, ':');
			if (portp) {
				*portp++ = '\0';
				port = atoi(portp);
				if (port<100) port += INCOMING_PORT_OFFSET;
			}

			// Attempt to create a new socket
			VSocket *tmpsock;
			tmpsock = new VSocket;
			if (!tmpsock)
				return TRUE;

			// Connect out to the specified host on the VNCviewer listen port
			// To be really good, we should allow a display number here but
			// for now we'll just assume we're connecting to display zero
			tmpsock->Create();
			if (tmpsock->Connect(hostname, port)) {
				// Add the new client to this server
				clientId = _this->m_shapp->server->AddClient(tmpsock, TRUE, TRUE);

				_this->m_shapp->server->GetClient(clientId)->SetClientPrettyName(name);

				// Add to the client list
				hList = GetDlgItem(hDlg, IDC_CLIENT_LIST);
				int listitem = SendMessage(hList, LB_ADDSTRING, 0, (LPARAM)(LPCTSTR) name);
				SendMessage(hList, LB_SETITEMDATA, (WPARAM) listitem, (LPARAM) clientId);
				// set the user prefs last client
				_this->m_shapp->SetLastClient(name);
			} else {
				// Print up an error message
				MessageBox(NULL, 
					"Failed to connect to listening VNC viewer",
					"Outgoing Connection",
					MB_OK | MB_ICONEXCLAMATION );
				delete tmpsock;
			}
			return TRUE;
		case IDC_DISCONNECT_CLIENT:
			// Get selected Client
			hList = GetDlgItem(hDlg, IDC_CLIENT_LIST);
			if ((curr_item = SendMessage(hList, LB_GETCURSEL, 0, 0)) != LB_ERR) {
				clientId = (vncClientId) SendMessage(hList, LB_GETITEMDATA, curr_item, 0);
				// remove from list box
				SendMessage(hList, LB_DELETESTRING, curr_item, 0);
				// disconnect the client
				_this->m_shapp->server->GetClient(clientId)->Kill();
			}
			return TRUE;
		case IDC_DISCONNECT_ALL:
			// clear the list box
			hList = GetDlgItem(hDlg, IDC_CLIENT_LIST);
			SendMessage(hList, LB_RESETCONTENT, 0, 0);
			// disconnect all clients
			_this->m_shapp->server->KillAuthClients();
			return TRUE;
		case IDC_DISABLE_INCOMING:
			if (IsDlgButtonChecked(hDlg, IDC_DISABLE_INCOMING) == BST_CHECKED)
			{
				_this->m_shapp->server->DisableClients(TRUE);
			} else {
				_this->m_shapp->server->DisableClients(FALSE);
			}
		}

		break;
	}
	return FALSE;
}


BOOL CALLBACK
sharedAppTabCtrl::modeDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam )
{
	sharedAppTabCtrl *_this = (sharedAppTabCtrl *) GetWindowLong(hDlg, GWL_USERDATA);
	HWND hText;

	switch (uMsg) {
	// Dialog has just been created
	case WM_INITDIALOG:
		{
			// Save the lParam into our user data so that subsequent calls have
			// access to the parent C++ object
			PROPSHEETPAGE *ps = (PROPSHEETPAGE *)lParam;
			SetWindowLong(hDlg, GWL_USERDATA, ps->lParam);
			_this = (sharedAppTabCtrl *) ps->lParam;

			_this->hClientDlg = hDlg;
			_this->hPropSht = GetParent(hDlg);

			CheckRadioButton(hDlg, IDC_RADIO_DESKTOP, IDC_RADIO_WINDOWS,
				(_this->m_shapp->bEnabled)	? IDC_RADIO_WINDOWS : IDC_RADIO_DESKTOP);

			// Return success!
			return TRUE;
		}

	case WM_NOTIFY:
		switch (((NMHDR FAR *) lParam)->code) 
		{

		case PSN_SETACTIVE:
			// initialize the controls
			//InvalidateRect(hDlg, NULL, FALSE);
			break;

		case PSN_KILLACTIVE:
			SetWindowLong(hDlg,	DWL_MSGRESULT, FALSE);
			return 1;
			break;

		case PSN_APPLY: // OK or Apply button
			SetWindowLong(hDlg,	DWL_MSGRESULT, TRUE);
			EndDialog(_this->hPropSht, IDOK);
			_this->hPropSht = NULL;
			break;

		case PSN_RESET: // Cancel button
			// rest to the original values
			SetWindowLong(hDlg,	DWL_MSGRESULT, FALSE);
			EndDialog(_this->hPropSht, IDCANCEL);
			_this->hPropSht = NULL;
			break;
		}
		return FALSE;

		// Dialog has just received a command
	case WM_COMMAND:
		switch (LOWORD(wParam)) 
		{
			/*
		case IDC_RADIO_NOSHARING:
			_this->m_shapp->bEnabled = TRUE;
			_this->m_shapp->bOn = FALSE;
			_this->m_shapp->RemoveAllWindows(false);
			return TRUE;
			*/
		case IDC_RADIO_DESKTOP:
			_this->m_shapp->bEnabled = FALSE;
			_this->m_shapp->SetClientsNeedUpdate();
			return TRUE;
		case IDC_RADIO_WINDOWS:
			_this->m_shapp->bEnabled = TRUE;
			//_this->m_shapp->bOn = TRUE;
			_this->m_shapp->RemoveWindow(0, false);
			_this->m_shapp->SetClientsNeedUpdate();
			return TRUE;
		case IDC_START_VIEWER:
			hText = GetDlgItem(hDlg, IDC_VIEWER_STATUS);
			if (_this->m_shapp->StartViewer())
			{
				SendMessage(hText, WM_SETTEXT, 0, (LPARAM)(LPCTSTR) "Started");
			} else {
				SendMessage(hText, WM_SETTEXT, 0, (LPARAM)(LPCTSTR) "Can't Access: May already be running?");
			}
			return TRUE;
		case IDC_STOP_VIEWER:
			hText = GetDlgItem(hDlg, IDC_VIEWER_STATUS);
			if (_this->m_shapp->StopViewer())
			{
				SendMessage(hText, WM_SETTEXT, 0, (LPARAM)(LPCTSTR) "Stopped");
			}
			return TRUE;
			
		}	
		break;
	}
	return FALSE;
}


/****************************************************************************
*    FUNCTION: CreatePropertySheet(HWND)
*
*    PURPOSE:  Creates a property sheet
*
****************************************************************************/
HWND sharedAppTabCtrl::CreatePropertySheet(HWND hwndOwner)
{
	PROPSHEETPAGE psp[3];
	PROPSHEETHEADER psh;

	psp[0].dwSize = sizeof(PROPSHEETPAGE);
	psp[0].dwFlags = PSP_USETITLE;
	psp[0].hInstance = hAppInstance;
	psp[0].pszTemplate = MAKEINTRESOURCE(IDD_SHARED_WINDOWS); 
	psp[0].pszIcon = NULL;
	psp[0].pfnDlgProc = winListDlgProc;
	psp[0].pszTitle = "Windows";
	psp[0].lParam = (LONG) this;

	psp[1].dwSize = sizeof(PROPSHEETPAGE);
	psp[1].dwFlags = PSP_USETITLE;
	psp[1].hInstance = hAppInstance;
	psp[1].pszTemplate =  MAKEINTRESOURCE(IDD_CLIENTS); 
	psp[1].pszIcon = NULL;
	psp[1].pfnDlgProc = connDlgProc;
	psp[1].pszTitle = "Clients";
	psp[1].lParam = (LONG) this;

	psp[2].dwSize = sizeof(PROPSHEETPAGE);
	psp[2].dwFlags = PSP_USETITLE;
	psp[2].hInstance = hAppInstance;
	psp[2].pszTemplate =  MAKEINTRESOURCE(IDD_MODE); 
	psp[2].pszIcon = NULL;
	psp[2].pfnDlgProc = modeDlgProc;
	psp[2].pszTitle = "Mode";
	psp[2].lParam = (LONG) this;

	psh.dwSize = sizeof(PROPSHEETHEADER);
	psh.dwFlags = PSH_PROPSHEETPAGE | PSH_NOAPPLYNOW | PSH_MODELESS;
	psh.hwndParent = hwndOwner;
	psh.hInstance = hAppInstance;
	psh.pszIcon = NULL;
	psh.pszCaption = (LPSTR) "SharedAppVnc";
	psh.nPages = sizeof(psp) / sizeof(PROPSHEETPAGE);
	psh.ppsp = (LPCPROPSHEETPAGE) &psp;

	return ((HWND) PropertySheet(&psh));
}

