/*
 * rfbserver.c - deal with server-side of the RFB protocol.
 *
 * Modified for SharedAppVnc by Grant Wallace
 * Modified for XFree86 4.x by Alan Hourihane <alanh@fairlite.demon.co.uk>
 */

/*
 *  Copyright (C) 2000-2004 Constantin Kaplinsky.  All Rights Reserved.
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

/* Use ``#define CORBA'' to enable CORBA control interface */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "windowstr.h"
#include "rfb.h"
#include "input.h"
#include "mipointer.h"
#if XFREE86VNC
#include <micmap.h>
#endif
#ifdef CHROMIUM
#include "mivalidate.h"
#endif
#include "sprite.h"
#include "propertyst.h"
#include <Xatom.h>
#include <mi.h>

#ifdef CORBA
#include <vncserverctrl.h>
#endif

extern int GenerateVncConnectedEvent (int sock);
extern int GenerateVncDisconnectedEvent (int sock);
#ifdef CHROMIUM
extern int GenerateVncChromiumConnectedEvent (int sock);
struct CRWindowTable *windowTable = NULL;
#endif

extern Atom VNC_CONNECT;

rfbClientPtr rfbClientHead = NULL;
rfbClientPtr pointerClient = NULL;      /* Mutex for pointer events */

static rfbClientPtr rfbNewClient (ScreenPtr pScreen, int sock);
static void rfbProcessClientProtocolVersion (rfbClientPtr cl);
static void rfbProcessClientInitMessage (rfbClientPtr cl);
static void rfbSendInteractionCaps (rfbClientPtr cl);
static void rfbProcessClientNormalMessage (rfbClientPtr cl);
#ifdef SHAREDAPP
Bool rfbSendCopyRegion (rfbClientPtr cl, RegionPtr reg, int dx, int dy);
Bool rfbSendLastRectMarker (rfbClientPtr cl);
#else
static Bool rfbSendCopyRegion (rfbClientPtr cl, RegionPtr reg, int dx, int dy);
static Bool rfbSendLastRectMarker (rfbClientPtr cl);
#endif

static char *text = NULL;

void
rfbRootPropertyChange (ScreenPtr pScreen)
{
  PropertyPtr pProp;
  WindowPtr pWin = WindowTable[pScreen->myNum];

  pProp = wUserProps (pWin);

  
  while (pProp)
  {
  
    if ((pProp->propertyName == XA_CUT_BUFFER0) &&
        (pProp->type == XA_STRING) && (pProp->format == 8))
    {
      /* Ensure we don't keep re-sending cut buffer */

      if ((text && strncmp (text, pProp->data, pProp->size)) || !text)
        rfbGotXCutText (pProp->data, pProp->size);

      if (text)
        xfree (text);
      text = xalloc (1 + pProp->size);
      if (!text)
        return;
      memcpy (text, pProp->data, pProp->size);
      text[pProp->size] = '\0';

      return;
    }
    if ((pProp->propertyName == VNC_CONNECT) && (pProp->type == XA_STRING)
        && (pProp->format == 8) && (pProp->size > 0))
    {
      int i;
      rfbClientPtr cl;
      int port = 5500;
      char *host = (char *) Xalloc (pProp->size + 1);
      memcpy (host, pProp->data, pProp->size);
      host[pProp->size] = 0;
      for (i = 0; i < pProp->size; i++)
      {
        if (host[i] == ':')
        {
          port = atoi (&host[i + 1]);
          host[i] = 0;
        }
      }
      rfbLog("VNC_CONNECT %s:%d\n", host, port);

      cl = rfbReverseConnection (pScreen, host, port);


      
      ChangeWindowProperty (pWin,
                            pProp->propertyName, pProp->type,
                            pProp->format, PropModeReplace, 0, NULL, FALSE);
      

      free (host);
    }
    pProp = pProp->next;
  }
}

int
rfbBitsPerPixel (depth)
     int depth;
{
  if (depth == 1)
    return 1;
  else if (depth <= 8)
    return 8;
  else if (depth <= 16)
    return 16;
  else
    return 32;
}

void
rfbUserAllow (int sock, int accept)
{
  rfbClientPtr cl;

  for (cl = rfbClientHead; cl; cl = cl->next)
  {
    if (cl->sock == sock)
    {
      cl->userAccepted = accept;
    }
  }
}

/*
 * rfbNewClientConnection is called from sockets.c when a new connection
 * comes in.
 */

void
rfbNewClientConnection (ScreenPtr pScreen, int sock)
{
  rfbClientPtr cl;

  cl = rfbNewClient (pScreen, sock);

  GenerateVncConnectedEvent (sock);

#if XFREE86VNC
  /* Someone is connected - disable VT switching */
  xf86EnableVTSwitch (FALSE);
#endif

#ifdef CORBA
  if (cl != NULL)
  {
    rfbLog("CORBA - newConnection\n");
    newConnection (cl, (KEYBOARD_DEVICE | POINTER_DEVICE), 1, 1, 1);
  }
#endif
}


/*
 * rfbReverseConnection is called by the CORBA stuff to make an outward
 * connection to a "listening" RFB client.
 */

rfbClientPtr
rfbReverseConnection (ScreenPtr pScreen, char *host, int port)
{
  int sock;
  rfbClientPtr cl;

  if ((sock = rfbConnect (pScreen, host, port)) < 0)
    return (rfbClientPtr) NULL;

  
  cl = rfbNewClient (pScreen, sock);

  if (cl)
  {
    cl->reverseConnection = TRUE;
  }

#ifdef CORBA
  if (cl != NULL)
  {
    rfbLog("CORBA - newConnection\n");
    newConnection (cl, (KEYBOARD_DEVICE | POINTER_DEVICE), 1, 1, 1);
  }
#endif

  return cl;
  
}


#ifdef CHROMIUM
/*
 * rfbSetClip --
 * 	Generate expose event.
 * 	This function is overkill and should be cleaned up, but it
 * 	works for now.
 */

void
rfbSetClip (WindowPtr pWin, BOOL enable)
{
  ScreenPtr pScreen = pWin->drawable.pScreen;
  WindowPtr pChild;
  Bool WasViewable = (Bool) (pWin->viewable);
  Bool anyMarked = FALSE;
  RegionPtr pOldClip = NULL, bsExposed;
#ifdef DO_SAVE_UNDERS
  Bool dosave = FALSE;
#endif
  WindowPtr pLayerWin;
  BoxRec box;

  if (WasViewable)
  {
    for (pChild = pWin->firstChild; pChild; pChild = pChild->nextSib)
    {
      (void) (*pScreen->MarkOverlappedWindows) (pChild, pChild, &pLayerWin);
    }
    (*pScreen->MarkWindow) (pWin);
    anyMarked = TRUE;
    if (pWin->valdata)
    {
      if (HasBorder (pWin))
      {
        RegionPtr borderVisible;

        borderVisible = REGION_CREATE (pScreen, NullBox, 1);
        REGION_SUBTRACT (pScreen, borderVisible,
                         &pWin->borderClip, &pWin->winSize);
        pWin->valdata->before.borderVisible = borderVisible;
      }
      pWin->valdata->before.resized = TRUE;
    }
  }

  /*
   * Use REGION_BREAK to avoid optimizations in ValidateTree
   * that assume the root borderClip can't change well, normally
   * it doesn't...)
   */
  if (enable)
  {
    box.x1 = 0;
    box.y1 = 0;
    box.x2 = pScreen->width;
    box.y2 = pScreen->height;
    REGION_INIT (pScreen, &pWin->winSize, &box, 1);
    REGION_INIT (pScreen, &pWin->borderSize, &box, 1);
    if (WasViewable)
      REGION_RESET (pScreen, &pWin->borderClip, &box);
    pWin->drawable.width = pScreen->width;
    pWin->drawable.height = pScreen->height;
    REGION_BREAK (pWin->drawable.pScreen, &pWin->clipList);
  } else
  {
    REGION_EMPTY (pScreen, &pWin->borderClip);
    REGION_BREAK (pWin->drawable.pScreen, &pWin->clipList);
  }

  ResizeChildrenWinSize (pWin, 0, 0, 0, 0);

  if (WasViewable)
  {
    if (pWin->backStorage)
    {
      pOldClip = REGION_CREATE (pScreen, NullBox, 1);
      REGION_COPY (pScreen, pOldClip, &pWin->clipList);
    }

    if (pWin->firstChild)
    {
      anyMarked |= (*pScreen->MarkOverlappedWindows) (pWin->firstChild,
                                                      pWin->firstChild,
                                                      (WindowPtr *) NULL);
    } else
    {
      (*pScreen->MarkWindow) (pWin);
      anyMarked = TRUE;
    }

#ifdef DO_SAVE_UNDERS
    if (DO_SAVE_UNDERS (pWin))
    {
      dosave = (*pScreen->ChangeSaveUnder) (pLayerWin, pLayerWin);
    }
#endif /* DO_SAVE_UNDERS */

    if (anyMarked)
      (*pScreen->ValidateTree) (pWin, NullWindow, VTOther);
  }

  if (pWin->backStorage && ((pWin->backingStore == Always) || WasViewable))
  {
    if (!WasViewable)
      pOldClip = &pWin->clipList;       /* a convenient empty region */
    bsExposed = (*pScreen->TranslateBackingStore)
      (pWin, 0, 0, pOldClip, pWin->drawable.x, pWin->drawable.y);
    if (WasViewable)
      REGION_DESTROY (pScreen, pOldClip);
    if (bsExposed)
    {
      RegionPtr valExposed = NullRegion;

      if (pWin->valdata)
        valExposed = &pWin->valdata->after.exposed;
      (*pScreen->WindowExposures) (pWin, valExposed, bsExposed);
      if (valExposed)
        REGION_EMPTY (pScreen, valExposed);
      REGION_DESTROY (pScreen, bsExposed);
    }
  }
  if (WasViewable)
  {
    if (anyMarked)
      (*pScreen->HandleExposures) (pWin);
#ifdef DO_SAVE_UNDERS
    if (dosave)
      (*pScreen->PostChangeSaveUnder) (pLayerWin, pLayerWin);
#endif /* DO_SAVE_UNDERS */
    if (anyMarked && pScreen->PostValidateTree)
      (*pScreen->PostValidateTree) (pWin, NullWindow, VTOther);
  }
  if (pWin->realized)
    WindowsRestructured ();
  FlushAllOutput ();
}
#endif /* CHROMIUM */

