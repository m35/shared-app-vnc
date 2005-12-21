/*
 *  Copyright (C) 2004 UCHINO Satoshi.  All Rights Reserved.
 *  Copyright (C) 2003 Tungsten Graphics, Inc. All Rights Reserved.
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
 * rfbproto.c - functions to deal with client side of RFB protocol.
 *
 * Modular loader donated by Tungsten Graphics, Inc.
 * See http://www.tungstengraphics.com for more information
 */

#include <unistd.h>
#include <errno.h>
#include <pwd.h>
#include <windowshare.h>
#include <vncauth.h>
#include <zlib.h>

#include "rfbmodule.h"
typedef Bool (*encoderFunc_t)(void *msg);
encoderFunc_t encoderFunc[MAX_ENCODINGS];
_vnc_module_close_t _vnc_module_close[MAX_ENCODINGS];
void *_vnc_module_libs[MAX_ENCODINGS];
int encoderType[MAX_ENCODINGS];
int encoders = 0;

int rfbsock;
char *desktopName;
rfbPixelFormat myFormat;
rfbServerInitMsg si;

int endianTest = 1;

CapsContainer *tunnelCaps;  /* known tunneling/encryption methods */
CapsContainer *authCaps;  /* known authentication schemes       */
CapsContainer *serverMsgCaps;  /* known non-standard server messages */
CapsContainer *clientMsgCaps;  /* known non-standard client messages */
CapsContainer *encodingCaps;  /* known encodings besides Raw        */

static Bool SetupTunneling(void);
static Bool PerformAuthenticationNew(void);
static Bool PerformAuthenticationOld(void);
static Bool AuthenticateVNC(void);
static Bool ReadInteractionCaps(void);
static Bool ReadCapabilityList(CapsContainer *caps, int count);
static void ReadConnFailedReason(void);

/* Note that the CoRRE encoding uses this buffer and assumes it is big enough
   to hold 255 * 255 * 32 bits -> 260100 bytes.  640*480 = 307200 bytes.
   Hextile also assumes it is big enough to hold 16 * 16 * 32 bits.
   Tight encoding assumes BUFFER_SIZE is at least 16384 bytes. */

/* Raw also uses this buffer */

#define BUFFER_SIZE (640*480)
char buffer[BUFFER_SIZE];

/*
 * InitCapabilities.
 */

void
InitCapabilities(void)
{
  tunnelCaps    = CapsNewContainer();
  authCaps      = CapsNewContainer();
  serverMsgCaps = CapsNewContainer();
  clientMsgCaps = CapsNewContainer();
  encodingCaps  = CapsNewContainer();

  /* Supported authentication methods */
  CapsAdd(authCaps, rfbAuthVNC, rfbStandardVendor, sig_rfbAuthVNC,
    "Standard VNC password authentication");

  /* NOTE! Should be moved to their modules when I reorder the calling
   * functions.... Alan.
   */
}


/*
 * ConnectToRFBServer.
 */

Bool
ConnectToRFBServer(const char *hostname, int port)
{
  unsigned int host;

  if (!StringToIPAddr(hostname, &host)) {
    fprintf(stderr,"Couldn't convert '%s' to host address\n", hostname);
    return False;
  }

  rfbsock = ConnectToTcpAddr(host, port);

  if (rfbsock < 0) {
    fprintf(stderr,"Unable to connect to VNC server\n");
    return False;
  }

  return SetNonBlocking(rfbsock);
}


/*
 * InitialiseRFBConnection.
 */

