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
#include "rfb.h"
#include "mipointer.h"
#include "sprite.h"
#include "sharedapp.h"

extern Bool rfbSendCopyRegion (rfbClientPtr cl, RegionPtr reg, int dx, int dy);
extern Bool rfbSendLastRectMarker (rfbClientPtr cl);

typedef struct _SharedWindow {
  unsigned int windowId;
  unsigned int parentId;
  WindowPtr windowPtr;
  RegionRec blackOutRegion;  /*region which is obscured by other windows */
} SharedWindow, *SharedWindowPtr;


static void sharedapp_AddWindow(rfbClientPtr cl, List *winlist, unsigned int winId, unsigned int parentId);
static void sharedapp_RemoveWindow(rfbClientPtr cl, List *winlist, unsigned int winId);
static void sharedapp_RemoveAllWindows(rfbClientPtr cl, List *winlist);
static void sharedapp_RemoveDialogWindows(rfbClientPtr cl, List *winlist);
static void sharedapp_InvalidateWindowRegion(ScreenPtr pScreen, unsigned int winId);
static void sharedapp_GetMenuWindows(ScreenPtr pScreen, SharedWindowPtr shwin, RegionPtr menuRegion, RegionPtr blkoutRegion);
static WindowPtr sharedapp_FindWindow(WindowPtr pTopWin, unsigned long winId);
static WindowPtr sharedapp_FindWindow_ExhaustiveSearch(WindowPtr pTopWin, unsigned long winId);
static Bool sharedapp_RfbGetCursorPos(ScreenPtr pScreen, int *x, int *y);
static Bool sharedapp_GetDialogWindows(rfbClientPtr cl, SharedWindowPtr shwin);
static Bool sharedapp_RfbSendWindowClose(rfbClientPtr cl, unsigned int winId);

static List *closeWindowList;

#define ID_MASK 0xfffe0000

void sharedapp_Init(SharedAppVncPtr shapp)
{
  
  shapp->bEnabled = TRUE;
  shapp->bOn = TRUE;
  shapp->bIncludeDialogWindows = TRUE;
  memset(&shapp->sharedAppList, 0, sizeof(shapp->sharedAppList));
  closeWindowList = List_New();
}


void sharedapp_HandleRequest(rfbClientPtr cl, unsigned int command, unsigned int id)
{
  VNCSCREENPTR(cl->pScreen);
  SharedAppVnc *shapp = &pVNC->sharedApp;

  /* rfbLog("rfbSharedAppRequest 0x%x 0x%x\n", command, id); */
  switch (command)
  {
  case rfbSharedAppRequestEnable:
    shapp->bEnabled = TRUE;
    sharedapp_RemoveWindow(cl, &shapp->sharedAppList, 0);
    rfbLog("SHAREDAPP Enabled\n");
    break;

  case rfbSharedAppRequestDisable:
    shapp->bEnabled = FALSE;
    /* call the root window 0 */
    sharedapp_AddWindow(cl, &shapp->sharedAppList, 0, 0);
    rfbLog("SHAREDAPP Disabled\n");
    break;

  case rfbSharedAppRequestOn:
    shapp->bEnabled = TRUE;
    shapp->bOn = TRUE;
    rfbLog("SHAREDAPP ON\n");
    break;

  case rfbSharedAppRequestOff:
    shapp->bEnabled = TRUE;
    shapp->bOn = FALSE;
    rfbLog("SHAREDAPP OFF\n");
    break;
  
  case rfbSharedAppRequestShow:
    if (id != 0)
    {
      sharedapp_AddWindow(cl, &shapp->sharedAppList, id, 0);
      rfbLog("SHAREDAPP Show 0x%x\n", id);
    }
    break;

  case rfbSharedAppRequestHide:
    if (id != 0)
    {
      sharedapp_RemoveWindow(cl, &shapp->sharedAppList, id);
      rfbLog("SHAREDAPP Hide 0x%x\n", id);
    }
    break;

  case rfbSharedAppRequestIncludeDialogs:
    shapp->bIncludeDialogWindows = TRUE;
    break;

  case rfbSharedAppRequestExcludeDialogs:
    shapp->bIncludeDialogWindows = FALSE;
    sharedapp_RemoveDialogWindows(cl, &shapp->sharedAppList);
    break;

  case rfbSharedAppRequestHideAll:
    sharedapp_RemoveAllWindows(cl, &shapp->sharedAppList);
    rfbLog("SHAREDAPP Hide All 0x%x\n", id);
    break;

  case rfbSharedAppReverseConnection:
  {
    int hostLen = id;
    int n;
    if (id > 0 && id < 256)
    {
      shapp->reverseConnectionHost = xalloc(hostLen + 1);
      if ((n = ReadExact(cl->sock, shapp->reverseConnectionHost, hostLen)) <= 0)
      {
        if (n != 0)
          rfbLogPerror("rfbSharedAppReverseConnection: read");
        rfbCloseSock(cl->pScreen, cl->sock);
        return;
      }
      shapp->reverseConnectionHost[hostLen]=0;
#if XFREE86VNC
      rfbLog("SHAREDAPP Init Reverse Connection\n", id);
      sharedapp_InitReverseConnection(cl->pScreen);
#else
      rfbLog("SHAREDAPP Schedule Reverse Connection\n", id);
#endif
    }
    break;
  }

  default:
    rfbLog("SHAREDAPP - Invalid command 0x%x\n", command);
    return;
  }

  /* Notify rfb server of updates */
  sharedapp_InvalidateWindowRegion(cl->pScreen, id);
  return;
}