/*
 * rfbNewClient is called when a new connection has been made by whatever
 * means.
 */

static rfbClientPtr
rfbNewClient (ScreenPtr pScreen, int sock)
{
  rfbProtocolVersionMsg pv;
  rfbClientPtr cl;
  BoxRec box;
  struct sockaddr_in addr;
  SOCKLEN_T addrlen = sizeof (struct sockaddr_in);
  VNCSCREENPTR (pScreen);
  int i;

  if (rfbClientHead == NULL)
  {
    /* no other clients - make sure we don't think any keys are pressed */
    KbdReleaseAllKeys ();
  } else
  {
    rfbLog ("  (other clients\n");
    for (cl = rfbClientHead; cl; cl = cl->next)
    {
      rfbLog (" %s\n", cl->host);
    }
    rfbLog (")\n");
  }

  cl = (rfbClientPtr) xalloc (sizeof (rfbClientRec));

#ifdef CHROMIUM
  cl->chromium_port = 0;        /* no GL application on this port, yet */
#endif
  cl->userAccepted = 0;         /* user hasn't even approached this yet .... */
  cl->sock = sock;
  getpeername (sock, (struct sockaddr *) &addr, &addrlen);
  cl->host = strdup (inet_ntoa (addr.sin_addr));
  cl->login = NULL;

  /* Dispatch client input to rfbProcessClientProtocolVersion(). */
  cl->state = RFB_PROTOCOL_VERSION;

  cl->viewOnly = FALSE;
  cl->reverseConnection = FALSE;
  cl->readyForSetColourMapEntries = FALSE;
  cl->useCopyRect = FALSE;
  cl->preferredEncoding = rfbEncodingRaw;
  cl->correMaxWidth = 48;
  cl->correMaxHeight = 48;
  cl->pScreen = pScreen;

  REGION_NULL (pScreen, &cl->copyRegion);
  cl->copyDX = 0;
  cl->copyDY = 0;

  box.x1 = box.y1 = 0;
  box.x2 = pVNC->width;
  box.y2 = pVNC->height;
  REGION_INIT (pScreen, &cl->modifiedRegion, &box, 0);

  REGION_NULL (pScreen, &cl->requestedRegion);

  cl->deferredUpdateScheduled = FALSE;
  cl->deferredUpdateTimer = NULL;

  cl->format = pVNC->rfbServerFormat;
  cl->translateFn = rfbTranslateNone;
  cl->translateLookupTable = NULL;

  cl->tightCompressLevel = TIGHT_DEFAULT_COMPRESSION;
  cl->tightQualityLevel = -1;
  for (i = 0; i < 4; i++)
    cl->zsActive[i] = FALSE;

  cl->enableCursorShapeUpdates = FALSE;
  cl->enableCursorPosUpdates = FALSE;
  cl->enableLastRectEncoding = FALSE;
#ifdef CHROMIUM
  cl->enableChromiumEncoding = FALSE;
#endif

#ifdef SHAREDAPP
  cl->supportsSharedAppEncoding = FALSE;
  cl->multicursor = 0;
#endif

  cl->next = rfbClientHead;
  rfbClientHead = cl;

  rfbResetStats (cl);

  cl->compStreamInited = FALSE;
  cl->compStream.total_in = 0;
  cl->compStream.total_out = 0;
  cl->compStream.zalloc = Z_NULL;
  cl->compStream.zfree = Z_NULL;
  cl->compStream.opaque = Z_NULL;

  cl->zlibCompressLevel = 5;

  sprintf (pv, rfbProtocolVersionFormat, rfbProtocolMajorVersion,
           rfbProtocolMinorVersion);

  if (WriteExact (sock, pv, sz_rfbProtocolVersionMsg) < 0)
  {
    rfbLogPerror ("rfbNewClient: write");
    rfbCloseSock (pScreen, sock);
    return NULL;
  }

  rfbLog("Sent Protocol Version\n");
  return cl;
}


/*
 * rfbClientConnectionGone is called from sockets.c just after a connection
 * has gone away.
 */

void
rfbClientConnectionGone (sock)
     int sock;
{
  rfbClientPtr cl, prev;
  ScreenPtr pScreen;
  int i;
#if XFREE86VNC
  int allowvt = TRUE;
#endif

  for (prev = NULL, cl = rfbClientHead; cl; prev = cl, cl = cl->next)
  {
    if (sock == cl->sock)
      break;
  }

  if (!cl)
  {
    rfbLog ("rfbClientConnectionGone: unknown socket %d\n", sock);
    return;
  }

  if (cl->login != NULL)
  {
    rfbLog ("Client %s (%s) gone\n", cl->login, cl->host);
    free (cl->login);
  } else
  {
    rfbLog ("Client %s gone\n", cl->host);
  }
  free (cl->host);

  /* Release the compression state structures if any. */
  if (cl->compStreamInited == TRUE)
  {
    deflateEnd (&(cl->compStream));
  }

  for (i = 0; i < 4; i++)
  {
    if (cl->zsActive[i])
      deflateEnd (&cl->zsStruct[i]);
  }

  if (pointerClient == cl)
    pointerClient = NULL;

#ifdef CORBA
  destroyConnection (cl);
#endif

  if (prev)
    prev->next = cl->next;
  else
    rfbClientHead = cl->next;

  REGION_UNINIT (cl->pScreen, &cl->copyRegion);
  REGION_UNINIT (cl->pScreen, &cl->modifiedRegion);
  TimerFree (cl->deferredUpdateTimer);

  rfbPrintStats (cl);

  if (cl->translateLookupTable)
    free (cl->translateLookupTable);

  pScreen = cl->pScreen;

  xfree (cl);

  GenerateVncDisconnectedEvent (sock);

#if XFREE86VNC
  for (cl = rfbClientHead; cl; cl = cl->next)
  {
    /* still someone connected */
    allowvt = FALSE;
  }

  xf86EnableVTSwitch (allowvt);


   
#endif
}


/*
 * rfbProcessClientMessage is called when there is data to read from a client.
 */

void
rfbProcessClientMessage (ScreenPtr pScreen, int sock)
{
  rfbClientPtr cl;

  for (cl = rfbClientHead; cl; cl = cl->next)
  {
    if (sock == cl->sock)
      break;
  }

  if (!cl)
  {
    rfbLog ("rfbProcessClientMessage: unknown socket %d\n", sock);
    rfbCloseSock (pScreen, sock);
    return;
  }
#ifdef CORBA
  if (isClosePending (cl))
  {
    rfbLog ("Closing connection to client %s\n", cl->host);
    rfbCloseSock (pScreen, sock);
    return;
  }
#endif

  switch (cl->state)
  {
  case RFB_PROTOCOL_VERSION:
    rfbLog ("rfbProcessClientMessage: Protocol Verion\n", sock);
    rfbProcessClientProtocolVersion (cl);
    break;
  case RFB_SECURITY_TYPE:      /* protocol 3.7 */
    rfbLog ("rfbProcessClientMessage: Security Type\n", sock);
    rfbProcessClientSecurityType (cl);
    break;
  case RFB_TUNNELING_TYPE:     /* protocol 3.7t */
    rfbLog ("rfbProcessClientMessage: Tunneling Type\n", sock);
    rfbProcessClientTunnelingType (cl);
    break;
  case RFB_AUTH_TYPE:          /* protocol 3.7t */
    rfbLog ("rfbProcessClientMessage: Authentication Type\n", sock);
    rfbProcessClientAuthType (cl);
    break;
  case RFB_AUTHENTICATION:
    rfbLog ("rfbProcessClientMessage: Authentication\n", sock);
    rfbVncAuthProcessResponse (cl);
    break;
  case RFB_INITIALISATION:
    rfbLog ("rfbProcessClientMessage: Initialization\n", sock);
    rfbProcessClientInitMessage (cl);
    break;
  default:
    rfbProcessClientNormalMessage (cl);
  }
}


/*
 * rfbProcessClientProtocolVersion is called when the client sends its
 * protocol version.
 */

static void
rfbProcessClientProtocolVersion (cl)
     rfbClientPtr cl;
{
  rfbProtocolVersionMsg pv;
  int n, major, minor;
  Bool mismatch;

  if ((n = ReadExact (cl->sock, pv, sz_rfbProtocolVersionMsg)) <= 0)
  {
    if (n == 0)
      rfbLog ("rfbProcessClientProtocolVersion: client gone\n");
    else
      rfbLogPerror ("rfbProcessClientProtocolVersion: read");
    rfbCloseSock (cl->pScreen, cl->sock);
    return;
  }

  pv[sz_rfbProtocolVersionMsg] = 0;
  if (sscanf (pv, rfbProtocolVersionFormat, &major, &minor) != 2)
  {
    rfbLog ("rfbProcessClientProtocolVersion: not a valid RFB client\n");
    rfbCloseSock (cl->pScreen, cl->sock);
    return;
  }
  rfbLog ("Using protocol version %d.%d\n", major, minor);

  if (major != rfbProtocolMajorVersion)
  {
    rfbLog ("RFB protocol version mismatch - server %d.%d, client %d.%d\n",
            rfbProtocolMajorVersion, rfbProtocolMinorVersion, major, minor);
    rfbCloseSock (cl->pScreen, cl->sock);
    return;
  }

  /* Always use one of the two standard versions of the RFB protocol. */
  cl->protocol_minor_ver = minor;
  if (minor > rfbProtocolMinorVersion)
  {
    cl->protocol_minor_ver = rfbProtocolMinorVersion;
  } else if (minor < rfbProtocolMinorVersion)
  {
    cl->protocol_minor_ver = rfbProtocolFallbackMinorVersion;
  }
  if (minor != rfbProtocolMinorVersion &&
      minor != rfbProtocolFallbackMinorVersion)
  {
    rfbLog ("Non-standard protocol version %d.%d, using %d.%d instead\n",
            major, minor, rfbProtocolMajorVersion, cl->protocol_minor_ver);
  }

  /* TightVNC protocol extensions are not enabled yet. */
  cl->protocol_tightvnc = FALSE;

  rfbAuthNewClient (cl);
}

