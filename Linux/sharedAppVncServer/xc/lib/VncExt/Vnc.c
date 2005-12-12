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

#include <X11/Xlibint.h>
#include <stdio.h>
#include "Xext.h"
#include "extutil.h"
#include "vncstr.h"

static XExtensionInfo _Vnc_info_data;
static XExtensionInfo *Vnc_info = &_Vnc_info_data;
static char *Vnc_extension_name = VNC_EXTENSION_NAME;

#define VncCheckExtension(dpy,i,val) \
  XextCheckExtension (dpy, i, Vnc_extension_name, val)
#define VncSimpleCheckExtension(dpy,i) \
  XextSimpleCheckExtension (dpy, i, Vnc_extension_name)

#define VncGetReq(name,req,info) GetReq (name, req); \
        req->reqType = info->codes->major_opcode; \
        req->vncReqType = X_##name;

/*****************************************************************************
 *                                                                           *
 *			   private utility routines                          *
 *                                                                           *
 *****************************************************************************/

/*
 * find_display - locate the display info block
 */
static int close_display();
static Bool     wire_to_event();
static Status   event_to_wire();
static char *error_string();
static XExtensionHooks Vnc_extension_hooks = {
    NULL,                               /* create_gc */
    NULL,                               /* copy_gc */
    NULL,                               /* flush_gc */
    NULL,                               /* free_gc */
    NULL,                               /* create_font */
    NULL,                               /* free_font */
    close_display,                      /* close_display */
    wire_to_event,                      /* wire_to_event */
    event_to_wire,                      /* event_to_wire */
    NULL,                               /* error */
    error_string                        /* error_string */
};

static char    *security_error_list[] = {
    "BadAuthorization"
    "BadAuthorizationProtocol"
};

static XEXT_GENERATE_FIND_DISPLAY (find_display, Vnc_info,
				   Vnc_extension_name, 
				   &Vnc_extension_hooks, 
				   XVncNumberEvents, NULL)

static XEXT_GENERATE_CLOSE_DISPLAY (close_display, Vnc_info)

static 
XEXT_GENERATE_ERROR_STRING(error_string, Vnc_extension_name,
			   XVncNumberErrors, security_error_list)

static Bool     
wire_to_event(dpy, event, wire)
    Display        *dpy;
    XEvent         *event;
    xEvent         *wire;
{
    XExtDisplayInfo *info = find_display(dpy);

    VncCheckExtension (dpy, info, False);

    switch ((wire->u.u.type & 0x7F) - info->codes->first_event)
    {
	case XVncConnected:
	{
	    xVncConnectedEvent *rwire =
		(xVncConnectedEvent *)wire;
	    XVncConnectedEvent *revent = 
		(XVncConnectedEvent *)event;

	  revent->type = rwire->type & 0x7F;
	  revent->serial = _XSetLastRequestRead(dpy,
						(xGenericReply *) wire);
	  revent->send_event = (rwire->type & 0x80) != 0;
	  revent->display = dpy;
	  revent->connected = rwire->connected;
	  revent->ipaddress = rwire->ipaddress;
	  return True;
	}
	case XVncChromiumConnected:
	{
	    xVncConnectedEvent *rwire =
		(xVncConnectedEvent *)wire;
	    XVncConnectedEvent *revent = 
		(XVncConnectedEvent *)event;

	  revent->type = rwire->type & 0x7F;
	  revent->serial = _XSetLastRequestRead(dpy,
						(xGenericReply *) wire);
	  revent->send_event = (rwire->type & 0x80) != 0;
	  revent->display = dpy;
	  revent->connected = rwire->connected;
	  revent->ipaddress = rwire->ipaddress;
	  return True;
	}
	case XVncDisconnected:
	{
	    xVncDisconnectedEvent *rwire =
		(xVncDisconnectedEvent *)wire;
	    XVncDisconnectedEvent *revent = 
		(XVncDisconnectedEvent *)event;

	  revent->type = rwire->type & 0x7F;
	  revent->serial = _XSetLastRequestRead(dpy,
						(xGenericReply *) wire);
	  revent->send_event = (rwire->type & 0x80) != 0;
	  revent->display = dpy;
	  revent->connected = rwire->connected;
	  return True;
	}
    }
    return False;
}

static Status 
event_to_wire(dpy, event, wire)
    Display        *dpy;
    XEvent         *event;
    xEvent         *wire;
{
    XExtDisplayInfo *info = find_display(dpy);

    VncCheckExtension(dpy, info, False);

    switch ((event->type & 0x7F) - info->codes->first_event)
    {
	case XVncConnected:
	{
	    xVncConnectedEvent *rwire =
		(xVncConnectedEvent *)wire;
	    XVncConnectedEvent *revent = 
		(XVncConnectedEvent *)event;
	    rwire->type = revent->type | (revent->send_event ? 0x80 : 0);
	    rwire->sequenceNumber = revent->serial & 0xFFFF;
	    return True;
	}
	case XVncDisconnected:
	{
	    xVncDisconnectedEvent *rwire =
		(xVncDisconnectedEvent *)wire;
	    XVncDisconnectedEvent *revent = 
		(XVncDisconnectedEvent *)event;
	    rwire->type = revent->type | (revent->send_event ? 0x80 : 0);
	    rwire->sequenceNumber = revent->serial & 0xFFFF;
	    return True;
	}
    }
    return False;
}

