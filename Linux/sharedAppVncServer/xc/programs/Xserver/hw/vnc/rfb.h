/*
 * rfb.h - header file for RFB DDX implementation.
 *
 * Modified for SharedAppVnc by Grant Wallace
 * Modified for XFree86 4.x by Alan Hourihane <alanh@fairlite.demon.co.uk>
 */

/*
 *  Copyright (C) 2000-2004 Const Kaplinsky.  All Rights Reserved.
 *  Copyright (C) 2000 Tridia Corporation.  All Rights Reserved.
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

#include <zlib.h>
#if 0 && XFREE86VNC /* not yet */
#include "xf86.h"
#include "xf86_OSproc.h"
#include "xf86_ansic.h"
#endif
#include "scrnintstr.h"
#include "colormapst.h"
#include "dixstruct.h"
#include "gcstruct.h"
#include "regionstr.h"
#include "windowstr.h"
#include "dixfontstr.h"
#if 1 /* && !XFREE86VNC */
#include "osdep.h"
#endif
#include <rfbproto.h>
#include <vncauth.h>
#define _VNC_SERVER
#include <X11/extensions/vnc.h>
#ifdef RENDER
#include "picturestr.h"
#endif
#ifdef SHAREDAPP
#include "sharedapp.h"
#endif

#if defined(sun) || defined(hpux)
#define SOCKLEN_T        int
#else
#define SOCKLEN_T        socklen_t
#endif

/* It's a good idea to keep these values a bit greater than required. */
#define MAX_ENCODINGS 10
#define MAX_SECURITY_TYPES 4
#define MAX_TUNNELING_CAPS 16
#define MAX_AUTH_CAPS 16

/* 
 * HTTP_BUF_SIZE for http transfers
 */
#define HTTP_BUF_SIZE 32768

/*
 * UPDATE_BUF_SIZE must be big enough to send at least one whole line of the
 * framebuffer.  So for a max screen width of say 2K with 32-bit pixels this
 * means 8K minimum.
 */
#define UPDATE_BUF_SIZE 30000

#if XFREE86VNC
#include "xf86.h"
#include "vnc.h"
#define VNCSCREENPTR(ptr) \
        vncScreenPtr pVNC = VNCPTR(ptr)
#else
#define VNCSCREENPTR(ptr) \
/* Soon \
        rfbScreenInfoPtr pVNC = rfbScreen[ptr->myNum] \
*/ \
        rfbScreenInfoPtr pVNC = &rfbScreen
#endif

/*
 * Per-screen (framebuffer) structure.  There is only one of these, since we
 * don't allow the X server to have multiple screens.
 */

typedef struct
{
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
    Bool                useGetImage;
    Bool                noCursor;
    Bool                rfbAlwaysShared;
    Bool                rfbNeverShared;
    Bool                rfbDontDisconnect;
    Bool                rfbUserAccept;
    Bool                rfbViewOnly;
    ColormapPtr                savedColormap;
    ColormapPtr         rfbInstalledColormap;
    rfbPixelFormat        rfbServerFormat;
    Bool                rfbAuthTooManyTries;
    int                        rfbAuthTries;
    Bool                loginAuthEnabled;
    struct in_addr        interface;
    OsTimerPtr                 timer;
    char updateBuf[UPDATE_BUF_SIZE];
    int ublen;
    int width;
    int paddedWidthInBytes;
    int height;
    int depth;
    int bitsPerPixel;
    int sizeInBytes;
    unsigned char *pfbMemory;
    Pixel blackPixel;
    Pixel whitePixel;

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
    SharedAppVnc                        sharedApp;
#endif


} rfbScreenInfo, *rfbScreenInfoPtr;


/*
 * rfbTranslateFnType is the type of translation functions.
 */

struct rfbClientRec;
typedef void (*rfbTranslateFnType)(ScreenPtr pScreen, 
                                   char *table, rfbPixelFormat *in,
                                   rfbPixelFormat *out,
                                   unsigned char *iptr, char *optr,
                                   int bytesBetweenInputLines,
                                   int width, int height,
                                   int x, int y);


/*
 * Per-client structure.
 */

