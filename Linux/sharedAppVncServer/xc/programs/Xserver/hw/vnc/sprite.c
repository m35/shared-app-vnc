/*
 * sprite.c
 *
 * software sprite routines - based on misprite
 *
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

/* $XConsortium: misprite.c,v 5.47 94/04/17 20:27:53 dpw Exp $ */

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

# include   "X.h"
# include   "Xproto.h"
# include   "misc.h"
# include   "pixmapstr.h"
# include   "input.h"
# include   "mi.h"
# include   "cursorstr.h"
# include   "font.h"
# include   "scrnintstr.h"
# include   "colormapst.h"
# include   "windowstr.h"
# include   "gcstruct.h"
# include   "mipointer.h"
# include   "spritest.h"
# include   "dixfontstr.h"
# include   "fontstruct.h"
#ifdef RENDER
# include   "mipict.h"
#endif
#include "rfb.h"

/*
 * screen wrappers
 */

static int  rfbSpriteScreenIndex;
static unsigned long rfbSpriteGeneration = 0;

static Bool            rfbSpriteCloseScreen(int i, ScreenPtr pScreen);
static void            rfbSpriteGetImage(DrawablePtr pDrawable, int sx, int sy,
                                     int w, int h, unsigned int format,
                                     unsigned long planemask, char *pdstLine);
static void            rfbSpriteGetSpans(DrawablePtr pDrawable, int wMax,
                                     DDXPointPtr ppt, int *pwidth, int nspans,
                             char *pdstStart);
static void            rfbSpriteSourceValidate(DrawablePtr pDrawable, int x, int y,
                                           int width, int height);
static Bool            rfbSpriteCreateGC(GCPtr pGC);
static void            rfbSpriteBlockHandler(int i, pointer blockData,
                                         pointer pTimeout,
                                         pointer pReadMask);
static void            rfbSpriteInstallColormap(ColormapPtr pMap);
static void            rfbSpriteStoreColors(ColormapPtr pMap, int ndef,
                                        xColorItem *pdef);

static void            rfbSpritePaintWindowBackground(WindowPtr pWin,
                                                  RegionPtr pRegion, int what);
static void            rfbSpritePaintWindowBorder(WindowPtr pWin,
                                              RegionPtr pRegion, int what);
static void            rfbSpriteCopyWindow(WindowPtr pWin, DDXPointRec ptOldOrg,
                                       RegionPtr pRegion);
static void            rfbSpriteClearToBackground(WindowPtr pWin, int x, int y,
                                              int w, int h,
                                              Bool generateExposures);

#ifdef RENDER
static void            rfbSpriteComposite(CARD8        op,
                                      PicturePtr pSrc,
                                      PicturePtr pMask,
                                      PicturePtr pDst,
                                      INT16        xSrc,
                                      INT16        ySrc,
                                      INT16        xMask,
                                      INT16        yMask,
                                      INT16        xDst,
                                      INT16        yDst,
                                      CARD16        width,
                                      CARD16        height);

static void            rfbSpriteGlyphs(CARD8        op,
                                   PicturePtr        pSrc,
                                   PicturePtr        pDst,
                                   PictFormatPtr maskFormat,
                                   INT16        xSrc,
                                   INT16        ySrc,
                                   int                nlist,
                                   GlyphListPtr        list,
                                   GlyphPtr        *glyphs);
#endif

static void            rfbSpriteSaveDoomedAreas(WindowPtr pWin,
                                            RegionPtr pObscured, int dx,
                                            int dy);
static RegionPtr    rfbSpriteRestoreAreas(WindowPtr pWin, RegionPtr pRgnExposed);
static void            rfbSpriteComputeSaved(ScreenPtr pScreen);

#define SCREEN_PROLOGUE(pScreen, field)\
  ((pScreen)->field = \
   ((rfbSpriteScreenPtr) (pScreen)->devPrivates[rfbSpriteScreenIndex].ptr)->field) 

#define SCREEN_EPILOGUE(pScreen, field, wrapper)\
    ((pScreen)->field = wrapper)

/*
 * GC func wrappers
 */

static int  rfbSpriteGCIndex;

static void rfbSpriteValidateGC(GCPtr pGC, unsigned long stateChanges,
                               DrawablePtr pDrawable);
static void rfbSpriteCopyGC(GCPtr pGCSrc, unsigned long mask, GCPtr pGCDst);
static void rfbSpriteDestroyGC(GCPtr pGC);
static void rfbSpriteChangeGC(GCPtr pGC, unsigned long mask);
static void rfbSpriteChangeClip(GCPtr pGC, int type, pointer pvalue, int nrects);
static void rfbSpriteDestroyClip(GCPtr pGC);
static void rfbSpriteCopyClip(GCPtr pgcDst, GCPtr pgcSrc);

static GCFuncs        rfbSpriteGCFuncs = {
    rfbSpriteValidateGC,
    rfbSpriteChangeGC,
    rfbSpriteCopyGC,
    rfbSpriteDestroyGC,
    rfbSpriteChangeClip,
    rfbSpriteDestroyClip,
    rfbSpriteCopyClip,
};

#define GC_FUNC_PROLOGUE(pGC)                                        \
    rfbSpriteGCPtr   pGCPriv =                                        \
        (rfbSpriteGCPtr) (pGC)->devPrivates[rfbSpriteGCIndex].ptr;\
    (pGC)->funcs = pGCPriv->wrapFuncs;                                \
    if (pGCPriv->wrapOps)                                        \
        (pGC)->ops = pGCPriv->wrapOps;

#define GC_FUNC_EPILOGUE(pGC)                                        \
    pGCPriv->wrapFuncs = (pGC)->funcs;                                \
    (pGC)->funcs = &rfbSpriteGCFuncs;                                \
    if (pGCPriv->wrapOps)                                        \
    {                                                                \
        pGCPriv->wrapOps = (pGC)->ops;                                \
        (pGC)->ops = &rfbSpriteGCOps;                                \
    }

/*
 * GC op wrappers
 */

static void            rfbSpriteFillSpans(DrawablePtr pDrawable, GCPtr pGC,
                                      int nInit, DDXPointPtr pptInit,
                                      int *pwidthInit, int fSorted);
static void            rfbSpriteSetSpans(DrawablePtr pDrawable, GCPtr pGC,
                                     char *psrc, DDXPointPtr ppt, int *pwidth,
                                     int nspans, int fSorted);
static void            rfbSpritePutImage(DrawablePtr pDrawable, GCPtr pGC,
                                     int depth, int x, int y, int w, int h,
                                     int leftPad, int format, char *pBits);
static RegionPtr    rfbSpriteCopyArea(DrawablePtr pSrc, DrawablePtr pDst,
                                     GCPtr pGC, int srcx, int srcy, int w,
                                     int h, int dstx, int dsty);
static RegionPtr    rfbSpriteCopyPlane(DrawablePtr pSrc, DrawablePtr pDst,
                                     GCPtr pGC, int srcx, int srcy, int w,
                                     int h, int dstx, int dsty,
                                     unsigned long plane);
static void            rfbSpritePolyPoint(DrawablePtr pDrawable, GCPtr pGC,
                                      int mode, int npt, xPoint *pptInit);
static void            rfbSpritePolylines(DrawablePtr pDrawable, GCPtr pGC,
                                      int mode, int npt, DDXPointPtr pptInit);
static void            rfbSpritePolySegment(DrawablePtr pDrawable, GCPtr pGC,
                                        int nseg, xSegment *pSegs);
static void            rfbSpritePolyRectangle(DrawablePtr pDrawable, GCPtr pGC,
                                          int nrects, xRectangle *pRects);
static void            rfbSpritePolyArc(DrawablePtr pDrawable, GCPtr pGC,
                                    int narcs, xArc *parcs);
static void            rfbSpriteFillPolygon(DrawablePtr pDrawable, GCPtr pGC,
                                        int shape, int mode, int count,
                                        DDXPointPtr pPts);
static void            rfbSpritePolyFillRect(DrawablePtr pDrawable, GCPtr pGC,
                                         int nrectFill, xRectangle *prectInit);
static void            rfbSpritePolyFillArc(DrawablePtr pDrawable, GCPtr pGC,
                                        int narcs, xArc *parcs);
static int            rfbSpritePolyText8(DrawablePtr pDrawable, GCPtr pGC,
                                      int x, int y, int count, char *chars);
static int            rfbSpritePolyText16(DrawablePtr pDrawable, GCPtr pGC,
                                       int x, int y, int count,
                                       unsigned short *chars);
static void            rfbSpriteImageText8(DrawablePtr pDrawable, GCPtr pGC,
                                       int x, int y, int count, char *chars);
static void            rfbSpriteImageText16(DrawablePtr pDrawable, GCPtr pGC,
                                        int x, int y, int count,
                                        unsigned short *chars);
static void            rfbSpriteImageGlyphBlt(DrawablePtr pDrawable, GCPtr pGC,
                                          int x, int y, unsigned int nglyph,
                                          CharInfoPtr *ppci,
                                          pointer pglyphBase);
static void            rfbSpritePolyGlyphBlt(DrawablePtr pDrawable, GCPtr pGC,
                                         int x, int y, unsigned int nglyph,
                                         CharInfoPtr *ppci,
                                         pointer pglyphBase);
static void            rfbSpritePushPixels(GCPtr pGC, PixmapPtr pBitMap,
                                       DrawablePtr pDst, int w, int h,
                                       int x, int y);
#ifdef NEED_LINEHELPER
static void            rfbSpriteLineHelper();
#endif

static GCOps rfbSpriteGCOps = {
    rfbSpriteFillSpans,            rfbSpriteSetSpans,            rfbSpritePutImage,        
    rfbSpriteCopyArea,            rfbSpriteCopyPlane,            rfbSpritePolyPoint,
    rfbSpritePolylines,            rfbSpritePolySegment,   rfbSpritePolyRectangle,
    rfbSpritePolyArc,            rfbSpriteFillPolygon,   rfbSpritePolyFillRect,
    rfbSpritePolyFillArc,   rfbSpritePolyText8,            rfbSpritePolyText16,
    rfbSpriteImageText8,    rfbSpriteImageText16,   rfbSpriteImageGlyphBlt,
    rfbSpritePolyGlyphBlt,  rfbSpritePushPixels
#ifdef NEED_LINEHELPER
    , rfbSpriteLineHelper
#endif
};

/*
 * testing only -- remove cursor for every draw.  Eventually,
 * each draw operation will perform a bounding box check against
 * the saved cursor area
 */

#define GC_SETUP_CHEAP(pDrawable)                                    \
    rfbSpriteScreenPtr        pScreenPriv = (rfbSpriteScreenPtr)            \
        (pDrawable)->pScreen->devPrivates[rfbSpriteScreenIndex].ptr; \

#define GC_SETUP(pDrawable, pGC)                                    \
    GC_SETUP_CHEAP(pDrawable)                                            \
    rfbSpriteGCPtr        pGCPrivate = (rfbSpriteGCPtr)                    \
        (pGC)->devPrivates[rfbSpriteGCIndex].ptr;                    \
    GCFuncs *oldFuncs = pGC->funcs;

