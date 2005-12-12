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
#include "extnsionst.h"
#define _VNC_SERVER
#include "vncstr.h"
#ifdef XFree86LOADER
#include "xf86Module.h"
#endif

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#ifdef CHROMIUM
extern void rfbSendChromiumStart(unsigned int ipaddress, unsigned int port);
extern void rfbChromiumMonitorWindowID(unsigned int cr_windowid, unsigned long windowid);
int GenerateVncChromiumConnectedEvent(int sock);
#endif

extern void rfbUserAllow(int sock, int accept);

int VncSelectNotify(ClientPtr client, BOOL onoff);
int GenerateVncConnectedEvent(int sock);
int GenerateVncDisconnectedEvent(int sock);
void VncExtensionInit(void);

static int VncErrorBase;  /* first vnc error number */
static int VncEventBase;  /* first vnc event number */

unsigned long VncResourceGeneration = 0;

static RESTYPE VncNotifyList;

static XID faked;

typedef struct _VncNotifyListRec {
  struct _VncNotifyListRec *next;
  ClientPtr client;
} VncNotifyListRec, *VncNotifyListPtr;

static int
ProcVncQueryVersion(ClientPtr client)
{
    xVncQueryVersionReply 	rep;

    REQUEST_SIZE_MATCH(xVncQueryVersionReq);
    rep.type        	= X_Reply;
    rep.sequenceNumber 	= client->sequence;
    rep.length         	= 0;
    rep.majorVersion  	= VNC_MAJOR_VERSION;
    rep.minorVersion  	= VNC_MINOR_VERSION;
    if(client->swapped)
    {
	register char n;
    	swaps(&rep.sequenceNumber, n);
	swaps(&rep.majorVersion, n);
	swaps(&rep.minorVersion, n);
    }
    (void)WriteToClient(client, SIZEOF(xVncQueryVersionReply),
			(char *)&rep);
    return (client->noClientException);
} /* ProcVncQueryVersion */

static int
ProcVncConnection(ClientPtr client)
{
    REQUEST(xVncConnectionReq);
    xVncConnectionReply 	rep;

    rfbUserAllow( stuff->sock, stuff->accept );

    REQUEST_SIZE_MATCH(xVncConnectionReq);
    rep.type        	= X_Reply;
    rep.sequenceNumber 	= client->sequence;
    rep.length         	= 0;
    rep.sock  		= 0;
    rep.accept  	= 0;
    if(client->swapped)
    {
	register char n;
    	swaps(&rep.sequenceNumber, n);
	swaps(&rep.sock, n);
	swaps(&rep.accept, n);
    }
    (void)WriteToClient(client, SIZEOF(xVncConnectionReply),
			(char *)&rep);
    return (client->noClientException);
} /* ProcVncConnection */

static int
ProcVncSelectNotify(ClientPtr client)
{
    REQUEST(xVncSelectNotifyReq);
    xVncSelectNotifyReply 	rep;

    VncSelectNotify(client, stuff->onoff);

    REQUEST_SIZE_MATCH(xVncSelectNotifyReq);
    rep.type        	= X_Reply;
    rep.sequenceNumber 	= client->sequence;
    rep.length         	= 0;
    if(client->swapped)
    {
	register char n;
    	swaps(&rep.sequenceNumber, n);
    }
    (void)WriteToClient(client, SIZEOF(xVncSelectNotifyReply),
			(char *)&rep);
    return (client->noClientException);

}

