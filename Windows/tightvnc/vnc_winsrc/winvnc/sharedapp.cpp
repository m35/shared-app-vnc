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


//typedef struct _SharedWindow {
//  HWND windowId;
//  HWND parentId;
//  RegionRec blackOutRegion;  /*region which is obscured by other windows */
//} SharedWindow, *SharedWindowPtr;

#include "stdhdrs.h"
#include "vncRegion.h"
#include "rectlist.h"
#include "sharedapp.h"
#include "vncWinList.h"
#include "vncClient.h"


SharedAppVnc::SharedAppVnc()
{
  bEnabled = TRUE;
  bOn = TRUE;
  bIncludeDialogWindows = TRUE;
}


SharedAppVnc::~SharedAppVnc()
{
}


/*
 * sharedapp_RfbSendUpdates - send the currently pending window updates to
 * the RFB client.
 */

BOOL SharedAppVnc::SendUpdates(vncClient* client)
{
	vncRegion toBeSent;			// Region to actually be sent
	rectlist toBeSentList;		// List of rectangles to actually send
	vncRegion toBeDone;			// Region to check
	vncRegion winRegion;
	SharedAppList::iterator sharedAppIter;

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
	client->m_changed_rgn.AddRect(client->m_copyrect_rect);
	client->m_copyrect_set = FALSE;


	// GRAB THE SCREEN DATA

	// Get the region to be scanned and potentially sent
	toBeDone.Clear();
	toBeDone.Combine(client->m_incr_rgn);
	toBeDone.Subtract(client->m_full_rgn);
	toBeDone.Intersect(client->m_changed_rgn);

	// Get the region to grab
	vncRegion toBeGrabbed;
	toBeGrabbed.Clear();
	toBeGrabbed.Combine(client->m_full_rgn);
	toBeGrabbed.Combine(toBeDone);
	client->GrabRegion(toBeGrabbed);

	// CLEAR REGIONS THAT WON'T BE SCANNED

	// Get the region to definitely be sent
	toBeSent.Clear();
	if (!client->m_full_rgn.IsEmpty())
	{
		rectlist rectsToClear;

		// Retrieve and clear the rectangles
		if (client->m_full_rgn.Rectangles(rectsToClear))
			client->ClearRects(toBeSent, rectsToClear);
	}

	// SCAN INCREMENTAL REGIONS FOR CHANGES

	if (!toBeDone.IsEmpty())
	{
		rectlist rectsToScan;

		// Retrieve and scan the rectangles
		if (toBeDone.Rectangles(rectsToScan))
			client->CheckRects(toBeSent, rectsToScan);
	}

	// CLEAN UP THE MAIN REGIONS

	// Clear the bits we're about to deal with from the changed region
	client->m_changed_rgn.Subtract(client->m_incr_rgn);
	client->m_changed_rgn.Subtract(client->m_full_rgn);

	// Clear the full & incremental regions, since we've dealt with them
	if (!toBeSent.IsEmpty())
	{
		client->m_full_rgn.Clear();
		client->m_incr_rgn.Clear();
	}


	// Loop through shared windows sending updates
	for (sharedAppIter = sharedAppList.begin(); sharedAppIter != sharedAppList.end(); sharedAppIter++)
	{
		HWND winHwnd = *sharedAppIter;
		RECT winRect;

		if (!IsWindow(winHwnd))
		{
			RemoveWindow(winHwnd);
		}

		GetWindowRect(winHwnd, &winRect);
		winRegion.AddRect(winRect);
		winRegion.Intersect(toBeSent);

		if (!client->m_cursor_update_sent && !client->m_cursor_update_pending) {
			if (!client->m_mousemoved) {
				vncRegion tmpMouseRgn;
				tmpMouseRgn.AddRect(client->m_oldmousepos);
				tmpMouseRgn.Intersect(toBeSent);
				if (!tmpMouseRgn.IsEmpty()) {
					client->m_mousemoved = true;
				}
			}
			if (client->m_mousemoved)
			{
				// Grab the mouse
				client->m_oldmousepos = client->m_buffer->GrabMouse();
				if (IntersectRect(&client->m_oldmousepos, &client->m_oldmousepos, &client->m_fullscreen))
					client->m_buffer->GetChangedRegion(toBeSent, client->m_oldmousepos);

				client->m_mousemoved = FALSE;
			}
		}



		// Get the list of changed rectangles!
		int numrects = 0;
		if (winRegion.Rectangles(toBeSentList))
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
			if (client->m_cursor_update_pending)
				numrects++;
			if (client->m_cursor_pos_changed)
				numrects++;
			// Count the copyrect region
			if (client->m_copyrect_set)
				numrects++;
			// If there are no rectangles then return
			if (numrects == 0)
				return FALSE;
		}

		// Otherwise, send <number of rectangles> header
		rfbSharedAppUpdateMsg header;
		header.win_id = Swap32IfLE((int)winHwnd);
        header.parent_id = Swap32IfLE(NULL);
        header.win_rect.x = Swap16IfLE(winRect.left);
        header.win_rect.y = Swap16IfLE(winRect.top);
        header.win_rect.w = Swap16IfLE(winRect.right-winRect.left);
        header.win_rect.h = Swap16IfLE(winRect.bottom-winRect.top);
		header.nRects = Swap16IfLE(numrects);
		if (!client->SendRFBMsg(rfbSharedAppUpdate, (BYTE *) &header, sz_rfbSharedAppUpdateMsg))
			return TRUE;

		// Send mouse cursor shape update
		if (client->m_cursor_update_pending) {
			if (!client->SendCursorShapeUpdate())
				return TRUE;
		}

		// Send cursor position update
		if (client->m_cursor_pos_changed) {
			if (!client->SendCursorPosUpdate())
				return TRUE;
		}

		// Encode & send the copyrect
		if (client->m_copyrect_set) {
			client->m_copyrect_set = FALSE;
			if(!client->SendCopyRect(client->m_copyrect_rect, client->m_copyrect_src))
				return TRUE;
		}

		// Encode & send the actual rectangles
		if (!client->SendRectangles(toBeSentList))
			return TRUE;

		// Send LastRect marker if needed.
		if (numrects == 0xFFFF) {
			if (!client->SendLastRect())
				return TRUE;
		}

		// Both lists should be empty when we're done
		_ASSERT(toBeSentList.empty());
	}

	return TRUE;
}


