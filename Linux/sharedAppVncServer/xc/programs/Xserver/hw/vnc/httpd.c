/*
 * httpd.c - a simple HTTP server
 *
 * Modified for XFree86 4.x by Alan Hourihane <alanh@fairlite.demon.co.uk>
 */

/*
 *  Copyright (C) 2002 Constantin Kaplinsky.  All Rights Reserved.
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

#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <errno.h>
#include <arpa/inet.h>
#include <pwd.h>
#include <netdb.h>

#ifndef USE_LIBWRAP
#define USE_LIBWRAP 0
#endif
#if USE_LIBWRAP
#include <tcpd.h>
#endif

#include "rfb.h"

#define NOT_FOUND_STR "HTTP/1.0 404 Not found\r\n\r\n" \
    "<HEAD><TITLE>File Not Found</TITLE></HEAD>\n" \
    "<BODY><H1>File Not Found</H1></BODY>\n"

#define OK_STR "HTTP/1.0 200 OK\r\n\r\n"

static void httpProcessInput(ScreenPtr pScreen);
static Bool compareAndSkip(char **ptr, const char *str);
static Bool parseParams(const char *request, char *result, int max_bytes);
static Bool validateString(char *str);

/*
 * httpInitSockets sets up the TCP socket to listen for HTTP connections.
 */

Bool
httpInitSockets(ScreenPtr pScreen)
{
    VNCSCREENPTR(pScreen);

    if (!pVNC->httpDir)
	return FALSE;

    pVNC->buf_filled = 0;

    if (pVNC->httpPort == 0) {
	pVNC->httpPort = 5800 + atoi(display) + pScreen->myNum;
    }

    if ((pVNC->httpListenSock = ListenOnTCPPort(pScreen, pVNC->httpPort)) < 0) {
	rfbLog("ListenOnTCPPort %d failed\n",pVNC->httpPort);
	pVNC->httpPort = 0;
	return FALSE;
    }

    rfbLog("Listening for HTTP connections on TCP port %d\n", pVNC->httpPort);
    rfbLog("  URL http://%s:%d\n",rfbThisHost,pVNC->httpPort);

    AddEnabledDevice(pVNC->httpListenSock);

    return TRUE;
}


/*
 * httpCheckFds is called from ProcessInputEvents to check for input on the
 * HTTP socket(s).  If there is input to process, httpProcessInput is called.
 */

void
httpCheckFds(ScreenPtr pScreen)
{
    VNCSCREENPTR(pScreen);
    int nfds;
    fd_set fds;
    struct timeval tv;
    struct sockaddr_in addr;
    SOCKLEN_T addrlen = sizeof(addr);

    if (!pVNC->httpDir)
	return;

    FD_ZERO(&fds);
    FD_SET(pVNC->httpListenSock, &fds);
    if (pVNC->httpSock >= 0) {
	FD_SET(pVNC->httpSock, &fds);
    }
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    nfds = select(max(pVNC->httpSock,pVNC->httpListenSock) + 1, &fds, NULL, NULL, &tv);
    if (nfds == 0) {
	return;
    }
    if (nfds < 0) {
	if (errno != EINTR) 
		rfbLogPerror("httpCheckFds: select");
	return;
    }

    if ((pVNC->httpSock >= 0) && FD_ISSET(pVNC->httpSock, &fds)) {
	httpProcessInput(pScreen);
    }

    if (FD_ISSET(pVNC->httpListenSock, &fds)) {
	int flags;

	if (pVNC->httpSock >= 0) close(pVNC->httpSock);

	if ((pVNC->httpSock = accept(pVNC->httpListenSock,
			       (struct sockaddr *)&addr, &addrlen)) < 0) {
	    rfbLogPerror("httpCheckFds: accept");
	    return;
	}

#if USE_LIBWRAP
	if (!hosts_ctl("Xvnc", STRING_UNKNOWN, inet_ntoa(addr.sin_addr),
		       STRING_UNKNOWN)) {
	    rfbLog("Rejected HTTP connection from client %s\n",
		   inet_ntoa(addr.sin_addr));
  	    close(pVNC->httpSock);
  	    pVNC->httpSock = -1;
  	    return;
  	}
#endif

	flags = fcntl (pVNC->httpSock, F_GETFL);

	if (flags == -1 ||
	fcntl (pVNC->httpSock, F_SETFL, flags | O_NONBLOCK) == -1) {
	    rfbLogPerror("httpCheckFds: fcntl");
	    close (pVNC->httpSock);
	    pVNC->httpSock = -1;
	    return;
	}

	AddEnabledDevice(pVNC->httpSock);
    }
}


static void
httpCloseSock(ScreenPtr pScreen)
{
    VNCSCREENPTR(pScreen);
    close(pVNC->httpSock);
    RemoveEnabledDevice(pVNC->httpSock);
    pVNC->httpSock = -1;
    pVNC->buf_filled = 0;
}


/*
 * httpProcessInput is called when input is received on the HTTP socket.
 */

