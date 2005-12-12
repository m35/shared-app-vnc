/*
 * init.c
 *
 * Modified for SharedAppVnc by Grant Wallace.
 * Modified for XFree86 4.x by Alan Hourihane <alanh@fairlite.demon.co.uk>
 */

/*
 *  Copyright (C) 1999 AT&T Laboratories Cambridge.  All Rights Reserved.
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

/*

Copyright (c) 1993  X Consortium

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE X CONSORTIUM BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of the X Consortium shall
not be used in advertising or otherwise to promote the sale, use or
other dealings in this Software without prior written authorization
from the X Consortium.

*/

/* Use ``#define CORBA'' to enable CORBA control interface */

#include <stdio.h>
#include <unistd.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include "X11/X.h"
#define NEED_EVENTS
#include "X11/Xproto.h"
#include "X11/Xos.h"
#include "scrnintstr.h"
#include "servermd.h"
#include "fb.h"
#include "mfb.h"
#include "mibstore.h"
#include "colormapst.h"
#include "gcstruct.h"
#include "input.h"
#include "mipointer.h"
#include "dixstruct.h"
#include <Xatom.h>
#include <errno.h>
#include <sys/param.h>
#include "dix.h"
#include "micmap.h"
#include "rfb.h"

#ifdef CORBA
#include <vncserverctrl.h>
#endif

#define RFB_DEFAULT_WIDTH  640
#define RFB_DEFAULT_HEIGHT 480
#define RFB_DEFAULT_DEPTH  8
#define RFB_DEFAULT_WHITEPIXEL 0
#define RFB_DEFAULT_BLACKPIXEL 1

static unsigned long VNCGeneration = 0;
rfbScreenInfo rfbScreen;
int rfbGCIndex;
extern char dispatchExceptionAtReset;

extern void VncExtensionInit(void);
extern Bool rfbDCInitialize(ScreenPtr, miPointerScreenFuncPtr);

static Bool initOutputCalled = FALSE;
static Bool noCursor = FALSE;
char *desktopName = "x11";

char rfbThisHost[256];

Atom VNC_LAST_CLIENT_ID;
Atom VNC_CONNECT;

static HWEventQueueType alwaysCheckForInput[2] = { 0, 1 };
static HWEventQueueType *mieqCheckForInput[2];

static char primaryOrder[4] = "";
static int redBits, greenBits, blueBits;

static Bool rfbScreenInit(int index, ScreenPtr pScreen, int argc,
                          char **argv);
static int rfbKeybdProc(DeviceIntPtr pDevice, int onoff);
static int rfbMouseProc(DeviceIntPtr pDevice, int onoff);
static Bool CheckDisplayNumber(int n);

static Bool rfbAlwaysTrue();
static unsigned char *rfbAllocateFramebufferMemory(rfbScreenInfoPtr prfb);
static Bool rfbCursorOffScreen(ScreenPtr *ppScreen, int *x, int *y);
static void rfbCrossScreen(ScreenPtr pScreen, Bool entering);

static miPointerScreenFuncRec rfbPointerCursorFuncs = {
    rfbCursorOffScreen,
    rfbCrossScreen,
    miPointerWarpCursor
};


int inetdSock = -1;
static char inetdDisplayNumStr[10];

/*
 * ddxProcessArgument is our first entry point and will be called at the
 * very start for each argument.  It is not called again on server reset.
 */

