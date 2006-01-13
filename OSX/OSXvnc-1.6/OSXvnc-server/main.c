/*
 *  OSXvnc Copyright (C) 2002-2004 Redstone Software osxvnc@redstonesoftware.com
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

#include <ApplicationServices/ApplicationServices.h>
#include <Cocoa/Cocoa.h>
#include <Carbon/Carbon.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h>

#include "rfb.h"
#include "localbuffer.h"

#include "rfbserver.h"
#import "VNCServer.h"
#import "SharedApp.h"

#import "../RFBBundleProtocol.h"

ScreenRec hackScreen;
rfbScreenInfo rfbScreen;

char desktopName[255] = "";

static int rfbPort = 5900;
int  rfbMaxBitDepth = 0;
Bool rfbAlwaysShared = FALSE;
Bool rfbNeverShared = FALSE;
Bool rfbDontDisconnect = FALSE;
Bool rfbLocalhostOnly = TRUE; //FALSE; // GRW SharedAppVnc
Bool rfbInhibitEvents = TRUE; //FALSE;
Bool rfbReverseMods = FALSE;

Bool rfbSwapButtons = FALSE;
Bool rfbDisableRemote = FALSE;
Bool rfbRemapShortcuts = FALSE;
BOOL rfbShouldSendUpdates = TRUE;
BOOL registered = FALSE;
BOOL restartOnUserSwitch = FALSE;

// OSXvnc 0.8 This flag will use a local buffer which will allow us to display the mouse cursor
Bool rfbLocalBuffer = FALSE;
static pthread_mutex_t logMutex;

static pthread_mutex_t listenerAccepting;
static pthread_cond_t listenerGotNewClient;

/* OSXvnc 0.8 for screensaver .... */
// setup screen saver disabling timer
// Not sure we want or need this...
//static EventLoopTimerUPP  screensaverTimerUPP;
//static EventLoopTimerRef screensaverTimer;
Bool rfbDisableScreenSaver = FALSE;

// Display ID
CGDirectDisplayID displayID = NULL;

extern void rfbScreensaverTimer(EventLoopTimerRef timer, void *userData);

int rfbDeferUpdateTime = 40; /* in ms */

static char reverseHost[255] = "";
static int reversePort = 5500;

CGDisplayErr displayErr;

// Server Data
rfbserver thisServer;
// List of Loaded Bundles
NSMutableArray *bundleArray = nil;

VNCServer *vncServerObject = nil;

//GRW - SharedAppVnc
int NMSG = 0;


//GRW
SharedApp *sharedApp = nil;
pthread_mutex_t sharedapp_mutex;
// GRW sharedAppVnc - made global here
pthread_t listener_thread;
pthread_t monitor_thread;


static void rfbScreenInit(void);

/*
 * rfbLog prints a time-stamped message to the log file (stderr).
 */

void rfbLog(char *format, ...) {
    va_list args;
    NSString *nsFormat = [[NSString alloc] initWithCString:format];

    pthread_mutex_lock(&logMutex);
    va_start(args, format);

    /*
     char buf[256];
     time_t clock;


     time(&clock);
     strftime(buf, 255, "%d/%m/%Y %T ", localtime(&clock));
     fprintf(stderr, buf);

     vfprintf(stderr, format, args);
     //fprintf(stderr, "\n");
     fflush(stderr);

     va_end(args);
     */
    NSLogv(nsFormat, args);
    va_end(args);

    [nsFormat release];
    pthread_mutex_unlock(&logMutex);
}

void rfbLogPerror(char *str) {
    rfbLog("%s: %s\n", str, strerror(errno));
}

void bundlesPerformSelector(SEL performSel) {
    NSAutoreleasePool *bundlePool = [[NSAutoreleasePool alloc] init];
    NSEnumerator *bundleEnum = [bundleArray objectEnumerator];
    NSBundle *bundle = nil;

    while ((bundle = [bundleEnum nextObject]))
        [(NSObject <RFBBundleProtocol> *)[bundle principalClass] performSelector:performSel];

    [bundlePool release];
}

// Some calls fail under older OS X'es so we will do some detected loading
void loadDynamicBundles(BOOL startup) {
    NSAutoreleasePool *startPool = [[NSAutoreleasePool alloc] init];
    NSBundle *osxvncBundle = [NSBundle mainBundle];
    NSString *execPath =[[NSProcessInfo processInfo] processName];

    // Setup thisServer structure
	thisServer.vncServer = vncServerObject;
	thisServer.desktopName = desktopName;
	thisServer.rfbPort = rfbPort;

    thisServer.keyTable = keyTable;
    thisServer.keyTableMods = keyTableMods;
    thisServer.pressModsForKeys = &pressModsForKeys;
	
    NSLog(@"Main Bundle: %@", [osxvncBundle bundlePath]);
    if (!osxvncBundle) {
        // If We Launched Relative - make it absolute
        if (![execPath isAbsolutePath])
            execPath = [[[NSFileManager defaultManager] currentDirectoryPath] stringByAppendingPathComponent:execPath];

        execPath = [execPath stringByStandardizingPath];
        execPath = [execPath stringByResolvingSymlinksInPath];

        osxvncBundle = [NSBundle bundleWithPath:execPath];
        //resourcesPath = [[[resourcesPath stringByDeletingLastPathComponent] stringByDeletingLastPathComponent] stringByAppendingPathComponent:@"Resources"];
    }

    if (osxvncBundle) {
        NSArray *bundlePathArray = [NSBundle pathsForResourcesOfType:@"bundle" inDirectory:[osxvncBundle resourcePath]];
        NSEnumerator *bundleEnum = [bundlePathArray reverseObjectEnumerator];
        NSString *bundlePath = nil;

        bundleArray = [[NSMutableArray alloc] init];

        while ((bundlePath = [bundleEnum nextObject])) {
            NSBundle *aBundle = [NSBundle bundleWithPath:bundlePath];

            NSLog(@"Loading Bundle %@", bundlePath);

            if ([aBundle load]) {
                [bundleArray addObject:aBundle];
                if (startup)
                    [[aBundle principalClass] rfbStartup: &thisServer];
            }
            else {
                NSLog(@"\t-Bundle Load Failed");
            }
        }
    }
    else {
        NSLog(@"No Bundles Loaded - Run %@ from inside OSXvnc.app", execPath);
    }

	// We might choose to RESYNC back from the server data if the bundles wanted to change some of the server state
	
    [startPool release];
}

