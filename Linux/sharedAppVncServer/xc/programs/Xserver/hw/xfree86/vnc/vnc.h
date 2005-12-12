/*
 *  Copyright (C) 2002 Alan Hourihane.  All Rights Reserved.
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
 *
 *  Author: Alan Hourihane <alanh@fairlite.demon.co.uk>
 */

#ifndef _VNC_H_
#define _VNC_H_

#include <xf86Cursor.h>
#include <xf86CursorPriv.h>

extern int vncScreenPrivateIndex;

#define VNCPTR(pScreen)\
   (vncScreenPtr)((pScreen)->devPrivates[vncScreenPrivateIndex].ptr)

typedef struct {
    int                        rfbPort;
    int                        rdpPort;
    int                        udpPort;
    int                        rfbListenSock;
    int                        rdpListenSock;
    int                        udpSock;
    int                        httpPort;
    int                        httpListenSock;
    int                        httpSock;
    char *                httpDir;
    char                buf[HTTP_BUF_SIZE];
    Bool                udpSockConnected;
    char *                rfbAuthPasswdFile;
    size_t                buf_filled;
    int                        maxFd;
    fd_set                allFds;
    unsigned char *        oldpfbMemory;
    Bool                useGetImage;
    Bool                rfbAlwaysShared;
    Bool                rfbNeverShared;
    Bool                rfbDontDisconnect;
    Bool                rfbUserAccept;
    Bool                rfbViewOnly;
    unsigned char *        pfbMemory;
    int                 paddedWidthInBytes;
    ColormapPtr         rfbInstalledColormap;
    ColormapPtr                savedColormap;
    rfbPixelFormat        rfbServerFormat;
    Bool                rfbAuthTooManyTries;
    int                        rfbAuthTries;
    Bool                loginAuthEnabled;
    struct in_addr        interface;
    OsTimerPtr                 timer;
    char                 updateBuf[UPDATE_BUF_SIZE];
    int                 ublen;
    int                 width;
    int                 height;
    int                 depth;
    int                 bitsPerPixel;

    /* The following two members are used to minimise the amount of unnecessary
       drawing caused by cursor movement.  Whenever any drawing affects the
       part of the screen where the cursor is, the cursor is removed first and
       then the drawing is done (this is what the sprite routines test for).
       Afterwards, however, we do not replace the cursor, even when the cursor
       is logically being moved across the screen.  We only draw the cursor
       again just as we are about to send the client a framebuffer update.

       We need to be careful when removing and drawing the cursor because of
       their relationship with the normal drawing routines.  The drawing
       routines can invoke the cursor routines, but also the cursor routines
       themselves end up invoking drawing routines.

       Removing the cursor (rfbSpriteRemoveCursor) is eventually achieved by
       doing a CopyArea from a pixmap to the screen, where the pixmap contains
       the saved contents of the screen under the cursor.  Before doing this,
       however, we set cursorIsDrawn to FALSE.  Then, when CopyArea is called,
       it sees that cursorIsDrawn is FALSE and so doesn't feel the need to
       (recursively!) remove the cursor before doing it.

       Putting up the cursor (rfbSpriteRestoreCursor) involves a call to
       PushPixels.  While this is happening, cursorIsDrawn must be FALSE so
       that PushPixels doesn't think it has to remove the cursor first.
       Obviously cursorIsDrawn is set to TRUE afterwards.

       Another problem we face is that drawing routines sometimes cause a
       framebuffer update to be sent to the RFB client.  When the RFB client is
       already waiting for a framebuffer update and some drawing to the
       framebuffer then happens, the drawing routine sees that the client is
       ready, so it calls rfbSendFramebufferUpdate.  If the cursor is not drawn
       at this stage, it must be put up, and so rfbSpriteRestoreCursor is
       called.  However, if the original drawing routine was actually called
       from within rfbSpriteRestoreCursor or rfbSpriteRemoveCursor we don't
       want this to happen.  So both the cursor routines set
       dontSendFramebufferUpdate to TRUE, and all the drawing routines check
       this before calling rfbSendFramebufferUpdate. */

    Bool cursorIsDrawn;                    /* TRUE if the cursor is currently drawn */
    Bool dontSendFramebufferUpdate; /* TRUE while removing or drawing the
                                       cursor */

    /* wrapped screen functions */

    CloseScreenProcPtr                        CloseScreen;
    CreateGCProcPtr                        CreateGC;
    PaintWindowBackgroundProcPtr        PaintWindowBackground;
    PaintWindowBorderProcPtr                PaintWindowBorder;
    CopyWindowProcPtr                        CopyWindow;
    ClearToBackgroundProcPtr                ClearToBackground;
    RestoreAreasProcPtr                        RestoreAreas;
    ScreenWakeupHandlerProcPtr                 WakeupHandler;
    InstallColormapProcPtr                InstallColormap;
    UninstallColormapProcPtr                UninstallColormap;
    ListInstalledColormapsProcPtr         ListInstalledColormaps;
    StoreColorsProcPtr                        StoreColors;
    xf86EnableDisableFBAccessProc        *EnableDisableFBAccess;
    miPointerSpriteFuncPtr                spriteFuncs;
    DisplayCursorProcPtr                DisplayCursor;
    CursorPtr                                pCurs;
    Bool                                (*UseHWCursor)(ScreenPtr, CursorPtr);
    Bool                                (*UseHWCursorARGB)(ScreenPtr, CursorPtr);
    Bool                                *SWCursor;
#ifdef CHROMIUM
    RealizeWindowProcPtr                RealizeWindow;
    UnrealizeWindowProcPtr                UnrealizeWindow;
    DestroyWindowProcPtr                DestroyWindow;
    ResizeWindowProcPtr                        ResizeWindow;
    PositionWindowProcPtr                PositionWindow;
    ClipNotifyProcPtr                        ClipNotify;
#endif
#ifdef RENDER
    CompositeProcPtr                        Composite;
#endif
#ifdef SHAREDAPP
    SharedAppVnc sharedApp;
#endif

} vncScreenRec, *vncScreenPtr;

extern Bool vncUseHWCursor(ScreenPtr pScreen, CursorPtr pCurs);
extern Bool vncUseHWCursorARGB(ScreenPtr pScreen, CursorPtr pCurs);
extern void rfbEnableDisableFBAccess (int index, Bool enable);

#endif /* _VNC_H_ */

