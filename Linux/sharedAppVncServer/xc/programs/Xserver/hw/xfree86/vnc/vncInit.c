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

#include "rfb.h"
#include "xf1bpp.h"
#include "xf4bpp.h"
#include "fb.h"

#include "xf86.h"
#include "xf86_OSproc.h"
#include "xf86Resources.h"
#include "xf86Version.h"
#include "xf86cmap.h"
#include "dixstruct.h"
#include "compiler.h"

#include "mipointer.h"

#include "mibstore.h"

#include "globals.h"
#define DPMS_SERVER
#include "extensions/dpms.h"

#include <netinet/in.h>

int vncScreenPrivateIndex = -1;
int rfbGCIndex = -1;
int inetdSock = -1;
Atom VNC_LAST_CLIENT_ID = 0;
Atom VNC_CONNECT = 0;
char *desktopName = "x11";
char rfbThisHost[256];

extern void VncExtensionInit(void);

extern void vncInitMouse(void);
extern void vncInitKeyb(void);
Bool VNCInit(ScreenPtr pScreen, unsigned char *FBStart);

#ifndef XFree86LOADER
static unsigned long VNCGeneration = 0;
#endif
static const OptionInfoRec *VNCAvailableOptions(void *unused);
static void rfbWakeupHandler (int i, pointer blockData, unsigned long err, pointer pReadmask);

static Bool vncCursorRealizeCursor(ScreenPtr, CursorPtr);
static Bool vncCursorUnrealizeCursor(ScreenPtr, CursorPtr);
static void vncCursorSetCursor(ScreenPtr, CursorPtr, int, int);
static void vncCursorMoveCursor(ScreenPtr, int, int);
static Bool vncDisplayCursor(ScreenPtr, CursorPtr);

static miPointerSpriteFuncRec vncCursorSpriteFuncs = {
   vncCursorRealizeCursor,
   vncCursorUnrealizeCursor,
   vncCursorSetCursor,
   vncCursorMoveCursor
};

/*
 * VNC Config options
 */

typedef enum {
    OPTION_USEVNC,
    OPTION_RFBPORT,
    OPTION_HTTPPORT,
    OPTION_ALWAYS_SHARED,
    OPTION_NEVER_SHARED,
    OPTION_DONT_DISCONNECT,
    OPTION_HTTPDIR,
    OPTION_PASSWD_FILE,
    OPTION_USER_ACCEPT,
    OPTION_LOCALHOST,
    OPTION_INTERFACE,
    OPTION_VIEWONLY,
    OPTION_LOGIN_AUTH,
    OPTION_USE_GETIMAGE
} VNCOpts;

static const OptionInfoRec VNCOptions[] = {
    {OPTION_USEVNC,                "usevnc",        OPTV_BOOLEAN,        {0}, FALSE },
    {OPTION_RFBPORT,                "rfbport",        OPTV_INTEGER,         {0}, FALSE },
    {OPTION_HTTPPORT,                "httpport",        OPTV_INTEGER,         {0}, FALSE },
    {OPTION_ALWAYS_SHARED,         "alwaysshared", OPTV_BOOLEAN,          {0}, FALSE },
    {OPTION_NEVER_SHARED,         "nevershared",         OPTV_BOOLEAN,          {0}, FALSE },
    {OPTION_DONT_DISCONNECT,         "dontdisconnect", OPTV_BOOLEAN,        {0}, FALSE },
    {OPTION_HTTPDIR,                "httpdir",        OPTV_STRING,         {0}, FALSE },
    {OPTION_PASSWD_FILE,        "rfbauth",        OPTV_STRING,         {0}, FALSE },
    {OPTION_USER_ACCEPT,        "useraccept",        OPTV_BOOLEAN,         {0}, FALSE },
    {OPTION_LOCALHOST,                "localhost",        OPTV_BOOLEAN,         {0}, FALSE },
    {OPTION_INTERFACE,                "interface",        OPTV_STRING,         {0}, FALSE },
    {OPTION_VIEWONLY,                "viewonly",        OPTV_BOOLEAN,         {0}, FALSE },
    {OPTION_LOGIN_AUTH,                "loginauth",        OPTV_BOOLEAN,         {0}, FALSE },
    {OPTION_USE_GETIMAGE,        "usegetimage",        OPTV_BOOLEAN,         {0}, FALSE },
    { -1,                        NULL,                OPTV_NONE,         {0}, FALSE }
};

