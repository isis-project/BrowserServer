OBJDIR := $(BUILD_TYPE)-$(PLATFORM)

# ------------------------------------------------------------------

TARGET_LIB := $(OBJDIR)/libYap.a

LIB_SOURCES := \
	YapPacket.cpp \
	YapProxy.cpp \
	YapServer.cpp \
	YapClient.cpp \
	IpcBuffer.cpp \
	BufferLock.cpp \
	BrowserRect.cpp \
	PluginDirWatcher.cpp

TARGET_APP := $(OBJDIR)/BrowserServer

APP_SOURCES := \
	Main.cpp \
	BrowserServer.cpp \
	BrowserPage.cpp \
	BrowserSyncReplyPipe.cpp \
	BrowserServerBase.cpp \
	BrowserOffscreenQt.cpp \
	BrowserPageManager.cpp \
	BackupManager.cpp \
	CpuAffinity.cpp \
	WebKitEventListener.cpp \
	Settings.cpp \
	SSLSupport.cpp \
	BrowserPage.moc.cpp \
	BrowserComboBox.cpp \
	BrowserComboBox.moc.cpp \
	qwebkitplatformplugin.moc.cpp \
	WebOSPlatformPlugin.moc.cpp

LIB_OBJS := $(LIB_SOURCES:%.cpp=$(OBJDIR)/%.o)
APP_OBJS := $(APP_SOURCES:%.cpp=$(OBJDIR)/%.o)

SOURCES := $(LIB_SOURCES) $(APP_SOURCES)

# ------------------------------------------------------------------

ifeq ("$(BUILD_TYPE)", "debug")
FLAGS_OPT := -O0 -pg -g -fno-exceptions -fno-rtti -fvisibility=hidden -DDEBUG -fPIC
else
FLAGS_OPT := -O2 -fno-exceptions -fno-rtti -fvisibility=hidden -DNDEBUG -fPIC
endif

ifeq ("$(PLATFORM)", "arm")
	FLAGS_OPT += -DTARGET_DEVICE
endif

INCLUDES := $(INCLUDES) \
	-I./Src -I./Yap \
	-I$(INCLUDE_DIR)/ime \
	-I$(INCLUDE_DIR)/QtCore \
	-I$(INCLUDE_DIR)/QtGui \
	-I$(INCLUDE_DIR)/QtNetwork \
	-I$(INCLUDE_DIR)/QtWebKit \
	-I$(INCLUDE_DIR)/WebKitSupplemental \
	`pkg-config --cflags glib-2.0`

LIBS := $(LIBS) -lglib-2.0 -llunaservice -lcjson -lpbnjson_cpp -lPmCertificateMgr -lQtGui -lQtWebKit -lQtNetwork -lWebKitMisc

ifeq ($(PLATFORM),arm)
LIBS := $(LIBS) -lmemchute
endif

LOCAL_CFLAGS := $(CXXFLAGS) -Wall -Werror $(FLAGS_OPT) 

LOCAL_LFLAGS := $(LDFLAGS) $(FLAGS_OPT) $(LIBS) 

# ------------------------------------------------------------------

all: setup $(TARGET_LIB) $(TARGET_APP) BrowserServer.conf

setup:
	@mkdir -p $(OBJDIR)

.PHONY: stage
stage: $(TARGET_APP) $(OBJDIR)/libYap.a
	install -d $(STAGING_INCDIR)/Yap
	install -m 444 Yap/YapDefs.h   $(STAGING_INCDIR)/Yap
	install -m 444 Yap/YapClient.h $(STAGING_INCDIR)/Yap
	install -m 444 Yap/YapPacket.h $(STAGING_INCDIR)/Yap
	install -m 444 Yap/YapProxy.h  $(STAGING_INCDIR)/Yap
	install -m 444 Yap/YapServer.h $(STAGING_INCDIR)/Yap
	install -m 444 Yap/BufferLock.h $(STAGING_INCDIR)/Yap
	install -m 444 Yap/OffscreenBuffer.h $(STAGING_INCDIR)/Yap
	install -m 444 Src/IpcBuffer.h $(STAGING_INCDIR)
	install -m 444 Src/BrowserRect.h $(STAGING_INCDIR)
	install -m 444 Src/BrowserOffscreenInfo.h $(STAGING_INCDIR)
	install -m 444 Src/BrowserOffscreenCalculations.h $(STAGING_INCDIR)
	install -d $(STAGING_LIBDIR)
	install -m 444 $(OBJDIR)/libYap.a $(STAGING_LIBDIR)/libYap.a

$(TARGET_LIB): $(LIB_OBJS)
	$(AR) rcs $(TARGET_LIB) $(LIB_OBJS)

$(TARGET_APP): $(APP_OBJS) $(TARGET_LIB)
	$(CXX) -o $(TARGET_APP) $(APP_OBJS) $(LIB_OBJS) $(LOCAL_LFLAGS)

qwebkitplatformplugin.moc.cpp: $(INCLUDE_DIR)/WebKitSupplemental/qwebkitplatformplugin.h
	$(MOC) -o $@ $<

WebOSPlatformPlugin.moc.cpp: $(INCLUDE_DIR)/WebKitSupplemental/WebOSPlatformPlugin.h
	$(MOC) -o $@ $<

BrowserComboBox.moc.cpp: BrowserComboBox.h
	$(MOC) -o $@ $<

BrowserPage.moc.cpp: BrowserPage.h
	$(MOC) -o $@ $<

.PHONY: BrowserServer.conf
BrowserServer.conf:
	./conf/mergeconf.pl conf/BrowserServer_base.conf conf/BrowserServer_${MACHINE}.conf > $@

vpath %.cpp Yap Src
vpath %.h Src

$(OBJDIR)/%.o: %.cpp
	$(CXX) -MD $(INCLUDES) $(LOCAL_CFLAGS) -c `readlink -fn $<` -o $@

-include $(SOURCES:%.cpp=$(OBJDIR)/%.d)

.PHONY: clean
clean:
	rm -rf $(OBJDIR)
	rm -rf $(STAGING_INCDIR)/Yap
	rm -f $(OBJDIR)/libYap.a
	rm -f $(STAGING_LIBDIR)/libYap.a
	rm -f $(TARGET_APP)
	rm -f BrowserPage.moc.cpp
	rm -f BrowserServer.conf
	rm -f BrowserComboBox.moc.cpp
	rm -f qwebkitplatformplugin.moc.cpp
	rm -f WebOSPlatformPlugin.moc.cpp
