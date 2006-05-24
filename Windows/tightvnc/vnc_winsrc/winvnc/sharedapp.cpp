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

 /* Some of sharedapp_RfbSendUpdates orginates from rfbserver.c 
  * under following GPL copyrights
  * Copyright (C) 2000-2004 Constantin Kaplinsky.  All Rights Reserved.
  * Copyright (C) 2000 Tridia Corporation.  All Rights Reserved.
  * Copyright (C) 1999 AT&T Laboratories Cambridge.  All Rights Reserved.
  */

/* sharedapp.c - adds extensions to VNC server that allow specific 
 *               application windows to be shared rather than the entire
 *               desktop.
 */


/* Locking Protocol Used:
 *
 * Locks are acquired only in the following order
 * 1) m_clientsLock  2) m_regionLock  3) m_sharedAppLock
 * 
 * If a client pointer is passed as a parameter it is assumed m_regionLock is locked
 */

/* Some notes on buffers
 *  m_backbuff holds the changed pixels to send to the client
 *  m_mainbuff is a copy of the screen pixels
 *  GrabRects and GrabRegion moves the pixels from the screen to the m_mainbuff
 *  ClearRects and CheckRects moves the pixels from the m_mainbuff to the m_backbuff
 *  ClearRects moves all pixels in the rect, CheckRects only moves pixels if some in the rect have changed.
 *  Encode and send works from the m_backbuff to the client socket buffer.
 *  CopyRect copies from one area of the backbuff to another and then fills in the source rect in the backbuff
 *
 */


#include "stdhdrs.h"
#include "vncRegion.h"
#include "rectlist.h"
#include "vncClient.h"
#include "resource.h"
#include "sharedapp.h"

extern HINSTANCE	hAppInstance;

SharedAppVnc::SharedAppVnc(vncServer *_server)
{
  bEnabled = TRUE;
  bIncludeDialogWindows = TRUE;
  server=_server;
  lastClient=NULL;
  hViewerProcess=NULL;
}


SharedAppVnc::~SharedAppVnc()
{
	StopViewer();
}



bool SharedAppVnc::StartViewer()
{
	bool result = false;
	
	if (!hViewerProcess)
	{
		STARTUPINFO startinfo;
		PROCESS_INFORMATION procinfo;
		BOOL is_success;
		char* viewerCmd = GetViewerCommand();

		if (viewerCmd)
		{
			memset(&startinfo, 0, sizeof(startinfo));
			startinfo.cb=sizeof(STARTUPINFO);

			is_success = CreateProcess(
				NULL, 
				viewerCmd, 
				NULL, 
				NULL,
				FALSE,
				CREATE_NO_WINDOW,
				NULL,
				NULL, 
				&startinfo,
				&procinfo);

			if (is_success) 
			{ 
				hViewerProcess = procinfo.hProcess;
				//CloseHandle(procinfo.hThread);
				vnclog.Print(LL_SHAREDAPP, "Viewer Started\n"); 
				result = true;
			}
		}
	} else {
		result = true;
	}
		
	return result;
}

bool SharedAppVnc::StopViewer()
{
	if (hViewerProcess)
	{
		TerminateProcess(hViewerProcess, 0);
		CloseHandle(hViewerProcess);
		hViewerProcess = NULL;
		return TRUE;
	}
	return FALSE;
}