void sharedapp_CheckForClosedWindows(ScreenPtr pScreen, rfbClientPtr rfbClientHead)
{
  VNCSCREENPTR(pScreen);
  SharedAppVnc *shapp = &pVNC->sharedApp;
  rfbClientPtr cl, nextCl;
  SharedWindowPtr shwin;

  while (List_Count(closeWindowList) > 0)
  {
    shwin = (SharedWindowPtr) List_Remove_Index(closeWindowList, 0);
    /* remove all instances of this from close list */
    List_Remove_Data(closeWindowList, shwin);
    rfbLog("Closing Window 0x%x\n", shwin->windowId);

    for (cl = rfbClientHead; cl; cl = nextCl)
    {
      nextCl = cl->next;

      /* check if SharedAppEncoding supported for this client */
      if (cl->supportsSharedAppEncoding)
      {
        sharedapp_RfbSendWindowClose(cl, shwin->windowId);
      }
    }
    List_Remove_Data(&shapp->sharedAppList, shwin);
    free(shwin);
  }
}

void sharedapp_InitReverseConnection(ScreenPtr pScreen)
{
  VNCSCREENPTR(pScreen);
  SharedAppVnc *shapp = &pVNC->sharedApp;
  rfbClientPtr cl;
  char *host, *pos;
  int port = 5500;

  if (shapp->reverseConnectionHost == NULL)
    return;

  host = shapp->reverseConnectionHost;

  pos = strchr(host, ':');
  if (pos)
  {
    *pos = 0;
    port = atoi (pos+1);
  }

  rfbLog("sharedApp_ReverseConnection %s:%d\n", host, port);
  
  cl = rfbReverseConnection (pScreen, host, port);

  free(shapp->reverseConnectionHost);
  shapp->reverseConnectionHost=NULL;
}


/* Use this one to only search windows reparented by the window manager */
WindowPtr sharedapp_FindWindow(WindowPtr pTopWin, unsigned long winId)
{
  WindowPtr pWin, pChild;

  for (pChild = pTopWin->firstChild; pChild; pChild = pChild->nextSib)
  {
    if (pChild->firstChild)
    {
      pWin = pChild->firstChild;
      if (pWin->drawable.id == winId) 
      {
        return pWin;
      }
    }
  }

  /*rfbLog("FindWindow: Trying Exhaustive Search\n");*/
  pWin = sharedapp_FindWindow_ExhaustiveSearch(pTopWin, winId);

  return pWin;
}


/* Use this one if you want to search every window for a match */
WindowPtr sharedapp_FindWindow_ExhaustiveSearch(WindowPtr pTopWin, unsigned long winId)
{
  WindowPtr pWin, pChild, pMatch;
  List *searchList;

  searchList = List_New();
  pMatch = NULL;

  List_Add(searchList, pTopWin);

  while (List_Count(searchList) > 0)
  {
    pWin = (WindowPtr) List_Remove_Index(searchList, 0);
    /* rfbLog("SHAREDAPP (FindWindow) searching for 0x%x in 0x%x\n", winId, pWin->drawable.id); */
    if (pWin->drawable.id == winId) 
    {
      pMatch = pWin;
      break;
    }

    /* Add all of pWin children to the list to visit */
    for (pChild = pWin->firstChild; pChild; pChild = pChild->nextSib)
    {
      List_Add(searchList, pChild);
    }
  }

  List_Delete(searchList);
  return pMatch; 
}