Bool
InitialiseRFBConnection(void)
{
  rfbProtocolVersionMsg pv;
  int server_major, server_minor;
  int viewer_major, viewer_minor;
  rfbClientInitMsg ci;

  /* if the connection is immediately closed, don't report anything, so
       that pmw's monitor can make test connections */

  if (!ReadFromRFBServer(pv, sz_rfbProtocolVersionMsg)) return False;

  errorMessageOnReadFailure = True;

  pv[sz_rfbProtocolVersionMsg] = 0;

  if (sscanf(pv,rfbProtocolVersionFormat,&server_major,&server_minor) != 2) {
    fprintf(stderr,"Not a valid VNC server\n");
    return False;
  }

  viewer_major = rfbProtocolMajorVersion;
  viewer_minor = rfbProtocolFallbackMinorVersion;/* use older protocol */

  fprintf(stderr, "Connected to RFB server, using protocol version %d.%d\n",
    viewer_major, viewer_minor);

  sprintf(pv, rfbProtocolVersionFormat, viewer_major, viewer_minor);
  
  if (!WriteExact(rfbsock, pv, sz_rfbProtocolVersionMsg))
    return False;
  
  if (!PerformAuthenticationOld()) /* authentication in protocol 3.3 */
    return False;

  ci.shared = 1;

  if (!WriteExact(rfbsock, (char *)&ci, sz_rfbClientInitMsg))
    return False;

  if (!ReadFromRFBServer((char *)&si, sz_rfbServerInitMsg))
    return False;

  si.framebufferWidth = Swap16IfLE(si.framebufferWidth);
  si.framebufferHeight = Swap16IfLE(si.framebufferHeight);
  si.format.redMax = Swap16IfLE(si.format.redMax);
  si.format.greenMax = Swap16IfLE(si.format.greenMax);
  si.format.blueMax = Swap16IfLE(si.format.blueMax);
  si.nameLength = Swap32IfLE(si.nameLength);

  desktopName = malloc(si.nameLength + 1);
  if (!desktopName) {
    fprintf(stderr, "Error allocating memory for desktop name, %lu bytes\n",
            (unsigned long)si.nameLength);
    return False;
  }

  if (!ReadFromRFBServer(desktopName, si.nameLength)) return False;

  desktopName[si.nameLength] = 0;

  fprintf(stderr,"Desktop name \"%s\"\n",desktopName);

  /* Only for protocol version 3.7t */
#if 0
    /* Read interaction capabilities */
    if (!ReadInteractionCaps())
      return False;
#endif

  return True;
}


/*
 * Setup tunneling, for the protocol version 3.7t.
 */

static Bool
SetupTunneling(void)
{
  rfbTunnelingCapsMsg caps;
  CARD32 tunnelType;

  /* In the protocol version 3.7t, the server informs us about supported
     tunneling methods supported. Here we read this information. */

  if (!ReadFromRFBServer((char *)&caps, sz_rfbTunnelingCapsMsg))
    return False;

  caps.nTunnelTypes = Swap16IfLE(caps.nTunnelTypes);

  if (caps.nTunnelTypes) {
    if (!ReadCapabilityList(tunnelCaps, caps.nTunnelTypes))
      return False;

    /* We cannot do tunneling anyway yet. */
    tunnelType = Swap32IfLE(rfbNoTunneling);
    if (!WriteExact(rfbsock, (char *)&tunnelType, sizeof(tunnelType)))
      return False;
  }

  return True;
}
 

/*
 * Negotiate authentication scheme (protocol version 3.7t)
 */

static Bool
PerformAuthenticationNew(void)
{
  rfbAuthenticationCapsMsg caps;
  CARD32 authScheme;
  int i;

  /* In the protocol version 3.7t, the server informs us about supported
     authentication schemes supported. Here we read this information. */

  if (!ReadFromRFBServer((char *)&caps, sz_rfbAuthenticationCapsMsg))
    return False;

  caps.nAuthTypes = Swap16IfLE(caps.nAuthTypes);

  if (!caps.nAuthTypes) {
    fprintf(stderr, "No authentication needed\n");
    return True;
  }

  if (!ReadCapabilityList(authCaps, caps.nAuthTypes))
    return False;

  /* Otherwise, try server's preferred authentication scheme. */
  for (i = 0; i < CapsNumEnabled(authCaps); i++) {
    authScheme = CapsGetByOrder(authCaps, i);
    if (authScheme != rfbAuthVNC)
      continue;                 /* unknown scheme - cannot use it */
    authScheme = Swap32IfLE(authScheme);
    if (!WriteExact(rfbsock, (char *)&authScheme, sizeof(authScheme)))
      return False;
    authScheme = Swap32IfLE(authScheme); /* convert it back */
    if (authScheme == rfbAuthVNC) {
      return AuthenticateVNC();
    } else {
      /* Should never happen. */
      fprintf(stderr, "Assertion failed: unknown authentication scheme\n");
      return False;
    }
  }

  fprintf(stderr, "No suitable authentication schemes offered by server\n");
  return False;
}

  
/*
 * Negotiate authentication scheme (protocol version 3.3)
 */