#define GC_SETUP_AND_CHECK(pDrawable, pGC)                            \
    GC_SETUP(pDrawable, pGC);                                            \
    if (GC_CHECK((WindowPtr)pDrawable))                                    \
        rfbSpriteRemoveCursor (pDrawable->pScreen);
    
#define GC_CHECK(pWin)                                                    \
    (pVNC->cursorIsDrawn &&                                            \
        (pScreenPriv->pCacheWin == pWin ?                            \
            pScreenPriv->isInCacheWin : (                            \
            (pScreenPriv->pCacheWin = (pWin)) ,                            \
            (pScreenPriv->isInCacheWin =                            \
                (pWin)->drawable.x < pScreenPriv->saved.x2 &&            \
                pScreenPriv->saved.x1 < (pWin)->drawable.x +            \
                                    (int) (pWin)->drawable.width && \
                (pWin)->drawable.y < pScreenPriv->saved.y2 &&            \
                pScreenPriv->saved.y1 < (pWin)->drawable.y +            \
                                    (int) (pWin)->drawable.height &&\
                RECT_IN_REGION((pWin)->drawable.pScreen, &(pWin)->borderClip, \
                        &pScreenPriv->saved) != rgnOUT))))

#define GC_OP_PROLOGUE(pGC) { \
    (pGC)->funcs = pGCPrivate->wrapFuncs; \
    (pGC)->ops = pGCPrivate->wrapOps; \
    }

#define GC_OP_EPILOGUE(pGC) { \
    pGCPrivate->wrapOps = (pGC)->ops; \
    (pGC)->funcs = oldFuncs; \
    (pGC)->ops = &rfbSpriteGCOps; \
    }

/*
 * pointer-sprite method table
 */

static Bool rfbSpriteRealizeCursor (),        rfbSpriteUnrealizeCursor ();
static void rfbSpriteSetCursor (),        rfbSpriteMoveCursor ();

miPointerSpriteFuncRec rfbSpritePointerFuncs = {
    rfbSpriteRealizeCursor,
    rfbSpriteUnrealizeCursor,
    rfbSpriteSetCursor,
    rfbSpriteMoveCursor,
};

/*
 * other misc functions
 */

static Bool rfbDisplayCursor (ScreenPtr pScreen, CursorPtr pCursor);


/*
 * rfbSpriteInitialize -- called from device-dependent screen
 * initialization proc after all of the function pointers have
 * been stored in the screen structure.
 */

Bool
rfbSpriteInitialize (pScreen, cursorFuncs, screenFuncs)
    ScreenPtr                    pScreen;
    rfbSpriteCursorFuncPtr   cursorFuncs;
    miPointerScreenFuncPtr  screenFuncs;
{
    rfbSpriteScreenPtr        pPriv;
    VisualPtr                pVisual;
#ifdef RENDER
    PictureScreenPtr        ps = GetPictureScreenIfSet(pScreen);
#endif
    
    if (rfbSpriteGeneration != serverGeneration)
    {
        rfbSpriteScreenIndex = AllocateScreenPrivateIndex ();
        if (rfbSpriteScreenIndex < 0)
            return FALSE;
        rfbSpriteGeneration = serverGeneration;
        rfbSpriteGCIndex = AllocateGCPrivateIndex ();
    }
    if (!AllocateGCPrivate(pScreen, rfbSpriteGCIndex, sizeof(rfbSpriteGCRec)))
        return FALSE;
    pPriv = (rfbSpriteScreenPtr) xalloc (sizeof (rfbSpriteScreenRec));
    if (!pPriv)
        return FALSE;
    if (!miPointerInitialize (pScreen, &rfbSpritePointerFuncs, screenFuncs,TRUE))
    {
        xfree ((pointer) pPriv);
        return FALSE;
    }
    for (pVisual = pScreen->visuals;
         pVisual->vid != pScreen->rootVisual;
         pVisual++)
        ;
    pPriv->pVisual = pVisual;
    pPriv->CloseScreen = pScreen->CloseScreen;
    pPriv->GetImage = pScreen->GetImage;
    pPriv->GetSpans = pScreen->GetSpans;
    pPriv->SourceValidate = pScreen->SourceValidate;
    pPriv->CreateGC = pScreen->CreateGC;
#if 0
    pPriv->BlockHandler = pScreen->BlockHandler;
#endif
    pPriv->InstallColormap = pScreen->InstallColormap;
    pPriv->StoreColors = pScreen->StoreColors;
    pPriv->DisplayCursor = pScreen->DisplayCursor;

    pPriv->PaintWindowBackground = pScreen->PaintWindowBackground;
    pPriv->PaintWindowBorder = pScreen->PaintWindowBorder;
    pPriv->CopyWindow = pScreen->CopyWindow;
    pPriv->ClearToBackground = pScreen->ClearToBackground;

    pPriv->SaveDoomedAreas = pScreen->SaveDoomedAreas;
    pPriv->RestoreAreas = pScreen->RestoreAreas;
#ifdef RENDER
    if (ps)
    {
        pPriv->Composite = ps->Composite;
        pPriv->Glyphs = ps->Glyphs;
    }
#endif

    pPriv->pCursor = NULL;
    pPriv->x = 0;
    pPriv->y = 0;
    pPriv->shouldBeUp = FALSE;
    pPriv->pCacheWin = NullWindow;
    pPriv->isInCacheWin = FALSE;
    pPriv->checkPixels = TRUE;
    pPriv->pInstalledMap = NULL;
    pPriv->pColormap = NULL;
    pPriv->funcs = cursorFuncs;
    pPriv->colors[SOURCE_COLOR].red = 0;
    pPriv->colors[SOURCE_COLOR].green = 0;
    pPriv->colors[SOURCE_COLOR].blue = 0;
    pPriv->colors[MASK_COLOR].red = 0;
    pPriv->colors[MASK_COLOR].green = 0;
    pPriv->colors[MASK_COLOR].blue = 0;
    pScreen->devPrivates[rfbSpriteScreenIndex].ptr = (pointer) pPriv;
    pScreen->CloseScreen = rfbSpriteCloseScreen;
    pScreen->GetImage = rfbSpriteGetImage;
    pScreen->GetSpans = rfbSpriteGetSpans;
    pScreen->SourceValidate = rfbSpriteSourceValidate;
    pScreen->CreateGC = rfbSpriteCreateGC;
#if 0
    pScreen->BlockHandler = rfbSpriteBlockHandler;
#endif
    pScreen->InstallColormap = rfbSpriteInstallColormap;
    pScreen->StoreColors = rfbSpriteStoreColors;

    pScreen->PaintWindowBackground = rfbSpritePaintWindowBackground;
    pScreen->PaintWindowBorder = rfbSpritePaintWindowBorder;
    pScreen->CopyWindow = rfbSpriteCopyWindow;
    pScreen->ClearToBackground = rfbSpriteClearToBackground;

    pScreen->SaveDoomedAreas = rfbSpriteSaveDoomedAreas;
    pScreen->RestoreAreas = rfbSpriteRestoreAreas;

    pScreen->DisplayCursor = rfbDisplayCursor;
#ifdef RENDER
    if (ps)
    {
        ps->Composite = rfbSpriteComposite;
        ps->Glyphs = rfbSpriteGlyphs;
    }
#endif

    return TRUE;
}

/*
 * Screen wrappers
 */

/*
 * CloseScreen wrapper -- unwrap everything, free the private data
 * and call the wrapped function
 */

static Bool
rfbSpriteCloseScreen (i, pScreen)
    ScreenPtr        pScreen;
{
    rfbSpriteScreenPtr   pScreenPriv;
#ifdef RENDER
    PictureScreenPtr        ps = GetPictureScreenIfSet(pScreen);
#endif

    pScreenPriv = (rfbSpriteScreenPtr) pScreen->devPrivates[rfbSpriteScreenIndex].ptr;

    pScreen->CloseScreen = pScreenPriv->CloseScreen;
    pScreen->GetImage = pScreenPriv->GetImage;
    pScreen->GetSpans = pScreenPriv->GetSpans;
    pScreen->SourceValidate = pScreenPriv->SourceValidate;
    pScreen->CreateGC = pScreenPriv->CreateGC;
#if 0
    pScreen->BlockHandler = pScreenPriv->BlockHandler;
#endif
    pScreen->InstallColormap = pScreenPriv->InstallColormap;
    pScreen->StoreColors = pScreenPriv->StoreColors;

    pScreen->PaintWindowBackground = pScreenPriv->PaintWindowBackground;
    pScreen->PaintWindowBorder = pScreenPriv->PaintWindowBorder;
    pScreen->CopyWindow = pScreenPriv->CopyWindow;
    pScreen->ClearToBackground = pScreenPriv->ClearToBackground;

    pScreen->SaveDoomedAreas = pScreenPriv->SaveDoomedAreas;
    pScreen->RestoreAreas = pScreenPriv->RestoreAreas;
#ifdef RENDER
    if (ps)
    {
        ps->Composite = pScreenPriv->Composite;
        ps->Glyphs = pScreenPriv->Glyphs;
    }
#endif

    xfree ((pointer) pScreenPriv);

    return (*pScreen->CloseScreen) (i, pScreen);
}

static void
rfbSpriteGetImage (pDrawable, sx, sy, w, h, format, planemask, pdstLine)
    DrawablePtr            pDrawable;
    int                    sx, sy, w, h;
    unsigned int    format;
    unsigned long   planemask;
    char            *pdstLine;
{
    ScreenPtr            pScreen = pDrawable->pScreen;
    rfbSpriteScreenPtr    pScreenPriv;
    VNCSCREENPTR(pScreen);
    
    SCREEN_PROLOGUE (pScreen, GetImage);

    pScreenPriv = (rfbSpriteScreenPtr) pScreen->devPrivates[rfbSpriteScreenIndex].ptr;

    if (pDrawable->type == DRAWABLE_WINDOW &&
        pVNC->cursorIsDrawn &&
        ORG_OVERLAP(&pScreenPriv->saved,pDrawable->x,pDrawable->y, sx, sy, w, h))
    {
        rfbSpriteRemoveCursor (pScreen);
    }

    (*pScreen->GetImage) (pDrawable, sx, sy, w, h,
                          format, planemask, pdstLine);

    SCREEN_EPILOGUE (pScreen, GetImage, rfbSpriteGetImage);
}

