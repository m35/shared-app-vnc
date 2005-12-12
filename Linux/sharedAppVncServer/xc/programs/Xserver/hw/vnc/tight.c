/*
 * tight.c
 *
 * Routines to implement Tight Encoding
 *
 * Modified for XFree86 4.x by Alan Hourihane <alanh@fairlite.demon.co.uk>
 */

/*
 *  Copyright (C) 2000, 2001 Const Kaplinsky.  All Rights Reserved.
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

#include <stdio.h>
#include "rfb.h"
#include <jpeglib.h>


/* Note: The following constant should not be changed. */
#define TIGHT_MIN_TO_COMPRESS 12

/* The parameters below may be adjusted. */
#define MIN_SPLIT_RECT_SIZE     4096
#define MIN_SOLID_SUBRECT_SIZE  2048
#define MAX_SPLIT_TILE_SIZE       16

/* May be set to TRUE with "-lazytight" Xvnc option. */
Bool rfbTightDisableGradient = FALSE;

/* This variable is set on every rfbSendRectEncodingTight() call. */
static Bool usePixelFormat24;


/* Compression level stuff. The following array contains various
   encoder parameters for each of 10 compression levels (0..9).
   Last three parameters correspond to JPEG quality levels (0..9). */

typedef struct TIGHT_CONF_s {
    int maxRectSize, maxRectWidth;
    int monoMinRectSize, gradientMinRectSize;
    int idxZlibLevel, monoZlibLevel, rawZlibLevel, gradientZlibLevel;
    int gradientThreshold, gradientThreshold24;
    int idxMaxColorsDivisor;
    int jpegQuality, jpegThreshold, jpegThreshold24;
} TIGHT_CONF;

static TIGHT_CONF tightConf[10] = {
    {   512,   32,   6, 65536, 0, 0, 0, 0,   0,   0,   4,  5, 10000, 23000 },
    {  2048,  128,   6, 65536, 1, 1, 1, 0,   0,   0,   8, 10,  8000, 18000 },
    {  6144,  256,   8, 65536, 3, 3, 2, 0,   0,   0,  24, 15,  6500, 15000 },
    { 10240, 1024,  12, 65536, 5, 5, 3, 0,   0,   0,  32, 25,  5000, 12000 },
    { 16384, 2048,  12, 65536, 6, 6, 4, 0,   0,   0,  32, 37,  4000, 10000 },
    { 32768, 2048,  12,  4096, 7, 7, 5, 4, 150, 380,  32, 50,  3000,  8000 },
    { 65536, 2048,  16,  4096, 7, 7, 6, 4, 170, 420,  48, 60,  2000,  5000 },
    { 65536, 2048,  16,  4096, 8, 8, 7, 5, 180, 450,  64, 70,  1000,  2500 },
    { 65536, 2048,  32,  8192, 9, 9, 8, 6, 190, 475,  64, 75,   500,  1200 },
    { 65536, 2048,  32,  8192, 9, 9, 9, 6, 200, 500,  96, 80,   200,   500 }
};

/* Stuff dealing with palettes. */

typedef struct COLOR_LIST_s {
    struct COLOR_LIST_s *next;
    int idx;
    CARD32 rgb;
} COLOR_LIST;

typedef struct PALETTE_ENTRY_s {
    COLOR_LIST *listNode;
    int numPixels;
} PALETTE_ENTRY;

typedef struct PALETTE_s {
    PALETTE_ENTRY entry[256];
    COLOR_LIST *hash[256];
    COLOR_LIST list[256];
} PALETTE;

static int paletteNumColors, paletteMaxColors;
static CARD32 monoBackground, monoForeground;
static PALETTE palette;

/* Pointers to dynamically-allocated buffers. */

static int tightBeforeBufSize = 0;
static char *tightBeforeBuf = NULL;

static int tightAfterBufSize = 0;
static char *tightAfterBuf = NULL;

static int *prevRowBuf = NULL;


/* Prototypes for static functions. */

static void FindBestSolidArea (ScreenPtr pScreen, int x, int y, int w, int h,
                               CARD32 colorValue, int *w_ptr, int *h_ptr);
static void ExtendSolidArea   (ScreenPtr pScreen, int x, int y, int w, int h,
                               CARD32 colorValue,
                               int *x_ptr, int *y_ptr, int *w_ptr, int *h_ptr);
static Bool CheckSolidTile    (ScreenPtr pScreen, int x, int y, int w, int h,
                               CARD32 *colorPtr, Bool needSameColor);
static Bool CheckSolidTile8   (ScreenPtr pScreen, int x, int y, int w, int h,
                               CARD32 *colorPtr, Bool needSameColor);
static Bool CheckSolidTile16  (ScreenPtr pScreen, int x, int y, int w, int h,
                               CARD32 *colorPtr, Bool needSameColor);
static Bool CheckSolidTile32  (ScreenPtr pScreen, int x, int y, int w, int h,
                               CARD32 *colorPtr, Bool needSameColor);

static Bool SendRectSimple    (rfbClientPtr cl, int x, int y, int w, int h);
static Bool SendSubrect       (rfbClientPtr cl, int x, int y, int w, int h);
static Bool SendTightHeader   (rfbClientPtr cl, int x, int y, int w, int h);

static Bool SendSolidRect     (rfbClientPtr cl);
static Bool SendMonoRect      (rfbClientPtr cl, int w, int h);
static Bool SendIndexedRect   (rfbClientPtr cl, int w, int h);
static Bool SendFullColorRect (rfbClientPtr cl, int w, int h);
static Bool SendGradientRect  (rfbClientPtr cl, int w, int h);

static Bool CompressData(rfbClientPtr cl, int streamId, int dataLen,
                         int zlibLevel, int zlibStrategy);
static Bool SendCompressedData(rfbClientPtr cl, int compressedLen);

static void FillPalette8(int count);
static void FillPalette16(int count);
static void FillPalette32(int count);

static void PaletteReset(void);
static int PaletteInsert(CARD32 rgb, int numPixels, int bpp);

static void Pack24(ScreenPtr pScreen, char *buf, rfbPixelFormat *fmt, int count);

static void EncodeIndexedRect16(CARD8 *buf, int count);
static void EncodeIndexedRect32(CARD8 *buf, int count);

static void EncodeMonoRect8(CARD8 *buf, int w, int h);
static void EncodeMonoRect16(CARD8 *buf, int w, int h);
static void EncodeMonoRect32(CARD8 *buf, int w, int h);

static void FilterGradient24(ScreenPtr pScreen, char *buf, rfbPixelFormat *fmt, int w, int h);
static void FilterGradient16(ScreenPtr pScreen, CARD16 *buf, rfbPixelFormat *fmt, int w, int h);
static void FilterGradient32(ScreenPtr pScreen, CARD32 *buf, rfbPixelFormat *fmt, int w, int h);

static int DetectSmoothImage(rfbClientPtr cl, rfbPixelFormat *fmt, int w, int h);
static unsigned long DetectSmoothImage24(rfbClientPtr cl, rfbPixelFormat *fmt, int w, int h);
static unsigned long DetectSmoothImage16(rfbClientPtr cl, rfbPixelFormat *fmt, int w, int h);
static unsigned long DetectSmoothImage32(rfbClientPtr cl, rfbPixelFormat *fmt, int w, int h);

static Bool SendJpegRect(rfbClientPtr cl, int x, int y, int w, int h,
                         int quality);
static void PrepareRowForJpeg(ScreenPtr pScreen, CARD8 *dst, int x, int y, int count);
static void PrepareRowForJpeg24(ScreenPtr pScreen, CARD8 *dst, int x, int y, int count);
static void PrepareRowForJpeg16(ScreenPtr pScreen, CARD8 *dst, int x, int y, int count);
static void PrepareRowForJpeg32(ScreenPtr pScreen, CARD8 *dst, int x, int y, int count);

static void JpegInitDestination(j_compress_ptr cinfo);
static boolean JpegEmptyOutputBuffer(j_compress_ptr cinfo);
static void JpegTermDestination(j_compress_ptr cinfo);
static void JpegSetDstManager(j_compress_ptr cinfo);


/*
 * Tight encoding implementation.
 */

