/* $XFree86: xc/programs/Xserver/hw/darwin/quartz/xpr/dri.c,v 1.1 2003/06/30 01:45:13 torrey Exp $ */
/**************************************************************************

Copyright 1998-1999 Precision Insight, Inc., Cedar Park, Texas.
Copyright 2000 VA Linux Systems, Inc.
Copyright (c) 2002 Apple Computer, Inc.
All Rights Reserved.

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sub license, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice (including the
next paragraph) shall be included in all copies or substantial portions
of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
IN NO EVENT SHALL PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR
ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

**************************************************************************/

/*
 * Authors:
 *   Jens Owen <jens@valinux.com>
 *   Rickard E. (Rik) Faith <faith@valinux.com>
 *
 */

#ifdef XFree86LOADER
#include "xf86.h"
#include "xf86_ansic.h"
#else
#include <sys/time.h>
#include <unistd.h>
#endif

#define NEED_REPLIES
#define NEED_EVENTS
#include "X.h"
#include "Xproto.h"
#include "misc.h"
#include "dixstruct.h"
#include "extnsionst.h"
#include "colormapst.h"
#include "cursorstr.h"
#include "scrnintstr.h"
#include "windowstr.h"
#include "servermd.h"
#define _APPLEDRI_SERVER_
#include "appledristr.h"
#include "swaprep.h"
#include "dri.h"
#include "dristruct.h"
#include "mi.h"
#include "mipointer.h"
#include "rootless.h"
#include "x-hash.h"
#include "x-hook.h"

static int DRIScreenPrivIndex = -1;
static int DRIWindowPrivIndex = -1;

static RESTYPE DRIDrawablePrivResType;

static x_hash_table *surface_hash;	/* maps surface ids -> drawablePrivs */

/* FIXME: don't hardcode this? */
#define CG_INFO_FILE "/System/Library/Frameworks/ApplicationServices.framework/Frameworks/CoreGraphics.framework/Resources/Info-macos.plist"

/* Corresponds to SU Jaguar Green */
#define CG_REQUIRED_MAJOR 1
#define CG_REQUIRED_MINOR 157
#define CG_REQUIRED_MICRO 11

/* Returns version as major.minor.micro in 10.10.10 fixed form */
static unsigned int
get_cg_version (void)
{
    static unsigned int version;

    FILE *fh;
    char *ptr;

    if (version != 0)
        return version;

    /* I tried CFBundleGetVersion, but it returns zero, so.. */

    fh = fopen (CG_INFO_FILE, "r");
    if (fh != NULL)
    {
        char buf[256];

        while (fgets (buf, sizeof (buf), fh) != NULL)
        {
            unsigned char c;

            if (!strstr (buf, "<key>CFBundleShortVersionString</key>")
                || fgets (buf, sizeof (buf), fh) == NULL)
            {
                continue;
            }

            ptr = strstr (buf, "<string>");
            if (ptr == NULL)
                continue;

            ptr += strlen ("<string>");

            /* Now PTR points to "MAJOR.MINOR.MICRO". */

            version = 0;

        again:
            switch ((c = *ptr++))
            {
            case '.':
                version = version * 1024;
                goto again;

            case '0': case '1': case '2': case '3': case '4':
            case '5': case '6': case '7': case '8': case '9':
                version = ((version & ~0x3ff)
                          + (version & 0x3ff) * 10 + (c - '0'));
                goto again;
            }
            break;
        }

        fclose (fh);
    }

    return version;
}

static Bool
test_cg_version (unsigned int major, unsigned int minor, unsigned int micro)
{
    unsigned int cg_ver = get_cg_version ();

    unsigned int cg_major = (cg_ver >> 20) & 0x3ff;
    unsigned int cg_minor = (cg_ver >> 10) & 0x3ff;
    unsigned int cg_micro =  cg_ver        & 0x3ff;

    if (cg_major > major)
        return TRUE;
    else if (cg_major < major)
        return FALSE;

    /* cg_major == major */

    if (cg_minor > minor)
        return TRUE;
    else if (cg_minor < minor)
        return FALSE;

    /* cg_minor == minor */

    if (cg_micro < micro)
        return FALSE;

    return TRUE;
}