static void
rfbSpriteGetSpans (pDrawable, wMax, ppt, pwidth, nspans, pdstStart)
    DrawablePtr        pDrawable;
    int                wMax;
    DDXPointPtr        ppt;
    int                *pwidth;
    int                nspans;
    char        *pdstStart;
{
    ScreenPtr                    pScreen = pDrawable->pScreen;
    rfbSpriteScreenPtr            pScreenPriv;
    VNCSCREENPTR(pScreen);
    
    SCREEN_PROLOGUE (pScreen, GetSpans);

    pScreenPriv = (rfbSpriteScreenPtr) pScreen->devPrivates[rfbSpriteScreenIndex].ptr;

    if (pDrawable->type == DRAWABLE_WINDOW && pVNC->cursorIsDrawn)
    {
        register DDXPointPtr    pts;
        register int            *widths;
        register int            nPts;
        register int            xorg,
                                yorg;

        xorg = pDrawable->x;
        yorg = pDrawable->y;

        for (pts = ppt, widths = pwidth, nPts = nspans;
             nPts--;
             pts++, widths++)
         {
            if (SPN_OVERLAP(&pScreenPriv->saved,pts->y+yorg,
                             pts->x+xorg,*widths))
            {
                rfbSpriteRemoveCursor (pScreen);
                break;
            }
        }
    }

    (*pScreen->GetSpans) (pDrawable, wMax, ppt, pwidth, nspans, pdstStart);

    SCREEN_EPILOGUE (pScreen, GetSpans, rfbSpriteGetSpans);
}

static void
rfbSpriteSourceValidate (pDrawable, x, y, width, height)
    DrawablePtr        pDrawable;
    int                x, y, width, height;
{
    ScreenPtr                    pScreen = pDrawable->pScreen;
    rfbSpriteScreenPtr            pScreenPriv;
    VNCSCREENPTR(pScreen);
    
    SCREEN_PROLOGUE (pScreen, SourceValidate);

    pScreenPriv = (rfbSpriteScreenPtr) pScreen->devPrivates[rfbSpriteScreenIndex].ptr;

    if (pDrawable->type == DRAWABLE_WINDOW && pVNC->cursorIsDrawn &&
        ORG_OVERLAP(&pScreenPriv->saved, pDrawable->x, pDrawable->y,
                    x, y, width, height))
    {
        rfbSpriteRemoveCursor (pScreen);
    }

    if (pScreen->SourceValidate)
        (*pScreen->SourceValidate) (pDrawable, x, y, width, height);

    SCREEN_EPILOGUE (pScreen, SourceValidate, rfbSpriteSourceValidate);
}

static Bool
rfbSpriteCreateGC (pGC)
    GCPtr   pGC;
{
    ScreenPtr            pScreen = pGC->pScreen;
    Bool            ret;
    rfbSpriteGCPtr   pPriv;

    SCREEN_PROLOGUE (pScreen, CreateGC);
    
    pPriv = (rfbSpriteGCPtr)pGC->devPrivates[rfbSpriteGCIndex].ptr;

    ret = (*pScreen->CreateGC) (pGC);

    pPriv->wrapOps = NULL;
    pPriv->wrapFuncs = pGC->funcs;
    pGC->funcs = &rfbSpriteGCFuncs;

    SCREEN_EPILOGUE (pScreen, CreateGC, rfbSpriteCreateGC);

    return ret;
}

static void
rfbSpriteBlockHandler (i, blockData, pTimeout, pReadmask)
    int        i;
    pointer        blockData;
    pointer        pTimeout;
    pointer        pReadmask;
{
    ScreenPtr                pScreen = screenInfo.screens[i];
    rfbSpriteScreenPtr        pPriv;
    VNCSCREENPTR(pScreen);

    pPriv = (rfbSpriteScreenPtr) pScreen->devPrivates[rfbSpriteScreenIndex].ptr;

    SCREEN_PROLOGUE(pScreen, BlockHandler);
    
    (*pScreen->BlockHandler) (i, blockData, pTimeout, pReadmask);

    SCREEN_EPILOGUE(pScreen, BlockHandler, rfbSpriteBlockHandler);

    if (!pVNC->cursorIsDrawn && pPriv->shouldBeUp)
        rfbSpriteRestoreCursor (pScreen);
}

static void
rfbSpriteInstallColormap (pMap)
    ColormapPtr        pMap;
{
    ScreenPtr                pScreen = pMap->pScreen;
    rfbSpriteScreenPtr        pPriv;
    VNCSCREENPTR(pScreen);

    pPriv = (rfbSpriteScreenPtr) pScreen->devPrivates[rfbSpriteScreenIndex].ptr;

    SCREEN_PROLOGUE(pScreen, InstallColormap);
    
    (*pScreen->InstallColormap) (pMap);

    SCREEN_EPILOGUE(pScreen, InstallColormap, rfbSpriteInstallColormap);

    pPriv->pInstalledMap = pMap;
    if (pPriv->pColormap != pMap)
    {
            pPriv->checkPixels = TRUE;
        if (pVNC->cursorIsDrawn)
            rfbSpriteRemoveCursor (pScreen);
    }
}

static void
rfbSpriteStoreColors (pMap, ndef, pdef)
    ColormapPtr        pMap;
    int                ndef;
    xColorItem        *pdef;
{
    ScreenPtr                pScreen = pMap->pScreen;
    rfbSpriteScreenPtr        pPriv;
    int                        i;
    int                        updated;
    VisualPtr                pVisual;
    VNCSCREENPTR(pScreen);

    pPriv = (rfbSpriteScreenPtr) pScreen->devPrivates[rfbSpriteScreenIndex].ptr;

    SCREEN_PROLOGUE(pScreen, StoreColors);
    
    (*pScreen->StoreColors) (pMap, ndef, pdef);

    SCREEN_EPILOGUE(pScreen, StoreColors, rfbSpriteStoreColors);

    if (pPriv->pColormap == pMap)
    {
        updated = 0;
        pVisual = pMap->pVisual;
        if (pVisual->class == DirectColor)
        {
            /* Direct color - match on any of the subfields */

#define MaskMatch(a,b,mask) ((a) & ((pVisual->mask) == (b)) & (pVisual->mask))

#define UpdateDAC(plane,dac,mask) {\
    if (MaskMatch (pPriv->colors[plane].pixel,pdef[i].pixel,mask)) {\
        pPriv->colors[plane].dac = pdef[i].dac; \
        updated = 1; \
    } \
}

#define CheckDirect(plane) \
            UpdateDAC(plane,red,redMask) \
            UpdateDAC(plane,green,greenMask) \
            UpdateDAC(plane,blue,blueMask)

            for (i = 0; i < ndef; i++)
            {
                CheckDirect (SOURCE_COLOR)
                CheckDirect (MASK_COLOR)
            }
        }
        else
        {
            /* PseudoColor/GrayScale - match on exact pixel */
            for (i = 0; i < ndef; i++)
            {
                    if (pdef[i].pixel == pPriv->colors[SOURCE_COLOR].pixel)
                    {
                    pPriv->colors[SOURCE_COLOR] = pdef[i];
                    if (++updated == 2)
                            break;
                    }
                    if (pdef[i].pixel == pPriv->colors[MASK_COLOR].pixel)
                    {
                    pPriv->colors[MASK_COLOR] = pdef[i];
                    if (++updated == 2)
                            break;
                    }
            }
        }
            if (updated)
            {
            pPriv->checkPixels = TRUE;
            if (pVNC->cursorIsDrawn)
                    rfbSpriteRemoveCursor (pScreen);
            }
    }
}

static void
rfbSpriteFindColors (pScreen)
    ScreenPtr        pScreen;
{
    rfbSpriteScreenPtr        pScreenPriv = (rfbSpriteScreenPtr)
                            pScreen->devPrivates[rfbSpriteScreenIndex].ptr;
    CursorPtr                pCursor;
    xColorItem                *sourceColor, *maskColor;

    pCursor = pScreenPriv->pCursor;
    sourceColor = &pScreenPriv->colors[SOURCE_COLOR];
    maskColor = &pScreenPriv->colors[MASK_COLOR];
    if (pScreenPriv->pColormap != pScreenPriv->pInstalledMap ||
        !(pCursor->foreRed == sourceColor->red &&
          pCursor->foreGreen == sourceColor->green &&
          pCursor->foreBlue == sourceColor->blue &&
          pCursor->backRed == maskColor->red &&
          pCursor->backGreen == maskColor->green &&
          pCursor->backBlue == maskColor->blue))
    {
        pScreenPriv->pColormap = pScreenPriv->pInstalledMap;
        sourceColor->red = pCursor->foreRed;
        sourceColor->green = pCursor->foreGreen;
        sourceColor->blue = pCursor->foreBlue;
        FakeAllocColor (pScreenPriv->pColormap, sourceColor);
        maskColor->red = pCursor->backRed;
        maskColor->green = pCursor->backGreen;
        maskColor->blue = pCursor->backBlue;
        FakeAllocColor (pScreenPriv->pColormap, maskColor);
        /* "free" the pixels right away, don't let this confuse you */
        FakeFreeColor(pScreenPriv->pColormap, sourceColor->pixel);
        FakeFreeColor(pScreenPriv->pColormap, maskColor->pixel);
    }
    pScreenPriv->checkPixels = FALSE;
}

/*
 * BackingStore wrappers
 */

static void
rfbSpriteSaveDoomedAreas (pWin, pObscured, dx, dy)
    WindowPtr        pWin;
    RegionPtr        pObscured;
    int                dx, dy;
{
    ScreenPtr                pScreen = pWin->drawable.pScreen;
    rfbSpriteScreenPtr   pScreenPriv;
    BoxRec                cursorBox;
    VNCSCREENPTR(pScreen);
    
    SCREEN_PROLOGUE (pScreen, SaveDoomedAreas);

    pScreenPriv = (rfbSpriteScreenPtr) pScreen->devPrivates[rfbSpriteScreenIndex].ptr;
    if (pVNC->cursorIsDrawn)
    {
        cursorBox = pScreenPriv->saved;

        if (dx || dy)
         {
            cursorBox.x1 += dx;
            cursorBox.y1 += dy;
            cursorBox.x2 += dx;
            cursorBox.y2 += dy;
        }
        if (RECT_IN_REGION( pScreen, pObscured, &cursorBox) != rgnOUT)
            rfbSpriteRemoveCursor (pScreen);
    }

    (*pScreen->SaveDoomedAreas) (pWin, pObscured, dx, dy);

    SCREEN_EPILOGUE (pScreen, SaveDoomedAreas, rfbSpriteSaveDoomedAreas);
}

static RegionPtr
rfbSpriteRestoreAreas (pWin, prgnExposed)
    WindowPtr        pWin;
    RegionPtr        prgnExposed;
{
    ScreenPtr                pScreen = pWin->drawable.pScreen;
    rfbSpriteScreenPtr   pScreenPriv;
    RegionPtr                result;
    VNCSCREENPTR(pScreen);

    SCREEN_PROLOGUE (pScreen, RestoreAreas);

    pScreenPriv = (rfbSpriteScreenPtr) pScreen->devPrivates[rfbSpriteScreenIndex].ptr;
    if (pVNC->cursorIsDrawn)
    {
        if (RECT_IN_REGION( pScreen, prgnExposed, &pScreenPriv->saved) != rgnOUT)
            rfbSpriteRemoveCursor (pScreen);
    }

    result = (*pScreen->RestoreAreas) (pWin, prgnExposed);

    SCREEN_EPILOGUE (pScreen, RestoreAreas, rfbSpriteRestoreAreas);

    return result;
}

/*
 * Window wrappers
 */

