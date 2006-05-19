//  Copyright (C) 2005 Grant Wallace, Princeton University.  All Rights Reserved.
//  Copyright (C) 2001,2002 HorizonLive.com, Inc.  All Rights Reserved.
//  Copyright (C) 2001,2002 Constantin Kaplinsky.  All Rights Reserved.
//  Copyright (C) 2000 Tridia Corporation.  All Rights Reserved.
//  Copyright (C) 1999 AT&T Laboratories Cambridge.  All Rights Reserved.
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

import java.io.*;
import java.net.*;
import java.awt.*;
import java.util.*;
import java.awt.image.*;
import java.util.zip.*;


class Dispatcher extends Thread {

  RfbProto rfb;
  Hashtable windowsHash;
  VncWindow win;

  Color[] colors;
  ColorModel cm8, cm24;

  boolean cursorPosReceived;

  // Zlib encoder's data.
  byte[] zlibBuf;
  int zlibBufLen = 0;
  Inflater zlibInflater;

  // Tight encoder's data.
  final static int tightZlibBufferSize = 512;
  Inflater[] tightInflaters;

  String remoteHost;
  Point screenLocation;

  static int DesktopWindowId = 0;

  int msgCount  = 0;


  Dispatcher(Socket s, Options o, Point location) throws Exception
  {
    screenLocation = location;
    windowsHash = new Hashtable();

    cm8 = new DirectColorModel(8, 7, (7 << 3), (3 << 6));
    cm24 = new DirectColorModel(24, 0xFF0000, 0x00FF00, 0x0000FF);

    colors = new Color[256];
    for (int i = 0; i < 256; i++)
      colors[i] = new Color(cm8.getRGB(i));

    tightInflaters = new Inflater[4];

    remoteHost = s.getInetAddress().getHostName();

    rfb = new RfbProto(s, o);

    start();
  }


  public void init() throws Exception
  {
    rfb.readVersionMsg();

    System.out.println("RFB server supports protocol version " +
                       rfb.serverMajor + "." + rfb.serverMinor);

    rfb.writeVersionMsg();

    int authScheme = rfb.readAuthScheme();

    switch (authScheme) {

    case RfbProto.NoAuth:
      System.out.println("No authentication needed");
      break;

    case RfbProto.VncAuth:
      System.out.println("Vnc authentication");
      byte[] challenge = new byte[16];
      String password = getPassword();
      rfb.is.readFully(challenge);


      if (password.length() > 8)
        password = password.substring(0, 8); // Truncate to 8 chars

      // vncEncryptBytes in the UNIX libvncauth truncates password
      // after the first zero byte. We do to.
      int firstZero = password.indexOf(0);
      if (firstZero != -1)
        password = password.substring(0, firstZero);

      byte[] key = {0, 0, 0, 0, 0, 0, 0, 0};
      System.arraycopy(password.getBytes(), 0, key, 0, password.length());

      DesCipher des = new DesCipher(key);

      des.encrypt(challenge, 0, challenge, 0);
      des.encrypt(challenge, 8, challenge, 8);

      rfb.os.write(challenge);

      password = null;

      int authResult = rfb.is.readInt();

      switch (authResult) {
      case RfbProto.VncAuthOK:
        System.out.println("VNC authentication succeeded");
        break;
      case RfbProto.VncAuthFailed:
        throw new Exception("VNC authentication failed");
      case RfbProto.VncAuthTooMany:
        throw new Exception("VNC authentication failed - too many tries");
      default:
        throw new Exception("Unknown VNC authentication result " + authResult);
      }
      break;

    default:
      throw new Exception("Unknown VNC authentication scheme " + authScheme);
    }

    // Do the protocol initialisation.
    rfb.writeClientInit();
    rfb.readServerInit();

    rfb.setEncodings();
    rfb.setPixelFormat();
    rfb.setMultiCursor();

    System.out.println("Desktop name is " + rfb.desktopName);
    System.out.println("Desktop size is " + rfb.framebufferWidth + " x " +
                       rfb.framebufferHeight);

    return;
  }


  private String getPassword() throws Exception
  {
    String password;
    PasswdDialog passwdDialog = new PasswdDialog();
    passwdDialog.setLocation(screenLocation);
    passwdDialog.setVisible(true);        // wait user's input
    password = passwdDialog.getPasswd();
    if (password == null)
    {
      throw new Exception("Connection canceled by user");
    } 
    return password;
  }


  public void run() 
  {
    try {
      init();

      rfb.writeFramebufferUpdateRequest(0, 0, rfb.framebufferWidth,
                                        rfb.framebufferHeight, false);
  
      //
      // main dispatch loop
      //
  
      while (true) {
  
        // Read message type from the server.
        int msgType = rfb.readServerMessageType();
        boolean fullUpdateNeeded = false;
  
        cursorPosReceived = false;

        System.out.println("TRACE [" + msgCount + "]: Message Type " + msgType);
        msgCount++;
  
        // Process the message depending on its type.
        switch (msgType) {
        case RfbProto.FramebufferUpdate:
          handleFramebufferUpdate();
          break;
  
        case RfbProto.SharedAppUpdate:
          handleSharedAppUpdate();
          if (rfb.is.available() > 0) continue;
          break;
  
        case RfbProto.SetColourMapEntries:
          throw new Exception("Can't handle SetColourMapEntries message");
  
        case RfbProto.Bell:
          Toolkit.getDefaultToolkit().beep();
          break;
  
        case RfbProto.ServerCutText:
          String s = rfb.readServerCutText();
          //viewer.clipboard.setCutText(s);
          break;

        case RfbProto.SharedAppDisconnect:
          System.out.println("server closed connection");
          rfb.close();
          closeAllWindows();
          return;
  
        default:
          throw new Exception("Unknown RFB message type " + msgType);
        }
  
      // Defer framebuffer update request if necessary. But wake up
      // immediately on keyboard or mouse event. Also, don't sleep
      // if there is some data to receive, or if the last update
      // included a PointerPos message.
      if (rfb.options.getDeferUpdateRequests() > 0 &&
          rfb.is.available() == 0 && !cursorPosReceived) {
        synchronized(rfb) {
          try {
            rfb.wait(rfb.options.getDeferUpdateRequests());
          } catch (InterruptedException e) {
          }
        }
      }
  
      // Before requesting framebuffer update, check if the pixel
      // format should be changed. If it should, request full update
      // instead of an incremental one.
      if (rfb.options.getEightBitColors() != (rfb.bytesPixel == 1)) {
         rfb.setPixelFormat();
         fullUpdateNeeded = true;
      }
  
//System.out.println("REQUEST Framebuffer Update " + fullUpdateNeeded);
      rfb.writeFramebufferUpdateRequest(0, 0, rfb.framebufferWidth,
                                        rfb.framebufferHeight,
                                        !fullUpdateNeeded);
      
      //rfb.writeFramebufferUpdateRequest(rfb.updateRect.x, rfb.updateRect.y,
      //                                  rfb.updateRect.width, rfb.updateRect.height,
      //                                  !fullUpdateNeeded);
  
        
      }
  
    } catch(Exception e) {
      e.printStackTrace();
      String str = e.getMessage();
      if (str != null && str.length() != 0) {
        System.out.println(e.getMessage());
      } else {
        System.out.println(e.toString());
      }
    }

    closeAllWindows();



  }