static Bool
PerformAuthenticationOld(void)
{
  CARD32 authScheme;

  /* Read the authentication type */
  if (!ReadFromRFBServer((char *)&authScheme, 4))
    return False;

  authScheme = Swap32IfLE(authScheme);

  switch (authScheme) {
  case rfbSecTypeInvalid:
    ReadConnFailedReason();
    return False;
  case rfbSecTypeNone:
    fprintf(stderr, "No authentication needed\n");
    break;
  case rfbSecTypeVncAuth:
    if (!AuthenticateVNC())
      return False;
    break;
  default:
    fprintf(stderr, "Unknown authentication scheme from VNC server: %d\n",
      (int)authScheme);
    return False;
  }

  return True;
}


/*
 * Standard VNC authentication.
 */

static Bool
AuthenticateVNC(void)
{
  CARD32 authScheme, authResult;
  CARD8 challenge[CHALLENGESIZE];
  char *passwd;

  fprintf(stderr, "Performing standard VNC authentication\n");

  if (!ReadFromRFBServer((char *)challenge, CHALLENGESIZE))
    return False;

  if (appData.passwordFile) {
    passwd = vncDecryptPasswdFromFile(appData.passwordFile);
    if (!passwd) {
      fprintf(stderr, "Cannot read valid password from file \"%s\"\n",
        appData.passwordFile);
      return False;
    }
  } else {
    passwd = getpass("VncServer Password: ");
  }

  if (!passwd || strlen(passwd) == 0) {
    fprintf(stderr, "Reading password failed\n");
    return False;
  }
  if (strlen(passwd) > 8) {
    passwd[8] = '\0';
  }

  vncEncryptBytes(challenge, passwd);

  /* Lose the password from memory */
  memset(passwd, '\0', strlen(passwd));

  if (!WriteExact(rfbsock, (char *)challenge, CHALLENGESIZE))
    return False;

  if (!ReadFromRFBServer((char *)&authResult, 4))
    return False;

  authResult = Swap32IfLE(authResult);

  switch (authResult) {
  case rfbVncAuthOK:
    fprintf(stderr, "VNC authentication succeeded\n");
    break;
  case rfbVncAuthFailed:
    fprintf(stderr, "VNC authentication failed\n");
    return False;
  case rfbVncAuthTooMany:
    fprintf(stderr, "VNC authentication failed - too many tries\n");
    return False;
  default:
    fprintf(stderr, "Unknown VNC authentication result: %d\n",
       (int)authResult);
    return False;
  }

  return True;
}

/*
 * In the protocol version 3.7t, the server informs us about supported
 * protocol messages and encodings. Here we read this information.
 */

static Bool
ReadInteractionCaps(void)
{
  rfbInteractionCapsMsg intr_caps;

  /* Read the counts of list items following */
  if (!ReadFromRFBServer((char *)&intr_caps, sz_rfbInteractionCapsMsg))
    return False;
  intr_caps.nServerMessageTypes = Swap16IfLE(intr_caps.nServerMessageTypes);
  intr_caps.nClientMessageTypes = Swap16IfLE(intr_caps.nClientMessageTypes);
  intr_caps.nEncodingTypes = Swap16IfLE(intr_caps.nEncodingTypes);

  /* Read the lists of server- and client-initiated messages */
  return (ReadCapabilityList(serverMsgCaps, intr_caps.nServerMessageTypes) &&
    ReadCapabilityList(clientMsgCaps, intr_caps.nClientMessageTypes) &&
    ReadCapabilityList(encodingCaps, intr_caps.nEncodingTypes));
}


/*
 * Read the list of rfbCapabilityInfo structures and enable corresponding
 * capabilities in the specified container. The count argument specifies how
 * many records to read from the socket.
 */

static Bool
ReadCapabilityList(CapsContainer *caps, int count)
{
  rfbCapabilityInfo msginfo;
  int i;

  for (i = 0; i < count; i++) {
    if (!ReadFromRFBServer((char *)&msginfo, sz_rfbCapabilityInfo))
      return False;
    msginfo.code = Swap32IfLE(msginfo.code);
    CapsEnable(caps, &msginfo);
    if (appData.debug) {
      char vendor[5];
      char name[9];
      strncpy(vendor, msginfo.vendorSignature, 4);
      vendor[4] = '\0';
      strncpy(name, msginfo.nameSignature, 8);
      name[8] = '\0';
      fprintf(stderr,"Caps: code=%Xh, vendor=%s, name=%s\n", msginfo.code, vendor, name);
    }
  }

  return True;
}