Bool
DRIScreenInit(ScreenPtr pScreen)
{
    DRIScreenPrivPtr    pDRIPriv;
    int                 i;

    pDRIPriv = (DRIScreenPrivPtr) xcalloc(1, sizeof(DRIScreenPrivRec));
    if (!pDRIPriv) {
        pScreen->devPrivates[DRIScreenPrivIndex].ptr = NULL;
        return FALSE;
    }

    pScreen->devPrivates[DRIScreenPrivIndex].ptr = (pointer) pDRIPriv;
    pDRIPriv->directRenderingSupport = TRUE;
    pDRIPriv->nrWindows = 0;

    /* Need recent cg for window access update */
    if (!test_cg_version (CG_REQUIRED_MAJOR,
                          CG_REQUIRED_MINOR,
                          CG_REQUIRED_MICRO))
    {
        ErrorF ("[DRI] disabled direct rendering; requires CoreGraphics %d.%d.%d\n",
                CG_REQUIRED_MAJOR, CG_REQUIRED_MINOR, CG_REQUIRED_MICRO);

        pDRIPriv->directRenderingSupport = FALSE;

        /* Note we don't nuke the dri private, since we need it for
           managing indirect surfaces. */
    }

    /* Initialize drawable tables */
    for (i = 0; i < DRI_MAX_DRAWABLES; i++) {
        pDRIPriv->DRIDrawables[i] = NULL;
    }

    return TRUE;
}

Bool
DRIFinishScreenInit(ScreenPtr pScreen)
{
    DRIScreenPrivPtr  pDRIPriv = DRI_SCREEN_PRIV(pScreen);

    /* Allocate zero sized private area for each window. Should a window
     * become a DRI window, we'll hang a DRIWindowPrivateRec off of this
     * private index.
     */
    if (!AllocateWindowPrivate(pScreen, DRIWindowPrivIndex, 0))
        return FALSE;

    /* Wrap DRI support */
    pDRIPriv->wrap.ValidateTree = pScreen->ValidateTree;
    pScreen->ValidateTree = DRIValidateTree;

    pDRIPriv->wrap.PostValidateTree = pScreen->PostValidateTree;
    pScreen->PostValidateTree = DRIPostValidateTree;

    pDRIPriv->wrap.WindowExposures = pScreen->WindowExposures;
    pScreen->WindowExposures = DRIWindowExposures;

    pDRIPriv->wrap.CopyWindow = pScreen->CopyWindow;
    pScreen->CopyWindow = DRICopyWindow;

    pDRIPriv->wrap.ClipNotify = pScreen->ClipNotify;
    pScreen->ClipNotify = DRIClipNotify;

    ErrorF("[DRI] screen %d installation complete\n", pScreen->myNum);

    return TRUE;
}

void
DRICloseScreen(ScreenPtr pScreen)
{
    DRIScreenPrivPtr pDRIPriv = DRI_SCREEN_PRIV(pScreen);

    if (pDRIPriv && pDRIPriv->directRenderingSupport) {
        xfree(pDRIPriv);
        pScreen->devPrivates[DRIScreenPrivIndex].ptr = NULL;
    }
}

Bool
DRIExtensionInit(void)
{
    static unsigned long DRIGeneration = 0;

    if (DRIGeneration != serverGeneration) {
        if ((DRIScreenPrivIndex = AllocateScreenPrivateIndex()) < 0)
            return FALSE;
        DRIGeneration = serverGeneration;
    }

    /* Allocate a window private index with a zero sized private area for
     * each window, then should a window become a DRI window, we'll hang
     * a DRIWindowPrivateRec off of this private index.
     */
    if ((DRIWindowPrivIndex = AllocateWindowPrivateIndex()) < 0)
        return FALSE;

    DRIDrawablePrivResType = CreateNewResourceType(DRIDrawablePrivDelete);

    return TRUE;
}

void
DRIReset(void)
{
    /*
     * This stub routine is called when the X Server recycles, resources
     * allocated by DRIExtensionInit need to be managed here.
     *
     * Currently this routine is a stub because all the interesting resources
     * are managed via the screen init process.
     */
}

Bool
DRIQueryDirectRenderingCapable(ScreenPtr pScreen, Bool* isCapable)
{
    DRIScreenPrivPtr pDRIPriv = DRI_SCREEN_PRIV(pScreen);

    if (pDRIPriv)
        *isCapable = pDRIPriv->directRenderingSupport;
    else
        *isCapable = FALSE;

    return TRUE;
}

Bool
DRIAuthConnection(ScreenPtr pScreen, unsigned int magic)
{
#if 0
    /* FIXME: something? */

    DRIScreenPrivPtr pDRIPriv = DRI_SCREEN_PRIV(pScreen);

    if (drmAuthMagic(pDRIPriv->drmFD, magic)) return FALSE;
#endif
    return TRUE;
}