Bool
sharedapp_RfbSendRectBlackOut (cl, x, y, w, h)
     rfbClientPtr cl;
     int x, y, w, h;
{
  VNCSCREENPTR (cl->pScreen);
  rfbFramebufferUpdateRectHeader rect;

  if (pVNC->ublen + sz_rfbFramebufferUpdateRectHeader > UPDATE_BUF_SIZE)
  {
    if (!rfbSendUpdateBuf (cl))
      return FALSE;
  }

  rect.encoding = Swap32IfLE (rfbEncodingBlackOut);
  rect.r.x = Swap16IfLE (x);
  rect.r.y = Swap16IfLE (y);
  rect.r.w = Swap16IfLE (w);
  rect.r.h = Swap16IfLE (h);

  memcpy (&pVNC->updateBuf[pVNC->ublen], (char *) &rect,
          sz_rfbFramebufferUpdateRectHeader);
  pVNC->ublen += sz_rfbFramebufferUpdateRectHeader;

  return TRUE;
}


/*
 * sharedapp_RfbSendUpdates - send the currently pending window updates to
 * the RFB client.
 */
Bool
sharedapp_RfbSendUpdates(pScreen, cl)
    ScreenPtr pScreen;
    rfbClientPtr cl;
{
    VNCSCREENPTR(pScreen);
    SharedAppVnc *shapp = &pVNC->sharedApp;
    int x1, x2, y1, y2;
    int listIndex, i, dx, dy;
    int nRects;
    SharedWindowPtr shwin;
    int nUpdateRegionRects;
    rfbSharedAppUpdateMsg *updateMsg;
    RegionRec winRegion, updateRegion, updateCopyRegion;
    RegionRec lcopyRegion, menuRegion, blkoutRegion, tmpRegion;
    BoxRec box;
    int cx, cy;
    Bool cursorInWindow = FALSE;
    Bool sendCursorShape = FALSE;
    Bool sendCursorPos = FALSE;
    Bool clearRequestedRegion = FALSE;

    /* check if SharedAppEncoding supported for this client */
    if (!cl->supportsSharedAppEncoding) return FALSE;

    /*
     * If this client understands cursor shape updates, cursor should be
     * removed from the framebuffer. Otherwise, make sure it's put up.
     */

#if !XFREE86VNC
    if (cl->enableCursorShapeUpdates) {
        if (pVNC->cursorIsDrawn)
            rfbSpriteRemoveCursor(pScreen);
        if (!pVNC->cursorIsDrawn && cl->cursorWasChanged)
            sendCursorShape = TRUE;
    } else {
        if (!pVNC->cursorIsDrawn)
            rfbSpriteRestoreCursor(pScreen);
    }
#else
    if (cl->enableCursorShapeUpdates)
        if (cl->cursorWasChanged) 
            sendCursorShape = TRUE;
#endif
    /*
     * Do we plan to send cursor position update?
     */

     if (cl->enableCursorPosUpdates && cl->cursorWasMoved)
         sendCursorPos = TRUE;


    REGION_NULL(pScreen,&lcopyRegion);
    REGION_NULL(pScreen,&winRegion);
    REGION_NULL(pScreen,&updateRegion);
    REGION_NULL(pScreen,&updateCopyRegion);
    REGION_NULL(pScreen,&menuRegion);
    REGION_NULL(pScreen,&blkoutRegion);
    REGION_NULL(pScreen,&tmpRegion);

    REGION_SUBTRACT(pScreen, &lcopyRegion, &cl->copyRegion, &cl->modifiedRegion);
    dx = cl->copyDX;
    dy = cl->copyDY;

    REGION_UNION(pScreen, &cl->modifiedRegion, &cl->modifiedRegion,
      &cl->copyRegion);

    pVNC->ublen = 0;

    if (shapp->bOn)
    {
      /*rfbLog("Sending WindowsUpdate\n");*/
      for( listIndex=0;
           (shwin=(SharedWindowPtr)List_Element(&shapp->sharedAppList, listIndex)) != NULL;
           listIndex++)
      {
        /*rfbLog("sharedAppUpdate window 0x%x\n",shwin->windowId);*/
        shwin->windowPtr = sharedapp_FindWindow(WindowTable[pScreen->myNum], shwin->windowId);
        if (!shwin->windowPtr)
        {
          /* no match, window may have been closed */
          rfbLog("SHAREDAPP (FindWindow 0x%x) No match. Removing from shared list\n", shwin->windowId); 
          sharedapp_RemoveWindow(cl, &shapp->sharedAppList, shwin->windowId);
          /*listIndex--;*/
          continue;
        }

        if (shapp->bIncludeDialogWindows)
        {
          sharedapp_GetDialogWindows(cl, shwin);  
        }
  
        REGION_EMPTY(pScreen, &updateRegion);
        REGION_EMPTY(pScreen, &updateCopyRegion);
        REGION_EMPTY(pScreen, &winRegion);
        REGION_EMPTY(pScreen, &menuRegion);
        REGION_EMPTY(pScreen, &blkoutRegion);

        REGION_INTERSECT(pScreen, &winRegion, &cl->requestedRegion,
          &shwin->windowPtr->borderClip);
  
        REGION_UNION(pScreen, &updateRegion, &lcopyRegion, &cl->modifiedRegion);

        /* Establish a black out region - area of window that is occluded by other windows */
        REGION_SUBTRACT(pScreen, &blkoutRegion, &shwin->windowPtr->winSize, &winRegion);

        /* look for client windows (menus etc). These will have 
         * redirect_overide TRUE and be children of root window.
         */
        sharedapp_GetMenuWindows(pScreen, shwin, &menuRegion, &blkoutRegion);
        REGION_INTERSECT(pScreen, &menuRegion, &menuRegion, &shwin->windowPtr->winSize);
        REGION_UNION(pScreen, &winRegion, &winRegion, &menuRegion);


         /* Only need to send black out for the newly occluded areas */
        REGION_COPY(pScreen, &tmpRegion, &blkoutRegion);
        REGION_SUBTRACT(pScreen, &blkoutRegion, &blkoutRegion, &shwin->blackOutRegion);
        REGION_COPY(pScreen, &shwin->blackOutRegion, &tmpRegion);

  
        REGION_INTERSECT(pScreen, &updateRegion, &updateRegion, &winRegion);
        
        sharedapp_RfbGetCursorPos(pScreen, &cx, &cy);
        if (POINT_IN_REGION(pScreen, &winRegion, cx, cy, &box)) cursorInWindow = TRUE;
        else cursorInWindow = FALSE;
  
  
  /* need to send change in window location to properly calc dx dy to support copy
   *     REGION_INTERSECT(pScreen, &updateCopyRegion, &lcopyRegion, &winRegion);
   *     REGION_TRANSLATE(pScreen, &winRegion, dx, dy);
   *     REGION_INTERSECT(pScreen, &updateCopyRegion, &updateCopyRegion,
   *       &winRegion);
   */
        REGION_SUBTRACT(pScreen, &updateRegion, &updateRegion, &updateCopyRegion);
  
        REGION_SUBTRACT(pScreen, &cl->modifiedRegion, &cl->modifiedRegion,
          &updateRegion);
        REGION_SUBTRACT(pScreen, &cl->modifiedRegion, &cl->modifiedRegion,
          &updateCopyRegion);


        /*
         * Now send the update.
         */
        if (cl->preferredEncoding == rfbEncodingCoRRE) {
            nUpdateRegionRects = 0;
    
            for (i = 0; i < REGION_NUM_RECTS(&updateRegion); i++) {
                int x = REGION_RECTS(&updateRegion)[i].x1;
                int y = REGION_RECTS(&updateRegion)[i].y1;
                int w = REGION_RECTS(&updateRegion)[i].x2 - x;
                int h = REGION_RECTS(&updateRegion)[i].y2 - y;
                nUpdateRegionRects += (((w-1) / cl->correMaxWidth + 1)
                                         * ((h-1) / cl->correMaxHeight + 1));
            }
        } else if (cl->preferredEncoding == rfbEncodingZlib) {
            nUpdateRegionRects = 0;
    
            for (i = 0; i < REGION_NUM_RECTS(&updateRegion); i++) {
                int x = REGION_RECTS(&updateRegion)[i].x1;
                int y = REGION_RECTS(&updateRegion)[i].y1;
                int w = REGION_RECTS(&updateRegion)[i].x2 - x;
                int h = REGION_RECTS(&updateRegion)[i].y2 - y;
                nUpdateRegionRects += (((h-1) / (ZLIB_MAX_SIZE( w ) / w)) + 1);
            }
        } else if (cl->preferredEncoding == rfbEncodingTight) {
            nUpdateRegionRects = 0;
    
            for (i = 0; i < REGION_NUM_RECTS(&updateRegion); i++) {
                int x = REGION_RECTS(&updateRegion)[i].x1;
                int y = REGION_RECTS(&updateRegion)[i].y1;
                int w = REGION_RECTS(&updateRegion)[i].x2 - x;
                int h = REGION_RECTS(&updateRegion)[i].y2 - y;
                int n = rfbNumCodedRectsTight(cl, x, y, w, h);
                if (n == 0) {
                    nUpdateRegionRects = 0xFFFF;
                    break;
                }
                nUpdateRegionRects += n;
            }
        } else {
            nUpdateRegionRects = REGION_NUM_RECTS(&updateRegion);
        }
    
        if (nUpdateRegionRects != 0xFFFF) {
            /*nRects = REGION_NUM_RECTS(&blkoutRegion) + REGION_NUM_RECTS(&updateCopyRegion) + 
             *        nUpdateRegionRects + !!sendCursorShape + !!sendCursorPos; 
             */
          if (cursorInWindow)
          {
            nRects = REGION_NUM_RECTS(&blkoutRegion) + REGION_NUM_RECTS(&updateCopyRegion) + 
                     nUpdateRegionRects + !!sendCursorShape + !!sendCursorPos; 
          } else {
            nRects = REGION_NUM_RECTS(&blkoutRegion) + REGION_NUM_RECTS(&updateCopyRegion) + 
                     nUpdateRegionRects;
          }
  
        } else {
            nRects = 0xFFFF;
        }
  
        x1 = shwin->windowPtr->winSize.extents.x1;
        y1 = shwin->windowPtr->winSize.extents.y1;
        x2 = shwin->windowPtr->winSize.extents.x2;
        y2 = shwin->windowPtr->winSize.extents.y2;
  
        /*rfbLog("WinSize 0x%x (%d, %d, %d, %d), rects %d\n",
          shwin->windowId, x1, y1, x2, y2, REGION_NUM_RECTS(&shwin->windowPtr->winSize)); */

        if (nRects == 0)
        {
            /*rfbLog("SHAREDAPP -- win 0x%x no rects to send\n", updateMsg->win_id);*/
            continue;
        }

        clearRequestedRegion = TRUE;
  
        if (pVNC->ublen + sz_rfbSharedAppUpdateMsg > UPDATE_BUF_SIZE) 
        {
          if (!rfbSendUpdateBuf(cl)) return FALSE;
        }
  
        updateMsg = (rfbSharedAppUpdateMsg *)(pVNC->updateBuf + pVNC->ublen);
        memset(updateMsg, 0xab, sz_rfbSharedAppUpdateMsg);
        updateMsg->type = rfbSharedAppUpdate;
        updateMsg->win_id = Swap32IfLE(shwin->windowId);
        updateMsg->parent_id = Swap32IfLE(shwin->parentId);
        updateMsg->win_rect.x = Swap16IfLE(x1);
        updateMsg->win_rect.y = Swap16IfLE(y1);
        updateMsg->win_rect.w = Swap16IfLE(x2-x1);
        updateMsg->win_rect.h = Swap16IfLE(y2-y1);
        updateMsg->cursorOffsetX = Swap16IfLE(x1);
        updateMsg->cursorOffsetY = Swap16IfLE(y1);
        updateMsg->nRects = Swap16IfLE(nRects);
        pVNC->ublen += sz_rfbSharedAppUpdateMsg;
        /*rfbLog("Send SharedAppUpdateMsg win 0x%x parent 0x%x (%d, %d, %d, %d), rects %d\n",
          shwin->windowId, shwin->parentId, x1, y1, x2-x1, y2-y1, nRects); */
    
        if ( cursorInWindow )
        {
          cursorInWindow = FALSE;
  
          if (sendCursorShape) 
          {
            cl->cursorWasChanged = FALSE;
            sendCursorShape = FALSE;
            if (!rfbSendCursorShape(cl, pScreen))
                return FALSE;
          }
  
          if (sendCursorPos) 
          {
            cl->cursorWasMoved = FALSE;
            sendCursorPos = FALSE;
            if (!rfbSendCursorPos(cl, pScreen))
                 return FALSE;
          }
        }
  
        if (REGION_NOTEMPTY(pScreen,&updateCopyRegion)) {
          if (!rfbSendCopyRegion(cl,&updateCopyRegion,dx,dy)) {
            rfbLog("WindowBufferUpdate -- Copy failed\n");
            return FALSE;
          }
        }
  
        /*rfbLog("sending %d rects\n", REGION_NUM_RECTS(&updateRegion));*/
        for (i = 0; i < REGION_NUM_RECTS(&updateRegion); i++) {
            int x = REGION_RECTS(&updateRegion)[i].x1;
            int y = REGION_RECTS(&updateRegion)[i].y1;
            int w = REGION_RECTS(&updateRegion)[i].x2 - x;
            int h = REGION_RECTS(&updateRegion)[i].y2 - y;
    
            cl->rfbRawBytesEquivalent += (sz_rfbFramebufferUpdateRectHeader
                                          + w * (cl->format.bitsPerPixel / 8) * h);
    
            switch (cl->preferredEncoding) {
            case rfbEncodingRaw:
                if (!rfbSendRectEncodingRaw(cl, x, y, w, h)) {
                    REGION_UNINIT(pScreen,&updateRegion);
                    return FALSE;
                }
                break;
            case rfbEncodingRRE:
                if (!rfbSendRectEncodingRRE(cl, x, y, w, h)) {
                    REGION_UNINIT(pScreen,&updateRegion);
                    return FALSE;
                }
                break;
            case rfbEncodingCoRRE:
                if (!rfbSendRectEncodingCoRRE(cl, x, y, w, h)) {
                    REGION_UNINIT(pScreen,&updateRegion);
                    return FALSE;
                }
                break;
            case rfbEncodingHextile:
                if (!rfbSendRectEncodingHextile(cl, x, y, w, h)) {
                    REGION_UNINIT(pScreen,&updateRegion);
                    return FALSE;
                }
                break;
            case rfbEncodingZlib:
                if (!rfbSendRectEncodingZlib(cl, x, y, w, h)) {
                    REGION_UNINIT(pScreen,&updateRegion);
                    return FALSE;
                }
                break;
            case rfbEncodingTight:
                if (!rfbSendRectEncodingTight(cl, x, y, w, h)) {
                    REGION_UNINIT(pScreen,&updateRegion);
                    return FALSE;
                }
                break;
            }
        }

        /* now send black out regions */
        /*rfbLog("sending %d blackOut rects\n", REGION_NUM_RECTS(&blkoutRegion));*/
        for (i = 0; i < REGION_NUM_RECTS(&blkoutRegion); i++) 
        {
            int x = REGION_RECTS(&blkoutRegion)[i].x1;
            int y = REGION_RECTS(&blkoutRegion)[i].y1;
            int w = REGION_RECTS(&blkoutRegion)[i].x2 - x;
            int h = REGION_RECTS(&blkoutRegion)[i].y2 - y;

            if (!sharedapp_RfbSendRectBlackOut(cl, x, y, w, h)) 
            {
              rfbLog("BlackOut False");
              return FALSE;
            }
        }
    
        if (nUpdateRegionRects == 0xFFFF && !rfbSendLastRectMarker(cl))
            return FALSE;
      }

      if (clearRequestedRegion)
      {
        REGION_EMPTY(pScreen, &cl->requestedRegion);
      }

      /*
       * cl->cursorWasMoved = FALSE;
       * cl->cursorWasChanged = FALSE;
       */
    }

    REGION_EMPTY(pScreen, &cl->copyRegion);
    cl->copyDX = 0;
    cl->copyDY = 0;

    /*rfbLog("Send All\n"); */
    if (!rfbSendUpdateBuf(cl))
        return FALSE;

    REGION_UNINIT(pScreen,&tmpRegion);
    REGION_UNINIT(pScreen,&blkoutRegion);
    REGION_UNINIT(pScreen,&menuRegion);
    REGION_UNINIT(pScreen,&lcopyRegion);
    REGION_UNINIT(pScreen,&winRegion);
    REGION_UNINIT(pScreen,&updateRegion);
    REGION_UNINIT(pScreen,&updateCopyRegion);
    cl->rfbFramebufferUpdateMessagesSent++;

    return TRUE;

}