  //
  // disconnect() - close connection to server.
  //

  synchronized public void disconnect() {
    System.out.println("Disconnect");
    if (rfb != null && !rfb.closed()) rfb.close();
    System.exit(0);
  }


  public void handleFramebufferUpdate() throws Exception
  {
    rfb.readFramebufferUpdate();
    Integer hashint = new Integer(DesktopWindowId);
    
    // close all sharedAppWindows
    closeAllWindowsExcept(DesktopWindowId);

    win = (VncWindow) windowsHash.get(hashint);
    if (win == null)
    {
      win = new VncWindow(this, screenLocation);
      win.setSize(rfb.framebufferWidth, rfb.framebufferHeight);
      win.setTitle("Desktop");
      win.updateWindowSize(rfb.framebufferWidth, rfb.framebufferHeight,
        rfb.bytesPixel);
      win.toFront();
      windowsHash.put(hashint, win);
      System.out.println("created new desktop window");
    } else if (win.rect.width != rfb.framebufferWidth ||
               win.rect.height != rfb.framebufferHeight ||
               win.bytesPixel != rfb.bytesPixel) 
    {
      win.updateWindowSize(rfb.framebufferWidth, rfb.framebufferHeight,
        rfb.bytesPixel);
    }
    rfb.updateRect.x = win.rect.x;
    rfb.updateRect.y = win.rect.y;
    rfb.updateRect.width = win.rect.width;
    rfb.updateRect.height = win.rect.height;

    System.out.println("    nRects 0x" + Integer.toHexString(rfb.updateNRects)); // TRACE

    for (int i = 0; i < rfb.updateNRects; i++) {
      rfb.readFramebufferUpdateRectHdr();
      int rx = rfb.updateRectX, ry = rfb.updateRectY;
      int rw = rfb.updateRectW, rh = rfb.updateRectH;

      if (rfb.updateRectEncoding == rfb.EncodingLastRect)
        break;

      if (rfb.updateRectEncoding == rfb.EncodingNewFBSize) {
        rfb.setFramebufferSize(rw, rh);
        win.updateWindowSize(rw, rh, rfb.bytesPixel);
        break;
      }

      if (rfb.updateRectEncoding == rfb.EncodingXCursor ||
          rfb.updateRectEncoding == rfb.EncodingRichCursor) {
        handleCursorShapeUpdate(rfb.updateRectEncoding, rx, ry, rw, rh, win);
        continue;
      }

      if (rfb.updateRectEncoding == rfb.EncodingPointerPos) {
        softCursorMove(rx, ry, win);
        cursorPosReceived = true;
        continue;
      }

      switch (rfb.updateRectEncoding) {
      case RfbProto.EncodingRaw:
        handleRawRect(rx, ry, rw, rh);
        break;
      case RfbProto.EncodingCopyRect:
        handleCopyRect(rx, ry, rw, rh);
        break;
      case RfbProto.EncodingRRE:
        handleRRERect(rx, ry, rw, rh);
        break;
      case RfbProto.EncodingCoRRE:
        handleCoRRERect(rx, ry, rw, rh);
        break;
      case RfbProto.EncodingHextile:
        handleHextileRect(rx, ry, rw, rh);
        break;
      case RfbProto.EncodingZlib:
        handleZlibRect(rx, ry, rw, rh);
        break;
      case RfbProto.EncodingTight:
        handleTightRect(rx, ry, rw, rh);
        break;
      default:
        throw new Exception("Unknown RFB rectangle encoding " +
                            rfb.updateRectEncoding);
      }
    }
  }


