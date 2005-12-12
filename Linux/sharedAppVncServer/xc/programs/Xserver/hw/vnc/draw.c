/*
 * draw.c - drawing routines for the RFB X server.  This is a set of
 * wrappers around the standard MI/MFB/CFB drawing routines which work out
 * to a fair approximation the region of the screen being modified by the
 * drawing.  If the RFB client is ready then the modified region of the screen
 * is sent to the client, otherwise the modified region will simply grow with
 * each drawing request until the client is ready.
 *
 * Modified for SharedAppVnc by Grant Wallace
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

Copyright (c) 1989  X Consortium

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
X CONSORTIUM BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of the X Consortium shall not be
used in advertising or otherwise to promote the sale, use or other dealings
in this Software without prior written authorization from the X Consortium.
*/

#include "rfb.h"

int rfbDeferUpdateTime = 40; /* ms */


/****************************************************************************/
/*
 * Macro definitions
 */
/****************************************************************************/

#define TRC(x) /* (rfbLog x) */

/* ADD_TO_MODIFIED_REGION adds the given region to the modified region for each
   client */

#define ADD_TO_MODIFIED_REGION(pScreen,reg)                                      \
  {                                                                              \
      rfbClientPtr cl;                                                              \
      for (cl = rfbClientHead; cl; cl = cl->next) {                              \
          REGION_UNION((pScreen),&cl->modifiedRegion,&cl->modifiedRegion,reg);\
      }                                                                              \
  }

/* SCHEDULE_FB_UPDATE is used at the end of each drawing routine to schedule an
   update to be sent to each client if there is one pending and the client is
   ready for it.  */

#define SCHEDULE_FB_UPDATE(pScreen,pVNC)                                \
  if (!pVNC->dontSendFramebufferUpdate) {                                \
      rfbClientPtr cl, nextCl;                                                \
      for (cl = rfbClientHead; cl; cl = nextCl) {                        \
          nextCl = cl->next;                                                \
          if (!cl->deferredUpdateScheduled && FB_UPDATE_PENDING(cl) &&         \
              REGION_NOTEMPTY(pScreen,&cl->requestedRegion))                 \
          {                                                                \
              rfbScheduleDeferredUpdate(pScreen, cl);                        \
          }                                                                \
      }                                                                        \
  }

/* function prototypes */

static void rfbScheduleDeferredUpdate(ScreenPtr pScreen, rfbClientPtr cl);
static void rfbCopyRegion(ScreenPtr pScreen, rfbClientPtr cl,
                          RegionPtr src, RegionPtr dst, int dx, int dy);
#ifdef DEBUG
static void PrintRegion(ScreenPtr pScreen, RegionPtr reg);
#endif

/* GC funcs */

static void rfbValidateGC(GCPtr, unsigned long /*changes*/, DrawablePtr);
static void rfbChangeGC(GCPtr, unsigned long /*mask*/);
static void rfbCopyGC(GCPtr /*src*/, unsigned long /*mask*/, GCPtr /*dst*/);
static void rfbDestroyGC(GCPtr);
static void rfbChangeClip(GCPtr, int /*type*/, pointer /*pValue*/,
                          int /*nrects*/);
static void rfbDestroyClip(GCPtr);
static void rfbCopyClip(GCPtr /*dst*/, GCPtr /*src*/);

/* GC ops */

static void rfbFillSpans(DrawablePtr pDrawable, GCPtr pGC, int nInit, DDXPointPtr pptInit, int *pwidthInit, int fSorted);
static void rfbSetSpans(DrawablePtr                 pDrawable, 
                            GCPtr                        pGC, 
                            char                        *psrc, 
                            register DDXPointPtr        ppt, 
                            int                        *pwidth, 
                            int                        nspans, 
                            int                        fSorted);
static void rfbPutImage();
static RegionPtr rfbCopyArea();
static RegionPtr rfbCopyPlane();
static void rfbPolyPoint();
static void rfbPolylines (DrawablePtr pDrawable, GCPtr pGC, int mode, int npt, DDXPointPtr ppts);
static void rfbPolySegment();
static void rfbPolyRectangle();
static void rfbPolyArc();
static void rfbFillPolygon();
static void rfbPolyFillRect();
static void rfbPolyFillArc();
static int rfbPolyText8();
static int rfbPolyText16();
static void rfbImageText8();
static void rfbImageText16();
static void rfbImageGlyphBlt();
static void rfbPolyGlyphBlt();
static void rfbPushPixels();


static GCFuncs rfbGCFuncs = {
    rfbValidateGC,
    rfbChangeGC,
    rfbCopyGC,
    rfbDestroyGC,
    rfbChangeClip,
    rfbDestroyClip,
    rfbCopyClip,
};


static GCOps rfbGCOps = {
    rfbFillSpans,        rfbSetSpans,        rfbPutImage,        
    rfbCopyArea,        rfbCopyPlane,        rfbPolyPoint,
    rfbPolylines,        rfbPolySegment,        rfbPolyRectangle,
    rfbPolyArc,                rfbFillPolygon,        rfbPolyFillRect,
    rfbPolyFillArc,        rfbPolyText8,        rfbPolyText16,
    rfbImageText8,        rfbImageText16,        rfbImageGlyphBlt,
    rfbPolyGlyphBlt,        rfbPushPixels
};



/****************************************************************************/
/*
 * Screen functions wrapper stuff
 */
/****************************************************************************/

#define SCREEN_PROLOGUE(scrn, field)                \
    ScreenPtr pScreen = scrn;                        \
    VNCSCREENPTR(pScreen);                 \
    pScreen->field = pVNC->field;

#define SCREEN_EPILOGUE(field, wrapper) \
    pScreen->field = wrapper;


/*
 * CloseScreen wrapper -- unwrap everything, free the private data
 * and call the wrapped CloseScreen function.
 */

Bool
rfbCloseScreen (int i, ScreenPtr pScreen)
{
    VNCSCREENPTR(pScreen);
#if XFREE86VNC
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
#endif
    int sock;

    for (sock = 0; sock <= pVNC->maxFd; sock++) {
        if (FD_ISSET(sock, &pVNC->allFds))
            if (sock != pVNC->rfbListenSock && sock != pVNC->httpListenSock) {
              rfbLog("CloseScreen: close socket\n");
              rfbCloseSock(pScreen, sock);
            }
    }

    if (pVNC->rfbListenSock > 0)
            if (close(pVNC->rfbListenSock))
                ErrorF("Close of port %d failed\n",pVNC->rfbPort);

    if (pVNC->httpListenSock > 0)
            if (close(pVNC->httpListenSock))
                ErrorF("Close of port %d failed\n",pVNC->httpPort);

    pScreen->CloseScreen = pVNC->CloseScreen;
    pScreen->CreateGC = pVNC->CreateGC;
    pScreen->PaintWindowBackground = pVNC->PaintWindowBackground;
    pScreen->PaintWindowBorder = pVNC->PaintWindowBorder;
    pScreen->CopyWindow = pVNC->CopyWindow;
    pScreen->ClearToBackground = pVNC->ClearToBackground;
    pScreen->RestoreAreas = pVNC->RestoreAreas;
    pScreen->WakeupHandler = pVNC->WakeupHandler;

#if XFREE86VNC
    pScreen->InstallColormap = pVNC->InstallColormap;
    pScreen->UninstallColormap = pVNC->UninstallColormap;
    pScreen->ListInstalledColormaps = pVNC->ListInstalledColormaps;
    pScreen->StoreColors = pVNC->StoreColors;
    pScrn->EnableDisableFBAccess = pVNC->EnableDisableFBAccess;

    xfree(pVNC);
#endif

    TRC((stderr,"Unwrapped screen functions\n"));

    return (*pScreen->CloseScreen) (i, pScreen);
}

#if XFREE86VNC
void
rfbEnableDisableFBAccess (int index, Bool enable)
{
    ScrnInfoPtr pScrn = xf86Screens[index];
    VNCSCREENPTR(pScrn->pScreen);

    /* 
     * Blank the screen for security while inputs are disabled.
     * When VT switching is fixed, we might be able to allow
     * control even when switched away. 
     */
    if (!enable) {
        WindowPtr pWin = WindowTable[index];
            ScreenPtr pScreen = pWin->drawable.pScreen;
            GCPtr pGC;
            xRectangle rect;

            rect.x = 0;
            rect.y = 0;
            rect.width = pScrn->virtualX;
            rect.height = pScrn->virtualY;

            if (!(pGC = GetScratchGC(pScreen->rootDepth, pScreen))) {
                ErrorF("Couldn't blank screen");
            } else {
            CARD32 attributes[2];
            attributes[0] = pScreen->whitePixel;
            attributes[1] = pScreen->blackPixel;
            (void)ChangeGC(pGC, GCForeground | GCBackground, attributes);

            ValidateGC((DrawablePtr)pWin, pGC);

              (*pGC->ops->PolyFillRect)((DrawablePtr)pWin, pGC, 1, &rect);

               FreeScratchGC(pGC);
            
            /* Flush pending packets */
            rfbCheckFds(pScreen);
            httpCheckFds(pScreen);
            }
    }

    pScrn->EnableDisableFBAccess = pVNC->EnableDisableFBAccess;
    (*pScrn->EnableDisableFBAccess)(index, enable);
    pScrn->EnableDisableFBAccess = rfbEnableDisableFBAccess;
}
#endif

