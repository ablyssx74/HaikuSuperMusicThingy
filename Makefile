# Optimized Haiku Build Script
SHELL := /bin/bash
NAME = HaikuSuperMusicThingy
VERSION = 1.0.9
PACKAGE_DIR := build/package
DUMMY_PC_PATH := $(shell pwd)/build/pkgconfig
ENABLE_PROJECTM := ON
REVISION = 1

# Forced dependencies
ifeq ($(ENABLE_PROJECTM), ON)
    ENABLE_SDL2 := ON
    ENABLE_GL := ON
endif

# --- 2. Architecture & Paths ---
UNAME_M := $(shell uname -m)
ifeq ($(UNAME_M), BePC)
    CXX = g++-x86
    TRAY_CXX = g++
    ARCH = x86_gcc2
    LIB_ARCH_DIR = /x86
    is32bit = _x86
    ENABLE_PROJECTM := OFF
    ENABLE_SDL2 := OFF
    ENABLE_GL := OFF
    DEFINES += -DIS_HAIKU_32BIT -DUSE_SYSTRAY 
    TPL_FILE := $(NAME).tpl 
else
    CXX = g++
    TRAY_CXX = g++
    ARCH = x86_64
    LIB_ARCH_DIR = 
    DEFINES += -DUSE_SYSTRAY 
    TPL_FILE := $(NAME).tpl 
endif

# Set up Pkg-Config Environment
export PKG_CONFIG_PATH := $(DUMMY_PC_PATH):/boot/home/config/non-packaged/lib/pkgconfig:/boot/home/config/non-packaged/lib$(LIB_ARCH_DIR)/pkgconfig:/boot/system/develop/lib$(LIB_ARCH_DIR)/pkgconfig

# --- 3. Compiler & Linker Flags ---
CXXFLAGS = -std=c++17 -O3 -Wall 
DEFINES := $(DEFINES)
INCLUDES = -I/boot/home/config/non-packaged/include -I/boot/system/develop/headers
LIB_PATH = -L/boot/system/lib$(LIB_ARCH_DIR) -L/boot/system/develop/lib$(LIB_ARCH_DIR) -L/boot/home/config/non-packaged/lib$(LIB_ARCH_DIR)

# Core Libraries (find_library openal)
EXTRA_LIBS = -lopenal $(shell pkg-config --libs mpv libcurl)
HAIKU_LIBS = -lnetwork -lroot -lpthread -lbe -lmedia -ltranslation

# Feature-specific Logic
ifeq ($(ENABLE_SDL2), ON)
    DEFINES += -DUSE_SDL2
    EXTRA_LIBS += $(shell pkg-config --libs sdl2)
endif

ifeq ($(ENABLE_GL), ON)
    DEFINES += -DUSE_GL
    # Link against libGL (satisfied by the dummy .pc or system)
    EXTRA_LIBS += -lGL
endif

ifeq ($(ENABLE_PROJECTM), ON)
    DEFINES += -DUSE_PROJECTM
    # Use pkg-config for projectM-4
 ifeq ($(UNAME_M), x86)
 	PKG_CONFIG_CMD = x86-pkg-config
 else
    PKG_CONFIG_CMD = pkg-config
 endif
    EXTRA_LIBS += $(shell pkg-config --libs projectM-4)
    CXXFLAGS += $(shell pkg-config --cflags projectM-4)
endif

# --- 4. Build Targets ---
.PHONY: build package release clean help setup_dummy

all: build

release: package

setup_dummy:
	@mkdir -p $(DUMMY_PC_PATH)
	@echo "Name: opengl" > $(DUMMY_PC_PATH)/opengl.pc
	@echo "Description: Dummy OpenGL for Haiku" >> $(DUMMY_PC_PATH)/opengl.pc
	@echo "Version: 1.0" >> $(DUMMY_PC_PATH)/opengl.pc
	@echo "Libs: -L/boot/system/lib$(LIB_ARCH_DIR) -lGL" >> $(DUMMY_PC_PATH)/opengl.pc
	@echo "Cflags: -I/boot/system/develop/headers" >> $(DUMMY_PC_PATH)/opengl.pc