void refreshCallback(CGRectCount count, const CGRect *rectArray, void *ignore) {
    BoxRec box;
    RegionRec region;
    rfbClientIteratorPtr iterator;
    rfbClientPtr cl = NULL;
    int i;

    if (rfbLocalBuffer && rfbClientsConnected()) {
        CGRect*		newRectArray = NULL;

        newRectArray = (CGRect*)malloc(sizeof(CGRect) * (count + 2));
        assert(newRectArray);

        if (count)
            memcpy(newRectArray, rectArray, sizeof(CGRect) * count);

        newRectArray[count] = rfbLocalBufferGetMouseRect();

        rfbLocalBufferSync(count, rectArray);

        newRectArray[count + 1] = rfbLocalBufferGetMouseRect();

        count += 2;
        rectArray = newRectArray;

        free(newRectArray);
    }

	//rfbLog("refresh for %d rects", count);
	
    for (i = 0; i < count; i++) {
        box.x1 = rectArray[i].origin.x;
        box.y1 = rectArray[i].origin.y;
        box.x2 = box.x1 + rectArray[i].size.width;
        box.y2 = box.y1 + rectArray[i].size.height;

        SAFE_REGION_INIT(&hackScreen, &region, &box, 0);

        iterator = rfbGetClientIterator();
        while ((cl = rfbClientIteratorNext(iterator)) != NULL) {
            pthread_mutex_lock(&cl->updateMutex);
            REGION_UNION(&hackScreen,&cl->modifiedRegion,&cl->modifiedRegion,&region);
            pthread_mutex_unlock(&cl->updateMutex);
            pthread_cond_signal(&cl->updateCond);
        }
        rfbReleaseClientIterator(iterator);

        REGION_UNINIT(&hackScreen, &region);
    }
}

void rfbCheckForScreenResolutionChange() {
    BOOL sizeChange = (rfbScreen.width != CGDisplayPixelsWide(displayID) ||
                       rfbScreen.height != CGDisplayPixelsHigh(displayID));

    // See if screen changed
    if (sizeChange || rfbScreen.bitsPerPixel != CGDisplayBitsPerPixel(displayID)) {
        rfbClientIteratorPtr iterator;
        rfbClientPtr cl = NULL;

        // Block listener from accepting new connections while we restart
        pthread_mutex_lock(&listenerAccepting);

        rfbLog("Screen Geometry Changed - (%d,%d) Depth: %d\n",
               CGDisplayPixelsWide(displayID),
               CGDisplayPixelsHigh(displayID),
               CGDisplayBitsPerPixel(displayID));

        iterator = rfbGetClientIterator();
        // Disconnect Existing Clients
        while ((cl = rfbClientIteratorNext(iterator))) {
            pthread_mutex_lock(&cl->updateMutex); 
            // Keep locked until after screen change
        }
		
		// GRW - this is bad, release clientIterator while updateMutex is locked
		// Another thread could grab the clientIterator and block on updateMutex causing deadlock
        rfbReleaseClientIterator(iterator); 

        rfbScreenInit();

        iterator = rfbGetClientIterator();
        while ((cl = rfbClientIteratorNext(iterator))) {
            // Only need to notify them on a SIZE change - other changes just make us re-init
            if (sizeChange) {
                if (cl->desktopSizeUpdate) {
                    BoxRec box;
                    rfbFramebufferUpdateMsg *fu = (rfbFramebufferUpdateMsg *)cl->updateBuf;
                    fu->type = rfbFramebufferUpdate;
                    fu->nRects = Swap16IfLE(1);
                    cl->ublen = sz_rfbFramebufferUpdateMsg;
					
					SHAREDAPP_TRACE("trace(%d): Message %d", NMSG++, rfbFramebufferUpdate);
					
                    rfbSendScreenUpdateEncoding(cl);

					// Reset Frame Buffer
					if (cl->scalingFrameBuffer && cl->scalingFrameBuffer != rfbGetFramebuffer())
						free(cl->scalingFrameBuffer);
					
					if (cl->scalingFactor == 1) {
						cl->scalingFrameBuffer = rfbGetFramebuffer();
						cl->scalingPaddedWidthInBytes = rfbScreen.paddedWidthInBytes;
					}
					else {
						const unsigned long csh = (rfbScreen.height+cl->scalingFactor-1)/ cl->scalingFactor;
						const unsigned long csw = (rfbScreen.width +cl->scalingFactor-1)/ cl->scalingFactor;
						
						cl->scalingFrameBuffer = malloc( csw*csh*rfbScreen.bitsPerPixel/8 );
						cl->scalingPaddedWidthInBytes = csw * rfbScreen.bitsPerPixel/8;
					}
					
                    box.x1 = box.y1 = 0;
                    box.x2 = rfbScreen.width;
                    box.y2 = rfbScreen.height;
					REGION_INIT(pScreen,&cl->modifiedRegion,&box,0);
                    //cl->needNewScreenSize = TRUE;
                }
                else
                    rfbCloseClient(cl);
            }
            else {
				// In theory we shouldn't need to disconnect them but some state in the cl record seems to cause a problem
				rfbCloseClient(cl);
                rfbSetTranslateFunction(cl);
            }

			sleep(2); // We may detect the new depth before OS X has quite finished getting everything ready for it.
            pthread_mutex_unlock(&cl->updateMutex);
            pthread_cond_signal(&cl->updateCond);
        }
        rfbReleaseClientIterator(iterator);

        // Accept new connections again
        pthread_mutex_unlock(&listenerAccepting);
    }
}