static int
ProcVncListConnections(ClientPtr client)
{
    xVncListConnectionsReply 	rep;
    rfbClientPtr cl;
    int count = 0;
    struct in_addr ipaddress;
    xVncConnectionListInfo Info;

    /* count connections */
    for (cl = rfbClientHead; cl; cl = cl->next)
#ifdef CHROMIUM
    /* 
     * Fix this, as it should be generic, but we're only using it
     * for Chromium at the moment, so we only list the connections
     * that have a valid chromium encoding. We should be able to list
     * any type requested. FIXME! XXXX
     * See furthur down this function too!!!
     */
	    if (cl->enableChromiumEncoding)
#endif
	    	count++;

    REQUEST_SIZE_MATCH(xVncListConnectionsReq);
    rep.type        	= X_Reply;
    rep.sequenceNumber 	= client->sequence;
    rep.count		= count;
    rep.length 		= count * sizeof(xVncConnectionListInfo) >> 2;
    if(client->swapped)
    {
	register char n;
    	swaps(&rep.sequenceNumber, n);
    	swaps(&rep.count, n);
    }
    (void)WriteToClient(client, SIZEOF(xVncListConnectionsReply),
			(char *)&rep);

    for (cl = rfbClientHead; cl; cl = cl->next) {
#ifdef CHROMIUM
        /* 
         * Fix this, as it should be generic, but we're only using it
         * for Chromium at the moment, so we only list the connections
         * that have a valid chromium encoding. We should be able to list
         * any type requested. FIXME! XXXX
         */
	if (!cl->enableChromiumEncoding)
	    continue;
#endif
	inet_aton(cl->host, &ipaddress);
	memcpy(&Info, &ipaddress, sizeof(struct in_addr));
	WriteToClient(client, SIZEOF(xVncConnectionListInfo), (char*)&Info);
    }

    return (client->noClientException);
} /* ProcVncListConnections */

#ifdef CHROMIUM
static int
ProcVncChromiumStart(ClientPtr client)
{
    REQUEST(xVncChromiumStartReq);
    xVncChromiumStartReply 	rep;

    rfbSendChromiumStart(stuff->ipaddress, stuff->port);

    REQUEST_SIZE_MATCH(xVncChromiumStartReq);
    rep.type        	= X_Reply;
    rep.sequenceNumber 	= client->sequence;
    rep.length         	= 0;
    if(client->swapped)
    {
	register char n;
    	swaps(&rep.sequenceNumber, n);
    }
    (void)WriteToClient(client, SIZEOF(xVncChromiumStartReply),
			(char *)&rep);
    return (client->noClientException);
} /* ProcVncChromiumStart */

static int
ProcVncChromiumMonitor(ClientPtr client)
{
    REQUEST(xVncChromiumMonitorReq);
    xVncChromiumMonitorReply 	rep;

    rfbChromiumMonitorWindowID(stuff->cr_windowid, stuff->windowid);

    REQUEST_SIZE_MATCH(xVncChromiumMonitorReq);
    rep.type        	= X_Reply;
    rep.sequenceNumber 	= client->sequence;
    rep.length         	= 0;
    if(client->swapped)
    {
	register char n;
    	swaps(&rep.sequenceNumber, n);
    }
    (void)WriteToClient(client, SIZEOF(xVncChromiumMonitorReply),
			(char *)&rep);
    return (client->noClientException);
} /* ProcVncChromiumMonitor */
#endif /* CHROMIUM */

static int
ProcVncDispatch(ClientPtr client)
{
    REQUEST(xReq);

    switch (stuff->data)
    {
	case X_VncQueryVersion:
	    return ProcVncQueryVersion(client);
	case X_VncSelectNotify:
	    return ProcVncSelectNotify(client);
	case X_VncConnection:
	    return ProcVncConnection(client);
	case X_VncListConnections:
	    return ProcVncListConnections(client);
#ifdef CHROMIUM
	case X_VncChromiumStart:
	    return ProcVncChromiumStart(client);
	case X_VncChromiumMonitor:
	    return ProcVncChromiumMonitor(client);
#endif
	default:
	    return BadRequest;
    }
} /* ProcVncDispatch */

static int
SProcVncQueryVersion(ClientPtr client)
{
    REQUEST(xVncQueryVersionReq);
    register char 	n;

    swaps(&stuff->length, n);
    REQUEST_SIZE_MATCH(xVncQueryVersionReq);
    swaps(&stuff->majorVersion, n);
    swaps(&stuff->minorVersion,n);
    return ProcVncQueryVersion(client);
} /* SProcVncQueryVersion */

static int
SProcVncSelectNotify(ClientPtr client)
{
    REQUEST(xVncSelectNotifyReq);
    register char 	n;

    swaps(&stuff->length, n);
    REQUEST_SIZE_MATCH(xVncSelectNotifyReq);
    return ProcVncSelectNotify(client);
} /* SProcVncSelectNotify */

