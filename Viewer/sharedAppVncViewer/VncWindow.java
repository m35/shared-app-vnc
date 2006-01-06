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

import java.awt.*;
import java.awt.event.*;
import java.awt.image.*;
import javax.swing.*;
import java.io.*;


//
// VncWindow draws a VNC shared window.
//

class VncWindow extends JComponent
  implements KeyListener, MouseListener, MouseMotionListener, WindowListener, ImageObserver {

  JFrame winframe;
  JScrollPane scrollPane;

  Dispatcher dispatcher;
  RfbProto rfb;

  int windowId;
  Rectangle rect;
  Point cursorOffset;

  Image memImage;
  Graphics memGraphics;

  Image rawPixelsImage;
  MemoryImageSource pixelsSource;
  byte[] pixels8;
  int[] pixels24;
  int bytesPixel;

  // Since JPEG images are loaded asynchronously, we have to remember
  // their position in the framebuffer. Also, this jpegRect object is
  // used for synchronization between the rfbThread and a JVM's thread
  // which decodes and loads JPEG images.
  Rectangle jpegRect;

  // True if we process keyboard and mouse events.
  boolean inputEnabled;

  //
  // The constructor.
  //

  VncWindow(Dispatcher _dispatcher, Point location) throws IOException {
    dispatcher = _dispatcher;
    rfb = dispatcher.rfb; // for convenience

    // This overrides windows look and feel which unfortunately
    // remembers the size and location of last window
    JFrame.setDefaultLookAndFeelDecorated(true);

    winframe = new JFrame();
    scrollPane = new JScrollPane(this);
    //winframe.setSize(10,10);
    winframe.getContentPane().add(scrollPane);
    winframe.addWindowListener(this);
    winframe.setDefaultCloseOperation(JFrame.HIDE_ON_CLOSE);

    ImageIcon icon = new ImageIcon("icon.jpg");
    winframe.setIconImage(icon.getImage());

    winframe.setLocation(location);

    rect = new Rectangle();
    cursorOffset = new Point(0,0);

    setFocusTraversalKeysEnabled(false); // we do not use TAB key for changing focuses

    inputEnabled = false;
    if (!rfb.options.getViewOnly())
    {
      enableInput(true);
    } else {
      // Keyboard listener is enabled even in view-only mode, to catch
      // 'r' or 'R' key presses used to request screen update.
      //addKeyListener(this);
    }

    winframe.pack();
    //winframe.setVisible(true);
  }

  public void close()
  {
    winframe.setVisible(false);
    winframe.dispose();
  }

  //
  // Callback methods to determine geometry of our Component.
  //

  public Dimension getPreferredSize() {
    return new Dimension(rect.width, rect.height);
  }

  public Dimension getMinimumSize() {
    return new Dimension(rect.width, rect.height);
  }

  public Dimension getMaximumSize() {
    return new Dimension(rect.width, rect.height);
  }

  public Dimension getSize() {
    return new Dimension(rect.width, rect.height);
  }

  public int getWidth() {
    return rect.width;
  }

  public int getHeight() {
    return rect.height;
  }


  void updateWindowSize(int width, int height, int bPixel) 
  {
    if (width==0 || height==0) return;
    rect.width = width;
    rect.height = height;
    bytesPixel = bPixel;

    //scrollPane.setSize(width, height);
    //winframe.pack();
    //winframe.setVisible(true);

    // Create new off-screen image either if it does not exist, or if
    // its geometry should be changed. It's not necessary to replace
    // existing image if only pixel format should be changed.
    if (memImage == null) {
      memImage = createImage(width, height);
      memGraphics = memImage.getGraphics();
    } else if (memImage.getWidth(null) != width ||
               memImage.getHeight(null) != height) {
      synchronized(memImage) {
        memImage = createImage(width, height);
        memGraphics = memImage.getGraphics();
      }
    }

    // Images with raw pixels should be re-allocated on every change
    // of geometry or pixel format.
    if (bytesPixel == 1) {
      pixels24 = null;
      pixels8 = new byte[width * height];

      pixelsSource =
        new MemoryImageSource(width, height, dispatcher.cm8, pixels8, 0, width);
    } else {
      pixels8 = null;
      pixels24 = new int[width * height];

      pixelsSource =
        new MemoryImageSource(width, height, dispatcher.cm24, pixels24, 0, width);
    }
    pixelsSource.setAnimated(true);
    rawPixelsImage = createImage(pixelsSource);

    // Update the size of desktop containers.
    scrollPane.setSize(width, height);
    winframe.setSize(width, height);
    winframe.pack();
    winframe.setVisible(true);
    System.out.println("image size: " + width + " " + height);
    System.out.println("scrollpane size: " + scrollPane.getWidth() + " " + scrollPane.getHeight());
    System.out.println("frame size: " + winframe.getWidth() + " " + winframe.getHeight());
  }


  public void setTitle(String title)
  {
    winframe.setTitle(title + " - " + dispatcher.remoteHost);
  }

  public void toFront()
  {
    winframe.toFront();
  }

  //
  // All painting is performed here.
  //

//  public void update(Graphics g) {
//    paint(g);
//  }

  public void paintComponent(Graphics g) {
    if (memImage != null)
    {
      synchronized(memImage) {
        g.drawImage(memImage, 0, 0, null);
      }
      if (dispatcher.showSoftCursor && dispatcher.windowWithCursor == this) {
        //int x0 = dispatcher.cursorX - dispatcher.hotX - rect.x;
        //int y0 = dispatcher.cursorY - dispatcher.hotY - rect.y;
        int x0 = dispatcher.cursorX - dispatcher.hotX - cursorOffset.x;
        int y0 = dispatcher.cursorY - dispatcher.hotY - cursorOffset.y;
        //System.out.println("cursor pos "+x0+" "+y0);
        Rectangle r = new Rectangle(x0, y0, dispatcher.cursorWidth, dispatcher.cursorHeight);
        if (r.intersects(g.getClipBounds())) {
          //System.out.println("draw cursor "+x0+" "+y0);
          g.drawImage(dispatcher.softCursor, x0, y0, null);
        }
      }
    }
  }


  //
  // Tell JVM to repaint specified desktop area.
  //

  void scheduleRepaint(int x, int y, int w, int h) {
    // Request repaint, deferred if necessary.
    repaint(rfb.options.getDeferScreenUpdates(), x, y, w, h);
  }


  //
  // Override the ImageObserver interface method to handle drawing of
  // JPEG-encoded data.
  //

  public boolean imageUpdate(Image img, int infoflags,
                             int x, int y, int width, int height) {
    if ((infoflags & (ALLBITS | ABORT)) == 0) {
      return true;                // We need more image data.
    } else {
      // If the whole image is available, draw it now.
      if ((infoflags & ALLBITS) != 0) {
        if (jpegRect != null) {
          synchronized(jpegRect) {
            memGraphics.drawImage(img, jpegRect.x, jpegRect.y, null);
            scheduleRepaint(jpegRect.x, jpegRect.y,
                            jpegRect.width, jpegRect.height);
            jpegRect.notify();
          }
        }
      }
      return false;                // All image data was processed.
    }
  }

  //
  // Start/stop receiving mouse events. Keyboard events are received
  // even in view-only mode, because we want to map the 'r' key to the
  // screen refreshing function.
  //

  public void enableInput(boolean enable) {
    if (enable && !inputEnabled) {
      inputEnabled = true;
      addMouseListener(this);
      addMouseMotionListener(this);
      addKeyListener(this);
    } else if (!enable && inputEnabled) {
      inputEnabled = false;
      removeMouseListener(this);
      removeMouseMotionListener(this);
      removeKeyListener(this);
    }
  }

  public void setWindowId(int id)
  {
    windowId = id;
  }

  public void setVncOrigin(int x, int y)
  {
    rect.x = x;
    rect.y = y;
  }

  public void setCursorOffset(int x, int y)
  {
    cursorOffset.x = x;
    cursorOffset.y = y;
  }


  //
  // Handle events.
  //

  public void keyPressed(KeyEvent evt) {
    processLocalKeyEvent(evt);
  }
  public void keyReleased(KeyEvent evt) {
    processLocalKeyEvent(evt);
  }
  public void keyTyped(KeyEvent evt) {
    evt.consume();
  }

  public void mousePressed(MouseEvent evt) {
    requestFocus();
    processLocalMouseEvent(evt, false);
  }
  public void mouseReleased(MouseEvent evt) {
    processLocalMouseEvent(evt, false);
  }
  public void mouseMoved(MouseEvent evt) {
    processLocalMouseEvent(evt, true);
  }
  public void mouseDragged(MouseEvent evt) {
    processLocalMouseEvent(evt, true);
  }

  public void processLocalKeyEvent(KeyEvent evt) {
    if (rfb != null && rfb.inNormalProtocol) {
      if (!inputEnabled) {
        if ((evt.getKeyChar() == 'r' || evt.getKeyChar() == 'R') &&
            evt.getID() == KeyEvent.KEY_PRESSED ) {
          // Request screen update.
          try {
            rfb.writeFramebufferUpdateRequest(0, 0, rfb.framebufferWidth,
                                              rfb.framebufferHeight, false);
          } catch (IOException e) {
            e.printStackTrace();
          }
        }
      } else {
        // Input enabled.
        synchronized(rfb) {
          try {
            rfb.writeKeyEvent(windowId, evt);
          } catch (Exception e) {
            e.printStackTrace();
          }
          rfb.notify();
        }
      }
    }
    // Don't ever pass keyboard events to AWT for default processing. 
    // Otherwise, pressing Tab would switch focus to ButtonPanel etc.
    evt.consume();
  }

  public void processLocalMouseEvent(MouseEvent evt, boolean moved) {
    if (rfb != null && rfb.inNormalProtocol) 
    {
//      evt.translatePoint(rect.x, rect.y);
      evt.translatePoint(cursorOffset.x, cursorOffset.y);

      //System.out.println("pointer move: " + evt.getX() + " " + evt.getY() + " : " + 
      //    cursorOffset.x + " " + cursorOffset.y + " " + windowId);

      if (moved) {
        dispatcher.softCursorMove(evt.getX(), evt.getY(), this);
      }

      synchronized(rfb) {
        try {
          rfb.writePointerEvent(windowId, evt);
        } catch (Exception e) {
          e.printStackTrace();
        }
        rfb.notify();
      }
    }
  }

  //
  // Ignored events.
  //

  public void mouseClicked(MouseEvent evt) {}
  public void mouseEntered(MouseEvent evt) {}
  public void mouseExited(MouseEvent evt) 
  {
    dispatcher.softCursorFree();
  }

  public void windowClosing(WindowEvent e) 
  {
    //winframe.setState(JFrame.ICONIFIED);
  }

  public void windowOpened(WindowEvent e) {}  
  public void windowActivated(WindowEvent e) {}
  public void windowClosed(WindowEvent e) {}
  public void windowDeactivated(WindowEvent e) {}
  public void windowDeiconified(WindowEvent e) {}
  public void windowIconified(WindowEvent e)  {}


}