typedef struct rfbClientRec {
    int sock;
    char *host;
    char *login;

    int protocol_minor_ver;        /* RFB protocol minor version in use. */
    Bool protocol_tightvnc;        /* TightVNC protocol extensions enabled */

    /* Possible client states: */

    enum {
        RFB_PROTOCOL_VERSION,        /* establishing protocol version */
        RFB_SECURITY_TYPE,        /* negotiating security (RFB v.3.7) */
        RFB_TUNNELING_TYPE,        /* establishing tunneling (RFB v.3.7t) */
        RFB_AUTH_TYPE,                /* negotiating authentication (RFB v.3.7t) */
        RFB_AUTHENTICATION,        /* authenticating (VNC authentication) */
        RFB_INITIALISATION,        /* sending initialisation messages */
        RFB_NORMAL                /* normal protocol messages */
    } state;

    Bool viewOnly;                /* Do not accept input from this client. */

    Bool reverseConnection;

    Bool readyForSetColourMapEntries;

    Bool useCopyRect;
    int preferredEncoding;
    int correMaxWidth, correMaxHeight;

    /* The list of security types sent to this client (protocol 3.7).
       Note that the first entry is the number of list items following. */

    CARD8 securityTypes[MAX_SECURITY_TYPES + 1];

    /* Lists of capability codes sent to clients. We remember these
       lists to restrict clients from choosing those tunneling and
       authentication types that were not advertised. */

    int nAuthCaps;
    CARD32 authCaps[MAX_AUTH_CAPS];

    /* This is not useful while we don't support tunneling:
    int nTunnelingCaps;
    CARD32 tunnelingCaps[MAX_TUNNELING_CAPS]; */

    /* The following member is only used during VNC authentication */

    CARD8 authChallenge[CHALLENGESIZE];

    /* The following members represent the update needed to get the client's
       framebuffer from its present state to the current state of our
       framebuffer.

       If the client does not accept CopyRect encoding then the update is
       simply represented as the region of the screen which has been modified
       (modifiedRegion).

       If the client does accept CopyRect encoding, then the update consists of
       two parts.  First we have a single copy from one region of the screen to
       another (the destination of the copy is copyRegion), and second we have
       the region of the screen which has been modified in some other way
       (modifiedRegion).

       Although the copy is of a single region, this region may have many
       rectangles.  When sending an update, the copyRegion is always sent
       before the modifiedRegion.  This is because the modifiedRegion may
       overlap parts of the screen which are in the source of the copy.

       In fact during normal processing, the modifiedRegion may even overlap
       the destination copyRegion.  Just before an update is sent we remove
       from the copyRegion anything in the modifiedRegion. */

    RegionRec copyRegion;        /* the destination region of the copy */
    int copyDX, copyDY;                /* the translation by which the copy happens */

    RegionRec modifiedRegion;        /* the region of the screen modified in any
                                   other way */

    /* As part of the FramebufferUpdateRequest, a client can express interest
       in a subrectangle of the whole framebuffer.  This is stored in the
       requestedRegion member.  In the normal case this is the whole
       framebuffer if the client is ready, empty if it's not. */

    RegionRec requestedRegion;

    /* The following members represent the state of the "deferred update" timer
       - when the framebuffer is modified and the client is ready, in most
       cases it is more efficient to defer sending the update by a few
       milliseconds so that several changes to the framebuffer can be combined
       into a single update. */

    Bool deferredUpdateScheduled;
    OsTimerPtr deferredUpdateTimer;

    /* translateFn points to the translation function which is used to copy
       and translate a rectangle from the framebuffer to an output buffer. */

    rfbTranslateFnType translateFn;

    char *translateLookupTable;

    rfbPixelFormat format;

    /* statistics */

    int rfbBytesSent[MAX_ENCODINGS];
    int rfbRectanglesSent[MAX_ENCODINGS];
    int rfbLastRectMarkersSent;
    int rfbLastRectBytesSent;
    int rfbCursorShapeBytesSent;
    int rfbCursorShapeUpdatesSent;
    int rfbCursorPosBytesSent;
    int rfbCursorPosUpdatesSent;
    int rfbFramebufferUpdateMessagesSent;
    int rfbRawBytesEquivalent;
    int rfbKeyEventsRcvd;
    int rfbPointerEventsRcvd;

    /* zlib encoding -- necessary compression state info per client */

    struct z_stream_s compStream;
    Bool compStreamInited;

    CARD32 zlibCompressLevel;

    /* tight encoding -- preserve zlib streams' state for each client */

    z_stream zsStruct[4];
    Bool zsActive[4];
    int zsLevel[4];
    int tightCompressLevel;
    int tightQualityLevel;

    Bool enableLastRectEncoding;   /* client supports LastRect encoding */
    Bool enableCursorShapeUpdates; /* client supports cursor shape updates */
    Bool enableCursorPosUpdates;   /* client supports PointerPos updates */
#ifdef CHROMIUM
    Bool enableChromiumEncoding;   /* client supports Chromium encoding */
#endif
    Bool useRichCursorEncoding;    /* rfbEncodingRichCursor is preferred */
    Bool cursorWasChanged;         /* cursor shape update should be sent */
    Bool cursorWasMoved;           /* cursor position update should be sent */

    int cursorX, cursorY;          /* client's cursor position */

    struct rfbClientRec *next;

    ScreenPtr pScreen;
    int userAccepted;

#ifdef CHROMIUM
    unsigned int chromium_port;
#endif

#ifdef SHAREDAPP
    Bool supportsSharedAppEncoding;
    unsigned int multicursor;
#endif

} rfbClientRec, *rfbClientPtr;