char* SharedAppVnc::GetViewerCommand()
{
	static char viewerCmd[256];
	static bool bCommandSet = false;

	if (! bCommandSet) 
	{
		char *viewerFile;
		int byteOffset;

		strcpy(viewerCmd, "java -jar ");
		byteOffset = strlen(viewerCmd);
		viewerFile = viewerCmd + byteOffset;

		// First get the full path of the application (C:\DIR\DIR\APP.EXE)
		GetModuleFileName(hAppInstance, viewerFile, sizeof(viewerCmd)- byteOffset);

		// Then trim off the program name leaving just the path to the directory (C:\DIR\DIR\)
		char *p = viewerFile;
		while(strchr(p,'\\')) {  // while there are more separators
			p = strchr(p,'\\');   // point to next separator
			p++;                  // point to char after last separator
		}
		*p = '\0';               // terminate string
		// viewerFile now has the application's directory
		strcat(viewerFile, "SharedAppViewer.jar");

		//Check if file exists
		HANDLE hFile;
		hFile = CreateFile( viewerFile, GENERIC_WRITE, 0, NULL, OPEN_ALWAYS, NULL, NULL);
		if (hFile==INVALID_HANDLE_VALUE) 
		{
			if (GetLastError() == 32)
			{
				vnclog.Print(LL_SHAREDAPP, "SharedAppViewer is already running in the background\n");
			} else {
				vnclog.Print(0, "GetViewer: CreateFile Failed %d\n", GetLastError());
			}
			return NULL;
		}
		if (GetFileSize(hFile, NULL) > 0)
		{
			bCommandSet = true;
		} else {
			// need to unpack jar from resources and write to file
			HRSRC resource;
			HGLOBAL hResource;
			char *resourcePtr;
			int resourceSize;
			DWORD bytesWritten;

			// Find the resource here
			if (resource = FindResource(NULL,
				MAKEINTRESOURCE(IDR_SHAREDAPP_VIEWER_JAR),
				"JavaArchive"))
			{
				// Get its size
				resourceSize = SizeofResource(NULL, resource);

				// Load the resource
				if (hResource = LoadResource(NULL, resource))
				{
					// Lock the resource
					if (resourcePtr = (char *)LockResource(hResource))
					{
						// Write Jar to file
						if (WriteFile(hFile, resourcePtr, resourceSize, &bytesWritten, NULL) && bytesWritten == resourceSize)
						{
							bCommandSet = true;
						}
					}
				}
			}
		}

		CloseHandle(hFile);
	}

	if (bCommandSet)
	{
		return viewerCmd;
	} else {
		return NULL;
	}
}

char* SharedAppVnc::GetLastClient()
{
	return lastClient;
}

void SharedAppVnc::SetLastClient(char* client)
{
	if (lastClient) free(lastClient);
	lastClient = strdup(client);
}

void SharedAppVnc::SetClientsNeedUpdate()
{
	vncClientList::iterator i;
	vncClientList clients = server->ClientList();

	// Post this update to all the connected clients
	for (i = clients.begin(); i != clients.end(); i++)
	{
		vncClient *client = server->GetClient(*i);
		omni_mutex_lock l(client->m_regionLock);
		client->m_full_rgn.AddRect(client->m_fullscreen);
		//client->m_incr_rgn.AddRect(client->m_fullscreen);
		//client->m_changed_rgn.AddRect(client->m_fullscreen);
		client->m_updatewanted = TRUE;
	}
}

void SharedAppVnc::AddWindow(HWND winHwnd, HWND parentHwnd)
{
	WindowList::iterator windowIter;

	// Check if this window is already added
	{ 	omni_mutex_lock l(m_sharedAppLock);
		for	(windowIter	= sharedAppList.begin(); windowIter	!= sharedAppList.end();	windowIter++)
		{
			if (*windowIter	== winHwnd)
			{
				// already here
				return;
			}
		}
		sharedAppList.push_back(winHwnd);
	}

	// Notify clients - in case a window is off the main screen. 
	// Note we'd also need to notify when a client first connects.
	/*
	vncClientList::iterator i;
	vncClientList clients = server->ClientList();
	// Post this update to all the connected clients
	for (i = clients.begin(); i != clients.end(); i++)
	{
		// Post the update
		vncClient *client = server->GetClient(*i);
		omni_mutex_lock l(client->m_regionLock);
		RfbSendWindowOpen(client, winHwnd);
	}
	*/

	SetClientsNeedUpdate();
	return;
}