int
ddxProcessArgument (argc, argv, i)
    int argc;
    char *argv[];
    int i;
{
    ScreenPtr pScreen = screenInfo.screens[i];
    VNCSCREENPTR(pScreen);
    static Bool firstTime = TRUE;

    if (firstTime)
    {
        pVNC->width  = RFB_DEFAULT_WIDTH;
        pVNC->height = RFB_DEFAULT_HEIGHT;
        pVNC->depth  = RFB_DEFAULT_DEPTH;
        pVNC->blackPixel = RFB_DEFAULT_BLACKPIXEL;
        pVNC->whitePixel = RFB_DEFAULT_WHITEPIXEL;
        pVNC->pfbMemory = NULL;
            pVNC->httpPort = 0;
            pVNC->httpDir = NULL;
        pVNC->rfbAuthPasswdFile = NULL;
        pVNC->udpPort = 0;
            pVNC->rfbPort = 0;
            pVNC->rdpPort = 3389;
        noCursor = FALSE;
          pVNC->loginAuthEnabled = FALSE;
        pVNC->rfbAlwaysShared = FALSE;
        pVNC->rfbNeverShared = FALSE;
        pVNC->rfbDontDisconnect = FALSE;
        pVNC->rfbViewOnly = FALSE;
        pVNC->useGetImage = FALSE;

#ifdef SHAREDAPP 
        sharedapp_Init(&pVNC->sharedApp);
#endif


        gethostname(rfbThisHost, 255);
        pVNC->interface.s_addr = htonl (INADDR_ANY);
        firstTime = FALSE;
    }

    if (strcmp (argv[i], "-geometry") == 0)        /* -geometry WxH */
    {
        if (i + 1 >= argc) UseMsg();
        if (sscanf(argv[i+1],"%dx%d",
                   &pVNC->width,&pVNC->height) != 2) {
            ErrorF("Invalid geometry %s\n", argv[i+1]);
            UseMsg();
        }
#ifdef CORBA
        screenWidth= pVNC->width;
        screenHeight= pVNC->height;
#endif
        return 2;
    }

    if (strcmp (argv[i], "-depth") == 0)        /* -depth D */
    {
        if (i + 1 >= argc) UseMsg();
        pVNC->depth = atoi(argv[i+1]);
#ifdef CORBA
        screenDepth= pVNC->depth;
#endif
        return 2;
    }

    if (strcmp (argv[i], "-pixelformat") == 0) {
        if (i + 1 >= argc) UseMsg();
        if (sscanf(argv[i+1], "%3s", primaryOrder) < 1) {
            ErrorF("Invalid pixel format %s\n", argv[i+1]);
            UseMsg();
        }

        return 2;
    }

    if (strcmp (argv[i], "-blackpixel") == 0) {        /* -blackpixel n */
        if (i + 1 >= argc) UseMsg();
        pVNC->blackPixel = atoi(argv[i+1]);
        return 2;
    }

    if (strcmp (argv[i], "-whitepixel") == 0) {        /* -whitepixel n */
        if (i + 1 >= argc) UseMsg();
        pVNC->whitePixel = atoi(argv[i+1]);
        return 2;
    }

    if (strcmp (argv[i], "-usegetimage") == 0) {
        pVNC->useGetImage = TRUE;
        return 1;
    }

    if (strcmp(argv[i], "-udpinputport") == 0) { /* -udpinputport port */
        if (i + 1 >= argc) UseMsg();
        pVNC->udpPort = atoi(argv[i+1]);
        return 2;
    }

    if (strcmp(argv[i], "-rfbport") == 0) {        /* -rfbport port */
        if (i + 1 >= argc) UseMsg();
        pVNC->rfbPort = atoi(argv[i+1]);
        return 2;
    }

    if (strcmp(argv[i], "-rfbwait") == 0) {        /* -rfbwait ms */
        if (i + 1 >= argc) UseMsg();
        rfbMaxClientWait = atoi(argv[i+1]);
        return 2;
    }

    if (strcmp(argv[i], "-nocursor") == 0) {
        noCursor = TRUE;
        return 1;
    }

    if (strcmp(argv[i], "-rfbauth") == 0) {        /* -rfbauth passwd-file */
        if (i + 1 >= argc) UseMsg();
        pVNC->rfbAuthPasswdFile = argv[i+1];
        return 2;
    }

    if (strcmp(argv[i], "-loginauth") == 0) {
        if (geteuid() == 0) {
            /* Only when run as root! */
            pVNC->loginAuthEnabled = TRUE;
        }
        return 1;
    }

    if (strcmp(argv[i], "-httpd") == 0) {
        if (i + 1 >= argc) UseMsg();
        pVNC->httpDir = argv[i+1];
        return 2;
    }

    if (strcmp(argv[i], "-httpport") == 0) {
        if (i + 1 >= argc) UseMsg();
        pVNC->httpPort = atoi(argv[i+1]);
        return 2;
    }

    if (strcmp(argv[i], "-deferupdate") == 0) {        /* -deferupdate ms */
        if (i + 1 >= argc) UseMsg();
        rfbDeferUpdateTime = atoi(argv[i+1]);
        return 2;
    }

    if (strcmp(argv[i], "-economictranslate") == 0) {
        rfbEconomicTranslate = TRUE;
        return 1;
    }

    if (strcmp(argv[i], "-lazytight") == 0) {
        rfbTightDisableGradient = TRUE;
        return 1;
    }

    if (strcmp(argv[i], "-desktop") == 0) {        /* -desktop desktop-name */
        if (i + 1 >= argc) UseMsg();
        desktopName = argv[i+1];
        return 2;
    }

#if 0 /* not deemed useful on standalone server - leave for completeness */
    if (strcmp(argv[i], "-useraccept") == 0) {
        pVNC->rfbUserAccept = TRUE;
        return 1;
    }
#endif

    if (strcmp(argv[i], "-alwaysshared") == 0) {
        pVNC->rfbAlwaysShared = TRUE;
        return 1;
    }

    if (strcmp(argv[i], "-nevershared") == 0) {
        pVNC->rfbNeverShared = TRUE;
        return 1;
    }

    if (strcmp(argv[i], "-dontdisconnect") == 0) {
        pVNC->rfbDontDisconnect = TRUE;
        return 1;
    }

    /* Run server in view-only mode - Ehud Karni SW */
    if (strcmp(argv[i], "-viewonly") == 0) {
        pVNC->rfbViewOnly = TRUE;
        return 1;
    }

    if (strcmp(argv[i], "-localhost") == 0) {
        pVNC->interface.s_addr = htonl (INADDR_LOOPBACK);
        return 1;
    }

    if (strcmp(argv[i], "-interface") == 0) {        /* -interface ipaddr */
        struct in_addr got;
        unsigned long octet;
        char *p, *end;
        int q;
        if (i + 1 >= argc) {
            UseMsg();
            return 2;
        }
        if (pVNC->interface.s_addr != htonl (INADDR_ANY)) {
            /* Already set (-localhost?). */
            return 2;
        }
        p = argv[i + 1];
        for (q = 0; q < 4; q++) {
            octet = strtoul (p, &end, 10);
            if (p == end || octet > 255) {
                UseMsg ();
                return 2;
            }
            if ((q < 3 && *end != '.') ||
                (q == 3 && *end != '\0')) {
                UseMsg ();
                return 2;
            }
            got.s_addr = (got.s_addr << 8) | octet;
            p = end + 1;
        }
        pVNC->interface.s_addr = htonl (got.s_addr);
        return 2;
    }

    if (strcmp(argv[i], "-inetd") == 0) {        /* -inetd */ 
        int n;
        for (n = 1; n < 100; n++) {
            if (CheckDisplayNumber(n))
                break;
        }

        if (n >= 100)
            FatalError("-inetd: couldn't find free display number");

        sprintf(inetdDisplayNumStr, "%d", n);
        display = inetdDisplayNumStr;

        /* fds 0, 1 and 2 (stdin, out and err) are all the same socket to the
           RFB client.  OsInit() closes stdout and stdin, and we don't want
           stderr to go to the RFB client, so make the client socket 3 and
           close stderr.  OsInit() will redirect stderr logging to an
           appropriate log file or /dev/null if that doesn't work. */

        dup2(0,3);
        inetdSock = 3;
        close(2);

        return 1;
    }

    if (strcmp(argv[i], "-version") == 0) {
        ErrorF("Xvnc version %s\n", XVNCRELEASE);
        exit(0);
    }

    if (inetdSock != -1 && argv[i][0] == ':') {
        FatalError("can't specify both -inetd and :displaynumber");
    }

    return 0;
}


