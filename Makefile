BUILD_TYPE := release
PLATFORM   := $(TARGET_ARCH)

MOC := ${STAGING_BINDIR_NATIVE}/moc-palm

ifneq (,$(findstring BUILD_BACKUP_MANAGER,$(CXXFLAGS)))
	BACKUP_MANAGER_SOURCE=BackupManager.cpp WebKitEventListener.cpp
else
	BACKUP_MANAGER_SOURCE=
endif

ifneq (,$(findstring BUILD_CPU_AFFINITY,$(CXXFLAGS)))
	CPU_AFFINITY_SOURCE=CpuAffinity.cpp
	LIBAFFINITY=-laffinity
else
	CPU_AFFINITY_SOURCE=
	LIBAFFINITY=
endif

ifneq (,$(findstring USE_CERT_MGR,$(CXXFLAGS)))
	LIBCERTMGR=-lPmCertificateMgr
else
	LIBCERTMGR=
endif

ifneq (,$(findstring USE_MEMCHUTE,$(CXXFLAGS)))
	LIBMEMCHUTE=-lmemchute
else
	LIBMEMCHUTE=
endif

ifneq (,$(findstring BUILD_SSL_SUPPORT,$(CXXFLAGS)))
	SSL_SUPPORT_SOURCE=SSLSupport.cpp
else
	SSL_SUPPORT_SOURCE=
endif

INCLUDES := \
	-isystem $(STAGING_INCDIR)/PmCertificateMgr/IncsPublic \
	-isystem $(STAGING_INCDIR)/webkit \
	-I./Src \
	-I./Yap \
        -I$(STAGING_INCDIR) \
	-I$(STAGING_INCDIR)/ime \
	-I$(STAGING_INCDIR)/QtWebKit \
        -I$(STAGING_INCDIR)/sysmgr-ipc \
	-I$(STAGING_INCDIR)/WebKitSupplemental \
        -I$(INCLUDE_DIR) \
        -I$(INCLUDE_DIR)/Qt \
	-I$(INCLUDE_DIR)/QtCore \
	-I$(INCLUDE_DIR)/QtGui \
	-I$(INCLUDE_DIR)/QtNetwork \
	`pkg-config --cflags glib-2.0` \
	`pkg-config --cflags luna-service2`

LIBS := \
    $(LIBAFFINITY) \
	-lpthread \
	$(LIBMEMCHUTE) \
	-lglib-2.0 \
	-lpbnjson_cpp \
	$(LIBCERTMGR) \
        -lrt \
        -lcrypto \
        -lQtCore \
        -ldl \
        -lssl \
	-lQtGui \
	-lQtWebKit \
	-lQtNetwork \
	-lWebKitMisc \
        `pkg-config --libs gthread-2.0` \
        `pkg-config --libs luna-service2`

FLAGS_OPT := -fno-exceptions -fno-rtti -fvisibility=hidden -DDEBUG -fPIC -DTARGET_DEVICE

ifeq ("$(BUILD_TYPE)", "debug")
FLAGS_OPT := -O0 -pg -g $(FLAGS_OPT)
else
FLAGS_OPT := -O2 $(FLAGS_OPT)
endif

LOCAL_CFLAGS := $(CXXFLAGS) -Wall -Werror $(FLAGS_OPT)

LOCAL_LFLAGS := $(LDFLAGS) $(FLAGS_OPT) $(LIBS)

OBJDIR := $(BUILD_TYPE)-$(PLATFORM)

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
	$(BACKUP_MANAGER_SOURCE) \
	$(CPU_AFFINITY_SOURCE) \
	Settings.cpp \
	$(SSL_SUPPORT_SOURCE) \
	JsonUtils.cpp \
	BrowserPage.moc.cpp \
	BrowserComboBox.cpp \
	BrowserComboBox.moc.cpp \
	qwebkitplatformplugin.moc.cpp \
	WebOSPlatformPlugin.moc.cpp

LIB_OBJS := $(LIB_SOURCES:%.cpp=$(OBJDIR)/%.o)
APP_OBJS := $(APP_SOURCES:%.cpp=$(OBJDIR)/%.o)

SOURCES := $(LIB_SOURCES) $(APP_SOURCES)

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

qwebkitplatformplugin.moc.cpp: $(STAGING_INCDIR)/WebKitSupplemental/qwebkitplatformplugin.h
	$(MOC) -o $@ $<

WebOSPlatformPlugin.moc.cpp: $(STAGING_INCDIR)/WebKitSupplemental/WebOSPlatformPlugin.h
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

.PHONY: install
install:
	mkdir -p $(INSTALL_DIR)/usr/bin
	install -m 0775 $(BUILD_TYPE)-$(PLATFORM)/BrowserServer $(INSTALL_DIR)/usr/bin/BrowserServer

CodeGen/CodeGen: CodeGen/YapCodeGen.cpp CodeGen/BrowserYapCommandMessages.defs
	bash -c "pushd CodeGen && qmake && popd"
	make --directory=CodeGen

# Run this target after modifying BrowserYapCommandMessages.defs to generate the
# Yap code for both the browser-adapter and BrowserServer
.PHONY : code
code: CodeGen/CodeGen
	bash -c "pushd CodeGen && ./CodeGen client Browser BrowserYapCommandMessages.defs && popd"
	bash -c "pushd CodeGen && ./CodeGen server Browser BrowserYapCommandMessages.defs && popd"