void SharedAppVnc::RemoveWindow( HWND winHwnd, bool bUpdateList )
{
	vncClientList::iterator i;
	vncClientList clients = server->ClientList();

	// Post this update to all the connected clients
	for (i = clients.begin(); i != clients.end(); i++)
	{
		// Post the update
		vncClient *client = server->GetClient(*i);
		omni_mutex_lock l(client->m_regionLock);
		RfbSendWindowClose(client, winHwnd);
	}


	{ omni_mutex_lock l(m_sharedAppLock);
		if (bUpdateList) sharedAppList.remove(winHwnd);
	}

	return;
}


void SharedAppVnc::RemoveAllWindows(bool bUpdateList) 
{
	vncClientList::iterator i;
	vncClientList clients = server->ClientList();
	
	// Post this update to all the connected clients
	for (i = clients.begin(); i != clients.end(); i++)
	{
		WindowList::iterator windowIter;
		vncClient *client = server->GetClient(*i);
		omni_mutex_lock l2(client->m_regionLock);
		omni_mutex_lock l3(m_sharedAppLock);
		for (windowIter = sharedAppList.begin(); windowIter != sharedAppList.end(); windowIter++)
		{
			HWND winHwnd = *windowIter;
			RfbSendWindowClose(client, winHwnd);
		}

		// remove full screen if exists
		RfbSendWindowClose(client, 0);
	}


	{ omni_mutex_lock l4(m_sharedAppLock);
		if (bUpdateList) sharedAppList.clear();
	}
}


void SharedAppVnc::GetVisibleRegion(HWND winHwnd, vncRegion& visibleRegionPtr)
{
	// loop through all windows - union the screen locations of windows that  are above this window
	// subtract union from this window's screen space.
	vncRegion unionRegion;
	//int windowCount;
	HWND iterHwnd;

	unionRegion.Clear();
	iterHwnd = GetForegroundWindow();
	while(iterHwnd)
	{
		RECT winRect;
		HWND tempHwnd;

		GetWindowRect(iterHwnd, &winRect);

		if (iterHwnd != winHwnd)
		{
			// union all higher z-order windows
			unionRegion.AddRect(winRect);
		} else {
			// Result is winRegion minus higher z-order windows
			visibleRegionPtr.Clear();
			visibleRegionPtr.AddRect(winRect);
			visibleRegionPtr.Subtract(unionRegion);
			break;
		}

		tempHwnd = GetNextWindow(iterHwnd, GW_HWNDNEXT);
		// just-in-case: MSDN mentions potential infinite loops when z-order is changing
		if (tempHwnd == iterHwnd) break;
		else iterHwnd = tempHwnd;
	}

	return;
}



BOOL SharedAppVnc::RfbSendWindowClose( vncClient* client, HWND winHwnd )
{
	rfbSharedAppUpdateMsg header;

	memset( &header, 0, sz_rfbSharedAppUpdateMsg);
	header.win_id = Swap32IfLE((int)winHwnd);

	SHAREDAPP_TRACE1("trace(%d), Message %d\n", nTrace++, rfbSharedAppUpdate);

	return client->SendRFBMsg(rfbSharedAppUpdate, (BYTE *) &header, sz_rfbSharedAppUpdateMsg);
}


BOOL SharedAppVnc::RfbSendWindowOpen( vncClient* client, HWND winHwnd )
{
	rfbSharedAppUpdateMsg header;
	RECT winRect;
				
	GetWindowRect(winHwnd, &winRect);
	if (winRect.left < 0) winRect.left=0;
	if (winRect.top < 0) winRect.top=0;
	if (winRect.bottom < 0) winRect.bottom=0;
	if (winRect.right < 0) winRect.right=0;

	header.type = rfbSharedAppUpdate;
	header.win_id = Swap32IfLE((int)winHwnd);
	header.parent_id = Swap32IfLE(NULL);
	header.win_rect.x = Swap16IfLE(winRect.left);
	header.win_rect.y = Swap16IfLE(winRect.top);
	header.win_rect.w = Swap16IfLE(winRect.right-winRect.left);
	header.win_rect.h = Swap16IfLE(winRect.bottom-winRect.top);
	header.cursorOffsetX = Swap16IfLE(winRect.left);
	header.cursorOffsetY = Swap16IfLE(winRect.top);
	header.nRects = Swap16IfLE(0);

	SHAREDAPP_TRACE2("TRACE(%d): Message %d   nRects %x\n", nTrace++, rfbSharedAppUpdate, 0);
	if (!client->SendRFBMsg(rfbSharedAppUpdate, (BYTE *) &header, sz_rfbSharedAppUpdateMsg))
	{
		vnclog.Print(0, "FAILURE SendUpdates: SendRFBMsg Failed.\n");
		return FALSE;
	}

	return TRUE;
}