#ifdef CHROMIUM
typedef struct CRWindowTable {
        unsigned long CRwinId;
        unsigned long XwinId;
        BoxPtr clipRects;
        int numRects;
        struct CRWindowTable *next;
} CRWindowTable, *CRWindowTablePtr;

extern struct CRWindowTable *windowTable;
#endif

/*
 * This macro is used to test whether there is a framebuffer update needing to
 * be sent to the client.
 */

#define FB_UPDATE_PENDING(cl)                                           \
    ((!(cl)->enableCursorShapeUpdates && !pVNC->cursorIsDrawn) ||   \
     ((cl)->enableCursorShapeUpdates && (cl)->cursorWasChanged) ||      \
     ((cl)->enableCursorPosUpdates && (cl)->cursorWasMoved) ||          \
     REGION_NOTEMPTY(((cl)->pScreen),&(cl)->copyRegion) ||                    \
     REGION_NOTEMPTY(((cl)->pScreen),&(cl)->modifiedRegion))

/*
 * This macro creates an empty region (ie. a region with no areas) if it is
 * given a rectangle with a width or height of zero. It appears that 
 * REGION_INTERSECT does not quite do the right thing with zero-width
 * rectangles, but it should with completely empty regions.
 */

#define SAFE_REGION_INIT(pscreen, preg, rect, size)          \
{                                                            \
      if ( ( (rect) ) &&                                     \
           ( ( (rect)->x2 == (rect)->x1 ) ||                 \
             ( (rect)->y2 == (rect)->y1 ) ) ) {              \
          REGION_NULL( (pscreen), (preg) );                      \
      } else {                                               \
          REGION_INIT( (pscreen), (preg), (rect), (size) );  \
      }                                                      \
}

/*
 * An rfbGCRec is where we store the pointers to the original GC funcs and ops
 * which we wrap (NULL means not wrapped).
 */

typedef struct {
    GCFuncs *wrapFuncs;
    GCOps *wrapOps;
} rfbGCRec, *rfbGCPtr;



/*
 * Macros for endian swapping.
 */

#define Swap16(s) ((((s) & 0xff) << 8) | (((s) >> 8) & 0xff))

#define Swap32(l) (((l) >> 24) | \
                   (((l) & 0x00ff0000) >> 8)  | \
                   (((l) & 0x0000ff00) << 8)  | \
                   ((l) << 24))

static const int rfbEndianTest = 1;

#define Swap16IfLE(s) (*(const char *)&rfbEndianTest ? Swap16(s) : (s))

#define Swap32IfLE(l) (*(const char *)&rfbEndianTest ? Swap32(l) : (l))


/*
 * Macro to fill in an rfbCapabilityInfo structure (protocol 3.130).
 * Normally, using macros is no good, but this macro saves us from
 * writing constants twice -- it constructs signature names from codes.
 * Note that "code_sym" argument should be a single symbol, not an expression.
 */

#define SetCapInfo(cap_ptr, code_sym, vendor)                \
{                                                        \
    rfbCapabilityInfo *pcap;                                \
    pcap = (cap_ptr);                                        \
    pcap->code = Swap32IfLE(code_sym);                        \
    memcpy(pcap->vendorSignature, (vendor),                \
           sz_rfbCapabilityInfoVendor);                        \
    memcpy(pcap->nameSignature, sig_##code_sym,                \
           sz_rfbCapabilityInfoName);                        \
}


/* init.c */

extern char *desktopName;
extern char rfbThisHost[];
extern Atom VNC_LAST_CLIENT_ID;

extern rfbScreenInfo rfbScreen;
extern int rfbGCIndex;

extern int inetdSock;

extern int rfbBitsPerPixel(int depth);
extern void rfbLog(char *format, ...);
extern void rfbLogPerror(char *str);


/* sockets.c */

extern int rfbMaxClientWait;

extern Bool rfbInitSockets(ScreenPtr pScreen);
extern void rfbDisconnectUDPSock(ScreenPtr pScreen);
extern void rfbCloseSock(ScreenPtr pScreen, int sock);
extern void rfbCheckFds(ScreenPtr pScreen);
extern void rfbWaitForClient(int sock);
extern int rfbConnect(ScreenPtr pScreen, char *host, int port);