/*ARGSUSED*/
static const OptionInfoRec *
VNCAvailableOptions(void *unused)
{
    return (VNCOptions);
}

/*
 * rfbLog prints a time-stamped message to the log file (stderr).
 */

void rfbLog(char *format, ...)
{
    va_list ap;
    char buf[256];
    time_t clock;

    time(&clock);
    strftime(buf, 255, "%d/%m/%Y %H:%M:%S ", localtime(&clock));
    xf86DrvMsgVerb(-1, X_INFO, 1, buf);

    va_start(ap, format);
    xf86VDrvMsgVerb(-1, X_NONE, 1, format, ap);
    va_end(ap);
}

void rfbLogPerror(char *str)
{
    rfbLog("");
    perror(str);
}

Bool
VNCInit(ScreenPtr pScreen, unsigned char *FBStart)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    VisualPtr visual;
    vncScreenPtr pScreenPriv;
    OptionInfoPtr options;
    char *interface_str = NULL;
    miPointerScreenPtr PointPriv;
    xf86CursorScreenPtr xf86CursorPriv;
#ifdef RENDER
    PictureScreenPtr        ps;
#endif

    if (!FBStart)
        return FALSE;

#ifndef XFree86LOADER
    if (VNCGeneration != serverGeneration) {
        VncExtensionInit();
        VNCGeneration = serverGeneration;
    }
#endif

    if (!AllocateGCPrivate(pScreen, rfbGCIndex, sizeof(rfbGCRec)))
        return FALSE;

    if (!(pScreenPriv = xalloc(sizeof(vncScreenRec))))
        return FALSE;

    pScreen->devPrivates[vncScreenPrivateIndex].ptr = (pointer)pScreenPriv;

    options = xnfalloc(sizeof(VNCOptions));
    (void)memcpy(options, VNCOptions, sizeof(VNCOptions));
    xf86ProcessOptions(pScrn->scrnIndex, pScrn->options, options);

    if (xf86ReturnOptValBool(options, OPTION_USEVNC, FALSE)) {
        xf86DrvMsg(pScrn->scrnIndex, X_CONFIG, "VNC enabled\n");
    } else {
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "VNC disabled\n");
        xfree(options);
        return FALSE;
    }
 
    pScreenPriv->rfbAuthTries = 0;
    pScreenPriv->rfbAuthTooManyTries = FALSE;
    pScreenPriv->timer = NULL;
    pScreenPriv->udpPort = 0;
    pScreenPriv->rfbListenSock = -1;
    pScreenPriv->udpSock = -1;
    pScreenPriv->udpSockConnected = FALSE;
    pScreenPriv->httpListenSock = -1;
    pScreenPriv->httpSock = -1;
    pScreenPriv->maxFd = 0;
    pScreenPriv->rfbAuthPasswdFile = NULL;
    pScreenPriv->httpDir = NULL;
    pScreenPriv->rfbInstalledColormap = NULL;
    pScreenPriv->interface.s_addr = htonl (INADDR_ANY);
    
#ifdef SHAREDAPP
    sharedapp_Init(&pScreenPriv->sharedApp);