  void handleSharedAppUpdate() throws Exception
  {
    Integer hashint;

    // If there is a VNC Desktop window open, close it.
    closeSharedWindow(DesktopWindowId);

    rfb.readSharedAppUpdate();

    if (rfb.updateRect.width==0 && rfb.updateRect.height==0 &&
        rfb.updateRect.x==0 && rfb.updateRect.y==0 )
    {
      // window closed
      System.out.println("Closing shared window 0x" + Integer.toHexString(rfb.windowId));
      closeSharedWindow(rfb.windowId);
      return;
    }

    hashint = new Integer(rfb.windowId);
    win = (VncWindow) windowsHash.get(hashint);
    if (win == null)
    {
      Point location = screenLocation;

      // Check if this is a dialog window (has a parent)
      if (rfb.parentId != 0)
      {
        VncWindow parent = findWindow(rfb.parentId);
        if (parent!=null)
        {
          location = parent.getLocationOnScreen();
          location.translate(40, 40);
        }
      }

      System.out.println("New Window 0x" + Integer.toHexString(rfb.windowId) + " " + rfb.updateRect);
      win = new VncWindow(this, location);
      win.setWindowId(rfb.windowId);
      win.setSize(rfb.updateRect.width, rfb.updateRect.height);
      win.setTitle("SharedApp");
      win.updateWindowSize(rfb.updateRect.width, rfb.updateRect.height, rfb.bytesPixel);
      win.toFront();
      windowsHash.put(hashint, win);

//Dimension wsize = win.getSize();
//System.out.println("winsize:" + wsize.width + ":" + wsize.height + "setsize " + rfb.updateRect.width + ":" + rfb.updateRect.height);
    } else if (win.rect.width != rfb.updateRect.width ||
               win.rect.height != rfb.updateRect.height ||
               win.bytesPixel != rfb.bytesPixel) 
    {
      win.updateWindowSize(rfb.updateRect.width, rfb.updateRect.height, rfb.bytesPixel);
    }

    win.setVncOrigin(rfb.updateRect.x, rfb.updateRect.y);
    win.setCursorOffset(rfb.cursorOffset.x, rfb.cursorOffset.y);

    //System.out.println("Update Win " + win.windowId + " rect: " + win.rect.toString());
    System.out.println("    nRects 0x" + Integer.toHexString(rfb.updateNRects)); // TRACE

    for (int j = 0; j < rfb.updateNRects; j++) {
      rfb.readFramebufferUpdateRectHdr();
      int rx = rfb.updateRectX;
      int ry = rfb.updateRectY;
      int rw = rfb.updateRectW;
      int rh = rfb.updateRectH;

      System.out.println("TRACE [" + msgCount + "]: Rect[" + j + "] Encoding " + Integer.toHexString(rfb.updateRectEncoding));
      msgCount++;

      if (rfb.updateRectEncoding == rfb.EncodingLastRect)
      {
        //System.out.println("Last Rectangle");
        break;
      }

      if (rfb.updateRectEncoding == rfb.EncodingNewFBSize) {
        rfb.setFramebufferSize(rw, rh);
        break;
      }

      if (rfb.updateRectEncoding == rfb.EncodingXCursor ||
          rfb.updateRectEncoding == rfb.EncodingRichCursor) {
        //System.out.println("WIN:Cursor Shape Update Received");
        handleCursorShapeUpdate(rfb.updateRectEncoding, rx, ry, rw, rh, win);
        continue;
      }

      if (rfb.updateRectEncoding == rfb.EncodingPointerPos) {
        //System.out.println("WIN:Cursor Pos Update Received");
        softCursorMove(rx, ry, win);
        cursorPosReceived = true;
        continue;
      }

      // translate rectangle pos respective to (0,0) imagebuffer
      rx = rx - rfb.updateRect.x;
      ry = ry - rfb.updateRect.y;

      switch (rfb.updateRectEncoding) {
      case RfbProto.EncodingRaw:
        handleRawRect(rx, ry, rw, rh);
        break;
      case RfbProto.EncodingCopyRect:
        handleCopyRectWU(rx, ry, rw, rh, rfb.updateRect.x, rfb.updateRect.y);
        break;
      case RfbProto.EncodingRRE:
        handleRRERect(rx, ry, rw, rh);
        break;
      case RfbProto.EncodingCoRRE:
        handleCoRRERect(rx, ry, rw, rh);
        break;
      case RfbProto.EncodingHextile:
        handleHextileRect(rx, ry, rw, rh);
        break;
      case RfbProto.EncodingZlib:
        handleZlibRect(rx, ry, rw, rh);
        break;
      case RfbProto.EncodingTight:
        handleTightRect(rx, ry, rw, rh);
        break;
      case RfbProto.EncodingBlackOut:
        //System.out.println("BlackOut " + rx + " " + ry + " " + rw+ " "+  rh);
        if (rfb.options.getUseBlackOut() == true) handleBlackOutRect(rx, ry, rw, rh);
        break;
      default:
        throw new Exception("Unknown RFB rectangle encoding " +
                            rfb.updateRectEncoding);
      }
    }
  }


  VncWindow findWindow(int winId)
  {
    Enumeration e;
    VncWindow w;
    for (e = windowsHash.elements(); e.hasMoreElements(); )
    {
      w = (VncWindow) e.nextElement();
      if (w.windowId == winId) 
      {
        return w;
      }
    }
    return null;
  }


  void closeSharedWindow(int closeWindowId)
  {
    Enumeration e;
    VncWindow w;
    for (e = windowsHash.elements(); e.hasMoreElements(); )
    {
      w = (VncWindow) e.nextElement();
      if (w.windowId == closeWindowId) 
      {
        Integer hashint = new Integer(w.windowId);
        w.close();
        windowsHash.remove(hashint);
      }
    }
  }

  void closeAllWindows()
  {
    Enumeration e;
    VncWindow w;
    for (e = windowsHash.elements(); e.hasMoreElements(); )
    {
      w = (VncWindow) e.nextElement();
      Integer hashint = new Integer(w.windowId);
      w.close();
      windowsHash.remove(hashint);
    }
  }

  void closeAllWindowsExcept(int winId)
  {
    Enumeration e;
    VncWindow w;
    for (e = windowsHash.elements(); e.hasMoreElements(); )
    {
      w = (VncWindow) e.nextElement();
      if (w.windowId == winId) continue;
      else
      {
        Integer hashint = new Integer(w.windowId);
        w.close();
        windowsHash.remove(hashint);
      }
    }
  }


  void handleBlackOutRect(int x, int y, int w, int h)
    throws IOException {

    win.memGraphics.setColor(Color.blue);
    win.memGraphics.fillRect(x, y, w, h);

    win.scheduleRepaint(x, y, w, h);
  }


  //
  // Handle a raw rectangle. The second form with paint==false is used
  // by the Hextile decoder for raw-encoded tiles.
  //

  void handleRawRect(int x, int y, int w, int h) throws IOException {
    handleRawRect(x, y, w, h, true);
  }