/*
 * rfbClientConnFailed is called when a client connection has failed
 * before the authentication stage.
 */

void
rfbClientConnFailed (cl, reason)
     rfbClientPtr cl;
     char *reason;
{
  int headerLen, reasonLen;
  char buf[8];

  headerLen = (cl->protocol_minor_ver >= 7) ? 1 : 4;
  reasonLen = strlen (reason);
  ((CARD32 *) buf)[0] = 0;
  ((CARD32 *) buf)[1] = Swap32IfLE (reasonLen);

  if (WriteExact (cl->sock, buf, headerLen) < 0 ||
      WriteExact (cl->sock, buf + 4, 4) < 0 ||
      WriteExact (cl->sock, reason, reasonLen) < 0)
  {
    rfbLogPerror ("rfbClientConnFailed: write");
  }

  rfbLog("ClientConnFailed\n");
  rfbCloseSock (cl->pScreen, cl->sock);
}


/*
 * rfbProcessClientInitMessage is called when the client sends its
 * initialisation message.
 */

static void
rfbProcessClientInitMessage (cl)
     rfbClientPtr cl;
{
  VNCSCREENPTR (cl->pScreen);
  rfbClientInitMsg ci;
  char buf[256];
  rfbServerInitMsg *si = (rfbServerInitMsg *) buf;
  struct passwd *user;
  int len, n;
  rfbClientPtr otherCl, nextCl;

  if ((n = ReadExact (cl->sock, (char *) &ci, sz_rfbClientInitMsg)) <= 0)
  {
    if (n == 0)
      rfbLog ("rfbProcessClientInitMessage: client gone\n");
    else
      rfbLogPerror ("rfbProcessClientInitMessage: read");
    rfbCloseSock (cl->pScreen, cl->sock);
    return;
  }

  si->framebufferWidth = Swap16IfLE (pVNC->width);
  si->framebufferHeight = Swap16IfLE (pVNC->height);
  si->format = pVNC->rfbServerFormat;
  si->format.redMax = Swap16IfLE (si->format.redMax);
  si->format.greenMax = Swap16IfLE (si->format.greenMax);
  si->format.blueMax = Swap16IfLE (si->format.blueMax);

  user = getpwuid (getuid ());

  if (strlen (desktopName) > 128)       /* sanity check on desktop name len */
    desktopName[128] = 0;

  if (user)
  {
    sprintf (buf + sz_rfbServerInitMsg, "%s's %s desktop (%s:%s)",
             user->pw_name, desktopName, rfbThisHost, display);
  } else
  {
    sprintf (buf + sz_rfbServerInitMsg, "%s desktop (%s:%s)",
             desktopName, rfbThisHost, display);
  }
  len = strlen (buf + sz_rfbServerInitMsg);
  si->nameLength = Swap32IfLE (len);

  if (WriteExact (cl->sock, buf, sz_rfbServerInitMsg + len) < 0)
  {
    rfbLogPerror ("rfbProcessClientInitMessage: write");
    rfbCloseSock (cl->pScreen, cl->sock);
    return;
  }

  if (cl->protocol_tightvnc)
    rfbSendInteractionCaps (cl);        /* protocol 3.7t */

  /* Dispatch client input to rfbProcessClientNormalMessage(). */
  cl->state = RFB_NORMAL;

  if (!cl->reverseConnection &&
      (pVNC->rfbNeverShared || (!pVNC->rfbAlwaysShared && !ci.shared)))
  {

    if (pVNC->rfbDontDisconnect)
    {
      for (otherCl = rfbClientHead; otherCl; otherCl = otherCl->next)
      {
        if ((otherCl != cl) && (otherCl->state == RFB_NORMAL))
        {
          rfbLog ("-dontdisconnect: Not shared & existing client\n");
          rfbLog ("  refusing new client %s\n", cl->host);
          rfbCloseSock (cl->pScreen, cl->sock);
          return;
        }
      }
    } else
    {
      for (otherCl = rfbClientHead; otherCl; otherCl = nextCl)
      {
        nextCl = otherCl->next;
        if ((otherCl != cl) && (otherCl->state == RFB_NORMAL))
        {
          rfbLog ("Not shared - closing connection to client %s\n",
                  otherCl->host);
          rfbCloseSock (otherCl->pScreen, otherCl->sock);
        }
      }
    }
  }
}


/*
 * rfbSendInteractionCaps is called after sending the server
 * initialisation message, only if the protocol version is 3.130.
 * In this function, we send the lists of supported protocol messages
 * and encodings.
 */

/* Update these constants on changing capability lists below! */
#define N_SMSG_CAPS  0
#define N_CMSG_CAPS  0
#define N_ENC_CAPS  12

void
rfbSendInteractionCaps (cl)
     rfbClientPtr cl;
{
  rfbInteractionCapsMsg intr_caps;
  rfbCapabilityInfo enc_list[N_ENC_CAPS];
  int i;

  /* Fill in the header structure sent prior to capability lists. */
  intr_caps.nServerMessageTypes = Swap16IfLE (N_SMSG_CAPS);
  intr_caps.nClientMessageTypes = Swap16IfLE (N_CMSG_CAPS);
  intr_caps.nEncodingTypes = Swap16IfLE (N_ENC_CAPS);
  intr_caps.pad = 0;

  /* Supported server->client message types. */
  /* For future file transfer support:
     i = 0;
     SetCapInfo(&smsg_list[i++], rfbFileListData,           rfbTightVncVendor);
     SetCapInfo(&smsg_list[i++], rfbFileDownloadData,       rfbTightVncVendor);
     SetCapInfo(&smsg_list[i++], rfbFileUploadCancel,       rfbTightVncVendor);
     SetCapInfo(&smsg_list[i++], rfbFileDownloadFailed,     rfbTightVncVendor);
     if (i != N_SMSG_CAPS) {
     rfbLog("rfbSendInteractionCaps: assertion failed, i != N_SMSG_CAPS\n");
     rfbCloseSock(cl->pScreen, cl->sock);
     return;
     }
   */

  /* Supported client->server message types. */
  /* For future file transfer support:
     i = 0;
     SetCapInfo(&cmsg_list[i++], rfbFileListRequest,        rfbTightVncVendor);
     SetCapInfo(&cmsg_list[i++], rfbFileDownloadRequest,    rfbTightVncVendor);
     SetCapInfo(&cmsg_list[i++], rfbFileUploadRequest,      rfbTightVncVendor);
     SetCapInfo(&cmsg_list[i++], rfbFileUploadData,         rfbTightVncVendor);
     SetCapInfo(&cmsg_list[i++], rfbFileDownloadCancel,     rfbTightVncVendor);
     SetCapInfo(&cmsg_list[i++], rfbFileUploadFailed,       rfbTightVncVendor);
     if (i != N_CMSG_CAPS) {
     rfbLog("rfbSendInteractionCaps: assertion failed, i != N_CMSG_CAPS\n");
     rfbCloseSock(cl->pScreen, cl->sock);
     return;
     }
   */

  /* Encoding types. */
  i = 0;
  SetCapInfo (&enc_list[i++], rfbEncodingCopyRect, rfbStandardVendor);
  SetCapInfo (&enc_list[i++], rfbEncodingRRE, rfbStandardVendor);
  SetCapInfo (&enc_list[i++], rfbEncodingCoRRE, rfbStandardVendor);
  SetCapInfo (&enc_list[i++], rfbEncodingHextile, rfbStandardVendor);
  SetCapInfo (&enc_list[i++], rfbEncodingZlib, rfbTridiaVncVendor);
  SetCapInfo (&enc_list[i++], rfbEncodingTight, rfbTightVncVendor);
  SetCapInfo (&enc_list[i++], rfbEncodingCompressLevel0, rfbTightVncVendor);
  SetCapInfo (&enc_list[i++], rfbEncodingQualityLevel0, rfbTightVncVendor);
  SetCapInfo (&enc_list[i++], rfbEncodingXCursor, rfbTightVncVendor);
  SetCapInfo (&enc_list[i++], rfbEncodingRichCursor, rfbTightVncVendor);
  SetCapInfo (&enc_list[i++], rfbEncodingPointerPos, rfbTightVncVendor);
  SetCapInfo (&enc_list[i++], rfbEncodingLastRect, rfbTightVncVendor);
  if (i != N_ENC_CAPS)
  {
    rfbLog ("rfbSendInteractionCaps: assertion failed, i != N_ENC_CAPS\n");
    rfbCloseSock (cl->pScreen, cl->sock);
    return;
  }

  /* Send header and capability lists */
  if (WriteExact (cl->sock, (char *) &intr_caps,
                  sz_rfbInteractionCapsMsg) < 0 ||
      WriteExact (cl->sock, (char *) &enc_list[0],
                  sz_rfbCapabilityInfo * N_ENC_CAPS) < 0)
  {
    rfbLogPerror ("rfbSendInteractionCaps: write");
    rfbCloseSock (cl->pScreen, cl->sock);
    return;
  }

  /* Dispatch client input to rfbProcessClientNormalMessage(). */
  cl->state = RFB_NORMAL;
}


/*
 * rfbProcessClientNormalMessage is called when the client has sent a normal
 * protocol message.
 */