#endif

    pScreenPriv->rfbPort = 0;
    xf86GetOptValInteger(options, OPTION_RFBPORT, &pScreenPriv->rfbPort);
    pScreenPriv->httpPort = 0;
    xf86GetOptValInteger(options, OPTION_HTTPPORT, &pScreenPriv->httpPort);
    pScreenPriv->rfbAuthPasswdFile = 
                        xf86GetOptValString(options, OPTION_PASSWD_FILE);
    pScreenPriv->httpDir =
                        xf86GetOptValString(options, OPTION_HTTPDIR);
    pScreenPriv->rfbAlwaysShared = FALSE;
    xf86GetOptValBool(options, OPTION_ALWAYS_SHARED, 
                                                &pScreenPriv->rfbAlwaysShared);
    pScreenPriv->rfbNeverShared = FALSE;
    xf86GetOptValBool(options, OPTION_NEVER_SHARED, 
                                                &pScreenPriv->rfbNeverShared);
    pScreenPriv->rfbUserAccept = FALSE;
    xf86GetOptValBool(options, OPTION_USER_ACCEPT,
                                                &pScreenPriv->rfbUserAccept);
    pScreenPriv->useGetImage = FALSE;
    xf86GetOptValBool(options, OPTION_USE_GETIMAGE,
                                                &pScreenPriv->useGetImage);
    pScreenPriv->rfbViewOnly = FALSE;
    xf86GetOptValBool(options, OPTION_VIEWONLY,
                                                &pScreenPriv->rfbViewOnly);
    pScreenPriv->rfbDontDisconnect = FALSE;
    xf86GetOptValBool(options, OPTION_DONT_DISCONNECT, 
                                                &pScreenPriv->rfbDontDisconnect);
    pScreenPriv->loginAuthEnabled = FALSE;
    xf86GetOptValBool(options, OPTION_LOGIN_AUTH, 
                                                &pScreenPriv->loginAuthEnabled);

    if (xf86ReturnOptValBool(options, OPTION_LOCALHOST, FALSE))
        pScreenPriv->interface.s_addr = htonl (INADDR_LOOPBACK);

    interface_str = xf86GetOptValString(options, OPTION_INTERFACE);

    if (interface_str && pScreenPriv->interface.s_addr == htonl(INADDR_ANY)) {
        Bool failed = FALSE;
        struct in_addr got;
        unsigned long octet;
        char *p = interface_str, *end;
        int q;

        for (q = 0; q < 4; q++) {
            octet = strtoul (p, &end, 10);

            if (p == end || octet > 255)
                failed = TRUE;

            if ((q < 3 && *end != '.') ||
                (q == 3 && *end != '\0'))
                failed = TRUE;

            got.s_addr = (got.s_addr << 8) | octet;
            p = end + 1;
        }

        if (!failed)
            pScreenPriv->interface.s_addr = htonl (got.s_addr);
        else
            xf86DrvMsg(pScrn->scrnIndex, X_INFO, "VNC interface option malformed, not using.\n");
    }

    xfree(options);

    if (!VNC_LAST_CLIENT_ID)
            VNC_LAST_CLIENT_ID = MakeAtom("VNC_LAST_CLIENT_ID",
                                  strlen("VNC_LAST_CLIENT_ID"), TRUE);
    if (!VNC_CONNECT)
            VNC_CONNECT = MakeAtom("VNC_CONNECT", strlen("VNC_CONNECT"), TRUE);

    rfbInitSockets(pScreen);
    if (inetdSock == -1)
            httpInitSockets(pScreen);
   
#ifdef CORBA
    initialiseCORBA(argc, argv, desktopName);
#endif

    pScreenPriv->width = pScrn->virtualX;
    pScreenPriv->height = pScrn->virtualY;
    pScreenPriv->depth = pScrn->depth;
    pScreenPriv->paddedWidthInBytes = PixmapBytePad(pScrn->displayWidth, pScrn->depth);
    pScreenPriv->bitsPerPixel = rfbBitsPerPixel(pScrn->depth);
    pScreenPriv->pfbMemory = FBStart;
    pScreenPriv->oldpfbMemory = FBStart;

    pScreenPriv->cursorIsDrawn = TRUE;
    pScreenPriv->dontSendFramebufferUpdate = FALSE;

    pScreenPriv->CloseScreen = pScreen->CloseScreen;
    pScreenPriv->CreateGC = pScreen->CreateGC;
    pScreenPriv->PaintWindowBackground = pScreen->PaintWindowBackground;
    pScreenPriv->PaintWindowBorder = pScreen->PaintWindowBorder;
    pScreenPriv->CopyWindow = pScreen->CopyWindow;
    pScreenPriv->ClearToBackground = pScreen->ClearToBackground;
    pScreenPriv->RestoreAreas = pScreen->RestoreAreas;
    pScreenPriv->WakeupHandler = pScreen->WakeupHandler;
    pScreenPriv->EnableDisableFBAccess = pScrn->EnableDisableFBAccess;
    pScreenPriv->InstallColormap = pScreen->InstallColormap;
    pScreenPriv->UninstallColormap = pScreen->UninstallColormap;
    pScreenPriv->ListInstalledColormaps = pScreen->ListInstalledColormaps;
    pScreenPriv->StoreColors = pScreen->StoreColors;
    pScreenPriv->DisplayCursor = pScreen->DisplayCursor;