static void
rfbSpritePaintWindowBackground (pWin, pRegion, what)
    WindowPtr        pWin;
    RegionPtr        pRegion;
    int                what;
{
    ScreenPtr            pScreen = pWin->drawable.pScreen;
    rfbSpriteScreenPtr    pScreenPriv;
    VNCSCREENPTR(pScreen);

    SCREEN_PROLOGUE (pScreen, PaintWindowBackground);

    pScreenPriv = (rfbSpriteScreenPtr) pScreen->devPrivates[rfbSpriteScreenIndex].ptr;
    if (pVNC->cursorIsDrawn)
    {
        /*
         * If the cursor is on the same screen as the window, check the
         * region to paint for the cursor and remove it as necessary
         */
        if (RECT_IN_REGION( pScreen, pRegion, &pScreenPriv->saved) != rgnOUT)
            rfbSpriteRemoveCursor (pScreen);
    }

    (*pScreen->PaintWindowBackground) (pWin, pRegion, what);

    SCREEN_EPILOGUE (pScreen, PaintWindowBackground, rfbSpritePaintWindowBackground);
}

static void
rfbSpritePaintWindowBorder (pWin, pRegion, what)
    WindowPtr        pWin;
    RegionPtr        pRegion;
    int                what;
{
    ScreenPtr            pScreen = pWin->drawable.pScreen;
    rfbSpriteScreenPtr    pScreenPriv;
    VNCSCREENPTR(pScreen);

    SCREEN_PROLOGUE (pScreen, PaintWindowBorder);

    pScreenPriv = (rfbSpriteScreenPtr) pScreen->devPrivates[rfbSpriteScreenIndex].ptr;
    if (pVNC->cursorIsDrawn)
    {
        /*
         * If the cursor is on the same screen as the window, check the
         * region to paint for the cursor and remove it as necessary
         */
        if (RECT_IN_REGION( pScreen, pRegion, &pScreenPriv->saved) != rgnOUT)
            rfbSpriteRemoveCursor (pScreen);
    }

    (*pScreen->PaintWindowBorder) (pWin, pRegion, what);

    SCREEN_EPILOGUE (pScreen, PaintWindowBorder, rfbSpritePaintWindowBorder);
}

static void
rfbSpriteCopyWindow (pWin, ptOldOrg, pRegion)
    WindowPtr        pWin;
    DDXPointRec        ptOldOrg;
    RegionPtr        pRegion;
{
    ScreenPtr            pScreen = pWin->drawable.pScreen;
    rfbSpriteScreenPtr    pScreenPriv;
    BoxRec            cursorBox;
    int                    dx, dy;
    VNCSCREENPTR(pScreen);

    SCREEN_PROLOGUE (pScreen, CopyWindow);

    pScreenPriv = (rfbSpriteScreenPtr) pScreen->devPrivates[rfbSpriteScreenIndex].ptr;
    if (pVNC->cursorIsDrawn)
    {
        /*
         * check both the source and the destination areas.  The given
         * region is source relative, so offset the cursor box by
         * the delta position
         */
        cursorBox = pScreenPriv->saved;
        dx = pWin->drawable.x - ptOldOrg.x;
        dy = pWin->drawable.y - ptOldOrg.y;
        cursorBox.x1 -= dx;
        cursorBox.x2 -= dx;
        cursorBox.y1 -= dy;
        cursorBox.y2 -= dy;
        if (RECT_IN_REGION( pScreen, pRegion, &pScreenPriv->saved) != rgnOUT ||
            RECT_IN_REGION( pScreen, pRegion, &cursorBox) != rgnOUT)
            rfbSpriteRemoveCursor (pScreen);
    }

    (*pScreen->CopyWindow) (pWin, ptOldOrg, pRegion);

    SCREEN_EPILOGUE (pScreen, CopyWindow, rfbSpriteCopyWindow);
}

static void
rfbSpriteClearToBackground (pWin, x, y, w, h, generateExposures)
    WindowPtr pWin;
    short x,y;
    unsigned short w,h;
    Bool generateExposures;
{
    ScreenPtr                pScreen = pWin->drawable.pScreen;
    rfbSpriteScreenPtr        pScreenPriv;
    int                        realw, realh;
    VNCSCREENPTR(pScreen);

    SCREEN_PROLOGUE (pScreen, ClearToBackground);

    pScreenPriv = (rfbSpriteScreenPtr) pScreen->devPrivates[rfbSpriteScreenIndex].ptr;
    if (GC_CHECK(pWin))
    {
        if (!(realw = w))
            realw = (int) pWin->drawable.width - x;
        if (!(realh = h))
            realh = (int) pWin->drawable.height - y;
        if (ORG_OVERLAP(&pScreenPriv->saved, pWin->drawable.x, pWin->drawable.y,
                        x, y, realw, realh))
        {
            rfbSpriteRemoveCursor (pScreen);
        }
    }

    (*pScreen->ClearToBackground) (pWin, x, y, w, h, generateExposures);

    SCREEN_EPILOGUE (pScreen, ClearToBackground, rfbSpriteClearToBackground);
}

/*
 * GC Func wrappers
 */

static void
rfbSpriteValidateGC (pGC, changes, pDrawable)
    GCPtr        pGC;
    unsigned long        changes;
    DrawablePtr        pDrawable;
{
    GC_FUNC_PROLOGUE (pGC);

    (*pGC->funcs->ValidateGC) (pGC, changes, pDrawable);
    
    pGCPriv->wrapOps = NULL;
    if (pDrawable->type == DRAWABLE_WINDOW && ((WindowPtr) pDrawable)->viewable)
    {
        WindowPtr   pWin;
        RegionPtr   pRegion;

        pWin = (WindowPtr) pDrawable;
        pRegion = &pWin->clipList;
        if (pGC->subWindowMode == IncludeInferiors)
            pRegion = &pWin->borderClip;
        if (REGION_NOTEMPTY(pDrawable->pScreen, pRegion))
            pGCPriv->wrapOps = pGC->ops;
    }

    GC_FUNC_EPILOGUE (pGC);
}

static void
rfbSpriteChangeGC (pGC, mask)
    GCPtr            pGC;
    unsigned long   mask;
{
    GC_FUNC_PROLOGUE (pGC);

    (*pGC->funcs->ChangeGC) (pGC, mask);
    
    GC_FUNC_EPILOGUE (pGC);
}

static void
rfbSpriteCopyGC (pGCSrc, mask, pGCDst)
    GCPtr            pGCSrc, pGCDst;
    unsigned long   mask;
{
    GC_FUNC_PROLOGUE (pGCDst);

    (*pGCDst->funcs->CopyGC) (pGCSrc, mask, pGCDst);
    
    GC_FUNC_EPILOGUE (pGCDst);
}

static void
rfbSpriteDestroyGC (pGC)
    GCPtr   pGC;
{
    GC_FUNC_PROLOGUE (pGC);

    (*pGC->funcs->DestroyGC) (pGC);
    
    GC_FUNC_EPILOGUE (pGC);
}

static void
rfbSpriteChangeClip (pGC, type, pvalue, nrects)
    GCPtr   pGC;
    int                type;
    pointer        pvalue;
    int                nrects;
{
    GC_FUNC_PROLOGUE (pGC);

    (*pGC->funcs->ChangeClip) (pGC, type, pvalue, nrects);

    GC_FUNC_EPILOGUE (pGC);
}

static void
rfbSpriteCopyClip(pgcDst, pgcSrc)
    GCPtr pgcDst, pgcSrc;
{
    GC_FUNC_PROLOGUE (pgcDst);

    (* pgcDst->funcs->CopyClip)(pgcDst, pgcSrc);

    GC_FUNC_EPILOGUE (pgcDst);
}

static void
rfbSpriteDestroyClip(pGC)
    GCPtr        pGC;
{
    GC_FUNC_PROLOGUE (pGC);

    (* pGC->funcs->DestroyClip)(pGC);

    GC_FUNC_EPILOGUE (pGC);
}

/*
 * GC Op wrappers
 */

static void
rfbSpriteFillSpans(pDrawable, pGC, nInit, pptInit, pwidthInit, fSorted)
    DrawablePtr pDrawable;
    GCPtr        pGC;
    int                nInit;                        /* number of spans to fill */
    DDXPointPtr pptInit;                /* pointer to list of start points */
    int                *pwidthInit;                /* pointer to list of n widths */
    int         fSorted;
{
    ScreenPtr                 pScreen = pDrawable->pScreen;
    VNCSCREENPTR(pScreen);

    GC_SETUP(pDrawable, pGC);

    if (GC_CHECK((WindowPtr) pDrawable))
    {
        register DDXPointPtr    pts;
        register int            *widths;
        register int            nPts;

        for (pts = pptInit, widths = pwidthInit, nPts = nInit;
             nPts--;
             pts++, widths++)
         {
             if (SPN_OVERLAP(&pScreenPriv->saved,pts->y,pts->x,*widths))
             {
                 rfbSpriteRemoveCursor (pDrawable->pScreen);
                 break;
             }
        }
    }

    GC_OP_PROLOGUE (pGC);

    (*pGC->ops->FillSpans) (pDrawable, pGC, nInit, pptInit, pwidthInit, fSorted);

    GC_OP_EPILOGUE (pGC);
}

static void
rfbSpriteSetSpans(pDrawable, pGC, psrc, ppt, pwidth, nspans, fSorted)
    DrawablePtr                pDrawable;
    GCPtr                pGC;
    char                *psrc;
    register DDXPointPtr ppt;
    int                        *pwidth;
    int                        nspans;
    int                        fSorted;
{
    ScreenPtr                 pScreen = pDrawable->pScreen;
    VNCSCREENPTR(pScreen);

    GC_SETUP(pDrawable, pGC);

    if (GC_CHECK((WindowPtr) pDrawable))
    {
        register DDXPointPtr    pts;
        register int            *widths;
        register int            nPts;

        for (pts = ppt, widths = pwidth, nPts = nspans;
             nPts--;
             pts++, widths++)
         {
             if (SPN_OVERLAP(&pScreenPriv->saved,pts->y,pts->x,*widths))
             {
                 rfbSpriteRemoveCursor(pDrawable->pScreen);
                 break;
             }
        }
    }

    GC_OP_PROLOGUE (pGC);

    (*pGC->ops->SetSpans) (pDrawable, pGC, psrc, ppt, pwidth, nspans, fSorted);

    GC_OP_EPILOGUE (pGC);
}

static void
rfbSpritePutImage(pDrawable, pGC, depth, x, y, w, h, leftPad, format, pBits)
    DrawablePtr          pDrawable;
    GCPtr             pGC;
    int                  depth;
    int                      x;
    int                      y;
    int                      w;
    int                      h;
    int                      format;
    char              *pBits;
{
    ScreenPtr                 pScreen = pDrawable->pScreen;
    VNCSCREENPTR(pScreen);

    GC_SETUP(pDrawable, pGC);

    if (GC_CHECK((WindowPtr) pDrawable))
    {
        if (ORG_OVERLAP(&pScreenPriv->saved,pDrawable->x,pDrawable->y,
                        x,y,w,h))
         {
            rfbSpriteRemoveCursor (pDrawable->pScreen);
        }
    }

    GC_OP_PROLOGUE (pGC);

    (*pGC->ops->PutImage) (pDrawable, pGC, depth, x, y, w, h, leftPad, format, pBits);

    GC_OP_EPILOGUE (pGC);
}