  void handleRawRect(int x, int y, int w, int h, boolean paint)
    throws IOException {

    if (rfb.bytesPixel == 1) {
      for (int dy = y; dy < y + h; dy++) {
        rfb.is.readFully(win.pixels8, dy * win.rect.width + x, w);
      }
    } else {
      byte[] buf = new byte[w * 4];
      int i, offset;
      for (int dy = y; dy < y + h; dy++) {
        rfb.is.readFully(buf);
        offset = dy * win.rect.width + x;
        for (i = 0; i < w; i++) {
          win.pixels24[offset + i] =
            (buf[i * 4 + 2] & 0xFF) << 16 |
            (buf[i * 4 + 1] & 0xFF) << 8 |
            (buf[i * 4] & 0xFF);
        }
      }
    }

    handleUpdatedPixels(x, y, w, h);
    if (paint)
      win.scheduleRepaint(x, y, w, h);
  }

  //
  // Handle a CopyRect rectangle.
  //

  void handleCopyRect(int x, int y, int w, int h) throws IOException {

    rfb.readCopyRect();
    win.memGraphics.copyArea(rfb.copyRectSrcX, rfb.copyRectSrcY, w, h,
                         x - rfb.copyRectSrcX, y - rfb.copyRectSrcY);

    win.scheduleRepaint(x, y, w, h);
  }

  //
  // Handle a CopyRect rectangle for WindowUpdates
  //

  void handleCopyRectWU(int x, int y, int w, int h, int winx, int winy) throws IOException {
    // to get this to work I'll have to send how much the window has moved since last update
    // note window moves should produce a dx, dx of zero.
    rfb.readCopyRect();
    int dx, dy;
    dx = winx + x - rfb.copyRectSrcX;
    dy = winy + y - rfb.copyRectSrcY;
    //System.out.println("CopyWU winx:"+winx+" winy: "+winy);
    //System.out.println("CopyWU SrcX:"+rfb.copyRectSrcX+" SrcY: "+rfb.copyRectSrcY);
    //System.out.println("CopyWU dstX:"+x+" dstY: "+y+" dx:"+dx+" dy:"+dy+")");
    win.memGraphics.copyArea(x-dx, y-dy, w, h,
                         dx, dy);

    win.scheduleRepaint(x, y, w, h);
  }

  //
  // Handle an RRE-encoded rectangle.
  //

  void handleRRERect(int x, int y, int w, int h) throws IOException {

    int nSubrects = rfb.is.readInt();

    byte[] bg_buf = new byte[rfb.bytesPixel];
    rfb.is.readFully(bg_buf);
    Color pixel;
    if (rfb.bytesPixel == 1) {
      pixel = colors[bg_buf[0] & 0xFF];
    } else {
      pixel = new Color(bg_buf[2] & 0xFF, bg_buf[1] & 0xFF, bg_buf[0] & 0xFF);
    }
    win.memGraphics.setColor(pixel);
    win.memGraphics.fillRect(x, y, w, h);

    byte[] buf = new byte[nSubrects * (rfb.bytesPixel + 8)];
    rfb.is.readFully(buf);
    DataInputStream ds = new DataInputStream(new ByteArrayInputStream(buf));


    int sx, sy, sw, sh;

    for (int j = 0; j < nSubrects; j++) {
      if (rfb.bytesPixel == 1) {
        pixel = colors[ds.readUnsignedByte()];
      } else {
        ds.skip(4);
        pixel = new Color(buf[j*12+2] & 0xFF,
                          buf[j*12+1] & 0xFF,
                          buf[j*12]   & 0xFF);
      }
      sx = x + ds.readUnsignedShort();
      sy = y + ds.readUnsignedShort();
      sw = ds.readUnsignedShort();
      sh = ds.readUnsignedShort();

      win.memGraphics.setColor(pixel);
      win.memGraphics.fillRect(sx, sy, sw, sh);
    }

    win.scheduleRepaint(x, y, w, h);
  }

  //
  // Handle a CoRRE-encoded rectangle.
  //

  void handleCoRRERect(int x, int y, int w, int h) throws IOException {
    int nSubrects = rfb.is.readInt();

    byte[] bg_buf = new byte[rfb.bytesPixel];
    rfb.is.readFully(bg_buf);
    Color pixel;
    if (rfb.bytesPixel == 1) {
      pixel = colors[bg_buf[0] & 0xFF];
    } else {
      pixel = new Color(bg_buf[2] & 0xFF, bg_buf[1] & 0xFF, bg_buf[0] & 0xFF);
    }
    win.memGraphics.setColor(pixel);
    win.memGraphics.fillRect(x, y, w, h);

    byte[] buf = new byte[nSubrects * (rfb.bytesPixel + 4)];
    rfb.is.readFully(buf);


    int sx, sy, sw, sh;
    int i = 0;

    for (int j = 0; j < nSubrects; j++) {
      if (rfb.bytesPixel == 1) {
        pixel = colors[buf[i++] & 0xFF];
      } else {
        pixel = new Color(buf[i+2] & 0xFF, buf[i+1] & 0xFF, buf[i] & 0xFF);
        i += 4;
      }
      sx = x + (buf[i++] & 0xFF);
      sy = y + (buf[i++] & 0xFF);
      sw = buf[i++] & 0xFF;
      sh = buf[i++] & 0xFF;

      win.memGraphics.setColor(pixel);
      win.memGraphics.fillRect(sx, sy, sw, sh);
    }

    win.scheduleRepaint(x, y, w, h);
  }

  //
  // Handle a Hextile-encoded rectangle.
  //

  // These colors should be kept between handleHextileSubrect() calls.
  private Color hextile_bg, hextile_fg;

  void handleHextileRect(int x, int y, int w, int h) throws IOException {

    hextile_bg = new Color(0);
    hextile_fg = new Color(0);

    for (int ty = y; ty < y + h; ty += 16) {
      int th = 16;
      if (y + h - ty < 16)
        th = y + h - ty;

      for (int tx = x; tx < x + w; tx += 16) {
        int tw = 16;
        if (x + w - tx < 16)
          tw = x + w - tx;

        handleHextileSubrect(tx, ty, tw, th);
      }

      // Finished with a row of tiles, now let's show it.
      win.scheduleRepaint(x, y, w, h);
    }
  }