/*
* sharedapp_RfbSendUpdates - send the currently pending window updates to
* the RFB client.
*/

int SharedAppVnc::SendUpdates(vncClient* client)
{
	WindowList closedWindowList;
	WindowList::iterator windowIter;
	int numUpdatesSent = 0;

	if (sharedAppList.empty()) return FALSE;

	// Prepare to send cursor position update if necessary
	if (client->m_cursor_pos_changed) {
		POINT cursor_pos;
		if (!GetCursorPos(&cursor_pos)) {
			cursor_pos.x = 0;
			cursor_pos.y = 0;
		}
		if (cursor_pos.x == client->m_cursor_pos.x && cursor_pos.y == client->m_cursor_pos.y) {
			client->m_cursor_pos_changed = FALSE;
		} else {
			RECT mouseRect = {
				client->m_cursor_pos.x, client->m_cursor_pos.y, 
				client->m_cursor_pos.x + 32, //GetSystemMetrics(SM_CXCURSOR),
				client->m_cursor_pos.y + 32 }; //GetSystemMetrics(SM_CYCURSOR) };
			
			client->m_oldMouseRegions.AddRect(mouseRect);

			client->m_cursor_pos.x = cursor_pos.x;
			client->m_cursor_pos.y = cursor_pos.y;
		}
	}
	
	// If there is nothing to send then exit
	if (client->m_changed_rgn.IsEmpty() &&
		client->m_full_rgn.IsEmpty() &&
		!client->m_copyrect_set &&
		!client->m_cursor_update_pending &&
		!client->m_cursor_pos_changed)
		return FALSE;

	// We currently don't handle copyrect with shared app
	// So combine copyrect area into changed region.
	if (client->m_copyrect_set)
	{
		int src_x = client->m_copyrect_src.x;
		int src_y = client->m_copyrect_src.y;
		int src_width = client->m_copyrect_rect.right - client->m_copyrect_rect.left;
		int src_height = client->m_copyrect_rect.bottom - client->m_copyrect_rect.top;
		RECT copyrect_src = {src_x, src_y, src_x + src_width, src_y + src_height};

		client->m_changed_rgn.AddRect(client->m_copyrect_rect);
		client->m_changed_rgn.AddRect(copyrect_src);

		client->m_copyrect_set = FALSE;
	}


	{ 	
		omni_mutex_lock l(m_sharedAppLock);

		// Loop through shared windows sending updates
		for (windowIter = sharedAppList.begin(); windowIter != sharedAppList.end(); windowIter++)
		{
			HWND winHwnd = *windowIter;
			vncRegion fullScreenRegion;
			vncRegion winRegion;
			vncRegion mouseRegion;
			vncRegion grabRegion;
			vncRegion toBeSent;			// Region to actually be sent
			rectlist toBeSentList;		// List of rectangles to actually send	
			RECT winRect;
			BOOL bCursorInWindow;


			if (!IsWindow(winHwnd))
			{
				closedWindowList.push_back(winHwnd);
				continue;
			}

			GetWindowRect(winHwnd, &winRect);
			if (winRect.left < 0) winRect.left=0;
			if (winRect.top < 0) winRect.top=0;

#ifdef DONT_OCCLUDE
			winRegion.Clear();
			winRegion.AddRect(winRect);
#else
			GetVisibleRegion(winHwnd, winRegion);
#endif

			// Restrict to fullscreen dimensions
			fullScreenRegion.Clear();
			fullScreenRegion.AddRect(client->m_fullscreen);
			winRegion.Intersect(fullScreenRegion);
			if (winRegion.IsEmpty()) continue;

			//bCursorInWindow = PtInRect(&winRect, client->m_cursor_pos);
			bCursorInWindow = winRegion.ContainsPoint(client->m_cursor_pos);

			mouseRegion.Clear();
			mouseRegion.Combine(client->m_oldMouseRegions);
			mouseRegion.Intersect(winRegion);

			// First we need to grab the pixels from the screen to the mainbuff
			grabRegion.Clear();
            if (!client->m_full_rgn.IsEmpty())
			{
				grabRegion.Combine(client->m_full_rgn);
				grabRegion.Intersect(winRegion);
			} else {
				grabRegion.Combine(client->m_incr_rgn);
				grabRegion.Intersect(client->m_changed_rgn);
				grabRegion.Intersect(winRegion);
			}
			grabRegion.Combine(mouseRegion);

			if (grabRegion.IsEmpty()) continue;

			// This is where the pixels get copied from the screen to the main buffer (GrabRegion GrabRect)
			client->GrabRegion(grabRegion);

			// Then we need to copy the requested pixels to the backbuff to be sent
			toBeSent.Clear();
			if (!client->m_full_rgn.IsEmpty())
			{
				// SEND WHOLE REGION
				rectlist rectsToClear;

				// Retrieve and clear the rectangles
				if (grabRegion.Rectangles(rectsToClear))
					client->ClearRects(toBeSent, rectsToClear);
			} else {
				// SCAN INCREMENTAL REGIONS FOR CHANGES
				rectlist rectsToScan;

				// Retrieve and scan the rectangles
				if (grabRegion.Rectangles(rectsToScan))
					client->CheckRects(toBeSent, rectsToScan);

				// Retrieve and clear the mouse region rectangles
				if (mouseRegion.Rectangles(rectsToScan))
					client->ClearRects(toBeSent, rectsToScan);
			}
			
			// Clear the bits we're about to deal with from the changed region
			client->m_changed_rgn.Subtract(toBeSent);
			client->m_oldMouseRegions.Subtract(mouseRegion);

			// Get the list of changed rectangles!
			int numrects = 0;
			if (toBeSent.Rectangles(toBeSentList))
			{
				// Find out how many rectangles this update will contain
				rectlist::iterator i;
				int numsubrects;
				for (i=toBeSentList.begin(); i != toBeSentList.end(); i++)
				{
					numsubrects = client->m_buffer->GetNumCodedRects(*i);

					// Skip rest rectangles if an encoder will use LastRect extension.
					if (numsubrects == 0) {
						numrects = 0xFFFF;
						break;
					}
					numrects += numsubrects;
				}
			}

			if (numrects != 0xFFFF) {
				// Count cursor shape and cursor position updates.
				if (client->m_cursor_update_pending && bCursorInWindow)
					numrects++;
				if (client->m_cursor_pos_changed && bCursorInWindow)
					numrects++;
				// If there are no rectangles then continue
				if (numrects == 0)
					continue; //return FALSE;
			}



			// Otherwise, send <number of rectangles> header
			rfbSharedAppUpdateMsg header;
			header.type = rfbSharedAppUpdate;
			header.win_id = Swap32IfLE((int)winHwnd);
			header.parent_id = Swap32IfLE(NULL);
			header.win_rect.x = Swap16IfLE(winRect.left);
			header.win_rect.y = Swap16IfLE(winRect.top);
			header.win_rect.w = Swap16IfLE(winRect.right-winRect.left);
			header.win_rect.h = Swap16IfLE(winRect.bottom-winRect.top);
			header.cursorOffsetX = Swap16IfLE(winRect.left);
			header.cursorOffsetY = Swap16IfLE(winRect.top);
			header.nRects = Swap16IfLE(numrects);

			numUpdatesSent++;

			SHAREDAPP_TRACE2("TRACE(%d): Message %d   nRects %x\n", nTrace++, rfbSharedAppUpdate, numrects);
			if (!client->SendRFBMsg(rfbSharedAppUpdate, (BYTE *) &header, sz_rfbSharedAppUpdateMsg))
			{
				vnclog.Print(0, "FAILURE SendUpdates: SendRFBMsg Failed.\n");
				break; // return TRUE;
			}

			// Send mouse cursor shape update
			if (bCursorInWindow && client->m_cursor_update_pending) {
				if (!client->SendCursorShapeUpdate())
				{
					vnclog.Print(0, "FAILURE SendUpdates: SendCursorShapeUpdate Failed.\n");
					break; // return TRUE;
				}
			}

			// Send cursor position update
			if (bCursorInWindow && client->m_cursor_pos_changed) {
				if (!client->SendCursorPosUpdate())
				{
					vnclog.Print(0, "FAILURE SendUpdates: SendCursorPosUpdate Failed.\n");
					break; // return TRUE;
				}
			}

			// Encode & send the actual rectangles
			if (!client->SendRectangles(toBeSentList))
			{
				vnclog.Print(0, "FAILURE SendUpdates: SendRectangles Failed.\n");
				break; // return TRUE;
			}

			// Send LastRect marker if needed.
			if (numrects == 0xFFFF) {
				if (!client->SendLastRect())
				{
					vnclog.Print(0, "FAILURE SendUpdates: SendLastRect Failed.\n");
					break; // return TRUE;
				}
			}

			// Both lists should be empty when we're done
			_ASSERT(toBeSentList.empty());
		}
	}

	for (windowIter = closedWindowList.begin(); windowIter != closedWindowList.end(); windowIter++)
	{
		HWND winHwnd = *windowIter;
		RemoveWindow(winHwnd, true);
	}

	if (numUpdatesSent > 0)
	{
		client->m_full_rgn.Clear();
		client->m_incr_rgn.Clear();
		
		return TRUE;
	}
	
	return FALSE;
}