static RegionPtr
rfbSpriteCopyArea (pSrc, pDst, pGC, srcx, srcy, w, h, dstx, dsty)
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
    RegionPtr rgn;
    ScreenPtr                 pScreen = pGC->pScreen;
    VNCSCREENPTR(pScreen);

    GC_SETUP(pDst, pGC);

    /* check destination/source overlap. */
    if (GC_CHECK((WindowPtr) pDst) &&
         (ORG_OVERLAP(&pScreenPriv->saved,pDst->x,pDst->y,dstx,dsty,w,h) ||
          ((pDst == pSrc) &&
           ORG_OVERLAP(&pScreenPriv->saved,pSrc->x,pSrc->y,srcx,srcy,w,h))))
    {
        rfbSpriteRemoveCursor (pDst->pScreen);
    }
 
    GC_OP_PROLOGUE (pGC);

    rgn = (*pGC->ops->CopyArea) (pSrc, pDst, pGC, srcx, srcy, w, h,
                                 dstx, dsty);

    GC_OP_EPILOGUE (pGC);

    return rgn;
}

static RegionPtr
rfbSpriteCopyPlane (pSrc, pDst, pGC, srcx, srcy, w, h, dstx, dsty, plane)
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
    ScreenPtr                 pScreen = pGC->pScreen;
    VNCSCREENPTR(pScreen);

    GC_SETUP(pDst, pGC);

    /*
     * check destination/source for overlap.
     */
    if (GC_CHECK((WindowPtr) pDst) &&
        (ORG_OVERLAP(&pScreenPriv->saved,pDst->x,pDst->y,dstx,dsty,w,h) ||
         ((pDst == pSrc) &&
          ORG_OVERLAP(&pScreenPriv->saved,pSrc->x,pSrc->y,srcx,srcy,w,h))))
    {
        rfbSpriteRemoveCursor (pDst->pScreen);
    }

    GC_OP_PROLOGUE (pGC);

    rgn = (*pGC->ops->CopyPlane) (pSrc, pDst, pGC, srcx, srcy, w, h,
                                  dstx, dsty, plane);

    GC_OP_EPILOGUE (pGC);

    return rgn;
}

static void
rfbSpritePolyPoint (pDrawable, pGC, mode, npt, pptInit)
    DrawablePtr pDrawable;
    GCPtr        pGC;
    int                mode;                /* Origin or Previous */
    int                npt;
    xPoint         *pptInit;
{
    xPoint        t;
    int                n;
    BoxRec        cursor;
    register xPoint *pts;
    ScreenPtr                 pScreen = pDrawable->pScreen;
    VNCSCREENPTR(pScreen);

    GC_SETUP (pDrawable, pGC);

    if (npt && GC_CHECK((WindowPtr) pDrawable))
    {
        cursor.x1 = pScreenPriv->saved.x1 - pDrawable->x;
        cursor.y1 = pScreenPriv->saved.y1 - pDrawable->y;
        cursor.x2 = pScreenPriv->saved.x2 - pDrawable->x;
        cursor.y2 = pScreenPriv->saved.y2 - pDrawable->y;

        if (mode == CoordModePrevious)
        {
            t.x = 0;
            t.y = 0;
            for (pts = pptInit, n = npt; n--; pts++)
            {
                t.x += pts->x;
                t.y += pts->y;
                if (cursor.x1 <= t.x && t.x <= cursor.x2 &&
                    cursor.y1 <= t.y && t.y <= cursor.y2)
                {
                    rfbSpriteRemoveCursor (pDrawable->pScreen);
                    break;
                }
            }
        }
        else
        {
            for (pts = pptInit, n = npt; n--; pts++)
            {
                if (cursor.x1 <= pts->x && pts->x <= cursor.x2 &&
                    cursor.y1 <= pts->y && pts->y <= cursor.y2)
                {
                    rfbSpriteRemoveCursor (pDrawable->pScreen);
                    break;
                }
            }
        }
    }

    GC_OP_PROLOGUE (pGC);

    (*pGC->ops->PolyPoint) (pDrawable, pGC, mode, npt, pptInit);

    GC_OP_EPILOGUE (pGC);
}

static void
rfbSpritePolylines (pDrawable, pGC, mode, npt, pptInit)
    DrawablePtr          pDrawable;
    GCPtr             pGC;
    int                      mode;
    int                      npt;
    DDXPointPtr          pptInit;
{
    BoxPtr  cursor;
    register DDXPointPtr pts;
    int            n;
    int            x, y, x1, y1, x2, y2;
    int            lw;
    int            extra;
    ScreenPtr                 pScreen = pDrawable->pScreen;
    VNCSCREENPTR(pScreen);

    GC_SETUP (pDrawable, pGC);

    if (npt && GC_CHECK((WindowPtr) pDrawable))
    {
        cursor = &pScreenPriv->saved;
        lw = pGC->lineWidth;
        x = pptInit->x + pDrawable->x;
        y = pptInit->y + pDrawable->y;

        if (npt == 1)
        {
            extra = lw >> 1;
            if (LINE_OVERLAP(cursor, x, y, x, y, extra))
                rfbSpriteRemoveCursor (pDrawable->pScreen);
        }
        else
        {
            extra = lw >> 1;
            /*
             * mitered joins can project quite a way from
             * the line end; the 11 degree miter limit limits
             * this extension to 10.43 * lw / 2, rounded up
             * and converted to int yields 6 * lw
             */
            if (pGC->joinStyle == JoinMiter)
                extra = 6 * lw;
            else if (pGC->capStyle == CapProjecting)
                extra = lw;
            for (pts = pptInit + 1, n = npt - 1; n--; pts++)
            {
                x1 = x;
                y1 = y;
                if (mode == CoordModeOrigin)
                {
                    x2 = pDrawable->x + pts->x;
                    y2 = pDrawable->y + pts->y;
                }
                else
                {
                    x2 = x + pts->x;
                    y2 = y + pts->y;
                }
                x = x2;
                y = y2;
                LINE_SORT(x1, y1, x2, y2);
                if (LINE_OVERLAP(cursor, x1, y1, x2, y2, extra))
                {
                    rfbSpriteRemoveCursor (pDrawable->pScreen);
                    break;
                }
            }
        }
    }
    GC_OP_PROLOGUE (pGC);

    (*pGC->ops->Polylines) (pDrawable, pGC, mode, npt, pptInit);

    GC_OP_EPILOGUE (pGC);
}

static void
rfbSpritePolySegment(pDrawable, pGC, nseg, pSegs)
    DrawablePtr pDrawable;
    GCPtr         pGC;
    int                nseg;
    xSegment        *pSegs;
{
    int            n;
    register xSegment *segs;
    BoxPtr  cursor;
    int            x1, y1, x2, y2;
    int            extra;
    ScreenPtr                 pScreen = pDrawable->pScreen;
    VNCSCREENPTR(pScreen);

    GC_SETUP(pDrawable, pGC);

    if (nseg && GC_CHECK((WindowPtr) pDrawable))
    {
        cursor = &pScreenPriv->saved;
        extra = pGC->lineWidth >> 1;
        if (pGC->capStyle == CapProjecting)
            extra = pGC->lineWidth;
        for (segs = pSegs, n = nseg; n--; segs++)
        {
            x1 = segs->x1 + pDrawable->x;
            y1 = segs->y1 + pDrawable->y;
            x2 = segs->x2 + pDrawable->x;
            y2 = segs->y2 + pDrawable->y;
            LINE_SORT(x1, y1, x2, y2);
            if (LINE_OVERLAP(cursor, x1, y1, x2, y2, extra))
            {
                rfbSpriteRemoveCursor (pDrawable->pScreen);
                break;
            }
        }
    }

    GC_OP_PROLOGUE (pGC);

    (*pGC->ops->PolySegment) (pDrawable, pGC, nseg, pSegs);

    GC_OP_EPILOGUE (pGC);
}

static void
rfbSpritePolyRectangle(pDrawable, pGC, nrects, pRects)
    DrawablePtr        pDrawable;
    GCPtr        pGC;
    int                nrects;
    xRectangle        *pRects;
{
    register xRectangle *rects;
    BoxPtr  cursor;
    int            lw;
    int            n;
    int     x1, y1, x2, y2;
    ScreenPtr                 pScreen = pDrawable->pScreen;
    VNCSCREENPTR(pScreen);
    
    GC_SETUP (pDrawable, pGC);

    if (GC_CHECK((WindowPtr) pDrawable))
    {
        lw = pGC->lineWidth >> 1;
        cursor = &pScreenPriv->saved;
        for (rects = pRects, n = nrects; n--; rects++)
        {
            x1 = rects->x + pDrawable->x;
            y1 = rects->y + pDrawable->y;
            x2 = x1 + (int)rects->width;
            y2 = y1 + (int)rects->height;
            if (LINE_OVERLAP(cursor, x1, y1, x2, y1, lw) ||
                LINE_OVERLAP(cursor, x2, y1, x2, y2, lw) ||
                LINE_OVERLAP(cursor, x1, y2, x2, y2, lw) ||
                LINE_OVERLAP(cursor, x1, y1, x1, y2, lw))
            {
                rfbSpriteRemoveCursor (pDrawable->pScreen);
                break;
            }
        }
    }

    GC_OP_PROLOGUE (pGC);

    (*pGC->ops->PolyRectangle) (pDrawable, pGC, nrects, pRects);

    GC_OP_EPILOGUE (pGC);
}

static void
rfbSpritePolyArc(pDrawable, pGC, narcs, parcs)
    DrawablePtr        pDrawable;
    register GCPtr        pGC;
    int                narcs;
    xArc        *parcs;
{
    BoxPtr  cursor;
    int            lw;
    int            n;
    register xArc *arcs;
    ScreenPtr                 pScreen = pDrawable->pScreen;
    VNCSCREENPTR(pScreen);
    
    GC_SETUP (pDrawable, pGC);

    if (GC_CHECK((WindowPtr) pDrawable))
    {
        lw = pGC->lineWidth >> 1;
        cursor = &pScreenPriv->saved;
        for (arcs = parcs, n = narcs; n--; arcs++)
        {
            if (ORG_OVERLAP (cursor, pDrawable->x, pDrawable->y,
                             arcs->x - lw, arcs->y - lw,
                             (int) arcs->width + pGC->lineWidth,
                              (int) arcs->height + pGC->lineWidth))
            {
                rfbSpriteRemoveCursor (pDrawable->pScreen);
                break;
            }
        }
    }

    GC_OP_PROLOGUE (pGC);

    (*pGC->ops->PolyArc) (pDrawable, pGC, narcs, parcs);

    GC_OP_EPILOGUE (pGC);
}