void sharedapp_AddWindow(rfbClientPtr cl, List *winlist, unsigned int winId, unsigned int parentId)
{
  SharedWindowPtr shwin;
  int sharedAppCount, i;

  sharedAppCount = List_Count(winlist);
  for( i=0; i<sharedAppCount; i++)
  {
    shwin = (SharedWindowPtr)List_Element(winlist, i);
    if (shwin->windowId == winId)
    {
      /*rfbLog("SHAREDAPP -- Window 0x%x already shared\n", winId);*/
      return;
    }
  }

  shwin = malloc(sizeof(SharedWindow));
  shwin->windowId = winId;
  shwin->parentId = parentId;
  shwin->windowPtr = NULL;
  REGION_NULL(cl->pScreen, &shwin->blackOutRegion);
  rfbLog("AddWindow 0x%x parent 0x%x\n", shwin->windowId, shwin->parentId);

  List_Add(winlist, shwin);
  return;
}


void sharedapp_RemoveWindow(rfbClientPtr cl, List *winlist, unsigned int winId)
{
  SharedWindowPtr shwin, compwin;
  int sharedAppCount, i, j;

  sharedAppCount = List_Count(winlist);
  for( i=sharedAppCount-1; i>=0; i-- )
  {
    shwin = (SharedWindowPtr)List_Element(winlist, i);
    if (shwin->windowId == winId)
    {
      if (shwin->parentId != 0)
      {
        /* has a parent, only remove this window */
        List_Add(closeWindowList, shwin);
        /*List_Remove_Data(winlist, shwin);*/
        /*free(shwin);*/
        return;
      } else { 
        /* remove all children windows also */
        for( j=sharedAppCount-1; j>=0; j-- )
        {
          compwin = (SharedWindowPtr)List_Element(winlist, j);
          if ((shwin->windowId & ID_MASK) == (compwin->windowId & ID_MASK))
          {
            /* related window */
            List_Add(closeWindowList, compwin);
            /*List_Remove_Data(winlist, compwin);*/
            /*free(compwin);*/
          }
        }
      }
    }
  }
}


