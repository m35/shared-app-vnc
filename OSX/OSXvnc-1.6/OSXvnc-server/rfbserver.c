/*
 * rfbserver.c - deal with server-side of the RFB protocol.
 */

/*
 *  OSXvnc Copyright (C) 2001 Dan McGuirk <mcguirk@incompleteness.net>.
 *  Original Xvnc code Copyright (C) 1999 AT&T Laboratories Cambridge.
 *  All Rights Reserved.
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

#include <Cocoa/Cocoa.h>
#include <Carbon/Carbon.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

#include <netdb.h>

#include "rfb.h"
#include "SharedApp.h"
extern SharedApp *sharedApp;

//char updateBuf[UPDATE_BUF_SIZE];
//int ublen;

rfbClientPtr pointerClient = NULL;  /* Mutex for pointer events with buttons down*/

static rfbClientPtr rfbClientHead;

struct rfbClientIterator {
    rfbClientPtr next;
};

static pthread_mutex_t rfbClientListMutex;
static struct rfbClientIterator rfbClientIteratorInstance;

void
rfbClientListInit(void)
{
    rfbClientHead = NULL;
    pthread_mutex_init(&rfbClientListMutex, NULL);
}

rfbClientIteratorPtr
rfbGetClientIterator(void)
{
    pthread_mutex_lock(&rfbClientListMutex);
    rfbClientIteratorInstance.next = rfbClientHead;

    return &rfbClientIteratorInstance;
}

rfbClientPtr
rfbClientIteratorNext(rfbClientIteratorPtr iterator)
{
    rfbClientPtr result = iterator->next;
    if (result)
        iterator->next = result->next;
    return result;
}

void
rfbReleaseClientIterator(rfbClientIteratorPtr iterator)
{
    pthread_mutex_unlock(&rfbClientListMutex);
}

Bool rfbClientsConnected()
{
    return (rfbClientHead != NULL);
}


/*
 * rfbNewClientConnection is called from sockets.c when a new connection
 * comes in.
 */

void rfbNewClientConnection(int sock) {
    rfbNewClient(sock);
}


// GRW SharedAppVnc - modified - AF_INET6 wasn't working
/*
 * rfbReverseConnection is called to make an outward
 * connection to a "listening" RFB client.
 */