static void
rfbSpriteFillPolygon(pDrawable, pGC, shape, mode, count, pPts)
    register DrawablePtr pDrawable;
    register GCPtr        pGC;
    int                        shape, mode;
    int                        count;
    DDXPointPtr                pPts;
{
    int x, y, minx, miny, maxx, maxy;
    register DDXPointPtr pts;
    int n;
    ScreenPtr                 pScreen = pDrawable->pScreen;
    VNCSCREENPTR(pScreen);

    GC_SETUP (pDrawable, pGC);

    if (count && GC_CHECK((WindowPtr) pDrawable))
    {
        x = pDrawable->x;
        y = pDrawable->y;
        pts = pPts;
        minx = maxx = pts->x;
        miny = maxy = pts->y;
        pts++;
        n = count - 1;

        if (mode == CoordModeOrigin)
        {
            for (; n--; pts++)
            {
                if (pts->x < minx)
                    minx = pts->x;
                else if (pts->x > maxx)
                    maxx = pts->x;
                if (pts->y < miny)
                    miny = pts->y;
                else if (pts->y > maxy)
                    maxy = pts->y;
            }
            minx += x;
            miny += y;
            maxx += x;
            maxy += y;
        }
        else
        {
            x += minx;
            y += miny;
            minx = maxx = x;
            miny = maxy = y;
            for (; n--; pts++)
            {
                x += pts->x;
                y += pts->y;
                if (x < minx)
                    minx = x;
                else if (x > maxx)
                    maxx = x;
                if (y < miny)
                    miny = y;
                else if (y > maxy)
                    maxy = y;
            }
        }
        if (BOX_OVERLAP(&pScreenPriv->saved,minx,miny,maxx,maxy))
            rfbSpriteRemoveCursor (pDrawable->pScreen);
    }

    GC_OP_PROLOGUE (pGC);

    (*pGC->ops->FillPolygon) (pDrawable, pGC, shape, mode, count, pPts);

    GC_OP_EPILOGUE (pGC);
}

static void
rfbSpritePolyFillRect(pDrawable, pGC, nrectFill, prectInit)
    DrawablePtr pDrawable;
    GCPtr        pGC;
    int                nrectFill;         /* number of rectangles to fill */
    xRectangle        *prectInit;          /* Pointer to first rectangle to fill */
{
    ScreenPtr                 pScreen = pDrawable->pScreen;
    VNCSCREENPTR(pScreen);

    GC_SETUP(pDrawable, pGC);

    if (GC_CHECK((WindowPtr) pDrawable))
    {
        register int            nRect;
        register xRectangle *pRect;
        register int            xorg, yorg;

        xorg = pDrawable->x;
        yorg = pDrawable->y;

        for (nRect = nrectFill, pRect = prectInit; nRect--; pRect++) {
            if (ORGRECT_OVERLAP(&pScreenPriv->saved,xorg,yorg,pRect)){
                rfbSpriteRemoveCursor(pDrawable->pScreen);
                break;
            }
        }
    }

    GC_OP_PROLOGUE (pGC);

    (*pGC->ops->PolyFillRect) (pDrawable, pGC, nrectFill, prectInit);

    GC_OP_EPILOGUE (pGC);
}

static void
rfbSpritePolyFillArc(pDrawable, pGC, narcs, parcs)
    DrawablePtr        pDrawable;
    GCPtr        pGC;
    int                narcs;
    xArc        *parcs;
{
    ScreenPtr                 pScreen = pDrawable->pScreen;
    VNCSCREENPTR(pScreen);

    GC_SETUP(pDrawable, pGC);

    if (GC_CHECK((WindowPtr) pDrawable))
    {
        register int        n;
        BoxPtr                cursor;
        register xArc *arcs;

        cursor = &pScreenPriv->saved;

        for (arcs = parcs, n = narcs; n--; arcs++)
        {
            if (ORG_OVERLAP(cursor, pDrawable->x, pDrawable->y,
                            arcs->x, arcs->y,
                             (int) arcs->width, (int) arcs->height))
            {
                rfbSpriteRemoveCursor (pDrawable->pScreen);
                break;
            }
        }
    }

    GC_OP_PROLOGUE (pGC);

    (*pGC->ops->PolyFillArc) (pDrawable, pGC, narcs, parcs);

    GC_OP_EPILOGUE (pGC);
}

/*
 * general Poly/Image text function.  Extract glyph information,
 * compute bounding box and remove cursor if it is overlapped.
 */

static Bool
rfbSpriteTextOverlap (pDraw, font, x, y, n, charinfo, imageblt, w, cursorBox)
    DrawablePtr   pDraw;
    FontPtr          font;
    int                  x, y;
    unsigned int  n;
    CharInfoPtr   *charinfo;
    Bool          imageblt;
    unsigned int  w;
    BoxPtr          cursorBox;
{
    ExtentInfoRec extents;

    x += pDraw->x;
    y += pDraw->y;

    if (FONTMINBOUNDS(font,characterWidth) >= 0)
    {
        /* compute an approximate (but covering) bounding box */
        if (!imageblt || (charinfo[0]->metrics.leftSideBearing < 0))
            extents.overallLeft = charinfo[0]->metrics.leftSideBearing;
        else
            extents.overallLeft = 0;
        if (w)
            extents.overallRight = w - charinfo[n-1]->metrics.characterWidth;
        else
            extents.overallRight = FONTMAXBOUNDS(font,characterWidth)
                                    * (n - 1);
        if (imageblt && (charinfo[n-1]->metrics.characterWidth >
                         charinfo[n-1]->metrics.rightSideBearing))
            extents.overallRight += charinfo[n-1]->metrics.characterWidth;
        else
            extents.overallRight += charinfo[n-1]->metrics.rightSideBearing;
        if (imageblt && FONTASCENT(font) > FONTMAXBOUNDS(font,ascent))
            extents.overallAscent = FONTASCENT(font);
        else
            extents.overallAscent = FONTMAXBOUNDS(font, ascent);
        if (imageblt && FONTDESCENT(font) > FONTMAXBOUNDS(font,descent))
            extents.overallDescent = FONTDESCENT(font);
        else
            extents.overallDescent = FONTMAXBOUNDS(font,descent);
        if (!BOX_OVERLAP(cursorBox,
                         x + extents.overallLeft,
                         y - extents.overallAscent,
                         x + extents.overallRight,
                         y + extents.overallDescent))
            return FALSE;
        else if (imageblt && w)
            return TRUE;
        /* if it does overlap, fall through and compute exactly, because
         * taking down the cursor is expensive enough to make this worth it
         */
    }
    QueryGlyphExtents(font, charinfo, n, &extents);
    if (imageblt)
    {
        if (extents.overallWidth > extents.overallRight)
            extents.overallRight = extents.overallWidth;
        if (extents.overallWidth < extents.overallLeft)
            extents.overallLeft = extents.overallWidth;
        if (extents.overallLeft > 0)
            extents.overallLeft = 0;
        if (extents.fontAscent > extents.overallAscent)
            extents.overallAscent = extents.fontAscent;
        if (extents.fontDescent > extents.overallDescent)
            extents.overallDescent = extents.fontDescent;
    }
    return (BOX_OVERLAP(cursorBox,
                        x + extents.overallLeft,
                        y - extents.overallAscent,
                        x + extents.overallRight,
                        y + extents.overallDescent));
}

/*
 * values for textType:
 */
#define TT_POLY8   0
#define TT_IMAGE8  1
#define TT_POLY16  2
#define TT_IMAGE16 3

static int 
rfbSpriteText (pDraw, pGC, x, y, count, chars, fontEncoding, textType, cursorBox)
    DrawablePtr            pDraw;
    GCPtr            pGC;
    int                    x,
                    y;
    unsigned long    count;
    char            *chars;
    FontEncoding    fontEncoding;
    Bool            textType;
    BoxPtr            cursorBox;
{
    CharInfoPtr *charinfo;
    register CharInfoPtr *info;
    unsigned long i;
    unsigned int  n;
    int                  w;
    void             (*drawFunc)() = NULL;

    Bool imageblt;

    imageblt = (textType == TT_IMAGE8) || (textType == TT_IMAGE16);

    charinfo = (CharInfoPtr *) ALLOCATE_LOCAL(count * sizeof(CharInfoPtr));
    if (!charinfo)
        return x;

    GetGlyphs(pGC->font, count, (unsigned char *)chars,
              fontEncoding, &i, charinfo);
    n = (unsigned int)i;
    w = 0;
    if (!imageblt)
        for (info = charinfo; i--; info++)
            w += (*info)->metrics.characterWidth;

    if (n != 0) {
        if (rfbSpriteTextOverlap(pDraw, pGC->font, x, y, n, charinfo, imageblt, w, cursorBox))
            rfbSpriteRemoveCursor(pDraw->pScreen);

#ifdef AVOID_GLYPHBLT
        /*
         * On displays like Apollos, which do not optimize the GlyphBlt functions because they
         * convert fonts to their internal form in RealizeFont and optimize text directly, we
         * want to invoke the text functions here, not the GlyphBlt functions.
         */
        switch (textType)
        {
        case TT_POLY8:
            drawFunc = (void (*)())pGC->ops->PolyText8;
            break;
        case TT_IMAGE8:
            drawFunc = pGC->ops->ImageText8;
            break;
        case TT_POLY16:
            drawFunc = (void (*)())pGC->ops->PolyText16;
            break;
        case TT_IMAGE16:
            drawFunc = pGC->ops->ImageText16;
            break;
        }
        (*drawFunc) (pDraw, pGC, x, y, (int) count, chars);
#else /* don't AVOID_GLYPHBLT */
        /*
         * On the other hand, if the device does use GlyphBlt ultimately to do text, we
         * don't want to slow it down by invoking the text functions and having them call
         * GetGlyphs all over again, so we go directly to the GlyphBlt functions here.
         */
        drawFunc = imageblt ? pGC->ops->ImageGlyphBlt : pGC->ops->PolyGlyphBlt;
        (*drawFunc) (pDraw, pGC, x, y, n, charinfo, FONTGLYPHS(pGC->font));
#endif /* AVOID_GLYPHBLT */
    }
    DEALLOCATE_LOCAL(charinfo);
    return x + w;
}

static int
rfbSpritePolyText8(pDrawable, pGC, x, y, count, chars)
    DrawablePtr pDrawable;
    GCPtr        pGC;
    int                x, y;
    int         count;
    char        *chars;
{
    int        ret;
    ScreenPtr                 pScreen = pDrawable->pScreen;
    VNCSCREENPTR(pScreen);

    GC_SETUP (pDrawable, pGC);

    GC_OP_PROLOGUE (pGC);

    if (GC_CHECK((WindowPtr) pDrawable))
        ret = rfbSpriteText (pDrawable, pGC, x, y, (unsigned long)count, chars,
                            Linear8Bit, TT_POLY8, &pScreenPriv->saved);
    else
        ret = (*pGC->ops->PolyText8) (pDrawable, pGC, x, y, count, chars);

    GC_OP_EPILOGUE (pGC);
    return ret;
}

