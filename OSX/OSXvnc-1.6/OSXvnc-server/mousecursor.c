/*
 *  untitled.c
 *  OSXvnc
 *
 *  Created by Jonathan Gillaspie on Wed Nov 20 2002.
 *  Copyright (c) 2002 __MyCompanyName__. All rights reserved.
 *
 */

#include <ApplicationServices/ApplicationServices.h>
#include <Carbon/Carbon.h>

#include <netinet/in.h>
#include <unistd.h>

#include "rfb.h"
//#import "rfbproto.h"
//#include "localbuffer.h"
#include "pthread.h"

#include "CGS.h"

static int lastCursorSeed = 0;
static CGPoint lastCursorPosition;

// Is the CGSConnection thread safe? or should each client have one...
static CGSConnectionRef sharedConnection = NULL;

CGPoint currentCursorLoc() {
    CGPoint cursorLoc;

    if (!sharedConnection) {
        if (CGSNewConnection(NULL, &sharedConnection) != kCGErrorSuccess)
            rfbLog("Error obtaining CGSConnection\n");
    }
    if (CGSGetCurrentCursorLocation(sharedConnection, &cursorLoc) != kCGErrorSuccess)
        rfbLog("Error obtaining cursor location\n");
    
    return cursorLoc;
}

void GetCursorInfo() {
    CGSConnectionRef connection;
    CGError err = noErr;
    int cursorDataSize, depth, components, bitsPerComponent, cursorRowSize;
    unsigned char*		cursorData;
    CGPoint				location, hotspot;
    CGRect				cursorRect;
    int i, j;

    err = CGSNewConnection(NULL, &connection);
    //printf("get active connection returns: %d, %d, %d\n", CGSGetActiveConnection(&temp, &connection), temp, connection);
    printf("new connection (err %d) = %d\n", err, connection);

    err = CGSGetCurrentCursorLocation(connection, &location);
    printf("location (err %d) = %d, %d\n", err, (int)location.x, (int)location.y);

    err = CGSGetGlobalCursorDataSize(connection, &cursorDataSize);
    printf("data size (err %d) = %d\n", err, cursorDataSize);

    cursorData = (unsigned char*)calloc(cursorDataSize, sizeof(unsigned char));

    err = CGSGetGlobalCursorData(connection,
                                 cursorData,
                                 &cursorDataSize,
                                 &cursorRowSize,
                                 &cursorRect,
                                 &hotspot,
                                 &depth,
                                 &components,
                                 &bitsPerComponent);

    printf("rect origin (%g, %g), dimensions (%g, %g)\n", cursorRect.origin.x, cursorRect.origin.y, cursorRect.size.width, cursorRect.size.height);

    printf("hotspot (%g, %g)\n", hotspot.x, hotspot.y);

    printf("depth: %d\n", depth);

    printf("components: %d\n", components);

    printf("bits per component: %d\n", bitsPerComponent);

    printf("Bytes Per Row: %d\n", cursorRowSize);


    printf("Components (err %d):\n", err);

    // Print Colors
    for (j=0; j < components; j++) {
        printf("\n");
        for (i=0; i < cursorDataSize; i++) {
            if (i % cursorRowSize == 0)
                printf("\n");
            if (i % components == j)
                printf("%02x", (int)cursorData[i]);
        }
    }

    printf("released connection (err %d)\n", CGSReleaseConnection(connection));
}

// We call this to see if we have a new cursor and should notify clients to do an update
// Or if cursor has moved
void rfbCheckForCursorChange() {
    CGPoint cursorLoc = currentCursorLoc();

    //rfbLog("Check For Cursor Change");
    // First Let's see if we have new info on the pasteboard - if so we'll send an update to each client
    if (lastCursorSeed != CGSCurrentCursorSeed() || !CGPointEqualToPoint(lastCursorPosition, cursorLoc)) {
        rfbClientIteratorPtr iterator = rfbGetClientIterator();
        rfbClientPtr cl;

        // Record first in case another change occurs after notifying clients
        lastCursorSeed = CGSCurrentCursorSeed();
        lastCursorPosition = cursorLoc;

        // Notify each client
        while ((cl = rfbClientIteratorNext(iterator)) != NULL) {
            if (rfbShouldSendNewCursor(cl) || (rfbShouldSendNewPosition(cl)))
                pthread_cond_signal(&cl->updateCond);
        }
        rfbReleaseClientIterator(iterator);
    }
}

Bool rfbShouldSendNewCursor(rfbClientPtr cl) {
    if (!cl->useRichCursorEncoding)
        return FALSE;
    else
        return (cl->currentCursorSeed != CGSCurrentCursorSeed());
}