#ifdef CHROMIUM
    pScreenPriv->RealizeWindow = pScreen->RealizeWindow;
    pScreenPriv->UnrealizeWindow = pScreen->UnrealizeWindow;
    pScreenPriv->DestroyWindow = pScreen->DestroyWindow;
    pScreenPriv->PositionWindow = pScreen->PositionWindow;
    pScreenPriv->ResizeWindow = pScreen->ResizeWindow;
    pScreenPriv->ClipNotify = pScreen->ClipNotify;
#endif
#ifdef RENDER
    ps = GetPictureScreenIfSet(pScreen);
    if (ps)
            pScreenPriv->Composite = ps->Composite;
#endif
    pScreen->CloseScreen = rfbCloseScreen;
    pScreen->CreateGC = rfbCreateGC;
    pScreen->PaintWindowBackground = rfbPaintWindowBackground;
    pScreen->PaintWindowBorder = rfbPaintWindowBorder;
    pScreen->CopyWindow = rfbCopyWindow;
    pScreen->ClearToBackground = rfbClearToBackground;
    pScreen->RestoreAreas = rfbRestoreAreas;
    pScreen->WakeupHandler = rfbWakeupHandler;
    pScrn->EnableDisableFBAccess = rfbEnableDisableFBAccess;
    pScreen->InstallColormap = rfbInstallColormap;
    pScreen->UninstallColormap = rfbUninstallColormap;
    pScreen->ListInstalledColormaps = rfbListInstalledColormaps;
    pScreen->StoreColors = rfbStoreColors;
    pScreen->DisplayCursor = vncDisplayCursor; /* it's defined in here */

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

    for (visual = pScreen->visuals; visual->vid != pScreen->rootVisual; visual++)
        ;

    if (!visual) {
        ErrorF("rfbScreenInit: couldn't find root visual\n");
        return FALSE;
    }

    pScreenPriv->rfbServerFormat.bitsPerPixel = pScrn->bitsPerPixel;
    pScreenPriv->rfbServerFormat.depth = pScrn->depth;
    pScreenPriv->rfbServerFormat.bigEndian = !(*(char *)&rfbEndianTest);
    pScreenPriv->rfbServerFormat.trueColour = (visual->class == TrueColor);
    if (pScreenPriv->rfbServerFormat.trueColour) {
        pScreenPriv->rfbServerFormat.redMax = visual->redMask >> visual->offsetRed;
        pScreenPriv->rfbServerFormat.greenMax = visual->greenMask >> visual->offsetGreen;
        pScreenPriv->rfbServerFormat.blueMax = visual->blueMask >> visual->offsetBlue;
        pScreenPriv->rfbServerFormat.redShift = visual->offsetRed;
        pScreenPriv->rfbServerFormat.greenShift = visual->offsetGreen;
        pScreenPriv->rfbServerFormat.blueShift = visual->offsetBlue;
    } else {
        pScreenPriv->rfbServerFormat.redMax
            = pScreenPriv->rfbServerFormat.greenMax 
            = pScreenPriv->rfbServerFormat.blueMax = 0;
        pScreenPriv->rfbServerFormat.redShift
            = pScreenPriv->rfbServerFormat.greenShift 
            = pScreenPriv->rfbServerFormat.blueShift = 0;
    }

    PointPriv = pScreen->devPrivates[miPointerScreenIndex].ptr;

    pScreenPriv->spriteFuncs = PointPriv->spriteFuncs;
    PointPriv->spriteFuncs = &vncCursorSpriteFuncs;

    if (xf86LoaderCheckSymbol("xf86CursorScreenIndex")) {
        int *si;

        si = LoaderSymbol("xf86CursorScreenIndex");

        if (*si != -1) {
                xf86CursorPriv = pScreen->devPrivates[*si].ptr;

            if (xf86CursorPriv) {
                pScreenPriv->UseHWCursor = xf86CursorPriv->CursorInfoPtr->UseHWCursor;
                xf86CursorPriv->CursorInfoPtr->UseHWCursor = vncUseHWCursor;
                pScreenPriv->UseHWCursorARGB = xf86CursorPriv->CursorInfoPtr->UseHWCursorARGB;
                xf86CursorPriv->CursorInfoPtr->UseHWCursorARGB = vncUseHWCursorARGB;
                pScreenPriv->SWCursor = &xf86CursorPriv->SWCursor;
            }
        }
    }

    return TRUE;
}