static int
rfbSpritePolyText16(pDrawable, pGC, x, y, count, chars)
    DrawablePtr pDrawable;
    GCPtr        pGC;
    int                x, y;
    int                count;
    unsigned short *chars;
{
    int        ret;
    ScreenPtr                 pScreen = pDrawable->pScreen;
    VNCSCREENPTR(pScreen);

    GC_SETUP(pDrawable, pGC);

    GC_OP_PROLOGUE (pGC);

    if (GC_CHECK((WindowPtr) pDrawable))
        ret = rfbSpriteText (pDrawable, pGC, x, y, (unsigned long)count,
                            (char *)chars,
                            FONTLASTROW(pGC->font) == 0 ?
                            Linear16Bit : TwoD16Bit, TT_POLY16, &pScreenPriv->saved);
    else
        ret = (*pGC->ops->PolyText16) (pDrawable, pGC, x, y, count, chars);

    GC_OP_EPILOGUE (pGC);
    return ret;
}

static void
rfbSpriteImageText8(pDrawable, pGC, x, y, count, chars)
    DrawablePtr pDrawable;
    GCPtr        pGC;
    int                x, y;
    int                count;
    char        *chars;
{
    ScreenPtr                 pScreen = pDrawable->pScreen;
    VNCSCREENPTR(pScreen);

    GC_SETUP(pDrawable, pGC);

    GC_OP_PROLOGUE (pGC);

    if (GC_CHECK((WindowPtr) pDrawable))
        (void) rfbSpriteText (pDrawable, pGC, x, y, (unsigned long)count,
                             chars, Linear8Bit, TT_IMAGE8, &pScreenPriv->saved);
    else
        (*pGC->ops->ImageText8) (pDrawable, pGC, x, y, count, chars);

    GC_OP_EPILOGUE (pGC);
}

static void
rfbSpriteImageText16(pDrawable, pGC, x, y, count, chars)
    DrawablePtr pDrawable;
    GCPtr        pGC;
    int                x, y;
    int                count;
    unsigned short *chars;
{
    ScreenPtr                 pScreen = pDrawable->pScreen;
    VNCSCREENPTR(pScreen);

    GC_SETUP(pDrawable, pGC);

    GC_OP_PROLOGUE (pGC);

    if (GC_CHECK((WindowPtr) pDrawable))
        (void) rfbSpriteText (pDrawable, pGC, x, y, (unsigned long)count,
                             (char *)chars,
                            FONTLASTROW(pGC->font) == 0 ?
                            Linear16Bit : TwoD16Bit, TT_IMAGE16, &pScreenPriv->saved);
    else
        (*pGC->ops->ImageText16) (pDrawable, pGC, x, y, count, chars);

    GC_OP_EPILOGUE (pGC);
}

static void
rfbSpriteImageGlyphBlt(pDrawable, pGC, x, y, nglyph, ppci, pglyphBase)
    DrawablePtr pDrawable;
    GCPtr         pGC;
    int         x, y;
    unsigned int nglyph;
    CharInfoPtr *ppci;                /* array of character info */
    pointer         pglyphBase;        /* start of array of glyphs */
{
    ScreenPtr                 pScreen = pDrawable->pScreen;
    VNCSCREENPTR(pScreen);

    GC_SETUP(pDrawable, pGC);

    GC_OP_PROLOGUE (pGC);

    if (GC_CHECK((WindowPtr) pDrawable) &&
        rfbSpriteTextOverlap (pDrawable, pGC->font, x, y, nglyph, ppci, TRUE, 0, &pScreenPriv->saved))
    {
        rfbSpriteRemoveCursor(pDrawable->pScreen);
    }
    (*pGC->ops->ImageGlyphBlt) (pDrawable, pGC, x, y, nglyph, ppci, pglyphBase);

    GC_OP_EPILOGUE (pGC);
}

static void
rfbSpritePolyGlyphBlt(pDrawable, pGC, x, y, nglyph, ppci, pglyphBase)
    DrawablePtr pDrawable;
    GCPtr        pGC;
    int         x, y;
    unsigned int nglyph;
    CharInfoPtr *ppci;                /* array of character info */
    pointer        pglyphBase;        /* start of array of glyphs */
{
    ScreenPtr                 pScreen = pDrawable->pScreen;
    VNCSCREENPTR(pScreen);

    GC_SETUP (pDrawable, pGC);

    GC_OP_PROLOGUE (pGC);

    if (GC_CHECK((WindowPtr) pDrawable) &&
        rfbSpriteTextOverlap (pDrawable, pGC->font, x, y, nglyph, ppci, FALSE, 0, &pScreenPriv->saved))
    {
        rfbSpriteRemoveCursor(pDrawable->pScreen);
    }
    (*pGC->ops->PolyGlyphBlt) (pDrawable, pGC, x, y, nglyph, ppci, pglyphBase);

    GC_OP_EPILOGUE (pGC);
}

static void
rfbSpritePushPixels(pGC, pBitMap, pDrawable, w, h, x, y)
    GCPtr        pGC;
    PixmapPtr        pBitMap;
    DrawablePtr pDrawable;
    int                w, h, x, y;
{
    VNCSCREENPTR(pScreen);
    GC_SETUP(pDrawable, pGC);

    if (GC_CHECK((WindowPtr) pDrawable) &&
        ORG_OVERLAP(&pScreenPriv->saved,pDrawable->x,pDrawable->y,x,y,w,h))
    {
        rfbSpriteRemoveCursor (pDrawable->pScreen);
    }

    GC_OP_PROLOGUE (pGC);

    (*pGC->ops->PushPixels) (pGC, pBitMap, pDrawable, w, h, x, y);

    GC_OP_EPILOGUE (pGC);
}

#ifdef NEED_LINEHELPER
/*
 * I don't expect this routine will ever be called, as the GC
 * will have been unwrapped for the line drawing
 */

static void
rfbSpriteLineHelper()
{
    FatalError("rfbSpriteLineHelper called\n");
}
#endif

#ifdef RENDER

# define mod(a,b)        ((b) == 1 ? 0 : (a) >= 0 ? (a) % (b) : (b) - (-a) % (b))

static void
rfbSpritePictureOverlap (PicturePtr  pPict,
                        INT16            x,
                        INT16            y,
                        CARD16            w,
                        CARD16            h)
{
    VNCSCREENPTR(pScreen);

    if (pPict->pDrawable->type == DRAWABLE_WINDOW)
    {
        WindowPtr                pWin = (WindowPtr) (pPict->pDrawable);
        rfbSpriteScreenPtr        pScreenPriv = (rfbSpriteScreenPtr)
            pPict->pDrawable->pScreen->devPrivates[rfbSpriteScreenIndex].ptr;
        if (GC_CHECK(pWin))
        {
            if (pPict->repeat)
            {
                x = mod(x,pWin->drawable.width);
                y = mod(y,pWin->drawable.height);
            }
            if (ORG_OVERLAP (&pScreenPriv->saved, pWin->drawable.x, pWin->drawable.y,
                             x, y, w, h))
                rfbSpriteRemoveCursor (pWin->drawable.pScreen);
        }
    }
}

#define PICTURE_PROLOGUE(ps, pScreenPriv, field) \
    ps->field = pScreenPriv->field

#define PICTURE_EPILOGUE(ps, field, wrap) \
    ps->field = wrap

static void
rfbSpriteComposite(CARD8        op,
                  PicturePtr pSrc,
                  PicturePtr pMask,
                  PicturePtr pDst,
                  INT16        xSrc,
                  INT16        ySrc,
                  INT16        xMask,
                  INT16        yMask,
                  INT16        xDst,
                  INT16        yDst,
                  CARD16        width,
                  CARD16        height)
{
    ScreenPtr                pScreen = pDst->pDrawable->pScreen;
    PictureScreenPtr        ps = GetPictureScreen(pScreen);
    rfbSpriteScreenPtr        pScreenPriv;

    pScreenPriv = (rfbSpriteScreenPtr) pScreen->devPrivates[rfbSpriteScreenIndex].ptr;
    PICTURE_PROLOGUE(ps, pScreenPriv, Composite);
    rfbSpritePictureOverlap (pSrc, xSrc, ySrc, width, height);
    if (pMask)
        rfbSpritePictureOverlap (pMask, xMask, yMask, width, height);
    rfbSpritePictureOverlap (pDst, xDst, yDst, width, height);

    (*ps->Composite) (op,
                       pSrc,
                       pMask,
                       pDst,
                       xSrc,
                       ySrc,
                       xMask,
                       yMask,
                       xDst,
                       yDst,
                       width,
                       height);
    
    PICTURE_EPILOGUE(ps, Composite, rfbSpriteComposite);
}

static void
rfbSpriteGlyphs(CARD8                op,
               PicturePtr        pSrc,
               PicturePtr        pDst,
               PictFormatPtr        maskFormat,
               INT16                xSrc,
               INT16                ySrc,
               int                nlist,
               GlyphListPtr        list,
               GlyphPtr                *glyphs)
{
    ScreenPtr                pScreen = pDst->pDrawable->pScreen;
    VNCSCREENPTR(pScreen);
    PictureScreenPtr        ps = GetPictureScreen(pScreen);
    rfbSpriteScreenPtr        pScreenPriv;

    pScreenPriv = (rfbSpriteScreenPtr) pScreen->devPrivates[rfbSpriteScreenIndex].ptr;
    PICTURE_PROLOGUE(ps, pScreenPriv, Glyphs);
    if (pSrc->pDrawable->type == DRAWABLE_WINDOW)
    {
        WindowPtr   pSrcWin = (WindowPtr) (pSrc->pDrawable);

        if (GC_CHECK(pSrcWin))
            rfbSpriteRemoveCursor (pScreen);
    }
    if (pDst->pDrawable->type == DRAWABLE_WINDOW)
    {
        WindowPtr   pDstWin = (WindowPtr) (pDst->pDrawable);

        if (GC_CHECK(pDstWin))
        {
            BoxRec  extents;

            miGlyphExtents (nlist, list, glyphs, &extents);
            if (BOX_OVERLAP(&pScreenPriv->saved,
                            extents.x1 + pDstWin->drawable.x,
                            extents.y1 + pDstWin->drawable.y,
                            extents.x2 + pDstWin->drawable.x,
                            extents.y2 + pDstWin->drawable.y))
            {
                rfbSpriteRemoveCursor (pScreen);
            }
        }
    }
    
    (*ps->Glyphs) (op, pSrc, pDst, maskFormat, xSrc, ySrc, nlist, list, glyphs);
    
    PICTURE_EPILOGUE (ps, Glyphs, rfbSpriteGlyphs);
}
#endif

/*
 * miPointer interface routines
 */

#define SPRITE_PAD 8

static Bool
rfbSpriteRealizeCursor (pScreen, pCursor)
    ScreenPtr        pScreen;
    CursorPtr        pCursor;
{
    rfbSpriteScreenPtr        pScreenPriv;

    pScreenPriv = (rfbSpriteScreenPtr) pScreen->devPrivates[rfbSpriteScreenIndex].ptr;
    if (pCursor == pScreenPriv->pCursor)
        pScreenPriv->checkPixels = TRUE;
    return (*pScreenPriv->funcs->RealizeCursor) (pScreen, pCursor);
}