void SharedAppVnc::AddWindow(HWND winHwnd, HWND parentHwnd)
{
	SharedAppList::iterator sharedAppIter;

	// Check if this window is already added
	for (sharedAppIter = sharedAppList.begin(); sharedAppIter != sharedAppList.end(); sharedAppIter++)
	{
		if (*sharedAppIter == winHwnd)
		{
			// already here
			return;
		}
	}
	sharedAppList.push_back(winHwnd);
	return;
}


void SharedAppVnc::RemoveWindow( HWND winHwnd )
{
	sharedAppList.remove(winHwnd);
	// Note if winHwnd has children, must remove them also
	// send window close message
	return;
}


void SharedAppVnc::RemoveAllWindows() 
{

	sharedAppList.clear();
}


BOOL SharedAppVnc::RfbSendWindowClose( vncClient* client, HWND winHwnd )
{
  rfbSharedAppUpdateMsg header;

  memset( &header, 0, sz_rfbSharedAppUpdateMsg);
  header.win_id = Swap32IfLE((int)winHwnd);

  return client->SendRFBMsg(rfbSharedAppUpdate, (BYTE *) &header, sz_rfbSharedAppUpdateMsg);
}


//void sharedapp_InitReverseConnection(ScreenPtr pScreen)
//{
//  VNCSCREENPTR(pScreen);
//  SharedAppVnc *shapp = &pVNC->sharedApp;
//  rfbClientPtr cl;
//  char *host, *pos;
//  int port = 5500;
//
//  if (shapp->reverseConnectionHost == NULL)
//    return;
//
//  host = shapp->reverseConnectionHost;
//
//  pos = strchr(host, ':');
//  if (pos)
//  {
//    *pos = 0;
//    port = atoi (pos+1);
//  }
//
//  rfbLog("sharedApp_ReverseConnection %s:%d\n", host, port);
//  
//  cl = rfbReverseConnection (pScreen, host, port);
//
//  free(shapp->reverseConnectionHost);
//  shapp->reverseConnectionHost=NULL;
//}