static void
DRIUpdateSurface(DRIDrawablePrivPtr pDRIDrawablePriv, WindowPtr pWin)
{
    WindowPtr pTopWin;
    xp_window_changes wc;

    if (pDRIDrawablePriv->sid == 0)
        return;

    pTopWin = TopLevelParent(pWin);

    wc.x = pWin->drawable.x - (pTopWin->drawable.x - pTopWin->borderWidth);
    wc.y = pWin->drawable.y - (pTopWin->drawable.y - pTopWin->borderWidth);
    wc.width = pWin->drawable.width + 2 * pWin->borderWidth;
    wc.height = pWin->drawable.height + 2 * pWin->borderWidth;
    wc.bit_gravity = XP_GRAVITY_NONE;

    wc.shape_nrects = REGION_NUM_RECTS(&pWin->clipList);
    wc.shape_rects = REGION_RECTS(&pWin->clipList);
    wc.shape_tx = - (pTopWin->drawable.x - pTopWin->borderWidth);
    wc.shape_ty = - (pTopWin->drawable.y - pTopWin->borderWidth);

    xp_configure_surface(pDRIDrawablePriv->sid, XP_BOUNDS | XP_SHAPE, &wc);
}

Bool
DRICreateSurface (ScreenPtr pScreen, Drawable id,
                  DrawablePtr pDrawable, xp_client_id client_id,
                  xp_surface_id *surface_id, unsigned int ret_key[2],
                  void (*notify) (void *arg, void *data), void *notify_data)
{
    DRIScreenPrivPtr	pDRIPriv = DRI_SCREEN_PRIV(pScreen);
    DRIDrawablePrivPtr	pDRIDrawablePriv;
    WindowPtr		pWin;

    if (pDrawable->type == DRAWABLE_WINDOW) {
        pWin = (WindowPtr)pDrawable;
        if ((pDRIDrawablePriv = DRI_DRAWABLE_PRIV_FROM_WINDOW(pWin))) {
            pDRIDrawablePriv->refCount++;
        }
        else {
            xp_window_id wid;
            xp_surface_id sid;
            xp_error err;
            unsigned int key[2];
            xp_window_changes wc;

            /* allocate a DRI Window Private record */
            if (!(pDRIDrawablePriv = xalloc(sizeof(DRIDrawablePrivRec)))) {
                return FALSE;
            }

            /* find the physical window */
            wid = (xp_window_id) RootlessFrameForWindow (pWin, TRUE);
            if (wid == 0) {
                xfree (pDRIDrawablePriv);
                return FALSE;
            }

            /* allocate the physical surface */
            err = xp_create_surface (wid, &sid);
            if (err != Success) {
                xfree (pDRIDrawablePriv);
                return FALSE;
            }

            /* try to give the client access to the surface */
            if (client_id != 0)
            {
                err = xp_export_surface (wid, sid, client_id, key);
                if (err != Success) {
                    xp_destroy_surface (sid);
                    xfree (pDRIDrawablePriv);
                    return FALSE;
                }
            }

            /* Make it visible */
            wc.stack_mode = XP_MAPPED_ABOVE;
            wc.sibling = 0;
            err = xp_configure_surface (sid, XP_STACKING, &wc);
            if (err != Success)
            {
                xp_destroy_surface (sid);
                xfree (pDRIDrawablePriv);
                return FALSE;
            }

            /* add it to the list of DRI drawables for this screen */
            pDRIDrawablePriv->sid = sid;
            pDRIDrawablePriv->pDraw = pDrawable;
            pDRIDrawablePriv->pScreen = pScreen;
            pDRIDrawablePriv->refCount = 1;
            pDRIDrawablePriv->drawableIndex = -1;
            pDRIDrawablePriv->key[0] = key[0];
            pDRIDrawablePriv->key[1] = key[1];
            pDRIDrawablePriv->notifiers = NULL;

            /* save private off of preallocated index */
            pWin->devPrivates[DRIWindowPrivIndex].ptr =
                                                (pointer)pDRIDrawablePriv;

            ++pDRIPriv->nrWindows;

            /* and stash it by surface id */
            if (surface_hash == NULL)
                surface_hash = x_hash_table_new (NULL, NULL, NULL, NULL);
            x_hash_table_insert (surface_hash, (void *) sid, pDRIDrawablePriv);

            /* track this in case this window is destroyed */
            AddResource(id, DRIDrawablePrivResType, (pointer)pWin);

            /* Initialize shape */
            DRIUpdateSurface (pDRIDrawablePriv, pWin);
        }

        if (notify != NULL) {
            pDRIDrawablePriv->notifiers
                = x_hook_add (pDRIDrawablePriv->notifiers,
                              notify, notify_data);
        }

        *surface_id = pDRIDrawablePriv->sid;

        if (ret_key != NULL)
        {
            ret_key[0] = pDRIDrawablePriv->key[0];
            ret_key[1] = pDRIDrawablePriv->key[1];
        }
    }
    else { /* pixmap (or for GLX 1.3, a PBuffer) */
        /* NOT_DONE */
        return FALSE;
    }

    return TRUE;
}