static void
httpProcessInput(ScreenPtr pScreen)
{
    VNCSCREENPTR(pScreen);
    struct sockaddr_in addr;
    SOCKLEN_T addrlen = sizeof(addr);
    char fullFname[512];
    char params[1024];
    char *ptr;
    char *fname;
    int maxFnameLen;
    int fd;
    Bool performSubstitutions = FALSE;
    char str[256];
    struct passwd *user = getpwuid(getuid());
  
    if (strlen(pVNC->httpDir) > 255) {
	rfbLog("-httpd directory too long\n");
  	httpCloseSock(pScreen);
	return;
    }
    strcpy(fullFname, pVNC->httpDir);
    fname = &fullFname[strlen(fullFname)];
    maxFnameLen = 511 - strlen(fullFname);
  
    /* Read data from the HTTP client until we get a complete request. */
    while (1) {
	ssize_t got = read (pVNC->httpSock, pVNC->buf + pVNC->buf_filled,
			    sizeof (pVNC->buf) - pVNC->buf_filled - 1);

	if (got <= 0) {
	    if (got == 0) {
		rfbLog("httpd: premature connection close\n");
	    } else {
		if (errno == EAGAIN) {
		    return;
		}
		rfbLogPerror("httpProcessInput: read");
	    }
	    httpCloseSock(pScreen);
  	    return;
	}
  
	pVNC->buf_filled += got;
	pVNC->buf[pVNC->buf_filled] = '\0';
  
	/* Is it complete yet (is there a blank line)? */
	if (strstr (pVNC->buf, "\r\r") || strstr (pVNC->buf, "\n\n") ||
	    strstr (pVNC->buf, "\r\n\r\n") || strstr (pVNC->buf, "\n\r\n\r"))
	    break;
    }
  
    /* Process the request. */
    if (strncmp(pVNC->buf, "GET ", 4)) {
	rfbLog("httpd: no GET line\n");
	httpCloseSock(pScreen);
	return;
    } else {
	/* Only use the first line. */
	pVNC->buf[strcspn(pVNC->buf, "\n\r")] = '\0';
    }
  
    if (strlen(pVNC->buf) > maxFnameLen) {
	rfbLog("httpd: GET line too long\n");
	httpCloseSock(pScreen);
	return;
    }
  
    if (sscanf(pVNC->buf, "GET %s HTTP/1.0", fname) != 1) {
	rfbLog("httpd: couldn't parse GET line\n");
	httpCloseSock(pScreen);
	return;
    }
  
    if (fname[0] != '/') {
	rfbLog("httpd: filename didn't begin with '/'\n");
	WriteExact(pVNC->httpSock, NOT_FOUND_STR, strlen(NOT_FOUND_STR));
	httpCloseSock(pScreen);
	return;
    }
  
    if (strchr(fname+1, '/') != NULL) {
	rfbLog("httpd: asking for file in other directory\n");
	WriteExact(pVNC->httpSock, NOT_FOUND_STR, strlen(NOT_FOUND_STR));
	httpCloseSock(pScreen);
	return;
    }
  
    getpeername(pVNC->httpSock, (struct sockaddr *)&addr, &addrlen);
    rfbLog("httpd: get '%s' for %s\n", fname+1,
	   inet_ntoa(addr.sin_addr));

    /* Extract parameters from the URL string if necessary */

    params[0] = '\0';
    ptr = strchr(fname, '?');
    if (ptr != NULL) {
	*ptr = '\0';
	if (!parseParams(&ptr[1], params, 1024)) {
	    params[0] = '\0';
	    rfbLog("httpd: bad parameters in the URL\n");
	}
    }

    /* If we were asked for '/', actually read the file index.vnc */

    if (strcmp(fname, "/") == 0) {
	strcpy(fname, "/index.vnc");
	rfbLog("httpd: defaulting to '%s'\n", fname+1);
    }

    /* Substitutions are performed on files ending .vnc */

    if (strlen(fname) >= 4 && strcmp(&fname[strlen(fname)-4], ".vnc") == 0) {
	performSubstitutions = TRUE;
    }

    /* Open the file */

    if ((fd = open(fullFname, O_RDONLY)) < 0) {
	rfbLogPerror("httpProcessInput: open");
	WriteExact(pVNC->httpSock, NOT_FOUND_STR, strlen(NOT_FOUND_STR));
	httpCloseSock(pScreen);
	return;
    }

    WriteExact(pVNC->httpSock, OK_STR, strlen(OK_STR));

    while (1) {
	int n = read(fd, pVNC->buf, HTTP_BUF_SIZE-1);
	if (n < 0) {
	    rfbLogPerror("httpProcessInput: read");
	    close(fd);
	    httpCloseSock(pScreen);
	    return;
	}

	if (n == 0)
	    break;

	if (performSubstitutions) {

	    /* Substitute $WIDTH, $HEIGHT, etc with the appropriate values.
	       This won't quite work properly if the .vnc file is longer than
	       HTTP_BUF_SIZE, but it's reasonable to assume that .vnc files will
	       always be short. */

	    char *ptr = pVNC->buf;
	    char *dollar;
	    pVNC->buf[n] = 0; /* make sure it's null-terminated */

	    while ((dollar = strchr(ptr, '$'))) {
		WriteExact(pVNC->httpSock, ptr, (dollar - ptr));

		ptr = dollar;

		if (compareAndSkip(&ptr, "$WIDTH")) {

		    sprintf(str, "%d", pVNC->width);
		    WriteExact(pVNC->httpSock, str, strlen(str));

		} else if (compareAndSkip(&ptr, "$HEIGHT")) {

		    sprintf(str, "%d", pVNC->height);
		    WriteExact(pVNC->httpSock, str, strlen(str));

		} else if (compareAndSkip(&ptr, "$APPLETWIDTH")) {

		    sprintf(str, "%d", pVNC->width);
		    WriteExact(pVNC->httpSock, str, strlen(str));

		} else if (compareAndSkip(&ptr, "$APPLETHEIGHT")) {

		    sprintf(str, "%d", pVNC->height + 32);
		    WriteExact(pVNC->httpSock, str, strlen(str));

		} else if (compareAndSkip(&ptr, "$PORT")) {

		    sprintf(str, "%d", pVNC->rfbPort);
		    WriteExact(pVNC->httpSock, str, strlen(str));

		} else if (compareAndSkip(&ptr, "$DESKTOP")) {

		    WriteExact(pVNC->httpSock, desktopName, strlen(desktopName));

		} else if (compareAndSkip(&ptr, "$DISPLAY")) {

		    sprintf(str, "%s:%s", rfbThisHost, display);
		    WriteExact(pVNC->httpSock, str, strlen(str));

		} else if (compareAndSkip(&ptr, "$USER")) {

		    if (user) {
			WriteExact(pVNC->httpSock, user->pw_name,
				   strlen(user->pw_name));
		    } else {
			WriteExact(pVNC->httpSock, "?", 1);
		    }

		} else if (compareAndSkip(&ptr, "$PARAMS")) {

		    if (params[0] != '\0')
			WriteExact(pVNC->httpSock, params, strlen(params));

		} else {
		    if (!compareAndSkip(&ptr, "$$"))
			ptr++;

		    if (WriteExact(pVNC->httpSock, "$", 1) < 0) {
			close(fd);
			httpCloseSock(pScreen);
			return;
		    }
		}
	    }
	    if (WriteExact(pVNC->httpSock, ptr, (&pVNC->buf[n] - ptr)) < 0)
		break;

	} else {

	    /* For files not ending .vnc, just write out the buffer */

	    if (WriteExact(pVNC->httpSock, pVNC->buf, n) < 0)
		break;
	}
    }

    close(fd);
    httpCloseSock(pScreen);
}