/*
 * InitOutput is called every time the server resets.  It should call
 * AddScreen for each screen (FIXME - but we only ever have one), 
 * and in turn this will call rfbScreenInit.
 */

/* Common pixmap formats */

static PixmapFormatRec formats[MAXFORMATS] = {
        { 1,        1,        BITMAP_SCANLINE_PAD },
        { 4,        8,        BITMAP_SCANLINE_PAD },
        { 8,        8,        BITMAP_SCANLINE_PAD },
        { 15,        16,        BITMAP_SCANLINE_PAD },
        { 16,        16,        BITMAP_SCANLINE_PAD },
        { 24,        32,        BITMAP_SCANLINE_PAD },
#ifdef RENDER
        { 32,        32,        BITMAP_SCANLINE_PAD },
#endif
};
#ifdef RENDER
static int numFormats = 7;
#else
static int numFormats = 6;
#endif

void
InitOutput(screenInfo, argc, argv)
    ScreenInfo *screenInfo;
    int argc;
    char **argv;
{
    int i;
    initOutputCalled = TRUE;

    rfbLog("Xvnc version %s\n", XVNCRELEASE);
    rfbLog("Copyright (C) 2001-2004 Alan Hourihane.\n");
    rfbLog("Copyright (C) 2000-2004 Constantin Kaplinsky\n");
    rfbLog("Copyright (C) 1999 AT&T Laboratories Cambridge\n");
    rfbLog("All Rights Reserved.\n");
    rfbLog("See http://www.tightvnc.com/ for information on TightVNC\n");
    rfbLog("See http://xf4vnc.sf.net for xf4vnc-specific information\n");
    rfbLog("Desktop name '%s' (%s:%s)\n",desktopName,rfbThisHost,display);
    rfbLog("Protocol versions supported: %d.%d, %d.%d\n",
           rfbProtocolMajorVersion, rfbProtocolMinorVersion,
           rfbProtocolMajorVersion, rfbProtocolFallbackMinorVersion);

    VNC_LAST_CLIENT_ID = MakeAtom("VNC_LAST_CLIENT_ID",
                                  strlen("VNC_LAST_CLIENT_ID"), TRUE);
    VNC_CONNECT = MakeAtom("VNC_CONNECT", strlen("VNC_CONNECT"), TRUE);

    /* initialize pixmap formats */

    screenInfo->imageByteOrder = IMAGE_BYTE_ORDER;
    screenInfo->bitmapScanlineUnit = BITMAP_SCANLINE_UNIT;
    screenInfo->bitmapScanlinePad = BITMAP_SCANLINE_PAD;
    screenInfo->bitmapBitOrder = BITMAP_BIT_ORDER;
    screenInfo->numPixmapFormats = numFormats;
    for (i = 0; i < numFormats; i++)
            screenInfo->formats[i] = formats[i];

    rfbGCIndex = AllocateGCPrivateIndex();
    if (rfbGCIndex < 0) {
        FatalError("InitOutput: AllocateGCPrivateIndex failed\n");
    }

    /* initialize screen */

    if (AddScreen(rfbScreenInit, argc, argv) == -1) {
        FatalError("Couldn't add screen");
    }

#ifdef CORBA
    initialiseCORBA(argc, argv, desktopName);
#endif
}

