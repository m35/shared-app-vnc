//
//  main.m
//  SharedAppVnc
//
//  Created by Grant Wallace on 10/18/05.
//  Copyright __MyCompanyName__ 2005. All rights reserved.
//

#import <Cocoa/Cocoa.h>

int main(int argc, char *argv[])
{
	
    [NSApplication sharedApplication];
    [NSBundle loadNibNamed:@"SharedApp" owner:NSApp];
    [NSApp run];
   //return NSApplicationMain(argc,  (const char **) argv);
}