  //
  // Handle one tile in the Hextile-encoded data.
  //

  void handleHextileSubrect(int tx, int ty, int tw, int th)
    throws IOException {

    int subencoding = rfb.is.readUnsignedByte();

    // Is it a raw-encoded sub-rectangle?
    if ((subencoding & rfb.HextileRaw) != 0) {
      handleRawRect(tx, ty, tw, th, false);
      return;
    }

    // Read and draw the background if specified.
    byte[] cbuf = new byte[rfb.bytesPixel];
    if ((subencoding & rfb.HextileBackgroundSpecified) != 0) {
      rfb.is.readFully(cbuf);
      if (rfb.bytesPixel == 1) {
        hextile_bg = colors[cbuf[0] & 0xFF];
      } else {
        hextile_bg = new Color(cbuf[2] & 0xFF, cbuf[1] & 0xFF, cbuf[0] & 0xFF);
      }
    }
    win.memGraphics.setColor(hextile_bg);
    win.memGraphics.fillRect(tx, ty, tw, th);

    // Read the foreground color if specified.
    if ((subencoding & rfb.HextileForegroundSpecified) != 0) {
      rfb.is.readFully(cbuf);
      if (rfb.bytesPixel == 1) {
        hextile_fg = colors[cbuf[0] & 0xFF];
      } else {
        hextile_fg = new Color(cbuf[2] & 0xFF, cbuf[1] & 0xFF, cbuf[0] & 0xFF);
      }
    }

    // Done with this tile if there is no sub-rectangles.
    if ((subencoding & rfb.HextileAnySubrects) == 0)
      return;

    int nSubrects = rfb.is.readUnsignedByte();
    int bufsize = nSubrects * 2;
    if ((subencoding & rfb.HextileSubrectsColoured) != 0) {
      bufsize += nSubrects * rfb.bytesPixel;
    }
    byte[] buf = new byte[bufsize];
    rfb.is.readFully(buf);

    int b1, b2, sx, sy, sw, sh;
    int i = 0;

    if ((subencoding & rfb.HextileSubrectsColoured) == 0) {

      // Sub-rectangles are all of the same color.
      win.memGraphics.setColor(hextile_fg);
      for (int j = 0; j < nSubrects; j++) {
        b1 = buf[i++] & 0xFF;
        b2 = buf[i++] & 0xFF;
        sx = tx + (b1 >> 4);
        sy = ty + (b1 & 0xf);
        sw = (b2 >> 4) + 1;
        sh = (b2 & 0xf) + 1;
        win.memGraphics.fillRect(sx, sy, sw, sh);
      }
    } else if (rfb.bytesPixel == 1) {

      // BGR233 (8-bit color) version for colored sub-rectangles.
      for (int j = 0; j < nSubrects; j++) {
        hextile_fg = colors[buf[i++] & 0xFF];
        b1 = buf[i++] & 0xFF;
        b2 = buf[i++] & 0xFF;
        sx = tx + (b1 >> 4);
        sy = ty + (b1 & 0xf);
        sw = (b2 >> 4) + 1;
        sh = (b2 & 0xf) + 1;
        win.memGraphics.setColor(hextile_fg);
        win.memGraphics.fillRect(sx, sy, sw, sh);
      }

    } else {

      // Full-color (24-bit) version for colored sub-rectangles.
      for (int j = 0; j < nSubrects; j++) {
        hextile_fg = new Color(buf[i+2] & 0xFF,
                               buf[i+1] & 0xFF,
                               buf[i] & 0xFF);
        i += 4;
        b1 = buf[i++] & 0xFF;
        b2 = buf[i++] & 0xFF;
        sx = tx + (b1 >> 4);
        sy = ty + (b1 & 0xf);
        sw = (b2 >> 4) + 1;
        sh = (b2 & 0xf) + 1;
        win.memGraphics.setColor(hextile_fg);
        win.memGraphics.fillRect(sx, sy, sw, sh);
      }

    }
  }

  //
  // Handle a Zlib-encoded rectangle.
  //

  void handleZlibRect(int x, int y, int w, int h) throws Exception {

    int nBytes = rfb.is.readInt();

    if (zlibBuf == null || zlibBufLen < nBytes) {
      zlibBufLen = nBytes * 2;
      zlibBuf = new byte[zlibBufLen];
    }

    rfb.is.readFully(zlibBuf, 0, nBytes);


    if (zlibInflater == null) {
      zlibInflater = new Inflater();
    }
    zlibInflater.setInput(zlibBuf, 0, nBytes);

    if (rfb.bytesPixel == 1) {
      for (int dy = y; dy < y + h; dy++) {
        zlibInflater.inflate(win.pixels8, dy * win.rect.width + x, w);
      }
    } else {
      byte[] buf = new byte[w * 4];
      int i, offset;
      for (int dy = y; dy < y + h; dy++) {
        zlibInflater.inflate(buf);
        offset = dy * win.rect.width + x;
        for (i = 0; i < w; i++) {
          win.pixels24[offset + i] =
            (buf[i * 4 + 2] & 0xFF) << 16 |
            (buf[i * 4 + 1] & 0xFF) << 8 |
            (buf[i * 4] & 0xFF);
        }
      }
    }

    handleUpdatedPixels(x, y, w, h);
    win.scheduleRepaint(x, y, w, h);
  }

  //
  // Handle a Tight-encoded rectangle.
  //

