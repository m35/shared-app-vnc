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
class vncClient;
class vncServer;
class vncRegion;

typedef std::list<HWND> SharedAppList;


class SharedAppVnc
{ 
public:
	SharedAppVnc(vncServer* _server);
	~SharedAppVnc();
	BOOL SendUpdates(vncClient *client);
	BOOL RfbSendWindowClose(vncClient* client, HWND winHwnd );
	void AddWindow(HWND	winHwnd, HWND parentHwnd);
	void RemoveWindow(HWND winHwnd);
	void RemoveAllWindows();
	void GetVisibleRegion(HWND winHwnd, vncRegion& visibleRegionPtr);

	void SetClientsNeedUpdate(BOOL bUpdateNeeded);

	boolean bEnabled;
	boolean bOn;

private:
	boolean bIncludeDialogWindows;
	SharedAppList sharedAppList;
	char* reverseConnectionHost;
	vncServer *server;

};

#endif