static int
SProcVncListConnections(ClientPtr client)
{
    REQUEST(xVncListConnectionsReq);
    register char 	n;

    swaps(&stuff->length, n);
    REQUEST_SIZE_MATCH(xVncListConnectionsReq);
    return ProcVncListConnections(client);
} /* SProcVncListConnections */

#ifdef CHROMIUM
static int
SProcVncChromiumStart(ClientPtr client)
{
    REQUEST(xVncChromiumStartReq);
    register char 	n;

    swaps(&stuff->ipaddress, n);
    swaps(&stuff->port, n);
    swaps(&stuff->length, n);
    REQUEST_SIZE_MATCH(xVncChromiumStartReq);
    return ProcVncChromiumStart(client);
} /* SProcVncChromiumStart */

static int
SProcVncChromiumMonitor(ClientPtr client)
{
    REQUEST(xVncChromiumMonitorReq);
    register char 	n;

    swaps(&stuff->length, n);
    swaps(&stuff->windowid, n);
    swaps(&stuff->cr_windowid, n);
    REQUEST_SIZE_MATCH(xVncChromiumMonitorReq);
    return ProcVncChromiumMonitor(client);
} /* SProcVncChromiumMonitor */
#endif /* CHROMIUM */

static int
SProcVncConnection(ClientPtr client)
{
    REQUEST(xVncConnectionReq);
    register char 	n;


    swaps(&stuff->length, n);
    REQUEST_SIZE_MATCH(xVncConnectionReq);
    swaps(&stuff->sock, n);
    swaps(&stuff->accept,n);
    return ProcVncConnection(client);
} /* SProcVncConnection */

static int
SProcVncDispatch(ClientPtr client)
{
    REQUEST(xReq);

    switch (stuff->data)
    {
	case X_VncQueryVersion:
	    return SProcVncQueryVersion(client);
	case X_VncSelectNotify:
	    return SProcVncSelectNotify(client);
	case X_VncConnection:
	    return SProcVncConnection(client);
	case X_VncListConnections:
	    return SProcVncListConnections(client);
#ifdef CHROMIUM
	case X_VncChromiumStart:
	    return SProcVncChromiumStart(client);
	case X_VncChromiumMonitor:
	    return SProcVncChromiumMonitor(client);
#endif
	default:
	    return BadRequest;
    }
} /* SProcVncDispatch */

static void 
SwapVncConnectedEvent(xVncConnectedEvent *from, xVncConnectedEvent *to)
{
    to->type = from->type;
    to->detail = from->detail;
    cpswaps(from->sequenceNumber, to->sequenceNumber);
    cpswapl(from->connected, to->connected);
}

#ifdef CHROMIUM
static void 
SwapVncChromiumConnectedEvent(xVncConnectedEvent *from, xVncConnectedEvent *to)
{
    to->type = from->type;
    to->detail = from->detail;
    cpswaps(from->sequenceNumber, to->sequenceNumber);
    cpswapl(from->connected, to->connected);
}
#endif

static void 
SwapVncDisconnectedEvent(xVncConnectedEvent *from, xVncConnectedEvent *to)
{
    to->type = from->type;
    to->detail = from->detail;
    cpswaps(from->sequenceNumber, to->sequenceNumber);
    cpswapl(from->connected, to->connected);
}

int
GenerateVncConnectedEvent(int sock)
{
    VncNotifyListPtr pn;

    pn = (VncNotifyListPtr)LookupIDByType(faked, VncNotifyList);

    while (pn)
      {
	if (pn->client) 
	{
    	  xVncConnectedEvent conn;
    	  SOCKLEN_T peer_len;
    	  struct sockaddr_in peer;

    	  conn.type = VncEventBase + XVncConnected;
    	  conn.sequenceNumber = pn->client->sequence;
    	  conn.connected = sock;

    	  peer_len = sizeof(peer);
    	  if (getpeername(sock, (struct sockaddr *) &peer, &peer_len) == -1)
		conn.ipaddress = 0;
   	  else
		conn.ipaddress = (CARD32)peer.sin_addr.s_addr;

	  (void) TryClientEvents(pn->client, (xEventPtr)&conn, 1, NoEventMask,
				 NoEventMask, NullGrab);
	}
	pn = pn->next;
    }

    return 1;
}