void sharedapp_RemoveAllWindows(rfbClientPtr cl, List *winlist) 
{
  SharedWindowPtr shwin;
  int sharedAppCount, i;

  sharedAppCount = List_Count(winlist);
  for( i=0; i<sharedAppCount; i++)
  {
    shwin = (SharedWindowPtr)List_Element(winlist, 0);
    /*sharedapp_RfbSendWindowClose(cl, shwin->windowId, shwin->parentId);*/
    List_Add(closeWindowList, shwin);
    /*List_Remove_Data(winlist, shwin);*/
    /*free(shwin);*/
  }
}


void sharedapp_RemoveDialogWindows(rfbClientPtr cl, List *winlist) 
{
  SharedWindowPtr shwin;
  int sharedAppCount, i;

  sharedAppCount = List_Count(winlist);
  for( i=sharedAppCount-1; i>=0; i-- )
  {
    shwin = (SharedWindowPtr)List_Element(winlist, i);
    if (shwin->parentId != 0)
    {
      /* This is a dialog window since it has a parent, remove this window */
      List_Add(closeWindowList, shwin);
    }
  }
}


Bool sharedapp_RfbSendWindowClose(rfbClientPtr cl, unsigned int windowId)
{
  VNCSCREENPTR(cl->pScreen);
  rfbSharedAppUpdateMsg *updateMsg;

  if (pVNC->ublen + sz_rfbSharedAppUpdateMsg > UPDATE_BUF_SIZE) 
  {
    if (!rfbSendUpdateBuf(cl)) return FALSE;
  }
  updateMsg = (rfbSharedAppUpdateMsg *)(pVNC->updateBuf + pVNC->ublen);
  pVNC->ublen += sz_rfbSharedAppUpdateMsg;
  memset(updateMsg, 0, sz_rfbSharedAppUpdateMsg);
  updateMsg->type = rfbSharedAppUpdate;
  updateMsg->win_id = Swap32IfLE(windowId);
  rfbSendUpdateBuf(cl);
  return TRUE;
}