/*
 * CreateGC - wrap the GC funcs (the GC ops will be wrapped when the GC
 * func "ValidateGC" is called).
 */

Bool
rfbCreateGC (GCPtr pGC)
{
    Bool ret;
    rfbGCPtr pGCPriv;

    SCREEN_PROLOGUE(pGC->pScreen,CreateGC);

    pGCPriv = (rfbGCPtr)pGC->devPrivates[rfbGCIndex].ptr;

    ret = (*pScreen->CreateGC) (pGC);

    TRC((stderr,"rfbCreateGC called\n"));

    pGCPriv->wrapOps = NULL;
    pGCPriv->wrapFuncs = pGC->funcs;
    pGC->funcs = &rfbGCFuncs;

    SCREEN_EPILOGUE(CreateGC,rfbCreateGC);

    return ret;
}

/*
 * PaintWindowBackground - the region being modified is just the given region.
 */

void
rfbPaintWindowBackground (WindowPtr pWin, RegionPtr pRegion, int what)
{
    SCREEN_PROLOGUE(pWin->drawable.pScreen,PaintWindowBackground);

    TRC((stderr,"rfbPaintWindowBackground called\n"));

    ADD_TO_MODIFIED_REGION(pScreen,pRegion);

    (*pScreen->PaintWindowBackground) (pWin, pRegion, what);

    SCHEDULE_FB_UPDATE(pScreen, pVNC);

    SCREEN_EPILOGUE(PaintWindowBackground,rfbPaintWindowBackground);
}

/*
 * PaintWindowBorder - the region being modified is just the given region.
 */

void
rfbPaintWindowBorder (WindowPtr pWin, RegionPtr pRegion, int what)
{
    SCREEN_PROLOGUE(pWin->drawable.pScreen,PaintWindowBorder);

    TRC((stderr,"rfbPaintWindowBorder called\n"));

    ADD_TO_MODIFIED_REGION(pScreen,pRegion);

    (*pScreen->PaintWindowBorder) (pWin, pRegion, what);

    SCHEDULE_FB_UPDATE(pScreen, pVNC);

    SCREEN_EPILOGUE(PaintWindowBorder,rfbPaintWindowBorder);
}

#ifdef CHROMIUM
Bool
rfbRealizeWindow(WindowPtr pWin)
{
    CRWindowTable *wt = NULL, *nextWt = NULL;
    Bool ret;
    SCREEN_PROLOGUE(pWin->drawable.pScreen,RealizeWindow);

    for (wt = windowTable; wt; wt = nextWt) {
            nextWt = wt->next;
         if (wt->XwinId == pWin->drawable.id) {
            rfbSendChromiumWindowShow(wt->CRwinId, 1);
         }
    }

    ret = (*pScreen->RealizeWindow)(pWin);

    SCREEN_EPILOGUE(RealizeWindow,rfbRealizeWindow);

    return ret;
}

Bool
rfbUnrealizeWindow(WindowPtr pWin)
{
    CRWindowTable *wt = NULL, *nextWt = NULL;
    Bool ret;
    SCREEN_PROLOGUE(pWin->drawable.pScreen,UnrealizeWindow);

    for (wt = windowTable; wt; wt = nextWt) {
            nextWt = wt->next;
         if (wt->XwinId == pWin->drawable.id) {
            rfbSendChromiumWindowShow(wt->CRwinId, 0);
         }
    }

    ret = (*pScreen->UnrealizeWindow)(pWin);

    SCREEN_EPILOGUE(UnrealizeWindow,rfbUnrealizeWindow);

    return ret;
}

Bool
rfbDestroyWindow(WindowPtr pWin)
{
    CRWindowTable *wt = NULL, *nextWt = NULL;
    Bool ret;
    SCREEN_PROLOGUE(pWin->drawable.pScreen,DestroyWindow);

    for (wt = windowTable; wt; wt = nextWt) {
            nextWt = wt->next;
         if (wt->XwinId == pWin->drawable.id) {
            rfbSendChromiumWindowShow(wt->CRwinId, 0);
         }
    }

    ret = (*pScreen->DestroyWindow)(pWin);

    SCREEN_EPILOGUE(DestroyWindow,rfbDestroyWindow);

    return ret;
}

void
rfbResizeWindow(WindowPtr pWin, int x, int y, unsigned int w, unsigned int h, WindowPtr pSib)
{
    CRWindowTable *wt = NULL, *nextWt = NULL;
    SCREEN_PROLOGUE(pWin->drawable.pScreen,ResizeWindow);

    for (wt = windowTable; wt; wt = nextWt) {
            nextWt = wt->next;
         if (wt->XwinId == pWin->drawable.id) {
             rfbSendChromiumMoveResizeWindow(wt->CRwinId, pWin->drawable.x, pWin->drawable.y, w, h);
         }
    }

    (*pScreen->ResizeWindow)(pWin, x, y, w, h, pSib);

    SCREEN_EPILOGUE(ResizeWindow,rfbResizeWindow);
}

Bool
rfbPositionWindow(WindowPtr pWin, int x, int y)
{
    Bool ret;
    CRWindowTable *wt, *nextWt;
    SCREEN_PROLOGUE(pWin->drawable.pScreen,PositionWindow);

    for (wt = windowTable; wt; wt = nextWt) {
            nextWt = wt->next;
         if (wt->XwinId == pWin->drawable.id) {
             rfbSendChromiumMoveResizeWindow(wt->CRwinId, x, y, pWin->drawable.width, pWin->drawable.height);
         }
    }

    ret = (*pScreen->PositionWindow)(pWin, x, y);

    SCREEN_EPILOGUE(PositionWindow,rfbPositionWindow);

    return ret;
}

void
rfbClipNotify(WindowPtr pWin, int x, int y)
{
    CRWindowTable *wt, *nextWt;
    SCREEN_PROLOGUE(pWin->drawable.pScreen,ClipNotify);

    for (wt = windowTable; wt; wt = nextWt) {
            nextWt = wt->next;
         if (wt->XwinId == pWin->drawable.id) {
            int numClipRects = REGION_NUM_RECTS(&pWin->clipList);
            BoxPtr pClipRects = REGION_RECTS(&pWin->clipList);

            /* Possible optimization - has the cliplist really? changed */

            rfbSendChromiumClipList(wt->CRwinId, pClipRects, numClipRects);
         }
    }

    if (*pScreen->ClipNotify) 
            (*pScreen->ClipNotify)(pWin, x, y);

    SCREEN_EPILOGUE(ClipNotify,rfbClipNotify);
}
#endif /* CHROMIUM */

/*
 * CopyWindow - the region being modified is the translation of the old
 * region, clipped to the border clip region of the window.  Note that any
 * parts of the window which have become newly-visible will not be affected by
 * this call - a separate PaintWindowBackground/Border will be called to do
 * that.  If the client will accept CopyRect messages then use rfbCopyRegion to
 * optimise the pending screen changes into a single "copy region" plus the
 * ordinary modified region.
 */

void
rfbCopyWindow (WindowPtr pWin, DDXPointRec ptOldOrg, RegionPtr pOldRegion)
{
    rfbClientPtr cl;
    RegionRec srcRegion, dstRegion;
    SCREEN_PROLOGUE(pWin->drawable.pScreen,CopyWindow);

    TRC((stderr,"rfbCopyWindow called\n"));

    REGION_NULL(pScreen,&dstRegion);
    REGION_COPY(pScreen,&dstRegion,pOldRegion);
    REGION_TRANSLATE(pWin->drawable.pScreen, &dstRegion,
                     pWin->drawable.x - ptOldOrg.x,
                     pWin->drawable.y - ptOldOrg.y);
    REGION_INTERSECT(pWin->drawable.pScreen, &dstRegion, &dstRegion,
                     &pWin->borderClip);

    for (cl = rfbClientHead; cl; cl = cl->next) {
        if (cl->useCopyRect) {
            REGION_NULL(pScreen,&srcRegion);
            REGION_COPY(pScreen,&srcRegion,pOldRegion);

            rfbCopyRegion(pScreen, cl, &srcRegion, &dstRegion,
                          pWin->drawable.x - ptOldOrg.x,
                          pWin->drawable.y - ptOldOrg.y);

            REGION_UNINIT(pScreen, &srcRegion);

        } else {

            REGION_UNION(pScreen, &cl->modifiedRegion, &cl->modifiedRegion,
                         &dstRegion);
        }
    }

    REGION_UNINIT(pScreen, &dstRegion);

    (*pScreen->CopyWindow) (pWin, ptOldOrg, pOldRegion);

    SCHEDULE_FB_UPDATE(pScreen, pVNC);

    SCREEN_EPILOGUE(CopyWindow,rfbCopyWindow);
}