#ifdef CHROMIUM
int
GenerateVncChromiumConnectedEvent(int sock)
{
    VncNotifyListPtr pn;

    pn = (VncNotifyListPtr)LookupIDByType(faked, VncNotifyList);

    while (pn)
      {
	if (pn->client) 
	{
    	  xVncConnectedEvent conn;
    	  SOCKLEN_T peer_len;
    	  struct sockaddr_in peer;

    	  conn.type = VncEventBase + XVncChromiumConnected;
    	  conn.sequenceNumber = pn->client->sequence;
    	  conn.connected = sock;

    	  peer_len = sizeof(peer);
    	  if (getpeername(sock, (struct sockaddr *)&peer, &peer_len) == -1)
		conn.ipaddress = 0;
   	  else
		conn.ipaddress = (CARD32)peer.sin_addr.s_addr;

	  (void) TryClientEvents(pn->client, (xEventPtr)&conn, 1, NoEventMask,
				 NoEventMask, NullGrab);
	}
	pn = pn->next;
    }

    return 1;
}
#endif /* CHROMIUM */

int
GenerateVncDisconnectedEvent(int sock)
{
    VncNotifyListPtr pn;

    pn = (VncNotifyListPtr)LookupIDByType(faked, VncNotifyList);

    while (pn)
      {
	if (pn->client) 
	{
    	  xVncDisconnectedEvent conn;
    	  conn.type = VncEventBase + XVncDisconnected;
    	  conn.sequenceNumber = pn->client->sequence;
    	  conn.connected = sock;
	  (void) TryClientEvents(pn->client, (xEventPtr)&conn, 1, NoEventMask,
				 NoEventMask, NullGrab);
	}
	pn = pn->next;
    }

    return 1;
}

static void
VncResetProc(ExtensionEntry *extEntry)
{
} /* VncResetProc */

int
VncSelectNotify(
  ClientPtr client,
  BOOL onoff
){
  VncNotifyListPtr pn,tpn,fpn;

  if (!faked)
	faked = FakeClientID(client->index);

  pn = (VncNotifyListPtr)LookupIDByType(faked, VncNotifyList);

  if (!onoff && !pn) return Success;

  if (!pn) 
    {
      if (!(tpn = (VncNotifyListPtr)xalloc(sizeof(VncNotifyListRec))))
	return BadAlloc;
      tpn->next = (VncNotifyListPtr)NULL;
      tpn->client = (ClientPtr)NULL;
      if (!AddResource(faked, VncNotifyList, tpn))
	{
	  xfree(tpn);
	  return BadAlloc;
	}
    }
  else
    {
      fpn = (VncNotifyListPtr)NULL;
      tpn = pn;

      /* FIXME - NEED TO HAVE AN OPTION FOR SINGLE CLIENT ONLY!!! */

      while (tpn)
	{
	  if (tpn->client == client) 
	    {
	      if (!onoff) tpn->client = (ClientPtr)NULL;
	      return Success;
	    }
	  if (!tpn->client) fpn = tpn; /* TAKE NOTE OF FREE ENTRY */
	  tpn = tpn->next;
	}

      /* IF TUNNING OFF, THEN JUST RETURN */

      if (!onoff) return Success;

      /* IF ONE ISN'T FOUND THEN ALLOCATE ONE AND LINK IT INTO THE LIST */

      if (fpn)
	{
	  tpn = fpn;
	}
      else
	{
	  if (!(tpn = (VncNotifyListPtr)xalloc(sizeof(VncNotifyListRec))))
	    return BadAlloc;
	  tpn->next = pn->next;
	  pn->next = tpn;
	}
    }

  /* INIT CLIENT PTR IN CASE WE CAN'T ADD RESOURCE */
  /* ADD RESOURCE SO THAT IF CLIENT EXITS THE CLIENT PTR WILL BE CLEARED */

  tpn->client = (ClientPtr)NULL;

  AddResource(faked, VncNotifyList, tpn);

  tpn->client = client;
  return Success;

}

static int
VncDestroyNotifyList(pointer pn, XID id)
{
  VncNotifyListPtr cpn = (VncNotifyListPtr) pn;

  cpn->client = NULL;

  return Success;
}