static void *clientOutput(void *data) {
    rfbClientPtr cl = (rfbClientPtr)data;
	NSAutoreleasePool *tempPool;
    RegionRec updateRegion;
    Bool haveUpdate = false;
	
    while (1) {
		
		tempPool = [[NSAutoreleasePool alloc] init];
		
        haveUpdate = false;
			
        pthread_mutex_lock(&cl->updateMutex);
        while (!haveUpdate) {
            if (cl->sock == -1) {
                /* Client has disconnected. */
                pthread_mutex_unlock(&cl->updateMutex);
                return NULL;
            }

            // Check for (and send immediately) pending PB changes
            rfbClientUpdatePasteboard(cl);
			
			// GRW - SharedAppVnc
			if ([sharedApp enabled] && cl->supportsSharedAppEncoding)
			{
				//get windows rects and set to cl->requestedRegion 
				//rfbLog("setting requested region");
				[sharedApp getSharedRegions:&cl->requestedRegion];
			}
			
			//rfbLog("Check for update");

			// Only do checks if we HAVE an outstanding request
			if (REGION_NOTEMPTY(&hackScreen, &cl->requestedRegion)) {

				//rfbLog("RequestedRegion not empty");
				
				/* REDSTONE */
				if (rfbDeferUpdateTime > 0 && !cl->immediateUpdate) {
					// Compare Request with Update Area
					REGION_INIT(&hackScreen, &updateRegion, NullBox, 0);
					REGION_INTERSECT(&hackScreen, &updateRegion, &cl->modifiedRegion, &cl->requestedRegion);
					haveUpdate = REGION_NOTEMPTY(&hackScreen, &updateRegion);

					REGION_UNINIT(&hackScreen, &updateRegion);
				}
				else {
					/*  If we've turned off deferred updating
					We are going to send an update as soon as we have a requested,
					regardless of if we have a "change" intersection */
					haveUpdate = TRUE;
				}

				// GRW - SharedAppVnc
				if ([sharedApp enabled] && cl->supportsSharedAppEncoding)
				{
					CGPoint p = currentCursorLoc();
					BoxRec resbox;
					if (POINT_IN_REGION(&hackScreen, &cl->requestedRegion, p.x, p.y, &resbox))
					{
						if (rfbShouldSendNewCursor(cl))
						{
							//rfbLog("NewCursor in region");
							haveUpdate = TRUE;
						}
						
						if (rfbShouldSendNewPosition(cl)) 
						{
							//rfbLog("NewPosition in region");
							haveUpdate = TRUE;
						}
					}
				
				} else {
					if (rfbShouldSendNewCursor(cl))
						haveUpdate = TRUE;
					else if (rfbShouldSendNewPosition(cl))
						// Could Compare with the request area but for now just always send it
						haveUpdate = TRUE;
					else if (cl->needNewScreenSize)
						haveUpdate = TRUE;
				}
				
				
			}
			
			// GRW - SharedAppVnc
			if ([sharedApp enabled] && [sharedApp areWindowsToClose]) haveUpdate = TRUE;
			
			if (!haveUpdate)
				pthread_cond_wait(&cl->updateCond, &cl->updateMutex);
        }
		//rfbLog("Outstanding requests exists, haveUpdate");
		
        // OK, now, to save bandwidth, wait a little while for more updates to come along.
        /* REDSTONE - Lets send it right away if no rfbDeferUpdateTime */
        if (rfbDeferUpdateTime > 0 && !cl->immediateUpdate && !cl->needNewScreenSize) {
            pthread_mutex_unlock(&cl->updateMutex);
            usleep(rfbDeferUpdateTime * 1000);
            pthread_mutex_lock(&cl->updateMutex);
        }

		//rfbLog("Sending Update");
		
        /* Now, get the region we're going to update, and remove
            it from cl->modifiedRegion _before_ we send the update.
            That way, if anything that overlaps the region we're sending
            is updated, we'll be sure to do another update later. */
        REGION_INIT(&hackScreen, &updateRegion, NullBox, 0);
        REGION_INTERSECT(&hackScreen, &updateRegion, &cl->modifiedRegion, &cl->requestedRegion);
        REGION_SUBTRACT(&hackScreen, &cl->modifiedRegion, &cl->modifiedRegion, &updateRegion);
        /* REDSTONE - We also want to clear out the requested region, so we don't process
            graphic updates in previously requested regions */
		// GRW move this down after send updates - so we can not clear if no windows to share
        //REGION_UNINIT(&hackScreen, &cl->requestedRegion);
        //REGION_INIT(&hackScreen, &cl->requestedRegion,NullBox,0);

        /*  This does happen but it's asynchronous (and slow to occur)
			what we really want to happen is to just temporarily hide the cursor (while sending to the remote screen)
			-- It's not even usually there (as it's handled by the display driver - but under certain occasions it does appear
         displayErr = CGDisplayHideCursor(displayID);
         if (displayErr != 0)
         rfbLog("Error Hiding Cursor %d", displayErr);
         CGDisplayMoveCursorToPoint(displayID, CGPointZero);
											*/

        /* Now actually send the update. */
		// GRW - SharedAppVnc
		if ([sharedApp enabled] && cl->supportsSharedAppEncoding)
		{
			char* prevScalingFrameBuffer = cl->scalingFrameBuffer;
			int   prevScalingPaddedWidthInBytes = cl->scalingPaddedWidthInBytes;
			
			pthread_mutex_lock(&sharedapp_mutex);
			[sharedApp rfbSendUpdates:cl screenRegion:updateRegion];
			pthread_mutex_unlock(&sharedapp_mutex);
			
			cl->scalingFrameBuffer = prevScalingFrameBuffer;
			cl->scalingPaddedWidthInBytes = prevScalingPaddedWidthInBytes;

		} else {
			REGION_UNINIT(&hackScreen, &cl->requestedRegion);
			REGION_INIT(&hackScreen, &cl->requestedRegion,NullBox,0);
			rfbSendFramebufferUpdate(cl, updateRegion);
		}
		
        /* If we were hiding it before make it reappear now
            displayErr = CGDisplayShowCursor(displayID);
        if (displayErr != 0)
            rfbLog("Error Showing Cursor %d", displayErr);
        */

        REGION_UNINIT(&hackScreen, &updateRegion);
        pthread_mutex_unlock(&cl->updateMutex);
		
		// GRW - SharedAppVnc
		// Moved checkForClosedWindows outside of the updateMutex lock because
		// It will need to iterate for each client and grab the lock.
		if ([sharedApp enabled]) [sharedApp checkForClosedWindows];
		[tempPool release];
    }

    return NULL;
}