build: setup_dummy
	@echo "--------- Building $(NAME) $(ARCH) ---------"
	$(CXX) $(CXXFLAGS) $(DEFINES) $(INCLUDES) haiku-supermusicthingy.cpp icons.cpp -o $(NAME) \
		$(LIB_PATH) $(EXTRA_LIBS) $(HAIKU_LIBS)

TRAY_LIB_NAME = SuperMusicTrayIconLibrary
TRAY_LIB_SRC  = x86/deskbar-tray-x86.cpp
TRAY_LIB_RDEF = x86/$(TRAY_LIB_NAME)_x86.rdef

build_tray_lib:
	@echo "--------- Building legacy gcc2 Deskbar tray helper ---------"
	$(TRAY_CXX) -O2 -Wall -Wno-multichar $(TRAY_LIB_SRC) -o $(TRAY_LIB_NAME) \
		-I/boot/system/develop/headers \
		-L/boot/system/lib -lbe -lroot
	rc -o $(TRAY_LIB_NAME).rsrc $(TRAY_LIB_RDEF)
	xres -o $(TRAY_LIB_NAME) $(TRAY_LIB_NAME).rsrc
	mimeset -f $(TRAY_LIB_NAME)


package: all
	@[ -n "$(PACKAGE_DIR)" ] || { echo "PACKAGE_DIR is undefined"; exit 1; }
	rm -rf "./$(PACKAGE_DIR)"
	mkdir -p $(PACKAGE_DIR)
	sed -e 's/$$(NAME)/$(NAME)/g' -e 's/$$(VERSION)/$(VERSION)/g' -e 's/$$(REVISION)/$(REVISION)/g' -e 's/$$(is32bit)/$(is32bit)/g'  -e 's/$$(ARCH)/$(ARCH)/' -e 's/$$(YEAR)/$(shell date +%Y)/' $(TPL_FILE) > $(PACKAGE_DIR)/.PackageInfo
	mkdir -p $(PACKAGE_DIR)/apps
	mkdir -p $(PACKAGE_DIR)/bin
ifeq ($(ENABLE_PROJECTM), ON)
	mkdir -p $(PACKAGE_DIR)/data/$(NAME)/milkdrops/presets_stock
	cp -r presets_stock/. $(PACKAGE_DIR)/data/$(NAME)/milkdrops/presets_stock/
ifeq ($(UNAME_M), BePC)
	mkdir -p $(PACKAGE_DIR)/lib/x86
	#cp x86/lib/lib* $(PACKAGE_DIR)/lib/x86
 else
	mkdir -p $(PACKAGE_DIR)/lib
	cp lib/lib* $(PACKAGE_DIR)/lib
 endif
endif
	mkdir -p $(PACKAGE_DIR)/data/deskbar/menu/Applications
	rc -o $(NAME).rsrc $(NAME).rdef  
ifeq ($(UNAME_M), BePC)
	$(MAKE) build_tray_lib
	mkdir -p $(PACKAGE_DIR)/apps
	cp $(TRAY_LIB_NAME) $(PACKAGE_DIR)/apps/
endif
	xres -o $(NAME) $(NAME).rsrc
	mimeset -f $(NAME)
	cp $(NAME) $(PACKAGE_DIR)/apps/
	ln -s /system/apps/$(NAME) $(PACKAGE_DIR)/bin/$(NAME)
	ln -s /system/apps/$(NAME) $(PACKAGE_DIR)/data/deskbar/menu/Applications/$(NAME)
	package create -C $(PACKAGE_DIR) $(NAME)-$(VERSION)-$(REVISION)-$(ARCH).hpkg

clean:
	rm -f $(NAME) *.rsrc *.hpkg
	rm -rf build
	
#----------------------------------------------------------	
# Help
#----------------------------------------------------------
help:
	@echo "============================================================================"
	@echo " Building $(NAME) for Haiku"
	@echo ""
	@echo " Default projectm builds...( Requires 3rd party projectm headers )"
	@echo " 1. make release"
	@echo ""
	@echo " Non projectm builds..."	
	@echo " 2. make release ENABLE_PROJECTM=OFF"
	@echo ""
	@echo "============================================================================"	