/****** miPointerSpriteFunctions *******/

static Bool
vncCursorRealizeCursor(ScreenPtr pScreen, CursorPtr pCurs)
{
    vncScreenPtr   pScreenPriv = VNCPTR(pScreen);

    return (*pScreenPriv->spriteFuncs->RealizeCursor)(pScreen, pCurs);
}

static Bool
vncCursorUnrealizeCursor(ScreenPtr pScreen, CursorPtr pCurs)
{
    vncScreenPtr   pScreenPriv = VNCPTR(pScreen);

    return (*pScreenPriv->spriteFuncs->UnrealizeCursor)(pScreen, pCurs);
}

static void
vncCursorSetCursor(ScreenPtr pScreen, CursorPtr pCurs, int x, int y)
{
    vncScreenPtr   pScreenPriv = VNCPTR(pScreen);

    pScreenPriv->pCurs = pCurs;

#if 0
    if (pCurs == NullCursor) {        /* means we're supposed to remove the cursor */
        if (pScreenPriv->cursorIsDrawn)
            pScreenPriv->cursorIsDrawn = FALSE;
        return;
    } 

    pScreenPriv->cursorIsDrawn = TRUE;
#endif

    (*pScreenPriv->spriteFuncs->SetCursor)(pScreen, pCurs, x, y);
}

static void
vncCursorMoveCursor(ScreenPtr pScreen, int x, int y)
{
    vncScreenPtr   pScreenPriv = VNCPTR(pScreen);
    rfbClientPtr cl;

    for (cl = rfbClientHead; cl ; cl = cl->next) {
        if (cl->enableCursorPosUpdates)
            cl->cursorWasMoved = TRUE;
    }

    (*pScreenPriv->spriteFuncs->MoveCursor)(pScreen, x, y);
}

Bool
vncUseHWCursor(pScreen, pCursor)
    ScreenPtr pScreen;
    CursorPtr pCursor;
{
    vncScreenPtr   pScreenPriv = VNCPTR(pScreen);
    rfbClientPtr cl;

    if (!*pScreenPriv->UseHWCursor) {
        /* If the driver doesn't have a UseHWCursor function we're
         * basically saying we can have the HWCursor on all the time 
         */
            pScreenPriv->SWCursor = (Bool *)FALSE;
        return TRUE;
    }

    pScreenPriv->SWCursor = (Bool *)FALSE;

    /* If someone's connected, we revert to software cursor */
    for (cl = rfbClientHead; cl ; cl = cl->next) {
        if (!cl->enableCursorShapeUpdates)
            pScreenPriv->SWCursor = (Bool *)TRUE;
    }
    
    if (pScreenPriv->SWCursor == (Bool *)TRUE)
        return FALSE;

    return (*pScreenPriv->UseHWCursor)(pScreen, pCursor);
}

#ifdef ARGB_CURSOR
#include "cursorstr.h"