static void *clientInput(void *data) {
    rfbClientPtr cl = (rfbClientPtr)data;
    pthread_t output_thread;

    pthread_create(&output_thread, NULL, clientOutput, (void *)cl);

    while (1) {
        bundlesPerformSelector(@selector(rfbReceivedClientMessage));
        rfbProcessClientMessage(cl);

        // Some people will connect but not request screen updates - just send events, this will delay registering the CG callback until then
        if (rfbShouldSendUpdates && !registered && REGION_NOTEMPTY(&hackScreen, &cl->requestedRegion)) {
            rfbLog("Client Connected - Registering Screen Update Notification\n");
            CGRegisterScreenRefreshCallback(refreshCallback, NULL);
            registered = TRUE;
        }
        
        if (cl->sock == -1) {
            /* Client has disconnected. */
            break;
        }
    }

    /* Get rid of the output thread. */
	rfbLog("get rid of output thread");
    //pthread_mutex_lock(&cl->updateMutex);
    pthread_cond_signal(&cl->updateCond);
    //pthread_mutex_unlock(&cl->updateMutex);
    pthread_join(output_thread, NULL);

	// GRW - SharedAppVnc
	[sharedApp removeClient:cl];
	rfbLog("client gone %s", cl->host);
	
    rfbClientConnectionGone(cl);

    return NULL;
}

void sighandler(int signum)
{
	//rfbLog("received signal\n");
	return;
}

static void *listenerRun(void *ignore) {
    int listen_fd, client_fd;
    struct sockaddr_in6 sin, peer;
    pthread_t client_thread;
    rfbClientPtr cl;
    int len, value;

    // GRW - SharedAppVnc - so we can catch signal to end thread
	struct sigaction sigact;
	sigset_t sigmask;
	memset(&sigact, 0, sizeof(sigact));
	sigact.sa_handler = sighandler;
	sigact.sa_flags = 0;
	sigaction(SIGHUP, &sigact, NULL);
	sigemptyset(&sigmask);
	sigaddset(&sigmask, SIGHUP);
	pthread_sigmask(SIG_UNBLOCK, &sigmask, NULL);
	
	
    bzero(&sin, sizeof(sin));
    sin.sin6_len = sizeof(sin);
    sin.sin6_family = AF_INET6;
    if (rfbLocalhostOnly)
        sin.sin6_addr = in6addr_loopback;
    else
        sin.sin6_addr = in6addr_any;
    sin.sin6_port = htons(rfbPort);

    if ((listen_fd = socket(PF_INET6, SOCK_STREAM, 0)) < 0) {
        return NULL;
    }
    value = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &value, sizeof(value)) < 0) {
        rfbLog("setsockopt SO_REUSEADDR failed\n");
    }

    if (bind(listen_fd, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
        rfbLog("Failed to Bind Socket: Port %d may be in use by another VNC\n", rfbPort);
		if (strlen(reverseHost)) {
			rfbLog("Listener Disabled\n");
			return NULL;
		}
		else {
			exit(250);
		}
    }

    if (listen(listen_fd, 5) < 0) {
        rfbLog("listen failed\n");
        exit(255);
    }

    len = sizeof(peer);
    rfbLog("Started Listener Thread on port %d\n", rfbPort);
	
	bundlesPerformSelector(@selector(rfbRunning));

    // This will block while wait for an accept // GRW SharedAppVnc added >0
    while ((client_fd = accept(listen_fd, (struct sockaddr *)&peer, &len))>0) {
		int one=1; 

        if (!rfbClientsConnected())
            rfbCheckForScreenResolutionChange();

        pthread_mutex_lock(&listenerAccepting);
		
		if (setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, (char *)&one, sizeof(one)) < 0)
			rfbLog("setsockopt TCP_NODELAY failed\n"); 
		
        if (rfbLocalBuffer && !rfbClientsConnected())
            rfbLocalBufferSyncAll();

        rfbUndim();
        cl = rfbNewClient(client_fd);
		[sharedApp addClient:cl];

        pthread_create(&client_thread, NULL, clientInput, (void *)cl);

        pthread_mutex_unlock(&listenerAccepting);
        pthread_cond_signal(&listenerGotNewClient);
    }

    //rfbLog("Accept failed %d\n", errno);

	// GRW - SharedAppVnc
	close(listen_fd);
	pthread_exit(NULL);

    //exit(1);
}

// GRW modified
rfbClientPtr connectReverseClient(const char* rHost, int rPort) {
    pthread_t client_thread;
    rfbClientPtr cl;
	
	rfbLog("Connecting to client %s:%d", rHost, rPort);
	
	pthread_mutex_lock(&listenerAccepting);
    rfbUndim();
    cl = rfbReverseConnection(rHost, rPort);
	if (cl) {	
		pthread_create(&client_thread, NULL, clientInput, (void *)cl);
		pthread_mutex_unlock(&listenerAccepting);
		pthread_cond_signal(&listenerGotNewClient);
	}
	else {
		pthread_mutex_unlock(&listenerAccepting);
	}
	return cl;
}