extern int ReadExact(int sock, char *buf, int len);
extern int WriteExact(int sock, char *buf, int len);
extern int ListenOnTCPPort(ScreenPtr pScreen, int port);
extern int ListenOnUDPPort(ScreenPtr pScreen, int port);
extern int ConnectToTcpAddr(char *host, int port);


/* cmap.c */


extern int rfbListInstalledColormaps(ScreenPtr pScreen, Colormap *pmaps);
extern void rfbInstallColormap(ColormapPtr pmap);
extern void rfbUninstallColormap(ColormapPtr pmap);
extern void rfbStoreColors(ColormapPtr pmap, int ndef, xColorItem *pdefs);


/* draw.c */

extern int rfbDeferUpdateTime;

extern void
rfbComposite(
    CARD8 op,
    PicturePtr pSrc,
    PicturePtr pMask,
    PicturePtr pDst,
    INT16 xSrc,
    INT16 ySrc,
    INT16 xMask,
    INT16 yMask,
    INT16 xDst,
    INT16 yDst,
    CARD16 width,
    CARD16 height
);

extern void rfbGlyphs(
    CARD8 op,
    PicturePtr pSrc,
    PicturePtr pDst,
    PictFormatPtr maskFormat,
    INT16 xSrc,
    INT16 ySrc,
    int nlistInit,
    GlyphListPtr listInit,
    GlyphPtr *glyphsInit
);
extern Bool rfbCloseScreen(int,ScreenPtr);
extern Bool rfbCreateGC(GCPtr);
extern void rfbPaintWindowBackground(WindowPtr, RegionPtr, int what);
extern void rfbPaintWindowBorder(WindowPtr, RegionPtr, int what);
extern void rfbCopyWindow(WindowPtr, DDXPointRec, RegionPtr);
#ifdef CHROMIUM
extern Bool rfbRealizeWindow(WindowPtr); 
extern Bool rfbUnrealizeWindow(WindowPtr); 
extern Bool rfbDestroyWindow(WindowPtr);
extern void rfbResizeWindow(WindowPtr, int x, int y, unsigned int w, unsigned int h, WindowPtr pSib);
extern Bool rfbPositionWindow(WindowPtr, int x, int y);
extern void rfbClipNotify(WindowPtr, int x, int y);
#endif
extern void rfbClearToBackground(WindowPtr, int x, int y, int w,
                                 int h, Bool generateExposures);
extern RegionPtr rfbRestoreAreas(WindowPtr, RegionPtr);
#ifdef SHAREDAPP
extern void rfbInvalidateRegion(ScreenPtr pScreen, RegionPtr reg);
#endif


/* cutpaste.c */

extern void rfbSetXCutText(char *str, int len);
extern void rfbGotXCutText(char *str, int len);


/* kbdptr.c */

extern Bool compatibleKbd;
extern unsigned char ptrAcceleration;

extern void PtrDeviceInit(void);
extern void PtrDeviceOn(DeviceIntPtr pDev);
extern void PtrDeviceOff(void);
extern void PtrDeviceControl(DeviceIntPtr dev, PtrCtrl *ctrl);
extern void PtrAddEvent(int buttonMask, int x, int y, rfbClientPtr cl);

extern void KbdDeviceInit(DeviceIntPtr pDevice, KeySymsPtr pKeySyms, CARD8 *pModMap);
extern void KbdDeviceOn(void);
extern void KbdDeviceOff(void);
extern void KbdAddEvent(Bool down, KeySym keySym, rfbClientPtr cl);
extern void KbdReleaseAllKeys(void);


/* rfbserver.c */


extern rfbClientPtr rfbClientHead;
extern rfbClientPtr pointerClient;

extern void rfbNewClientConnection(ScreenPtr pScreen, int sock);
extern rfbClientPtr rfbReverseConnection(ScreenPtr pScreen, char *host, int port);
extern void rfbRootPropertyChange(ScreenPtr pScreen);
extern void rfbClientConnectionGone(int sock);
extern void rfbProcessClientMessage(ScreenPtr pScreen, int sock);
extern void rfbClientConnFailed(rfbClientPtr cl, char *reason);
extern void rfbNewUDPConnection(int sock);
extern void rfbProcessUDPInput(ScreenPtr pScreen, int sock);
extern Bool rfbSendFramebufferUpdate(ScreenPtr pScreen, rfbClientPtr cl);
extern Bool rfbSendRectEncodingRaw(rfbClientPtr cl, int x,int y,int w,int h);
extern Bool rfbSendUpdateBuf(rfbClientPtr cl);
extern Bool rfbSendSetColourMapEntries(rfbClientPtr cl, int firstColour,
                                       int nColours);