rfbClientPtr rfbReverseConnection(char *host, int port) {
    int sock;
    struct sockaddr_in sin;
	struct addrinfo *res, hint;
	int errCode;
    rfbClientPtr cl;
	
    bzero(&sin, sizeof(sin));
	sin.sin_len = sizeof(sin);
	sin.sin_family = AF_INET;
	sin.sin_port = htons(port);
	memset(&hint, 0, sizeof(hint));
	hint.ai_family = PF_INET;
	hint.ai_socktype = SOCK_STREAM;
	if ((errCode = getaddrinfo(host, NULL, &hint, &res)) != 0) {
		rfbLog("Error resolving reverse host %s: %s\n", host, gai_strerror(errCode));
		return NULL;
    }
	sin.sin_addr = ((struct sockaddr_in *)res->ai_addr)->sin_addr;
	freeaddrinfo(res);
	
    if ((sock = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
        rfbLog("Error creating reverse socket\n");
        return NULL;
    }
    
    if (connect(sock, (struct sockaddr *)&sin, sizeof(sin)) != 0) {
        rfbLog("Error connecting to reverse host %s:%d\n", host, port);
        return NULL;
    }
	
    cl = rfbNewClient(sock);
	
    if (cl) {
        cl->reverseConnection = TRUE;
    }
	
    return cl;
}
/*
rfbClientPtr rfbReverseConnection(char *host, int port) {
    int sock;
    struct sockaddr_in6 sin;
	struct addrinfo *res, hint;
	int errCode;
    rfbClientPtr cl;
	
    bzero(&sin, sizeof(sin));
	sin.sin6_len = sizeof(sin);
	sin.sin6_family = AF_INET6;
	sin.sin6_port = htons(port);
	memset(&hint, 0, sizeof(hint));
	hint.ai_family = PF_INET6;
	hint.ai_socktype = SOCK_STREAM;
	if ((errCode = getaddrinfo(host, NULL, &hint, &res)) != 0) {
		rfbLog("Error resolving reverse host %s: %s\n", host, gai_strerror(errCode));
		return NULL;
    }
	sin.sin6_addr = ((struct sockaddr_in6 *)res->ai_addr)->sin6_addr;
	freeaddrinfo(res);

    if ((sock = socket(PF_INET6, SOCK_STREAM, 0)) < 0) {
        rfbLog("Error creating reverse socket\n");
        return NULL;
    }
    
    if (connect(sock, (struct sockaddr *)&sin, sizeof(sin)) != 0) {
        rfbLog("Error connecting to reverse host %s:%d\n", host, port);
        return NULL;
    }
	
    cl = rfbNewClient(sock);
	
    if (cl) {
        cl->reverseConnection = TRUE;
    }

    return cl;
}
*/


/*
 * rfbNewClient is called when a new connection has been made by whatever
 * means.
 */

rfbClientPtr rfbNewClient(int sock) {
    rfbProtocolVersionMsg pv;
    rfbClientPtr cl;
    BoxRec box;
    struct sockaddr_in addr;
    int i, addrlen = sizeof(struct sockaddr_in);
	//struct hostent *host;

    /*
     {
         rfbClientIteratorPtr iterator;

         rfbLog("syncing other clients:\n");
         iterator = rfbGetClientIterator();
         while ((cl = rfbClientIteratorNext(iterator)) != NULL) {
             rfbLog("     %s\n",cl->host);
         }
         rfbReleaseClientIterator(iterator);
     }
     */

    cl = (rfbClientPtr)xalloc(sizeof(rfbClientRec));

    cl->sock = sock;
    getpeername(sock, (struct sockaddr *)&addr, &addrlen);
    cl->host = strdup(inet_ntoa(addr.sin_addr));
	
	//host = gethostbyaddr(&addr.sin_addr, addrlen, AF_INET);
	//if (host) rfbLog("host %s connected", host->h_name);
	
    pthread_mutex_init(&cl->outputMutex, NULL);

    cl->state = RFB_PROTOCOL_VERSION;

    /* REDSTONE - Adding some features
        In theory these need not be global, but could be set per client
        */
    cl->disableRemoteEvents = rfbDisableRemote;      // Ignore PB, Keyboard and Mouse events
    cl->swapMouseButtons23 = rfbSwapButtons;         // How to interpret mouse buttons 2 & 3

    cl->needNewScreenSize = NO;
	initPasteboardForClient(cl);

    cl->reverseConnection = FALSE;
    cl->preferredEncoding = rfbEncodingRaw;
    cl->correMaxWidth = 48;
    cl->correMaxHeight = 48;
    cl->zrleData = 0;
    cl->mosData = 0;

    box.x1 = box.y1 = 0;
    box.x2 = rfbScreen.width;
    box.y2 = rfbScreen.height;
    REGION_INIT(pScreen,&cl->modifiedRegion,&box,0);

    pthread_mutex_init(&cl->updateMutex, NULL);
    pthread_cond_init(&cl->updateCond, NULL);

    REGION_INIT(pScreen,&cl->requestedRegion,NullBox,0);

    cl->format = rfbServerFormat;
    cl->translateFn = rfbTranslateNone;
    cl->translateLookupTable = NULL;

    /* SERVER SCALING EXTENSIONS -- Server Scaling is off by default */
    cl->scalingFactor = 1;
    cl->scalingFrameBuffer = rfbGetFramebuffer();
	cl->scalingPaddedWidthInBytes = rfbScreen.paddedWidthInBytes;

    cl->tightCompressLevel = TIGHT_DEFAULT_COMPRESSION;
    cl->tightQualityLevel = -1;
    for (i = 0; i < 4; i++)
        cl->zsActive[i] = FALSE;

    cl->enableLastRectEncoding = FALSE;
    cl->enableXCursorShapeUpdates = FALSE;
    cl->useRichCursorEncoding = FALSE;
    cl->enableCursorPosUpdates = FALSE;
    cl->desktopSizeUpdate = FALSE;
    cl->immediateUpdate = FALSE;
	
#ifdef SHAREDAPP
	cl->supportsSharedAppEncoding = FALSE;
#endif
	
    
    pthread_mutex_lock(&rfbClientListMutex);
    cl->next = rfbClientHead;
    cl->prev = NULL;
    if (rfbClientHead)
        rfbClientHead->prev = cl;

    rfbClientHead = cl;
    pthread_mutex_unlock(&rfbClientListMutex);

    rfbResetStats(cl);

    cl->compStreamInited = FALSE;
    cl->compStream.total_in = 0;
    cl->compStream.total_out = 0;
    cl->compStream.zalloc = Z_NULL;
    cl->compStream.zfree = Z_NULL;
    cl->compStream.opaque = Z_NULL;

    cl->zlibCompressLevel = 5;

    cl->compStreamRaw.total_in = ZLIBHEX_COMP_UNINITED;
    cl->compStreamHex.total_in = ZLIBHEX_COMP_UNINITED;

    cl->client_zlibBeforeBufSize = 0;
    cl->client_zlibBeforeBuf = NULL;

    cl->client_zlibAfterBufSize = 0;
    cl->client_zlibAfterBuf = NULL;
    cl->client_zlibAfterBufLen = 0;
    
    sprintf(pv,rfbProtocolVersionFormat,rfbProtocolMajorVersion, rfbProtocolMinorVersion);

    if (WriteExact(cl, pv, sz_rfbProtocolVersionMsg) < 0) {
        rfbLogPerror("rfbNewClient: write");
        rfbCloseClient(cl);
        return NULL;
    }

    return cl;
}


/*
 * rfbClientConnectionGone is called from sockets.c just after a connection
 * has gone away.
 */

void rfbClientConnectionGone(rfbClientPtr cl) {
    int i;

    rfbLog("Client %s disconnected\n",cl->host);

    // RedstoneOSX - Track and release depressed modifier keys whenever the client disconnects
    //rfbLog("Client %s release modifier keys\n",cl->host);
    keyboardReleaseKeysForClient(cl);

    pthread_mutex_lock(&rfbClientListMutex);

    //rfbLog("Client %s release compression streams\n",cl->host);
    /* Release the compression state structures if any. */
    if ( cl->compStreamInited == TRUE ) {
        deflateEnd( &(cl->compStream) );
    }

    for (i = 0; i < 4; i++) {
        if (cl->zsActive[i])
            deflateEnd(&cl->zsStruct[i]);
    }

    if (pointerClient == cl)
        pointerClient = NULL;

    if (cl->prev)
        cl->prev->next = cl->next;
    else
        rfbClientHead = cl->next;
    if (cl->next)
        cl->next->prev = cl->prev;

    pthread_mutex_unlock(&rfbClientListMutex);
    //rfbLog("Client %s removed from client list\n",cl->host);

    REGION_UNINIT(pScreen,&cl->modifiedRegion);

    rfbPrintStats(cl);

    FreeZrleData(cl);

    free(cl->host);

    if (cl->translateLookupTable)
        free(cl->translateLookupTable);

    /* SERVER SCALING EXTENSIONS */
    if( cl->scalingFrameBuffer && cl->scalingFrameBuffer != rfbGetFramebuffer() ){
        free(cl->scalingFrameBuffer);
    }

    pthread_cond_destroy(&cl->updateCond);
    pthread_mutex_destroy(&cl->updateMutex);
    pthread_mutex_destroy(&cl->outputMutex);

    xfree(cl);
    // Not sure why but this log message seems to prevent a crash
    rfbLog("Client gone\n");
}


/*
 * rfbProcessClientMessage is called when there is data to read from a client.
 */

void rfbProcessClientMessage(rfbClientPtr cl) {
    switch (cl->state) {
        case RFB_PROTOCOL_VERSION:
            rfbProcessClientProtocolVersion(cl);
            return;
        case RFB_AUTH_VERSION:
            rfbProcessAuthVersion(cl);
            return;
        case RFB_AUTHENTICATION:
            rfbAuthProcessClientMessage(cl);
            return;
        case RFB_INITIALISATION:
            rfbProcessClientInitMessage(cl);
            return;
        default:
            rfbProcessClientNormalMessage(cl);
            return;
    }
}


/*
 * rfbProcessClientProtocolVersion is called when the client sends its
 * protocol version.
 */

void rfbProcessClientProtocolVersion(rfbClientPtr cl) {
    rfbProtocolVersionMsg pv;
    int n;
    char failureReason[256];

    if ((n = ReadExact(cl, pv, sz_rfbProtocolVersionMsg)) <= 0) {
        if (n == 0)
            rfbLog("rfbProcessClientProtocolVersion: client gone\n");
        else
            rfbLogPerror("rfbProcessClientProtocolVersion: read");
        rfbCloseClient(cl);
        return;
    }

    pv[sz_rfbProtocolVersionMsg] = 0;
    if (sscanf(pv,rfbProtocolVersionFormat,&cl->major,&cl->minor) != 2) {
        rfbLog("rfbProcessClientProtocolVersion: not a valid RFB client\n");
        rfbCloseClient(cl);
        return;
    }
    rfbLog("Protocol version %d.%d\n", cl->major, cl->minor);

    if (cl->major != rfbProtocolMajorVersion) {
        /* Major version mismatch - send a ConnFailed message */
        rfbLog("Major version mismatch\n");
        sprintf(failureReason,
                "RFB protocol version mismatch - server %d.%d, client %d.%d",
                rfbProtocolMajorVersion,rfbProtocolMinorVersion,cl->major,cl->minor);
        rfbClientConnFailed(cl, failureReason);
        return;
    }

    if (cl->minor != rfbProtocolMinorVersion) {
        /* Minor version mismatch - warn but try to continue */
        rfbLog("Ignoring minor version mismatch\n");
    }

    rfbAuthNewClient(cl);
}


/*
 * rfbClientConnFailed is called when a client connection has failed either
 * because it talks the wrong protocol or it has failed authentication.
 */

void rfbClientConnFailed(rfbClientPtr cl, char *reason) {
    char *buf;
    int len = strlen(reason);

    buf = (char *)xalloc(8 + len);
    ((CARD32 *)buf)[0] = Swap32IfLE(rfbConnFailed);
    ((CARD32 *)buf)[1] = Swap32IfLE(len);
    memcpy(buf + 8, reason, len);

    if (WriteExact(cl, buf, 8 + len) < 0)
        rfbLogPerror("rfbClientConnFailed: write");
    xfree(buf);
    rfbCloseClient(cl);
}


/*
 * rfbProcessClientInitMessage is called when the client sends its
 * initialisation message.
 */

void rfbProcessClientInitMessage(rfbClientPtr cl) {
    rfbClientInitMsg ci;
    char buf[256];
    rfbServerInitMsg *si = (rfbServerInitMsg *)buf;
    int len, n;
    rfbClientIteratorPtr iterator;
    rfbClientPtr otherCl;

    if ((n = ReadExact(cl, (char *)&ci,sz_rfbClientInitMsg)) <= 0) {
        if (n == 0)
            rfbLog("rfbProcessClientInitMessage: client gone\n");
        else
            rfbLogPerror("rfbProcessClientInitMessage: read");
        rfbCloseClient(cl);
        return;
    }

    si->framebufferWidth = Swap16IfLE(rfbScreen.width);
    si->framebufferHeight = Swap16IfLE(rfbScreen.height);
    si->format = rfbServerFormat;
    si->format.redMax = Swap16IfLE(si->format.redMax);
    si->format.greenMax = Swap16IfLE(si->format.greenMax);
    si->format.blueMax = Swap16IfLE(si->format.blueMax);

    if (strlen(desktopName) > 128)      /* sanity check on desktop name len */
        desktopName[128] = 0;

    strcpy(buf + sz_rfbServerInitMsg, desktopName);
    len = strlen(buf + sz_rfbServerInitMsg);
    si->nameLength = Swap32IfLE(len);

    if (WriteExact(cl, buf, sz_rfbServerInitMsg + len) < 0) {
        rfbLogPerror("rfbProcessClientInitMessage: write");
        rfbCloseClient(cl);
        return;
    }

    cl->state = RFB_NORMAL;

    if (!cl->reverseConnection &&
        (rfbNeverShared || (!rfbAlwaysShared && !ci.shared))) {

        if (rfbDontDisconnect) {
            iterator = rfbGetClientIterator();
            while ((otherCl = rfbClientIteratorNext(iterator)) != NULL) {
                if ((otherCl != cl) && (otherCl->state == RFB_NORMAL)) {
                    rfbLog("-dontdisconnect: Not shared & existing client\n");
                    rfbLog("  refusing new client %s\n", cl->host);
                    rfbCloseClient(cl);
                    rfbReleaseClientIterator(iterator);
                    return;
                }
            }
            rfbReleaseClientIterator(iterator);
        } else {
            iterator = rfbGetClientIterator();
            while ((otherCl = rfbClientIteratorNext(iterator)) != NULL) {
                if ((otherCl != cl) && (otherCl->state == RFB_NORMAL)) {
                    rfbLog("Not shared - closing connection to client %s\n",
                           otherCl->host);
                    rfbCloseClient(otherCl);
                }
            }
            rfbReleaseClientIterator(iterator);
        }
    }
}


/*
 * rfbProcessClientNormalMessage is called when the client has sent a normal
 * protocol message.
 */

void rfbProcessClientNormalMessage(rfbClientPtr cl) {
    int n;
    rfbClientToServerMsg msg;
    char *str;

    if ((n = ReadExact(cl, (char *)&msg, 1)) <= 0) {
        if (n != 0)
            rfbLogPerror("rfbProcessClientNormalMessage: read");
        rfbCloseClient(cl);
        return;
    }

    switch (msg.type) {

        case rfbSetPixelFormat:
		{
            if ((n = ReadExact(cl, ((char *)&msg) + 1,
                               sz_rfbSetPixelFormatMsg - 1)) <= 0) {
                if (n != 0)
                    rfbLogPerror("rfbProcessClientNormalMessage: read");
                rfbCloseClient(cl);
                return;
            }

            cl->format.bitsPerPixel = msg.spf.format.bitsPerPixel;
            cl->format.depth = msg.spf.format.depth;
            cl->format.bigEndian = (msg.spf.format.bigEndian ? 1 : 0);
            cl->format.trueColour = (msg.spf.format.trueColour ? 1 : 0);
            cl->format.redMax = Swap16IfLE(msg.spf.format.redMax);
            cl->format.greenMax = Swap16IfLE(msg.spf.format.greenMax);
            cl->format.blueMax = Swap16IfLE(msg.spf.format.blueMax);
            cl->format.redShift = msg.spf.format.redShift;
            cl->format.greenShift = msg.spf.format.greenShift;
            cl->format.blueShift = msg.spf.format.blueShift;

            rfbSetTranslateFunction(cl);
            return;
		}

        case rfbFixColourMapEntries:
		{
            if ((n = ReadExact(cl, ((char *)&msg) + 1,
                               sz_rfbFixColourMapEntriesMsg - 1)) <= 0) {
                if (n != 0)
                    rfbLogPerror("rfbProcessClientNormalMessage: read");
                rfbCloseClient(cl);
                return;
            }
            rfbLog("rfbProcessClientNormalMessage: %s",
                   "FixColourMapEntries unsupported\n");
            rfbCloseClient(cl);
            return;
		}
			
        case rfbSetEncodings: {
            int i;
            CARD32 enc;

            if ((n = ReadExact(cl, ((char *)&msg) + 1,
                               sz_rfbSetEncodingsMsg - 1)) <= 0) {
                if (n != 0)
                    rfbLogPerror("rfbProcessClientNormalMessage: read");
                rfbCloseClient(cl);
                return;
            }

            msg.se.nEncodings = Swap16IfLE(msg.se.nEncodings);

            pthread_mutex_lock(&cl->updateMutex);

            // Since there is not protocol to "clear" these extensions we always clear them and expect them to be re-sent if
            // the client continues to support those options
            cl->preferredEncoding = -1;
            cl->enableLastRectEncoding = FALSE;
            cl->enableXCursorShapeUpdates = FALSE;
            cl->useRichCursorEncoding = FALSE;
            cl->enableCursorPosUpdates = FALSE;
            cl->desktopSizeUpdate = FALSE;
            cl->immediateUpdate = FALSE;
			
#ifdef SHAREDAPP
			cl->supportsSharedAppEncoding = FALSE;
#endif

            for (i = 0; i < msg.se.nEncodings; i++) {
                if ((n = ReadExact(cl, (char *)&enc, 4)) <= 0) {
                    if (n != 0)
                        rfbLogPerror("rfbProcessClientNormalMessage: read");
                    rfbCloseClient(cl);
                    return;
                }
                enc = Swap32IfLE(enc);

                switch (enc) {
                    case rfbEncodingCopyRect:
                        break;
                    case rfbEncodingRaw:
                    case rfbEncodingRRE:
                    case rfbEncodingCoRRE:
                    case rfbEncodingHextile:
                    case rfbEncodingZlib:
                    case rfbEncodingTight:
                    case rfbEncodingZlibHex:
                    case rfbEncodingZRLE:
                        if (cl->preferredEncoding == -1) {
                            cl->preferredEncoding = enc;
                            rfbLog("ENCODING: %s for client %s\n", encNames[cl->preferredEncoding], cl->host);
                        }
                        break;
					case rfbEncodingUltra:
						rfbLog("\tULTRA Encoding not supported(ignored): %u (%X)\n", (int)enc, (int)enc);
						break;
                        /* PSEUDO_ENCODINGS */
                    case rfbEncodingLastRect:
                        rfbLog("\tEnabling LastRect protocol extension for client %s\n", cl->host);
                        cl->enableLastRectEncoding = TRUE;
                        break;
                    case rfbEncodingXCursor:
                        //rfbLog("Enabling XCursor protocol extension for client %s\n", cl->host);
                        cl->enableXCursorShapeUpdates = TRUE;
                        break;
                    case rfbEncodingRichCursor:
                        rfbLog("\tEnabling Cursor Shape protocol extension for client %s\n", cl->host);
                        cl->useRichCursorEncoding = TRUE;
                        cl->currentCursorSeed = 0;
                        break;
                    case rfbEncodingPointerPos:
                        rfbLog("\tEnabling Cursor Position protocol extension for client %s\n", cl->host);
                        cl->enableCursorPosUpdates = TRUE;
                        cl->clientCursorLocation = CGPointMake(-1.0, -1.0);
                        break;
                    case rfbEncodingDesktopResize:
                        rfbLog("\tEnabling Dynamic Desktop Sizing for client %s\n", cl->host);
                        cl->desktopSizeUpdate = TRUE;
                        break;
                    case rfbImmediateUpdate:
                        rfbLog("\tEnabling Immediate updates for client " "%s\n", cl->host);
                        cl->immediateUpdate = TRUE;
                        break;
					case rfbPasteboardRequest:
						rfbLog("\tEnabling Pasteboard Request" "%s\n", cl->host);
						cl->pasteBoardLastChange = -2; // This will cause it to send a single update that shows the current PB
						break;
#ifdef SHAREDAPP
					case rfbEncodingSharedApp:
						rfbLog("Client supports ShareApp Encoding\n");
						cl->supportsSharedAppEncoding = TRUE;
						break;
#endif

                        // Tight encoding options
                    default:
                        if ( enc >= (CARD32)rfbEncodingCompressLevel0 &&
                             enc <= (CARD32)rfbEncodingCompressLevel9 ) {
                            cl->zlibCompressLevel = enc & 0x0F;
                            cl->tightCompressLevel = enc & 0x0F;
                            rfbLog("\tUsing compression level %d for client %s\n",
                                   cl->tightCompressLevel, cl->host);
                        }
                        else if ( enc >= (CARD32)rfbEncodingQualityLevel0 &&
                                    enc <= (CARD32)rfbEncodingQualityLevel9 ) {
                            cl->tightQualityLevel = enc & 0x0F;
                            rfbLog("\tUsing jpeg image quality level %d for client %s\n",
                                   cl->tightQualityLevel, cl->host);
                        }
                        else {
                            rfbLog("\tUnknown Encoding Type(ignored): %u (%X)\n", (int)enc, (int)enc);
                        }
                }
            }

            if (cl->preferredEncoding == -1) {
                cl->preferredEncoding = rfbEncodingRaw;
            }

            pthread_mutex_unlock(&cl->updateMutex);

            // Force a new update to the client
            if (rfbShouldSendNewCursor(cl) || (rfbShouldSendNewPosition(cl)))
                pthread_cond_signal(&cl->updateCond);
            
            return;
        }


        case rfbFramebufferUpdateRequest:
        {
            RegionRec tmpRegion;
            BoxRec box;

            if ((n = ReadExact(cl, ((char *)&msg) + 1,
                               sz_rfbFramebufferUpdateRequestMsg-1)) <= 0) {
                if (n != 0)
                    rfbLogPerror("rfbProcessClientNormalMessage: read");
                rfbCloseClient(cl);
                return;
            }

            //rfbLog("FUR: %d (%d,%d x %d,%d)\n", msg.fur.incremental, msg.fur.x, msg.fur.y,  msg.fur.w, msg.fur.h);

            box.x1 = Swap16IfLE(msg.fur.x)*cl->scalingFactor;
            box.y1 = Swap16IfLE(msg.fur.y)*cl->scalingFactor;
            box.x2 = (box.x1 + Swap16IfLE(msg.fur.w))*cl->scalingFactor;
            box.y2 = (box.y1 + Swap16IfLE(msg.fur.h))*cl->scalingFactor;
            SAFE_REGION_INIT(pScreen,&tmpRegion,&box,0);

            pthread_mutex_lock(&cl->updateMutex);
            REGION_UNION(pScreen, &cl->requestedRegion, &cl->requestedRegion,
                         &tmpRegion);
            if (!msg.fur.incremental) {
                REGION_UNION(pScreen,&cl->modifiedRegion,&cl->modifiedRegion,
                             &tmpRegion);
            }
            pthread_mutex_unlock(&cl->updateMutex);
            pthread_cond_signal(&cl->updateCond);
            REGION_UNINIT(pScreen,&tmpRegion);

            return;
        }

        case rfbKeyEvent:
		{
            if (!cl->disableRemoteEvents)
                cl->rfbKeyEventsRcvd++;

            if ((n = ReadExact(cl, ((char *)&msg) + 1,
                               sz_rfbKeyEventMsg - 1)) <= 0)
			{
                if (n != 0)
                    rfbLogPerror("rfbProcessClientNormalMessage: read");
                rfbCloseClient(cl);
                return;
            }

#ifdef SHAREDAPP
			if ([sharedApp enabled])
			{
				VNCWinInfo *win;
				int windowId;
				NSAutoreleasePool *tempPool = [[NSAutoreleasePool alloc] init];		
				
				windowId = Swap32IfLE(msg.pe.windowId);	
				win = [sharedApp getWindowWithId:windowId];
				
				[tempPool release];
				if (![win isTopWindow]) return;

			}

#endif
			
                if (!cl->disableRemoteEvents)
                    KbdAddEvent(msg.ke.down, (KeySym)Swap32IfLE(msg.ke.key), cl);

                return;
		}
        case rfbPointerEvent: {
            if (!cl->disableRemoteEvents)
                cl->rfbPointerEventsRcvd++;

            if ((n = ReadExact(cl, ((char *)&msg) + 1, sz_rfbPointerEventMsg - 1)) <= 0) {
                if (n != 0)
                    rfbLogPerror("rfbProcessClientNormalMessage: read");
                rfbCloseClient(cl);
                return;
            }

			//rfbLog("%d %d %x %d", Swap16IfLE(msg.pe.x), Swap16IfLE(msg.pe.y), Swap16IfLE(msg.pe.pad), Swap32IfLE(msg.pe.windowId));
            
			if (cl->disableRemoteEvents || (pointerClient && (pointerClient != cl)))
                return;
			
#ifdef SHAREDAPP
			if ([sharedApp enabled])
			{
				RegionRec visibleRegion;
				BoxRec resbox;
				VNCWinInfo *win;
				int windowId;
				int x, y;
				BOOL pointInRegion = FALSE;
				NSAutoreleasePool *tempPool = [[NSAutoreleasePool alloc] init];
				
				REGION_INIT(&hackScreen, &visibleRegion, NullBox, 0);
				
				x = Swap16IfLE(msg.pe.x);
				y = Swap16IfLE(msg.pe.y);
				windowId = Swap32IfLE(msg.pe.windowId);
				win = [sharedApp getWindowWithId:windowId];

				if (win)
				{
					[win getVisibleRegion:&visibleRegion];
					if (POINT_IN_REGION(&hackScreen, &visibleRegion, x, y, &resbox))
					{
						pointInRegion = TRUE;
					}
				}
				
				REGION_UNINIT(&hackScreen, &visibleRegion);
				[tempPool release];
				
				if (!pointInRegion) return;
			}
#endif
				
            if (msg.pe.buttonMask == 0)
                pointerClient = NULL;
            else
                pointerClient = cl;

			//rfbLog("CursorEvent %d %d", Swap16IfLE(msg.pe.x), Swap16IfLE(msg.pe.y));
			
            PtrAddEvent(msg.pe.buttonMask,
                (Swap16IfLE(msg.pe.x)+cl->scalingFactor-1)*cl->scalingFactor,
                (Swap16IfLE(msg.pe.y)+cl->scalingFactor-1)*cl->scalingFactor,
                cl);

            return;
        }
		case rfbMultiCursor: {
			int cursor;
			if ((n = ReadExact(cl, ((char *)&msg) + 1, sz_rfbMultiCursorMsg - 1)) <= 0) {
                if (n != 0)
                    rfbLogPerror("rfbProcessClientNormalMessage: read");
                rfbCloseClient(cl);
                return;
            }
			cursor = Swap16IfLE(msg.mc.cursorNumber);
			rfbLog("Cursor %d request: Multicursors Unsupported");
			return;
		}
        case rfbClientCutText: {

            if ((n = ReadExact(cl, ((char *)&msg) + 1,
                               sz_rfbClientCutTextMsg - 1)) <= 0) {
                if (n != 0)
                    rfbLogPerror("rfbProcessClientNormalMessage: read");
                rfbCloseClient(cl);
                return;
            }

            if (!cl->disableRemoteEvents) {
                msg.cct.length = Swap32IfLE(msg.cct.length);

                str = (char *)xalloc(msg.cct.length);

                if ((n = ReadExact(cl, str, msg.cct.length)) <= 0) {
                    if (n != 0)
                        rfbLogPerror("rfbProcessClientNormalMessage: read");
                    xfree(str);
                    rfbCloseClient(cl);
                    return;
                }

                rfbSetCutText(cl, str, msg.cct.length);

                xfree(str);
            }
            return;
        }

        /* SERVER SCALING EXTENSIONS */
		case rfbSetScaleFactorULTRA:
		case rfbSetScaleFactor: 
		{
            rfbReSizeFrameBufferMsg rsfb;
            if ((n = ReadExact(cl, ((char *)&msg) + 1,
                               sz_rfbSetScaleFactorMsg-1)) <= 0) {
                if (n != 0)
                    rfbLogPerror("rfbProcessClientNormalMessage: read");
                rfbCloseClient(cl);
                return;
            }

            if( cl->scalingFactor != msg.ssf.scale ){
				const unsigned long csh = (rfbScreen.height+msg.ssf.scale-1) / msg.ssf.scale;
				const unsigned long csw = (rfbScreen.width +msg.ssf.scale-1) / msg.ssf.scale;

                cl->scalingFactor = msg.ssf.scale;

				rfbLog("Server Side Scaling: %d for client %s\n", msg.ssf.scale, cl->host);
							
				if (cl->scalingFrameBuffer && cl->scalingFrameBuffer != rfbGetFramebuffer())
					free(cl->scalingFrameBuffer);
				
				if (cl->scalingFactor == 1) {
					cl->scalingFrameBuffer = rfbGetFramebuffer();
					cl->scalingPaddedWidthInBytes = rfbScreen.paddedWidthInBytes;
				}
				else {					
					cl->scalingFrameBuffer = malloc( csw*csh*rfbScreen.bitsPerPixel/8 );
					cl->scalingPaddedWidthInBytes = csw * rfbScreen.bitsPerPixel/8;
				}

				/* Now notify the client of the new desktop area */
				if (msg.type == rfbSetScaleFactor) {
					rsfb.type = rfbReSizeFrameBuffer;
					rsfb.desktop_w = Swap16IfLE(rfbScreen.width);
					rsfb.desktop_h = Swap16IfLE(rfbScreen.height);
					rsfb.buffer_w = Swap16IfLE(csw);
					rsfb.buffer_h = Swap16IfLE(csh);
					
					SHAREDAPP_TRACE("trace(%d): Message %d", NMSG++, rfbReSizeFrameBuffer);
					if (WriteExact(cl, (char *)&rsfb, sizeof(rsfb)) < 0) {
						rfbLogPerror("rfbProcessClientNormalMessage: write");
						rfbCloseClient(cl);
						return;
					}
				}
				else {
					// What does UltraVNC expect here probably just a resize event
					rfbFramebufferUpdateMsg *fu = (rfbFramebufferUpdateMsg *)cl->updateBuf;
                    fu->type = rfbFramebufferUpdate;
                    fu->nRects = Swap16IfLE(1);
                    cl->ublen = sz_rfbFramebufferUpdateMsg;
					
					SHAREDAPP_TRACE("trace(%d): Message %d", NMSG++, rfbFramebufferUpdate);
										
                    rfbSendScreenUpdateEncoding(cl);
				}
			}
			
            return;
        }

        default: 
		{
            rfbLog("ERROR: Client Sent Message: unknown message type %d\n", msg.type);
            rfbLog("...... Closing connection to client %s\n", cl->host);
            rfbCloseClient(cl);
            return;
		}
    }
}

/*
 * rfbSendFramebufferUpdate - send the currently pending framebuffer update to
 * the RFB client.
 */

Bool rfbSendFramebufferUpdate(rfbClientPtr cl, RegionRec updateRegion) {
    int i;
    int nUpdateRegionRects;
    Bool sendRichCursorEncoding = FALSE;
    Bool sendCursorPositionEncoding = FALSE;

    rfbFramebufferUpdateMsg *fu = (rfbFramebufferUpdateMsg *)cl->updateBuf;

    /* Now send the update */

    cl->rfbFramebufferUpdateMessagesSent++;

#if !DEBUG_SEND_PIXELS_ONLY
    if (cl->needNewScreenSize) {
        nUpdateRegionRects++;
    }
#endif	        

    if (cl->preferredEncoding == rfbEncodingCoRRE) {
        nUpdateRegionRects = 0;

        for (i = 0; i < REGION_NUM_RECTS(&updateRegion); i++) {
            int x = REGION_RECTS(&updateRegion)[i].x1;
            int y = REGION_RECTS(&updateRegion)[i].y1;
            int w = REGION_RECTS(&updateRegion)[i].x2 - x;
            int h = REGION_RECTS(&updateRegion)[i].y2 - y;
            nUpdateRegionRects += (((w-1) / cl->correMaxWidth + 1)
                                   * ((h-1) / cl->correMaxHeight + 1));
        }
    } else if (cl->preferredEncoding == rfbEncodingZlib) {
        nUpdateRegionRects = 0;

        for (i = 0; i < REGION_NUM_RECTS(&updateRegion); i++) {
            int x = REGION_RECTS(&updateRegion)[i].x1;
            int y = REGION_RECTS(&updateRegion)[i].y1;
            int w = REGION_RECTS(&updateRegion)[i].x2 - x;
            int h = REGION_RECTS(&updateRegion)[i].y2 - y;
            nUpdateRegionRects += (((h-1) / (ZLIB_MAX_SIZE( w ) / w)) + 1);
        }
    } else if (cl->preferredEncoding == rfbEncodingTight) {
        nUpdateRegionRects = 0;

        for (i = 0; i < REGION_NUM_RECTS(&updateRegion); i++) {
            int x = REGION_RECTS(&updateRegion)[i].x1;
            int y = REGION_RECTS(&updateRegion)[i].y1;
            int w = REGION_RECTS(&updateRegion)[i].x2 - x;
            int h = REGION_RECTS(&updateRegion)[i].y2 - y;
            int n = rfbNumCodedRectsTight(cl, x, y, w, h);
            if (n == 0) {
                nUpdateRegionRects = 0xFFFF;
                break;
            }
            nUpdateRegionRects += n;
        }
    } else {
        nUpdateRegionRects = REGION_NUM_RECTS(&updateRegion);
    }

    // Sometimes send the mouse cursor update also
#if !DEBUG_SEND_PIXELS_ONLY
    if (nUpdateRegionRects != 0xFFFF) {
        if (rfbShouldSendNewCursor(cl)) {
            sendRichCursorEncoding = TRUE;
            nUpdateRegionRects++;
        }
        if (rfbShouldSendNewPosition(cl)) {
            sendCursorPositionEncoding = TRUE;
            nUpdateRegionRects++;
        }
    }
#endif
	
    fu->type = rfbFramebufferUpdate;
    fu->nRects = Swap16IfLE(nUpdateRegionRects);
    cl->ublen = sz_rfbFramebufferUpdateMsg;
	
	SHAREDAPP_TRACE("trace(%d): Message %d", NMSG++, rfbFramebufferUpdate);
#if !DEBUG_SEND_PIXELS_ONLY
    if (cl->needNewScreenSize) {
        if (rfbSendScreenUpdateEncoding(cl)) {
            cl->needNewScreenSize = FALSE;
        }
        else {
            rfbLog("Error Sending Cursor\n");
            return FALSE;
        }            
    }
    
    // Sometimes send the mouse cursor update
    if (sendRichCursorEncoding) {
        if (!rfbSendRichCursorUpdate(cl)) {
            rfbLog("Error Sending Cursor\n");
            return FALSE;
        }
    }
    if (sendCursorPositionEncoding) {
        if (!rfbSendCursorPos(cl)) {
            rfbLog("Error Sending Cursor\n");
            return FALSE;
        }

    }
#endif
	
    for (i = 0; i < REGION_NUM_RECTS(&updateRegion); i++) {
        int x = REGION_RECTS(&updateRegion)[i].x1;
        int y = REGION_RECTS(&updateRegion)[i].y1;
        int w = REGION_RECTS(&updateRegion)[i].x2 - x;
        int h = REGION_RECTS(&updateRegion)[i].y2 - y;

		if (cl->scalingFactor != 1)
			CopyScalingRect( cl, &x, &y, &w, &h, TRUE);
		
        cl->rfbRawBytesEquivalent += (sz_rfbFramebufferUpdateRectHeader
                                      + w * (cl->format.bitsPerPixel / 8) * h);

        switch (cl->preferredEncoding) {
            case rfbEncodingRaw:
                if (!rfbSendRectEncodingRaw(cl, x, y, w, h)) {
                    return FALSE;
                }
                break;
            case rfbEncodingRRE:
                if (!rfbSendRectEncodingRRE(cl, x, y, w, h)) {
                    return FALSE;
                }
                break;
            case rfbEncodingCoRRE:
                if (!rfbSendRectEncodingCoRRE(cl, x, y, w, h)) {
                    return FALSE;
                }
                break;
            case rfbEncodingHextile:
                if (!rfbSendRectEncodingHextile(cl, x, y, w, h)) {
                    return FALSE;
                }
                break;
            case rfbEncodingZlib:
                if (!rfbSendRectEncodingZlib(cl, x, y, w, h)) {
                    return FALSE;
                }
                break;
            case rfbEncodingTight:
                if (!rfbSendRectEncodingTight(cl, x, y, w, h)) {
                    return FALSE;
                }
                break;
            case rfbEncodingZlibHex:
                if (!rfbSendRectEncodingZlibHex(cl, x, y, w, h)) {
                    return FALSE;
                }
                break;
            case rfbEncodingZRLE:
                if (!rfbSendRectEncodingZRLE(cl, x, y, w, h)) {
                    return FALSE;
                }
                break;
        }
    }

    if (nUpdateRegionRects == 0xFFFF && !rfbSendLastRectMarker(cl))
        return FALSE;

    if (!rfbSendUpdateBuf(cl))
        return FALSE;

    return TRUE;
}

Bool rfbSendScreenUpdateEncoding(rfbClientPtr cl) {
    rfbFramebufferUpdateRectHeader rect;

    if (cl->ublen + sz_rfbFramebufferUpdateRectHeader > UPDATE_BUF_SIZE) {
        if (!rfbSendUpdateBuf(cl))
            return FALSE;
    }

    rect.r.x = 0;
    rect.r.y = 0;
    rect.r.w = Swap16IfLE((rfbScreen.width +cl->scalingFactor-1) / cl->scalingFactor);
    rect.r.h = Swap16IfLE((rfbScreen.height+cl->scalingFactor-1) / cl->scalingFactor);
    rect.encoding = Swap32IfLE(rfbEncodingDesktopResize);

	SHAREDAPP_TRACE("trace(%d): Rect %x", NMSG++, rfbEncodingDesktopResize);
		
    memcpy(&cl->updateBuf[cl->ublen], (char *)&rect,sz_rfbFramebufferUpdateRectHeader);
    cl->ublen += sz_rfbFramebufferUpdateRectHeader;


    cl->rfbRectanglesSent[rfbStatsDesktopResize]++;
    cl->rfbBytesSent[rfbStatsDesktopResize] += sz_rfbFramebufferUpdateRectHeader;

    // Let's push this out right away
    return rfbSendUpdateBuf(cl);
}

/*
 * Send a given rectangle in raw encoding (rfbEncodingRaw).
 */

Bool rfbSendRectEncodingRaw(rfbClientPtr cl, int x, int y, int w, int h) {
    rfbFramebufferUpdateRectHeader rect;
    int nlines;
    int bytesPerLine = w * (cl->format.bitsPerPixel / 8);
    char *fbptr = (cl->scalingFrameBuffer + (cl->scalingPaddedWidthInBytes * y)
                   + (x * (rfbScreen.bitsPerPixel / 8)));

    if (cl->ublen + sz_rfbFramebufferUpdateRectHeader > UPDATE_BUF_SIZE) {
        if (!rfbSendUpdateBuf(cl))
            return FALSE;
    }

    rect.r.x = Swap16IfLE(x);
    rect.r.y = Swap16IfLE(y);
    rect.r.w = Swap16IfLE(w);
    rect.r.h = Swap16IfLE(h);
    rect.encoding = Swap32IfLE(rfbEncodingRaw);

	SHAREDAPP_TRACE("trace(%d): Rect %x", NMSG++, rfbEncodingRaw);
		
    memcpy(&cl->updateBuf[cl->ublen], (char *)&rect,sz_rfbFramebufferUpdateRectHeader);
    cl->ublen += sz_rfbFramebufferUpdateRectHeader;


    cl->rfbRectanglesSent[rfbEncodingRaw]++;
    cl->rfbBytesSent[rfbEncodingRaw]
        += sz_rfbFramebufferUpdateRectHeader + bytesPerLine * h;

    nlines = (UPDATE_BUF_SIZE - cl->ublen) / bytesPerLine;

    while (TRUE) {
        if (nlines > h)
            nlines = h;

        (*cl->translateFn)(cl->translateLookupTable, &rfbServerFormat,
                           &cl->format, fbptr, &cl->updateBuf[cl->ublen],
                           cl->scalingPaddedWidthInBytes, w, nlines);

        cl->ublen += nlines * bytesPerLine;
        h -= nlines;

        if (h == 0)     /* rect fitted in buffer, do next one */
            return TRUE;

        /* buffer full - flush partial rect and do another nlines */

        if (!rfbSendUpdateBuf(cl))
            return FALSE;

        fbptr += (cl->scalingPaddedWidthInBytes * nlines);

        nlines = (UPDATE_BUF_SIZE - cl->ublen) / bytesPerLine;
        if (nlines == 0) {
            rfbLog("rfbSendRectEncodingRaw: send buffer too small for %d "
                   "bytes per line\n", bytesPerLine);
            rfbCloseClient(cl);
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

Bool rfbSendLastRectMarker(rfbClientPtr cl) {
    rfbFramebufferUpdateRectHeader rect;

    if (cl->ublen + sz_rfbFramebufferUpdateRectHeader > UPDATE_BUF_SIZE) {
        if (!rfbSendUpdateBuf(cl))
            return FALSE;
    }

    rect.encoding = Swap32IfLE(rfbEncodingLastRect);
    rect.r.x = 0;
    rect.r.y = 0;
    rect.r.w = 0;
    rect.r.h = 0;

	SHAREDAPP_TRACE("trace(%d): Rect %x", NMSG++, rfbEncodingLastRect);
		
    memcpy(&cl->updateBuf[cl->ublen], (char *)&rect,sz_rfbFramebufferUpdateRectHeader);
    cl->ublen += sz_rfbFramebufferUpdateRectHeader;

    cl->rfbLastRectMarkersSent++;
    cl->rfbLastRectBytesSent += sz_rfbFramebufferUpdateRectHeader;
	
    return TRUE;
}


/*
 * Send the contents of updateBuf.  Returns 1 if successful, -1 if
 * not (errno should be set).
 */

Bool rfbSendUpdateBuf(rfbClientPtr cl) {
    /*
     int i;
     for (i = 0; i < cl->ublen; i++) {
         fprintf(stderr,"%02x ",((unsigned char *)cl->updateBuf)[i]);
     }
     fprintf(stderr,"\n");
     */
	
    if (WriteExact(cl, cl->updateBuf, cl->ublen) < 0) {
        rfbLogPerror("rfbSendUpdateBuf: write");
        rfbCloseClient(cl);
        return FALSE;
    }

    cl->ublen = 0;
    return TRUE;
}


/*
 * rfbSendServerCutText sends a ServerCutText message to all the clients.
 */

void rfbSendServerCutText(rfbClientPtr cl, char *str, int len) {

#if DEBUG_SEND_PIXELS_ONLY
	return;
#endif
	
    rfbServerCutTextMsg sct;

    sct.type = rfbServerCutText;
    sct.length = Swap32IfLE(len);
	
	SHAREDAPP_TRACE("trace(%d): Message %d", NMSG++, rfbServerCutText);
    if (WriteExact(cl, (char *)&sct, sz_rfbServerCutTextMsg) < 0) {
        rfbLogPerror("rfbSendServerCutText: write");
        rfbCloseClient(cl);
    }

    if (WriteExact(cl, str, len) < 0) {
        rfbLogPerror("rfbSendServerCutText: write");
        rfbCloseClient(cl);
    }
}
/*
 void
 rfbSendServerCutText(char *str, int len)
 {
     rfbClientPtr cl;
     rfbServerCutTextMsg sct;
     rfbClientIteratorPtr iterator;

     // XXX bad-- writing with client list lock held
     iterator = rfbGetClientIterator();
     while ((cl = rfbClientIteratorNext(iterator)) != NULL) {
         sct.type = rfbServerCutText;
         sct.length = Swap32IfLE(len);
         if (WriteExact(cl, (char *)&sct,
                        sz_rfbServerCutTextMsg) < 0) {
             rfbLogPerror("rfbSendServerCutText: write");
             rfbCloseClient(cl);
             continue;
         }
         if (WriteExact(cl, str, len) < 0) {
             rfbLogPerror("rfbSendServerCutText: write");
             rfbCloseClient(cl);
         }
     }
     rfbReleaseClientIterator(iterator);
 }
 */

/* SERVER SCALING EXTENSIONS */
void CopyScalingRect( rfbClientPtr cl, int* x, int* y, int* w, int* h, Bool bDoScaling ){
    unsigned long cx, cy, cw, ch;
    unsigned long rx, ry, rw, rh;
    unsigned char* srcptr;
    unsigned char* dstptr;
    unsigned char* tmpptr;
    unsigned long pixel_value=0, red, green, blue;
    unsigned long xx, yy, u, v;
    const unsigned long bytesPerPixel = rfbScreen.bitsPerPixel/8;
    const unsigned long csh = (rfbScreen.height+cl->scalingFactor-1)/ cl->scalingFactor;
    const unsigned long csw = (rfbScreen.width +cl->scalingFactor-1)/ cl->scalingFactor;

    cy = (*y) / cl->scalingFactor;
    ch = (*h+cl->scalingFactor-1) / cl->scalingFactor+1;
    cx = (*x) / cl->scalingFactor;
    cw = (*w+cl->scalingFactor-1) / cl->scalingFactor+1;

    if( cy > csh ){
        cy = csh;
    }
    if( cy + ch > csh ){
        ch = csh - cy;
    }
    if( cx > csw ){
        cx = csw;
    }
    if( cx + cw > csw ){
        cw = csw - cx;
    }

    if( bDoScaling ){
        ry = cy * cl->scalingFactor;
        rh = ch * cl->scalingFactor;
        rx = cx * cl->scalingFactor;
        rw = cw * cl->scalingFactor;

        /* Copy and scale data from screen buffer to scaling buffer */
        srcptr = (unsigned char*)rfbGetFramebuffer()
            + (ry * rfbScreen.paddedWidthInBytes ) + (rx * bytesPerPixel);
        dstptr = (unsigned char*)cl->scalingFrameBuffer
            + (cy * cl->scalingPaddedWidthInBytes) + (cx * bytesPerPixel);

        if( cl->format.trueColour ){ /* Blend neighbouring pixels together */
            for( yy=0; yy < ch; yy++ ){
                for( xx=0; xx < cw; xx++ ){
                    red = green = blue = 0;
                    for( v = 0; v < (unsigned long)cl->scalingFactor; v++ ){
                        tmpptr = srcptr;
                        for( u = 0; u < (unsigned long)cl->scalingFactor; u++ ){
                            switch( bytesPerPixel ){
                            case 1:
                                pixel_value = (unsigned long)*(unsigned char* )tmpptr;
                                break;
                            case 2:
                                pixel_value = (unsigned long)*(unsigned short*)tmpptr;
                                break;
                            case 3:    /* 24bpp may cause bus error? */
                            case 4:
                                pixel_value = (unsigned long)*(unsigned long* )tmpptr;
                                break;
                            }
                            red   += (pixel_value >> rfbServerFormat.redShift  )& rfbServerFormat.redMax;
                            green += (pixel_value >> rfbServerFormat.greenShift)& rfbServerFormat.greenMax;
                            blue  += (pixel_value >> rfbServerFormat.blueShift )& rfbServerFormat.blueMax;
                            tmpptr  += rfbScreen.paddedWidthInBytes;
                        }
                        srcptr  += bytesPerPixel;
                    }
                    red   /= cl->scalingFactor * cl->scalingFactor;
                    green /= cl->scalingFactor * cl->scalingFactor;
                    blue  /= cl->scalingFactor * cl->scalingFactor;

                    pixel_value = (red   << rfbServerFormat.redShift)
                                + (green << rfbServerFormat.greenShift)
                                + (blue  << rfbServerFormat.blueShift);

                    switch( bytesPerPixel ){
                    case 1:
                        *(unsigned char* )dstptr = (unsigned char )pixel_value;
                        break;
                    case 2:
                        *(unsigned short*)dstptr = (unsigned short)pixel_value;
                        break;
                    case 3:    /* 24bpp may cause bus error? */
                    case 4:
                        *(unsigned long* )dstptr = (unsigned long )pixel_value;
                        break;
                    }
                    dstptr += bytesPerPixel;
                }
                srcptr += (rfbScreen.paddedWidthInBytes - cw * bytesPerPixel)* cl->scalingFactor;
                dstptr += cl->scalingPaddedWidthInBytes - cw * bytesPerPixel;
            }
        }else{ /* Not truecolour, so we can't blend. Just use the top-left pixel instead */
            for( yy=0; yy < ch; yy++ ){
                for( xx=0; xx < cw; xx++ ){
                    memcpy( dstptr, srcptr, bytesPerPixel);
                    srcptr += bytesPerPixel * cl->scalingFactor;
                    dstptr += bytesPerPixel;
                }
                srcptr += (rfbScreen.paddedWidthInBytes - cw * bytesPerPixel)* cl->scalingFactor;
                dstptr += cl->scalingPaddedWidthInBytes - cw * bytesPerPixel;
            }
        }
    }

    *y = cy;
    *h = ch;
    *x = cx;
    *w = cw;
}