static Bool
rfbSpriteUnrealizeCursor (pScreen, pCursor)
    ScreenPtr        pScreen;
    CursorPtr        pCursor;
{
    rfbSpriteScreenPtr        pScreenPriv;

    pScreenPriv = (rfbSpriteScreenPtr) pScreen->devPrivates[rfbSpriteScreenIndex].ptr;
    return (*pScreenPriv->funcs->UnrealizeCursor) (pScreen, pCursor);
}

static void
rfbSpriteSetCursor (pScreen, pCursor, x, y)
    ScreenPtr        pScreen;
    CursorPtr        pCursor;
{
    rfbSpriteScreenPtr        pScreenPriv;
    rfbClientPtr cl, nextCl;
    VNCSCREENPTR(pScreen);

    pScreenPriv
        = (rfbSpriteScreenPtr) pScreen->devPrivates[rfbSpriteScreenIndex].ptr;
    if (!pCursor)
    {
            pScreenPriv->shouldBeUp = FALSE;
            if (pVNC->cursorIsDrawn)
            rfbSpriteRemoveCursor (pScreen);
        pScreenPriv->pCursor = 0;
        return;
    }
    pScreenPriv->shouldBeUp = TRUE;
    if (pScreenPriv->x == x &&
        pScreenPriv->y == y &&
        pScreenPriv->pCursor == pCursor &&
        !pScreenPriv->checkPixels)
    {
        return;
    }
    pScreenPriv->x = x;
    pScreenPriv->y = y;
    pScreenPriv->pCacheWin = NullWindow;
    if (pScreenPriv->checkPixels || pScreenPriv->pCursor != pCursor)
    {
        pScreenPriv->pCursor = pCursor;
        rfbSpriteFindColors (pScreen);
    }
    if (pVNC->cursorIsDrawn) {
        int        sx, sy;
        /*
         * check to see if the old saved region
         * encloses the new sprite, in which case we use
         * the flicker-free MoveCursor primitive.
         */
        sx = pScreenPriv->x - (int)pCursor->bits->xhot;
        sy = pScreenPriv->y - (int)pCursor->bits->yhot;
        if (sx + (int) pCursor->bits->width >= pScreenPriv->saved.x1 &&
            sx < pScreenPriv->saved.x2 &&
            sy + (int) pCursor->bits->height >= pScreenPriv->saved.y1 &&
            sy < pScreenPriv->saved.y2 &&
            (int) pCursor->bits->width + (2 * SPRITE_PAD) ==
                pScreenPriv->saved.x2 - pScreenPriv->saved.x1 &&
            (int) pCursor->bits->height + (2 * SPRITE_PAD) ==
                pScreenPriv->saved.y2 - pScreenPriv->saved.y1
            )
        {
            pVNC->cursorIsDrawn = FALSE;
            if (!(sx >= pScreenPriv->saved.x1 &&
                        sx + (int)pCursor->bits->width < pScreenPriv->saved.x2 &&
                        sy >= pScreenPriv->saved.y1 &&
                        sy + (int)pCursor->bits->height < pScreenPriv->saved.y2))
            {
                int oldx1, oldy1, dx, dy;

                oldx1 = pScreenPriv->saved.x1;
                oldy1 = pScreenPriv->saved.y1;
                dx = oldx1 - (sx - SPRITE_PAD);
                dy = oldy1 - (sy - SPRITE_PAD);
                pScreenPriv->saved.x1 -= dx;
                pScreenPriv->saved.y1 -= dy;
                pScreenPriv->saved.x2 -= dx;
                pScreenPriv->saved.y2 -= dy;
                (void) (*pScreenPriv->funcs->ChangeSave) (pScreen,
                                pScreenPriv->saved.x1,
                                 pScreenPriv->saved.y1,
                                pScreenPriv->saved.x2 - pScreenPriv->saved.x1,
                                pScreenPriv->saved.y2 - pScreenPriv->saved.y1,
                                dx, dy);
            }
            (void) (*pScreenPriv->funcs->MoveCursor) (pScreen, pCursor,
                                  pScreenPriv->saved.x1,
                                   pScreenPriv->saved.y1,
                                  pScreenPriv->saved.x2 - pScreenPriv->saved.x1,
                                  pScreenPriv->saved.y2 - pScreenPriv->saved.y1,
                                  sx - pScreenPriv->saved.x1,
                                  sy - pScreenPriv->saved.y1,
                                  pScreenPriv->colors[SOURCE_COLOR].pixel,
                                  pScreenPriv->colors[MASK_COLOR].pixel);
            pVNC->cursorIsDrawn = TRUE;
        }
        else
        {
            rfbSpriteRemoveCursor (pScreen);
        }
    }
#if 0
    if (!pVNC->cursorIsDrawn && pScreenPriv->pCursor)
        rfbSpriteRestoreCursor (pScreen);
#endif
    if (pVNC->cursorIsDrawn)
        rfbSpriteRemoveCursor (pScreen);

    for (cl = rfbClientHead; cl; cl = nextCl) {
        nextCl = cl->next;
        if (cl->enableCursorPosUpdates) {
            if (x == cl->cursorX && y == cl->cursorY) {
                cl->cursorWasMoved = FALSE;
                continue;
            }
            cl->cursorWasMoved = TRUE;
        }
        if (REGION_NOTEMPTY(pScreen,&cl->requestedRegion)) {
            /* cursorIsDrawn is guaranteed to be FALSE here, so we definitely
               want to send a screen update to the client, even if that's only
               putting up the cursor */
            rfbSendFramebufferUpdate(pScreen, cl);
        }
    }
}

static void
rfbSpriteMoveCursor (pScreen, x, y)
    ScreenPtr        pScreen;
    int                x, y;
{
    rfbSpriteScreenPtr        pScreenPriv;

    pScreenPriv = (rfbSpriteScreenPtr) pScreen->devPrivates[rfbSpriteScreenIndex].ptr;
    rfbSpriteSetCursor (pScreen, pScreenPriv->pCursor, x, y);
}

/*
 * undraw/draw cursor
 */

void
rfbSpriteRemoveCursor (pScreen)
    ScreenPtr        pScreen;
{
    rfbSpriteScreenPtr   pScreenPriv;
    VNCSCREENPTR(pScreen);

    if (!pVNC->cursorIsDrawn)
        return;

    pScreenPriv
        = (rfbSpriteScreenPtr) pScreen->devPrivates[rfbSpriteScreenIndex].ptr;

    pVNC->dontSendFramebufferUpdate = TRUE;
    pVNC->cursorIsDrawn = FALSE;
    pScreenPriv->pCacheWin = NullWindow;
    if (!(*pScreenPriv->funcs->RestoreUnderCursor) (pScreen,
                                         pScreenPriv->saved.x1,
                                         pScreenPriv->saved.y1,
                                         pScreenPriv->saved.x2 - pScreenPriv->saved.x1,
                                         pScreenPriv->saved.y2 - pScreenPriv->saved.y1))
    {
        pVNC->cursorIsDrawn = TRUE;
    }
    pVNC->dontSendFramebufferUpdate = FALSE;
}


void
rfbSpriteRestoreCursor (pScreen)
    ScreenPtr        pScreen;
{
    rfbSpriteScreenPtr   pScreenPriv;
    int                        x, y;
    CursorPtr                pCursor;
    VNCSCREENPTR(pScreen);

    pScreenPriv
        = (rfbSpriteScreenPtr) pScreen->devPrivates[rfbSpriteScreenIndex].ptr;
    pCursor = pScreenPriv->pCursor;

    if (pVNC->cursorIsDrawn || !pCursor)
        return;

    pVNC->dontSendFramebufferUpdate = TRUE;

    rfbSpriteComputeSaved (pScreen);

    x = pScreenPriv->x - (int)pCursor->bits->xhot;
    y = pScreenPriv->y - (int)pCursor->bits->yhot;
    if ((*pScreenPriv->funcs->SaveUnderCursor) (pScreen,
                                      pScreenPriv->saved.x1,
                                      pScreenPriv->saved.y1,
                                      pScreenPriv->saved.x2 - pScreenPriv->saved.x1,
                                      pScreenPriv->saved.y2 - pScreenPriv->saved.y1))
    {
        if (pScreenPriv->checkPixels)
            rfbSpriteFindColors (pScreen);
        if ((*pScreenPriv->funcs->PutUpCursor) (pScreen, pCursor, x, y,
                                  pScreenPriv->colors[SOURCE_COLOR].pixel,
                                  pScreenPriv->colors[MASK_COLOR].pixel))
            pVNC->cursorIsDrawn = TRUE;
    }

    pVNC->dontSendFramebufferUpdate = FALSE;
}

/*
 * compute the desired area of the screen to save
 */

static void
rfbSpriteComputeSaved (pScreen)
    ScreenPtr        pScreen;
{
    rfbSpriteScreenPtr   pScreenPriv;
    int                    x, y, w, h;
    int                    wpad, hpad;
    CursorPtr            pCursor;

    pScreenPriv = (rfbSpriteScreenPtr) pScreen->devPrivates[rfbSpriteScreenIndex].ptr;
    pCursor = pScreenPriv->pCursor;
    x = pScreenPriv->x - (int)pCursor->bits->xhot;
    y = pScreenPriv->y - (int)pCursor->bits->yhot;
    w = pCursor->bits->width;
    h = pCursor->bits->height;
    wpad = SPRITE_PAD;
    hpad = SPRITE_PAD;
    pScreenPriv->saved.x1 = x - wpad;
    pScreenPriv->saved.y1 = y - hpad;
    pScreenPriv->saved.x2 = pScreenPriv->saved.x1 + w + wpad * 2;
    pScreenPriv->saved.y2 = pScreenPriv->saved.y1 + h + hpad * 2;
}


/*
 * this function is called when the cursor shape is being changed
 */

static Bool
rfbDisplayCursor(pScreen, pCursor)
    ScreenPtr pScreen;
    CursorPtr pCursor;
{
    rfbClientPtr cl;
    rfbSpriteScreenPtr pPriv;

    for (cl = rfbClientHead; cl ; cl = cl->next) {
        if (cl->enableCursorShapeUpdates)
            cl->cursorWasChanged = TRUE;
    }

    pPriv = (rfbSpriteScreenPtr)pScreen->devPrivates[rfbSpriteScreenIndex].ptr;
    return (*pPriv->DisplayCursor)(pScreen, pCursor);
}


/*
 * obtain current cursor pointer
 */

CursorPtr
rfbSpriteGetCursorPtr (pScreen)
    ScreenPtr pScreen;
{
    rfbSpriteScreenPtr pScreenPriv;

    pScreenPriv = (rfbSpriteScreenPtr)
        pScreen->devPrivates[rfbSpriteScreenIndex].ptr;

    return pScreenPriv->pCursor;
}

/*
 * obtain current cursor position
 */

void
rfbSpriteGetCursorPos (pScreen, px, py)
    ScreenPtr pScreen;
    int *px, *py;
{
    rfbSpriteScreenPtr pScreenPriv;

    pScreenPriv = (rfbSpriteScreenPtr)
        pScreen->devPrivates[rfbSpriteScreenIndex].ptr;

    *px = pScreenPriv->x;
    *py = pScreenPriv->y;
}