static void
rfbWakeupHandler (
    int         i,        
    pointer     blockData,
    unsigned long err,
    pointer     pReadmask
){
    ScreenPtr      pScreen = screenInfo.screens[i];
    VNCSCREENPTR(pScreen);
    int e = (int)err;

    if (e < 0)
        goto SKIPME;
        
    rfbRootPropertyChange(pScreen);

#if XFREE86VNC
    if (pScrn->vtSema) {
            rfbCheckFds(pScreen);
            httpCheckFds(pScreen);
#if 0
        rdpCheckFds(pScreen);
#endif
#ifdef CORBA
            corbaCheckFds();
#endif
    } else {
            rfbCheckFds(pScreen);
#if 0
        rdpCheckFds(pScreen);
#endif
    }
#else
    rfbCheckFds(pScreen);
    httpCheckFds(pScreen);
#if 0
    rdpCheckFds(pScreen);
#endif
#ifdef CORBA
    corbaCheckFds();
#endif
#endif

SKIPME:

    pScreen->WakeupHandler = pVNC->WakeupHandler;
    (*pScreen->WakeupHandler) (i, blockData, err, pReadmask);
    pScreen->WakeupHandler = rfbWakeupHandler;
}

static Bool
rfbScreenInit(index, pScreen, argc, argv)
    int index;
    ScreenPtr pScreen;
    int argc;
    char ** argv;
{
    rfbScreenInfoPtr prfb = &rfbScreen;
    int dpix = 75, dpiy = 75;
    int ret;
    unsigned char *pbits;
    VisualPtr vis;
#ifdef RENDER
    PictureScreenPtr        ps;
#endif

    if (VNCGeneration != serverGeneration) {
        VncExtensionInit();
        VNCGeneration = serverGeneration;
    }

    if (monitorResolution != 0) {
        dpix = monitorResolution;
        dpiy = monitorResolution;
    }

    prfb->rfbAuthTries = 0;
    prfb->rfbAuthTooManyTries = FALSE;
    prfb->rfbUserAccept = FALSE;
    prfb->udpSockConnected = FALSE;
    prfb->timer = NULL;
    prfb->httpListenSock = -1;
    prfb->httpSock = -1;
    prfb->rfbListenSock = -1;
    prfb->rdpListenSock = -1;
    prfb->paddedWidthInBytes = PixmapBytePad(prfb->width, prfb->depth);
    prfb->bitsPerPixel = rfbBitsPerPixel(prfb->depth);
    pbits = rfbAllocateFramebufferMemory(prfb);
    if (!pbits) return FALSE;

    miClearVisualTypes();

    if (defaultColorVisualClass == -1)
            defaultColorVisualClass = TrueColor;

    if (!miSetVisualTypes(prfb->depth, miGetDefaultVisualMask(prfb->depth), 8,
                                                defaultColorVisualClass) )
        return FALSE;

    miSetPixmapDepths();

    switch (prfb->bitsPerPixel)
    {
    case 1:
        ret = mfbScreenInit(pScreen, pbits, prfb->width, prfb->height,
                            dpix, dpiy, prfb->paddedWidthInBytes * 8);
        break;
    case 8:
        ret = fbScreenInit(pScreen, pbits, prfb->width, prfb->height,
                            dpix, dpiy, prfb->paddedWidthInBytes, 8);
        break;
    case 16:
        ret = fbScreenInit(pScreen, pbits, prfb->width, prfb->height,
                              dpix, dpiy, prfb->paddedWidthInBytes / 2, 16);
        if (prfb->depth == 15) {
              blueBits = 5; greenBits = 5; redBits = 5;
        } else {
            blueBits = 5; greenBits = 6; redBits = 5;
        }
        break;
    case 32:
        ret = fbScreenInit(pScreen, pbits, prfb->width, prfb->height,
                              dpix, dpiy, prfb->paddedWidthInBytes / 4, 32);
        blueBits = 8; greenBits = 8; redBits = 8;
        break;
    default:
        return FALSE;
    }

    if (!ret) return FALSE;

    miInitializeBackingStore(pScreen);

    if (prfb->bitsPerPixel > 8) {
            if (strcasecmp(primaryOrder, "bgr") == 0) {
            rfbLog("BGR format %d %d %d\n", blueBits, greenBits, redBits);
            vis = pScreen->visuals + pScreen->numVisuals;
            while (--vis >= pScreen->visuals) {
                    if ((vis->class | DynamicClass) == DirectColor) {
                    vis->offsetRed = 0;
                    vis->redMask = (1 << redBits) - 1;
                    vis->offsetGreen = redBits;
                    vis->greenMask = ((1 << greenBits) - 1) << vis->offsetGreen;
                    vis->offsetBlue = redBits + greenBits;
                    vis->blueMask = ((1 << blueBits) - 1) << vis->offsetBlue;
                    }
            }
            } else {
            rfbLog("RGB format %d %d %d\n", blueBits, greenBits, redBits);
                   vis = pScreen->visuals + pScreen->numVisuals;
            while (--vis >= pScreen->visuals) {
                    if ((vis->class | DynamicClass) == DirectColor) {
                    vis->offsetBlue = 0;
                    vis->blueMask = (1 << blueBits) - 1;
                    vis->offsetGreen = blueBits;
                    vis->greenMask = ((1 << greenBits) - 1) << vis->offsetGreen;
                    vis->offsetRed = blueBits + greenBits;
                    vis->redMask = ((1 << redBits) - 1) << vis->offsetRed;
                    }
            }
            }
    }

    if (prfb->bitsPerPixel > 4)
        fbPictureInit(pScreen, 0, 0);

    if (!AllocateGCPrivate(pScreen, rfbGCIndex, sizeof(rfbGCRec))) {
        FatalError("rfbScreenInit: AllocateGCPrivate failed\n");
    }

    prfb->cursorIsDrawn = FALSE;
    prfb->dontSendFramebufferUpdate = FALSE;

    prfb->CloseScreen = pScreen->CloseScreen;
    prfb->WakeupHandler = pScreen->WakeupHandler;
    prfb->CreateGC = pScreen->CreateGC;
    prfb->PaintWindowBackground = pScreen->PaintWindowBackground;
    prfb->PaintWindowBorder = pScreen->PaintWindowBorder;
    prfb->CopyWindow = pScreen->CopyWindow;
    prfb->ClearToBackground = pScreen->ClearToBackground;
    prfb->RestoreAreas = pScreen->RestoreAreas;
#ifdef CHROMIUM
    prfb->RealizeWindow = pScreen->RealizeWindow;
    prfb->UnrealizeWindow = pScreen->UnrealizeWindow;
    prfb->DestroyWindow = pScreen->DestroyWindow;
    prfb->PositionWindow = pScreen->PositionWindow;
    prfb->ResizeWindow = pScreen->ResizeWindow;
    prfb->ClipNotify = pScreen->ClipNotify;
#endif
#ifdef RENDER
    ps = GetPictureScreenIfSet(pScreen);
    if (ps)
            prfb->Composite = ps->Composite;
#endif
    pScreen->CloseScreen = rfbCloseScreen;
    pScreen->WakeupHandler = rfbWakeupHandler;
    pScreen->CreateGC = rfbCreateGC;
    pScreen->PaintWindowBackground = rfbPaintWindowBackground;
    pScreen->PaintWindowBorder = rfbPaintWindowBorder;
    pScreen->CopyWindow = rfbCopyWindow;
    pScreen->ClearToBackground = rfbClearToBackground;
    pScreen->RestoreAreas = rfbRestoreAreas;
#ifdef CHROMIUM
    pScreen->RealizeWindow = rfbRealizeWindow;
    pScreen->UnrealizeWindow = rfbUnrealizeWindow;
    pScreen->DestroyWindow = rfbDestroyWindow;
    pScreen->PositionWindow = rfbPositionWindow;
    pScreen->ResizeWindow = rfbResizeWindow;
    pScreen->ClipNotify = rfbClipNotify;
#endif
#ifdef RENDER
    if (ps)
            ps->Composite = rfbComposite;
#endif

    pScreen->InstallColormap = rfbInstallColormap;
    pScreen->UninstallColormap = rfbUninstallColormap;
    pScreen->ListInstalledColormaps = rfbListInstalledColormaps;
    pScreen->StoreColors = rfbStoreColors;

    pScreen->SaveScreen = rfbAlwaysTrue;

    rfbDCInitialize(pScreen, &rfbPointerCursorFuncs);

    if (noCursor) {
        pScreen->DisplayCursor = rfbAlwaysTrue;
        prfb->cursorIsDrawn = TRUE;
    }

    pScreen->blackPixel = prfb->blackPixel;
    pScreen->whitePixel = prfb->whitePixel;

    prfb->rfbServerFormat.bitsPerPixel = prfb->bitsPerPixel;
    prfb->rfbServerFormat.depth = prfb->depth;
    prfb->rfbServerFormat.bigEndian = !(*(char *)&rfbEndianTest);

    /* Find the root visual and set the server format */
    for (vis = pScreen->visuals; vis->vid != pScreen->rootVisual; vis++)
        ;
    prfb->rfbServerFormat.trueColour = (vis->class == TrueColor);

    if ( (vis->class == TrueColor) || (vis->class == DirectColor) ) {
        prfb->rfbServerFormat.redMax = vis->redMask >> vis->offsetRed;
        prfb->rfbServerFormat.greenMax = vis->greenMask >> vis->offsetGreen;
        prfb->rfbServerFormat.blueMax = vis->blueMask >> vis->offsetBlue;
        prfb->rfbServerFormat.redShift = vis->offsetRed;
        prfb->rfbServerFormat.greenShift = vis->offsetGreen;
        prfb->rfbServerFormat.blueShift = vis->offsetBlue;
    } else {
        prfb->rfbServerFormat.redMax
            = prfb->rfbServerFormat.greenMax 
            = prfb->rfbServerFormat.blueMax = 0;
        prfb->rfbServerFormat.redShift
            = prfb->rfbServerFormat.greenShift 
            = prfb->rfbServerFormat.blueShift = 0;
    }

    if (prfb->bitsPerPixel == 1)
    {
        ret = mfbCreateDefColormap(pScreen);
    }
    else
    {
        ret = fbCreateDefColormap(pScreen);
    }

    rfbInitSockets(pScreen);
#if 0
    rdpInitSockets(pScreen);
#endif
    if (inetdSock == -1)
        httpInitSockets(pScreen);

    
#ifdef SHAREDAPP 
    sharedapp_InitReverseConnection(pScreen);
#endif

    return ret;

} /* end rfbScreenInit */