void sharedapp_InvalidateWindowRegion(ScreenPtr pScreen, unsigned int winId)
{
  RegionRec invalidateRegion;
  WindowPtr winPtr;

  if (winId)
  {
    winPtr = sharedapp_FindWindow(WindowTable[pScreen->myNum], winId);
    if (!winPtr) return;
    rfbLog("SHAREDAPP - UpdateFB winId");
    REGION_NULL(pScreen,&invalidateRegion);
    REGION_COPY(pScreen, &invalidateRegion, &winPtr->borderClip);
  } else {
    BoxRec box;
    box.x1 = 0;
    box.y1 = 0;
    box.x2 = pScreen->width;
    box.y2 = pScreen->height;
    REGION_INIT(pScreen, &invalidateRegion, &box, 0);
    rfbLog("SHAREDAPP - UpdateFB All");
  }
  rfbInvalidateRegion(pScreen, &invalidateRegion);

  REGION_UNINIT(pScreen, &invalidateRegion);
  return;
}


Bool sharedapp_GetDialogWindows(rfbClientPtr cl, SharedWindowPtr shwin)
{
  VNCSCREENPTR(cl->pScreen);
  SharedAppVnc *shapp = &pVNC->sharedApp;
  WindowPtr pWin, pChild;
  unsigned int parentMask; 

  pWin = WindowTable[cl->pScreen->myNum];

  
  if (shwin && shwin->windowPtr) 
    parentMask = shwin->windowPtr->parent->drawable.id & ID_MASK;
  else return FALSE;
  

  for (pChild = pWin->firstChild; pChild; pChild = pChild->nextSib)
  {
    if ((pChild->drawable.id & ID_MASK) == parentMask)
    {
      if (pChild->firstChild)
      {
        pWin = pChild->firstChild;
        if (pWin->drawable.id == shwin->windowId) 
        {
          /* this is the same window */
          continue;
        }
        else if (pWin->mapped)
        {
          if ((pWin->drawable.id & ID_MASK) == (shwin->windowId & ID_MASK))
          {
            /* this is a dialog window - add it to our list of shared windows */
            /*rfbLog("adding dialog window 0x%x parent 0x%x\n", pWin->drawable.id, shwin->windowId);*/
            sharedapp_AddWindow(cl, &shapp->sharedAppList, pWin->drawable.id, shwin->windowId);

          }
        }
      }
    }
  }

  return TRUE;
}


