/*
 * Source File: VNCWinInfo.m 
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

#import "VNCWinInfo.h"
#include "CGS-Private.h"


@implementation VNCWinInfo

-init
{
	return [self initWithWindowId: -1 connectionId: -1];
}

-(void)dealloc
{
	if (data) free(data);
	if (bitmap_context) CGContextRelease(bitmap_context);
	if (processName) [processName release];
	[super dealloc];
}


-initWithWindowId: (CGSWindow) wid 
	 connectionId: (CGSConnection) cid
{
	[super init];
	
	defaultCon = _CGSDefaultConnection();
	windowId = wid;
	connectionId = cid;
	
	data = 0;
	window_context = 0;
	bitmap_context = 0;
	processName = 0;
	doFullUpdate = TRUE;
	//validate = TRUE;
	lastPixelAccessTime = [NSDate date]; //dateWithTimeIntervalSinceReferenceDate:0];

	CGSConnectionGetPID(cid, &processId, defaultCon);
	processName = [self lookupProcessName]; // @"Unknown";

	// setup up for shared access
	CGSGetSharedWindow(defaultCon, windowId, 1, 0);
	
	return self;
}


-(CGSWindow) windowId {	return windowId; }
-(CGRect) location { return location; }
-(int) originX { return location.origin.x; }
-(int) originY { return location.origin.y; }
-(int) width { return location.size.width; }
-(int) height { return location.size.height; }
-(int) bytesPerRow { return bytesPerRow; }
-(NSString*) processName { return processName; }

-(NSString*) lookupProcessName{
	ProcessSerialNumber psn;
	ProcessInfoRec		p_info;
	const int			maxLen = 127;
	char				name[maxLen+1];
	
	if (GetProcessForPID(processId, &psn) != noErr) {
		printf("Warning: Failed to get process serial num for pid %d\n", processId);
		return 0;
	}
	memset(&p_info, 0, sizeof(ProcessInfoRec));
	memset(name, 0, maxLen+1);
	p_info.processInfoLength	= sizeof(ProcessInfoRec);
	p_info.processName			= name;
	if (GetProcessInformation(&psn, &p_info) != noErr) {
		printf("Warning: Failed to get process info pid %d/psn %d\n", processId, *(int*)&psn);
		return 0;
	}
	return [[NSString alloc] initWithCString:name];
}


-(BOOL) validated
{
	OSStatus status;
	CGRect vloc;
	
	status = CGSGetScreenRectForWindow(defaultCon, windowId, &vloc);
	if (status != noErr)  
	{
		printf("Validation Error\n");
		return FALSE;
	}
	
	if (vloc.size.width != location.size.width || vloc.size.height != location.size.height ||
		vloc.origin.x != location.origin.x || vloc.origin.y != location.origin.y)
	{
		CGColorSpaceRef color_space;
		int depth=32;
		int bitsPerComponent = 8;
		
		NSLog(@"Get New bitmap_context");

		if (data) { free(data); data = 0; }
		if (bitmap_context) { CGContextRelease(bitmap_context); bitmap_context = 0; }
		
		bytesPerRow =  vloc.size.width * (depth / 8);
	
		data = malloc(bytesPerRow * vloc.size.height);
		color_space	= CGColorSpaceCreateDeviceRGB();
		bitmap_context = CGBitmapContextCreate(data, vloc.size.width, vloc.size.height, bitsPerComponent, bytesPerRow, color_space, kCGImageAlphaNoneSkipFirst);
		CGColorSpaceRelease(color_space);
	}
	location = vloc;
	//rfbLog("win %d location (%.0f %.0f) size (%.0f %.0f)", windowId, location.origin.x, location.origin.y, location.size.width, location.size.height);
	return TRUE;
}


-(BOOL)	doFullUpdate
{
	return doFullUpdate;
}

-(void) setDoFullUpdate:(BOOL)flag
{
	doFullUpdate = flag;
}


-(BOOL) isTopWindow
{
	int workspace, window, retCount;
	OSStatus status;
	
	status =  CGSGetWorkspace(defaultCon, &workspace);
	if (status != noErr) { rfbLog("GetWindowWorkspace Error"); return FALSE; }
	
	status = CGSGetWorkspaceWindowList(defaultCon, workspace, 1, &window, &retCount);
	if (status != noErr) { rfbLog("GetWorkspaceWindowList Error"); return FALSE; }
	
	if (window == windowId) return TRUE;
	else return FALSE;
}

-(void) getVisibleRegion:(RegionRec*)visibleRegionPtr
{
	// loop through all windows - union the screen locations of windows that are above this window
	// subtract union from this window's screen space.
	#define MAX_WINDOW_COUNT 512
	int workspace, windowCount, i;
	int windowList[MAX_WINDOW_COUNT];
	RegionRec unionRegion, winRegion;
	BoxRec box;
	CGRect rect;
	OSStatus status;
	
	REGION_EMPTY(&hackScreen, visibleRegionPtr);
	
	status =  CGSGetWorkspace(defaultCon, &workspace);
	if (status != noErr) { rfbLog("GetWindowWorkspace Error"); return; }
	
	status = CGSGetWorkspaceWindowCount(defaultCon, workspace, &windowCount);
	if (status != noErr) { rfbLog("GetWorkspaceCount Error"); return; }
	
	windowCount = (MAX_WINDOW_COUNT > windowCount) ? windowCount : MAX_WINDOW_COUNT;
	
	status = CGSGetWorkspaceWindowList(defaultCon, workspace, windowCount, windowList, &windowCount);
	if (status != noErr) { rfbLog("GetWorkspaceWindowList workspace %d windowCount %d Error, %d", workspace, windowCount, status); return; }
	
	REGION_INIT(&hackScreen, &unionRegion, NullBox, 0);
	REGION_INIT(&hackScreen, &winRegion, NullBox, 0);	
	
	for (i=0; i<windowCount; i++)
	{

		// window level is like the first bit (dock, menu, modal, normal etc.)
		//status = CGSGetWindowLevel(defaultCon, windowList[i], &windowLevel);
   
		status = CGSGetScreenRectForWindow(defaultCon, windowList[i], &rect);
		if (status != noErr) { rfbLog("GetScreenRectForWindow Error"); return; }
		//rfbLog("WINDOW %d Level %d x %.0f y %.0f w %.0f h %.0f", windowList[i], windowLevel,
		//	   rect.origin.x, rect.origin.y, rect.size.width, rect.size.height);
		
		box.x1 = rect.origin.x;
		box.y1 = rect.origin.y;
		box.x2 = box.x1 + rect.size.width;
		box.y2 = box.y1 + rect.size.height;
		
		REGION_RESET(&hackScreen, &winRegion, &box);
		if (windowList[i] != windowId)
		{
			// union all higher level windows
			REGION_UNION(&hackScreen, &unionRegion, &unionRegion, &winRegion);
		} else {
			// subtract higher level windows region from window region
			REGION_SUBTRACT(&hackScreen, visibleRegionPtr, &winRegion, &unionRegion);	
			break;
		}
	}
	REGION_UNINIT(&hackScreen, &unionRegion);
	REGION_UNINIT(&hackScreen, &winRegion);
	return;
}


-(char*) getPixelAccess
{
	CGImageRef image = 0;
	uint32_t *pixel;
	OSStatus status;
	CGRect scr;
	CGRect vloc;
	char *retval = NULL;
	NSTimeInterval secondsElapsed;
	double mintime = 0.05;
	
	if (![self validated]) return NULL;
	
	//[accessLock lock]
		//secondsElapsed = [[NSDate date] timeIntervalSinceDate:lastPixelAccessTime];
		//if (secondsElapsed > mintime) lastPixelAccessTime = [NSDate date];
	//[accessLock unlock]
	//if (secondsElapsed < mintime) return data;
	
	
	scr = location;
	scr.origin.x = scr.origin.y = 0;
	
	window_context = CGWindowContextCreate(defaultCon, windowId, scr);
	if (window_context)
	{
		pixel = CGContextGetPixelAccess(window_context);
		if (pixel) 
		{
			status = CGPixelAccessLock(pixel);
			if (status != 0) 
			{
				// double check that size of position haven't changed
				status = CGSGetScreenRectForWindow(defaultCon, windowId, &vloc);
				if (status == noErr) 
				{
					if (vloc.size.width == location.size.width && vloc.size.height == location.size.height &&
						vloc.origin.x == location.origin.x && vloc.origin.y == location.origin.y)
					{
						image = CGPixelAccessCreateImageFromRect(pixel, scr);
						if (image) 
						{
							CGContextDrawImage(bitmap_context, scr, image);
							CGImageRelease(image);
							retval = data;
						} else {
							NSLog(@"getPixelAccess: createImage failed");
						}
					} else {
						NSLog(@"getPixelAccess: size/location changed");
					}
				} else {
					NSLog(@"getPixelAccess: screenRect failed");
				}
				status = CGPixelAccessUnlock(pixel);
			} else {
				NSLog(@"getPixelAccess: pixel lock failed");
			}
		} else {
			NSLog(@"getPixelAccess: pixel access failed");
		}
		CGContextRelease(window_context);
		window_context = 0;
	} else {
		NSLog(@"getPixelAccess: windowContext failed");
	}
	
	return retval;
}



@end