/*
 * InitInput is also called every time the server resets.  It is called after
 * InitOutput so we can assume that rfbInitSockets has already been called.
 */

void
InitInput(argc, argv)
    int argc;
    char *argv[];
{
    DeviceIntPtr p, k;
    k = AddInputDevice(rfbKeybdProc, TRUE);
    p = AddInputDevice(rfbMouseProc, TRUE);
    RegisterKeyboardDevice(k);
    RegisterPointerDevice(p);
    miRegisterPointerDevice(screenInfo.screens[0], p);
    (void)mieqInit ((DevicePtr)k, (DevicePtr)p);
#if 0
    mieqCheckForInput[0] = checkForInput[0];
    mieqCheckForInput[1] = checkForInput[1];
    SetInputCheck(&alwaysCheckForInput[0], &alwaysCheckForInput[1]);
#endif
}


static int
rfbKeybdProc(pDevice, onoff)
    DeviceIntPtr pDevice;
    int onoff;
{
    KeySymsRec                keySyms;
    CARD8                 modMap[MAP_LENGTH];
    DevicePtr pDev = (DevicePtr)pDevice;

    switch (onoff)
    {
    case DEVICE_INIT: 
        KbdDeviceInit(pDevice, &keySyms, modMap);
        InitKeyboardDeviceStruct(pDev, &keySyms, modMap,
                                 (BellProcPtr)rfbSendBell,
                                 (KbdCtrlProcPtr)NoopDDA);
            break;
    case DEVICE_ON: 
        pDev->on = TRUE;
        KbdDeviceOn();
        break;
    case DEVICE_OFF: 
        pDev->on = FALSE;
        KbdDeviceOff();
        break;
    case DEVICE_CLOSE:
        if (pDev->on)
            KbdDeviceOff();
        break;
    }
    return Success;
}