int
rfbNumCodedRectsTight(cl, x, y, w, h)
    rfbClientPtr cl;
    int x, y, w, h;
{
    int maxRectSize, maxRectWidth;
    int subrectMaxWidth, subrectMaxHeight;

    /* No matter how many rectangles we will send if LastRect markers
       are used to terminate rectangle stream. */
    if (cl->enableLastRectEncoding && w * h >= MIN_SPLIT_RECT_SIZE)
      return 0;

    maxRectSize = tightConf[cl->tightCompressLevel].maxRectSize;
    maxRectWidth = tightConf[cl->tightCompressLevel].maxRectWidth;

    if (w > maxRectWidth || w * h > maxRectSize) {
        subrectMaxWidth = (w > maxRectWidth) ? maxRectWidth : w;
        subrectMaxHeight = maxRectSize / subrectMaxWidth;
        return (((w - 1) / maxRectWidth + 1) *
                ((h - 1) / subrectMaxHeight + 1));
    } else {
        return 1;
    }
}

Bool
rfbSendRectEncodingTight(cl, x, y, w, h)
    rfbClientPtr cl;
    int x, y, w, h;
{
    VNCSCREENPTR(cl->pScreen);
    int nMaxRows;
    CARD32 colorValue;
    int dx, dy, dw, dh;
    int x_best, y_best, w_best, h_best;
    unsigned char *fbptr;

    if ( cl->format.depth == 24 && cl->format.redMax == 0xFF &&
         cl->format.greenMax == 0xFF && cl->format.blueMax == 0xFF ) {
        usePixelFormat24 = TRUE;
    } else {
        usePixelFormat24 = FALSE;
    }

    if (!cl->enableLastRectEncoding || w * h < MIN_SPLIT_RECT_SIZE)
        return SendRectSimple(cl, x, y, w, h);

    /* Make sure we can write at least one pixel into tightBeforeBuf. */

    if (tightBeforeBufSize < 4) {
        tightBeforeBufSize = 4;
        if (tightBeforeBuf == NULL)
            tightBeforeBuf = (char *)xalloc(tightBeforeBufSize);
        else
            tightBeforeBuf = (char *)xrealloc(tightBeforeBuf,
                                              tightBeforeBufSize);
    }

    /* Calculate maximum number of rows in one non-solid rectangle. */

    {
        int maxRectSize, maxRectWidth, nMaxWidth;

        maxRectSize = tightConf[cl->tightCompressLevel].maxRectSize;
        maxRectWidth = tightConf[cl->tightCompressLevel].maxRectWidth;
        nMaxWidth = (w > maxRectWidth) ? maxRectWidth : w;
        nMaxRows = maxRectSize / nMaxWidth;
    }

    /* Try to find large solid-color areas and send them separately. */

    for (dy = y; dy < y + h; dy += MAX_SPLIT_TILE_SIZE) {

        /* If a rectangle becomes too large, send its upper part now. */

        if (dy - y >= nMaxRows) {
            if (!SendRectSimple(cl, x, y, w, nMaxRows))
                return 0;
            y += nMaxRows;
            h -= nMaxRows;
        }

        dh = (dy + MAX_SPLIT_TILE_SIZE <= y + h) ?
            MAX_SPLIT_TILE_SIZE : (y + h - dy);

        for (dx = x; dx < x + w; dx += MAX_SPLIT_TILE_SIZE) {

            dw = (dx + MAX_SPLIT_TILE_SIZE <= x + w) ?
                MAX_SPLIT_TILE_SIZE : (x + w - dx);

            if (CheckSolidTile(cl->pScreen, dx, dy, dw, dh, &colorValue, FALSE)) {

                /* Get dimensions of solid-color area. */

                FindBestSolidArea(cl->pScreen, dx, dy, w - (dx - x), h - (dy - y),
				  colorValue, &w_best, &h_best);

                /* Make sure a solid rectangle is large enough
                   (or the whole rectangle is of the same color). */

                if ( w_best * h_best != w * h &&
                     w_best * h_best < MIN_SOLID_SUBRECT_SIZE )
                    continue;

                /* Try to extend solid rectangle to maximum size. */

                x_best = dx; y_best = dy;
                ExtendSolidArea(cl->pScreen, x, y, w, h, colorValue,
                                &x_best, &y_best, &w_best, &h_best);

                /* Send rectangles at top and left to solid-color area. */

                if ( y_best != y &&
                     !SendRectSimple(cl, x, y, w, y_best-y) )
                    return FALSE;
                if ( x_best != x &&
                     !rfbSendRectEncodingTight(cl, x, y_best,
                                               x_best-x, h_best) )
                    return FALSE;

                /* Send solid-color rectangle. */

                if (!SendTightHeader(cl, x_best, y_best, w_best, h_best))
                    return FALSE;

                fbptr = (pVNC->pfbMemory +
                         (pVNC->paddedWidthInBytes * y_best) +
                         (x_best * (pVNC->bitsPerPixel / 8)));

                (*cl->translateFn)(cl->pScreen, cl->translateLookupTable, 
				   &pVNC->rfbServerFormat,
                                   &cl->format, fbptr, tightBeforeBuf,
                                   pVNC->paddedWidthInBytes, 1, 1, 
				   x_best, y_best);

                if (!SendSolidRect(cl))
                    return FALSE;

                /* Send remaining rectangles (at right and bottom). */

                if ( x_best + w_best != x + w &&
                     !rfbSendRectEncodingTight(cl, x_best+w_best, y_best,
                                               w-(x_best-x)-w_best, h_best) )
                    return FALSE;
                if ( y_best + h_best != y + h &&
                     !rfbSendRectEncodingTight(cl, x, y_best+h_best,
                                               w, h-(y_best-y)-h_best) )
                    return FALSE;

                /* Return after all recursive calls are done. */

                return TRUE;
            }

        }

    }

    /* No suitable solid-color rectangles found. */

    return SendRectSimple(cl, x, y, w, h);
}

static void
FindBestSolidArea(pScreen, x, y, w, h, colorValue, w_ptr, h_ptr)
    ScreenPtr pScreen;
    int x, y, w, h;
    CARD32 colorValue;
    int *w_ptr, *h_ptr;
{
    int dx, dy, dw, dh;
    int w_prev;
    int w_best = 0, h_best = 0;

    w_prev = w;

    for (dy = y; dy < y + h; dy += MAX_SPLIT_TILE_SIZE) {

        dh = (dy + MAX_SPLIT_TILE_SIZE <= y + h) ?
            MAX_SPLIT_TILE_SIZE : (y + h - dy);
        dw = (w_prev > MAX_SPLIT_TILE_SIZE) ?
            MAX_SPLIT_TILE_SIZE : w_prev;

        if (!CheckSolidTile(pScreen, x, dy, dw, dh, &colorValue, TRUE))
            break;

        for (dx = x + dw; dx < x + w_prev;) {
            dw = (dx + MAX_SPLIT_TILE_SIZE <= x + w_prev) ?
                MAX_SPLIT_TILE_SIZE : (x + w_prev - dx);
            if (!CheckSolidTile(pScreen, dx, dy, dw, dh, &colorValue, TRUE))
                break;
	    dx += dw;
        }

        w_prev = dx - x;
        if (w_prev * (dy + dh - y) > w_best * h_best) {
            w_best = w_prev;
            h_best = dy + dh - y;
        }
    }

    *w_ptr = w_best;
    *h_ptr = h_best;
}

static void
ExtendSolidArea(pScreen, x, y, w, h, colorValue, x_ptr, y_ptr, w_ptr, h_ptr)
    ScreenPtr pScreen;
    int x, y, w, h;
    CARD32 colorValue;
    int *x_ptr, *y_ptr, *w_ptr, *h_ptr;
{
    int cx, cy;

    /* Try to extend the area upwards. */
    for ( cy = *y_ptr - 1;
          cy >= y && CheckSolidTile(pScreen, *x_ptr, cy, *w_ptr, 1, &colorValue, TRUE);
          cy-- );
    *h_ptr += *y_ptr - (cy + 1);
    *y_ptr = cy + 1;

    /* ... downwards. */
    for ( cy = *y_ptr + *h_ptr;
          cy < y + h &&
              CheckSolidTile(pScreen, *x_ptr, cy, *w_ptr, 1, &colorValue, TRUE);
          cy++ );
    *h_ptr += cy - (*y_ptr + *h_ptr);

    /* ... to the left. */
    for ( cx = *x_ptr - 1;
          cx >= x && CheckSolidTile(pScreen, cx, *y_ptr, 1, *h_ptr, &colorValue, TRUE);
          cx-- );
    *w_ptr += *x_ptr - (cx + 1);
    *x_ptr = cx + 1;

    /* ... to the right. */
    for ( cx = *x_ptr + *w_ptr;
          cx < x + w &&
              CheckSolidTile(pScreen, cx, *y_ptr, 1, *h_ptr, &colorValue, TRUE);
          cx++ );
    *w_ptr += cx - (*x_ptr + *w_ptr);
}