void sharedapp_GetMenuWindows(ScreenPtr pScreen, SharedWindowPtr shwin, RegionPtr menuRegion, RegionPtr blkoutRegion)
{
  WindowPtr pWin, pChild;
  RegionRec tmpRegion;
  BoxRec box;

  REGION_NULL(pScreen, &tmpRegion);
  
  pWin = WindowTable[pScreen->myNum];

  for (pChild = pWin->firstChild; pChild; pChild = pChild->nextSib)
  {
    if (pChild->overrideRedirect && pChild->mapped)
    {
      if ((shwin->windowId & ID_MASK) == (pChild->drawable.id & ID_MASK))
      {
        REGION_INTERSECT(pScreen, &tmpRegion, &shwin->windowPtr->winSize, 
          &pChild->winSize);
        if (REGION_NOTEMPTY(pScreen, &tmpRegion))
        {
          /* Menu present */
          int x, y;
          x = (tmpRegion.extents.x1 + tmpRegion.extents.x2) / 2;
          y = tmpRegion.extents.y1 - 1;

          if (POINT_IN_REGION(pScreen, &shwin->windowPtr->winSize, x, y, &box) &&
              !POINT_IN_REGION(pScreen, blkoutRegion, x, y, &box))
          {
            /* top level window - add menus */
            REGION_UNION(pScreen, menuRegion, menuRegion, &pChild->borderClip);
            REGION_SUBTRACT(pScreen, blkoutRegion, blkoutRegion, &pChild->borderClip);
          }
        }
      }
    }
  }
  REGION_UNINIT(pScreen, &tmpRegion);
}


