/*
 *Copyright (C) 1994-2000 The XFree86 Project, Inc. All Rights Reserved.
 *
 *Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 *"Software"), to deal in the Software without restriction, including
 *without limitation the rights to use, copy, modify, merge, publish,
 *distribute, sublicense, and/or sell copies of the Software, and to
 *permit persons to whom the Software is furnished to do so, subject to
 *the following conditions:
 *
 *The above copyright notice and this permission notice shall be
 *included in all copies or substantial portions of the Software.
 *
 *THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 *EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 *MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 *NONINFRINGEMENT. IN NO EVENT SHALL THE XFREE86 PROJECT BE LIABLE FOR
 *ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 *CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 *WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 *Except as contained in this notice, the name of the XFree86 Project
 *shall not be used in advertising or otherwise to promote the sale, use
 *or other dealings in this Software without prior written authorization
 *from the XFree86 Project.
 *
 * Authors:	Kensuke Matsuzaki
 *		Harold L Hunt II
 */
/* $XFree86: xc/programs/Xserver/hw/xwin/winmultiwindowshape.c,v 1.2 2003/11/10 18:22:44 tsi Exp $ */

#ifdef SHAPE

#include "win.h"


/*
 * External global variables
 */

extern HICON		g_hiconX;


/*
 * winSetShapeMultiWindow - See Porting Layer Definition - p. 42
 */

void
winSetShapeMultiWindow (WindowPtr pWin)
{
#if CYGMULTIWINDOW_DEBUG
  ErrorF ("winSetShapeMultiWindow - pWin: %08x\n", pWin);
#endif
  
  /* Call any wrapped SetShape function */
  if (winGetScreenPriv(pWin->drawable.pScreen)->SetShape)
    winGetScreenPriv(pWin->drawable.pScreen)->SetShape (pWin);
  
  /* Update the Windows window's shape */
  winReshapeMultiWindow (pWin);
  winUpdateRgnMultiWindow (pWin);

  return;
}


/*
 * winUpdateRgnMultiWindow - Local function to update a Windows window region
 */

void
winUpdateRgnMultiWindow (WindowPtr pWin)
{
  SetWindowRgn (winGetWindowPriv(pWin)->hWnd,
		winGetWindowPriv(pWin)->hRgn, TRUE);
}


/*
 * winReshapeMultiWindow - Computes the composite clipping region for a window
 */

void
winReshapeMultiWindow (WindowPtr pWin)
{
  int		nRects;
  RegionRec	rrNewShape;
  BoxPtr	pShape, pRects, pEnd;
  HRGN		hRgn, hRgnRect;
  winWindowPriv(pWin);

#if CYGDEBUG
  ErrorF ("winReshape ()\n");
#endif
  
  /* Bail if the window is the root window */
  if (pWin->parent == NULL)
    return;

  /* Bail if the window is not top level */
  if (pWin->parent->parent != NULL)
    return;

  /* Bail if Windows window handle is invalid */
  if (pWinPriv->hWnd == NULL)
    return;
  
  /* Free any existing window region stored in the window privates */
  if (pWinPriv->hRgn != NULL)
    {
      DeleteObject (pWinPriv->hRgn);
      pWinPriv->hRgn = NULL;
    }
  
  /* Bail if the window has no bounding region defined */
  if (!wBoundingShape (pWin))
    return;

  REGION_NULL(pScreen, &rrNewShape);
  REGION_COPY(pScreen, &rrNewShape, wBoundingShape(pWin));
  REGION_TRANSLATE(pScreen,
		   &rrNewShape,
		   pWin->borderWidth,
                   pWin->borderWidth);
  
  nRects = REGION_NUM_RECTS(&rrNewShape);
  pShape = REGION_RECTS(&rrNewShape);
  
  /* Don't do anything if there are no rectangles in the region */
  if (nRects > 0)
    {
      RECT			rcClient;
      RECT			rcWindow;
      int			iOffsetX, iOffsetY;
      
      /* Get client rectangle */
      if (!GetClientRect (pWinPriv->hWnd, &rcClient))
	{
	  ErrorF ("winReshape - GetClientRect failed, bailing: %d\n",
		  GetLastError ());
	  return;
	}

      /* Translate client rectangle coords to screen coords */
      /* NOTE: Only transforms top and left members */
      ClientToScreen (pWinPriv->hWnd, (LPPOINT) &rcClient);

      /* Get window rectangle */
      if (!GetWindowRect (pWinPriv->hWnd, &rcWindow))
	{
	  ErrorF ("winReshape - GetWindowRect failed, bailing: %d\n",
		  GetLastError ());
	  return;
	}

      /* Calculate offset from window upper-left to client upper-left */
      iOffsetX = rcClient.left - rcWindow.left;
      iOffsetY = rcClient.top - rcWindow.top;

      /* Create initial Windows region for title bar */
      /* FIXME: Mean, nasty, ugly hack!!! */
      hRgn = CreateRectRgn (0, 0, rcWindow.right, iOffsetY);
      if (hRgn == NULL)
	{
	  ErrorF ("winReshape - Initial CreateRectRgn (%d, %d, %d, %d) "
		  "failed: %d\n",
		  0, 0, rcWindow.right, iOffsetY, GetLastError ());
	}

      /* Loop through all rectangles in the X region */
      for (pRects = pShape, pEnd = pShape + nRects; pRects < pEnd; pRects++)
        {
	  /* Create a Windows region for the X rectangle */
	  hRgnRect = CreateRectRgn (pRects->x1 + iOffsetX - 1,
				    pRects->y1 + iOffsetY - 1,
				    pRects->x2 + iOffsetX - 1,
				    pRects->y2 + iOffsetY - 1);
	  if (hRgnRect == NULL)
	    {
	      ErrorF ("winReshape - Loop CreateRectRgn (%d, %d, %d, %d) "
		      "failed: %d\n"
		      "\tx1: %d x2: %d xOff: %d y1: %d y2: %d yOff: %d\n",
		      pRects->x1 + iOffsetX - 1,
		      pRects->y1 + iOffsetY - 1,
		      pRects->x2 + iOffsetX - 1,
		      pRects->y2 + iOffsetY - 1,
		      GetLastError (),
		      pRects->x1, pRects->x2, iOffsetX,
		      pRects->y1, pRects->y2, iOffsetY);
	    }

	  /* Merge the Windows region with the accumulated region */
	  if (CombineRgn (hRgn, hRgn, hRgnRect, RGN_OR) == ERROR)
	    {
	      ErrorF ("winReshape - CombineRgn () failed: %d\n",
		      GetLastError ());
	    }

	  /* Delete the temporary Windows region */
	  DeleteObject (hRgnRect);
        }
      
      /* Save a handle to the composite region in the window privates */
      pWinPriv->hRgn = hRgn;
    }

  REGION_UNINIT(pScreen, &rrNewShape);
  
  return;
}
#endif