Bool rfbShouldSendNewPosition(rfbClientPtr cl) {
    if (!cl->enableCursorPosUpdates)
        return FALSE;
    else {
        return (!CGPointEqualToPoint(cl->clientCursorLocation,currentCursorLoc()));
    }
}

Bool rfbSendCursorPos(rfbClientPtr cl) {
    rfbFramebufferUpdateRectHeader rect;

    if (cl->ublen + sz_rfbFramebufferUpdateRectHeader > UPDATE_BUF_SIZE) {
        if (!rfbSendUpdateBuf(cl))
            return FALSE;
    }

    cl->clientCursorLocation = currentCursorLoc();

    rect.encoding = Swap32IfLE(rfbEncodingPointerPos);
    rect.r.x = Swap16IfLE((CARD16)cl->clientCursorLocation.x);
    rect.r.y = Swap16IfLE((CARD16)cl->clientCursorLocation.y);
    rect.r.w = 0;
    rect.r.h = 0;

	SHAREDAPP_TRACE("trace(%d): Rect %x", NMSG++, rfbEncodingPointerPos);
		
    memcpy(&cl->updateBuf[cl->ublen], (char *)&rect, sz_rfbFramebufferUpdateRectHeader);
    cl->ublen += sz_rfbFramebufferUpdateRectHeader;

    cl->rfbRectanglesSent[rfbStatsCursorPosition]++;
    cl->rfbBytesSent[rfbStatsCursorPosition] += sz_rfbFramebufferUpdateRectHeader;

    if (!rfbSendUpdateBuf(cl))
        return FALSE;

	//rfbLog("send cursor %d %d", rect.r.x, rect.r.y);
	
    return TRUE;
}

/* Still To Do

Problems with occasional artifacts - turning off the cursor didn't seem to help
    Perhaps if we resend the area where the cursor just was..

*/