/*
 * ClearToBackground - when generateExposures is false, the region being
 * modified is the given rectangle (clipped to the "window clip region").
 */

void
rfbClearToBackground (WindowPtr pWin, int x, int y, int w, int h, 
                      Bool generateExposures)
{
    RegionRec tmpRegion;
    BoxRec box;
    SCREEN_PROLOGUE(pWin->drawable.pScreen,ClearToBackground);

    TRC((stderr,"rfbClearToBackground called\n"));

    if (!generateExposures) {
        box.x1 = x + pWin->drawable.x;
        box.y1 = y + pWin->drawable.y;
        box.x2 = w ? (box.x1 + w) : (pWin->drawable.x + pWin->drawable.width);
        box.y2 = h ? (box.y1 + h) : (pWin->drawable.y + pWin->drawable.height);

        SAFE_REGION_INIT(pScreen, &tmpRegion, &box, 0);

        REGION_INTERSECT(pScreen, &tmpRegion, &tmpRegion, &pWin->clipList);

        ADD_TO_MODIFIED_REGION(pScreen, &tmpRegion);

        REGION_UNINIT(pScreen, &tmpRegion);
    }

    (*pScreen->ClearToBackground) (pWin, x, y, w, h, generateExposures);

    if (!generateExposures) {
        SCHEDULE_FB_UPDATE(pScreen, pVNC);
    }

    SCREEN_EPILOGUE(ClearToBackground,rfbClearToBackground);
}

/*
 * RestoreAreas - just be safe here - the region being modified is the whole
 * exposed region.
 */

RegionPtr
rfbRestoreAreas (WindowPtr pWin, RegionPtr prgnExposed)
{
    RegionPtr result;
    SCREEN_PROLOGUE(pWin->drawable.pScreen,RestoreAreas);

    TRC((stderr,"rfbRestoreAreas called\n"));

    ADD_TO_MODIFIED_REGION(pScreen, prgnExposed);

    result = (*pScreen->RestoreAreas) (pWin, prgnExposed);

    SCHEDULE_FB_UPDATE(pScreen, pVNC);

    SCREEN_EPILOGUE(RestoreAreas,rfbRestoreAreas);

    return result;
}



/****************************************************************************/
/*
 * GC funcs wrapper stuff
 *
 * We only really want to wrap the GC ops, but to do this we need to wrap
 * ValidateGC and so all the other GC funcs must be wrapped as well.
 */
/****************************************************************************/

#define GC_FUNC_PROLOGUE(pGC)                                                \
    rfbGCPtr pGCPriv = (rfbGCPtr) (pGC)->devPrivates[rfbGCIndex].ptr;        \
    (pGC)->funcs = pGCPriv->wrapFuncs;                                        \
    if (pGCPriv->wrapOps)                                                \
        (pGC)->ops = pGCPriv->wrapOps;

#define GC_FUNC_EPILOGUE(pGC)                \
    pGCPriv->wrapFuncs = (pGC)->funcs;        \
    (pGC)->funcs = &rfbGCFuncs;                \
    if (pGCPriv->wrapOps) {                \
        pGCPriv->wrapOps = (pGC)->ops;        \
        (pGC)->ops = &rfbGCOps;                \
    }


/*
 * ValidateGC - call the wrapped ValidateGC, then wrap the resulting GC ops if
 * the drawing will be to a viewable window.
 */

static void
rfbValidateGC (GCPtr pGC, unsigned long changes, DrawablePtr pDrawable)
{
    VNCSCREENPTR(pGC->pScreen);
    GC_FUNC_PROLOGUE(pGC);

    TRC((stderr,"rfbValidateGC called\n"));

    (*pGC->funcs->ValidateGC) (pGC, changes, pDrawable);
    
    pGCPriv->wrapOps = NULL;
    if (pDrawable->type == DRAWABLE_WINDOW && ((WindowPtr)pDrawable)->viewable)
    {
        WindowPtr   pWin = (WindowPtr) pDrawable;
        RegionPtr   pRegion = &pWin->clipList;

        if (pGC->subWindowMode == IncludeInferiors)
            pRegion = &pWin->borderClip;
        if (REGION_NOTEMPTY(pDrawable->pScreen, pRegion)) {
            pGCPriv->wrapOps = pGC->ops;
            TRC((stderr,"rfbValidateGC: wrapped GC ops\n"));
        }
    }

    GC_FUNC_EPILOGUE(pGC);
}

/*
 * All other GC funcs simply unwrap the GC funcs and ops, call the wrapped
 * function and then rewrap the funcs and ops.
 */

static void
rfbChangeGC (pGC, mask)
    GCPtr            pGC;
    unsigned long   mask;
{
    GC_FUNC_PROLOGUE(pGC);
    (*pGC->funcs->ChangeGC) (pGC, mask);
    GC_FUNC_EPILOGUE(pGC);
}

static void
rfbCopyGC (pGCSrc, mask, pGCDst)
    GCPtr            pGCSrc, pGCDst;
    unsigned long   mask;
{
    GC_FUNC_PROLOGUE(pGCDst);
    (*pGCDst->funcs->CopyGC) (pGCSrc, mask, pGCDst);
    GC_FUNC_EPILOGUE(pGCDst);
}

static void
rfbDestroyGC (pGC)
    GCPtr   pGC;
{
    GC_FUNC_PROLOGUE(pGC);
    (*pGC->funcs->DestroyGC) (pGC);
    GC_FUNC_EPILOGUE(pGC);
}

static void
rfbChangeClip (pGC, type, pvalue, nrects)
    GCPtr   pGC;
    int                type;
    pointer        pvalue;
    int                nrects;
{
    GC_FUNC_PROLOGUE(pGC);
    (*pGC->funcs->ChangeClip) (pGC, type, pvalue, nrects);
    GC_FUNC_EPILOGUE(pGC);
}

static void
rfbDestroyClip(pGC)
    GCPtr        pGC;
{
    GC_FUNC_PROLOGUE(pGC);
    (* pGC->funcs->DestroyClip)(pGC);
    GC_FUNC_EPILOGUE(pGC);
}

static void
rfbCopyClip(pgcDst, pgcSrc)
    GCPtr pgcDst, pgcSrc;
{
    GC_FUNC_PROLOGUE(pgcDst);
    (* pgcDst->funcs->CopyClip)(pgcDst, pgcSrc);
    GC_FUNC_EPILOGUE(pgcDst);
}


/****************************************************************************/
/*
 * GC ops wrapper stuff
 *
 * Note that these routines will only have been wrapped for drawing to
 * viewable windows so we don't need to check each time that the drawable
 * is a viewable window.
 */
/****************************************************************************/

#define GC_OP_PROLOGUE(pDrawable,pGC) \
    ScreenPtr pScreen = pGC->pScreen;                        \
    VNCSCREENPTR(pScreen);                        \
    rfbGCPtr pGCPrivate = (rfbGCPtr) (pGC)->devPrivates[rfbGCIndex].ptr; \
    GCFuncs *oldFuncs = pGC->funcs; \
    (void) pScreen; /* silence compiler */ \
    (pGC)->funcs = pGCPrivate->wrapFuncs; \
    (pGC)->ops = pGCPrivate->wrapOps;

#define GC_OP_EPILOGUE(pGC) \
    pGCPrivate->wrapOps = (pGC)->ops; \
    (pGC)->funcs = oldFuncs; \
    (pGC)->ops = &rfbGCOps;


/*
 * FillSpans - being very safe - the region being modified is the border clip
 * region of the window.
 */

static void
rfbFillSpans(pDrawable, pGC, nInit, pptInit, pwidthInit, fSorted)
    DrawablePtr pDrawable;
    GCPtr        pGC;
    int                nInit;                        /* number of spans to fill */
    DDXPointPtr pptInit;                /* pointer to list of start points */
    int                *pwidthInit;                /* pointer to list of n widths */
    int         fSorted;
{
    GC_OP_PROLOGUE(pDrawable,pGC);

    TRC((stderr,"rfbFillSpans called\n"));

    ADD_TO_MODIFIED_REGION(pDrawable->pScreen,
                           &((WindowPtr)pDrawable)->borderClip);

    (*pGC->ops->FillSpans) (pDrawable, pGC, nInit, pptInit,pwidthInit,fSorted);

    SCHEDULE_FB_UPDATE(pDrawable->pScreen, pVNC);

    GC_OP_EPILOGUE(pGC);
}

/*
 * SetSpans - being very safe - the region being modified is the border clip
 * region of the window.
 */