/*
 * Check if a rectangle is all of the same color. If needSameColor is
 * set to non-zero, then also check that its color equals to the
 * *colorPtr value. The result is 1 if the test is successfull, and in
 * that case new color will be stored in *colorPtr.
 */

static Bool
CheckSolidTile(pScreen, x, y, w, h, colorPtr, needSameColor) 
    ScreenPtr pScreen;
    int x, y, w, h;
    CARD32 *colorPtr;
    Bool needSameColor;
{
    VNCSCREENPTR(pScreen);
    switch(pVNC->rfbServerFormat.bitsPerPixel) {
    case 32:
        return CheckSolidTile32(pScreen, x, y, w, h, colorPtr, needSameColor);
    case 16:
        return CheckSolidTile16(pScreen, x, y, w, h, colorPtr, needSameColor);
    default:
        return CheckSolidTile8(pScreen, x, y, w, h, colorPtr, needSameColor);
    }
}

#define DEFINE_CHECK_SOLID_FUNCTION(bpp)                                      \
                                                                              \
static Bool                                                                   \
CheckSolidTile##bpp(pScreen, x, y, w, h, colorPtr, needSameColor)             \
    ScreenPtr pScreen;							      \
    int x, y;                                                                 \
    CARD32 *colorPtr;                                                         \
    Bool needSameColor;                                                       \
{                                                                             \
    VNCSCREENPTR(pScreen);						      \
    CARD##bpp *fbptr;                                                         \
    CARD##bpp colorValue;                                                     \
    int dx, dy;                                                               \
                                                                              \
    fbptr = (CARD##bpp *)                                                     \
        &pVNC->pfbMemory[y * pVNC->paddedWidthInBytes + x * (bpp/8)]; \
                                                                              \
    colorValue = *fbptr;                                                      \
    if (needSameColor && (CARD32)colorValue != *colorPtr)                     \
        return FALSE;                                                         \
                                                                              \
    for (dy = 0; dy < h; dy++) {                                              \
        for (dx = 0; dx < w; dx++) {                                          \
            if (colorValue != fbptr[dx])                                      \
                return FALSE;                                                 \
        }                                                                     \
        fbptr = (CARD##bpp *)((CARD8 *)fbptr + pVNC->paddedWidthInBytes);     \
    }                                                                         \
                                                                              \
    *colorPtr = (CARD32)colorValue;                                           \
    return TRUE;                                                              \
}

DEFINE_CHECK_SOLID_FUNCTION(8)
DEFINE_CHECK_SOLID_FUNCTION(16)
DEFINE_CHECK_SOLID_FUNCTION(32)

static Bool
SendRectSimple(cl, x, y, w, h)
    rfbClientPtr cl;
    int x, y, w, h;
{
    int maxBeforeSize, maxAfterSize;
    int maxRectSize, maxRectWidth;
    int subrectMaxWidth, subrectMaxHeight;
    int dx, dy;
    int rw, rh;

    maxRectSize = tightConf[cl->tightCompressLevel].maxRectSize;
    maxRectWidth = tightConf[cl->tightCompressLevel].maxRectWidth;

    maxBeforeSize = maxRectSize * (cl->format.bitsPerPixel / 8);
    maxAfterSize = maxBeforeSize + (maxBeforeSize + 99) / 100 + 12;

    if (tightBeforeBufSize < maxBeforeSize) {
        tightBeforeBufSize = maxBeforeSize;
        if (tightBeforeBuf == NULL)
            tightBeforeBuf = (char *)xalloc(tightBeforeBufSize);
        else
            tightBeforeBuf = (char *)xrealloc(tightBeforeBuf,
                                              tightBeforeBufSize);
    }

    if (tightAfterBufSize < maxAfterSize) {
        tightAfterBufSize = maxAfterSize;
        if (tightAfterBuf == NULL)
            tightAfterBuf = (char *)xalloc(tightAfterBufSize);
        else
            tightAfterBuf = (char *)xrealloc(tightAfterBuf,
                                             tightAfterBufSize);
    }

    if (w > maxRectWidth || w * h > maxRectSize) {
        subrectMaxWidth = (w > maxRectWidth) ? maxRectWidth : w;
        subrectMaxHeight = maxRectSize / subrectMaxWidth;

        for (dy = 0; dy < h; dy += subrectMaxHeight) {
            for (dx = 0; dx < w; dx += maxRectWidth) {
                rw = (dx + maxRectWidth < w) ? maxRectWidth : w - dx;
                rh = (dy + subrectMaxHeight < h) ? subrectMaxHeight : h - dy;
                if (!SendSubrect(cl, x+dx, y+dy, rw, rh))
                    return FALSE;
            }
        }
    } else {
        if (!SendSubrect(cl, x, y, w, h))
            return FALSE;
    }

    return TRUE;
}

static Bool
SendSubrect(cl, x, y, w, h)
    rfbClientPtr cl;
    int x, y, w, h;
{
    VNCSCREENPTR(cl->pScreen);
    unsigned char *fbptr;
    Bool success = FALSE;

    /* Send pending data if there is more than 128 bytes. */
    if (pVNC->ublen > 128) {
        if (!rfbSendUpdateBuf(cl))
            return FALSE;
    }

    if (!SendTightHeader(cl, x, y, w, h))
        return FALSE;

    fbptr = (pVNC->pfbMemory + (pVNC->paddedWidthInBytes * y)
             + (x * (pVNC->bitsPerPixel / 8)));

    (*cl->translateFn)(cl->pScreen, cl->translateLookupTable, &pVNC->rfbServerFormat,
                       &cl->format, fbptr, tightBeforeBuf,
                       pVNC->paddedWidthInBytes, w, h, x, y);

    paletteMaxColors = w * h / tightConf[cl->tightCompressLevel].idxMaxColorsDivisor;
    if ( paletteMaxColors < 2 &&
         w * h >= tightConf[cl->tightCompressLevel].monoMinRectSize ) {
        paletteMaxColors = 2;
    }
    switch (cl->format.bitsPerPixel) {
    case 8:
        FillPalette8(w * h);
        break;
    case 16:
        FillPalette16(w * h);
        break;
    default:
        FillPalette32(w * h);
    }

    switch (paletteNumColors) {
    case 0:
        /* Truecolor image */
        if (DetectSmoothImage(cl, &cl->format, w, h)) {
            if (cl->tightQualityLevel != -1) {
                success = SendJpegRect(cl, x, y, w, h,
                                  tightConf[cl->tightQualityLevel].jpegQuality);
            } else {
                success = SendGradientRect(cl, w, h);
            }
        } else {
            success = SendFullColorRect(cl, w, h);
        }
        break;
    case 1:
        /* Solid rectangle */
        success = SendSolidRect(cl);
        break;
    case 2:
        /* Two-color rectangle */
        success = SendMonoRect(cl, w, h);
        break;
    default:
        /* Up to 256 different colors */
        if ( paletteNumColors > 96 &&
             cl->tightQualityLevel != -1 && cl->tightQualityLevel <= 3 &&
             DetectSmoothImage(cl, &cl->format, w, h) ) {
            success = SendJpegRect(cl, x, y, w, h,
                                  tightConf[cl->tightQualityLevel].jpegQuality);
        } else {
            success = SendIndexedRect(cl, w, h);
        }
    }
    return success;
}

static Bool
SendTightHeader(cl, x, y, w, h)
    rfbClientPtr cl;
    int x, y, w, h;
{
    VNCSCREENPTR(cl->pScreen);
    rfbFramebufferUpdateRectHeader rect;

    if (pVNC->ublen + sz_rfbFramebufferUpdateRectHeader > UPDATE_BUF_SIZE) {
        if (!rfbSendUpdateBuf(cl))
            return FALSE;
    }

    rect.r.x = Swap16IfLE(x);
    rect.r.y = Swap16IfLE(y);
    rect.r.w = Swap16IfLE(w);
    rect.r.h = Swap16IfLE(h);
    rect.encoding = Swap32IfLE(rfbEncodingTight);

    memcpy(&pVNC->updateBuf[pVNC->ublen], (char *)&rect,
           sz_rfbFramebufferUpdateRectHeader);
    pVNC->ublen += sz_rfbFramebufferUpdateRectHeader;

    cl->rfbRectanglesSent[rfbEncodingTight]++;
    cl->rfbBytesSent[rfbEncodingTight] += sz_rfbFramebufferUpdateRectHeader;

    return TRUE;
}

/*
 * Subencoding implementations.
 */

static Bool
SendSolidRect(cl)
    rfbClientPtr cl;
{
    VNCSCREENPTR(cl->pScreen);
    int len;

    if (usePixelFormat24) {
        Pack24(cl->pScreen, tightBeforeBuf, &cl->format, 1);
        len = 3;
    } else
        len = cl->format.bitsPerPixel / 8;

    if (pVNC->ublen + 1 + len > UPDATE_BUF_SIZE) {
        if (!rfbSendUpdateBuf(cl))
            return FALSE;
    }

    pVNC->updateBuf[pVNC->ublen++] = (char)(rfbTightFill << 4);
    memcpy (&pVNC->updateBuf[pVNC->ublen], tightBeforeBuf, len);
    pVNC->ublen += len;

    cl->rfbBytesSent[rfbEncodingTight] += len + 1;

    return TRUE;
}

static Bool
SendMonoRect(cl, w, h)
    rfbClientPtr cl;
    int w, h;
{
    VNCSCREENPTR(cl->pScreen);
    int streamId = 1;
    int paletteLen, dataLen;

    if ( (pVNC->ublen + TIGHT_MIN_TO_COMPRESS + 6 +
          2 * cl->format.bitsPerPixel / 8) > UPDATE_BUF_SIZE ) {
        if (!rfbSendUpdateBuf(cl))
            return FALSE;
    }

    /* Prepare tight encoding header. */
    dataLen = (w + 7) / 8;
    dataLen *= h;

    pVNC->updateBuf[pVNC->ublen++] = (streamId | rfbTightExplicitFilter) << 4;
    pVNC->updateBuf[pVNC->ublen++] = rfbTightFilterPalette;
    pVNC->updateBuf[pVNC->ublen++] = 1;

    /* Prepare palette, convert image. */
    switch (cl->format.bitsPerPixel) {

    case 32:
        EncodeMonoRect32((CARD8 *)tightBeforeBuf, w, h);

        ((CARD32 *)tightAfterBuf)[0] = monoBackground;
        ((CARD32 *)tightAfterBuf)[1] = monoForeground;
        if (usePixelFormat24) {
            Pack24(cl->pScreen, tightAfterBuf, &cl->format, 2);
            paletteLen = 6;
        } else
            paletteLen = 8;

        memcpy(&pVNC->updateBuf[pVNC->ublen], tightAfterBuf, paletteLen);
        pVNC->ublen += paletteLen;
        cl->rfbBytesSent[rfbEncodingTight] += 3 + paletteLen;
        break;

    case 16:
        EncodeMonoRect16((CARD8 *)tightBeforeBuf, w, h);

        ((CARD16 *)tightAfterBuf)[0] = (CARD16)monoBackground;
        ((CARD16 *)tightAfterBuf)[1] = (CARD16)monoForeground;

        memcpy(&pVNC->updateBuf[pVNC->ublen], tightAfterBuf, 4);
        pVNC->ublen += 4;
        cl->rfbBytesSent[rfbEncodingTight] += 7;
        break;

    default:
        EncodeMonoRect8((CARD8 *)tightBeforeBuf, w, h);

        pVNC->updateBuf[pVNC->ublen++] = (char)monoBackground;
        pVNC->updateBuf[pVNC->ublen++] = (char)monoForeground;
        cl->rfbBytesSent[rfbEncodingTight] += 5;
    }

    return CompressData(cl, streamId, dataLen,
                        tightConf[cl->tightCompressLevel].monoZlibLevel,
                        Z_DEFAULT_STRATEGY);
}

static Bool
SendIndexedRect(cl, w, h)
    rfbClientPtr cl;
    int w, h;
{
    VNCSCREENPTR(cl->pScreen);
    int streamId = 2;
    int i, entryLen;

    if ( (pVNC->ublen + TIGHT_MIN_TO_COMPRESS + 6 +
          paletteNumColors * cl->format.bitsPerPixel / 8) > UPDATE_BUF_SIZE ) {
        if (!rfbSendUpdateBuf(cl))
            return FALSE;
    }

    /* Prepare tight encoding header. */
    pVNC->updateBuf[pVNC->ublen++] = (streamId | rfbTightExplicitFilter) << 4;
    pVNC->updateBuf[pVNC->ublen++] = rfbTightFilterPalette;
    pVNC->updateBuf[pVNC->ublen++] = (char)(paletteNumColors - 1);

    /* Prepare palette, convert image. */
    switch (cl->format.bitsPerPixel) {

    case 32:
        EncodeIndexedRect32((CARD8 *)tightBeforeBuf, w * h);

        for (i = 0; i < paletteNumColors; i++) {
            ((CARD32 *)tightAfterBuf)[i] =
                palette.entry[i].listNode->rgb;
        }
        if (usePixelFormat24) {
            Pack24(cl->pScreen, tightAfterBuf, &cl->format, paletteNumColors);
            entryLen = 3;
        } else
            entryLen = 4;

        memcpy(&pVNC->updateBuf[pVNC->ublen], tightAfterBuf, paletteNumColors * entryLen);
        pVNC->ublen += paletteNumColors * entryLen;
        cl->rfbBytesSent[rfbEncodingTight] += 3 + paletteNumColors * entryLen;
        break;

    case 16:
        EncodeIndexedRect16((CARD8 *)tightBeforeBuf, w * h);

        for (i = 0; i < paletteNumColors; i++) {
            ((CARD16 *)tightAfterBuf)[i] =
                (CARD16)palette.entry[i].listNode->rgb;
        }

        memcpy(&pVNC->updateBuf[pVNC->ublen], tightAfterBuf, paletteNumColors * 2);
        pVNC->ublen += paletteNumColors * 2;
        cl->rfbBytesSent[rfbEncodingTight] += 3 + paletteNumColors * 2;
        break;

    default:
        return FALSE;           /* Should never happen. */
    }

    return CompressData(cl, streamId, w * h,
                        tightConf[cl->tightCompressLevel].idxZlibLevel,
                        Z_DEFAULT_STRATEGY);
}

static Bool
SendFullColorRect(cl, w, h)
    rfbClientPtr cl;
    int w, h;
{
    VNCSCREENPTR(cl->pScreen);
    int streamId = 0;
    int len;

    if (pVNC->ublen + TIGHT_MIN_TO_COMPRESS + 1 > UPDATE_BUF_SIZE) {
        if (!rfbSendUpdateBuf(cl))
            return FALSE;
    }

    pVNC->updateBuf[pVNC->ublen++] = 0x00;  /* stream id = 0, no flushing, no filter */
    cl->rfbBytesSent[rfbEncodingTight]++;

    if (usePixelFormat24) {
        Pack24(cl->pScreen, tightBeforeBuf, &cl->format, w * h);
        len = 3;
    } else
        len = cl->format.bitsPerPixel / 8;

    return CompressData(cl, streamId, w * h * len,
                        tightConf[cl->tightCompressLevel].rawZlibLevel,
                        Z_DEFAULT_STRATEGY);
}

static Bool
SendGradientRect(cl, w, h)
    rfbClientPtr cl;
    int w, h;
{
    VNCSCREENPTR(cl->pScreen);
    int streamId = 3;
    int len;

    if (cl->format.bitsPerPixel == 8)
        return SendFullColorRect(cl, w, h);

    if (pVNC->ublen + TIGHT_MIN_TO_COMPRESS + 2 > UPDATE_BUF_SIZE) {
        if (!rfbSendUpdateBuf(cl))
            return FALSE;
    }

    if (prevRowBuf == NULL)
        prevRowBuf = (int *)xalloc(2048 * 3 * sizeof(int));

    pVNC->updateBuf[pVNC->ublen++] = (streamId | rfbTightExplicitFilter) << 4;
    pVNC->updateBuf[pVNC->ublen++] = rfbTightFilterGradient;
    cl->rfbBytesSent[rfbEncodingTight] += 2;

    if (usePixelFormat24) {
        FilterGradient24(cl->pScreen, tightBeforeBuf, &cl->format, w, h);
        len = 3;
    } else if (cl->format.bitsPerPixel == 32) {
        FilterGradient32(cl->pScreen, (CARD32 *)tightBeforeBuf, &cl->format, w, h);
        len = 4;
    } else {
        FilterGradient16(cl->pScreen, (CARD16 *)tightBeforeBuf, &cl->format, w, h);
        len = 2;
    }

    return CompressData(cl, streamId, w * h * len,
                        tightConf[cl->tightCompressLevel].gradientZlibLevel,
                        Z_FILTERED);
}

static Bool
CompressData(cl, streamId, dataLen, zlibLevel, zlibStrategy)
    rfbClientPtr cl;
    int streamId, dataLen, zlibLevel, zlibStrategy;
{
    VNCSCREENPTR(cl->pScreen);
    z_streamp pz;
    int err;

    if (dataLen < TIGHT_MIN_TO_COMPRESS) {
        memcpy(&pVNC->updateBuf[pVNC->ublen], tightBeforeBuf, dataLen);
        pVNC->ublen += dataLen;
        cl->rfbBytesSent[rfbEncodingTight] += dataLen;
        return TRUE;
    }

    pz = &cl->zsStruct[streamId];

    /* Initialize compression stream if needed. */
    if (!cl->zsActive[streamId]) {
        pz->zalloc = Z_NULL;
        pz->zfree = Z_NULL;
        pz->opaque = Z_NULL;

        err = deflateInit2 (pz, zlibLevel, Z_DEFLATED, MAX_WBITS,
                            MAX_MEM_LEVEL, zlibStrategy);
        if (err != Z_OK)
            return FALSE;

        cl->zsActive[streamId] = TRUE;
        cl->zsLevel[streamId] = zlibLevel;
    }

    /* Prepare buffer pointers. */
    pz->next_in = (Bytef *)tightBeforeBuf;
    pz->avail_in = dataLen;
    pz->next_out = (Bytef *)tightAfterBuf;
    pz->avail_out = tightAfterBufSize;

    /* Change compression parameters if needed. */
    if (zlibLevel != cl->zsLevel[streamId]) {
        if (deflateParams (pz, zlibLevel, zlibStrategy) != Z_OK) {
            return FALSE;
        }
        cl->zsLevel[streamId] = zlibLevel;
    }

    /* Actual compression. */
    if ( deflate (pz, Z_SYNC_FLUSH) != Z_OK ||
         pz->avail_in != 0 || pz->avail_out == 0 ) {
        return FALSE;
    }

    return SendCompressedData(cl, tightAfterBufSize - pz->avail_out);
}

static Bool SendCompressedData(cl, compressedLen)
    rfbClientPtr cl;
    int compressedLen;
{
    VNCSCREENPTR(cl->pScreen);
    int i, portionLen;

    pVNC->updateBuf[pVNC->ublen++] = compressedLen & 0x7F;
    cl->rfbBytesSent[rfbEncodingTight]++;
    if (compressedLen > 0x7F) {
        pVNC->updateBuf[pVNC->ublen-1] |= 0x80;
        pVNC->updateBuf[pVNC->ublen++] = compressedLen >> 7 & 0x7F;
        cl->rfbBytesSent[rfbEncodingTight]++;
        if (compressedLen > 0x3FFF) {
            pVNC->updateBuf[pVNC->ublen-1] |= 0x80;
            pVNC->updateBuf[pVNC->ublen++] = compressedLen >> 14 & 0xFF;
            cl->rfbBytesSent[rfbEncodingTight]++;
        }
    }

    portionLen = UPDATE_BUF_SIZE;
    for (i = 0; i < compressedLen; i += portionLen) {
        if (i + portionLen > compressedLen) {
            portionLen = compressedLen - i;
        }
        if (pVNC->ublen + portionLen > UPDATE_BUF_SIZE) {
            if (!rfbSendUpdateBuf(cl))
                return FALSE;
        }
        memcpy(&pVNC->updateBuf[pVNC->ublen], &tightAfterBuf[i], portionLen);
        pVNC->ublen += portionLen;
    }
    cl->rfbBytesSent[rfbEncodingTight] += compressedLen;
    return TRUE;
}

/*
 * Code to determine how many different colors used in rectangle.
 */

static void
FillPalette8(count)
    int count;
{
    CARD8 *data = (CARD8 *)tightBeforeBuf;
    CARD8 c0, c1;
    int i, n0, n1;

    paletteNumColors = 0;

    c0 = data[0];
    for (i = 1; i < count && data[i] == c0; i++);
    if (i == count) {
        paletteNumColors = 1;
        return;                 /* Solid rectangle */
    }

    if (paletteMaxColors < 2)
        return;

    n0 = i;
    c1 = data[i];
    n1 = 0;
    for (i++; i < count; i++) {
        if (data[i] == c0) {
            n0++;
        } else if (data[i] == c1) {
            n1++;
        } else
            break;
    }
    if (i == count) {
        if (n0 > n1) {
            monoBackground = (CARD32)c0;
            monoForeground = (CARD32)c1;
        } else {
            monoBackground = (CARD32)c1;
            monoForeground = (CARD32)c0;
        }
        paletteNumColors = 2;   /* Two colors */
    }
}

#define DEFINE_FILL_PALETTE_FUNCTION(bpp)                               \
                                                                        \
static void                                                             \
FillPalette##bpp(count)                                                 \
    int count;                                                          \
{                                                                       \
    CARD##bpp *data = (CARD##bpp *)tightBeforeBuf;                      \
    CARD##bpp c0, c1, ci = 0;                                           \
    int i, n0, n1, ni;                                                  \
                                                                        \
    c0 = data[0];                                                       \
    for (i = 1; i < count && data[i] == c0; i++);                       \
    if (i >= count) {                                                   \
        paletteNumColors = 1;   /* Solid rectangle */                   \
        return;                                                         \
    }                                                                   \
                                                                        \
    if (paletteMaxColors < 2) {                                         \
        paletteNumColors = 0;   /* Full-color encoding preferred */     \
        return;                                                         \
    }                                                                   \
                                                                        \
    n0 = i;                                                             \
    c1 = data[i];                                                       \
    n1 = 0;                                                             \
    for (i++; i < count; i++) {                                         \
        ci = data[i];                                                   \
        if (ci == c0) {                                                 \
            n0++;                                                       \
        } else if (ci == c1) {                                          \
            n1++;                                                       \
        } else                                                          \
            break;                                                      \
    }                                                                   \
    if (i >= count) {                                                   \
        if (n0 > n1) {                                                  \
            monoBackground = (CARD32)c0;                                \
            monoForeground = (CARD32)c1;                                \
        } else {                                                        \
            monoBackground = (CARD32)c1;                                \
            monoForeground = (CARD32)c0;                                \
        }                                                               \
        paletteNumColors = 2;   /* Two colors */                        \
        return;                                                         \
    }                                                                   \
                                                                        \
    PaletteReset();                                                     \
    PaletteInsert (c0, (CARD32)n0, bpp);                                \
    PaletteInsert (c1, (CARD32)n1, bpp);                                \
                                                                        \
    ni = 1;                                                             \
    for (i++; i < count; i++) {                                         \
        if (data[i] == ci) {                                            \
            ni++;                                                       \
        } else {                                                        \
            if (!PaletteInsert (ci, (CARD32)ni, bpp))                   \
                return;                                                 \
            ci = data[i];                                               \
            ni = 1;                                                     \
        }                                                               \
    }                                                                   \
    PaletteInsert (ci, (CARD32)ni, bpp);                                \
}

DEFINE_FILL_PALETTE_FUNCTION(16)
DEFINE_FILL_PALETTE_FUNCTION(32)


/*
 * Functions to operate with palette structures.
 */

#define HASH_FUNC16(rgb) ((int)((((rgb) >> 8) + (rgb)) & 0xFF))
#define HASH_FUNC32(rgb) ((int)((((rgb) >> 16) + ((rgb) >> 8)) & 0xFF))

static void
PaletteReset(void)
{
    paletteNumColors = 0;
    memset(palette.hash, 0, 256 * sizeof(COLOR_LIST *));
}

static int
PaletteInsert(CARD32 rgb, int numPixels, int bpp)
{
    COLOR_LIST *pnode;
    COLOR_LIST *prev_pnode = NULL;
    int hash_key, idx, new_idx, count;

    hash_key = (bpp == 16) ? HASH_FUNC16(rgb) : HASH_FUNC32(rgb);

    pnode = palette.hash[hash_key];

    while (pnode != NULL) {
        if (pnode->rgb == rgb) {
            /* Such palette entry already exists. */
            new_idx = idx = pnode->idx;
            count = palette.entry[idx].numPixels + numPixels;
            if (new_idx && palette.entry[new_idx-1].numPixels < count) {
                do {
                    palette.entry[new_idx] = palette.entry[new_idx-1];
                    palette.entry[new_idx].listNode->idx = new_idx;
                    new_idx--;
                }
                while (new_idx && palette.entry[new_idx-1].numPixels < count);
                palette.entry[new_idx].listNode = pnode;
                pnode->idx = new_idx;
            }
            palette.entry[new_idx].numPixels = count;
            return paletteNumColors;
        }
        prev_pnode = pnode;
        pnode = pnode->next;
    }

    /* Check if palette is full. */
    if (paletteNumColors == 256 || paletteNumColors == paletteMaxColors) {
        paletteNumColors = 0;
        return 0;
    }

    /* Move palette entries with lesser pixel counts. */
    for ( idx = paletteNumColors;
          idx > 0 && palette.entry[idx-1].numPixels < numPixels;
          idx-- ) {
        palette.entry[idx] = palette.entry[idx-1];
        palette.entry[idx].listNode->idx = idx;
    }

    /* Add new palette entry into the freed slot. */
    pnode = &palette.list[paletteNumColors];
    if (prev_pnode != NULL) {
        prev_pnode->next = pnode;
    } else {
        palette.hash[hash_key] = pnode;
    }
    pnode->next = NULL;
    pnode->idx = idx;
    pnode->rgb = rgb;
    palette.entry[idx].listNode = pnode;
    palette.entry[idx].numPixels = numPixels;

    return (++paletteNumColors);
}


/*
 * Converting 32-bit color samples into 24-bit colors.
 * Should be called only when redMax, greenMax and blueMax are 255.
 * Color components assumed to be byte-aligned.
 */

static void Pack24(pScreen, buf, fmt, count)
    ScreenPtr pScreen;
    char *buf;
    rfbPixelFormat *fmt;
    int count;
{
    VNCSCREENPTR(pScreen);
    CARD32 *buf32;
    CARD32 pix;
    int r_shift, g_shift, b_shift;

    buf32 = (CARD32 *)buf;

    if (!pVNC->rfbServerFormat.bigEndian == !fmt->bigEndian) {
        r_shift = fmt->redShift;
        g_shift = fmt->greenShift;
        b_shift = fmt->blueShift;
    } else {
        r_shift = 24 - fmt->redShift;
        g_shift = 24 - fmt->greenShift;
        b_shift = 24 - fmt->blueShift;
    }

    while (count--) {
        pix = *buf32++;
        *buf++ = (char)(pix >> r_shift);
        *buf++ = (char)(pix >> g_shift);
        *buf++ = (char)(pix >> b_shift);
    }
}


/*
 * Converting truecolor samples into palette indices.
 */

#define DEFINE_IDX_ENCODE_FUNCTION(bpp)                                 \
                                                                        \
static void                                                             \
EncodeIndexedRect##bpp(buf, count)                                      \
    CARD8 *buf;                                                         \
    int count;                                                          \
{                                                                       \
    COLOR_LIST *pnode;                                                  \
    CARD##bpp *src;                                                     \
    CARD##bpp rgb;                                                      \
    int rep = 0;                                                        \
                                                                        \
    src = (CARD##bpp *) buf;                                            \
                                                                        \
    while (count--) {                                                   \
        rgb = *src++;                                                   \
        while (count && *src == rgb) {                                  \
            rep++, src++, count--;                                      \
        }                                                               \
        pnode = palette.hash[HASH_FUNC##bpp(rgb)];                      \
        while (pnode != NULL) {                                         \
            if ((CARD##bpp)pnode->rgb == rgb) {                         \
                *buf++ = (CARD8)pnode->idx;                             \
                while (rep) {                                           \
                    *buf++ = (CARD8)pnode->idx;                         \
                    rep--;                                              \
                }                                                       \
                break;                                                  \
            }                                                           \
            pnode = pnode->next;                                        \
        }                                                               \
    }                                                                   \
}

DEFINE_IDX_ENCODE_FUNCTION(16)
DEFINE_IDX_ENCODE_FUNCTION(32)

#define DEFINE_MONO_ENCODE_FUNCTION(bpp)                                \
                                                                        \
static void                                                             \
EncodeMonoRect##bpp(buf, w, h)                                          \
    CARD8 *buf;                                                         \
    int w, h;                                                           \
{                                                                       \
    CARD##bpp *ptr;                                                     \
    CARD##bpp bg;                                                       \
    unsigned int value, mask;                                           \
    int aligned_width;                                                  \
    int x, y, bg_bits;                                                  \
                                                                        \
    ptr = (CARD##bpp *) buf;                                            \
    bg = (CARD##bpp) monoBackground;                                    \
    aligned_width = w - w % 8;                                          \
                                                                        \
    for (y = 0; y < h; y++) {                                           \
        for (x = 0; x < aligned_width; x += 8) {                        \
            for (bg_bits = 0; bg_bits < 8; bg_bits++) {                 \
                if (*ptr++ != bg)                                       \
                    break;                                              \
            }                                                           \
            if (bg_bits == 8) {                                         \
                *buf++ = 0;                                             \
                continue;                                               \
            }                                                           \
            mask = 0x80 >> bg_bits;                                     \
            value = mask;                                               \
            for (bg_bits++; bg_bits < 8; bg_bits++) {                   \
                mask >>= 1;                                             \
                if (*ptr++ != bg) {                                     \
                    value |= mask;                                      \
                }                                                       \
            }                                                           \
            *buf++ = (CARD8)value;                                      \
        }                                                               \
                                                                        \
        mask = 0x80;                                                    \
        value = 0;                                                      \
        if (x >= w)                                                     \
            continue;                                                   \
                                                                        \
        for (; x < w; x++) {                                            \
            if (*ptr++ != bg) {                                         \
                value |= mask;                                          \
            }                                                           \
            mask >>= 1;                                                 \
        }                                                               \
        *buf++ = (CARD8)value;                                          \
    }                                                                   \
}

DEFINE_MONO_ENCODE_FUNCTION(8)
DEFINE_MONO_ENCODE_FUNCTION(16)
DEFINE_MONO_ENCODE_FUNCTION(32)


/*
 * ``Gradient'' filter for 24-bit color samples.
 * Should be called only when redMax, greenMax and blueMax are 255.
 * Color components assumed to be byte-aligned.
 */

static void
FilterGradient24(pScreen, buf, fmt, w, h)
    ScreenPtr pScreen;
    char *buf;
    rfbPixelFormat *fmt;
    int w, h;
{
    VNCSCREENPTR(pScreen);
    CARD32 *buf32;
    CARD32 pix32;
    int *prevRowPtr;
    int shiftBits[3];
    int pixHere[3], pixUpper[3], pixLeft[3], pixUpperLeft[3];
    int prediction;
    int x, y, c;

    buf32 = (CARD32 *)buf;
    memset (prevRowBuf, 0, w * 3 * sizeof(int));

    if (!pVNC->rfbServerFormat.bigEndian == !fmt->bigEndian) {
        shiftBits[0] = fmt->redShift;
        shiftBits[1] = fmt->greenShift;
        shiftBits[2] = fmt->blueShift;
    } else {
        shiftBits[0] = 24 - fmt->redShift;
        shiftBits[1] = 24 - fmt->greenShift;
        shiftBits[2] = 24 - fmt->blueShift;
    }

    for (y = 0; y < h; y++) {
        for (c = 0; c < 3; c++) {
            pixUpper[c] = 0;
            pixHere[c] = 0;
        }
        prevRowPtr = prevRowBuf;
        for (x = 0; x < w; x++) {
            pix32 = *buf32++;
            for (c = 0; c < 3; c++) {
                pixUpperLeft[c] = pixUpper[c];
                pixLeft[c] = pixHere[c];
                pixUpper[c] = *prevRowPtr;
                pixHere[c] = (int)(pix32 >> shiftBits[c] & 0xFF);
                *prevRowPtr++ = pixHere[c];

                prediction = pixLeft[c] + pixUpper[c] - pixUpperLeft[c];
                if (prediction < 0) {
                    prediction = 0;
                } else if (prediction > 0xFF) {
                    prediction = 0xFF;
                }
                *buf++ = (char)(pixHere[c] - prediction);
            }
        }
    }
}


/*
 * ``Gradient'' filter for other color depths.
 */

#define DEFINE_GRADIENT_FILTER_FUNCTION(bpp)                             \
                                                                         \
static void                                                              \
FilterGradient##bpp(pScreen, buf, fmt, w, h)                             \
    ScreenPtr pScreen;							 \
    CARD##bpp *buf;                                                      \
    rfbPixelFormat *fmt;                                                 \
    int w, h;                                                            \
{                                                                        \
    VNCSCREENPTR(pScreen);						 \
    CARD##bpp pix, diff;                                                 \
    Bool endianMismatch;                                                 \
    int *prevRowPtr;                                                     \
    int maxColor[3], shiftBits[3];                                       \
    int pixHere[3], pixUpper[3], pixLeft[3], pixUpperLeft[3];            \
    int prediction;                                                      \
    int x, y, c;                                                         \
                                                                         \
    memset (prevRowBuf, 0, w * 3 * sizeof(int));                         \
                                                                         \
    endianMismatch = (!pVNC->rfbServerFormat.bigEndian != !fmt->bigEndian);    \
                                                                         \
    maxColor[0] = fmt->redMax;                                           \
    maxColor[1] = fmt->greenMax;                                         \
    maxColor[2] = fmt->blueMax;                                          \
    shiftBits[0] = fmt->redShift;                                        \
    shiftBits[1] = fmt->greenShift;                                      \
    shiftBits[2] = fmt->blueShift;                                       \
                                                                         \
    for (y = 0; y < h; y++) {                                            \
        for (c = 0; c < 3; c++) {                                        \
            pixUpper[c] = 0;                                             \
            pixHere[c] = 0;                                              \
        }                                                                \
        prevRowPtr = prevRowBuf;                                         \
        for (x = 0; x < w; x++) {                                        \
            pix = *buf;                                                  \
            if (endianMismatch) {                                        \
                pix = Swap##bpp(pix);                                    \
            }                                                            \
            diff = 0;                                                    \
            for (c = 0; c < 3; c++) {                                    \
                pixUpperLeft[c] = pixUpper[c];                           \
                pixLeft[c] = pixHere[c];                                 \
                pixUpper[c] = *prevRowPtr;                               \
                pixHere[c] = (int)(pix >> shiftBits[c] & maxColor[c]);   \
                *prevRowPtr++ = pixHere[c];                              \
                                                                         \
                prediction = pixLeft[c] + pixUpper[c] - pixUpperLeft[c]; \
                if (prediction < 0) {                                    \
                    prediction = 0;                                      \
                } else if (prediction > maxColor[c]) {                   \
                    prediction = maxColor[c];                            \
                }                                                        \
                diff |= ((pixHere[c] - prediction) & maxColor[c])        \
                    << shiftBits[c];                                     \
            }                                                            \
            if (endianMismatch) {                                        \
                diff = Swap##bpp(diff);                                  \
            }                                                            \
            *buf++ = diff;                                               \
        }                                                                \
    }                                                                    \
}

DEFINE_GRADIENT_FILTER_FUNCTION(16)
DEFINE_GRADIENT_FILTER_FUNCTION(32)


/*
 * Code to guess if given rectangle is suitable for smooth image
 * compression (by applying "gradient" filter or JPEG coder).
 */

#define JPEG_MIN_RECT_SIZE  4096

#define DETECT_SUBROW_WIDTH    7
#define DETECT_MIN_WIDTH       8
#define DETECT_MIN_HEIGHT      8

static int
DetectSmoothImage (cl, fmt, w, h)
    rfbClientPtr cl;
    rfbPixelFormat *fmt;
    int w, h;
{
    VNCSCREENPTR(cl->pScreen);
    unsigned long avgError;

    if ( pVNC->rfbServerFormat.bitsPerPixel == 8 || fmt->bitsPerPixel == 8 ||
         w < DETECT_MIN_WIDTH || h < DETECT_MIN_HEIGHT ) {
        return 0;
    }

    if (cl->tightQualityLevel != -1) {
        if (w * h < JPEG_MIN_RECT_SIZE) {
            return 0;
        }
    } else {
        if ( rfbTightDisableGradient ||
             w * h < tightConf[cl->tightCompressLevel].gradientMinRectSize ) {
            return 0;
        }
    }

    if (fmt->bitsPerPixel == 32) {
        if (usePixelFormat24) {
            avgError = DetectSmoothImage24(cl, fmt, w, h);
            if (cl->tightQualityLevel != -1) {
                return (avgError < tightConf[cl->tightQualityLevel].jpegThreshold24);
            }
            return (avgError < tightConf[cl->tightCompressLevel].gradientThreshold24);
        } else {
            avgError = DetectSmoothImage32(cl, fmt, w, h);
        }
    } else {
        avgError = DetectSmoothImage16(cl, fmt, w, h);
    }
    if (cl->tightQualityLevel != -1) {
        return (avgError < tightConf[cl->tightQualityLevel].jpegThreshold);
    }
    return (avgError < tightConf[cl->tightCompressLevel].gradientThreshold);
}

static unsigned long
DetectSmoothImage24 (cl, fmt, w, h)
    rfbClientPtr cl;
    rfbPixelFormat *fmt;
    int w, h;
{
    int off;
    int x, y, d, dx, c;
    int diffStat[256];
    int pixelCount = 0;
    int pix, left[3];
    unsigned long avgError;

    /* If client is big-endian, color samples begin from the second
       byte (offset 1) of a 32-bit pixel value. */
    off = (fmt->bigEndian != 0);

    memset(diffStat, 0, 256*sizeof(int));

    y = 0, x = 0;
    while (y < h && x < w) {
        for (d = 0; d < h - y && d < w - x - DETECT_SUBROW_WIDTH; d++) {
            for (c = 0; c < 3; c++) {
                left[c] = (int)tightBeforeBuf[((y+d)*w+x+d)*4+off+c] & 0xFF;
            }
            for (dx = 1; dx <= DETECT_SUBROW_WIDTH; dx++) {
                for (c = 0; c < 3; c++) {
                    pix = (int)tightBeforeBuf[((y+d)*w+x+d+dx)*4+off+c] & 0xFF;
                    diffStat[abs(pix - left[c])]++;
                    left[c] = pix;
                }
                pixelCount++;
            }
        }
        if (w > h) {
            x += h;
            y = 0;
        } else {
            x = 0;
            y += w;
        }
    }

    if (diffStat[0] * 33 / pixelCount >= 95)
        return 0;

    avgError = 0;
    for (c = 1; c < 8; c++) {
        avgError += (unsigned long)diffStat[c] * (unsigned long)(c * c);
        if (diffStat[c] == 0 || diffStat[c] > diffStat[c-1] * 2)
            return 0;
    }
    for (; c < 256; c++) {
        avgError += (unsigned long)diffStat[c] * (unsigned long)(c * c);
    }
    avgError /= (pixelCount * 3 - diffStat[0]);

    return avgError;
}

#define DEFINE_DETECT_FUNCTION(bpp)                                          \
                                                                             \
static unsigned long                                                         \
DetectSmoothImage##bpp (cl, fmt, w, h)                                       \
    rfbClientPtr cl;							     \
    rfbPixelFormat *fmt;                                                     \
    int w, h;                                                                \
{                                                                            \
    VNCSCREENPTR(cl->pScreen);						     \
    Bool endianMismatch;                                                     \
    CARD##bpp pix;                                                           \
    int maxColor[3], shiftBits[3];                                           \
    int x, y, d, dx, c;                                                      \
    int diffStat[256];                                                       \
    int pixelCount = 0;                                                      \
    int sample, sum, left[3];                                                \
    unsigned long avgError;                                                  \
                                                                             \
    endianMismatch = (!pVNC->rfbServerFormat.bigEndian != !fmt->bigEndian);        \
                                                                             \
    maxColor[0] = fmt->redMax;                                               \
    maxColor[1] = fmt->greenMax;                                             \
    maxColor[2] = fmt->blueMax;                                              \
    shiftBits[0] = fmt->redShift;                                            \
    shiftBits[1] = fmt->greenShift;                                          \
    shiftBits[2] = fmt->blueShift;                                           \
                                                                             \
    memset(diffStat, 0, 256*sizeof(int));                                    \
                                                                             \
    y = 0, x = 0;                                                            \
    while (y < h && x < w) {                                                 \
        for (d = 0; d < h - y && d < w - x - DETECT_SUBROW_WIDTH; d++) {     \
            pix = ((CARD##bpp *)tightBeforeBuf)[(y+d)*w+x+d];                \
            if (endianMismatch) {                                            \
                pix = Swap##bpp(pix);                                        \
            }                                                                \
            for (c = 0; c < 3; c++) {                                        \
                left[c] = (int)(pix >> shiftBits[c] & maxColor[c]);          \
            }                                                                \
            for (dx = 1; dx <= DETECT_SUBROW_WIDTH; dx++) {                  \
                pix = ((CARD##bpp *)tightBeforeBuf)[(y+d)*w+x+d+dx];         \
                if (endianMismatch) {                                        \
                    pix = Swap##bpp(pix);                                    \
                }                                                            \
                sum = 0;                                                     \
                for (c = 0; c < 3; c++) {                                    \
                    sample = (int)(pix >> shiftBits[c] & maxColor[c]);       \
                    sum += abs(sample - left[c]);                            \
                    left[c] = sample;                                        \
                }                                                            \
                if (sum > 255)                                               \
                    sum = 255;                                               \
                diffStat[sum]++;                                             \
                pixelCount++;                                                \
            }                                                                \
        }                                                                    \
        if (w > h) {                                                         \
            x += h;                                                          \
            y = 0;                                                           \
        } else {                                                             \
            x = 0;                                                           \
            y += w;                                                          \
        }                                                                    \
    }                                                                        \
                                                                             \
    if ((diffStat[0] + diffStat[1]) * 100 / pixelCount >= 90)                \
        return 0;                                                            \
                                                                             \
    avgError = 0;                                                            \
    for (c = 1; c < 8; c++) {                                                \
        avgError += (unsigned long)diffStat[c] * (unsigned long)(c * c);     \
        if (diffStat[c] == 0 || diffStat[c] > diffStat[c-1] * 2)             \
            return 0;                                                        \
    }                                                                        \
    for (; c < 256; c++) {                                                   \
        avgError += (unsigned long)diffStat[c] * (unsigned long)(c * c);     \
    }                                                                        \
    avgError /= (pixelCount - diffStat[0]);                                  \
                                                                             \
    return avgError;                                                         \
}

DEFINE_DETECT_FUNCTION(16)
DEFINE_DETECT_FUNCTION(32)


/*
 * JPEG compression stuff.
 */

static struct jpeg_destination_mgr jpegDstManager;
static Bool jpegError;
static int jpegDstDataLen;

static Bool
SendJpegRect(cl, x, y, w, h, quality)
    rfbClientPtr cl;
    int x, y, w, h;
    int quality;
{
    VNCSCREENPTR(cl->pScreen);
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;
    CARD8 *srcBuf;
    JSAMPROW rowPointer[1];
    int dy;

    if (pVNC->rfbServerFormat.bitsPerPixel == 8)
        return SendFullColorRect(cl, w, h);

    srcBuf = (CARD8 *)xalloc(w * 3);
    if (srcBuf == NULL) {
        return SendFullColorRect(cl, w, h);
    }
    rowPointer[0] = srcBuf;

    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);

    cinfo.image_width = w;
    cinfo.image_height = h;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_RGB;

    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, quality, TRUE);

    JpegSetDstManager (&cinfo);

    jpeg_start_compress(&cinfo, TRUE);

    for (dy = 0; dy < h; dy++) {
        PrepareRowForJpeg(cl->pScreen, srcBuf, x, y + dy, w);
        jpeg_write_scanlines(&cinfo, rowPointer, 1);
        if (jpegError)
            break;
    }

    if (!jpegError)
        jpeg_finish_compress(&cinfo);

    jpeg_destroy_compress(&cinfo);
    xfree((char *)srcBuf);

    if (jpegError)
        return SendFullColorRect(cl, w, h);

    if (pVNC->ublen + TIGHT_MIN_TO_COMPRESS + 1 > UPDATE_BUF_SIZE) {
        if (!rfbSendUpdateBuf(cl))
            return FALSE;
    }

    pVNC->updateBuf[pVNC->ublen++] = (char)(rfbTightJpeg << 4);
    cl->rfbBytesSent[rfbEncodingTight]++;

    return SendCompressedData(cl, jpegDstDataLen);
}

static void
PrepareRowForJpeg(pScreen, dst, x, y, count)
    ScreenPtr pScreen;
    CARD8 *dst;
    int x, y, count;
{
    VNCSCREENPTR(pScreen);
    if (pVNC->rfbServerFormat.bitsPerPixel == 32) {
        if ( pVNC->rfbServerFormat.redMax == 0xFF &&
             pVNC->rfbServerFormat.greenMax == 0xFF &&
             pVNC->rfbServerFormat.blueMax == 0xFF ) {
            PrepareRowForJpeg24(pScreen, dst, x, y, count);
        } else {
            PrepareRowForJpeg32(pScreen, dst, x, y, count);
        }
    } else {
        /* 16 bpp assumed. */
        PrepareRowForJpeg16(pScreen, dst, x, y, count);
    }
}

static void
PrepareRowForJpeg24(pScreen, dst, x, y, count)
    ScreenPtr pScreen;
    CARD8 *dst;
    int x, y, count;
{
    VNCSCREENPTR(pScreen);
    CARD32 *fbptr;
    CARD32 pix;

    fbptr = (CARD32 *)
        &pVNC->pfbMemory[y * pVNC->paddedWidthInBytes + x * 4];

    while (count--) {
        pix = *fbptr++;
        *dst++ = (CARD8)(pix >> pVNC->rfbServerFormat.redShift);
        *dst++ = (CARD8)(pix >> pVNC->rfbServerFormat.greenShift);
        *dst++ = (CARD8)(pix >> pVNC->rfbServerFormat.blueShift);
    }
}

#define DEFINE_JPEG_GET_ROW_FUNCTION(bpp)                                   \
                                                                            \
static void                                                                 \
PrepareRowForJpeg##bpp(pScreen, dst, x, y, count)                           \
    ScreenPtr pScreen;							    \
    CARD8 *dst;                                                             \
    int x, y, count;                                                        \
{                                                                           \
    VNCSCREENPTR(pScreen);						    \
    CARD##bpp *fbptr;                                                       \
    CARD##bpp pix;                                                          \
    int inRed, inGreen, inBlue;                                             \
                                                                            \
    fbptr = (CARD##bpp *)                                                   \
        &pVNC->pfbMemory[y * pVNC->paddedWidthInBytes +                        \
                             x * (bpp / 8)];                                \
                                                                            \
    while (count--) {                                                       \
        pix = *fbptr++;                                                     \
                                                                            \
        inRed = (int)                                                       \
            (pix >> pVNC->rfbServerFormat.redShift   & pVNC->rfbServerFormat.redMax);   \
        inGreen = (int)                                                     \
            (pix >> pVNC->rfbServerFormat.greenShift & pVNC->rfbServerFormat.greenMax); \
        inBlue  = (int)                                                     \
            (pix >> pVNC->rfbServerFormat.blueShift  & pVNC->rfbServerFormat.blueMax);  \
                                                                            \
	*dst++ = (CARD8)((inRed   * 255 + pVNC->rfbServerFormat.redMax / 2) /     \
                         pVNC->rfbServerFormat.redMax);                           \
	*dst++ = (CARD8)((inGreen * 255 + pVNC->rfbServerFormat.greenMax / 2) /   \
                         pVNC->rfbServerFormat.greenMax);                         \
	*dst++ = (CARD8)((inBlue  * 255 + pVNC->rfbServerFormat.blueMax / 2) /    \
                         pVNC->rfbServerFormat.blueMax);                          \
    }                                                                       \
}

DEFINE_JPEG_GET_ROW_FUNCTION(16)
DEFINE_JPEG_GET_ROW_FUNCTION(32)

/*
 * Destination manager implementation for JPEG library.
 */

static void
JpegInitDestination(j_compress_ptr cinfo)
{
    jpegError = FALSE;
    jpegDstManager.next_output_byte = (JOCTET *)tightAfterBuf;
    jpegDstManager.free_in_buffer = (size_t)tightAfterBufSize;
}

static boolean
JpegEmptyOutputBuffer(j_compress_ptr cinfo)
{
    jpegError = TRUE;
    jpegDstManager.next_output_byte = (JOCTET *)tightAfterBuf;
    jpegDstManager.free_in_buffer = (size_t)tightAfterBufSize;

    return TRUE;
}

static void
JpegTermDestination(j_compress_ptr cinfo)
{
    jpegDstDataLen = tightAfterBufSize - jpegDstManager.free_in_buffer;
}

static void
JpegSetDstManager(j_compress_ptr cinfo)
{
    jpegDstManager.init_destination = JpegInitDestination;
    jpegDstManager.empty_output_buffer = JpegEmptyOutputBuffer;
    jpegDstManager.term_destination = JpegTermDestination;
    cinfo->dest = &jpegDstManager;
}

