//
//  Copyright (C) 2005 Grant Wallace. Princeton University.  All Rights Reserved.
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
//
// Options data corresponding to OptionFrame
//
// This deals with all the options the user can play with.
// It sets the encodings array and some booleans.
//

class Options {
  static String[] encodingNames = {
    "Raw", "RRE", "CoRRE", "Hextile", "Zlib", "Tight"
  };
  static String[] compressLevelNames = {
    "Default", "1", "2", "3", "4", "5", "6", "7", "8", "9"
  };
  static String[] jpegQualityNames = {
    "JPEG off", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9"
  };
  static int [] encodingValues = {
    RfbProto.EncodingRaw,
    RfbProto.EncodingRRE,
    RfbProto.EncodingCoRRE,
    RfbProto.EncodingHextile,
    RfbProto.EncodingZlib,
    RfbProto.EncodingTight
  };

  private int[] encodings = new int[20];
  private int nEncodings;

  private int compressLevel;
    // 0 -> "Default", 1 -> "1", 2 -> "2", ...
  private int jpegQuality;
    // -1 -> "JPEG off", 0 -> "0", 1 -> "1", ...

  private boolean eightBitColors;

  private boolean requestCursorUpdates;
  private boolean ignoreCursorUpdates;

  private boolean reverseMouseButtons2And3;
  private boolean shareDesktop;
  private boolean viewOnly;

  private int preferredEncoding;
  private boolean useCopyRect;

  private int deferScreenUpdates;
  private int deferCursorUpdates;
  private int deferUpdateRequests;

  private boolean useSharedApp;
  private int multiCursor;
  private boolean useBlackOut;

  public boolean trace = false;


  Options() {
    // Set up defaults

    preferredEncoding = RfbProto.EncodingTight;
    //preferredEncoding = RfbProto.EncodingRaw;

    compressLevel = 0; /* default */
    jpegQuality = 6;
    requestCursorUpdates = true;
    ignoreCursorUpdates = false;
    useCopyRect = true;
    useSharedApp = true;
    useBlackOut = false;

    eightBitColors = false;
    reverseMouseButtons2And3 = false;
    viewOnly = false;
    shareDesktop = true;

    deferScreenUpdates = 20;
    deferCursorUpdates = 10;
    deferUpdateRequests = 50;


    // Make the encodings array
    setEncodings();
  }

  //
  // setEncodings looks at the encoding, compression level, JPEG
  // quality level, cursor shape updates and copyRect choices and sets
  // the encodings array appropriately.
  //

  void setEncodings() {
    nEncodings = 0;
    if (useCopyRect) {
      encodings[nEncodings++] = RfbProto.EncodingCopyRect;
    }

    boolean enableCompressLevel = false;

    encodings[nEncodings++] = preferredEncoding;
    if (preferredEncoding != RfbProto.EncodingHextile) {
      encodings[nEncodings++] = RfbProto.EncodingHextile;
    }
    if (preferredEncoding != RfbProto.EncodingTight) {
      encodings[nEncodings++] = RfbProto.EncodingTight;
      enableCompressLevel = true;
    }
    if (preferredEncoding != RfbProto.EncodingZlib) {
      encodings[nEncodings++] = RfbProto.EncodingZlib;
      enableCompressLevel = true;
    }
    if (preferredEncoding != RfbProto.EncodingCoRRE) {
      encodings[nEncodings++] = RfbProto.EncodingCoRRE;
    }
    if (preferredEncoding != RfbProto.EncodingRRE) {
      encodings[nEncodings++] = RfbProto.EncodingRRE;
    }

    // Handle compression level setting.

    if (enableCompressLevel &&
        compressLevel >= 1 && compressLevel <= 9) {
      encodings[nEncodings++] =
        RfbProto.EncodingCompressLevel0 + compressLevel;
    }

    // Handle JPEG quality setting.

    if (preferredEncoding == RfbProto.EncodingTight && !eightBitColors &&
        jpegQuality >= 0 && jpegQuality <= 9) {
      encodings[nEncodings++] =
        RfbProto.EncodingQualityLevel0 + jpegQuality;
    }

    // Request cursor shape updates if necessary.

    if (requestCursorUpdates) {
      encodings[nEncodings++] = RfbProto.EncodingXCursor;
      encodings[nEncodings++] = RfbProto.EncodingRichCursor;
      if (!ignoreCursorUpdates)
        encodings[nEncodings++] = RfbProto.EncodingPointerPos;
    }

    if (useSharedApp)
    {
      //System.out.println("adding useSharedApp to encodings");
      encodings[nEncodings++] = RfbProto.EncodingSharedApp;
    }


    encodings[nEncodings++] = RfbProto.EncodingLastRect;
    encodings[nEncodings++] = RfbProto.EncodingNewFBSize;

  }