static void
rfbSetSpans(DrawablePtr                 pDrawable, 
            GCPtr                        pGC, 
            char                        *psrc, 
            register DDXPointPtr        ppt, 
            int                                *pwidth, 
            int                                nspans, 
            int                                fSorted)
{
    GC_OP_PROLOGUE(pDrawable,pGC);

    TRC((stderr,"rfbSetSpans called\n"));

    ADD_TO_MODIFIED_REGION(pDrawable->pScreen,
                           &((WindowPtr)pDrawable)->borderClip);

    (*pGC->ops->SetSpans) (pDrawable, pGC, psrc, ppt, pwidth, nspans, fSorted);

    SCHEDULE_FB_UPDATE(pDrawable->pScreen, pVNC);

    GC_OP_EPILOGUE(pGC);
}

/*
 * PutImage - the region being modified is the rectangle of the
 * PutImage (clipped to the window clip region).
 */

static void
rfbPutImage(pDrawable, pGC, depth, x, y, w, h, leftPad, format, pBits)
    DrawablePtr          pDrawable;
    GCPtr             pGC;
    int                  depth;
    int                      x;
    int                      y;
    int                      w;
    int                      h;
    int                  leftPad;
    int                      format;
    char              *pBits;
{
    RegionRec tmpRegion;
    BoxRec box;
    GC_OP_PROLOGUE(pDrawable, pGC);

    TRC((stderr,"rfbPutImage called\n"));

    box.x1 = x + pDrawable->x;
    box.y1 = y + pDrawable->y;
    box.x2 = box.x1 + w;
    box.y2 = box.y1 + h;

    SAFE_REGION_INIT(pDrawable->pScreen, &tmpRegion, &box, 0);

    REGION_INTERSECT(pDrawable->pScreen, &tmpRegion, &tmpRegion,
                                                             pGC->pCompositeClip);

    ADD_TO_MODIFIED_REGION(pDrawable->pScreen, &tmpRegion);

    REGION_UNINIT(pDrawable->pScreen, &tmpRegion);

    (*pGC->ops->PutImage) (pDrawable, pGC, depth, x, y, w, h,
                           leftPad, format, pBits);

    SCHEDULE_FB_UPDATE(pDrawable->pScreen, pVNC);

    GC_OP_EPILOGUE(pGC);
}

/*
 * CopyArea - the region being modified is the destination rectangle (clipped
 * to the window clip region).
 * If the client will accept CopyRect messages then use rfbCopyRegion
 * to optimise the pending screen changes into a single "copy region" plus
 * the ordinary modified region.
 */

static RegionPtr
rfbCopyArea (pSrc, pDst, pGC, srcx, srcy, w, h, dstx, dsty)
    DrawablePtr          pSrc;
    DrawablePtr          pDst;
    GCPtr             pGC;
    int                      srcx;
    int                      srcy;
    int                      w;
    int                      h;
    int                      dstx;
    int                      dsty;
{
    rfbClientPtr cl;
    RegionPtr rgn;
    RegionRec srcRegion, dstRegion;
    BoxRec box;
    GC_OP_PROLOGUE(pDst, pGC);

    TRC((stderr,"rfbCopyArea called\n"));

    box.x1 = dstx + pDst->x;
    box.y1 = dsty + pDst->y;
    box.x2 = box.x1 + w;
    box.y2 = box.y1 + h;

    SAFE_REGION_INIT(pDst->pScreen, &dstRegion, &box, 0);
    REGION_INTERSECT(pDst->pScreen, &dstRegion, &dstRegion,
                                                             pGC->pCompositeClip);

    if ((pSrc->type == DRAWABLE_WINDOW) && (pSrc->pScreen == pDst->pScreen)) {
        box.x1 = srcx + pSrc->x;
        box.y1 = srcy + pSrc->y;
        box.x2 = box.x1 + w;
        box.y2 = box.y1 + h;

        for (cl = rfbClientHead; cl; cl = cl->next) {
            if (cl->useCopyRect) {
                SAFE_REGION_INIT(pSrc->pScreen, &srcRegion, &box, 0);
                REGION_INTERSECT(pSrc->pScreen, &srcRegion, &srcRegion,
                                 &((WindowPtr)pSrc)->clipList);

                rfbCopyRegion(pSrc->pScreen, cl, &srcRegion, &dstRegion,
                              dstx + pDst->x - srcx - pSrc->x,
                              dsty + pDst->y - srcy - pSrc->y);

                REGION_UNINIT(pSrc->pScreen, &srcRegion);

            } else {

                REGION_UNION(pScreen, &cl->modifiedRegion, &cl->modifiedRegion,
                             &dstRegion);
            }
        }

    } else {

        ADD_TO_MODIFIED_REGION(pDst->pScreen, &dstRegion);
    }

    REGION_UNINIT(pDst->pScreen, &dstRegion);

    rgn = (*pGC->ops->CopyArea) (pSrc, pDst, pGC, srcx, srcy, w, h,
                                 dstx, dsty);

    SCHEDULE_FB_UPDATE(pDst->pScreen, pVNC);

    GC_OP_EPILOGUE(pGC);

    return rgn;
}


/*
 * CopyPlane - the region being modified is the destination rectangle (clipped
 * to the window clip region).
 */

static RegionPtr
rfbCopyPlane (pSrc, pDst, pGC, srcx, srcy, w, h, dstx, dsty, plane)
    DrawablePtr          pSrc;
    DrawablePtr          pDst;
    register GCPtr pGC;
    int               srcx,
                  srcy;
    int               w,
                  h;
    int               dstx,
                  dsty;
    unsigned long  plane;
{
    RegionPtr rgn;
    RegionRec tmpRegion;
    BoxRec box;
    GC_OP_PROLOGUE(pDst, pGC);

    TRC((stderr,"rfbCopyPlane called\n"));

    box.x1 = dstx + pDst->x;
    box.y1 = dsty + pDst->y;
    box.x2 = box.x1 + w;
    box.y2 = box.y1 + h;

    SAFE_REGION_INIT(pDst->pScreen, &tmpRegion, &box, 0);

    REGION_INTERSECT(pDst->pScreen, &tmpRegion, &tmpRegion,
                                                             pGC->pCompositeClip);

    ADD_TO_MODIFIED_REGION(pDst->pScreen, &tmpRegion);

    REGION_UNINIT(pDst->pScreen, &tmpRegion);

    rgn = (*pGC->ops->CopyPlane) (pSrc, pDst, pGC, srcx, srcy, w, h,
                                  dstx, dsty, plane);

    SCHEDULE_FB_UPDATE(pDst->pScreen, pVNC);

    GC_OP_EPILOGUE(pGC);

    return rgn;
}

/*
 * PolyPoint - find the smallest rectangle which encloses the points drawn
 * (and clip).
 */

static void
rfbPolyPoint (pDrawable, pGC, mode, npt, pts)
    DrawablePtr pDrawable;
    GCPtr        pGC;
    int                mode;                /* Origin or Previous */
    int                npt;
    xPoint         *pts;
{
    int i;
    RegionRec tmpRegion;
    BoxRec box;
    GC_OP_PROLOGUE(pDrawable, pGC);

    TRC((stderr,"rfbPolyPoint called\n"));

    if (npt) {
        int minX = pts[0].x, maxX = pts[0].x;
        int minY = pts[0].y, maxY = pts[0].y;

        if (mode == CoordModePrevious)
        {
            int x = pts[0].x, y = pts[0].y;

            for (i = 1; i < npt; i++) {
                x += pts[i].x;
                y += pts[i].y;
                if (x < minX) minX = x;
                if (x > maxX) maxX = x;
                if (y < minY) minY = y;
                if (y > maxY) maxY = y;
            }
        }
        else
        {
            for (i = 1; i < npt; i++) {
                if (pts[i].x < minX) minX = pts[i].x;
                if (pts[i].x > maxX) maxX = pts[i].x;
                if (pts[i].y < minY) minY = pts[i].y;
                if (pts[i].y > maxY) maxY = pts[i].y;
            }
        }

        box.x1 = minX + pDrawable->x;
        box.y1 = minY + pDrawable->y;
        box.x2 = maxX + 1 + pDrawable->x;
        box.y2 = maxY + 1 + pDrawable->y;

        SAFE_REGION_INIT(pDrawable->pScreen, &tmpRegion, &box, 0);

        REGION_INTERSECT(pDrawable->pScreen, &tmpRegion, &tmpRegion,
                                                             pGC->pCompositeClip);

        ADD_TO_MODIFIED_REGION(pDrawable->pScreen, &tmpRegion);

        REGION_UNINIT(pDrawable->pScreen, &tmpRegion);
    }

    (*pGC->ops->PolyPoint) (pDrawable, pGC, mode, npt, pts);

    if (npt) {
        SCHEDULE_FB_UPDATE(pDrawable->pScreen, pVNC);
    }

    GC_OP_EPILOGUE(pGC);
}