//Bool
//sharedapp_RfbSendRectBlackOut (cl, x, y, w, h)
//     rfbClientPtr cl;
//     int x, y, w, h;
//{
//  VNCSCREENPTR (cl->pScreen);
//  rfbFramebufferUpdateRectHeader rect;
//
//  if (pVNC->ublen + sz_rfbFramebufferUpdateRectHeader > UPDATE_BUF_SIZE)
//  {
//    if (!rfbSendUpdateBuf (cl))
//      return FALSE;
//  }
//
//  rect.encoding = Swap32IfLE (rfbEncodingBlackOut);
//  rect.r.x = Swap16IfLE (x);
//  rect.r.y = Swap16IfLE (y);
//  rect.r.w = Swap16IfLE (w);
//  rect.r.h = Swap16IfLE (h);
//
//  memcpy (&pVNC->updateBuf[pVNC->ublen], (char *) &rect,
//          sz_rfbFramebufferUpdateRectHeader);
//  pVNC->ublen += sz_rfbFramebufferUpdateRectHeader;
//
//  return TRUE;
//}

//void sharedapp_RemoveDialogWindows(rfbClientPtr cl, List *winlist) 
//{
//  SharedWindowPtr shwin;
//  int sharedAppCount, i;
//
//  sharedAppCount = List_Count(winlist);
//  for( i=sharedAppCount-1; i>=0; i-- )
//  {
//    shwin = (SharedWindowPtr)List_Element(winlist, i);
//    if (shwin->parentId != 0)
//    {
//      /* This is a dialog window since it has a parent, remove this window */
//      List_Add(closeWindowList, shwin);
//    }
//  }
//}

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


//Bool sharedapp_GetDialogWindows(rfbClientPtr cl, SharedWindowPtr shwin)
//{
//  VNCSCREENPTR(cl->pScreen);
//  SharedAppVnc *shapp = &pVNC->sharedApp;
//  WindowPtr pWin, pChild;
//  unsigned int parentMask; 
//
//  pWin = WindowTable[cl->pScreen->myNum];
//
//  
//  if (shwin && shwin->windowPtr) 
//    parentMask = shwin->windowPtr->parent->drawable.id & ID_MASK;
//  else return FALSE;
//  
//
//  for (pChild = pWin->firstChild; pChild; pChild = pChild->nextSib)
//  {
//    if ((pChild->drawable.id & ID_MASK) == parentMask)
//    {
//      if (pChild->firstChild)
//      {
//        pWin = pChild->firstChild;
//        if (pWin->drawable.id == shwin->windowId) 
//        {
//          /* this is the same window */
//          continue;
//        }
//        else if (pWin->mapped)
//        {
//          if ((pWin->drawable.id & ID_MASK) == (shwin->windowId & ID_MASK))
//          {
//            /* this is a dialog window - add it to our list of shared windows */
//            /*rfbLog("adding dialog window 0x%x parent 0x%x\n", pWin->drawable.id, shwin->windowId);*/
//            sharedapp_AddWindow(cl, &shapp->sharedAppList, pWin->drawable.id, shwin->windowId);
//
//          }
//        }
//      }
//    }
//  }
//
//  return TRUE;
//}