static void
rfbProcessClientNormalMessage (cl)
     rfbClientPtr cl;
{
  VNCSCREENPTR (cl->pScreen);
  int n;
  rfbClientToServerMsg msg;
  char *str;

  if (pVNC->rfbUserAccept)
  {
    /* 
     * We've asked for another level of user authentication
     * If the user has not validated this connected, don't
     * process it.
     */
    /*
     * NOTE: we do it here, so the vncviewer knows it's
     * connected, but waiting for the first screen update
     */
    if (cl->userAccepted == VNC_USER_UNDEFINED)
      return;
    if (cl->userAccepted == VNC_USER_DISCONNECT)
    {
      rfbLog("VNC_USER_DISCONNECT\n");
      rfbCloseSock (cl->pScreen, cl->sock);
      return;
    }
  }

  if ((n = ReadExact (cl->sock, (char *) &msg, 1)) <= 0)
  {
    if (n != 0)
      rfbLogPerror ("rfbProcessClientNormalMessage: read");
    rfbCloseSock (cl->pScreen, cl->sock);
    return;
  }

  switch (msg.type)
  {

  case rfbSetPixelFormat:

    if ((n = ReadExact (cl->sock, ((char *) &msg) + 1,
                        sz_rfbSetPixelFormatMsg - 1)) <= 0)
    {
      if (n != 0)
        rfbLogPerror ("rfbProcessClientNormalMessage: read");
      rfbCloseSock (cl->pScreen, cl->sock);
      return;
    }

    cl->format.bitsPerPixel = msg.spf.format.bitsPerPixel;
    cl->format.depth = msg.spf.format.depth;
    cl->format.bigEndian = (msg.spf.format.bigEndian ? 1 : 0);
    cl->format.trueColour = (msg.spf.format.trueColour ? 1 : 0);
    cl->format.redMax = Swap16IfLE (msg.spf.format.redMax);
    cl->format.greenMax = Swap16IfLE (msg.spf.format.greenMax);
    cl->format.blueMax = Swap16IfLE (msg.spf.format.blueMax);
    cl->format.redShift = msg.spf.format.redShift;
    cl->format.greenShift = msg.spf.format.greenShift;
    cl->format.blueShift = msg.spf.format.blueShift;

    cl->readyForSetColourMapEntries = TRUE;

    rfbSetTranslateFunction (cl);
    return;


  case rfbFixColourMapEntries:
    if ((n = ReadExact (cl->sock, ((char *) &msg) + 1,
                        sz_rfbFixColourMapEntriesMsg - 1)) <= 0)
    {
      if (n != 0)
        rfbLogPerror ("rfbProcessClientNormalMessage: read");
      rfbCloseSock (cl->pScreen, cl->sock);
      return;
    }
    rfbLog ("rfbProcessClientNormalMessage: %s",
            "FixColourMapEntries unsupported\n");
    rfbCloseSock (cl->pScreen, cl->sock);
    return;


  case rfbSetEncodings:
  {
    int i;
    CARD32 enc;

    if ((n = ReadExact (cl->sock, ((char *) &msg) + 1,
                        sz_rfbSetEncodingsMsg - 1)) <= 0)
    {
      if (n != 0)
        rfbLogPerror ("rfbProcessClientNormalMessage: read");
      rfbCloseSock (cl->pScreen, cl->sock);
      return;
    }

    msg.se.nEncodings = Swap16IfLE (msg.se.nEncodings);

    cl->preferredEncoding = -1;
    cl->useCopyRect = FALSE;
    cl->enableCursorShapeUpdates = FALSE;
    cl->enableCursorPosUpdates = FALSE;
    cl->enableLastRectEncoding = FALSE;
#ifdef CHROMIUM
    cl->enableChromiumEncoding = FALSE;
#endif
#ifdef SHAREDAPP
    cl->supportsSharedAppEncoding = FALSE;
#endif

    cl->tightCompressLevel = TIGHT_DEFAULT_COMPRESSION;
    cl->tightQualityLevel = -1;

    for (i = 0; i < msg.se.nEncodings; i++)
    {
      if ((n = ReadExact (cl->sock, (char *) &enc, 4)) <= 0)
      {
        if (n != 0)
          rfbLogPerror ("rfbProcessClientNormalMessage: read");
        rfbCloseSock (cl->pScreen, cl->sock);
        return;
      }
      enc = Swap32IfLE (enc);

      switch (enc)
      {

      case rfbEncodingCopyRect:
        cl->useCopyRect = TRUE;
        rfbLog ("Using copyrect encoding for client %s\n", cl->host);
        break;
      case rfbEncodingRaw:
        if (cl->preferredEncoding == -1)
        {
          cl->preferredEncoding = enc;
          rfbLog ("Using raw encoding for client %s\n", cl->host);
        }
        break;
      case rfbEncodingRRE:
        if (cl->preferredEncoding == -1)
        {
          cl->preferredEncoding = enc;
          rfbLog ("Using rre encoding for client %s\n", cl->host);
        }
        break;
      case rfbEncodingCoRRE:
        if (cl->preferredEncoding == -1)
        {
          cl->preferredEncoding = enc;
          rfbLog ("Using CoRRE encoding for client %s\n", cl->host);
        }
        break;
      case rfbEncodingHextile:
        if (cl->preferredEncoding == -1)
        {
          cl->preferredEncoding = enc;
          rfbLog ("Using hextile encoding for client %s\n", cl->host);
        }
        break;
      case rfbEncodingZlib:
        if (cl->preferredEncoding == -1)
        {
          cl->preferredEncoding = enc;
          rfbLog ("Using zlib encoding for client %s\n", cl->host);
        }
        break;
      case rfbEncodingTight:
        if (cl->preferredEncoding == -1)
        {
          cl->preferredEncoding = enc;
          rfbLog ("Using tight encoding for client %s\n", cl->host);
        }
        break;
      case rfbEncodingXCursor:
        rfbLog ("Enabling X-style cursor updates for client %s\n", cl->host);
        cl->enableCursorShapeUpdates = TRUE;
        cl->useRichCursorEncoding = FALSE;
        cl->cursorWasChanged = TRUE;
        break;
      case rfbEncodingRichCursor:
        if (!cl->enableCursorShapeUpdates)
        {
          rfbLog ("Enabling full-color cursor updates for client "
                  "%s\n", cl->host);
          cl->enableCursorShapeUpdates = TRUE;
          cl->useRichCursorEncoding = TRUE;
          cl->cursorWasChanged = TRUE;
        }
        break;
      case rfbEncodingPointerPos:
        if (!cl->enableCursorPosUpdates)
        {
          rfbLog ("Enabling cursor position updates for client %s\n",
                  cl->host);
          cl->enableCursorPosUpdates = TRUE;
          cl->cursorWasMoved = TRUE;
          cl->cursorX = -1;
          cl->cursorY = -1;
        }
        break;
      case rfbEncodingLastRect:
        if (!cl->enableLastRectEncoding)
        {
          rfbLog ("Enabling LastRect protocol extension for client "
                  "%s\n", cl->host);
          cl->enableLastRectEncoding = TRUE;
        }
        break;
#ifdef CHROMIUM
      case rfbEncodingChromium:
        if (!cl->enableChromiumEncoding)
        {
          WindowPtr pWin = WindowTable[cl->pScreen->myNum];
          rfbLog ("Enabling Chromium protocol extension for client "
                  "%s\n", cl->host);
          cl->enableChromiumEncoding = TRUE;
          /* Now generate an event, so we can start our GL app */
          GenerateVncChromiumConnectedEvent (cl->sock);
          /* Generate exposures for all windows */
          rfbSetClip (pWin, 1);
        }
        break;
#endif
#ifdef SHAREDAPP
      case rfbEncodingSharedApp:
        rfbLog("Client supports ShareApp Encoding\n");
        cl->supportsSharedAppEncoding = TRUE;
        break;
#endif
      default:
        if (enc >= (CARD32) rfbEncodingCompressLevel0 &&
            enc <= (CARD32) rfbEncodingCompressLevel9)
        {
          cl->zlibCompressLevel = enc & 0x0F;
          cl->tightCompressLevel = enc & 0x0F;
          rfbLog ("Using compression level %d for client %s\n",
                  cl->tightCompressLevel, cl->host);
        } else if (enc >= (CARD32) rfbEncodingQualityLevel0 &&
                   enc <= (CARD32) rfbEncodingQualityLevel9)
        {
          cl->tightQualityLevel = enc & 0x0F;
          rfbLog ("Using image quality level %d for client %s\n",
                  cl->tightQualityLevel, cl->host);
        } else
        {
          rfbLog ("rfbSetEncodings: ignoring unknown "
                  "encoding %d\n", (int) enc);
        }
      }
    }

    if (cl->preferredEncoding == -1)
    {
      cl->preferredEncoding = rfbEncodingRaw;
      rfbLog ("No encoding specified - using raw encoding for client %s\n",
              cl->host);
    }

    if (cl->enableCursorPosUpdates && !cl->enableCursorShapeUpdates)
    {
      rfbLog ("Disabling cursor position updates for client %s\n", cl->host);
      cl->enableCursorPosUpdates = FALSE;
    }
#if XFREE86VNC
    /*
     * With XFree86 and the hardware cursor's we need to put up the
     * cursor again, and if we've detected a cursor shapeless client
     * we need to disable hardware cursors.
     */
    if (!cl->enableCursorShapeUpdates)
      pVNC->SWCursor = (Bool *) TRUE;
    else
      pVNC->SWCursor = (Bool *) FALSE;

    {
      int x, y;
      miPointerPosition (&x, &y);
      (*pVNC->spriteFuncs->SetCursor) (cl->pScreen, pVNC->pCurs, x, y);
    }
#endif

    return;
  }


  case rfbFramebufferUpdateRequest:
  {
    RegionRec tmpRegion;
    BoxRec box;

#ifdef CORBA
    addCapability (cl, DISPLAY_DEVICE);
#endif

    if ((n = ReadExact (cl->sock, ((char *) &msg) + 1,
                        sz_rfbFramebufferUpdateRequestMsg - 1)) <= 0)
    {
      if (n != 0)
        rfbLogPerror ("rfbProcessClientNormalMessage: read");
      rfbCloseSock (cl->pScreen, cl->sock);
      return;
    }

    box.x1 = Swap16IfLE (msg.fur.x);
    box.y1 = Swap16IfLE (msg.fur.y);
    box.x2 = box.x1 + Swap16IfLE (msg.fur.w);
    box.y2 = box.y1 + Swap16IfLE (msg.fur.h);
    SAFE_REGION_INIT (cl->pScreen, &tmpRegion, &box, 0);

    REGION_UNION (cl->pScreen, &cl->requestedRegion, &cl->requestedRegion,
                  &tmpRegion);

    if (!cl->readyForSetColourMapEntries)
    {
      /* client hasn't sent a SetPixelFormat so is using server's */
      cl->readyForSetColourMapEntries = TRUE;
      if (!cl->format.trueColour)
      {
        if (!rfbSetClientColourMap (cl, 0, 0))
        {
          REGION_UNINIT (cl->pScreen, &tmpRegion);
          return;
        }
      }
    }

    if (!msg.fur.incremental)
    {
      REGION_UNION (cl->pScreen, &cl->modifiedRegion, &cl->modifiedRegion,
                    &tmpRegion);
      REGION_SUBTRACT (cl->pScreen, &cl->copyRegion, &cl->copyRegion,
                       &tmpRegion);
    }

    if (FB_UPDATE_PENDING (cl))
    {
      rfbSendFramebufferUpdate (cl->pScreen, cl);
    }

    REGION_UNINIT (cl->pScreen, &tmpRegion);
    return;
  }

  case rfbKeyEvent:

    cl->rfbKeyEventsRcvd++;

    if ((n = ReadExact (cl->sock, ((char *) &msg) + 1,
                        sz_rfbKeyEventMsg - 1)) <= 0)
    {
      if (n != 0)
        rfbLogPerror ("rfbProcessClientNormalMessage: read");
      rfbCloseSock (cl->pScreen, cl->sock);
      return;
    }
#ifdef CORBA
    addCapability (cl, KEYBOARD_DEVICE);

    if (!isKeyboardEnabled (cl))
      return;
#endif
    if (!pVNC->rfbViewOnly && !cl->viewOnly)
    {
      KbdAddEvent (msg.ke.down, (KeySym) Swap32IfLE (msg.ke.key), cl);
    }
    return;


  case rfbPointerEvent:

    cl->rfbPointerEventsRcvd++;

    if ((n = ReadExact (cl->sock, ((char *) &msg) + 1,
                        sz_rfbPointerEventMsg - 1)) <= 0)
    {
      if (n != 0)
        rfbLogPerror ("rfbProcessClientNormalMessage: read");
      rfbCloseSock (cl->pScreen, cl->sock);
      return;
    }
#ifdef CORBA
    addCapability (cl, POINTER_DEVICE);

    if (!isPointerEnabled (cl))
      return;
#endif

    if (pointerClient && (pointerClient != cl))
      return;

    if (msg.pe.buttonMask == 0)
      pointerClient = NULL;
    else
      pointerClient = cl;

    if (!pVNC->rfbViewOnly && !cl->viewOnly)
    {
#ifdef SHAREDAPP
      if (!pVNC->sharedApp.bEnabled || sharedapp_CheckPointer(cl, &msg.pe))
#endif
      {
        cl->cursorX = (int) Swap16IfLE (msg.pe.x);
        cl->cursorY = (int) Swap16IfLE (msg.pe.y);
        PtrAddEvent (msg.pe.buttonMask, cl->cursorX, cl->cursorY, cl);
      }
    }
    return;


  case rfbClientCutText:

    if ((n = ReadExact (cl->sock, ((char *) &msg) + 1,
                        sz_rfbClientCutTextMsg - 1)) <= 0)
    {
      if (n != 0)
        rfbLogPerror ("rfbProcessClientNormalMessage: read");
      rfbCloseSock (cl->pScreen, cl->sock);
      return;
    }

    msg.cct.length = Swap32IfLE (msg.cct.length);

    str = (char *) xalloc (msg.cct.length);

    if ((n = ReadExact (cl->sock, str, msg.cct.length)) <= 0)
    {
      if (n != 0)
        rfbLogPerror ("rfbProcessClientNormalMessage: read");
      xfree (str);
      rfbCloseSock (cl->pScreen, cl->sock);
      return;
    }

    /* NOTE: We do not accept cut text from a view-only client */
    if (!cl->viewOnly)
      rfbSetXCutText (str, msg.cct.length);

    xfree (str);
    return;

#ifdef CHROMIUM
  case rfbChromiumStop:

    if ((n = ReadExact (cl->sock, ((char *) &msg) + 1,
                        sz_rfbChromiumStopMsg - 1)) <= 0)
    {
      if (n != 0)
        rfbLogPerror ("rfbProcessClientNormalMessage: read");
      rfbCloseSock (cl->pScreen, cl->sock);
      return;
    }

    /* would we use msg.csd.port ??? */

    cl->chromium_port = 0;

    /* tear down window information */
    {
      CRWindowTable *wt, *nextWt = NULL;

      for (wt = windowTable; wt; wt = nextWt)
      {
        nextWt = wt->next;
        xfree (wt);
      }

      windowTable = NULL;
    }

    return;

  case rfbChromiumExpose:

    if ((n = ReadExact (cl->sock, ((char *) &msg) + 1,
                        sz_rfbChromiumExposeMsg - 1)) <= 0)
    {
      if (n != 0)
        rfbLogPerror ("rfbProcessClientNormalMessage: read");
      rfbCloseSock (cl->pScreen, cl->sock);
      return;
    }

    /* find the window and re-expose it */
    {
      CRWindowTable *wt, *nextWt = NULL;

      for (wt = windowTable; wt; wt = nextWt)
      {
        nextWt = wt->next;
        if (wt->CRwinId == msg.cse.winid)
        {
          WindowPtr pWin;
          pWin = LookupIDByType (wt->XwinId, RT_WINDOW);
          if (pWin)
          {
            miSendExposures (pWin, &pWin->clipList,
                             pWin->drawable.x, pWin->drawable.y);
            FlushAllOutput ();
          }
        }
      }
    }

    return;
#endif

#ifdef SHAREDAPP
    case rfbSharedAppRequest:
        if ((n = ReadExact(cl->sock, ((char *)&msg) + 1,
                           sz_rfbSharedAppRequestMsg - 1)) <= 0) {
            if (n != 0)
                rfbLogPerror("rfbSharedAppRequestMsg: read");
            rfbCloseSock(cl->pScreen, cl->sock);
            return;
        }

        msg.sap.command = Swap16IfLE(msg.sap.command);
        msg.sap.id = Swap32IfLE(msg.sap.id);

        sharedapp_HandleRequest(cl, msg.sap.command, msg.sap.id);
        return;

    case rfbMultiCursor:
        if ((n = ReadExact(cl->sock, ((char *)&msg) + 1,
                           sz_rfbMultiCursorMsg - 1)) <= 0) {
            if (n != 0)
                rfbLogPerror("rfbMultiCursorMsg: read");
            rfbCloseSock(cl->pScreen, cl->sock);
            return;
        }
        cl->multicursor = Swap16IfLE(msg.mc.cursorNumber);
        rfbLog("Multicursor set to %d", cl->multicursor);
        return;

#endif

  default:

    rfbLog ("rfbProcessClientNormalMessage: unknown message type %d\n",
            msg.type);
    rfbLog (" ... closing connection\n");
    rfbCloseSock (cl->pScreen, cl->sock);
    return;
  }
}