  void handleTightRect(int x, int y, int w, int h) throws Exception {

    //System.out.println("handleTight " +x+ ", " +y+ ", " +w+ ", " +h);
    int comp_ctl = rfb.is.readUnsignedByte();

    // Flush zlib streams if we are told by the server to do so.
    for (int stream_id = 0; stream_id < 4; stream_id++) {
      if ((comp_ctl & 1) != 0 && tightInflaters[stream_id] != null) {
        tightInflaters[stream_id] = null;
      }
      comp_ctl >>= 1;
    }

    // Check correctness of subencoding value.
    if (comp_ctl > rfb.TightMaxSubencoding) {
      throw new Exception("Incorrect tight subencoding: " + comp_ctl);
    }

    // Handle solid-color rectangles.
    if (comp_ctl == rfb.TightFill) {

      if (rfb.bytesPixel == 1) {
        int idx = rfb.is.readUnsignedByte();
        win.memGraphics.setColor(colors[idx]);
      } else {
        byte[] buf = new byte[3];
        rfb.is.readFully(buf);
        Color bg = new Color(0xFF000000 | (buf[0] & 0xFF) << 16 |
                             (buf[1] & 0xFF) << 8 | (buf[2] & 0xFF));
        win.memGraphics.setColor(bg);
      }
      win.memGraphics.fillRect(x, y, w, h);
      win.scheduleRepaint(x, y, w, h);
      return;

    }

    if (comp_ctl == rfb.TightJpeg) {

      // Read JPEG data.
      byte[] jpegData = new byte[rfb.readCompactLen()];
      rfb.is.readFully(jpegData);

      // Create an Image object from the JPEG data.
      Image jpegImage = Toolkit.getDefaultToolkit().createImage(jpegData);

      // Remember the rectangle where the image should be drawn.
      win.jpegRect = new Rectangle(x, y, w, h);

      // Let the imageUpdate() method do the actual drawing, here just
      // wait until the image is fully loaded and drawn.
      synchronized(win.jpegRect) {
        Toolkit.getDefaultToolkit().prepareImage(jpegImage, -1, -1, win);
        try {
          // Wait no longer than three seconds.
          win.jpegRect.wait(3000);
        } catch (InterruptedException e) {
          throw new Exception("Interrupted while decoding JPEG image");
        }
      }

      // Done, jpegRect is not needed any more.
      win.jpegRect = null;
      return;

    }

    // Read filter id and parameters.
    int numColors = 0, rowSize = w;
    byte[] palette8 = new byte[2];
    int[] palette24 = new int[256];
    boolean useGradient = false;
    if ((comp_ctl & rfb.TightExplicitFilter) != 0) {
      int filter_id = rfb.is.readUnsignedByte();
      if (filter_id == rfb.TightFilterPalette) {
        numColors = rfb.is.readUnsignedByte() + 1;
        if (rfb.bytesPixel == 1) {
          if (numColors != 2) {
            throw new Exception("Incorrect tight palette size: " + numColors);
          }
          rfb.is.readFully(palette8);
        } else {
          byte[] buf = new byte[numColors * 3];
          rfb.is.readFully(buf);
          for (int i = 0; i < numColors; i++) {
            palette24[i] = ((buf[i * 3] & 0xFF) << 16 |
                            (buf[i * 3 + 1] & 0xFF) << 8 |
                            (buf[i * 3 + 2] & 0xFF));
          }
        }
        if (numColors == 2)
          rowSize = (w + 7) / 8;
      } else if (filter_id == rfb.TightFilterGradient) {
        useGradient = true;
      } else if (filter_id != rfb.TightFilterCopy) {
        throw new Exception("Incorrect tight filter id: " + filter_id);
      }
    }
    if (numColors == 0 && rfb.bytesPixel == 4)
      rowSize *= 3;

    // Read, optionally uncompress and decode data.
    int dataSize = h * rowSize;
    if (dataSize < rfb.TightMinToCompress) {
      // Data size is small - not compressed with zlib.
      if (numColors != 0) {
        // Indexed colors.
        byte[] indexedData = new byte[dataSize];
        rfb.is.readFully(indexedData);
        if (numColors == 2) {
          // Two colors.
          if (rfb.bytesPixel == 1) {
            decodeMonoData(x, y, w, h, indexedData, palette8);
          } else {
            decodeMonoData(x, y, w, h, indexedData, palette24);
          }
        } else {
          // 3..255 colors (assuming rfb.bytesPixel == 4).
          int i = 0;
          for (int dy = y; dy < y + h; dy++) {
            for (int dx = x; dx < x + w; dx++) {
              win.pixels24[dy * win.rect.width + dx] =
                palette24[indexedData[i++] & 0xFF];
            }
          }
        }
      } else if (useGradient) {
        // "Gradient"-processed data
        byte[] buf = new byte[w * h * 3];
        rfb.is.readFully(buf);
        decodeGradientData(x, y, w, h, buf);
      } else {
        // Raw truecolor data.
        if (rfb.bytesPixel == 1) {
          for (int dy = y; dy < y + h; dy++) {
            rfb.is.readFully(win.pixels8, dy * win.rect.width + x, w);
          }
        } else {
          byte[] buf = new byte[w * 3];
          int i, offset;
          for (int dy = y; dy < y + h; dy++) {
            rfb.is.readFully(buf);
            offset = dy * win.rect.width + x;
            for (i = 0; i < w; i++) {
              win.pixels24[offset + i] =
                (buf[i * 3] & 0xFF) << 16 |
                (buf[i * 3 + 1] & 0xFF) << 8 |
                (buf[i * 3 + 2] & 0xFF);
            }
          }
        }
      }
    } else {
      // Data was compressed with zlib.
      int zlibDataLen = rfb.readCompactLen();
      byte[] zlibData = new byte[zlibDataLen];
      rfb.is.readFully(zlibData);
      int stream_id = comp_ctl & 0x03;
      if (tightInflaters[stream_id] == null) {
        tightInflaters[stream_id] = new Inflater();
      }
      Inflater myInflater = tightInflaters[stream_id];
      myInflater.setInput(zlibData);
      byte[] buf = new byte[dataSize];
      myInflater.inflate(buf);

      if (numColors != 0) {
        // Indexed colors.
        if (numColors == 2) {
          // Two colors.
          if (rfb.bytesPixel == 1) {
            decodeMonoData(x, y, w, h, buf, palette8);
          } else {
            decodeMonoData(x, y, w, h, buf, palette24);
          }
        } else {
          // More than two colors (assuming rfb.bytesPixel == 4).
          int i = 0;
          for (int dy = y; dy < y + h; dy++) {
            for (int dx = x; dx < x + w; dx++) {
              win.pixels24[dy * win.rect.width + dx] =
                palette24[buf[i++] & 0xFF];
            }
          }
        }
      } else if (useGradient) {
        // Compressed "Gradient"-filtered data (assuming rfb.bytesPixel == 4).
        decodeGradientData(x, y, w, h, buf);
      } else {
        // Compressed truecolor data.
        if (rfb.bytesPixel == 1) {
          int destOffset = y * win.rect.width + x;
          for (int dy = 0; dy < h; dy++) {
            System.arraycopy(buf, dy * w, win.pixels8, destOffset, w);
            destOffset += win.rect.width;
          }
        } else {
          int srcOffset = 0;
          int destOffset, i;
          for (int dy = 0; dy < h; dy++) {
            myInflater.inflate(buf);
            destOffset = (y + dy) * win.rect.width + x;
            for (i = 0; i < w; i++) {
              win.pixels24[destOffset + i] =
                (buf[srcOffset] & 0xFF) << 16 |
                (buf[srcOffset + 1] & 0xFF) << 8 |
                (buf[srcOffset + 2] & 0xFF);
              srcOffset += 3;
            }
          }
        }
      }
    }

    handleUpdatedPixels(x, y, w, h);
    win.scheduleRepaint(x, y, w, h);
  }