static int
rfbMouseProc(pDevice, onoff)
    DeviceIntPtr pDevice;
    int onoff;
{
    BYTE map[6];
    DevicePtr pDev = (DevicePtr)pDevice;

    switch (onoff)
    {
    case DEVICE_INIT:
        PtrDeviceInit();
        map[1] = 1;
        map[2] = 2;
        map[3] = 3;
        map[4] = 4;
        map[5] = 5;
        InitPointerDeviceStruct(pDev, map, 5, miPointerGetMotionEvents,
                                PtrDeviceControl,
                                miPointerGetMotionBufferSize());
        break;

    case DEVICE_ON:
        pDev->on = TRUE;
        PtrDeviceOn(pDevice);
        break;

    case DEVICE_OFF:
        pDev->on = FALSE;
        PtrDeviceOff();
        break;

    case DEVICE_CLOSE:
        if (pDev->on)
            PtrDeviceOff();
        break;
    }
    return Success;
}


Bool
LegalModifier(key, pDev)
    unsigned int key;
    DevicePtr        pDev;
{
    return TRUE;
}


void
ProcessInputEvents()
{
#if 0
    if (*mieqCheckForInput[0] != *mieqCheckForInput[1]) {
#endif
        mieqProcessInputEvents();
        miPointerUpdate();
#if 0
    }
#endif
}


