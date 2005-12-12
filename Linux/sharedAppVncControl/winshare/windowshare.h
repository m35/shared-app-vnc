/*  Window monitor - control windows / talk with vncserver
 *  Copyright (C) 2004 UCHINO Satoshi.  All Rights Reserved.
 *
 *  based on vncviewer.h
 */
/*
 *  Copyright (C) 2000, 2001 Const Kaplinsky.  All Rights Reserved.
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

/*
 * vncviewer.h
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <pwd.h>
#include <X11/IntrinsicP.h>
#include <X11/StringDefs.h>
#include <X11/Shell.h>
#include <X11/Xmd.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <X11/Xmu/StdSel.h>
#include "rfbproto.h"
#include "caps.h"

#if defined(sun)
#define SOCKLEN_T	int
#else
#define SOCKLEN_T	socklen_t
#endif

extern int endianTest;

#define Swap16IfLE(s) \
    (*(char *)&endianTest ? ((((s) & 0xff) << 8) | (((s) >> 8) & 0xff)) : (s))

#define Swap32IfLE(l) \
    (*(char *)&endianTest ? ((((l) & 0xff000000) >> 24) | \
			     (((l) & 0x00ff0000) >> 8)  | \
			     (((l) & 0x0000ff00) << 8)  | \
			     (((l) & 0x000000ff) << 24))  : (l))

#define MAX_ENCODINGS 20

#define FLASH_PORT_OFFSET 5400
#define LISTEN_PORT_OFFSET 5500
#define TUNNEL_PORT_OFFSET 5500
#define SERVER_PORT_OFFSET 5900

#define DEFAULT_SSH_CMD "/usr/bin/ssh"
#define DEFAULT_TUNNEL_CMD  \
  (DEFAULT_SSH_CMD " -f -L %L:localhost:%R %H sleep 20")
#define DEFAULT_VIA_CMD     \
  (DEFAULT_SSH_CMD " -f -L %L:%H:%R %G sleep 20")

#define DEBUG_PRINTF(fmt) if (appData.debug) printf fmt;

/* argsresources.c */

typedef struct {
  Bool shareDesktop;
  Bool viewOnly;
  Bool fullScreen;
  Bool grabKeyboard;
  Bool raiseOnBeep;

  String encodingsString;

  Bool useBGR233;
  int nColours;
  Bool useSharedColours;
  Bool forceOwnCmap;
  Bool forceTrueColour;
  int requestedDepth;

  Bool useShm;

  int wmDecorationWidth;
  int wmDecorationHeight;

  char *userLogin;

  char *passwordFile;

  int rawDelay;
  int copyRectDelay;

  Bool debug;

  int popupButtonCount;

  int bumpScrollTime;
  int bumpScrollPixels;

  int compressLevel;
  int qualityLevel;
  Bool enableJPEG;
  Bool useRemoteCursor;
  Bool useX11Cursor;

  Bool frame; 

} AppData;

extern AppData appData;

extern char *fallback_resources[];
extern char vncServerHost[];
extern int vncServerPort;
extern Bool listenSpecified;
extern int listenPort, flashPort;
extern int windowCommand;
extern int windowId;
extern char* reverseConnectHost;

extern XrmOptionDescRec cmdLineOptions[];
extern int numCmdLineOptions;

extern void usage(void);
extern void GetArgsAndResources(int argc, char **argv);

/* rfbproto.c */

extern int rfbsock;
extern Bool canUseCoRRE;
extern Bool canUseHextile;
extern char *desktopName;
extern rfbPixelFormat myFormat;
extern rfbServerInitMsg si;

extern CapsContainer *tunnelCaps;    /* known tunneling/encryption methods */
extern CapsContainer *authCaps;      /* known authentication schemes       */
extern CapsContainer *serverMsgCaps; /* known non-standard server messages */
extern CapsContainer *clientMsgCaps; /* known non-standard client messages */
extern CapsContainer *encodingCaps;  /* known encodings besides Raw        */

extern void InitCapabilities(void);

extern Bool ConnectToRFBServer(const char *hostname, int port);
extern Bool InitialiseRFBConnection(void);
extern Bool HandleRFBServerMessage(void);

extern void PrintPixelFormat(rfbPixelFormat *format);
extern Bool SendWindowShare(CARD16 command, CARD32 id, char* rcHost);

/* sockets.c */

extern Bool errorMessageOnReadFailure;

extern Bool ReadFromRFBServer(char *out, unsigned int n);
extern Bool WriteExact(int sock, char *buf, int n);
extern int FindFreeTcpPort(void);
extern int ListenAtTcpPort(int port);
extern int ConnectToTcpAddr(unsigned int host, int port);
extern int AcceptTcpConnection(int listenSock);
extern Bool SetNonBlocking(int sock);

extern int StringToIPAddr(const char *str, unsigned int *addr);
extern Bool SameMachine(int sock);

/* windowshare.c */
extern char *programName;