static Bool
compareAndSkip(char **ptr, const char *str)
{
    if (strncmp(*ptr, str, strlen(str)) == 0) {
	*ptr += strlen(str);
	return TRUE;
    }

    return FALSE;
}

/*
 * Parse the request tail after the '?' character, and format a sequence
 * of <param> tags for inclusion into an HTML page with embedded applet.
 */

static Bool
parseParams(const char *request, char *result, int max_bytes)
{
    char param_request[128];
    char param_formatted[196];
    const char *tail;
    char *delim_ptr;
    char *value_str;
    int cur_bytes, len;

    result[0] = '\0';
    cur_bytes = 0;

    tail = request;
    for (;;) {
	/* Copy individual "name=value" string into a buffer */
	delim_ptr = strchr((char *)tail, '&');
	if (delim_ptr == NULL) {
	    if (strlen(tail) >= sizeof(param_request)) {
		return FALSE;
	    }
	    strcpy(param_request, tail);
	} else {
	    len = delim_ptr - tail;
	    if (len >= sizeof(param_request)) {
		return FALSE;
	    }
	    memcpy(param_request, tail, len);
	    param_request[len] = '\0';
	}

	/* Split the request into parameter name and value */
	value_str = strchr(&param_request[1], '=');
	if (value_str == NULL) {
	    return FALSE;
	}
	*value_str++ = '\0';
	if (strlen(value_str) == 0) {
	    return FALSE;
	}

	/* Validate both parameter name and value */
	if (!validateString(param_request) || !validateString(value_str)) {
	    return FALSE;
	}

	/* Prepare HTML-formatted representation of the name=value pair */
	len = sprintf(param_formatted,
		      "<PARAM NAME=\"%s\" VALUE=\"%s\">\n",
		      param_request, value_str);
	if (cur_bytes + len + 1 > max_bytes) {
	    return FALSE;
	}
	strcat(result, param_formatted);
	cur_bytes += len;

	/* Go to the next parameter */
	if (delim_ptr == NULL) {
	    break;
	}
	tail = delim_ptr + 1;
    }
    return TRUE;
}

/*
 * Check if the string consists only of alphanumeric characters, '+'
 * signs, underscores, and dots. Replace all '+' signs with spaces.
 */

static Bool
validateString(char *str)
{
    char *ptr;

    for (ptr = str; *ptr != '\0'; ptr++) {
	if (!isalnum(*ptr) && *ptr != '_' && *ptr != '.') {
	    if (*ptr == '+') {
		*ptr = ' ';
	    } else {
		return FALSE;
	    }
	}
    }
    return TRUE;
}

