/*
 * Source File: SharedApp.m 
 * Project: SharedAppVnc OS X Server
 *
 * Copyright (C) 2005 Grant Wallace, Princeton University. All Rights Reserved.
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

//  Created by Grant Wallace on 9/23/05.
//  Copyright 2005 Princeton University. All rights reserved.
//

#include <pthread.h>
#import "SharedApp.h"
#import "SharedAppClient.h"
#include "CGS-Private.h"

extern int CGSCurrentCursorSeed(void);
extern void refreshCallback(CGRectCount count, const CGRect *rectArray, void *ignore);
extern rfbClientPtr connectReverseClient(const char* reverseHost, int reversePort);
//extern CGPoint currentCursorLoc();
extern void setShareApp(SharedApp* sa);
extern void restartListenerThread();

extern ScreenRec hackScreen;

#define preferences [NSUserDefaults standardUserDefaults]

NSString *DKenableSharedApp = @"EnableAppSharing";
NSString *DKdisableRemoteEvents = @"DisableRemoteEvents";
NSString *DKlimitToLocalConnections = @"LimitToLocalConnections";
NSString *DKswapMouse = @"SwapMouseButtons23";
NSString *DKpreventDimming = @"PreventDisplayDimming";
NSString *DKpreventSleep = @"PreventComputerSleeping";
NSString *DKpasswordFile = @"PasswordFile";
NSString *DKrecentClientsArray = @"RecentClients";
NSString *DKautosaveName = @"SharedAppVncWindow";

@implementation SharedApp

+ (void)initialize { 
	// create the user defaults here if none exists
    NSMutableDictionary *defaultPrefs = [NSMutableDictionary dictionary];
    
	// put default prefs in the dictionary
    [defaultPrefs setObject:[NSNumber numberWithBool:YES] forKey:DKenableSharedApp];
	[defaultPrefs setObject:[NSNumber numberWithBool:YES] forKey:DKdisableRemoteEvents];
	[defaultPrefs setObject:[NSNumber numberWithBool:YES] forKey:DKlimitToLocalConnections];
	[defaultPrefs setObject:[NSNumber numberWithBool:NO] forKey:DKswapMouse];
	[defaultPrefs setObject:[NSNumber numberWithBool:NO] forKey:DKpreventDimming];
	[defaultPrefs setObject:[NSNumber numberWithBool:NO] forKey:DKpreventSleep];
	//[defaultPrefs setObject:[NSNumber numberWithBool:NO] forKey:DKpreventScreenSaver];
    [defaultPrefs setObject:@"" forKey:@"PasswordFile"];
    
// register the dictionary of defaults
    [preferences registerDefaults: defaultPrefs];
}

- (id)init
{
	self = [super init]; //self = [super initWithWindowNibName:@"WindowSelector"];
	arrayLock = [[NSLock alloc] init];
	pixelLock = [[NSLock alloc] init];
	sharedWindowsArray = [[NSMutableArray alloc] init];
	windowsToBeClosedArray = [[NSMutableArray alloc] init];
	connectedClientsArray = [[NSMutableArray alloc] init];
	prevClientsArray = [[NSMutableArray alloc] init];
	setShareApp(self);

	return self;
}

-(void)dealloc
{
	[window saveFrameUsingName:DKautosaveName];
	[sharedWindowsArray removeAllObjects];
	[sharedWindowsArray release];
	[windowsToBeClosedArray removeAllObjects];
	[windowsToBeClosedArray release];
	[connectedClientsArray removeAllObjects];
	[connectedClientsArray release];
	[prevClientsArray removeAllObjects];
	[prevClientsArray release];
	[arrayLock release];
	[pixelLock release];
	[passwordFile release];
	[super dealloc];
}


- (void)awakeFromNib
{
	[window setFrameAutosaveName:DKautosaveName];
	[window setFrameUsingName:DKautosaveName];

    [self loadUserDefaults:self];

}

- (void) loadUserDefaults: sender 
{
	[self setEnabled:[preferences boolForKey:DKenableSharedApp]];
	[self setDisableRemoteEvents:[preferences boolForKey:DKdisableRemoteEvents]];
	[self setLimitLocal:[preferences boolForKey:DKlimitToLocalConnections]];
	[self setSwapMouse:[preferences boolForKey:DKswapMouse]];
	[self setNoDimming:[preferences boolForKey:DKpreventDimming]];
	[self setNoSleep:[preferences boolForKey:DKpreventSleep]];
	//[self setNoScreenSaver:[preferences boolForKey:DKpreventScreenSaver]];
				
	[prevClientsArray addObjectsFromArray:[preferences stringArrayForKey:DKrecentClientsArray]];
	if ([prevClientsArray count] > 0)
	{
		[comboboxConnectToClient setStringValue:[prevClientsArray objectAtIndex:0]];
		[comboboxConnectToClient addItemsWithObjectValues:prevClientsArray];	
	}
	
	//passwordfile setup from VNCController.h
	NSArray *passwordFiles = [NSArray arrayWithObjects:
        [preferences stringForKey:DKpasswordFile],
        //[[[NSBundle mainBundle] bundlePath] stringByAppendingPathComponent:@".osxvncauth"],
		@"~/.osxvncauth",
        @"/tmp/.osxvncauth",
        nil];
    NSEnumerator *passwordEnumerators = [passwordFiles objectEnumerator];
    // Find first writable location for the password file
    while (passwordFile = [passwordEnumerators nextObject]) 
	{
        passwordFile = [passwordFile stringByStandardizingPath];
        if ([passwordFile length] && [self canWriteToFile:passwordFile])
		{
            [passwordFile retain];
			[preferences setObject:passwordFile forKey:DKpasswordFile];
			if ([[NSFileManager defaultManager] fileExistsAtPath:passwordFile])
			{
				[textfieldPassword setStringValue:@"********"];
				rfbAuthPasswdFile = strdup([passwordFile cString]);
			} else {
				[textfieldPassword setStringValue:@""];
			}
			rfbLog("using passwordfile %s", rfbAuthPasswdFile);
            break;
        }
	}

}

- (BOOL) canWriteToFile: (NSString *) path 
{
    if ([[NSFileManager defaultManager] fileExistsAtPath:path])
        return [[NSFileManager defaultManager] isWritableFileAtPath:path];
    else
        return [[NSFileManager defaultManager] isWritableFileAtPath:[path stringByDeletingLastPathComponent]];
}


- (IBAction) actionShowPreferencePanel:(id)sender
{
	NSPoint origin = [window frame].origin;
	origin.x += 10;
	origin.y += 10;
	[preferencePanel setFrameOrigin:origin];
	[preferencePanel orderFront:self];
}



-(IBAction) changePassword:(id)sender
{
	if (![[textfieldPassword stringValue] isEqualToString:@"********"])
	{
        [[NSFileManager defaultManager] removeFileAtPath:passwordFile handler:nil];
		
        if ([[textfieldPassword stringValue] length]) 
		{
            if (vncEncryptAndStorePasswd((char *)[[textfieldPassword stringValue] cString], (char *)[passwordFile cString]) != 0)
			{
				[textfieldPassword setStringValue:@""];
				[preferences setObject:@"" forKey:DKpasswordFile];
				rfbLog("Password Error! Unable to store password to %s", rfbAuthPasswdFile);
				NSRunAlertPanel(@"Password Error!", @"Problem - Unable to store password", @"OK", nil, nil);
            } else {
				[textfieldPassword setStringValue:@"********"];
				//rfbLog("stored password to file %s === %s", rfbAuthPasswdFile, [passwordFile cString]);
			}
        }
    }
}

-(IBAction) changeDimming:(id)sender
{
	[self setNoDimming:[buttonPreventDimming state]];
}

-(void) setNoDimming:(BOOL)flag
{
	if (rfbNoDimming != flag)
	{
		rfbNoDimming = flag;
		if (rfbNoDimming)
		{
			// turn dimming off
			setDimming(NO);
		} else {
			// turn dimming on
			setDimming(YES);
		}
	}
	[buttonPreventDimming setState:flag];
	[preferences setBool:flag forKey:DKpreventDimming];
	
}

-(IBAction) changeSleep:(id)sender
{
	[self setNoSleep:[buttonPreventSleeping state]];
}

-(void) setNoSleep:(BOOL)flag
{
	if (rfbNoSleep != flag)
	{
		rfbNoSleep = flag;
		if (rfbNoSleep)
		{
			// turn sleep off
			setSleep(NO);
			// turn screensaver off also
			rfbDisableScreenSaver = TRUE;
			setScreenSaver(NO);
		} else {
			// turn sleep on
			setSleep(YES);
			// turn screensaver on also
			rfbDisableScreenSaver = FALSE;
			setScreenSaver(YES);
		}
	}
	[buttonPreventSleeping setState:flag];
	[preferences setBool:flag forKey:DKpreventSleep];
	
}


-(IBAction) changeSwapMouse:(id)sender
{
	[self setSwapMouse:[buttonSwapMouseButtons state]];
}

-(void) setSwapMouse:(BOOL) flag
{
	if (rfbSwapButtons != flag)
	{
		rfbClientIteratorPtr iterator;
		rfbClientPtr cl = NULL;
		
		rfbSwapButtons = flag;
		
		iterator = rfbGetClientIterator();
		while ((cl = rfbClientIteratorNext(iterator)) != NULL) 
		{
			pthread_mutex_lock(&cl->updateMutex);
			cl->swapMouseButtons23 = rfbSwapButtons; 
			pthread_mutex_unlock(&cl->updateMutex);
		}
		rfbReleaseClientIterator(iterator);
	}
	[buttonSwapMouseButtons setState:flag];
	[preferences setBool:flag forKey:DKswapMouse];
}


-(IBAction) changeDisableRemoteEvents:(id)sender
{
    [self setDisableRemoteEvents:[buttonDisableRemoteEvents state]];
}

-(void) setDisableRemoteEvents:(BOOL) flag
{
	if (rfbDisableRemote != flag)
	{
		rfbClientIteratorPtr iterator;
		rfbClientPtr cl = NULL;
		
		rfbDisableRemote = flag;
		
		iterator = rfbGetClientIterator();
		while ((cl = rfbClientIteratorNext(iterator)) != NULL) 
		{
			pthread_mutex_lock(&cl->updateMutex);
			cl->disableRemoteEvents = rfbDisableRemote;
			pthread_mutex_unlock(&cl->updateMutex);
		}
		rfbReleaseClientIterator(iterator);
	}
	[buttonDisableRemoteEvents setState:flag];
	[preferences setBool:flag forKey:DKdisableRemoteEvents];
}

-(IBAction) changeLimitLocal:(id)sender
{
	[self setLimitLocal:[buttonLimitToLocalConnections state]];
}

-(void) setLimitLocal:(BOOL) flag
{
	if (rfbLocalhostOnly != flag)
	{
		rfbLocalhostOnly = flag;
		restartListenerThread();
	}
	[buttonLimitToLocalConnections setState:flag];
	[preferences setBool:flag forKey:DKlimitToLocalConnections];
}

- (IBAction)changeSharing:(id)sender {
	BOOL state = ([buttonEnable state] == NSOffState) ? TRUE : FALSE;
	[self setEnabled:state];
}

-(void) setEnabled:(BOOL)flag
{

	if (flag)
	{
		[buttonUnshare setEnabled:YES];
		[buttonUnshareAll setEnabled:YES];
		[buttonSelectToShare setEnabled:YES];
		[tableSharedWindows setHidden:NO];
		
		[self refreshAllWindows];
	}
	else 
	{
		[buttonUnshare setEnabled:NO];
		[buttonUnshareAll setEnabled:NO];
		[buttonSelectToShare setEnabled:NO];
		[tableSharedWindows setHidden:YES];
		
		CGRectCount rectCount = 1;
		CGRect rectArray[1];
		rectArray[0].origin.x = 0;
		rectArray[0].origin.y = 0;
		rectArray[0].size.width = rfbScreen.width;
		rectArray[0].size.height = rfbScreen.height;
		refreshCallback(rectCount, rectArray, NULL);
		[self resetClientRequestedArea];
	}
	enabled = flag;
	[buttonEnable setState:!flag];
	[preferences setBool:flag forKey: DKenableSharedApp];
}

- (BOOL) enabled
{
	return enabled;
}


- (IBAction) actionConnectToClient: (id)sender
{
	rfbClientPtr cl = nil;
	NSArray *textArray;
	NSString *usertext;
	const char *host;
	int port;
	
	usertext = [comboboxConnectToClient stringValue];
	textArray = [usertext componentsSeparatedByString:@":"];
	if ([textArray count] <= 0) return;
	else if ([textArray count] == 1)
	{
		host = [[textArray objectAtIndex:0] cString];
		port = 5500;
	}
	if ([textArray count] == 2)
	{
		host = [[textArray objectAtIndex:0] cString];
		port = [[textArray objectAtIndex:1] intValue];
	} 

	if (port < 100) port += 5500;
	cl = connectReverseClient(host, port);
	
	if (cl)
	{
		SharedAppClient *client = [[SharedAppClient alloc] initWithRfbClient:cl];
		[arrayLock lock];
		[connectedClientsArray addObject:client];
		[arrayLock unlock];
		[tableConnectedClients reloadData];
		[client release];
		
		// update user defaults
		[prevClientsArray removeObject:usertext];
		[prevClientsArray insertObject:usertext atIndex:0];
		[preferences setObject:prevClientsArray forKey: DKrecentClientsArray];
		[comboboxConnectToClient removeAllItems];
		[comboboxConnectToClient addItemsWithObjectValues: prevClientsArray];
	} else {
		rfbLog("Connection Failed: NULL cl");
		NSRunAlertPanel(@"Connection Error!", @"Unable to connect to client %s:%d", @"OK", nil, nil, host, port);
		
	}
	[self refreshAllWindows];
}

-(void) addClient: (rfbClientPtr)cl
{
	SharedAppClient *client = [[SharedAppClient alloc] initWithRfbClient:cl];
	[arrayLock lock];
	[connectedClientsArray addObject:client];
	[arrayLock unlock];
	[tableConnectedClients reloadData];
	[client release];
	[self refreshAllWindows];
}


- (IBAction) actionDisconnectClient: (id)sender
{
	int rowIndex = [tableConnectedClients selectedRow];
	if (rowIndex == -1) return;
	
	SharedAppClient *client = [connectedClientsArray objectAtIndex:rowIndex];
	rfbCloseClient([client clientStruct]);
	[arrayLock lock];
	[connectedClientsArray removeObject:client];
	[arrayLock unlock];
	[tableConnectedClients reloadData];
}



- (IBAction)actionSelectWindow:(id)sender {
	NSScreen			*screen;
	NSRect				screen_rect;

	screen						= [NSScreen mainScreen];
	screen_rect					= [screen frame];
	screen_rect.origin.x		= 0;
	screen_rect.origin.y		= 0;
	cover_window				= [[DTFullScreenWindow alloc] initWithContentRect:screen_rect styleMask:NSBorderlessWindowMask backing:NSBackingStoreBuffered defer:NO screen:screen];
	[cover_window set_controller:self];
	[cover_window setAlphaValue:0.0];
	[cover_window setIgnoresMouseEvents:NO];
	[cover_window setLevel:CGShieldingWindowLevel()];
	[cover_window makeKeyAndOrderFront:self];
	 
}

- (void)finish_select:(CGPoint)loc {
	int				unknown, res;
	CGSWindow		wid = 0;
	CGSConnection   con = _CGSDefaultConnection(), cid = 0;
	
	if (cover_window) {
		[cover_window orderOut:self];
		[cover_window close];
		cover_window	= 0;
	}
	
	res	= CGSFindWindowAndOwner(con, 0,1,0, &loc, &unknown, &wid, &cid);
	if (!res && wid && cid) {
		// We sucessfully found the window id
		VNCWinInfo *win;		
		NSEnumerator *enumerator;
	
		enumerator = [[NSArray arrayWithArray:sharedWindowsArray] objectEnumerator];
		while (win = [enumerator nextObject]) {
			if ([win windowId] == wid)
			{
				NSLog(@"Duplicate window %d", wid);
				return;
			}
		}

		win = [[VNCWinInfo alloc] initWithWindowId: wid connectionId: cid];
		
		[arrayLock lock];
		[sharedWindowsArray addObject:win];
		[arrayLock unlock];

		[tableSharedWindows reloadData];
		[self refreshWindow:win];
		//usleep(10000);
		//[win getVisibleRegion];
		[win release];
	}

}


- (IBAction)actionHideSelected:(id)sender {

	int rowIndex = [tableSharedWindows selectedRow];
	if (rowIndex == -1) return;
	
	VNCWinInfo *win = [sharedWindowsArray objectAtIndex:rowIndex];
	
	[arrayLock lock];
	if (![windowsToBeClosedArray containsObject:win])
	{
		[windowsToBeClosedArray addObject:win];
	}
	[sharedWindowsArray removeObject:win];
	[arrayLock unlock];
	
	[tableSharedWindows reloadData];
	
}


- (IBAction)actionHideAll: (id)sender {
	VNCWinInfo *win;
	NSEnumerator *enumerator;
	
	[arrayLock lock];
	enumerator = [sharedWindowsArray objectEnumerator];
	while (win = [enumerator nextObject]) 
	{
		if (![windowsToBeClosedArray containsObject:win])
		{
			[windowsToBeClosedArray addObject:win];
		}
	}
	[sharedWindowsArray removeAllObjects];
	[arrayLock unlock];	
	
	[tableSharedWindows reloadData];

}


-(void) refreshAllWindows
{
	VNCWinInfo *win;		
	NSEnumerator *enumerator;
	
	enumerator = [[NSArray arrayWithArray:sharedWindowsArray] objectEnumerator];
	while (win = [enumerator nextObject]) {
		[self refreshWindow:win];
	}
}

-(void) refreshWindow:(VNCWinInfo*)win
{
	CGRectCount rectCount = 1;
	CGRect rectArray[1];
	
	[win setDoFullUpdate:TRUE];
	
    rectArray[0].origin.x = [win originX];
    rectArray[0].origin.y = [win originY];
	rectArray[0].size.width = [win width];
	rectArray[0].size.height = [win height];
	
	//rfbLog("Sending refresh for rect %f %f %f %f", 
	//	   rectArray[0].origin.x, rectArray[0].origin.y, rectArray[0].size.width, rectArray[0].size.height);
	
	refreshCallback(rectCount, rectArray, NULL);
	
	return;
}

// NSTableView callbacks
- (int)numberOfRowsInTableView : (NSTableView*)table {

	if (table == tableSharedWindows) 
	{
		return [sharedWindowsArray count];
	}
	else if (table == tableConnectedClients)
	{
		return [connectedClientsArray count];
	}
	else return 0;
}


- (id)tableView:(NSTableView*)table objectValueForTableColumn:(NSTableColumn*)tableColumn row:(int)rowIndex {
	
	int			count;
	id           theObject = nil, theValue = nil;
    NSArray     *dataSource = nil;
	NSString    *identifier = [tableColumn identifier];
			
	if (table == tableSharedWindows) dataSource = sharedWindowsArray;
	else if (table == tableConnectedClients) dataSource = connectedClientsArray;
			 
	count = [dataSource count];
    if (count > 0)
    {
        theObject = [dataSource objectAtIndex: rowIndex];
        theValue = [theObject valueForKey: identifier];
    }

    return theValue;
}

-(void) resetClientRequestedArea
{
	rfbClientIteratorPtr iterator;
	rfbClientPtr cl = NULL;
	BoxRec box;
	
	box.x1 = 0;
	box.y1 = 0;
	box.x2 = rfbScreen.width;
	box.y2 = rfbScreen.height;
	
	iterator = rfbGetClientIterator();
	while ((cl = rfbClientIteratorNext(iterator)) != NULL) 
	{
		pthread_mutex_lock(&cl->updateMutex);
		REGION_RESET(&hackScreen, &cl->requestedRegion, &box);
		pthread_mutex_unlock(&cl->updateMutex);
	}
	rfbReleaseClientIterator(iterator);
}

//*****************************************************************************
// The following functions may introduce race conditions from multiple client threads
// Called by Main.c and rfbserver.c possibly by multiple threads (1 per client)


// called by rfbserver when client leaves
// withing clientInput thread - no autorelease pool
-(void) removeClient: (rfbClientPtr)cl
{
	NSEnumerator *enumerator;
	SharedAppClient *client;
	NSAutoreleasePool *tempPool;
		
	tempPool = [[NSAutoreleasePool alloc] init];
		
	enumerator = [[NSArray arrayWithArray:connectedClientsArray] objectEnumerator];
	while (client = [enumerator nextObject]) 
	{
		if ([client clientStruct] == cl)
		{
			[arrayLock lock];
			[connectedClientsArray removeObject:client];
			[arrayLock unlock];
		}
	}
	[tableConnectedClients reloadData];
	
	[tempPool release];
	return;
	
}

-(VNCWinInfo*) getWindowWithId:(int)wid
{
	VNCWinInfo *win;		
	NSEnumerator *enumerator;
	
	enumerator = [[NSArray arrayWithArray:sharedWindowsArray] objectEnumerator];
	while (win = [enumerator nextObject]) 
	{
		if ([win windowId] == wid)
		{
			return win;
		}
	}
	
	return NULL;
}

-(void) getSharedRegions:(RegionRec*)sharedRegionPtr
{
	VNCWinInfo *win;
	NSEnumerator *enumerator;
	RegionRec winRegion;
	
	REGION_INIT(&hackScreen, &winRegion, NullBox, 0);
	REGION_EMPTY(&hackScreen, sharedRegionPtr);
	
	[arrayLock lock];
	enumerator = [sharedWindowsArray objectEnumerator];
	while (win = [enumerator nextObject]) 
	{
		[win getVisibleRegion:&winRegion];
		REGION_UNION(&hackScreen, sharedRegionPtr, sharedRegionPtr, &winRegion);
	}
	[arrayLock unlock];	
	
	REGION_UNINIT(&hackScreen,&winRegion);
}


-(BOOL) areWindowsToClose
{
	return ([windowsToBeClosedArray count] > 0);
}

// called within clientOutput thread - no autorelease pool
-(void) checkForClosedWindows
{
	VNCWinInfo* win;
	BOOL bRefreshTableView = FALSE;		
	NSEnumerator *enumerator;
	//NSAutoreleasePool *tempPool;
	
	if ([windowsToBeClosedArray count] == 0) return;

	//tempPool = [[NSAutoreleasePool alloc] init];
	
	enumerator = [[NSArray arrayWithArray:windowsToBeClosedArray] objectEnumerator];
	while (win = [enumerator nextObject])
	{
		rfbSharedAppUpdateMsg *updateMsg;
		rfbClientIteratorPtr iterator;
		rfbClientPtr cl = NULL;
		
		//rfbLog("rfbSendWindowClose %x", [win windowId]);
		
		iterator = rfbGetClientIterator();
		while ((cl = rfbClientIteratorNext(iterator)) != NULL) 
		{
			pthread_mutex_lock(&cl->updateMutex);
			
			if (cl->ublen + sz_rfbSharedAppUpdateMsg > UPDATE_BUF_SIZE) 
			{
				rfbSendUpdateBuf(cl);
			}
			updateMsg = (rfbSharedAppUpdateMsg *)(cl->updateBuf + cl->ublen);
			cl->ublen += sz_rfbSharedAppUpdateMsg;
			memset(updateMsg, 0, sz_rfbSharedAppUpdateMsg);
			updateMsg->type = rfbSharedAppUpdate;
			updateMsg->win_id = Swap32IfLE([win windowId]);
			
			SHAREDAPP_TRACE("trace(%d): Message %d", NMSG++, rfbSharedAppUpdate);
			rfbSendUpdateBuf(cl);
			
			pthread_mutex_unlock(&cl->updateMutex);
		}
		rfbReleaseClientIterator(iterator);

		[arrayLock lock];
		if ([sharedWindowsArray containsObject:win])
		{
			[sharedWindowsArray removeObject:win];
			bRefreshTableView = TRUE;
		}
		[windowsToBeClosedArray removeObject:win];
		[arrayLock unlock];
	}

	
	if (bRefreshTableView) 	[tableSharedWindows reloadData];
	
	//[tempPool release];
		
	return;
}


-(BOOL)
rfbSendUpdates: (rfbClientPtr)cl screenRegion:(RegionRec)screenUpdateRegion
{
	int i;
    int nUpdateRegionRects;
	NSEnumerator *enumerator;
	VNCWinInfo *win;
	BoxRec box;
	RegionRec winRegion, updateRegion;
	rfbSharedAppUpdateMsg *updateMsg;
	BOOL clearRequestedRegion = FALSE;
	BOOL bResetCursorLocation = FALSE;
	BOOL bResetCursorType = FALSE;
	BOOL bUsePixelImage = TRUE;

    // check if SharedAppEncoding supported for this client 
    if (!cl->supportsSharedAppEncoding) return FALSE;
	
	REGION_INIT(&hackScreen, &updateRegion, NullBox, 0);
	REGION_INIT(&hackScreen, &winRegion, NullBox, 0);
		
	// get the enumerator from an immutable copy since docs say don't modifify array during enumeration.
	enumerator = [[NSArray arrayWithArray:sharedWindowsArray] objectEnumerator];
	while (win = [enumerator nextObject])
	{
		BOOL sendRichCursorEncoding = FALSE;
		BOOL sendCursorPositionEncoding = FALSE;
		BOOL bCursorOnly = FALSE;
			
		if (![win validated])
		{
			[arrayLock lock];
			rfbLog("remove window");
			[windowsToBeClosedArray addObject:win];
			[arrayLock unlock];
			continue;
		}
		
		REGION_EMPTY(&hackScreen, &updateRegion);
		
		[win getVisibleRegion:&winRegion];
		
		if ([win doFullUpdate])
		{
			box.x1 = [win originX];
			box.y1 = [win originY];
			box.x2 = [win originX] + [win width];
			box.y2 = [win originY] + [win height];
			
			REGION_RESET(&hackScreen, &updateRegion, &box);
			[win setDoFullUpdate:FALSE];
		} else {
			REGION_INTERSECT(&hackScreen, &updateRegion, &screenUpdateRegion, &winRegion);
		}

		//rfbLog("winRegion %d %d %d %d", box.x1, box.y1, box.x2, box.y2);
		
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
			
		if ( nUpdateRegionRects == 0)
		{
			// no pixels to send, but we may need to send cursor information
			bCursorOnly = TRUE;
			//rfbLog("Cursor Only");
		}

#if !DEBUG_SEND_PIXELS_ONLY		
		if (nUpdateRegionRects != 0xFFFF) 
		{	
			if (rfbShouldSendNewCursor(cl)) 
			{
				CGPoint p = currentCursorLoc();
				BoxRec resbox;
				if (POINT_IN_REGION(&hackScreen, &winRegion, p.x, p.y, &resbox))
				{	
					sendRichCursorEncoding = TRUE;
					nUpdateRegionRects++;
				}
				bResetCursorType = TRUE;
			}
			if (rfbShouldSendNewPosition(cl)) 
			{
				CGPoint p = currentCursorLoc();
				BoxRec resbox;
				if (POINT_IN_REGION(&hackScreen, &winRegion, p.x, p.y, &resbox))
				{	
					sendCursorPositionEncoding = TRUE;
					nUpdateRegionRects++;
				}
				bResetCursorLocation = TRUE;
			}
			
			if (cl->needNewScreenSize) {
				nUpdateRegionRects++;
			}
		}
#endif
		
		if (nUpdateRegionRects == 0)
        {
            //rfbLog("SHAREDAPP -- no rects to send\n");
            continue;
        }

		if (bUsePixelImage)
		{
			if (!bCursorOnly)
			{
				int prevX = [win originX];
				int prevY = [win originY];
				int prevW = [win width];
				int prevH = [win height];
				
				// Get access to the window pixels
				
				cl->scalingFrameBuffer = [win getPixelAccess];
				cl->scalingPaddedWidthInBytes = [win bytesPerRow]; // * rfbScreen.bitsPerPixel/8;
				
				if (cl->scalingFrameBuffer == NULL || 
					prevX != [win originX] || prevY != [win originY] || 
					prevW != [win width] || prevH != [win height])
				{
					rfbLog("Size or Location changed");
					//[self refreshWindow:win]; // this will cause deadlock on updateMutex
					continue;
				}
				
				// update and translate regions pointers appropriately
				REGION_TRANSLATE(&hackScreen, &updateRegion, -[win originX], -[win originY]);
			}
			box.x1 = 0;
			box.y1 = 0;
		}
		
		// Send stuff
		// Clear client requested region - since we are sending stuff they will send another request
		clearRequestedRegion = TRUE;
		
		if (cl->ublen + sz_rfbSharedAppUpdateMsg > UPDATE_BUF_SIZE) 
        {
			if (!rfbSendUpdateBuf(cl)) {
				REGION_UNINIT(&hackScreen,&winRegion);
				REGION_UNINIT(&hackScreen,&updateRegion);
				return FALSE;
			}
        }
        updateMsg = (rfbSharedAppUpdateMsg *)(cl->updateBuf + cl->ublen);
        memset(updateMsg, 0xab, sz_rfbSharedAppUpdateMsg);
        updateMsg->type = rfbSharedAppUpdate;
        updateMsg->win_id = Swap32IfLE([win windowId]);
        updateMsg->parent_id = Swap32IfLE(0);
		updateMsg->cursorOffsetX = Swap16IfLE([win originX]);
		updateMsg->cursorOffsetY = Swap16IfLE([win originY]);
		updateMsg->win_rect.x = Swap16IfLE(box.x1);
        updateMsg->win_rect.y = Swap16IfLE(box.y1);
        updateMsg->win_rect.w = Swap16IfLE([win width]);
        updateMsg->win_rect.h = Swap16IfLE([win height]);
        updateMsg->nRects = Swap16IfLE(nUpdateRegionRects);
        cl->ublen += sz_rfbSharedAppUpdateMsg;
		
		SHAREDAPP_TRACE("trace(%d): Message %d	nRects 0x%x", NMSG++, rfbSharedAppUpdate, nUpdateRegionRects);
		
#if !DEBUG_SEND_PIXELS_ONLY		
		if (cl->needNewScreenSize) {
			if (rfbSendScreenUpdateEncoding(cl)) {
				cl->needNewScreenSize = FALSE;
			} else {
				rfbLog("Error Sending Cursor\n");
				REGION_UNINIT(&hackScreen,&winRegion);
				REGION_UNINIT(&hackScreen,&updateRegion);
				return FALSE;
			}            
		}
		
		// Sometimes send the mouse cursor update
		if (sendRichCursorEncoding) {
			if (!rfbSendRichCursorUpdate(cl)) {
				rfbLog("Error Sending Cursor\n");
				REGION_UNINIT(&hackScreen,&winRegion);
				REGION_UNINIT(&hackScreen,&updateRegion);
				return FALSE;
			}
		}
		if (sendCursorPositionEncoding) {
			if (!rfbSendCursorPos(cl)) {
				rfbLog("Error Sending Cursor\n");
				REGION_UNINIT(&hackScreen,&winRegion);
				REGION_UNINIT(&hackScreen,&updateRegion);
				return FALSE;
			}
			
		}
#endif	
		
		for (i = 0; i < REGION_NUM_RECTS(&updateRegion); i++) {
			int x = REGION_RECTS(&updateRegion)[i].x1;
			int y = REGION_RECTS(&updateRegion)[i].y1;
			int w = REGION_RECTS(&updateRegion)[i].x2 - x;
			int h = REGION_RECTS(&updateRegion)[i].y2 - y;
			
			//rfbLog("Send Rect %d", i);
			
			if (cl->scalingFactor != 1)
				CopyScalingRect( cl, &x, &y, &w, &h, TRUE);
			
			cl->rfbRawBytesEquivalent += (sz_rfbFramebufferUpdateRectHeader
										  + w * (cl->format.bitsPerPixel / 8) * h);
			
			switch (cl->preferredEncoding) {
				case rfbEncodingRaw:
					if (!rfbSendRectEncodingRaw(cl, x, y, w, h)) {
						REGION_UNINIT(&hackScreen,&winRegion);
						REGION_UNINIT(&hackScreen,&updateRegion);
						return FALSE;
					}
					break;
				case rfbEncodingRRE:
					if (!rfbSendRectEncodingRRE(cl, x, y, w, h)) {
						REGION_UNINIT(&hackScreen,&winRegion);
						REGION_UNINIT(&hackScreen,&updateRegion);
						return FALSE;
					}
					break;
				case rfbEncodingCoRRE:
					if (!rfbSendRectEncodingCoRRE(cl, x, y, w, h)) {
						REGION_UNINIT(&hackScreen,&winRegion);
						REGION_UNINIT(&hackScreen,&updateRegion);
						return FALSE;
					}
					break;
				case rfbEncodingHextile:
					if (!rfbSendRectEncodingHextile(cl, x, y, w, h)) {
						REGION_UNINIT(&hackScreen,&winRegion);
						REGION_UNINIT(&hackScreen,&updateRegion);
						return FALSE;
					}
					break;
				case rfbEncodingZlib:
					if (!rfbSendRectEncodingZlib(cl, x, y, w, h)) {
						REGION_UNINIT(&hackScreen,&winRegion);
						REGION_UNINIT(&hackScreen,&updateRegion);
						return FALSE;
					}
					break;
				case rfbEncodingTight:
					if (!rfbSendRectEncodingTight(cl, x, y, w, h)) {
						REGION_UNINIT(&hackScreen,&winRegion);
						REGION_UNINIT(&hackScreen,&updateRegion);
						return FALSE;
					}
					break;
				case rfbEncodingZlibHex:
					if (!rfbSendRectEncodingZlibHex(cl, x, y, w, h)) {
						REGION_UNINIT(&hackScreen,&winRegion);
						REGION_UNINIT(&hackScreen,&updateRegion);
						return FALSE;
					}
					break;
				case rfbEncodingZRLE:
					if (!rfbSendRectEncodingZRLE(cl, x, y, w, h)) {
						REGION_UNINIT(&hackScreen,&winRegion);
						REGION_UNINIT(&hackScreen,&updateRegion);
						return FALSE;
					}
					break;
			}
		}
		
        if (nUpdateRegionRects == 0xFFFF && !rfbSendLastRectMarker(cl)) {
			REGION_UNINIT(&hackScreen,&winRegion);
			REGION_UNINIT(&hackScreen,&updateRegion);
			return FALSE;
		} 

		if (!rfbSendUpdateBuf(cl))
			return FALSE;
		
		cl->rfbFramebufferUpdateMessagesSent++;
	
	}
	
	if (clearRequestedRegion)
	{
		REGION_UNINIT(&hackScreen, &cl->requestedRegion);
		REGION_INIT(&hackScreen, &cl->requestedRegion,NullBox,0);
	}
	
	if (bResetCursorLocation) cl->clientCursorLocation = currentCursorLoc();
	
	if (bResetCursorType) cl->currentCursorSeed = CGSCurrentCursorSeed();
	
	REGION_UNINIT(&hackScreen,&winRegion);
    REGION_UNINIT(pScreen,&updateRegion);
	

	
    return TRUE;
	
}



@end
