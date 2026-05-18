# Optimized Haiku Build Script
SHELL := /bin/bash
NAME = HaikuSuperMusicThingy
VERSION = 1.0.0
PACKAGE_DIR := build/package
DUMMY_PC_PATH := $(shell pwd)/build/pkgconfig


ENABLE_PROJECTM := OFF

# Forced dependencies
ifeq ($(ENABLE_PROJECTM), ON)
    ENABLE_SDL2 := ON
    ENABLE_GL := ON
endif

# --- 2. Architecture & Paths ---
UNAME_M := $(shell uname -p)
ifeq ($(UNAME_M), x86)
    CXX = g++-x86
    ARCH = x86_gcc2
    LIB_ARCH_DIR = /x86
    DEFINES += -DIS_HAIKU_32BIT
ifeq ($(ENABLE_PROJECTM), ON)
    TPL_FILE := $(NAME)_projectm_x86.tpl
else
    TPL_FILE := $(NAME)_x86.tpl
endif
else
    CXX = g++
    ARCH = x86_64
    LIB_ARCH_DIR = 
    DEFINES += -DUSE_SYSTRAY
ifeq ($(ENABLE_PROJECTM), ON)
    TPL_FILE := $(NAME)_projectm.tpl
else
    TPL_FILE := $(NAME).tpl
endif  
endif

# Set up Pkg-Config Environment
export PKG_CONFIG_PATH := $(DUMMY_PC_PATH):/boot/home/config/non-packaged/lib$(LIB_ARCH_DIR)/pkgconfig:/boot/system/develop/lib$(LIB_ARCH_DIR)/pkgconfig

# --- 3. Compiler & Linker Flags ---
CXXFLAGS = -std=c++17 -O3 -Wall -rdynamic  
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

package: all
	@[ -n "$(PACKAGE_DIR)" ] || { echo "PACKAGE_DIR is undefined"; exit 1; }
	rm -rf "./$(PACKAGE_DIR)"
	mkdir -p $(PACKAGE_DIR)
	sed -e 's/$$(NAME)/$(NAME)/g' -e 's/$$(VERSION)/$(VERSION)/g' -e 's/$$(ARCH)/$(ARCH)/' -e 's/$$(YEAR)/$(shell date +%Y)/' tpl/$(TPL_FILE) > $(PACKAGE_DIR)/.PackageInfo
	mkdir -p $(PACKAGE_DIR)/apps
	mkdir -p $(PACKAGE_DIR)/bin
ifeq ($(ENABLE_PROJECTM), ON)
 ifeq ($(ARCH), x86_64)
	mkdir -p $(PACKAGE_DIR)/lib
	cp lib/lib* $(PACKAGE_DIR)/lib
 endif
endif
	mkdir -p $(PACKAGE_DIR)/data/deskbar/menu/Applications
ifeq ($(UNAME_M), x86_64)
	rc -o $(NAME).rsrc $(NAME).rdef
endif
ifeq ($(UNAME_M), x86)	
	rc -o $(NAME).rsrc $(NAME)_x86.rdef
endif  
	xres -o $(NAME) $(NAME).rsrc
	mimeset -f $(NAME)
	cp $(NAME) $(PACKAGE_DIR)/apps/
	ln -s /system/apps/$(NAME) $(PACKAGE_DIR)/bin/$(NAME)
	ln -s /system/apps/$(NAME) $(PACKAGE_DIR)/data/deskbar/menu/Applications/$(NAME)
	package create -C $(PACKAGE_DIR) $(NAME)-$(VERSION)-1-$(ARCH).hpkg

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
	@echo " Non projectm builds..."
	@echo " 1. make release"
	@echo ""
	@echo " projectm builds..."	
	@echo " 2. make release ENABLE_PROJECTM=ON"
	@echo ""
	@echo "============================================================================"	
