# Optimized Haiku Build Script
SHELL := /bin/bash
PACKAGE_DIR := build/package
NAME = HaikuSuperMusicThingy
VERSION = 1.0.0

UNAME_M := $(shell uname -p)
ifeq ($(UNAME_M), x86)
CXX = g++-x86 -DENABLE_PROJECTM=OFF -DENABLE_SDL2=OFF -DENABLE_GL=OFF -DUSE_KONSOLE_ON_HAIKU=OFF
CC = gcc-x86
MAKE := setarch x86 $(MAKE)
ARCH = x86_gcc2
SIMD_FLAGS := -O2
INCLUDE = -L/boot/system/lib/x86 
else ifeq ($(UNAME_M), x86_64)
CXX = g++ -DENABLE_PROJECTM=OFF -DENABLE_SDL2=OFF -DENABLE_GL=OFF -DUSE_KONSOLE_ON_HAIKU=OFF
CC = gcc
ARCH = x86_64
SIMD_FLAGS := -O3
INCLUDE = -L/boot/system/lib
endif


BUILD_FLAGS = $(SIMD_FLAGS) 
LD_OPTIMIZE = -Wl,--gc-sections


EXTRA_LIBS = -lmpv -lcurl -lopenal
HAIKU_LIBS = -lnetwork -lroot -lpthread  -lbe -lmedia -ltranslation 


.PHONY: build package clean

all: build

release: build package

build: 
	@echo "--------- Building $(NAME) $(ARCH) ---------"
	$(CXX) $(BUILD_FLAGS) $(INCLUDE) haiku-supermusicthingy.cpp  -o $(NAME) $(LD_OPTIMIZE) $(EXTRA_LIBS) $(HAIKU_LIBS) 

package: all
	@[ -n "$(PACKAGE_DIR)" ] || { echo "PACKAGE_DIR is undefined"; exit 1; }
	rm -rf "./$(PACKAGE_DIR)"
	mkdir -p $(PACKAGE_DIR)
	sed -e 's/$$(NAME)/$(NAME)/g' -e 's/$$(VERSION)/$(VERSION)/g' -e 's/$$(ARCH)/$(ARCH)/' -e 's/$$(YEAR)/$(shell date +%Y)/' $(NAME).tpl > $(PACKAGE_DIR)/.PackageInfo
	mkdir -p $(PACKAGE_DIR)/apps
	mkdir -p $(PACKAGE_DIR)/bin
	mkdir -p $(PACKAGE_DIR)/data/deskbar/menu/Applications
	rc -o $(NAME).rsrc $(NAME).rdef 
	xres -o $(NAME) $(NAME).rsrc  
	mimeset -f $(NAME)
	cp $(NAME) $(PACKAGE_DIR)/apps/$(NAME)
	ln -s ../apps/$(NAME) $(PACKAGE_DIR)/bin/$(NAME)
	ln -s ../../../../apps/$(NAME) $(PACKAGE_DIR)/data/deskbar/menu/Applications/$(NAME)
	package create -C $(PACKAGE_DIR) $(NAME)-$(VERSION)-1-$(ARCH).hpkg


clean:
	rm -f $(NAME)
	rm -f $(NAME).rsrc
	rm -fr build*
	rm -r *.hpkg



