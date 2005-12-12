//
//  DTFullScreenWindow.m
//  Desktop Transporter
//
//  Created by Daniel St¿dle on Wed Mar 31 2004.
//  Copyright (c) 2004  Daniel Stødle, daniels@stud.cs.uit.no. All rights reserved.
//

#import "DTFullScreenWindow.h"
#import "SharedApp.h"

@implementation DTFullScreenWindow

- (void)set_controller:(id)ctrl { controller = ctrl; }

- (void)keyUp:(NSEvent*)evt {
	//[(DTScreenViewController*)sv_controller exit_fullscreen:self];
}


- (void)mouseUp:(NSEvent*)evt {
	NSPoint		pt;
	NSRect		b;
	
	b   = [self frame];
	pt  = [evt locationInWindow];
	pt.y	= b.size.height-pt.y;
	[(SharedApp*)controller finish_select:*(CGPoint*)&pt];
}

- (BOOL)canBecomeKeyWindow { return YES; }

@end