char *rfbGetFramebuffer(void)
{
    if (rfbLocalBuffer)
        return rfbFrontBufferBaseAddress();
    else
        return (char *)CGDisplayBaseAddress(displayID);
}

static void rfbScreenInit(void) {
    (void) GetMainDevice();
    // necessary to init the display manager,
    // otherwise CGDisplayBitsPerPixel doesn't
    // always works correctly after a resolution change

    if (CGDisplaySamplesPerPixel(displayID) != 3) {
        rfbLog("screen format not supported.  exiting.\n");
        exit(255);
    }

    if (rfbLocalBuffer) {
        rfbLocalBufferInfo(&rfbScreen.width, &rfbScreen.height,
                           &rfbScreen.bitsPerPixel, &rfbScreen.depth,
                           &rfbScreen.paddedWidthInBytes);
    }
    else {
        rfbScreen.width = CGDisplayPixelsWide(displayID);
        rfbScreen.height = CGDisplayPixelsHigh(displayID);
        rfbScreen.bitsPerPixel = CGDisplayBitsPerPixel(displayID);
        rfbScreen.depth = CGDisplaySamplesPerPixel(displayID) * CGDisplayBitsPerSample(displayID);
        rfbScreen.paddedWidthInBytes = CGDisplayBytesPerRow(displayID);
    }
    rfbServerFormat.bitsPerPixel = rfbScreen.bitsPerPixel;
    rfbServerFormat.depth = rfbScreen.depth;
    rfbServerFormat.bigEndian = !(rfbEndianTest);
/*
    if ((rfbScreen.bitsPerPixel) == 8) {
        rfbServerFormat.trueColour = FALSE;
    }
    else 
 */
	{
        int bitsPerSample = CGDisplayBitsPerSample(displayID);

        rfbServerFormat.trueColour = TRUE;

        rfbServerFormat.redMax = (1 << bitsPerSample) - 1;
        rfbServerFormat.greenMax = (1 << bitsPerSample) - 1;
        rfbServerFormat.blueMax = (1 << bitsPerSample) - 1;
        rfbServerFormat.redShift = bitsPerSample * 2;
        rfbServerFormat.greenShift = bitsPerSample;
        rfbServerFormat.blueShift = 0;
    }

    /* We want to use the X11 REGION_* macros without having an actual
        X11 ScreenPtr, so we do this.  Pretty ugly, but at least it lets us
        avoid hacking up regionstr.h, or changing every call to REGION_* */
    hackScreen.RegionCreate = miRegionCreate;
    hackScreen.RegionInit = miRegionInit;
    hackScreen.RegionCopy = miRegionCopy;
    hackScreen.RegionDestroy = miRegionDestroy;
    hackScreen.RegionUninit = miRegionUninit;
    hackScreen.Intersect = miIntersect;
    hackScreen.Union = miUnion;
    hackScreen.Subtract = miSubtract;
    hackScreen.Inverse = miInverse;
    hackScreen.RegionReset = miRegionReset;
    hackScreen.TranslateRegion = miTranslateRegion;
    hackScreen.RectIn = miRectIn;
    hackScreen.PointInRegion = miPointInRegion;
    hackScreen.RegionNotEmpty = miRegionNotEmpty;
    hackScreen.RegionEmpty = miRegionEmpty;
    hackScreen.RegionExtents = miRegionExtents;
    hackScreen.RegionAppend = miRegionAppend;
    hackScreen.RegionValidate = miRegionValidate;
}

static void usage(void) {
    fprintf(stderr, "\nAvailable options:\n\n");

    fprintf(stderr, "-rfbport port          TCP port for RFB protocol\n");
    fprintf(stderr, "-rfbwait time          max time in ms to wait for RFB client\n");
    fprintf(stderr, "-rfbauth passwd-file   use authentication on RFB protocol\n"
            "                       (use 'storepasswd' to create a password file)\n");
    fprintf(stderr, "-deferupdate time      time in ms to defer updates (default 0)\n");
    fprintf(stderr, "-desktop name          VNC desktop name (default \"MacOS X\")\n");
    fprintf(stderr, "-alwaysshared          always treat new clients as shared\n");
    fprintf(stderr, "-nevershared           never treat new clients as shared\n");
    fprintf(stderr, "-dontdisconnect        don't disconnect existing clients when a new non-shared\n"
            "                       connection comes in (refuse new connection instead)\n");
    fprintf(stderr, "-nodimming             never allow the display to dim\n"
            "                       (default: display can dim, input undims)\n");
    fprintf(stderr, "-maxdepth bits         maximum allowed bit depth for connecting clients.\n"
            "                       (default: bit depth of display)\n");
    /*
     fprintf(stderr, "-reversemods           reverse the interpretation of control\n");
     fprintf(stderr, "                       and command (for windows clients)\n");
     */
    fprintf(stderr, "-allowsleep            allow machine to sleep\n"
            "                       (default: sleep is disabled)\n");
    fprintf(stderr, "-disableScreenSaver    Disable screen saver while users are connected\n"
            "                       (default: no, allow screen saver to engage)\n");
    fprintf(stderr, "-swapButtons           swap mouse buttons 2 & 3\n"
            "                       (default: no)\n");
    fprintf(stderr, "-disableRemoteEvents   ignore remote keyboard, pointer, and pasteboard event\n"
            "                       (default: no, process them)\n");
	fprintf(stderr, "-connectHost host      Host Name or IP of listening client to establishing a reverse conneect\n");
	fprintf(stderr, "-connectPort port      TCP port of listening client to establishing a reverse conneect\n"
			"                       (default: 5500)\n");
	fprintf(stderr, "-noupdates             Prevent registering for screen updates, for use with x2vnc or win2vnc\n");
    /* This isn't ready to go yet
    {
        CGDisplayCount displayCount;
        CGDirectDisplayID activeDisplays[100];
        int index = 0;

        fprintf(stderr, "-display DisplayID     server displayID to indicate which display to serve\n");

        CGGetActiveDisplayList(100, activeDisplays, &displayCount);

        for (index=0; index < displayCount; index++)
            fprintf(stderr, "\t\t%d = (%ld,%ld)\n", index, CGDisplayPixelsWide(activeDisplays[index]), CGDisplayPixelsHigh(activeDisplays[index]));
    }
    */
    fprintf(stderr, "-localhost             Only allow connections from the same machine, literally localhost (127.0.0.1)\n");
    fprintf(stderr, "                       If you use SSH and want to stop non-SSH connections from any other hosts \n");
    fprintf(stderr, "                       (default: no, allow remote connections)\n");
    fprintf(stderr, "-restartonuserswitch flag  For Use on Panther 10.3 systems, this will cause the server to restart when a fast user switch occurs");
    fprintf(stderr, "                           (default: no)\n");
    bundlesPerformSelector(@selector(rfbUsage));
    fprintf(stderr, "\n");

    exit(255);
}