Bool
SetFormatAndEncodings()
{
  rfbSetPixelFormatMsg spf;
  char buf[sz_rfbSetEncodingsMsg + MAX_ENCODINGS * 4];
  rfbSetEncodingsMsg *se = (rfbSetEncodingsMsg *)buf;
  CARD32 *encs = (CARD32 *)(&buf[sz_rfbSetEncodingsMsg]);
  int len = 0;
  Bool requestCompressLevel = False;
  Bool requestQualityLevel = False;
  Bool requestLastRectEncoding = False;

  memset(buf, 0, sz_rfbSetEncodingsMsg + MAX_ENCODINGS * 4);

  spf.type = rfbSetPixelFormat;
  spf.format = myFormat;
  spf.format.redMax = Swap16IfLE(spf.format.redMax);
  spf.format.greenMax = Swap16IfLE(spf.format.greenMax);
  spf.format.blueMax = Swap16IfLE(spf.format.blueMax);

  if (!WriteExact(rfbsock, (char *)&spf, sz_rfbSetPixelFormatMsg))
    return False;

  se->type = rfbSetEncodings;
  se->nEncodings = 0;

  len = sz_rfbSetEncodingsMsg + se->nEncodings * 4;

  se->nEncodings = Swap16IfLE(se->nEncodings);

  if (!WriteExact(rfbsock, buf, len)) return False;

  return True;
}


/*
 * PrintPixelFormat.
 */

static void
ReadConnFailedReason(void)
{
  CARD32 reasonLen;
  char *reason = NULL;

  if (ReadFromRFBServer((char *)&reasonLen, sizeof(reasonLen))) {
    reasonLen = Swap32IfLE(reasonLen);
    if ((reason = malloc(reasonLen)) != NULL &&
        ReadFromRFBServer(reason, reasonLen)) {
      fprintf(stderr,"VNC connection failed: %.*s\n", (int)reasonLen, reason);
      free(reason);
      return;
    }
  }

  fprintf(stderr, "VNC connection failed\n");

  if (reason != NULL)
    free(reason);
}

/*
 * SendWindowSharing
 * (windowname can be null if name is unchanged since last update)
 */

Bool
SendWindowShare(CARD16 command, CARD32 id, char* rcHost)
{
  rfbSharedAppRequestMsg sap;
  Bool ret;

  if (command == rfbSharedAppReverseConnection) id = strlen(rcHost);

  sap.type = rfbSharedAppRequest;
  sap.command = Swap16IfLE(command);
  sap.id = Swap32IfLE(id);


  fprintf(stderr,"Sending windowCommand %x windowId %x\n", sap.command, sap.id);

  ret = WriteExact(rfbsock, (char *)&sap, sz_rfbSharedAppRequestMsg);
  if (ret && (command == rfbSharedAppReverseConnection))
  {
    ret = WriteExact(rfbsock, rcHost, id);
  }
  return ret;
}


/*
 * HandleRFBServerMessage.
 */

Bool
HandleRFBServerMessage()
{
  unsigned int i,j;
  rfbServerToClientMsg msg;

  if (!ReadFromRFBServer((char *)&msg, 1))
    return False;

  for (i = 0; i < encoders; i++) {
    if (encoderFunc[i] && (encoderType[i] == RFB_MODULE_IS_ENCODER) && 
      encoderFunc[i](&msg)) 
    return True;
  }

  switch (msg.type) {
  case rfbSetColourMapEntries:
    fprintf(stderr,"Ignoring message 'SetColourMapEntries' from VNC server\n");
    break;

  case rfbFramebufferUpdate:
    fprintf(stderr,"Ignoring message 'FramebufferUpdate' from VNC server\n");
    break;

  case rfbBell:
    fprintf(stderr,"Ignoring message 'Bell' from VNC server\n");
    break;

  case rfbServerCutText:
    fprintf(stderr,"Ignoring message 'ServerCutText' from VNC server\n");
    break;

  default:
    fprintf(stderr,"Unknown message type %d from VNC server\n",msg.type);
    return False;
  }

  return True;
}