static Bool
CreateResourceTypes(void)
{
  if (VncResourceGeneration == serverGeneration) return TRUE;

  VncResourceGeneration = serverGeneration;

  if (!(VncNotifyList = CreateNewResourceType(VncDestroyNotifyList)))
    {
      ErrorF("CreateResourceTypes: failed to allocate vnc notify list resource.\n");
      return FALSE;
    }

  return TRUE;

}

#if XFREE86VNC
static unsigned long vncCreateScreenResourcesIndex;
static unsigned long vncExtGeneration = 0;
extern Bool VNCInit(ScreenPtr pScreen, unsigned char *FBStart);

/* copied from miscrinit.c */
typedef struct
{
    pointer pbits; /* pointer to framebuffer */
    int width;    /* delta to add to a framebuffer addr to move one row down */
} miScreenInitParmsRec, *miScreenInitParmsPtr;

static Bool
vncCreateScreenResources(ScreenPtr pScreen)
{
  int ret = TRUE;
  CreateScreenResourcesProcPtr CreateScreenResources =
    (CreateScreenResourcesProcPtr)(pScreen->devPrivates[vncCreateScreenResourcesIndex].ptr);
  miScreenInitParmsPtr pScrInitParms;

  pScrInitParms = (miScreenInitParmsPtr)pScreen->devPrivate;

  if ( pScreen->CreateScreenResources != vncCreateScreenResources ) {
    /* Can't find hook we are hung on */
	xf86DrvMsg(pScreen->myNum, X_WARNING /* X_ERROR */,
		  "vncCreateScreenResources %p called when not in pScreen->CreateScreenResources %p n",
		   vncCreateScreenResources, pScreen->CreateScreenResources );
  }

  /* Now do our stuff */
  VNCInit(pScreen, pScrInitParms->pbits);

  /* Unhook this function ... */
  pScreen->CreateScreenResources = CreateScreenResources;
  pScreen->devPrivates[vncCreateScreenResourcesIndex].ptr = NULL;

  /* ... and call the previous CreateScreenResources fuction, if any */
  if (NULL!=pScreen->CreateScreenResources) {
    ret = (*pScreen->CreateScreenResources)(pScreen);
  }

#ifdef DEBUG
  ErrorF("vncCreateScreenResources() returns %d\n", ret);
#endif
  return (ret);
}
#endif

void
VncExtensionInit(void)
{
    ExtensionEntry	*extEntry;

#if XFREE86VNC
    if (vncExtGeneration != serverGeneration) {
	unsigned int i;

	vncExtGeneration = serverGeneration;

    	if ( ((vncCreateScreenResourcesIndex = AllocateScreenPrivateIndex()) < 0) ||
	     ((vncScreenPrivateIndex = AllocateScreenPrivateIndex()) < 0) ||
	     ((rfbGCIndex = AllocateGCPrivateIndex()) < 0) )
		return;

    	for (i = 0 ; i < screenInfo.numScreens; i++) 
	{
      	    screenInfo.screens[i]->devPrivates[vncCreateScreenResourcesIndex].ptr
	 		= (void*)(xf86Screens[i]->pScreen->CreateScreenResources);
      	    xf86Screens[i]->pScreen->CreateScreenResources = vncCreateScreenResources;
    	}

	gethostname(rfbThisHost, 255);
    }
#endif

    if (!CreateResourceTypes())
	    return;

    extEntry = AddExtension(VNC_EXTENSION_NAME,
			    XVncNumberEvents, XVncNumberErrors,
			    ProcVncDispatch, SProcVncDispatch,
                            VncResetProc, StandardMinorOpcode);

    VncErrorBase = extEntry->errorBase;
    VncEventBase = extEntry->eventBase;

    EventSwapVector[VncEventBase + XVncConnected] =
	(EventSwapPtr)SwapVncConnectedEvent;
    EventSwapVector[VncEventBase + XVncDisconnected] =
	(EventSwapPtr)SwapVncDisconnectedEvent;
#ifdef CHROMIUM
    EventSwapVector[VncEventBase + XVncChromiumConnected] =
	(EventSwapPtr)SwapVncChromiumConnectedEvent;
#endif
} /* VncExtensionInit */
