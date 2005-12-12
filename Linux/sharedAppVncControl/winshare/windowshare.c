/*
 * Copyright (C) 2005 Grant Wallace, Princeton University.  All Rights Reserved.
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
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

#include "windowshare.h"

char *programName;

int
main(int argc, char **argv)
{
  int i;

  programName = argv[0];

  /* Interpret resource specs and process any remaining command-line arguments
     (i.e. the VNC server name).  If the server name isn't specified on the
     command line, getArgsAndResources() will pop up a dialog box and wait
     for one to be entered. */

  GetArgsAndResources(argc, argv);

  /* Initialize our capability lists */

  InitCapabilities();

  /* Make a TCP connection to the given VNC server */

  if (!ConnectToRFBServer(vncServerHost, vncServerPort)) exit(1);

  /* Initialise the VNC connection, including reading the password */

  if (!InitialiseRFBConnection()) exit(1);

  /* send window sharing command */
  /* printf("Sending windowCommand %x windowId %x\n", windowCommand, windowId); */
  SendWindowShare(windowCommand, windowId, reverseConnectHost);
  
  /* Now enter the main loop, processing VNC messages.  GTK events will
     automatically be processed whenever the VNC connection is idle. */
  /*
  while (1) {
    if (!HandleRFBServerMessage())
      break;
  }
  */

  return 0;
}