Bool rfbSendRichCursorUpdate(rfbClientPtr cl) {
    rfbFramebufferUpdateRectHeader rect;
    rfbPixelFormat cursorFormat;
    char *cursorData;
    int bufferMaskOffset;
    int cursorSize; // Size of cursor data from size
    int cursorRowBytes;
    int cursorDataSize; // Size to be sent to client
    int cursorMaskSize; // Mask Size to be sent to client
    int cursorDepth;
    int cursorBitsPerComponent;
    BOOL cursorIsDifferentFormat = FALSE;

    CGError err;
    CGSConnectionRef connection;
    int components; // Cursor Components

    CGPoint hotspot;
    CGRect cursorRect;

    //rfbLog("Sending Cursor To Client");
    //GetCursorInfo();

    if (!sharedConnection) {
        if (CGSNewConnection(NULL, &sharedConnection) != kCGErrorSuccess) {
            rfbLog("Error obtaining CGSConnection - cursor not sent\n");
            return FALSE;
        }
    }
    connection = sharedConnection;
    
    if (CGSGetGlobalCursorDataSize(connection, &cursorDataSize) != kCGErrorSuccess) {
        rfbLog("Error obtaining cursor data - cursor not sent\n");
        return FALSE;
    }

    cursorData = (unsigned char*)malloc(sizeof(unsigned char) * cursorDataSize);
    err = CGSGetGlobalCursorData(connection,
                                 cursorData,
                                 &cursorDataSize,
                                 &cursorRowBytes,
                                 &cursorRect,
                                 &hotspot,
                                 &cursorDepth,
                                 &components,
                                 &cursorBitsPerComponent);

    //CGSReleaseConnection(connection);
    if (err != kCGErrorSuccess) {
        rfbLog("Error obtaining cursor data - cursor not sent\n");
		// GRW SharedApp fix memory leak
		free(cursorData);
        return FALSE;
    }

    // For This We Don't send location just the cursor shape (and Hot Spot)
    cursorFormat.depth = cursorDepth;
    cursorFormat.bitsPerPixel = cursorDepth;
    cursorFormat.bigEndian = TRUE;
    cursorFormat.trueColour = TRUE;
    cursorFormat.redMax = cursorFormat.greenMax = cursorFormat.blueMax = (unsigned short) ((1<<cursorBitsPerComponent) - 1);
    cursorFormat.redShift = (unsigned char) cursorBitsPerComponent * 2;
    cursorFormat.greenShift = (unsigned char) cursorBitsPerComponent;
    cursorFormat.blueShift = 0;
    //GetCursorInfo();
    //PrintPixelFormat(&cursorFormat);
    cursorIsDifferentFormat = !(PF_EQ(cursorFormat,rfbServerFormat));
    
    cursorSize = (cursorRect.size.width * cursorRect.size.height * (cl->format.bitsPerPixel / 8));
    cursorMaskSize = floor((cursorRect.size.width+7)/8) * cursorRect.size.height;

    // Make Sure we have space on the buffer (otherwise push the data out now)

    if (cl->ublen + sz_rfbFramebufferUpdateRectHeader + cursorSize + cursorMaskSize > UPDATE_BUF_SIZE) {
        if (!rfbSendUpdateBuf(cl))
			// GRW SharedApp fix memory leak
			free(cursorData);
            return FALSE;
    }
	
    // Send The Header
    rect.r.x = Swap16IfLE((short) hotspot.x);
    rect.r.y = Swap16IfLE((short) hotspot.y);
    rect.r.w = Swap16IfLE((short) cursorRect.size.width);
    rect.r.h = Swap16IfLE((short) cursorRect.size.height);
    rect.encoding = Swap32IfLE(rfbEncodingRichCursor);

	SHAREDAPP_TRACE("trace(%d): Rect %x", NMSG++, rfbEncodingRichCursor);
		
    memcpy(&cl->updateBuf[cl->ublen], (char *)&rect,sz_rfbFramebufferUpdateRectHeader);
    cl->ublen += sz_rfbFramebufferUpdateRectHeader;

    // Apple Cursors can use a full Alpha channel.
    // Since we can only send a bit mask - to get closer we will compose the full color with a white
    // This requires us to jump ahead to write in the update buffer
    bufferMaskOffset = cl->ublen + cursorSize;

    // For starters we'll set it all off
    memset(&cl->updateBuf[bufferMaskOffset], 0, cursorMaskSize);
    // This algorithm assumes the Alpha channel is the first component
    {
        unsigned char *cursorRowData = cursorData;
        unsigned char *cursorColumnData = cursorData;
        unsigned int cursorBytesPerPixel = (cursorDepth/8);
        unsigned int alphaShift = 8 - cursorBitsPerComponent;
        unsigned char mask = 0;
        unsigned char fullOn = 0x00FF >> alphaShift;
        unsigned char alphaThreshold = 0x60 >> alphaShift; // Only include the pixel if it's coverage is greater than this
        int dataX, dataY, componentIndex;

        for (dataY = 0; dataY < cursorRect.size.height; dataY++) {
            cursorColumnData = cursorRowData;
            for (dataX = 0; dataX < cursorRect.size.width; dataX++) {
                mask = (unsigned char)(*cursorColumnData) >> alphaShift;
                if (mask > alphaThreshold) {
                    // Write the Bit For The Mask to be ON
                    cl->updateBuf[bufferMaskOffset+(dataX/8)] |= 0x0080 >> (dataX % 8);
                    // Composite Alpha into real cursors other channels - only for 32 bit
                    if (cursorDepth == 32 && mask != fullOn) {
                        // Set Alpha Pixel
                        *cursorColumnData = (unsigned char) 0x00;
                        cursorColumnData++;
                        for (componentIndex = 1; componentIndex < components; componentIndex++) {
                            *cursorColumnData = (unsigned char) (fullOn - mask + ((*cursorColumnData * mask)/fullOn)) & 0xFF;
                            cursorColumnData++;
                        }
                    }
                    else
                        cursorColumnData += cursorBytesPerPixel;
                }
                else
                    cursorColumnData += cursorBytesPerPixel;
            }

            bufferMaskOffset += floor((cursorRect.size.width+7)/8);
            cursorRowData += cursorRowBytes;
        }
    }

    // Temporarily set it to the cursor format
    if (cursorIsDifferentFormat)
        rfbSetTranslateFunctionUsingFormat(cl, cursorFormat);
    
    // Now Send The Cursor
    (*cl->translateFn)(cl->translateLookupTable, // The Lookup Table
                       &cursorFormat, // Our Cursor format
                       &cl->format, // Client Format
                       cursorData, // Data we're sending
                       &cl->updateBuf[cl->ublen], // where to write it
                       cursorRowBytes, // bytesBetweenInputLines
                       cursorRect.size.width,
                       cursorRect.size.height);
    cl->ublen += cursorSize;

    if (cursorIsDifferentFormat)
        rfbSetTranslateFunctionUsingFormat(cl, rfbServerFormat);
    
    // Now Send The Cursor Bitmap (1 for on, 0 for clear)
    // We already wrote in the bitmap, see above, just increment the ublen
    cl->ublen += cursorMaskSize;

    // Update Stats

    cl->rfbRectanglesSent[rfbStatsRichCursor]++;
    cl->rfbBytesSent[rfbStatsRichCursor] += sz_rfbFramebufferUpdateRectHeader + cursorSize + cursorMaskSize;
    cl->currentCursorSeed = CGSCurrentCursorSeed();

	// GRW SharedApp fix memory leak
	free(cursorData);
	
    return TRUE;
}