Bool
DRIDestroySurface(ScreenPtr pScreen, Drawable id, DrawablePtr pDrawable,
                  void (*notify) (void *, void *), void *notify_data)
{
    DRIDrawablePrivPtr	pDRIDrawablePriv;
    WindowPtr		pWin;

    if (pDrawable->type == DRAWABLE_WINDOW) {
        pWin = (WindowPtr)pDrawable;
        pDRIDrawablePriv = DRI_DRAWABLE_PRIV_FROM_WINDOW(pWin);
        if (pDRIDrawablePriv != NULL) {
            if (notify != NULL)
            {
                pDRIDrawablePriv->notifiers
                    = x_hook_remove (pDRIDrawablePriv->notifiers,
                                     notify, notify_data);
            }
            if (--pDRIDrawablePriv->refCount <= 0) {
                /* This calls back to DRIDrawablePrivDelete
                   which frees the private area */
                FreeResourceByType(id, DRIDrawablePrivResType, FALSE);
            }
        }
    }
    else { /* pixmap (or for GLX 1.3, a PBuffer) */
        /* NOT_DONE */
        return FALSE;
    }

    return TRUE;
}

Bool
DRIDrawablePrivDelete(pointer pResource, XID id)
{
    DrawablePtr		pDrawable = (DrawablePtr)pResource;
    DRIScreenPrivPtr	pDRIPriv = DRI_SCREEN_PRIV(pDrawable->pScreen);
    DRIDrawablePrivPtr	pDRIDrawablePriv;
    WindowPtr		pWin;

    if (pDrawable->type == DRAWABLE_WINDOW) {
        pWin = (WindowPtr)pDrawable;
        pDRIDrawablePriv = DRI_DRAWABLE_PRIV_FROM_WINDOW(pWin);

        if (pDRIDrawablePriv->drawableIndex != -1) {
            /* release drawable table entry */
            pDRIPriv->DRIDrawables[pDRIDrawablePriv->drawableIndex] = NULL;
        }

        if (pDRIDrawablePriv->sid != 0) {
            xp_destroy_surface (pDRIDrawablePriv->sid);
            x_hash_table_remove (surface_hash, (void *) pDRIDrawablePriv->sid);
        }

        if (pDRIDrawablePriv->notifiers != NULL)
            x_hook_free (pDRIDrawablePriv->notifiers);

        xfree(pDRIDrawablePriv);
        pWin->devPrivates[DRIWindowPrivIndex].ptr = NULL;

        --pDRIPriv->nrWindows;
    }
    else { /* pixmap (or for GLX 1.3, a PBuffer) */
        /* NOT_DONE */
        return FALSE;
    }

    return TRUE;
}

void
DRIWindowExposures(WindowPtr pWin, RegionPtr prgn, RegionPtr bsreg)
{
    ScreenPtr pScreen = pWin->drawable.pScreen;
    DRIScreenPrivPtr pDRIPriv = DRI_SCREEN_PRIV(pScreen);
    DRIDrawablePrivPtr pDRIDrawablePriv = DRI_DRAWABLE_PRIV_FROM_WINDOW(pWin);

    if(pDRIDrawablePriv) {
        /* FIXME: something? */
    }

    pScreen->WindowExposures = pDRIPriv->wrap.WindowExposures;

    (*pScreen->WindowExposures)(pWin, prgn, bsreg);

    pDRIPriv->wrap.WindowExposures = pScreen->WindowExposures;
    pScreen->WindowExposures = DRIWindowExposures;

}

void
DRICopyWindow(WindowPtr pWin, DDXPointRec ptOldOrg, RegionPtr prgnSrc)
{
    ScreenPtr pScreen = pWin->drawable.pScreen;
    DRIScreenPrivPtr pDRIPriv = DRI_SCREEN_PRIV(pScreen);
    DRIDrawablePrivPtr pDRIDrawablePriv;

    if (pDRIPriv->nrWindows > 0) {
       pDRIDrawablePriv = DRI_DRAWABLE_PRIV_FROM_WINDOW (pWin);
       if (pDRIDrawablePriv != NULL) {
            DRIUpdateSurface (pDRIDrawablePriv, pWin);
       }
    }

    /* unwrap */
    pScreen->CopyWindow = pDRIPriv->wrap.CopyWindow;

    /* call lower layers */
    (*pScreen->CopyWindow)(pWin, ptOldOrg, prgnSrc);

    /* rewrap */
    pDRIPriv->wrap.CopyWindow = pScreen->CopyWindow;
    pScreen->CopyWindow = DRICopyWindow;
}