  //
  // Decode 1bpp-encoded bi-color rectangle (8-bit and 24-bit versions).
  //

  void decodeMonoData(int x, int y, int w, int h, byte[] src, byte[] palette) {

    int dx, dy, n;
    int i = y * win.rect.width + x;
    int rowBytes = (w + 7) / 8;
    byte b;

    for (dy = 0; dy < h; dy++) {
      for (dx = 0; dx < w / 8; dx++) {
        b = src[dy*rowBytes+dx];
        for (n = 7; n >= 0; n--)
          win.pixels8[i++] = palette[b >> n & 1];
      }
      for (n = 7; n >= 8 - w % 8; n--) {
        win.pixels8[i++] = palette[src[dy*rowBytes+dx] >> n & 1];
      }
      i += (win.rect.width - w);
    }
  }

  void decodeMonoData(int x, int y, int w, int h, byte[] src, int[] palette) {

    int dx, dy, n;
    int i = y * win.rect.width + x;
    int rowBytes = (w + 7) / 8;
    byte b;

    for (dy = 0; dy < h; dy++) {
      for (dx = 0; dx < w / 8; dx++) {
        b = src[dy*rowBytes+dx];
        for (n = 7; n >= 0; n--)
          win.pixels24[i++] = palette[b >> n & 1];
      }
      for (n = 7; n >= 8 - w % 8; n--) {
        win.pixels24[i++] = palette[src[dy*rowBytes+dx] >> n & 1];
      }
      i += (win.rect.width - w);
    }
  }

  //
  // Decode data processed with the "Gradient" filter.
  //

  void decodeGradientData (int x, int y, int w, int h, byte[] buf) {

    int dx, dy, c;
    byte[] prevRow = new byte[w * 3];
    byte[] thisRow = new byte[w * 3];
    byte[] pix = new byte[3];
    int[] est = new int[3];

    int offset = y * win.rect.width + x;

    for (dy = 0; dy < h; dy++) {

      // First pixel in a row 
      for (c = 0; c < 3; c++) {
        pix[c] = (byte)(prevRow[c] + buf[dy * w * 3 + c]);
        thisRow[c] = pix[c];
      }
      win.pixels24[offset++] =
        (pix[0] & 0xFF) << 16 | (pix[1] & 0xFF) << 8 | (pix[2] & 0xFF);

      // Remaining pixels of a row 
      for (dx = 1; dx < w; dx++) {
        for (c = 0; c < 3; c++) {
          est[c] = ((prevRow[dx * 3 + c] & 0xFF) + (pix[c] & 0xFF) -
                    (prevRow[(dx-1) * 3 + c] & 0xFF));
          if (est[c] > 0xFF) {
            est[c] = 0xFF;
          } else if (est[c] < 0x00) {
            est[c] = 0x00;
          }
          pix[c] = (byte)(est[c] + buf[(dy * w + dx) * 3 + c]);
          thisRow[dx * 3 + c] = pix[c];
        }
        win.pixels24[offset++] =
          (pix[0] & 0xFF) << 16 | (pix[1] & 0xFF) << 8 | (pix[2] & 0xFF);
      }

      System.arraycopy(thisRow, 0, prevRow, 0, w * 3);
      offset += (win.rect.width - w);
    }
  }

  //
  // Display newly updated area of pixels.
  //

  void handleUpdatedPixels(int x, int y, int w, int h) {

    // Draw updated pixels of the off-screen image.
    win.pixelsSource.newPixels(x, y, w, h);
    win.memGraphics.setClip(x, y, w, h);
    win.memGraphics.drawImage(win.rawPixelsImage, 0, 0, null);
    win.memGraphics.setClip(0, 0, win.rect.width, win.rect.height);
  }

  //////////////////////////////////////////////////////////////////
  //
  // Handle cursor shape updates (XCursor and RichCursor encodings).
  //

  boolean showSoftCursor = false;

  int[] softCursorPixels;
  MemoryImageSource softCursorSource;
  Image softCursor;

  int cursorX = 0, cursorY = 0;
  int cursorWidth, cursorHeight;
  int hotX, hotY;
  VncWindow windowWithCursor;

  //
  // Handle cursor shape update (XCursor and RichCursor encodings).
  //