//void sharedapp_GetMenuWindows(ScreenPtr pScreen, SharedWindowPtr shwin, RegionPtr menuRegion, RegionPtr blkoutRegion)
//{
//  WindowPtr pWin, pChild;
//  RegionRec tmpRegion;
//  BoxRec box;
//
//  REGION_NULL(pScreen, &tmpRegion);
//  
//  pWin = WindowTable[pScreen->myNum];
//
//  for (pChild = pWin->firstChild; pChild; pChild = pChild->nextSib)
//  {
//    if (pChild->overrideRedirect && pChild->mapped)
//    {
//      if ((shwin->windowId & ID_MASK) == (pChild->drawable.id & ID_MASK))
//      {
//        REGION_INTERSECT(pScreen, &tmpRegion, &shwin->windowPtr->winSize, 
//          &pChild->winSize);
//        if (REGION_NOTEMPTY(pScreen, &tmpRegion))
//        {
//          /* Menu present */
//          int x, y;
//          x = (tmpRegion.extents.x1 + tmpRegion.extents.x2) / 2;
//          y = tmpRegion.extents.y1 - 1;
//
//          if (POINT_IN_REGION(pScreen, &shwin->windowPtr->winSize, x, y, &box) &&
//              !POINT_IN_REGION(pScreen, blkoutRegion, x, y, &box))
//          {
//            /* top level window - add menus */
//            REGION_UNION(pScreen, menuRegion, menuRegion, &pChild->borderClip);
//            REGION_SUBTRACT(pScreen, blkoutRegion, blkoutRegion, &pChild->borderClip);
//          }
//        }
//      }
//    }
//  }
//  REGION_UNINIT(pScreen, &tmpRegion);
//}


//Bool sharedapp_CheckPointer(rfbClientPtr cl, rfbPointerEventMsg *pe)
//{
//  VNCSCREENPTR(cl->pScreen);
//  SharedAppVnc *shapp = &pVNC->sharedApp;
//  SharedWindowPtr shwin;
//  unsigned int winId;
//  BoxRec box;
//  int listIndex;
//  int x,y;
//
//  /* Remove This */
//  if (!cl->supportsSharedAppEncoding) return TRUE;
//
//  winId = Swap32IfLE (pe->windowId);
//  x = (int) Swap16IfLE (pe->x);
//  y = (int) Swap16IfLE (pe->y);
//
//  /*rfbLog("CheckPointer looking for window 0x%x\n", winId);*/
//
//  for( listIndex=0;
//       (shwin=(SharedWindowPtr)List_Element(&shapp->sharedAppList, listIndex)) != NULL;
//       listIndex++)
//  {
//    if (shwin->windowId == winId)
//    {
//      if (!POINT_IN_REGION(cl->pScreen, &shwin->blackOutRegion, x, y, &box))
//      {
//        if (shwin->windowPtr)
//        {
//          if (POINT_IN_REGION(cl->pScreen, &shwin->windowPtr->winSize, x, y, &box))
//          {
//            /*rfbLog("Pointer in region");*/
//            return TRUE;
//          } else {
//            /*rfbLog("Pointer NOT in region");*/
//          }
//        } else {
//          /*rfbLog("Pointer no winPtr");*/
//          return TRUE;
//        }
//      } else {
//        /*rfbLog("Pointer in blackout");*/
//      }
//    }
//  }
//
//  /*rfbLog("CheckPointer FALSE");*/
//  return FALSE;
//}

//Bool
//sharedapp_RfbGetCursorPos(pScreen, x, y)
//  ScreenPtr pScreen;
//  int *x;
//  int *y;
//{   
//#if XFREE86VNC
//  ScreenPtr   pCursorScreen = miPointerCurrentScreen();
//#endif
//  int _x, _y;
//                      
//#if XFREE86VNC
//  if (pScreen == pCursorScreen) 
//    miPointerPosition(&_x, &_y);
//#else   
//  rfbSpriteGetCursorPos(pScreen, &_x, &_y);
//#endif  
//                                  
//  *x=_x;
//  *y=_y;
//  return TRUE;
//}