int
DRIValidateTree(WindowPtr pParent, WindowPtr pChild, VTKind kind)
{
    ScreenPtr pScreen = pParent->drawable.pScreen;
    DRIScreenPrivPtr pDRIPriv = DRI_SCREEN_PRIV(pScreen);
    int returnValue;

    /* unwrap */
    pScreen->ValidateTree = pDRIPriv->wrap.ValidateTree;

    /* call lower layers */
    returnValue = (*pScreen->ValidateTree)(pParent, pChild, kind);

    /* rewrap */
    pDRIPriv->wrap.ValidateTree = pScreen->ValidateTree;
    pScreen->ValidateTree = DRIValidateTree;

    return returnValue;
}

void
DRIPostValidateTree(WindowPtr pParent, WindowPtr pChild, VTKind kind)
{
    ScreenPtr pScreen;
    DRIScreenPrivPtr pDRIPriv;

    if (pParent) {
        pScreen = pParent->drawable.pScreen;
    } else {
        pScreen = pChild->drawable.pScreen;
    }
    pDRIPriv = DRI_SCREEN_PRIV(pScreen);

    if (pDRIPriv->wrap.PostValidateTree) {
        /* unwrap */
        pScreen->PostValidateTree = pDRIPriv->wrap.PostValidateTree;

        /* call lower layers */
        (*pScreen->PostValidateTree)(pParent, pChild, kind);

        /* rewrap */
        pDRIPriv->wrap.PostValidateTree = pScreen->PostValidateTree;
        pScreen->PostValidateTree = DRIPostValidateTree;
    }
}

void
DRIClipNotify(WindowPtr pWin, int dx, int dy)
{
    ScreenPtr pScreen = pWin->drawable.pScreen;
    DRIScreenPrivPtr pDRIPriv = DRI_SCREEN_PRIV(pScreen);
    DRIDrawablePrivPtr	pDRIDrawablePriv;

    if ((pDRIDrawablePriv = DRI_DRAWABLE_PRIV_FROM_WINDOW(pWin))) {
        DRIUpdateSurface (pDRIDrawablePriv, pWin);
    }

    if(pDRIPriv->wrap.ClipNotify) {
        pScreen->ClipNotify = pDRIPriv->wrap.ClipNotify;

        (*pScreen->ClipNotify)(pWin, dx, dy);

        pDRIPriv->wrap.ClipNotify = pScreen->ClipNotify;
        pScreen->ClipNotify = DRIClipNotify;
    }
}

/* This lets us get at the unwrapped functions so that they can correctly
 * call the lower level functions, and choose whether they will be
 * called at every level of recursion (eg in validatetree).
 */
DRIWrappedFuncsRec *
DRIGetWrappedFuncs(ScreenPtr pScreen)
{
    return &(DRI_SCREEN_PRIV(pScreen)->wrap);
}

void
DRIQueryVersion(int *majorVersion,
                int *minorVersion,
                int *patchVersion)
{
    *majorVersion = APPLE_DRI_MAJOR_VERSION;
    *minorVersion = APPLE_DRI_MINOR_VERSION;
    *patchVersion = APPLE_DRI_PATCH_VERSION;
}

void
DRISurfaceNotify (xp_surface_id id, int kind)
{
    DRIDrawablePrivPtr pDRIDrawablePriv = NULL;
    DRISurfaceNotifyArg arg;

    arg.id = id;
    arg.kind = kind;

    if (surface_hash != NULL)
    {
        pDRIDrawablePriv = x_hash_table_lookup (surface_hash,
                                                (void *) id, NULL);
    }

    if (pDRIDrawablePriv == NULL)
        return;

    if (kind == AppleDRISurfaceNotifyDestroyed)
    {
        pDRIDrawablePriv->sid = 0;
        x_hash_table_remove (surface_hash, (void *) id);
    }

    x_hook_run (pDRIDrawablePriv->notifiers, &arg);

    if (kind == AppleDRISurfaceNotifyDestroyed)
    {
        /* Kill off the handle. */

        FreeResourceByType (pDRIDrawablePriv->pDraw->id,
                            DRIDrawablePrivResType, FALSE);
    }
}