  synchronized void
    handleCursorShapeUpdate(int encodingType,
                            int xhot, int yhot, int width, int height, VncWindow w)
    throws IOException {

    int bytesPerRow = (width + 7) / 8;
    int bytesMaskData = bytesPerRow * height;

    softCursorFree();

    if (width * height == 0)
      return;

    // Ignore cursor shape data if requested by user.

    if (rfb.options.getIgnoreCursorUpdates()) {
      if (encodingType == rfb.EncodingXCursor) {
        rfb.is.skipBytes(6 + bytesMaskData * 2);
      } else {
        // rfb.EncodingRichCursor
        rfb.is.skipBytes(width * height + bytesMaskData);
      }
      return;
    }

    // Decode cursor pixel data.

    softCursorPixels = new int[width * height];

    if (encodingType == rfb.EncodingXCursor) {

      // Read foreground and background colors of the cursor.
      byte[] rgb = new byte[6];
      rfb.is.readFully(rgb);
      int[] colors = { (0xFF000000 | (rgb[3] & 0xFF) << 16 |
                        (rgb[4] & 0xFF) << 8 | (rgb[5] & 0xFF)),
                       (0xFF000000 | (rgb[0] & 0xFF) << 16 |
                        (rgb[1] & 0xFF) << 8 | (rgb[2] & 0xFF)) };

      // Read pixel and mask data.
      byte[] pixBuf = new byte[bytesMaskData];
      rfb.is.readFully(pixBuf);
      byte[] maskBuf = new byte[bytesMaskData];
      rfb.is.readFully(maskBuf);

      // Decode pixel data into softCursorPixels[].
      byte pixByte, maskByte;
      int x, y, n, result;
      int i = 0;
      for (y = 0; y < height; y++) {
        for (x = 0; x < width / 8; x++) {
          pixByte = pixBuf[y * bytesPerRow + x];
          maskByte = maskBuf[y * bytesPerRow + x];
          for (n = 7; n >= 0; n--) {
            if ((maskByte >> n & 1) != 0) {
              result = colors[pixByte >> n & 1];
            } else {
              result = 0;        // Transparent pixel
            }
            softCursorPixels[i++] = result;
          }
        }
        for (n = 7; n >= 8 - width % 8; n--) {
          if ((maskBuf[y * bytesPerRow + x] >> n & 1) != 0) {
            result = colors[pixBuf[y * bytesPerRow + x] >> n & 1];
          } else {
            result = 0;                // Transparent pixel
          }
          softCursorPixels[i++] = result;
        }
      }

    } else {
      // encodingType == rfb.EncodingRichCursor

      // Read pixel and mask data.
      byte[] pixBuf = new byte[width * height * rfb.bytesPixel];
      rfb.is.readFully(pixBuf);
      byte[] maskBuf = new byte[bytesMaskData];
      rfb.is.readFully(maskBuf);

      // Decode pixel data into softCursorPixels[].
      byte pixByte, maskByte;
      int x, y, n, result;
      int i = 0;
      for (y = 0; y < height; y++) {
        for (x = 0; x < width / 8; x++) {
          maskByte = maskBuf[y * bytesPerRow + x];
          for (n = 7; n >= 0; n--) {
            if ((maskByte >> n & 1) != 0) {
              if (rfb.bytesPixel == 1) {
                result = cm8.getRGB(pixBuf[i]);
              } else {
                result = 0xFF000000 |
                  (pixBuf[i * 4 + 1] & 0xFF) << 16 |
                  (pixBuf[i * 4 + 2] & 0xFF) << 8 |
                  (pixBuf[i * 4 + 3] & 0xFF);
              }
            } else {
              result = 0;        // Transparent pixel
            }
            softCursorPixels[i++] = result;
          }
        }
        for (n = 7; n >= 8 - width % 8; n--) {
          if ((maskBuf[y * bytesPerRow + x] >> n & 1) != 0) {
            if (rfb.bytesPixel == 1) {
              result = cm8.getRGB(pixBuf[i]);
            } else {
              result = 0xFF000000 |
                (pixBuf[i * 4 + 1] & 0xFF) << 16 |
                (pixBuf[i * 4 + 2] & 0xFF) << 8 |
                (pixBuf[i * 4 + 3] & 0xFF);
            }
          } else {
            result = 0;                // Transparent pixel
          }
          softCursorPixels[i++] = result;
        }
      }

    }

    // Draw the cursor on an off-screen image.

    softCursorSource =
      new MemoryImageSource(width, height, softCursorPixels, 0, width);
    // TODO -- may have to change this - create generically
    softCursor = Toolkit.getDefaultToolkit().createImage(softCursorSource);

    // Set remaining data associated with cursor.

    cursorWidth = width;
    cursorHeight = height;
    hotX = xhot;
    hotY = yhot;
    windowWithCursor = w;

    showSoftCursor = true;

    // Show the cursor.
    w.repaint(rfb.options.getDeferCursorUpdates(), cursorX - hotX - w.rect.x,
              cursorY - hotY - w.rect.y, cursorWidth, cursorHeight);
  }

  //
  // softCursorMove(). Moves soft cursor into a particular location.
  //

  synchronized void softCursorMove(int x, int y, VncWindow w) {
    if (showSoftCursor) {
      //System.out.println("softCursorMove " + x + " " + y);
      w.repaint(rfb.options.getDeferCursorUpdates(),
                cursorX - hotX - w.cursorOffset.x, cursorY - hotY - w.cursorOffset.y,
                cursorWidth, cursorHeight);
      w.repaint(rfb.options.getDeferCursorUpdates(),
                x - hotX - w.cursorOffset.x, y - hotY - w.cursorOffset.y,
                cursorWidth, cursorHeight);
    }
    cursorX = x;
    cursorY = y;
    windowWithCursor = w;
  }

  //
  // softCursorFree(). Remove soft cursor, dispose resources.
  //

  synchronized void softCursorFree() {
    if (showSoftCursor) {
      showSoftCursor = false;
      softCursor = null;
      softCursorSource = null;
      softCursorPixels = null;

      for (Enumeration e = windowsHash.elements(); e.hasMoreElements();) 
      {
        VncWindow w = (VncWindow) e.nextElement();
        if (w.rect.contains(cursorX, cursorY)) 
        {
          w.repaint(rfb.options.getDeferCursorUpdates(),
              cursorX - hotX - w.rect.x, cursorY - hotY - w.rect.y,
              cursorWidth, cursorHeight);
        }
      }
    }
  }
  
}
