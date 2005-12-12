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

#ifndef _VNCSTR_H
#define _VNCSTR_H

#include <X11/extensions/vnc.h>

#define VNC_EXTENSION_NAME		"VNC"
#define VNC_MAJOR_VERSION		2
#define VNC_MINOR_VERSION		0

#define X_VncQueryVersion		0
#define X_VncSelectNotify		1
#define X_VncConnection			2
#define X_VncListConnections		3
#define X_VncChromiumStart		4
#define X_VncChromiumMonitor		5

typedef struct {
    CARD8       reqType;
    CARD8       vncReqType;
    CARD16      length B16;
    CARD16      majorVersion B16;
    CARD16      minorVersion B16;
} xVncQueryVersionReq;
#define sz_xVncQueryVersionReq 	8

typedef struct {
    CARD8   type;
    CARD8   pad0;
    CARD16  sequenceNumber B16;
    CARD32  length	 B32;
    CARD16  majorVersion B16;
    CARD16  minorVersion B16;
    CARD32  pad1	 B32;
    CARD32  pad2	 B32;
    CARD32  pad3	 B32;
    CARD32  pad4	 B32;
    CARD32  pad5	 B32;
 } xVncQueryVersionReply;
#define sz_xVncQueryVersionReply  	32

typedef struct {
    CARD8       reqType;
    CARD8       vncReqType;
    CARD16      length B16;
    BOOL	onoff;
    CARD8       pad0;
    CARD16      pad1 B16;
} xVncSelectNotifyReq;
#define sz_xVncSelectNotifyReq 	8

typedef struct {
    CARD8   type;
    CARD8   pad0;
    CARD16  sequenceNumber B16;
    CARD32  length	 B32;
    CARD32  pad1	 B32;
    CARD32  pad2	 B32;
    CARD32  pad3	 B32;
    CARD32  pad4	 B32;
    CARD32  pad5	 B32;
    CARD32  pad6	 B32;
 } xVncSelectNotifyReply;
#define sz_xVncSelectNotifyReply  	32

typedef struct {
    CARD8       reqType;
    CARD8       vncReqType;
    CARD16      length B16;
    CARD16      sock B16;
    CARD16      accept B16;
} xVncConnectionReq;
#define sz_xVncConnectionReq 	8

typedef struct {
    CARD8   type;
    CARD8   pad0;
    CARD16  sequenceNumber B16;
    CARD32  length	 B32;
    CARD16  sock B16;
    CARD16  accept B16;
    CARD32  pad1	 B32;
    CARD32  pad2	 B32;
    CARD32  pad3	 B32;
    CARD32  pad4	 B32;
    CARD32  pad5	 B32;
 } xVncConnectionReply;
#define sz_xVncConnectionReply  	32

typedef struct {
    CARD8       reqType;
    CARD8       vncReqType;
    CARD16      length B16;
    CARD16      pad0 B16;
    CARD16      pad1 B16;
} xVncListConnectionsReq;
#define sz_xVncListConnectionsReq 	8

typedef struct {
    CARD8   type;
    CARD8   pad0;
    CARD16  sequenceNumber B16;
    CARD32  length	 B32;
    CARD32  count	 B32;
    CARD32  pad2	 B32;
    CARD32  pad3	 B32;
    CARD32  pad4	 B32;
    CARD32  pad5	 B32;
    CARD32  pad6	 B32;
 } xVncListConnectionsReply;
#define sz_xVncListConnectionsReply  	32

typedef struct {
    CARD32	ipaddress;
    CARD32	pad1;
} xVncConnectionListInfo;
#define sz_xVncConnectionListInfo	8

typedef struct {
    CARD8       reqType;
    CARD8       vncReqType;
    CARD16      length B16;
    CARD32      port B32;
    CARD32      ipaddress B32;
} xVncChromiumStartReq;
#define sz_xVncChromiumStartReq 	12

typedef struct {
    CARD8   type;
    CARD8   pad0;
    CARD16  sequenceNumber B16;
    CARD32  length	 B32;
    CARD32  pad1	 B32;
    CARD32  pad2	 B32;
    CARD32  pad3	 B32;
    CARD32  pad4	 B32;
    CARD32  pad5	 B32;
    CARD32  pad6	 B32;
 } xVncChromiumStartReply;
#define sz_xVncChromiumStartReply  	32

typedef struct {
    CARD8       reqType;
    CARD8       vncReqType;
    CARD16      length B16;
    CARD32      windowid B32;
    CARD32	cr_windowid B32;
} xVncChromiumMonitorReq;
#define sz_xVncChromiumMonitorReq 	12

typedef struct {
    CARD8   type;
    CARD8   pad0;
    CARD16  sequenceNumber B16;
    CARD32  length	 B32;
    CARD32  pad1	 B32;
    CARD32  pad2	 B32;
    CARD32  pad3	 B32;
    CARD32  pad4	 B32;
    CARD32  pad5	 B32;
    CARD32  pad6	 B32;
 } xVncChromiumMonitorReply;
#define sz_xVncChromiumMonitorReply  	32

typedef struct _xVncConnectedEvent {
    BYTE	type;
    BYTE	detail;
    CARD16	sequenceNumber B16;
    CARD32	connected B32;
    CARD32	ipaddress B32;
    CARD32	pad1	 B32;
    CARD32	pad2	 B32;
    CARD32	pad3	 B32;
    CARD32	pad4	 B32;
    CARD32	pad5	 B32;
} xVncConnectedEvent;
#define sz_xVncConnectedEvent 32

typedef struct _xVncDisconnectedEvent {
    BYTE	type;
    BYTE	detail;
    CARD16	sequenceNumber B16;
    CARD32	connected B32;
    CARD32	pad0     B32;
    CARD32	pad1	 B32;
    CARD32	pad2	 B32;
    CARD32	pad3	 B32;
    CARD32	pad4	 B32;
    CARD32	pad5	 B32;
} xVncDisconnectedEvent;
#define sz_xVncDisconnectedEvent 32

#endif /* _VNCSTR_H */
