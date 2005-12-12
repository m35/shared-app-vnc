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

#ifndef _VNC_H
#define _VNC_H

#include <netinet/in.h>

/* constants that server, library, and application all need */

#define VNC_USER_UNDEFINED		0
#define VNC_USER_CONNECT		1
#define VNC_USER_DISCONNECT		2

#define XVncNumberEvents		3
#define XVncNumberErrors		0

/* event offsets */
#define XVncConnected			 0
#define XVncDisconnected		 1
#define XVncChromiumConnected		 2
    
#ifndef _VNC_SERVER

typedef struct {
	in_addr_t	ipaddress;
} VncConnectionList;

_XFUNCPROTOBEGIN

Status XVncQueryExtension (
    Display *dpy,
    int *major_version_return,
    int *minor_version_return);

Status XVncSelectNotify (
    Display *dpy,
    Bool onoff);

Status XVncConnection (
    Display *dpy,
    int sock,
    int accept);

int XVncGetEventBase(
    Display *dpy);

VncConnectionList * XVncListConnections (
    Display *dpy,
    int *num);

Status XVncChromiumStart (
    Display *dpy,
    unsigned int ipaddress,
    unsigned int port);

Status XVncChromiumMonitor (
    Display *dpy,
    unsigned int cr_window,
    XID window );

_XFUNCPROTOEND

typedef struct {
    int type;		      /* event base + XSecurityAuthorizationRevoked */
    unsigned long serial;     /* # of last request processed by server */
    Bool send_event;	      /* true if this came from a SendEvent request */
    Display *display;	      /* Display the event was read from */
    CARD32 connected;	      /* connected socket number */
    CARD32 ipaddress;         /* IP address of connected host */
} XVncConnectedEvent;

typedef struct {
    int type;		      /* event base + XSecurityAuthorizationRevoked */
    unsigned long serial;     /* # of last request processed by server */
    Bool send_event;	      /* true if this came from a SendEvent request */
    Display *display;	      /* Display the event was read from */
    CARD32 connected;	      /* connected socket number */
} XVncDisconnectedEvent;

#endif /* _VNC_SERVER */

#endif /* _VNC_H */
