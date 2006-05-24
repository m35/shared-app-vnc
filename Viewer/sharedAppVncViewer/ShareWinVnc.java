//
//  Copyright (C) 2005 Grant Wallace, Princeton University.  All Rights Reserved.
//
//  This is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation; either version 2 of the License, or
//  (at your option) any later version.
//
//  This software is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this software; if not, write to the Free Software
//  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307,
//  USA.
//
//

import java.io.*;
import java.net.*;
import java.util.*;
import java.awt.*;

class ShareWinVnc
{
  //
  // Connect to the RFB server and authenticate the user.
  //

  private Vector shappServers;
  private String host;
  private int port;
  private int initialXOffset, initialYOffset;
  private Rectangle screenRect;
  private Options options;
  
  private final static int Xspacing = 512;
  private final static int Yspacing = 384;

  ShareWinVnc(String[] argv, Rectangle _screenRect)
  {
    screenRect = _screenRect;
    shappServers = new Vector(10);
    options = new Options();
    getArgs(argv);
  }

  //
  // main() is called when run as a java program from the command line.
  //

  public static void main(String[] argv)
  {
    Dispatcher dispatcher = null;
    ServerSocket serverSocket = null;
    Socket sock = null;

    Rectangle displayBounds = getDisplayBounds();
    //Rectangle displayBounds = new Rectangle(0, 0, 1024, 768);
    System.out.println("Display Resolution: " + displayBounds.x + "," + displayBounds.y + ":" + displayBounds.width + "x" + displayBounds.height);

    ShareWinVnc swVnc = new ShareWinVnc(argv, displayBounds);

    if (swVnc.host != null)
    {
      try {
      dispatcher = swVnc.connect();
      } catch(Exception e) {
        System.out.println("Connect " + swVnc.host + ":" + swVnc.port + " failed");
        System.out.println(e.getMessage());
        System.out.println(e.toString());
      }

      swVnc.shappServers.addElement(dispatcher);
    }
    
    // listen for reverse connections
    int tries = 0;
    do {
      try {
        serverSocket = new ServerSocket(5500 + tries);
        break;
      } catch (IOException e) {
        if (tries == 5)
          fatalError("Could not listen on port: 5500-5505", e);
      }
    } while(tries++ < 5);

    System.out.println("listening on port " + (5500+tries));

    
    while (true)
    {
      try {
        sock = serverSocket.accept();
        // change the location slightly for each new connection
        Point p = swVnc.getClientScreenLocation(swVnc.shappServers.size());
        dispatcher = new Dispatcher(sock, swVnc.options, p);
        swVnc.shappServers.addElement(dispatcher);
        swVnc.cleanFinishedThreads();
      } catch (Exception e) {
        System.out.println("ServerSocket accept failed");
        System.out.println(e.getMessage());
        System.out.println(e.toString());
      }
    }
  }

  void cleanFinishedThreads()
  {
    Enumeration e;
    Dispatcher d;
    for (e = shappServers.elements(); e.hasMoreElements(); )
    {
      d = (Dispatcher) e.nextElement();
      if (!d.isAlive())
      {
        shappServers.remove(d);
      }
    }
  }

  Dispatcher connect() throws Exception
  {
    RfbProto rfb;
    Socket sock;

    try {

      sock = new Socket(host, port);
      
    } catch (NoRouteToHostException e) {
      throw new Exception("Network error: no route to server: " + host, e);
    } catch (UnknownHostException e) {
      throw new Exception("Network error: server name unknown: " + host, e);
    } catch (ConnectException e) {
      throw new Exception("Network error: could not connect to server: " +
                 host + ":" + port, e);
    } catch (EOFException e) {
        throw new Exception("Network error: remote side closed connection", e);
    } catch (IOException e) {
      String str = e.getMessage();
      if (str != null && str.length() != 0) {
        throw new Exception("Network Error: " + str, e);
      } else {
        throw new Exception(e.toString(), e);
      }
    }

    Point p = getClientScreenLocation(shappServers.size());
    return new Dispatcher(sock, options, p);

  }


  private void getArgs(String[] argv)
  {
    host = null;
    port = 0;
    initialXOffset = 0;
    initialYOffset = 0;

    for (int i = 0; i < argv.length; i++) {
      if (argv[i].startsWith("-")) {
        if (argv[i].equals("-host")) {
          if (i + 1 > argv.length) usage();
          host = argv[++i];
        } else if (argv[i].equals("-port")) {
          if (i + 1 > argv.length) usage();
          port = Integer.parseInt(argv[++i]);
        } else if (argv[i].equals("-xoff")) {
          if (i + 1 > argv.length) usage();
          initialXOffset = Integer.parseInt(argv[++i]);
        } else if (argv[i].equals("-yoff")) {
          if (i + 1 > argv.length) usage();
          initialYOffset = Integer.parseInt(argv[++i]);
        } else if (argv[i].equals("-normalVNC")) {
          System.out.println("Using Normal VNC");
          options.setUseSharedApp(false);
        } else if (argv[i].equals("-multicursor")) {
          if (i + 1 > argv.length) usage();
          options.setMultiCursor(Integer.parseInt(argv[++i]));
        } else if (argv[i].equals("-enable-blackout")) {
          System.out.println("Enabling blackout regions");
          options.setUseBlackOut(true);
        } else if (argv[i].equals("-trace")) {
          System.out.println("Enabling message trace");
          options.trace = true;
        }

      }
    }
    return;
  }


  private Point getClientScreenLocation(int nClients)
  {
    int nRows, nCols, row, col;
    int dw, dh;
    int xpos, ypos;

    dw = screenRect.width - initialXOffset;
    dh = screenRect.height - initialYOffset;

    nCols = (int) dw/Xspacing;
    nRows = (int) dh/Yspacing;
    row = (nClients / nCols) % nRows; // mod nRows loops the placement around if necessary
    col = nClients % nCols;

    xpos = screenRect.x + initialXOffset + col*Xspacing;
    ypos = screenRect.y + initialYOffset + row*Yspacing;

    System.out.println("Positioning client " + nClients + ": " + xpos + ", " + ypos );
    return new Point(xpos, ypos);
  }


  private void usage()
  {
    System.out.println("SharedWinVnc -host <HOST> -port <port> -xoff <xoffset> -yoff <yoffset> -multicursor <cursornum> -enable-blackout [-normalVNC]");
  }


  static private Rectangle getDisplayBounds()
  {
    Rectangle virtualBounds = new Rectangle();
    GraphicsEnvironment ge = GraphicsEnvironment.getLocalGraphicsEnvironment();
    GraphicsDevice[] gs = ge.getScreenDevices();

    System.out.print("Determining Screen Resolution");
    for (int i = 0; i < gs.length; i++) 
    { 
      GraphicsDevice gd = gs[i];
      GraphicsConfiguration[] gc = gd.getConfigurations();
      System.out.print(".");
      for (int j=0; j < gc.length; j++) 
      {
        System.out.print("+");
        virtualBounds = virtualBounds.union(gc[j].getBounds());
      }
    } 
    System.out.println("");
    return virtualBounds;
  }


  //
  // fatalError() - print out a fatal error message.
  //

  static public void fatalError(String str) {
    System.out.println(str);
    System.exit(1);
  }

  static public void fatalError(String str, Exception e) {
    System.out.println(str);
    e.printStackTrace();
    System.exit(1);
  }

}