/*
 * PolyLines - take the union of bounding boxes around each line (and clip).
 */

static void
rfbPolylines (DrawablePtr pDrawable, GCPtr pGC, int mode, int npt, DDXPointPtr ppts)
{
    RegionPtr tmpRegion;
    xRectangle *rects;
    int i, extra, nlines, lw;
    int x1, x2, y1, y2;
    GC_OP_PROLOGUE(pDrawable, pGC);

    TRC((stderr,"rfbPolylines called\n"));

    if (npt) {
        lw = pGC->lineWidth;
        if (lw == 0)
            lw = 1;

        if (npt == 1)
        {
            nlines = 1;
            rects = (xRectangle *)xalloc(sizeof(xRectangle));
            if (!rects) {
                FatalError("rfbPolylines: xalloc failed\n");
            }

            rects[0].x = ppts[0].x - lw + pDrawable->x; /* being safe here */
            rects[0].y = ppts[0].y - lw + pDrawable->y;
            rects[0].width = 2*lw;
            rects[0].height = 2*lw;
        }
        else
        {
            nlines = npt - 1;
            rects = (xRectangle *)xalloc(nlines*sizeof(xRectangle));
            if (!rects) {
                FatalError("rfbPolylines: xalloc failed\n");
            }

            /*
             * mitered joins can project quite a way from
             * the line end; the 11 degree miter limit limits
             * this extension to lw / (2 * tan(11/2)), rounded up
             * and converted to int yields 6 * lw
             */

            if (pGC->joinStyle == JoinMiter) {
                extra = 6 * lw;
            } else {
                extra = lw / 2;
            }

            x1 = ppts[0].x + pDrawable->x;
            y1 = ppts[0].y + pDrawable->y;

            for (i = 0; i < nlines; i++) {
                if (mode == CoordModeOrigin) {
                    x2 = pDrawable->x + ppts[i+1].x;
                    y2 = pDrawable->y + ppts[i+1].y;
                } else {
                    x2 = x1 + ppts[i+1].x;
                    y2 = y1 + ppts[i+1].y;
                }

                if (x1 > x2) {
                    rects[i].x = x2 - extra;
                    rects[i].width = x1 - x2 + 1 + 2 * extra;
                } else {
                    rects[i].x = x1 - extra;
                    rects[i].width = x2 - x1 + 1 + 2 * extra;
                }

                if (y1 > y2) {
                    rects[i].y = y2 - extra;
                    rects[i].height = y1 - y2 + 1 + 2 * extra;
                } else {
                    rects[i].y = y1 - extra;
                    rects[i].height = y2 - y1 + 1 + 2 * extra;
                }

                x1 = x2;
                y1 = y2;
            }
        }
        tmpRegion = RECTS_TO_REGION(pDrawable->pScreen, nlines, rects,CT_NONE);
        REGION_INTERSECT(pDrawable->pScreen, tmpRegion, tmpRegion,
                                                             pGC->pCompositeClip);

        ADD_TO_MODIFIED_REGION(pDrawable->pScreen, tmpRegion);

        REGION_DESTROY(pDrawable->pScreen, tmpRegion);
        xfree((char *)rects);
    }

    (*pGC->ops->Polylines) (pDrawable, pGC, mode, npt, ppts);

    if (npt) {
        SCHEDULE_FB_UPDATE(pDrawable->pScreen, pVNC);
    }

    GC_OP_EPILOGUE(pGC);
}

/*
 * PolySegment - take the union of bounding boxes around each segment (and
 * clip).
 */

static void
rfbPolySegment(pDrawable, pGC, nseg, segs)
    DrawablePtr pDrawable;
    GCPtr         pGC;
    int                nseg;
    xSegment        *segs;
{
    RegionPtr tmpRegion;
    xRectangle *rects;
    int i, extra, lw;

    GC_OP_PROLOGUE(pDrawable, pGC);

    TRC((stderr,"rfbPolySegment called\n"));

    if (nseg) {
        rects = (xRectangle *)xalloc(nseg*sizeof(xRectangle));
        if (!rects) {
            FatalError("rfbPolySegment: xalloc failed\n");
        }

        lw = pGC->lineWidth;
        if (lw == 0)
            lw = 1;

        extra = lw / 2;

        for (i = 0; i < nseg; i++)
        {
            if (segs[i].x1 > segs[i].x2) {
                rects[i].x = segs[i].x2 - extra + pDrawable->x;
                rects[i].width = segs[i].x1 - segs[i].x2 + 1 + 2 * extra;
            } else {
                rects[i].x = segs[i].x1 - extra + pDrawable->x;
                rects[i].width = segs[i].x2 - segs[i].x1 + 1 + 2 * extra;
            }

            if (segs[i].y1 > segs[i].y2) {
                rects[i].y = segs[i].y2 - extra + pDrawable->y;
                rects[i].height = segs[i].y1 - segs[i].y2 + 1 + 2 * extra;
            } else {
                rects[i].y = segs[i].y1 - extra + pDrawable->y;
                rects[i].height = segs[i].y2 - segs[i].y1 + 1 + 2 * extra;
            }
        }

        tmpRegion = RECTS_TO_REGION(pDrawable->pScreen, nseg, rects, CT_NONE);
        REGION_INTERSECT(pDrawable->pScreen, tmpRegion, tmpRegion,
                                                             pGC->pCompositeClip);

        ADD_TO_MODIFIED_REGION(pDrawable->pScreen, tmpRegion);

        REGION_DESTROY(pDrawable->pScreen, tmpRegion);
        xfree((char *)rects);
    }

    (*pGC->ops->PolySegment) (pDrawable, pGC, nseg, segs);

    if (nseg) {
        SCHEDULE_FB_UPDATE(pDrawable->pScreen, pVNC);
    }

    GC_OP_EPILOGUE(pGC);
}

/*
 * PolyRectangle (rectangle outlines) - take the union of bounding boxes
 * around each line (and clip).
 */

static void
rfbPolyRectangle(pDrawable, pGC, nrects, rects)
    DrawablePtr        pDrawable;
    GCPtr        pGC;
    int                nrects;
    xRectangle        *rects;
{
    int i, extra, lw;
    RegionPtr tmpRegion;
    xRectangle *regRects;
    GC_OP_PROLOGUE(pDrawable, pGC);

    TRC((stderr,"rfbPolyRectangle called\n"));

    if (nrects) {
        regRects = (xRectangle *)xalloc(nrects*4*sizeof(xRectangle));
        if (!regRects) {
            FatalError("rfbPolyRectangle: xalloc failed\n");
        }

        lw = pGC->lineWidth;
        if (lw == 0)
            lw = 1;

        extra = lw / 2;

        for (i = 0; i < nrects; i++)
        {
            regRects[i*4].x = rects[i].x - extra + pDrawable->x;
            regRects[i*4].y = rects[i].y - extra + pDrawable->y;
            regRects[i*4].width = rects[i].width + 1 + 2 * extra;
            regRects[i*4].height = 1 + 2 * extra;

            regRects[i*4+1].x = rects[i].x - extra + pDrawable->x;
            regRects[i*4+1].y = rects[i].y - extra + pDrawable->y;
            regRects[i*4+1].width = 1 + 2 * extra;
            regRects[i*4+1].height = rects[i].height + 1 + 2 * extra;

            regRects[i*4+2].x
                = rects[i].x + rects[i].width - extra + pDrawable->x;
            regRects[i*4+2].y = rects[i].y - extra + pDrawable->y;
            regRects[i*4+2].width = 1 + 2 * extra;
            regRects[i*4+2].height = rects[i].height + 1 + 2 * extra;

            regRects[i*4+3].x = rects[i].x - extra + pDrawable->x;
            regRects[i*4+3].y
                = rects[i].y + rects[i].height - extra + pDrawable->y;
            regRects[i*4+3].width = rects[i].width + 1 + 2 * extra;
            regRects[i*4+3].height = 1 + 2 * extra;
        }

        tmpRegion = RECTS_TO_REGION(pDrawable->pScreen, nrects*4,
                                    regRects, CT_NONE);
        REGION_INTERSECT(pDrawable->pScreen, tmpRegion, tmpRegion,
                                                             pGC->pCompositeClip);

        ADD_TO_MODIFIED_REGION(pDrawable->pScreen, tmpRegion);

        REGION_DESTROY(pDrawable->pScreen, tmpRegion);
        xfree((char *)regRects);
    }

    (*pGC->ops->PolyRectangle) (pDrawable, pGC, nrects, rects);

    if (nrects) {
        SCHEDULE_FB_UPDATE(pDrawable->pScreen, pVNC);
    }

    GC_OP_EPILOGUE(pGC);
}

/*
 * PolyArc - take the union of bounding boxes around each arc (and clip).
 * Bounding boxes assume each is a full circle / ellipse.
 */