int XVncGetEventBase(dpy)
    Display *dpy;
{
    XExtDisplayInfo *info = find_display (dpy);

    if (XextHasExtension(info)) {
	return info->codes->first_event;
    } else {
	return -1;
    }
}

/*****************************************************************************
 *                                                                           *
 *		            VNC public interfaces                            *
 *                                                                           *
 *****************************************************************************/

Status XVncQueryExtension (
    Display *dpy,
    int *major_version_return,
    int *minor_version_return)
{
    XExtDisplayInfo *info = find_display (dpy);
    xVncQueryVersionReply rep;
    register xVncQueryVersionReq *req;

    if (!XextHasExtension (info))
        return (Status)0; /* failure */

    LockDisplay (dpy);
    VncGetReq (VncQueryVersion, req, info);
    req->majorVersion = VNC_MAJOR_VERSION;
    req->minorVersion = VNC_MINOR_VERSION;

    if (!_XReply (dpy, (xReply *) &rep, 0, xTrue)) {
	UnlockDisplay (dpy);
	SyncHandle ();
	return (Status)0; /* failure */
    }
    *major_version_return = rep.majorVersion;
    *minor_version_return = rep.minorVersion;
    UnlockDisplay (dpy);

    SyncHandle ();

    if (*major_version_return != VNC_MAJOR_VERSION)
        return (Status)0; /* failure */
    else
        return (Status)1; /* success */
}

Status XVncSelectNotify (
    Display *dpy,
    Bool onoff)
{
    XExtDisplayInfo *info = find_display (dpy);
    xVncSelectNotifyReply rep;
    register xVncSelectNotifyReq *req;

    if (!XextHasExtension (info))
        return (Status)0; /* failure */

    LockDisplay (dpy);
    VncGetReq (VncSelectNotify, req, info);
    req->onoff = onoff;

    if (!_XReply (dpy, (xReply *) &rep, 0, xTrue)) {
	UnlockDisplay (dpy);
	SyncHandle ();
	return (Status)0; /* failure */
    }
    UnlockDisplay (dpy);

    SyncHandle ();

    return (Status)1; /* success */
}

Status XVncConnection (
    Display *dpy,
    int sock,
    int accept)
{
    XExtDisplayInfo *info = find_display (dpy);
    xVncConnectionReply rep;
    register xVncConnectionReq *req;

    if (!XextHasExtension (info))
        return (Status)0; /* failure */

    LockDisplay (dpy);
    VncGetReq (VncConnection, req, info);
    req->sock = sock;
    req->accept = accept;

    if (!_XReply (dpy, (xReply *) &rep, 0, xTrue)) {
	UnlockDisplay (dpy);
	SyncHandle ();
	return (Status)0; /* failure */
    }
    UnlockDisplay (dpy);

    SyncHandle ();

    return (Status)1; /* success */
}

VncConnectionList * XVncListConnections (
    Display *dpy,
    int	*num)
{
    XExtDisplayInfo *info = find_display (dpy);
    xVncListConnectionsReply rep;
    register xVncListConnectionsReq *req;
    VncConnectionList *ret = NULL;
    int i;

    *num = 0;

    if (!XextHasExtension (info))
	return ret; 

    LockDisplay (dpy);
    VncGetReq (VncListConnections, req, info);

    if (!_XReply (dpy, (xReply *) &rep, 0, xFalse)) {
	UnlockDisplay (dpy);
	SyncHandle ();
	return ret; 
    }

    if(rep.count) {
	int size = (rep.count * sizeof(VncConnectionList));

      	if ((ret = Xmalloc(size))) {
	    VncConnectionList Info;
	    int i;

	    for(i = 0; i < rep.count; i++) {
            	_XRead(dpy, (char*)(&Info), sz_xVncConnectionListInfo);
	      	ret[i].ipaddress = Info.ipaddress;	      
	        (*num)++;
	    }
        } else
	_XEatData(dpy, rep.length << 2);
    }

    UnlockDisplay (dpy);

    SyncHandle ();

    return ret; /* success */
}


Status XVncChromiumStart (
    Display *dpy,
    unsigned int ipaddress,
    unsigned int port)
{
    XExtDisplayInfo *info = find_display (dpy);
    xVncChromiumStartReply rep;
    register xVncChromiumStartReq *req;

    if (!XextHasExtension (info))
        return (Status)0; /* failure */

    LockDisplay (dpy);
    VncGetReq (VncChromiumStart, req, info);
    req->ipaddress = ipaddress;
    req->port = port;

    if (!_XReply (dpy, (xReply *) &rep, 0, xTrue)) {
	UnlockDisplay (dpy);
	SyncHandle ();
	return (Status)0; /* failure */
    }
    UnlockDisplay (dpy);

    SyncHandle ();

    return (Status)1; /* success */
}

Status XVncChromiumMonitor (
    Display *dpy,
    unsigned int cr_window,
    XID window )
{
    XExtDisplayInfo *info = find_display (dpy);
    xVncChromiumMonitorReply rep;
    register xVncChromiumMonitorReq *req;

    if (!XextHasExtension (info))
        return (Status)0; /* failure */

    LockDisplay (dpy);
    VncGetReq (VncChromiumMonitor, req, info);
    req->windowid = window;
    req->cr_windowid = cr_window;

    if (!_XReply (dpy, (xReply *) &rep, 0, xTrue)) {
	UnlockDisplay (dpy);
	SyncHandle ();
	return (Status)0; /* failure */
    }
    UnlockDisplay (dpy);

    SyncHandle ();

    return (Status)1; /* success */
}
