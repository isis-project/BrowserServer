# Device makefile
BUILD_TYPE := release
PLATFORM   ?= arm

#GFX_BACKEND := "piranha"

MOC := ${STAGING_BINDIR_NATIVE}/moc-palm

INCLUDES := \
	-isystem $(INCLUDE_DIR)/PmCertificateMgr/IncsPublic \
	-isystem $(INCLUDE_DIR)/webkit

LIBS := -L$(LIB_DIR) -ljemalloc_mt -lpthread

ifeq ($(GFX_BACKEND), "piranha")
	INCLUDES += \
		-isystem $(INCLUDE_DIR)/Piranha \
		-isystem $(INCLUDE_DIR)/Piranha/Backend \
		-isystem $(INCLUDE_DIR)/Piranha/Font \
		-isystem $(INCLUDE_DIR)/Piranha/Utils/lfp

	LIBS += -lPiranha
else
	INCLUDES += \
		-isystem $(INCLUDE_DIR)/Graphics \
		-isystem $(INCLUDE_DIR)/Graphics/MiniGraphics \
		-isystem $(INCLUDE_DIR)/Graphics/Transform \
		-isystem $(INCLUDE_DIR)/Graphics/Utility

	LIBS += -lMiniGraphics -lGraphicUtilities
endif

INCLUDES := $(INCLUDES) \
	-I./Src -I./Yap -I./Offscreen \
	`pkg-config --cflags glib-2.0`

LIBS := -L$(LIB_DIR) \
	-lpthread \
	-lglib-2.0 \
	-llunaservice \
	-lcjson \
	-lpbnjson_c \
	-lpbnjson_cpp \
	-lyajl \
	-luriparser \
	-lPmCertificateMgr \
	-laffinity

ifeq ($(TARGET_ARCH),arm)
LIBS := $(LIBS) -lmemchute -ljemalloc_mt -lpthread -lGLESv2 -lEGL
else ifeq ($(MACHINE),qemux86)
LIBS := $(LIBS) -lmemchute -ljemalloc_mt -lpthread
else
LIBS := $(LIBS) -ltcmalloc -lGLESv2 -lEGL
endif

include Makefile.inc

install:
	mkdir -p $(INSTALL_DIR)/usr/bin
	install -m 0775 $(BUILD_TYPE)-$(PLATFORM)/BrowserServer $(INSTALL_DIR)/usr/bin/BrowserServer