static void
rfbPolyArc(pDrawable, pGC, narcs, arcs)
    DrawablePtr        pDrawable;
    register GCPtr        pGC;
    int                narcs;
    xArc        *arcs;
{
    int i, extra, lw;
    RegionPtr tmpRegion;
    xRectangle *rects;
    GC_OP_PROLOGUE(pDrawable, pGC);

    TRC((stderr,"rfbPolyArc called\n"));

    if (narcs) {
        rects = (xRectangle *)xalloc(narcs*sizeof(xRectangle));
        if (!rects) {
            FatalError("rfbPolyArc: xalloc failed\n");
        }

        lw = pGC->lineWidth;
        if (lw == 0)
            lw = 1;

        extra = lw / 2;

        for (i = 0; i < narcs; i++)
        {
            rects[i].x = arcs[i].x - extra + pDrawable->x;
            rects[i].y = arcs[i].y - extra + pDrawable->y;
            rects[i].width = arcs[i].width + lw;
            rects[i].height = arcs[i].height + lw;
        }

        tmpRegion = RECTS_TO_REGION(pDrawable->pScreen, narcs, rects, CT_NONE);
        REGION_INTERSECT(pDrawable->pScreen, tmpRegion, tmpRegion,
                                                             pGC->pCompositeClip);

        ADD_TO_MODIFIED_REGION(pDrawable->pScreen, tmpRegion);

        REGION_DESTROY(pDrawable->pScreen, tmpRegion);
        xfree((char *)rects);
    }

    (*pGC->ops->PolyArc) (pDrawable, pGC, narcs, arcs);

    if (narcs) {
        SCHEDULE_FB_UPDATE(pDrawable->pScreen, pVNC);
    }

    GC_OP_EPILOGUE(pGC);
}

/*
 * FillPolygon - take bounding box around polygon (and clip).
 */

static void
rfbFillPolygon(pDrawable, pGC, shape, mode, count, pts)
    register DrawablePtr pDrawable;
    register GCPtr        pGC;
    int                        shape, mode;
    int                        count;
    DDXPointPtr                pts;
{
    int i;
    RegionRec tmpRegion;
    BoxRec box;
    GC_OP_PROLOGUE(pDrawable, pGC);

    TRC((stderr,"rfbFillPolygon called\n"));

    if (count) {
        int minX = pts[0].x, maxX = pts[0].x;
        int minY = pts[0].y, maxY = pts[0].y;

        if (mode == CoordModePrevious)
        {
            int x = pts[0].x, y = pts[0].y;

            for (i = 1; i < count; i++) {
                x += pts[i].x;
                y += pts[i].y;
                if (x < minX) minX = x;
                if (x > maxX) maxX = x;
                if (y < minY) minY = y;
                if (y > maxY) maxY = y;
            }
        }
        else
        {
            for (i = 1; i < count; i++) {
                if (pts[i].x < minX) minX = pts[i].x;
                if (pts[i].x > maxX) maxX = pts[i].x;
                if (pts[i].y < minY) minY = pts[i].y;
                if (pts[i].y > maxY) maxY = pts[i].y;
            }
        }

        box.x1 = minX + pDrawable->x;
        box.y1 = minY + pDrawable->y;
        box.x2 = maxX + 1 + pDrawable->x;
        box.y2 = maxY + 1 + pDrawable->y;

        SAFE_REGION_INIT(pDrawable->pScreen, &tmpRegion, &box, 0);

        REGION_INTERSECT(pDrawable->pScreen, &tmpRegion, &tmpRegion,
                                                             pGC->pCompositeClip);

        ADD_TO_MODIFIED_REGION(pDrawable->pScreen, &tmpRegion);

        REGION_UNINIT(pDrawable->pScreen, &tmpRegion);
    }

    (*pGC->ops->FillPolygon) (pDrawable, pGC, shape, mode, count, pts);

    if (count) {
        SCHEDULE_FB_UPDATE(pDrawable->pScreen, pVNC);
    }

    GC_OP_EPILOGUE(pGC);
}

/*
 * PolyFillRect - take the union of the given rectangles (and clip).
 */

static void
rfbPolyFillRect(pDrawable, pGC, nrects, rects)
    DrawablePtr pDrawable;
    GCPtr        pGC;
    int                nrects;
    xRectangle        *rects;
{
    RegionPtr tmpRegion;
    xRectangle *regRects;
    int i;
    GC_OP_PROLOGUE(pDrawable, pGC);

    TRC((stderr,"rfbPolyFillRect called\n"));

    if (nrects) {
        regRects = (xRectangle *)xalloc(nrects*sizeof(xRectangle));
        if (!regRects) {
            FatalError("rfbPolyFillRect: xalloc failed\n");
        }

        for (i = 0; i < nrects; i++) {
            regRects[i].x = rects[i].x + pDrawable->x;
            regRects[i].y = rects[i].y + pDrawable->y;
            regRects[i].width = rects[i].width;
            regRects[i].height = rects[i].height;
        }

        tmpRegion = RECTS_TO_REGION(pDrawable->pScreen, nrects, regRects,
                                    CT_NONE);
        REGION_INTERSECT(pDrawable->pScreen, tmpRegion, tmpRegion,
                                                             pGC->pCompositeClip);

        ADD_TO_MODIFIED_REGION(pDrawable->pScreen, tmpRegion);

        REGION_DESTROY(pDrawable->pScreen, tmpRegion);
        xfree((char *)regRects);
    }

    (*pGC->ops->PolyFillRect) (pDrawable, pGC, nrects, rects);

    if (nrects) {
        SCHEDULE_FB_UPDATE(pDrawable->pScreen, pVNC);
    }

    GC_OP_EPILOGUE(pGC);
}

/*
 * PolyFillArc - take the union of bounding boxes around each arc (and clip).
 * Bounding boxes assume each is a full circle / ellipse.
 */

static void
rfbPolyFillArc(pDrawable, pGC, narcs, arcs)
    DrawablePtr        pDrawable;
    GCPtr        pGC;
    int                narcs;
    xArc        *arcs;
{
    int i, extra, lw;
    RegionPtr tmpRegion;
    xRectangle *rects;
    GC_OP_PROLOGUE(pDrawable, pGC);

    TRC((stderr,"rfbPolyFillArc called\n"));

    if (narcs) {
        rects = (xRectangle *)xalloc(narcs*sizeof(xRectangle));
        if (!rects) {
            FatalError("rfbPolyFillArc: xalloc failed\n");
        }

        lw = pGC->lineWidth;
        if (lw == 0)
            lw = 1;

        extra = lw / 2;

        for (i = 0; i < narcs; i++)
        {
            rects[i].x = arcs[i].x - extra + pDrawable->x;
            rects[i].y = arcs[i].y - extra + pDrawable->y;
            rects[i].width = arcs[i].width + lw;
            rects[i].height = arcs[i].height + lw;
        }

        tmpRegion = RECTS_TO_REGION(pDrawable->pScreen, narcs, rects, CT_NONE);
        REGION_INTERSECT(pDrawable->pScreen, tmpRegion, tmpRegion,
                                                             pGC->pCompositeClip);

        ADD_TO_MODIFIED_REGION(pDrawable->pScreen, tmpRegion);

        REGION_DESTROY(pDrawable->pScreen, tmpRegion);
        xfree((char *)rects);
    }

    (*pGC->ops->PolyFillArc) (pDrawable, pGC, narcs, arcs);

    if (narcs) {
        SCHEDULE_FB_UPDATE(pDrawable->pScreen, pVNC);
    }

    GC_OP_EPILOGUE(pGC);
}

/*
 * Get a rough bounding box around n characters of the given font.
 */

static void GetTextBoundingBox(pDrawable, font, x, y, n, pbox)
    DrawablePtr pDrawable;
    FontPtr font;
    int x, y, n;
    BoxPtr pbox;
{
    int maxAscent, maxDescent, maxCharWidth;

    if (FONTASCENT(font) > FONTMAXBOUNDS(font,ascent))
        maxAscent = FONTASCENT(font);
    else
        maxAscent = FONTMAXBOUNDS(font,ascent);

    if (FONTDESCENT(font) > FONTMAXBOUNDS(font,descent))
        maxDescent = FONTDESCENT(font);
    else
        maxDescent = FONTMAXBOUNDS(font,descent);

    if (FONTMAXBOUNDS(font,rightSideBearing) > FONTMAXBOUNDS(font,characterWidth))
        maxCharWidth = FONTMAXBOUNDS(font,rightSideBearing);
    else
        maxCharWidth = FONTMAXBOUNDS(font,characterWidth);

    pbox->x1 = pDrawable->x + x;
    pbox->y1 = pDrawable->y + y - maxAscent;
    pbox->x2 = pbox->x1 + maxCharWidth * n;
    pbox->y2 = pbox->y1 + maxAscent + maxDescent;

    if (FONTMINBOUNDS(font,leftSideBearing) < 0) {
        pbox->x1 += FONTMINBOUNDS(font,leftSideBearing);
    }
}