static Bool CheckDisplayNumber(int n)
{
    char fname[32];
    int sock;
    struct sockaddr_in addr;

    sock = socket(AF_INET, SOCK_STREAM, 0);

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(6000+n);
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(sock);
        return FALSE;
    }
    close(sock);

    sprintf(fname, "/tmp/.X%d-lock", n);
    if (access(fname, F_OK) == 0)
        return FALSE;

    sprintf(fname, "/tmp/.X11-unix/X%d", n);
    if (access(fname, F_OK) == 0)
        return FALSE;

    return TRUE;
}

static Bool
rfbAlwaysTrue(void)
{
    return TRUE;
}


static unsigned char *
rfbAllocateFramebufferMemory(prfb)
    rfbScreenInfoPtr prfb;
{
    if (prfb->pfbMemory) return prfb->pfbMemory; /* already done */

    prfb->sizeInBytes = (prfb->paddedWidthInBytes * prfb->height);

    prfb->pfbMemory = (unsigned char *)Xalloc(prfb->sizeInBytes);

    return prfb->pfbMemory;
}


static Bool
rfbCursorOffScreen (ppScreen, x, y)
    ScreenPtr   *ppScreen;
    int         *x, *y;
{
    return FALSE;
}

static void
rfbCrossScreen (pScreen, entering)
    ScreenPtr   pScreen;
    Bool        entering;
{
}