static void checkForUsage(int argc, char *argv[]) {
    int i;

    for (i = 1; i < argc; i++) {
        if (strncmp(argv[i], "-h", 2) == 0 ||
            strncmp(argv[i], "-H", 2) == 0 ||
            strncmp(argv[i], "--h", 3) == 0 ||
            strncmp(argv[i], "--H", 3) == 0 ||
            strncmp(argv[i], "-usage", 6) == 0 ||
            strncmp(argv[i], "--usage", 7) == 0 ||
            strcmp(argv[i], "-?") == 0) {
            loadDynamicBundles(FALSE);
            usage();
        }
    }
}

static void processArguments(int argc, char *argv[]) {
	char argString[1024] = "Arguments: ";
    int i;
	
    for (i = 1; i < argc; i++) {
		strcat(argString, argv[i]);
		strcat(argString, " ");
	}
	rfbLog(argString);
	
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-rfbport") == 0) { // -rfbport port
            if (i + 1 >= argc) usage();
            rfbPort = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-rfbwait") == 0) {  // -rfbwait ms
            if (i + 1 >= argc) usage();
            rfbMaxClientWait = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-rfbauth") == 0) {  // -rfbauth passwd-file
            if (i + 1 >= argc) usage();
            rfbAuthPasswdFile = argv[++i];
        } else if (strcmp(argv[i], "-connectHost") == 0) {  // -connect host
            if (i + 1 >= argc) usage();
			strncpy(reverseHost, argv[++i], 255);
            if (strlen(reverseHost) == 0) usage();
        } else if (strcmp(argv[i], "-connectPort") == 0) {  // -connect host
            if (i + 1 >= argc) usage();
            reversePort = atoi(argv[++i]);
		} else if (strcmp(argv[i], "-deferupdate") == 0) {  // -deferupdate ms
            if (i + 1 >= argc) usage();
            rfbDeferUpdateTime = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-maxdepth") == 0) {  // -maxdepth
            if (i + 1 >= argc) usage();
            rfbMaxBitDepth = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-desktop") == 0) {  // -desktop desktop-name
            if (i + 1 >= argc) usage();
			strncpy(desktopName, argv[++i], 255);
        } else if (strcmp(argv[i], "-display") == 0) {  // -display DisplayID
            CGDisplayCount displayCount;
            CGDirectDisplayID activeDisplays[100];
			
            CGGetActiveDisplayList(100, activeDisplays, &displayCount);
			
            if (i + 1 >= argc || atoi(argv[i+1]) >= displayCount)
                usage();
			
            displayID = activeDisplays[atoi(argv[++i])];
        } else if (strcmp(argv[i], "-alwaysshared") == 0) {
            rfbAlwaysShared = TRUE;
        } else if (strcmp(argv[i], "-nevershared") == 0) {
            rfbNeverShared = TRUE;
        } else if (strcmp(argv[i], "-dontdisconnect") == 0) {
            rfbDontDisconnect = TRUE;
        } else if (strcmp(argv[i], "-nodimming") == 0) {
            rfbNoDimming = TRUE;
        } else if (strcmp(argv[i], "-allowsleep") == 0) {
            rfbNoSleep = FALSE;
        } else if (strcmp(argv[i], "-reversemods") == 0) {
            rfbReverseMods = TRUE;
        } else if (strcmp(argv[i], "-disableScreenSaver") == 0) {
            rfbDisableScreenSaver = TRUE;
        } else if (strcmp(argv[i], "-swapButtons") == 0) {
            rfbSwapButtons = TRUE;
        } else if (strcmp(argv[i], "-disableRemoteEvents") == 0) {
            rfbDisableRemote = TRUE;
        } else if (strcmp(argv[i], "-rfbLocalBuffer") == 0) {
            rfbLog("WARNING - rfbLocalBuffer option is Deprecated and will be removed soon");
            rfbLocalBuffer = TRUE;
        } else if (strcmp(argv[i], "-localhost") == 0) {
            rfbLocalhostOnly = TRUE;
        } else if (strcmp(argv[i], "-inhibitevents") == 0) {
            rfbInhibitEvents = TRUE;
		} else if (strcmp(argv[i], "-noupdates") == 0) {
			rfbShouldSendUpdates = FALSE;
		} 	
		else if (strcmp(argv[i], "-restartonuserswitch") == 0) {
			if (i + 1 >= argc) 
				usage();
			else {
				char *argument = argv[++i];
				restartOnUserSwitch = (argument[0] == 'y' || argument[0] == 'Y' || argument[0] == 't' || argument[0] == 'T' || atoi(argument));
			}
		}
	}
	
	if (!rfbAuthPasswdFile)
		rfbLog("Note: No password file specified, running with no authentication");
}