extern void rfbSendBell(void);
extern void rfbSendServerCutText(char *str, int len);
#ifdef CHROMIUM
extern void rfbSendChromiumWindowShow(unsigned int winid, unsigned int show);
extern void rfbSendChromiumMoveResizeWindow(unsigned int winid, int x, int y, unsigned int w, unsigned int h);
extern void rfbSendChromiumClipList(unsigned int winid, BoxPtr pClipRects, int numClipRects);
#endif

/* translate.c */

extern Bool rfbEconomicTranslate;

extern void rfbTranslateNone(ScreenPtr pScreen, char *table, rfbPixelFormat *in,
                             rfbPixelFormat *out,
                             unsigned char *iptr, char *optr,
                             int bytesBetweenInputLines,
                             int width, int height,
                             int x, int y);
extern Bool rfbSetTranslateFunction(rfbClientPtr cl);
extern void rfbSetClientColourMaps(int firstColour, int nColours);
extern Bool rfbSetClientColourMap(rfbClientPtr cl, int firstColour,
                                  int nColours);


/* httpd.c */

extern Bool httpInitSockets(ScreenPtr pScreen);
extern void httpCheckFds(ScreenPtr pScreen);



/* auth.c */

extern void rfbAuthNewClient(rfbClientPtr cl);
extern void rfbProcessClientSecurityType(rfbClientPtr cl);
extern void rfbProcessClientTunnelingType(rfbClientPtr cl);
extern void rfbProcessClientAuthType(rfbClientPtr cl);
extern void rfbVncAuthProcessResponse(rfbClientPtr cl);

/* Functions to prevent too many successive authentication failures */
extern Bool rfbAuthConsiderBlocking(rfbClientPtr cl);
extern void rfbAuthUnblock(rfbClientPtr cl);
extern Bool rfbAuthIsBlocked(rfbClientPtr cl);

/* loginauth.c */

extern void rfbLoginAuthProcessClientMessage(rfbClientPtr cl);
  
/* rre.c */

extern Bool rfbSendRectEncodingRRE(rfbClientPtr cl, int x,int y,int w,int h);


/* corre.c */

extern Bool rfbSendRectEncodingCoRRE(rfbClientPtr cl, int x,int y,int w,int h);


/* hextile.c */

extern Bool rfbSendRectEncodingHextile(rfbClientPtr cl, int x, int y, int w,
                                       int h);


/* zlib.c */

/* Minimum zlib rectangle size in bytes.  Anything smaller will
 * not compress well due to overhead.
 */
#define VNC_ENCODE_ZLIB_MIN_COMP_SIZE (17)

/* Set maximum zlib rectangle size in pixels.  Always allow at least
 * two scan lines.
 */
#define ZLIB_MAX_RECT_SIZE (128*256)
#define ZLIB_MAX_SIZE(min) ((( min * 2 ) > ZLIB_MAX_RECT_SIZE ) ? \
                            ( min * 2 ) : ZLIB_MAX_RECT_SIZE )

extern Bool rfbSendRectEncodingZlib(rfbClientPtr cl, int x, int y, int w,
                                    int h);


/* tight.c */

#define TIGHT_DEFAULT_COMPRESSION  6

extern Bool rfbTightDisableGradient;

extern int rfbNumCodedRectsTight(rfbClientPtr cl, int x,int y,int w,int h);
extern Bool rfbSendRectEncodingTight(rfbClientPtr cl, int x,int y,int w,int h);


/* cursor.c */

extern Bool rfbSendCursorShape(rfbClientPtr cl, ScreenPtr pScreen);
extern Bool rfbSendCursorPos(rfbClientPtr cl, ScreenPtr pScreen);


/* stats.c */

extern void rfbResetStats(rfbClientPtr cl);
extern void rfbPrintStats(rfbClientPtr cl);


#ifdef SHAREDAPP
/* sharedapp.h */
extern void sharedapp_Init(SharedAppVncPtr shapp);
extern void sharedapp_HandleRequest(rfbClientPtr cl, unsigned int command, unsigned int id);
extern Bool sharedapp_RfbSendUpdates(ScreenPtr pScreen, rfbClientPtr cl);
extern Bool sharedapp_CheckPointer(rfbClientPtr cl, sharedAppPointerEventMsg *spe);
extern void sharedapp_InitReverseConnection(ScreenPtr pScreen);
extern void sharedapp_CheckForClosedWindows(ScreenPtr pScreen, rfbClientPtr rfbClientHead);
#endif