  public int[] getEncodings() {
    return encodings;
  }

  public int getNEncodings() {
    return nEncodings;
  }

  public void setPreferredEncoding(int encoding) {
    preferredEncoding = encoding;
    setEncodings();
  }

  public int getPreferredEncoding() {
    return preferredEncoding;
  }

  public void setUseCopyRect(boolean flag) {
    useCopyRect = flag;
    setEncodings();
  }

  public boolean getUseCopyRect() {
    return useCopyRect;
  }

  public void setCompressLevel(int level) {
    compressLevel = level;
    setEncodings();
  }

  public int getCompressLevel() {
    return compressLevel;
  }

  public void setJpegQuality(int quality) {
    jpegQuality = quality;
    setEncodings();
  }

  public int getJpegQuality() {
    return jpegQuality;
  }

  public void setEightBitColors(boolean flag) {
    eightBitColors = flag;
  }

  public boolean getEightBitColors() {
    return eightBitColors;
  }

  public void setRequestCursorUpdates(boolean _requestCursorUpdates) {
    requestCursorUpdates = _requestCursorUpdates;
    setEncodings();
  }

  public boolean getRequestCursorUpdates() {
    return requestCursorUpdates;
  }

  public void setIgnoreCursorUpdates(boolean _ignoreCursorUpdates) {
    ignoreCursorUpdates  = _ignoreCursorUpdates;
    setEncodings();
  }

  public boolean getIgnoreCursorUpdates() {
    return ignoreCursorUpdates;
  }

  public void setReverseMouseButtons2And3(boolean flag) {
    reverseMouseButtons2And3 = flag;
  }

  public boolean getReverseMouseButtons2And3() {
    return reverseMouseButtons2And3;
  }

  public void setShareDesktop(boolean flag) {
    shareDesktop = flag;
  }

  public boolean getShareDesktop() {
    return shareDesktop;
  }

  public void setViewOnly(boolean flag) {
    viewOnly = flag;
  }

  public boolean getViewOnly() {
    return viewOnly;
  }

  public void setDeferScreenUpdates(int d) {
    deferScreenUpdates = d;
  }

  public int getDeferScreenUpdates() {
    return deferScreenUpdates;
  }

  public void setDeferCursorUpdates(int d) {
    deferCursorUpdates = d;
  }

  public int getDeferCursorUpdates() {
    return deferCursorUpdates;
  }

  public void setDeferUpdateRequests(int d) {
    deferUpdateRequests = d;
  }

  public int getDeferUpdateRequests() {
    return deferUpdateRequests;
  }

  public void setUseSharedApp(boolean flag) {
    useSharedApp = flag;
    setEncodings();
  }

  public void setUseBlackOut(boolean flag) {
    useBlackOut = flag;
  }

  public boolean getUseBlackOut() {
    return useBlackOut;
  }

  public void setMultiCursor(int c) {
    multiCursor = c;
  }

  public int getMultiCursor() {
    return multiCursor;
  }


}
