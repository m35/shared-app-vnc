/*
 *  Copyright (C) 2004 Alan Hourihane.  All Rights Reserved.
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

#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include "rfb.h"

typedef struct rdpClientRec {
	ScreenPtr pScreen;
} rdpClientRec, *rdpClientPtr;

typedef struct rdpInRec {
	char version;
	char pad;
	short length; 
	char hdrlen;
	unsigned char pdu;
} rdpInRec, *rdpInPtr;

static void
rdpCloseSock(ScreenPtr pScreen)
{
    VNCSCREENPTR(pScreen);
    close(pVNC->rdpListenSock);
    RemoveEnabledDevice(pVNC->rdpListenSock);
    pVNC->rdpListenSock = -1;
}

/*
 * rdpNewClient is called when a new connection has been made by whatever
 * means.
 */

static rdpClientPtr
rdpNewClient(ScreenPtr pScreen, int sock)
{
    rdpInRec in;
    rdpClientPtr cl;
    BoxRec box;
    struct sockaddr_in addr;
    SOCKLEN_T addrlen = sizeof(struct sockaddr_in);
    VNCSCREENPTR(pScreen);
    int i;

    cl = (rdpClientPtr)xalloc(sizeof(rdpClientRec));

    cl->pScreen = pScreen;

    in.version = 3;
    in.pad = 0;
    in.length = 0x600; /* big endian */
    in.hdrlen = 0x00; 
    in.pdu = 0xCC; 

    if (WriteExact(sock, (char *)&in, sizeof(rdpInRec)) < 0) {
	rfbLogPerror("rfbNewClient: write");
	rdpCloseSock(pScreen);
	return NULL;
    }

    return cl;
}
/*
 * rdpCheckFds is called from ProcessInputEvents to check for input on the
 * HTTP socket(s).  If there is input to process, rdpProcessInput is called.
 */

void
rdpCheckFds(ScreenPtr pScreen)
{
    VNCSCREENPTR(pScreen);
    int nfds;
    fd_set fds;
    struct timeval tv;
    struct sockaddr_in addr;
    int sock;
    const int one =1;
    SOCKLEN_T addrlen = sizeof(addr);

    FD_ZERO(&fds);
    FD_SET(pVNC->rdpListenSock, &fds);
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    nfds = select(pVNC->rdpListenSock + 1, &fds, NULL, NULL, &tv);
    if (nfds == 0) {
	return;
    }
    if (nfds < 0) {
	if (errno != EINTR) 
		rfbLogPerror("httpCheckFds: select");
	return;
    }

    if (pVNC->rdpListenSock != -1 && FD_ISSET(pVNC->rdpListenSock, &fds)) {

	if ((sock = accept(pVNC->rdpListenSock,
			   (struct sockaddr *)&addr, &addrlen)) < 0) {
	    rfbLogPerror("rdpCheckFds: accept");
	    return;
	}

	if (fcntl(sock, F_SETFL, O_NONBLOCK) < 0) {
	    rfbLogPerror("rdpCheckFds: fcntl");
	    close(sock);
	    return;
	}

	if (setsockopt(sock, IPPROTO_TCP, TCP_NODELAY,
		       (char *)&one, sizeof(one)) < 0) {
	    rfbLogPerror("rdpCheckFds: setsockopt");
	    close(sock);
	    return;
	}

	rfbLog("\n");

	rfbLog("Got RDP connection from client %s\n", inet_ntoa(addr.sin_addr));

	AddEnabledDevice(sock);

	rdpNewClient(pScreen, sock);
    }
}