/*
 * PolyText8 - use rough bounding box.
 */

static int
rfbPolyText8(pDrawable, pGC, x, y, count, chars)
    DrawablePtr pDrawable;
    GCPtr        pGC;
    int                x, y;
    int         count;
    char        *chars;
{
    int        ret;
    RegionRec tmpRegion;
    BoxRec box;
    GC_OP_PROLOGUE(pDrawable, pGC);

    TRC((stderr,"rfbPolyText8 called '%.*s'\n",count,chars));

    if (count) {
        GetTextBoundingBox(pDrawable, pGC->font, x, y, count, &box);

        SAFE_REGION_INIT(pDrawable->pScreen, &tmpRegion, &box, 0);

        REGION_INTERSECT(pDrawable->pScreen, &tmpRegion, &tmpRegion,
                                                             pGC->pCompositeClip);

        ADD_TO_MODIFIED_REGION(pDrawable->pScreen, &tmpRegion);

        REGION_UNINIT(pDrawable->pScreen, &tmpRegion);
    }

    ret = (*pGC->ops->PolyText8) (pDrawable, pGC, x, y, count, chars);

    if (count) {
        SCHEDULE_FB_UPDATE(pDrawable->pScreen, pVNC);
    }

    GC_OP_EPILOGUE(pGC);
    return ret;
}

/*
 * PolyText16 - use rough bounding box.
 */

static int
rfbPolyText16(pDrawable, pGC, x, y, count, chars)
    DrawablePtr pDrawable;
    GCPtr        pGC;
    int                x, y;
    int                count;
    unsigned short *chars;
{
    int        ret;
    RegionRec tmpRegion;
    BoxRec box;
    GC_OP_PROLOGUE(pDrawable, pGC);

    TRC((stderr,"rfbPolyText16 called\n"));

    if (count) {
        GetTextBoundingBox(pDrawable, pGC->font, x, y, count, &box);

        SAFE_REGION_INIT(pDrawable->pScreen, &tmpRegion, &box, 0);

        REGION_INTERSECT(pDrawable->pScreen, &tmpRegion, &tmpRegion,
                                                             pGC->pCompositeClip);

        ADD_TO_MODIFIED_REGION(pDrawable->pScreen, &tmpRegion);

        REGION_UNINIT(pDrawable->pScreen, &tmpRegion);
    }

    ret = (*pGC->ops->PolyText16) (pDrawable, pGC, x, y, count, chars);

    if (count) {
        SCHEDULE_FB_UPDATE(pDrawable->pScreen, pVNC);
    }

    GC_OP_EPILOGUE(pGC);
    return ret;
}

/*
 * ImageText8 - use rough bounding box.
 */

static void
rfbImageText8(pDrawable, pGC, x, y, count, chars)
    DrawablePtr pDrawable;
    GCPtr        pGC;
    int                x, y;
    int                count;
    char        *chars;
{
    RegionRec tmpRegion;
    BoxRec box;
    GC_OP_PROLOGUE(pDrawable, pGC);

    TRC((stderr,"rfbImageText8 called '%.*s'\n",count,chars));

    if (count) {
        GetTextBoundingBox(pDrawable, pGC->font, x, y, count, &box);

        SAFE_REGION_INIT(pDrawable->pScreen, &tmpRegion, &box, 0);

        REGION_INTERSECT(pDrawable->pScreen, &tmpRegion, &tmpRegion,
                                                             pGC->pCompositeClip);

        ADD_TO_MODIFIED_REGION(pDrawable->pScreen, &tmpRegion);

        REGION_UNINIT(pDrawable->pScreen, &tmpRegion);
    }

    (*pGC->ops->ImageText8) (pDrawable, pGC, x, y, count, chars);

    if (count) {
        SCHEDULE_FB_UPDATE(pDrawable->pScreen, pVNC);
    }

    GC_OP_EPILOGUE(pGC);
}

/*
 * ImageText16 - use rough bounding box.
 */

static void
rfbImageText16(pDrawable, pGC, x, y, count, chars)
    DrawablePtr pDrawable;
    GCPtr        pGC;
    int                x, y;
    int                count;
    unsigned short *chars;
{
    RegionRec tmpRegion;
    BoxRec box;
    GC_OP_PROLOGUE(pDrawable, pGC);

    TRC((stderr,"rfbImageText16 called\n"));

    if (count) {
        GetTextBoundingBox(pDrawable, pGC->font, x, y, count, &box);

        SAFE_REGION_INIT(pDrawable->pScreen, &tmpRegion, &box, 0);

        REGION_INTERSECT(pDrawable->pScreen, &tmpRegion, &tmpRegion,
                                                             pGC->pCompositeClip);

        ADD_TO_MODIFIED_REGION(pDrawable->pScreen, &tmpRegion);

        REGION_UNINIT(pDrawable->pScreen, &tmpRegion);
    }

    (*pGC->ops->ImageText16) (pDrawable, pGC, x, y, count, chars);

    if (count) {
        SCHEDULE_FB_UPDATE(pDrawable->pScreen, pVNC);
    }

    GC_OP_EPILOGUE(pGC);
}

/*
 * ImageGlyphBlt - use rough bounding box.
 */

static void
rfbImageGlyphBlt(pDrawable, pGC, x, y, nglyph, ppci, pglyphBase)
    DrawablePtr pDrawable;
    GCPtr         pGC;
    int         x, y;
    unsigned int nglyph;
    CharInfoPtr *ppci;                /* array of character info */
    pointer         pglyphBase;        /* start of array of glyphs */
{
    RegionRec tmpRegion;
    BoxRec box;
    GC_OP_PROLOGUE(pDrawable, pGC);

    TRC((stderr,"rfbImageGlyphBlt called\n"));

    if (nglyph) {
        GetTextBoundingBox(pDrawable, pGC->font, x, y, nglyph, &box);

        SAFE_REGION_INIT(pDrawable->pScreen, &tmpRegion, &box, 0);

        REGION_INTERSECT(pDrawable->pScreen, &tmpRegion, &tmpRegion,
                                                             pGC->pCompositeClip);

        ADD_TO_MODIFIED_REGION(pDrawable->pScreen, &tmpRegion);

        REGION_UNINIT(pDrawable->pScreen, &tmpRegion);
    }

    (*pGC->ops->ImageGlyphBlt) (pDrawable, pGC, x, y, nglyph, ppci,pglyphBase);

    if (nglyph) {
        SCHEDULE_FB_UPDATE(pDrawable->pScreen, pVNC);
    }

    GC_OP_EPILOGUE(pGC);
}

/*
 * PolyGlyphBlt - use rough bounding box.
 */

static void
rfbPolyGlyphBlt(pDrawable, pGC, x, y, nglyph, ppci, pglyphBase)
    DrawablePtr pDrawable;
    GCPtr        pGC;
    int         x, y;
    unsigned int nglyph;
    CharInfoPtr *ppci;                /* array of character info */
    pointer        pglyphBase;        /* start of array of glyphs */
{
    RegionRec tmpRegion;
    BoxRec box;
    GC_OP_PROLOGUE(pDrawable, pGC);

    TRC((stderr,"rfbPolyGlyphBlt called\n"));

    if (nglyph) {
        GetTextBoundingBox(pDrawable, pGC->font, x, y, nglyph, &box);

        SAFE_REGION_INIT(pDrawable->pScreen, &tmpRegion, &box, 0);

        REGION_INTERSECT(pDrawable->pScreen, &tmpRegion, &tmpRegion,
                                                             pGC->pCompositeClip);

        ADD_TO_MODIFIED_REGION(pDrawable->pScreen, &tmpRegion);

        REGION_UNINIT(pDrawable->pScreen, &tmpRegion);
    }

    (*pGC->ops->PolyGlyphBlt) (pDrawable, pGC, x, y, nglyph, ppci, pglyphBase);

    if (nglyph) {
        SCHEDULE_FB_UPDATE(pDrawable->pScreen, pVNC);
    }

    GC_OP_EPILOGUE(pGC);
}

/*
 * PushPixels - be fairly safe - region modified is intersection of the given
 * rectangle with the window clip region.
 */