/*
 * rfbSendFramebufferUpdate - send the currently pending framebuffer update to
 * the RFB client.
 */

Bool
rfbSendFramebufferUpdate (pScreen, cl)
     ScreenPtr pScreen;
     rfbClientPtr cl;
{
  VNCSCREENPTR (pScreen);
  int i;
  int nUpdateRegionRects;
  rfbFramebufferUpdateMsg *fu = (rfbFramebufferUpdateMsg *) pVNC->updateBuf;
  RegionRec updateRegion, updateCopyRegion;
  int dx, dy;
  Bool sendCursorShape = FALSE;
  Bool sendCursorPos = FALSE;

  /*
   * If this client understands cursor shape updates, cursor should be
   * removed from the framebuffer. Otherwise, make sure it's put up.
   */

#ifdef SHAREDAPP
  /* Remove this later */
  if (cl->supportsSharedAppEncoding)
  {

  sharedapp_CheckForClosedWindows(pScreen, rfbClientHead);
  if (pVNC->sharedApp.bEnabled)
  {
    return sharedapp_RfbSendUpdates(pScreen, cl); 
  }

  }
#endif

#if !XFREE86VNC
  if (cl->enableCursorShapeUpdates)
  {
    if (pVNC->cursorIsDrawn)
      rfbSpriteRemoveCursor (pScreen);
    if (!pVNC->cursorIsDrawn && cl->cursorWasChanged)
      sendCursorShape = TRUE;
  } else
  {
    if (!pVNC->cursorIsDrawn)
      rfbSpriteRestoreCursor (pScreen);
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

  /*
   * The modifiedRegion may overlap the destination copyRegion.  We remove
   * any overlapping bits from the copyRegion (since they'd only be
   * overwritten anyway).
   */

  REGION_SUBTRACT (pScreen, &cl->copyRegion, &cl->copyRegion,
                   &cl->modifiedRegion);

  /*
   * The client is interested in the region requestedRegion.  The region
   * which should be updated now is the intersection of requestedRegion
   * and the union of modifiedRegion and copyRegion.  If it's empty then
   * no update is needed.
   */

  REGION_NULL (pScreen, &updateRegion);
  REGION_UNION (pScreen, &updateRegion, &cl->copyRegion, &cl->modifiedRegion);
  REGION_INTERSECT (pScreen, &updateRegion, &cl->requestedRegion,
                    &updateRegion);

  if (!REGION_NOTEMPTY (pScreen, &updateRegion) &&
      !sendCursorShape && !sendCursorPos)
  {
    REGION_UNINIT (pScreen, &updateRegion);
    return TRUE;
  }

  /*
   * We assume that the client doesn't have any pixel data outside the
   * requestedRegion.  In other words, both the source and destination of a
   * copy must lie within requestedRegion.  So the region we can send as a
   * copy is the intersection of the copyRegion with both the requestedRegion
   * and the requestedRegion translated by the amount of the copy.  We set
   * updateCopyRegion to this.
   */

  REGION_NULL (pScreen, &updateCopyRegion);
  REGION_INTERSECT (pScreen, &updateCopyRegion, &cl->copyRegion,
                    &cl->requestedRegion);
  REGION_TRANSLATE (pScreen, &cl->requestedRegion, cl->copyDX, cl->copyDY);
  REGION_INTERSECT (pScreen, &updateCopyRegion, &updateCopyRegion,
                    &cl->requestedRegion);
  dx = cl->copyDX;
  dy = cl->copyDY;

  /*
   * Next we remove updateCopyRegion from updateRegion so that updateRegion
   * is the part of this update which is sent as ordinary pixel data (i.e not
   * a copy).
   */

  REGION_SUBTRACT (pScreen, &updateRegion, &updateRegion, &updateCopyRegion);

  /*
   * Finally we leave modifiedRegion to be the remainder (if any) of parts of
   * the screen which are modified but outside the requestedRegion.  We also
   * empty both the requestedRegion and the copyRegion - note that we never
   * carry over a copyRegion for a future update.
   */

  REGION_UNION (pScreen, &cl->modifiedRegion, &cl->modifiedRegion,
                &cl->copyRegion);
  REGION_SUBTRACT (pScreen, &cl->modifiedRegion, &cl->modifiedRegion,
                   &updateRegion);
  REGION_SUBTRACT (pScreen, &cl->modifiedRegion, &cl->modifiedRegion,
                   &updateCopyRegion);

  REGION_EMPTY (pScreen, &cl->requestedRegion);
  REGION_EMPTY (pScreen, &cl->copyRegion);
  cl->copyDX = 0;
  cl->copyDY = 0;

  /*
   * Now send the update.
   */

  cl->rfbFramebufferUpdateMessagesSent++;

  if (cl->preferredEncoding == rfbEncodingCoRRE)
  {
    nUpdateRegionRects = 0;

    for (i = 0; i < REGION_NUM_RECTS (&updateRegion); i++)
    {
      int x = REGION_RECTS (&updateRegion)[i].x1;
      int y = REGION_RECTS (&updateRegion)[i].y1;
      int w = REGION_RECTS (&updateRegion)[i].x2 - x;
      int h = REGION_RECTS (&updateRegion)[i].y2 - y;
      nUpdateRegionRects += (((w - 1) / cl->correMaxWidth + 1)
                             * ((h - 1) / cl->correMaxHeight + 1));
    }
  } else if (cl->preferredEncoding == rfbEncodingZlib)
  {
    nUpdateRegionRects = 0;

    for (i = 0; i < REGION_NUM_RECTS (&updateRegion); i++)
    {
      int x = REGION_RECTS (&updateRegion)[i].x1;
      int y = REGION_RECTS (&updateRegion)[i].y1;
      int w = REGION_RECTS (&updateRegion)[i].x2 - x;
      int h = REGION_RECTS (&updateRegion)[i].y2 - y;
      nUpdateRegionRects += (((h - 1) / (ZLIB_MAX_SIZE (w) / w)) + 1);
    }
  } else if (cl->preferredEncoding == rfbEncodingTight)
  {
    nUpdateRegionRects = 0;

    for (i = 0; i < REGION_NUM_RECTS (&updateRegion); i++)
    {
      int x = REGION_RECTS (&updateRegion)[i].x1;
      int y = REGION_RECTS (&updateRegion)[i].y1;
      int w = REGION_RECTS (&updateRegion)[i].x2 - x;
      int h = REGION_RECTS (&updateRegion)[i].y2 - y;
      int n = rfbNumCodedRectsTight (cl, x, y, w, h);
      if (n == 0)
      {
        nUpdateRegionRects = 0xFFFF;
        break;
      }
      nUpdateRegionRects += n;
    }
  } else
  {
    nUpdateRegionRects = REGION_NUM_RECTS (&updateRegion);
  }

  fu->type = rfbFramebufferUpdate;
  if (nUpdateRegionRects != 0xFFFF)
  {
    fu->nRects = Swap16IfLE (REGION_NUM_RECTS (&updateCopyRegion) +
                             nUpdateRegionRects +
                             !!sendCursorShape + !!sendCursorPos);
  } else
  {
    fu->nRects = 0xFFFF;
  }
  pVNC->ublen = sz_rfbFramebufferUpdateMsg;

  if (sendCursorShape)
  {
    cl->cursorWasChanged = FALSE;
    if (!rfbSendCursorShape (cl, pScreen))
      return FALSE;
  }

  if (sendCursorPos)
  {
    cl->cursorWasMoved = FALSE;
    if (!rfbSendCursorPos (cl, pScreen))
      return FALSE;
  }

  if (REGION_NOTEMPTY (pScreen, &updateCopyRegion))
  {
    if (!rfbSendCopyRegion (cl, &updateCopyRegion, dx, dy))
    {
      REGION_UNINIT (pScreen, &updateRegion);
      REGION_UNINIT (pScreen, &updateCopyRegion);
      return FALSE;
    }
  }

  REGION_UNINIT (pScreen, &updateCopyRegion);

  for (i = 0; i < REGION_NUM_RECTS (&updateRegion); i++)
  {
    int x = REGION_RECTS (&updateRegion)[i].x1;
    int y = REGION_RECTS (&updateRegion)[i].y1;
    int w = REGION_RECTS (&updateRegion)[i].x2 - x;
    int h = REGION_RECTS (&updateRegion)[i].y2 - y;

    cl->rfbRawBytesEquivalent += (sz_rfbFramebufferUpdateRectHeader
                                  + w * (cl->format.bitsPerPixel / 8) * h);

    switch (cl->preferredEncoding)
    {
    case rfbEncodingRaw:
      if (!rfbSendRectEncodingRaw (cl, x, y, w, h))
      {
        REGION_UNINIT (pScreen, &updateRegion);
        return FALSE;
      }
      break;
    case rfbEncodingRRE:
      if (!rfbSendRectEncodingRRE (cl, x, y, w, h))
      {
        REGION_UNINIT (pScreen, &updateRegion);
        return FALSE;
      }
      break;
    case rfbEncodingCoRRE:
      if (!rfbSendRectEncodingCoRRE (cl, x, y, w, h))
      {
        REGION_UNINIT (pScreen, &updateRegion);
        return FALSE;
      }
      break;
    case rfbEncodingHextile:
      if (!rfbSendRectEncodingHextile (cl, x, y, w, h))
      {
        REGION_UNINIT (pScreen, &updateRegion);
        return FALSE;
      }
      break;
    case rfbEncodingZlib:
      if (!rfbSendRectEncodingZlib (cl, x, y, w, h))
      {
        REGION_UNINIT (pScreen, &updateRegion);
        return FALSE;
      }
      break;
    case rfbEncodingTight:
      if (!rfbSendRectEncodingTight (cl, x, y, w, h))
      {
        REGION_UNINIT (pScreen, &updateRegion);
        return FALSE;
      }
      break;
    }
  }

  REGION_UNINIT (pScreen, &updateRegion);

  if (nUpdateRegionRects == 0xFFFF && !rfbSendLastRectMarker (cl))
    return FALSE;

  if (!rfbSendUpdateBuf (cl))
    return FALSE;

  return TRUE;
}



/*
 * Send the copy region as a string of CopyRect encoded rectangles.
 * The only slightly tricky thing is that we should send the messages in
 * the correct order so that an earlier CopyRect will not corrupt the source
 * of a later one.
 */

Bool
rfbSendCopyRegion (cl, reg, dx, dy)
     rfbClientPtr cl;
     RegionPtr reg;
     int dx, dy;
{
  VNCSCREENPTR (cl->pScreen);
  int nrects, nrectsInBand, x_inc, y_inc, thisRect, firstInNextBand;
  int x, y, w, h;
  rfbFramebufferUpdateRectHeader rect;
  rfbCopyRect cr;

  nrects = REGION_NUM_RECTS (reg);

  if (dx <= 0)
  {
    x_inc = 1;
  } else
  {
    x_inc = -1;
  }

  if (dy <= 0)
  {
    thisRect = 0;
    y_inc = 1;
  } else
  {
    thisRect = nrects - 1;
    y_inc = -1;
  }

  while (nrects > 0)
  {

    firstInNextBand = thisRect;
    nrectsInBand = 0;

    while ((nrects > 0) &&
           (REGION_RECTS (reg)[firstInNextBand].y1
            == REGION_RECTS (reg)[thisRect].y1))
    {
      firstInNextBand += y_inc;
      nrects--;
      nrectsInBand++;
    }

    if (x_inc != y_inc)
    {
      thisRect = firstInNextBand - y_inc;
    }

    while (nrectsInBand > 0)
    {
      if ((pVNC->ublen + sz_rfbFramebufferUpdateRectHeader
           + sz_rfbCopyRect) > UPDATE_BUF_SIZE)
      {
        if (!rfbSendUpdateBuf (cl))
          return FALSE;
      }

      x = REGION_RECTS (reg)[thisRect].x1;
      y = REGION_RECTS (reg)[thisRect].y1;
      w = REGION_RECTS (reg)[thisRect].x2 - x;
      h = REGION_RECTS (reg)[thisRect].y2 - y;

      rect.r.x = Swap16IfLE (x);
      rect.r.y = Swap16IfLE (y);
      rect.r.w = Swap16IfLE (w);
      rect.r.h = Swap16IfLE (h);
      rect.encoding = Swap32IfLE (rfbEncodingCopyRect);

      memcpy (&pVNC->updateBuf[pVNC->ublen], (char *) &rect,
              sz_rfbFramebufferUpdateRectHeader);
      pVNC->ublen += sz_rfbFramebufferUpdateRectHeader;

      cr.srcX = Swap16IfLE (x - dx);
      cr.srcY = Swap16IfLE (y - dy);

      memcpy (&pVNC->updateBuf[pVNC->ublen], (char *) &cr, sz_rfbCopyRect);
      pVNC->ublen += sz_rfbCopyRect;

      cl->rfbRectanglesSent[rfbEncodingCopyRect]++;
      cl->rfbBytesSent[rfbEncodingCopyRect]
        += sz_rfbFramebufferUpdateRectHeader + sz_rfbCopyRect;

      thisRect += x_inc;
      nrectsInBand--;
    }

    thisRect = firstInNextBand;
  }

  return TRUE;
}


/*
 * Send a given rectangle in raw encoding (rfbEncodingRaw).
 */

Bool
rfbSendRectEncodingRaw (cl, x, y, w, h)
     rfbClientPtr cl;
     int x, y, w, h;
{
  VNCSCREENPTR (cl->pScreen);
  rfbFramebufferUpdateRectHeader rect;
  int nlines;
  int bytesPerLine = w * (cl->format.bitsPerPixel / 8);
  unsigned char *fbptr = NULL;
  int newy = 0;

  if (pVNC->useGetImage)
  {
    newy = y;
  } else
  {
    fbptr = (pVNC->pfbMemory + (pVNC->paddedWidthInBytes * y)
             + (x * (pVNC->bitsPerPixel / 8)));
  }

  /* Flush the buffer to guarantee correct alignment for translateFn(). */
  if (pVNC->ublen > 0)
  {
    if (!rfbSendUpdateBuf (cl))
      return FALSE;
  }

  rect.r.x = Swap16IfLE (x);
  rect.r.y = Swap16IfLE (y);
  rect.r.w = Swap16IfLE (w);
  rect.r.h = Swap16IfLE (h);
  rect.encoding = Swap32IfLE (rfbEncodingRaw);

  memcpy (&pVNC->updateBuf[pVNC->ublen], (char *) &rect,
          sz_rfbFramebufferUpdateRectHeader);
  pVNC->ublen += sz_rfbFramebufferUpdateRectHeader;

  cl->rfbRectanglesSent[rfbEncodingRaw]++;
  cl->rfbBytesSent[rfbEncodingRaw]
    += sz_rfbFramebufferUpdateRectHeader + bytesPerLine * h;

  nlines = (UPDATE_BUF_SIZE - pVNC->ublen) / bytesPerLine;

  while (TRUE)
  {
    if (nlines > h)
      nlines = h;

    if (pVNC->useGetImage)
    {
      (*cl->pScreen->GetImage) ((DrawablePtr) WindowTable[cl->pScreen->myNum],
                                x, newy, w, nlines, ZPixmap, ~0,
                                &pVNC->updateBuf[pVNC->ublen]);
      newy += nlines;
    } else
    {
      (*cl->translateFn) (cl->pScreen, cl->translateLookupTable,
                          &pVNC->rfbServerFormat, &cl->format, fbptr,
                          &pVNC->updateBuf[pVNC->ublen],
                          pVNC->paddedWidthInBytes, w, nlines, x, y);
    }

    pVNC->ublen += nlines * bytesPerLine;
    h -= nlines;

    if (h == 0)                 /* rect fitted in buffer, do next one */
      return TRUE;

    /* buffer full - flush partial rect and do another nlines */

    if (!rfbSendUpdateBuf (cl))
      return FALSE;

    if (!pVNC->useGetImage)
      fbptr += (pVNC->paddedWidthInBytes * nlines);

    nlines = (UPDATE_BUF_SIZE - pVNC->ublen) / bytesPerLine;
    if (nlines == 0)
    {
      rfbLog ("rfbSendRectEncodingRaw: send buffer too small for %d "
              "bytes per line\n", bytesPerLine);
      rfbCloseSock (cl->pScreen, cl->sock);
      return FALSE;
    }
  }
}


/*
 * Send an empty rectangle with encoding field set to value of
 * rfbEncodingLastRect to notify client that this is the last
 * rectangle in framebuffer update ("LastRect" extension of RFB
 * protocol).
 */

Bool
rfbSendLastRectMarker (cl)
     rfbClientPtr cl;
{
  VNCSCREENPTR (cl->pScreen);
  rfbFramebufferUpdateRectHeader rect;

  if (pVNC->ublen + sz_rfbFramebufferUpdateRectHeader > UPDATE_BUF_SIZE)
  {
    if (!rfbSendUpdateBuf (cl))
      return FALSE;
  }

  rect.encoding = Swap32IfLE (rfbEncodingLastRect);
  rect.r.x = 0;
  rect.r.y = 0;
  rect.r.w = 0;
  rect.r.h = 0;

  memcpy (&pVNC->updateBuf[pVNC->ublen], (char *) &rect,
          sz_rfbFramebufferUpdateRectHeader);
  pVNC->ublen += sz_rfbFramebufferUpdateRectHeader;

  cl->rfbLastRectMarkersSent++;
  cl->rfbLastRectBytesSent += sz_rfbFramebufferUpdateRectHeader;

  return TRUE;
}


/*
 * Send the contents of pVNC->updateBuf.  Returns 1 if successful, -1 if
 * not (errno should be set).
 */

Bool
rfbSendUpdateBuf (cl)
     rfbClientPtr cl;
{
  VNCSCREENPTR (cl->pScreen);

  /*
     int i;
     for (i = 0; i < pVNC->ublen; i++) {
     rfbLog("%02x ",((unsigned char *)pVNC->updateBuf)[i]);
     }
     rfbLog("\n");
   */

  if (pVNC->ublen > 0
      && WriteExact (cl->sock, pVNC->updateBuf, pVNC->ublen) < 0)
  {
    rfbLogPerror ("rfbSendUpdateBuf: write");
    rfbCloseSock (cl->pScreen, cl->sock);
    return FALSE;
  }

  pVNC->ublen = 0;
  return TRUE;
}



/*
 * rfbSendSetColourMapEntries sends a SetColourMapEntries message to the
 * client, using values from the currently installed colormap.
 */

Bool
rfbSendSetColourMapEntries (cl, firstColour, nColours)
     rfbClientPtr cl;
     int firstColour;
     int nColours;
{
#if !XFREE86VNC
  VNCSCREENPTR (cl->pScreen);
#endif
  char buf[sz_rfbSetColourMapEntriesMsg + 256 * 3 * 2];
  rfbSetColourMapEntriesMsg *scme = (rfbSetColourMapEntriesMsg *) buf;
  CARD16 *rgb = (CARD16 *) (&buf[sz_rfbSetColourMapEntriesMsg]);
  EntryPtr pent;
  EntryPtr redEntry, greenEntry, blueEntry;
  unsigned short redPart, greenPart, bluePart;
  int i, len;

  scme->type = rfbSetColourMapEntries;
  scme->nColours = Swap16IfLE (nColours);

  len = sz_rfbSetColourMapEntriesMsg;

  /* PseudoColor */
#if XFREE86VNC
  if (miInstalledMaps[cl->pScreen->myNum]->class == PseudoColor)
  {
#else
  if (pVNC->rfbInstalledColormap->class == PseudoColor)
  {
#endif
    scme->firstColour = Swap16IfLE (firstColour);
#if XFREE86VNC
    pent = (EntryPtr) & miInstalledMaps[cl->pScreen->myNum]->red[firstColour];
#else
    pent = (EntryPtr) & pVNC->rfbInstalledColormap->red[firstColour];
#endif
    for (i = 0; i < nColours; i++)
    {
      if (pent->fShared)
      {
        rgb[i * 3] = Swap16IfLE (pent->co.shco.red->color);
        rgb[i * 3 + 1] = Swap16IfLE (pent->co.shco.green->color);
        rgb[i * 3 + 2] = Swap16IfLE (pent->co.shco.blue->color);
      } else
      {
        rgb[i * 3] = Swap16IfLE (pent->co.local.red);
        rgb[i * 3 + 1] = Swap16IfLE (pent->co.local.green);
        rgb[i * 3 + 2] = Swap16IfLE (pent->co.local.blue);
      }
      pent++;
    }
  }

  else
  {

    /* Break the DirectColor pixel into its r/g/b components */
#if XFREE86VNC
    redPart =
      (firstColour & miInstalledMaps[cl->pScreen->myNum]->pVisual->
       redMask) >> miInstalledMaps[cl->pScreen->myNum]->pVisual->offsetRed;
    greenPart =
      (firstColour & miInstalledMaps[cl->pScreen->myNum]->pVisual->
       greenMask) >> miInstalledMaps[cl->pScreen->myNum]->pVisual->
      offsetGreen;
    bluePart =
      (firstColour & miInstalledMaps[cl->pScreen->myNum]->pVisual->
       blueMask) >> miInstalledMaps[cl->pScreen->myNum]->pVisual->offsetBlue;
#else
    redPart = (firstColour & pVNC->rfbInstalledColormap->pVisual->redMask)
      >> pVNC->rfbInstalledColormap->pVisual->offsetRed;
    greenPart = (firstColour & pVNC->rfbInstalledColormap->pVisual->greenMask)
      >> pVNC->rfbInstalledColormap->pVisual->offsetGreen;
    bluePart = (firstColour & pVNC->rfbInstalledColormap->pVisual->blueMask)
      >> pVNC->rfbInstalledColormap->pVisual->offsetBlue;
#endif

    /*
     * The firstColour field is only 16 bits. To support 24-bit pixels we
     * sneak the red component in the 8-bit padding field which we renamed
     * to redIndex. Green and blue are in firstColour (MSB, LSB respectively).
     */
    scme->redIndex = Swap16IfLE (redPart);
    scme->firstColour = Swap16IfLE ((greenPart << 8) | bluePart);

#if XFREE86VNC
    redEntry = (EntryPtr) & miInstalledMaps[cl->pScreen->myNum]->red[redPart];
    greenEntry =
      (EntryPtr) & miInstalledMaps[cl->pScreen->myNum]->green[greenPart];
    blueEntry =
      (EntryPtr) & miInstalledMaps[cl->pScreen->myNum]->blue[bluePart];
#else
    redEntry = (EntryPtr) & pVNC->rfbInstalledColormap->red[redPart];
    greenEntry = (EntryPtr) & pVNC->rfbInstalledColormap->green[greenPart];
    blueEntry = (EntryPtr) & pVNC->rfbInstalledColormap->blue[bluePart];
#endif
    for (i = 0; i < nColours; i++)
    {
      if (redEntry->fShared)
        rgb[i * 3] = Swap16IfLE (redEntry->co.shco.red->color);
      else
        rgb[i * 3] = Swap16IfLE (redEntry->co.local.red);

      if (greenEntry->fShared)
        rgb[i * 3 + 1] = Swap16IfLE (greenEntry->co.shco.green->color);
      else
        rgb[i * 3 + 1] = Swap16IfLE (greenEntry->co.local.green);

      if (blueEntry->fShared)
        rgb[i * 3 + 2] = Swap16IfLE (blueEntry->co.shco.blue->color);
      else
        rgb[i * 3 + 2] = Swap16IfLE (blueEntry->co.local.blue);

      redEntry++;
      greenEntry++;
      blueEntry++;
    }
  }

  len += nColours * 3 * 2;

  if (WriteExact (cl->sock, buf, len) < 0)
  {
    rfbLogPerror ("rfbSendSetColourMapEntries: write");
    rfbCloseSock (cl->pScreen, cl->sock);
    return FALSE;
  }
  return TRUE;
}


/*
 * rfbSendBell sends a Bell message to all the clients.
 */

void
rfbSendBell ()
{
  rfbClientPtr cl, nextCl;
  rfbBellMsg b;

  for (cl = rfbClientHead; cl; cl = nextCl)
  {
    nextCl = cl->next;
    b.type = rfbBell;
    if (WriteExact (cl->sock, (char *) &b, sz_rfbBellMsg) < 0)
    {
      rfbLogPerror ("rfbSendBell: write");
      rfbCloseSock (cl->pScreen, cl->sock);
    }
  }
}

#ifdef CHROMIUM
#ifdef sun
extern int inet_aton (const char *cp, struct in_addr *inp);
#endif

void
rfbSendChromiumStart (unsigned int ipaddress, unsigned int port)
{
  rfbClientPtr cl, nextCl;
  rfbChromiumStartMsg scd;
  struct in_addr ip;
  unsigned int vncipaddress;

  for (cl = rfbClientHead; cl; cl = nextCl)
  {
    nextCl = cl->next;
    if (!cl->enableChromiumEncoding)
      continue;
    inet_aton (cl->host, &ip);
    memcpy (&vncipaddress, &ip, sizeof (unsigned int));
    if (ipaddress == vncipaddress && !cl->chromium_port)
    {
      cl->chromium_port = port;
      scd.type = rfbChromiumStart;
      scd.port = port;
      if (WriteExact (cl->sock, (char *) &scd, sz_rfbChromiumStartMsg) < 0)
      {
        rfbLogPerror ("rfbSendChromiumStart: write");
        rfbCloseSock (cl->pScreen, cl->sock);
      }
      /* We only start one client at at time, so break now! */
      break;
    }
  }
}

void
rfbChromiumMonitorWindowID (unsigned int cr_windowid, unsigned long windowid)
{
  CRWindowTable *nextRec;
  CRWindowTable *wt, *nextWt = NULL;

  /* See if we're already managing this window */
  for (wt = windowTable; wt; wt = nextWt)
  {
    nextWt = wt->next;
    /* and if so, update it's window ID */
    if (wt->CRwinId == cr_windowid)
    {
      wt->XwinId = windowid;
      return;
    }
  }

  /* o.k, new window so create new slot information */
  nextRec = (CRWindowTable *) xalloc (sizeof (CRWindowTable));

  if (!nextRec)
  {
    rfbLog ("OUCH, Chromium can't monitor window ID\n");
    return;
  }

  nextRec->next = NULL;
  nextRec->CRwinId = cr_windowid;
  nextRec->XwinId = windowid;
  nextRec->clipRects = NULL;
  nextRec->numRects = 0;

  if (!windowTable)
    windowTable = nextRec;
  else
  {
    for (wt = windowTable; wt; wt = nextWt)
    {
      nextWt = wt->next;
      if (!wt->next)            /* found the next slot */
        wt->next = nextRec;
    }
  }
}

void
rfbSendChromiumMoveResizeWindow (unsigned int winid, int x, int y,
                                 unsigned int w, unsigned int h)
{
  rfbClientPtr cl, nextCl;
  rfbChromiumMoveResizeWindowMsg scm;

  for (cl = rfbClientHead; cl; cl = nextCl)
  {
    nextCl = cl->next;
    if (!cl->enableChromiumEncoding)
      continue;
    if (cl->chromium_port)
    {
      scm.type = rfbChromiumMoveResizeWindow;
      scm.winid = winid;
      scm.x = x;
      scm.y = y;
      scm.w = w;
      scm.h = h;
      if (WriteExact (cl->sock, (char *) &scm,
                      sz_rfbChromiumMoveResizeWindowMsg) < 0)
      {
        rfbLogPerror ("rfbSendChromiumMoveResizeWindow: write");
        rfbCloseSock (cl->pScreen, cl->sock);
        continue;
      }
    }
  }
}

void
rfbSendChromiumClipList (unsigned int winid, BoxPtr pClipRects,
                         int numClipRects)
{
  rfbClientPtr cl, nextCl;
  rfbChromiumClipListMsg sccl;
  int len = sizeof (BoxRec) * numClipRects;

  for (cl = rfbClientHead; cl; cl = nextCl)
  {
    nextCl = cl->next;
    if (!cl->enableChromiumEncoding)
      continue;
    if (cl->chromium_port)
    {
      sccl.type = rfbChromiumClipList;
      sccl.winid = winid;
      sccl.length = Swap32IfLE (len);
      if (WriteExact (cl->sock, (char *) &sccl,
                      sz_rfbChromiumClipListMsg) < 0)
      {
        rfbLogPerror ("rfbSendChromiumClipList: write");
        rfbCloseSock (cl->pScreen, cl->sock);
        continue;
      }
      if (WriteExact (cl->sock, (char *) pClipRects, len) < 0)
      {
        rfbLogPerror ("rfbSendChromiumClipList: write");
        rfbCloseSock (cl->pScreen, cl->sock);
        continue;
      }
    }
  }
}

void
rfbSendChromiumWindowShow (unsigned int winid, unsigned int show)
{
  rfbClientPtr cl, nextCl;
  rfbChromiumWindowShowMsg scws;

  for (cl = rfbClientHead; cl; cl = nextCl)
  {
    nextCl = cl->next;
    if (!cl->enableChromiumEncoding)
      continue;
    if (cl->chromium_port)
    {
      scws.type = rfbChromiumWindowShow;
      scws.winid = winid;
      scws.show = show;
      if (WriteExact (cl->sock, (char *) &scws,
                      sz_rfbChromiumWindowShowMsg) < 0)
      {
        rfbLogPerror ("rfbSendChromiumWindowShow: write");
        rfbCloseSock (cl->pScreen, cl->sock);
        continue;
      }
    }
  }
}
#endif /* CHROMIUM */

/*
 * rfbSendServerCutText sends a ServerCutText message to all the clients.
 */

void
rfbSendServerCutText (char *str, int len)
{
  rfbClientPtr cl, nextCl = NULL;
  rfbServerCutTextMsg sct;

  for (cl = rfbClientHead; cl; cl = nextCl)
  {
    if (cl->state != RFB_NORMAL)
      continue;
    nextCl = cl->next;
    sct.type = rfbServerCutText;
    sct.length = Swap32IfLE (len);
    if (WriteExact (cl->sock, (char *) &sct, sz_rfbServerCutTextMsg) < 0)
    {
      rfbLogPerror ("rfbSendServerCutText: write");
      rfbCloseSock (cl->pScreen, cl->sock);
      continue;
    }
    if (WriteExact (cl->sock, str, len) < 0)
    {
      rfbLogPerror ("rfbSendServerCutText: write");
      rfbCloseSock (cl->pScreen, cl->sock);
    }
  }
}




/*****************************************************************************
 *
 * UDP can be used for keyboard and pointer events when the underlying
 * network is highly reliable.  This is really here to support ORL's
 * videotile, whose TCP implementation doesn't like sending lots of small
 * packets (such as 100s of pen readings per second!).
 */

void
rfbNewUDPConnection (sock)
     int sock;
{
  if (write (sock, &ptrAcceleration, 1) < 0)
  {
    rfbLogPerror ("rfbNewUDPConnection: write");
  }
}

/*
 * Because UDP is a message based service, we can't read the first byte and
 * then the rest of the packet separately like we do with TCP.  We will always
 * get a whole packet delivered in one go, so we ask read() for the maximum
 * number of bytes we can possibly get.
 */

void
rfbProcessUDPInput (ScreenPtr pScreen, int sock)
{
  VNCSCREENPTR (pScreen);
  int n;
  rfbClientToServerMsg msg;

  if ((n = read (sock, (char *) &msg, sizeof (msg))) <= 0)
  {
    if (n < 0)
    {
      rfbLogPerror ("rfbProcessUDPInput: read");
    }
    rfbDisconnectUDPSock (pScreen);
    return;
  }

  switch (msg.type)
  {

  case rfbKeyEvent:
    if (n != sz_rfbKeyEventMsg)
    {
      rfbLog ("rfbProcessUDPInput: key event incorrect length\n");
      rfbDisconnectUDPSock (pScreen);
      return;
    }
    if (!pVNC->rfbViewOnly)
    {
      KbdAddEvent (msg.ke.down, (KeySym) Swap32IfLE (msg.ke.key), 0);
    }
    break;

  case rfbPointerEvent:
    if (n != sz_rfbPointerEventMsg)
    {
      rfbLog ("rfbProcessUDPInput: ptr event incorrect length\n");
      rfbDisconnectUDPSock (pScreen);
      return;
    }
    if (!pVNC->rfbViewOnly)
    {
      PtrAddEvent (msg.pe.buttonMask,
                   Swap16IfLE (msg.pe.x), Swap16IfLE (msg.pe.y), 0);
    }
    break;

  default:
    rfbLog ("rfbProcessUDPInput: unknown message type %d\n", msg.type);
    rfbDisconnectUDPSock (pScreen);
  }
}