void
ddxGiveUp()
{
    Xfree(rfbScreen.pfbMemory);
    if (initOutputCalled) {
        char unixSocketName[32];
        sprintf(unixSocketName,"/tmp/.X11-unix/X%s",display);
        unlink(unixSocketName);
#ifdef CORBA
        shutdownCORBA();
#endif
    }
}

void
AbortDDX()
{
    ddxGiveUp();
}

void
OsVendorInit()
{
}

void
OsVendorFatalError()
{
}

#ifdef DDXTIME /* from ServerOSDefines */
CARD32
GetTimeInMillis()
{
    struct timeval  tp;

    X_GETTIMEOFDAY(&tp);
    return(tp.tv_sec * 1000) + (tp.tv_usec / 1000);
}
#endif

void
ddxUseMsg()
{
    ErrorF("-geometry WxH          set framebuffer width & height\n");
    ErrorF("-depth D               set framebuffer depth\n");
    ErrorF("-pixelformat format    set pixel format (BGRnnn or RGBnnn)\n");
    ErrorF("-udpinputport port     UDP port for keyboard/pointer data\n");
    ErrorF("-rfbport port          TCP port for RFB protocol\n");
    ErrorF("-rfbwait time          max time in ms to wait for RFB client\n");
    ErrorF("-nocursor              don't put up a cursor\n");
    ErrorF("-rfbauth passwd-file   use authentication on RFB protocol\n");
    ErrorF("-loginauth             use login-style Unix authentication\n");
    ErrorF("-httpd dir             serve files via HTTP from here\n");
    ErrorF("-httpport port         port for HTTP\n");
    ErrorF("-deferupdate time      time in ms to defer updates "
                                                             "(default 40)\n");
    ErrorF("-economictranslate     less memory-hungry translation\n");
    ErrorF("-lazytight             disable \"gradient\" filter in tight "
                                                                "encoding\n");
    ErrorF("-desktop name          VNC desktop name (default x11)\n");
    ErrorF("-alwaysshared          always treat new clients as shared\n");
    ErrorF("-nevershared           never treat new clients as shared\n");
    ErrorF("-dontdisconnect        don't disconnect existing clients when a "
                                                             "new non-shared\n"
           "                       connection comes in (refuse new connection "
                                                                 "instead)\n");
    ErrorF("-localhost             only allow connections from localhost\n"
           "                           to the vnc ports. Use -nolisten tcp to disable\n"
           "                           remote X clients as well.\n");
    ErrorF("-viewonly              let clients only view the desktop\n");
    ErrorF("-interface ipaddr      only bind to specified interface "
                                                                "address\n");
    ErrorF("-inetd                 Xvnc is launched by inetd\n");
    exit(1);
}

/*
 * rfbLog prints a time-stamped message to the log file (stderr).
 */

void rfbLog(char *format, ...)
{
    va_list args;
    char buf[256];
    time_t clock;

    va_start(args, format);

    time(&clock);
    strftime(buf, 255, "%d/%m/%Y %H:%M:%S ", localtime(&clock));
    fprintf(stderr, buf);

    vfprintf(stderr, format, args);
    fflush(stderr);

    va_end(args);
}

void rfbLogPerror(char *str)
{
    rfbLog("Perror: %s", str);
    rfbLog("");
    perror(str);
}
