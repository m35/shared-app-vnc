/*
 * sharedapp.h
 *
 * Copyright (C) 2005 Grant Wallace.  All Rights Reserved.
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

#if (!defined(_SHAREDAPP_H))
#define _SHAREDAPP_H

#include <list>
#include <omnithread.h>
class vncClient;
class vncServer;
class vncRegion;

typedef std::list<HWND> WindowList;


class SharedAppVnc
{ 
public:
	friend class sharedAppTabCtrl;
	SharedAppVnc(vncServer* _server);
	~SharedAppVnc();
	BOOL SendUpdates(vncClient *client);
	BOOL RfbSendWindowClose(vncClient* client, HWND winHwnd );
	void AddWindow(HWND	winHwnd, HWND parentHwnd);
	void RemoveWindow(HWND winHwnd, bool bUpdateList);
	void RemoveAllWindows(bool bUpdateList);
	void GetVisibleRegion(HWND winHwnd, vncRegion& visibleRegionPtr);
	BOOL CheckPointer(POINT pt, HWND pointerWindow);

	void SetClientsNeedUpdate();

	char* GetLastClient();
	void SetLastClient(char *);

	bool StartViewer();
	bool StopViewer();

	char *GetViewerCommand();

	WindowList SharedAppList() { return sharedAppList; }

	boolean bEnabled;
	omni_mutex	m_sharedAppLock;

private:
	boolean bIncludeDialogWindows;
	WindowList sharedAppList;
	char* reverseConnectionHost;
	char* lastClient;
	//char* exePath;
	vncServer *server;
	HANDLE hViewerProcess;


};

#endif