void rfbShutdown(void) {
    bundlesPerformSelector(@selector(rfbShutdown));
    [bundleArray release];

    CGUnregisterScreenRefreshCallback(refreshCallback, NULL);
    //CGDisplayShowCursor(displayID);
    rfbDimmingShutdown();

    [[[NSWorkspace sharedWorkspace] notificationCenter] removeObserver: vncServerObject];

    if (rfbDisableScreenSaver) {
        /* remove the screensaver timer */
		// GRW - SharedAppVnc 
		// turn screensaver back on
		setScreenSaver(YES);
        //RemoveEventLoopTimer(screensaverTimer);
        //DisposeEventLoopTimerUPP(screensaverTimerUPP);
    }

    if (rfbLocalBuffer)
        rfbLocalBufferShutdown();
}

static void rfbShutdownOnSignal(int signal) {
    rfbLog("OSXvnc-server received signal: %d\n", signal);
    rfbShutdown();

    exit (signal);
}

void daemonize( void ) {
    int i;

    // Fork New Process
    if ( fork() != 0 )
        exit( 0 );

    // Become session leader
    setsid();

    // Ignore signals here
    signal(SIGHUP, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);
    signal(SIGTERM, rfbShutdownOnSignal);
    signal(SIGINT, rfbShutdownOnSignal);
    signal(SIGQUIT, rfbShutdownOnSignal);
    
    // Fork a second new process
    if ( fork( ) != 0 )
        exit( 0 );

    // chdir ( "/" );
    umask( 0 );

    // Close open FDs
    for ( i = getdtablesize( ) - 1; i > STDERR_FILENO; i-- )
        close( i );

    /* from this point on we should only send output to server log or syslog */
}


static void *monitorRun(void *ignore) {
	// This segment is what is responsible for causing the server to shutdown when a user logs out
    // The problem being that OS X sends it first a SIGTERM and then a SIGKILL (un-trappable)
    // Presumable because it's running a Carbon Event loop
	BOOL keepRunning = TRUE;
	OSStatus resultCode = 0;
	
	while (keepRunning) {
		// No Clients - go into hibernation
		if (!rfbClientsConnected()) {
			pthread_mutex_lock(&listenerAccepting);
			
			// You would think that there is no point in getting events with no clients
			// But it seems that unregistering but keeping the process (or event loop) around can cause a stuttering behavior in OS X.
			if (0 && registered) {
				rfbLog("UnRegistering Screen Update Notification - waiting for clients\n");
				CGUnregisterScreenRefreshCallback(refreshCallback, NULL);
				registered = NO;
			}
			else
				rfbLog("Waiting for clients\n");
			
			pthread_cond_wait(&listenerGotNewClient, &listenerAccepting);
			
			pthread_mutex_unlock(&listenerAccepting);
		}
		bundlesPerformSelector(@selector(rfbPoll));
		
		rfbCheckForPasteboardChange();
		rfbCheckForCursorChange();
		rfbCheckForScreenResolutionChange();
		// Run The Event loop a moment to see if we have a screen update
		// No better luck with RunApplicationEventLoop() avoiding the shutdown on logout problem
		resultCode = RunCurrentEventLoop(kEventDurationSecond/30); //EventTimeout
		if (resultCode != eventLoopTimedOutErr) {
			rfbLog("Received Result: %d during event loop, Shutting Down", resultCode);
			keepRunning = NO;
			
		}
	}
	rfbShutdown();
	[NSApp terminate];
	return NULL;
}

// GRW SharedAppVnc
void restartListenerThread()
{
	int retval;
	void *exitcode;
	
	pthread_kill(listener_thread, SIGHUP);
	//rfbLog("joining thread");
	retval = pthread_join(listener_thread, &exitcode);
	//rfbLog("listener joined %d", retval);
	pthread_create(&listener_thread, NULL, listenerRun, NULL);
}

//GRW SharedAppVnc
void setShareApp(SharedApp* sa)
{
	sharedApp = sa;
}