Bool sharedapp_CheckPointer(rfbClientPtr cl, rfbPointerEventMsg *pe)
{
  VNCSCREENPTR(cl->pScreen);
  SharedAppVnc *shapp = &pVNC->sharedApp;
  SharedWindowPtr shwin;
  unsigned int winId;
  BoxRec box;
  int listIndex;
  int x,y;

  /* Remove This */
  if (!cl->supportsSharedAppEncoding) return TRUE;

  winId = Swap32IfLE (pe->windowId);
  x = (int) Swap16IfLE (pe->x);
  y = (int) Swap16IfLE (pe->y);

  /*rfbLog("CheckPointer looking for window 0x%x\n", winId);*/

  for( listIndex=0;
       (shwin=(SharedWindowPtr)List_Element(&shapp->sharedAppList, listIndex)) != NULL;
       listIndex++)
  {
    if (shwin->windowId == winId)
    {
      if (!POINT_IN_REGION(cl->pScreen, &shwin->blackOutRegion, x, y, &box))
      {
        if (shwin->windowPtr)
        {
          if (POINT_IN_REGION(cl->pScreen, &shwin->windowPtr->winSize, x, y, &box))
          {
            /*rfbLog("Pointer in region");*/
            return TRUE;
          } else {
            /*rfbLog("Pointer NOT in region");*/
          }
        } else {
          /*rfbLog("Pointer no winPtr");*/
          return TRUE;
        }
      } else {
        /*rfbLog("Pointer in blackout");*/
      }
    }
  }

  /*rfbLog("CheckPointer FALSE");*/
  return FALSE;
}

Bool
sharedapp_RfbGetCursorPos(pScreen, x, y)
  ScreenPtr pScreen;
  int *x;
  int *y;
{   
#if XFREE86VNC
  ScreenPtr   pCursorScreen = miPointerCurrentScreen();
#endif
  int _x, _y;
                      
#if XFREE86VNC
  if (pScreen == pCursorScreen) 
    miPointerPosition(&_x, &_y);
#else   
  rfbSpriteGetCursorPos(pScreen, &_x, &_y);
#endif  
                                  
  *x=_x;
  *y=_y;
  return TRUE;
}

