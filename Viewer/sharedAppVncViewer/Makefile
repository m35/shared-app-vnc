#
# Making the VNC applet.
#

CP = cp
JC = javac
JAR = jar
ARCHIVE = ShareWinVncViewer.jar
MANIFEST = MANIFEST.MF
INSTALL_DIR = /usr/local/vnc/classes

CLASSES = ShareWinVnc.class RfbProto.class Dispatcher.class \
	  Options.class DesCipher.class PasswdDialog.class \
	  SocketFactory.class VncWindow.class

SOURCES = ShareWinVnc.java RfbProto.java Dispatcher.java \
	  Options.java DesCipher.java PasswdDialog.java \
	  SocketFactory.java VncWindow.java

all: $(CLASSES) $(ARCHIVE)

$(CLASSES): $(SOURCES)
	$(JC) -O $(SOURCES)

$(ARCHIVE): $(CLASSES) $(MANIFEST)
	$(JAR) cfm $(ARCHIVE) $(MANIFEST) $(CLASSES)

install: $(CLASSES) $(ARCHIVE)
	$(CP) $(CLASSES) $(ARCHIVE) $(PAGES) $(INSTALL_DIR)

export:: $(CLASSES) $(ARCHIVE) $(PAGES)
	@$(ExportJavaClasses)

clean::
	$(RM) *.class *.jar