static void
rfbPushPixels(pGC, pBitMap, pDrawable, w, h, x, y)
    GCPtr        pGC;
    PixmapPtr        pBitMap;
    DrawablePtr pDrawable;
    int                w, h, x, y;
{
    RegionRec tmpRegion;
    BoxRec box;
    GC_OP_PROLOGUE(pDrawable, pGC);

    TRC((stderr,"rfbPushPixels called\n"));

    box.x1 = x + pDrawable->x;
    box.y1 = y + pDrawable->y;
    box.x2 = box.x1 + w;
    box.y2 = box.y1 + h;

    SAFE_REGION_INIT(pDrawable->pScreen, &tmpRegion, &box, 0);

    REGION_INTERSECT(pDrawable->pScreen, &tmpRegion, &tmpRegion,
                                                             pGC->pCompositeClip);

    ADD_TO_MODIFIED_REGION(pDrawable->pScreen, &tmpRegion);

    REGION_UNINIT(pDrawable->pScreen, &tmpRegion);

    (*pGC->ops->PushPixels) (pGC, pBitMap, pDrawable, w, h, x, y);

    SCHEDULE_FB_UPDATE(pDrawable->pScreen, pVNC);

    GC_OP_EPILOGUE(pGC);
}

#ifdef RENDER
void
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
){
    ScreenPtr pScreen = pDst->pDrawable->pScreen;
    VNCSCREENPTR(pScreen);
    RegionRec tmpRegion;
    BoxRec box;
    PictureScreenPtr ps = GetPictureScreen(pScreen);

    box.x1 = pDst->pDrawable->x + xDst;
    box.y1 = pDst->pDrawable->y + yDst;
    box.x2 = box.x1 + width;
    box.y2 = box.y1 + height;

    REGION_INIT(pScreen, &tmpRegion, &box, 0);

    ADD_TO_MODIFIED_REGION(pScreen, &tmpRegion);

    ps->Composite = pVNC->Composite;
    (*ps->Composite)(op, pSrc, pMask, pDst, xSrc, ySrc,
                     xMask, yMask, xDst, yDst, width, height);
    ps->Composite = rfbComposite;

    SCHEDULE_FB_UPDATE(pScreen, pVNC);

    REGION_UNINIT(pScreen, &tmpRegion);
}
#endif /* RENDER */

/****************************************************************************/
/*
 * Other functions
 */
/****************************************************************************/

/*
 * rfbCopyRegion.  Args are src and dst regions plus a translation (dx,dy).
 * Takes these args together with the existing modified region and possibly an
 * existing copy region and translation.  Produces a combined modified region
 * plus copy region and translation.  Note that the copy region is the
 * destination of the copy.
 *
 * First we trim parts of src which are invalid (ie in the modified region).
 * Then we see if there is any overlap between the src and the existing copy
 * region.  If not then the two copies cannot be combined, so we choose
 * whichever is bigger to form the basis of a new copy, while the other copy is
 * just done the hard way by being added to the modified region.  So if the
 * existing copy is bigger then we simply add the destination of the new copy
 * to the modified region and we're done.  If the new copy is bigger, we add
 * the old copy region to the modified region and behave as though there is no
 * existing copy region.
 * 
 * At this stage we now know that either the two copies can be combined, or
 * that there is no existing copy.  We temporarily add both the existing copy
 * region and dst to the modified region (this is the entire area of the screen
 * affected in any way).  Finally we calculate the new copy region, and remove
 * it from the modified region.
 *
 * Note:
 *   1. The src region is modified by this routine.
 *   2. When the copy region is empty, copyDX and copyDY MUST be set to zero.
 */

static void
rfbCopyRegion(pScreen, cl, src, dst, dx, dy)
    ScreenPtr pScreen;
    rfbClientPtr cl;
    RegionPtr src;
    RegionPtr dst;
    int dx, dy;
{
    RegionRec tmp;

    /* src = src - modifiedRegion */

    REGION_SUBTRACT(pScreen, src, src, &cl->modifiedRegion);

    if (REGION_NOTEMPTY(pScreen, &cl->copyRegion)) {

        REGION_NULL(pScreen, &tmp);
        REGION_INTERSECT(pScreen, &tmp, src, &cl->copyRegion);

        if (REGION_NOTEMPTY(pScreen, &tmp)) {

            /* if src and copyRegion overlap:
                 src = src intersect copyRegion */

            REGION_COPY(pScreen, src, &tmp);

        } else {

            /* if no overlap, find bigger region */

            int newArea = (((REGION_EXTENTS(pScreen,src))->x2
                            - (REGION_EXTENTS(pScreen,src))->x1)
                           * ((REGION_EXTENTS(pScreen,src))->y2
                              - (REGION_EXTENTS(pScreen,src))->y1));

            int oldArea = (((REGION_EXTENTS(pScreen,&cl->copyRegion))->x2
                            - (REGION_EXTENTS(pScreen,&cl->copyRegion))->x1)
                           * ((REGION_EXTENTS(pScreen,&cl->copyRegion))->y2
                             - (REGION_EXTENTS(pScreen,&cl->copyRegion))->y1));

            if (oldArea > newArea) {

                /* existing copy is bigger:
                     modifiedRegion = modifiedRegion union dst
                     copyRegion = copyRegion - dst
                     return */

                REGION_UNION(pScreen, &cl->modifiedRegion, &cl->modifiedRegion,
                             dst);
                REGION_SUBTRACT(pScreen, &cl->copyRegion, &cl->copyRegion,
                                dst);
                if (!REGION_NOTEMPTY(pScreen, &cl->copyRegion)) {
                    cl->copyDX = 0;
                    cl->copyDY = 0;
                }
                return;
            }

            /* new copy is bigger:
                 modifiedRegion = modifiedRegion union copyRegion
                 copyRegion = empty */

            REGION_UNION(pScreen, &cl->modifiedRegion, &cl->modifiedRegion,
                         &cl->copyRegion);
            REGION_EMPTY(pScreen, &cl->copyRegion);
            cl->copyDX = cl->copyDY = 0;
        }
    }


    /* modifiedRegion = modifiedRegion union dst union copyRegion */

    REGION_UNION(pScreen, &cl->modifiedRegion, &cl->modifiedRegion, dst);
    REGION_UNION(pScreen, &cl->modifiedRegion, &cl->modifiedRegion,
                 &cl->copyRegion);

    /* copyRegion = T(src) intersect dst */

    REGION_TRANSLATE(pScreen, src, dx, dy);
    REGION_INTERSECT(pScreen, &cl->copyRegion, src, dst);

    /* modifiedRegion = modifiedRegion - copyRegion */

    REGION_SUBTRACT(pScreen, &cl->modifiedRegion, &cl->modifiedRegion,
                    &cl->copyRegion);

    /* combine new translation T with existing translation */

    if (REGION_NOTEMPTY(pScreen, &cl->copyRegion)) {
        cl->copyDX += dx;
        cl->copyDY += dy;
    } else {
        cl->copyDX = 0;
        cl->copyDY = 0;
    }
}


/*
 * rfbDeferredUpdateCallback() is called when a client's deferredUpdateTimer
 * goes off.
 */

static CARD32
rfbDeferredUpdateCallback(OsTimerPtr timer, CARD32 now, pointer arg)
{
  rfbClientPtr cl = (rfbClientPtr)arg;

  rfbSendFramebufferUpdate(cl->pScreen, cl);

  cl->deferredUpdateScheduled = FALSE;
  return 0;
}


/*
 * rfbScheduleDeferredUpdate() is called from the SCHEDULE_FB_UPDATE macro
 * to schedule an update.
 */

static void
rfbScheduleDeferredUpdate(ScreenPtr pScreen, rfbClientPtr cl)
{
    if (rfbDeferUpdateTime != 0) {
        cl->deferredUpdateTimer = TimerSet(cl->deferredUpdateTimer, 0,
                                           rfbDeferUpdateTime,
                                           rfbDeferredUpdateCallback, cl);
        cl->deferredUpdateScheduled = TRUE;
    } else {
        rfbSendFramebufferUpdate(pScreen, cl);
    }
}


/*
 * PrintRegion is useful for debugging.
 */

#ifdef DEBUG
static void
PrintRegion(ScreenPtr pScreen, RegionPtr reg)
{
    int nrects = REGION_NUM_RECTS(reg);
    int i;

    ErrorF("Region num rects %d extents %d,%d %d,%d\n",nrects,
           (REGION_EXTENTS(pScreen,reg))->x1,
           (REGION_EXTENTS(pScreen,reg))->y1,
           (REGION_EXTENTS(pScreen,reg))->x2,
           (REGION_EXTENTS(pScreen,reg))->y2);

    for (i = 0; i < nrects; i++) {
        ErrorF("    rect %d,%d %dx%d\n",
               REGION_RECTS(reg)[i].x1,
               REGION_RECTS(reg)[i].y1,
               REGION_RECTS(reg)[i].x2-REGION_RECTS(reg)[i].x1,
               REGION_RECTS(reg)[i].y2-REGION_RECTS(reg)[i].y1);
    }
}
#endif

#ifdef SHAREDAPP
void rfbInvalidateRegion(ScreenPtr pScreen, RegionPtr reg)
{
  VNCSCREENPTR(pScreen);
  ADD_TO_MODIFIED_REGION(pScreen, reg);
  SCHEDULE_FB_UPDATE(pScreen, pVNC);
  return;
}
#endif