int main(int argc, char *argv[]) {
	NSAutoreleasePool *tempPool = [[NSAutoreleasePool alloc] init];
    vncServerObject = [[VNCServer alloc] init];
    checkForUsage(argc,argv);
    
    // This guarantees separating us from any terminal - 
	// Right now this causes problems with the keep-alive script and the GUI app (since it causes the process to return right away)
	// it allows you to survive when launched in SSH, etc but doesn't solves the problem of being killed on GUI logout.
	// It also doesn't help with any of the pasteboard security issues, those requires secure sessionID's, see:
	// http://developer.apple.com/documentation/MacOSX/Conceptual/BPMultipleUsers/index.html
	// 
	// daemonize();

    // Let's not shutdown on a SIGHUP at some point perhaps we can use that to reload configuration
    signal(SIGHUP, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);
    signal(SIGTERM, rfbShutdownOnSignal);
    signal(SIGINT, rfbShutdownOnSignal);
    signal(SIGQUIT, rfbShutdownOnSignal);

    // This fix should be in CGDirectDisplay.h
#undef kCGDirectMainDisplay
    
#if MAC_OS_X_VERSION_MIN_REQUIRED < MAC_OS_X_VERSION_10_2
#define kCGDirectMainDisplay ((CGDirectDisplayID)0)
#warning Using Obsolete kCGDirectMainDisplay for backwards compatibility to 10.1
#else
#define kCGDirectMainDisplay CGMainDisplayID()
#endif
    
    displayID = kCGDirectMainDisplay;

    //pthread_t listener_thread;
	//pthread_t monitor_thread;

    pthread_mutex_init(&logMutex, NULL);
    pthread_mutex_init(&listenerAccepting, NULL);
    pthread_cond_init(&listenerGotNewClient, NULL);

    pthread_mutex_init(&sharedapp_mutex, NULL); //SHAREDAPP - GRW
    
	loadKeyTable();

    processArguments(argc, argv);

	// If no Desktop Name Provided Try to Get it
	if (strlen(desktopName) == 0) {
		[[[NSProcessInfo processInfo] hostName] getCString:desktopName];
	}
	
	loadDynamicBundles(TRUE);

    if (rfbLocalBuffer)
        rfbLocalBufferInit();

    rfbScreenInit();
    rfbClientListInit();
    rfbDimmingInit();

	initPasteboard();

    // Register for User Switch Notification
    // This works on non-Panther systems since the Notification just wont get called
    if (restartOnUserSwitch) {
        [[[NSWorkspace sharedWorkspace] notificationCenter] addObserver:vncServerObject
                                                               selector:@selector(userSwitched:)
                                                                   name:@"NSWorkspaceSessionDidBecomeActiveNotification"
                                                                 object:nil];
        [[[NSWorkspace sharedWorkspace] notificationCenter] addObserver:vncServerObject
                                                               selector:@selector(userSwitched:)
                                                                   name:@"NSWorkspaceSessionDidResignActiveNotification"
                                                                 object:nil];
    }
    
	
    /* That's great that they #define it to use the new symbol that doesn't exist in older versions
        better to just not even define it - but give a warning or something  */
    // This should be in CGRemoteOperationApi.h
#if MAC_OS_X_VERSION_MIN_REQUIRED < MAC_OS_X_VERSION_10_3
#undef CGSetLocalEventsFilterDuringSupressionState
#warning Using Obsolete CGSetLocalEventsFilterDuringSupressionState for backwards compatibility to 10.2
CG_EXTERN CGError CGSetLocalEventsFilterDuringSupressionState(CGEventFilterMask filter, CGEventSuppressionState state);
#endif
    
    // Does this need to be in 10.1 and greater (does any of this stuff work in 10.0?)
    if (!rfbInhibitEvents) {
        //NSLog(@"Core Graphics - Event Suppression Turned Off");
        // This seems to actually sometimes inhibit REMOTE events as well, but all the same let's let everything pass through for now
        CGSetLocalEventsFilterDuringSupressionState(kCGEventFilterMaskPermitAllEvents, kCGEventSupressionStateSupressionInterval);
        CGSetLocalEventsFilterDuringSupressionState(kCGEventFilterMaskPermitAllEvents, kCGEventSupressionStateRemoteMouseDrag);
    }

    if (rfbDisableScreenSaver) {
        /* setup screen saver disabling timer */
		// GRW - sharedAppVnc
		setScreenSaver(NO);
		
        //screensaverTimerUPP = NewEventLoopTimerUPP(rfbScreensaverTimer);
        //InstallEventLoopTimer(GetMainEventLoop(),
        //                      kEventDurationSecond * 30,
        //                      kEventDurationSecond * 30,
        //                      screensaverTimerUPP,
        //                      NULL,
        //                      &screensaverTimer);
    }

    pthread_create(&listener_thread, NULL, listenerRun, NULL);
		
	if (strlen(reverseHost) > 0)
		connectReverseClient(reverseHost, reversePort);
	

	// This segment is what is responsible for causing the server to shutdown when a user logs out
    // The problem being that OS X sends it first a SIGTERM and then a SIGKILL (un-trappable)
    // Presumable because it's running a Carbon Event loop
	
	// GRW SharedAppVnc - the loop below has been moved to a thread
#if 0 // Don't use monitor thread - causes hang after sleep. We aren't using pasterboard
	  // screenres change, or keyboard layout change. Cursor change is handled by clients.
	pthread_create(&monitor_thread, NULL, monitorRun, NULL);
#endif
	
	/* -- moved to a thread --
    if (1) {
        BOOL keepRunning = TRUE;
        OSStatus resultCode = 0;
		
        while (keepRunning) {
            // No Clients - go into hibernation
            if (!rfbClientsConnected()) {
                pthread_mutex_lock(&listenerAccepting);

                // You would think that there is no point in getting events with no clients
                // But it seems that unregistering but keeping the process (or event loop) around can cause a stuttering behavior in OS X.
                if (0 && registered) {
                    rfbLog("UnRegistering Screen Update Notification - waiting for clients\n");
                    CGUnregisterScreenRefreshCallback(refreshCallback, NULL);
                    registered = NO;
                }
                else
                    rfbLog("Waiting for clients\n");
                    
                pthread_cond_wait(&listenerGotNewClient, &listenerAccepting);

                pthread_mutex_unlock(&listenerAccepting);
            }
            bundlesPerformSelector(@selector(rfbPoll));

            rfbCheckForPasteboardChange();
            rfbCheckForCursorChange();
            rfbCheckForScreenResolutionChange();
            // Run The Event loop a moment to see if we have a screen update
            // No better luck with RunApplicationEventLoop() avoiding the shutdown on logout problem
            resultCode = RunCurrentEventLoop(kEventDurationSecond/30); //EventTimeout
            if (resultCode != eventLoopTimedOutErr) {
                rfbLog("Received Result: %d during event loop, Shutting Down", resultCode);
                keepRunning = NO;
            }
        }
    }
    else while (1) {
        // So this looks like it should fix it but I get no response on the CGWaitForScreenRefreshRect....
        // It doesn't seem to get called at all when not running an event loop
        CGRectCount rectCount;
        CGRect *rectArray;
        CGEventErr result;

        result = CGWaitForScreenRefreshRects( &rectArray, &rectCount );
        refreshCallback(rectCount, rectArray, NULL);
        CGReleaseScreenRefreshRects( rectArray );
    }
    */
		
		// GRW - SharedApp
	NSApplicationLoad();
	[NSApplication sharedApplication];
	[NSBundle loadNibNamed:@"SharedApp" owner:NSApp];
    [NSApp run];
	
	[tempPool release];

	rfbShutdown();

    return 0;
}