BOOL SharedAppVnc::CheckPointer(POINT pt, HWND pointerWindow)
{
	WindowList::iterator windowIter;

	omni_mutex_lock l(m_sharedAppLock);

	// Loop through shared windows sending updates
	for (windowIter = sharedAppList.begin(); windowIter != sharedAppList.end(); windowIter++)
	{
		HWND win = *windowIter;

		if (win == pointerWindow)
		{
			vncRegion winRegion;
			winRegion.Clear();
			GetVisibleRegion(win, winRegion);
			if (winRegion.ContainsPoint(pt)) return TRUE;
			else return FALSE;
		}
	}

	return FALSE;
}


//void sharedapp_InvalidateWindowRegion(ScreenPtr pScreen, unsigned int winId)
//{
//  RegionRec invalidateRegion;
//  WindowPtr winPtr;
//
//  if (winId)
//  {
//    winPtr = sharedapp_FindWindow(WindowTable[pScreen->myNum], winId);
//    if (!winPtr) return;
//    rfbLog("SHAREDAPP - UpdateFB winId");
//    REGION_NULL(pScreen,&invalidateRegion);
//    REGION_COPY(pScreen, &invalidateRegion, &winPtr->borderClip);
//  } else {
//    BoxRec box;
//    box.x1 = 0;
//    box.y1 = 0;
//    box.x2 = pScreen->width;
//    box.y2 = pScreen->height;
//    REGION_INIT(pScreen, &invalidateRegion, &box, 0);
//    rfbLog("SHAREDAPP - UpdateFB All");
//  }
//  rfbInvalidateRegion(pScreen, &invalidateRegion);
//
//  REGION_UNINIT(pScreen, &invalidateRegion);
//  return;
//}