Bool
vncUseHWCursorARGB(pScreen, pCursor)
    ScreenPtr pScreen;
    CursorPtr pCursor;
{
    vncScreenPtr   pScreenPriv = VNCPTR(pScreen);
    rfbClientPtr cl;

    if (!*pScreenPriv->UseHWCursorARGB) {
            pScreenPriv->SWCursor = (Bool *)TRUE;
        return FALSE;
    }

    pScreenPriv->SWCursor = (Bool *)FALSE;

    /* If someone's connected, we revert to software cursor */
    for (cl = rfbClientHead; cl ; cl = cl->next) {
        if (!cl->enableCursorShapeUpdates)
            pScreenPriv->SWCursor = (Bool *)TRUE;
    }
    
    if (pScreenPriv->SWCursor == (Bool *)TRUE)
        return FALSE;

    return (*pScreenPriv->UseHWCursorARGB)(pScreen, pCursor);
}
#endif

static Bool
vncDisplayCursor(pScreen, pCursor)
    ScreenPtr pScreen;
    CursorPtr pCursor;
{
    vncScreenPtr   pScreenPriv = VNCPTR(pScreen);
    rfbClientPtr cl;
    Bool ret;

    pScreen->DisplayCursor = pScreenPriv->DisplayCursor;

    for (cl = rfbClientHead; cl ; cl = cl->next) {
        if (cl->enableCursorShapeUpdates)
            cl->cursorWasChanged = TRUE;
    }

    ret = (*pScreen->DisplayCursor)(pScreen, pCursor);

    pScreen->DisplayCursor = vncDisplayCursor;

    return ret;
}

static void
rfbWakeupHandler (
    int         i,        
    pointer     blockData,
    unsigned long err,
    pointer     pReadmask
){
    ScreenPtr      pScreen = screenInfo.screens[i];
    vncScreenPtr   pScreenPriv = VNCPTR(pScreen);
    ScrnInfoPtr    pScrn = xf86Screens[pScreen->myNum];
    int sigstate = xf86BlockSIGIO();

    rfbRootPropertyChange(pScreen); /* Check clipboard */

    if (pScrn->vtSema) {
            rfbCheckFds(pScreen);
            httpCheckFds(pScreen);
#ifdef CORBA
            corbaCheckFds();
#endif
    } else {
            rfbCheckFds(pScreen);
    }

    xf86UnblockSIGIO(sigstate);                    

    pScreen->WakeupHandler = pScreenPriv->WakeupHandler;
    (*pScreen->WakeupHandler) (i, blockData, err, pReadmask);
    pScreen->WakeupHandler = rfbWakeupHandler;
}

#ifdef XFree86LOADER
static MODULESETUPPROTO(vncSetup);

static XF86ModuleVersionInfo vncVersRec =
{
        "vnc",
        "xf4vnc Project, see http://xf4vnc.sf.net (4.3.0.999)",
        MODINFOSTRING1,
        MODINFOSTRING2,
        XF86_VERSION_CURRENT,
        1, 1, 0,
        ABI_CLASS_EXTENSION,
        ABI_EXTENSION_VERSION,
        MOD_CLASS_EXTENSION,
        {0,0,0,0}
};

XF86ModuleData vncModuleData = { &vncVersRec, vncSetup, NULL };

ModuleInfoRec VNC = {
    1,
    "VNC",
    NULL,
    0,
    VNCAvailableOptions,
};


ExtensionModule vncExtensionModule = {
        VncExtensionInit,
        "VNC",
        NULL,
        NULL,
        NULL
};


/*ARGSUSED*/
static pointer
vncSetup(pointer Module, pointer Options, int *ErrorMajor, int *ErrorMinor)
{
    static Bool Initialised = FALSE;

    if (!Initialised) {
        Initialised = TRUE;
#ifndef REMOVE_LOADER_CHECK_MODULE_INFO
        if (xf86LoaderCheckSymbol("xf86AddModuleInfo"))
#endif
        xf86AddModuleInfo(&VNC, Module);
    }

    LoadExtension(&vncExtensionModule, FALSE);
    vncInitMouse();
    vncInitKeyb();
    xf86Msg(X_INFO, "Ignore errors regarding the loading of the rfbmouse & rfbkeyb drivers\n");

    return (pointer)TRUE;
}
#endif
