# Awase, the client.
#
#   make                  on Renku OS
#   make HAIKU_CROSS=1    on Linux, for Renku x86_64: first run
#                         tools/make-sysroot.sh and source .sysroot-x86_64/env.sh
#   make test             the core's unit tests, with the host compiler
#
# Copyright 2026, Ian Dumych, idumych@gmail.com.
# SPDX-License-Identifier: MIT

NAME := Awase
CORE := core/Pool.cpp core/DemoPool.cpp core/Json.cpp core/Sha256.cpp \
	core/Portal.cpp core/Builder.cpp core/LivePool.cpp
APP := app/App.cpp app/ConnectWindow.cpp app/Ink.cpp app/LimitsView.cpp \
	app/MainWindow.cpp \
	app/Panels.cpp app/PoolView.cpp app/Settings.cpp

ifeq ($(HAIKU_CROSS),1)
  CXX := $(HAIKU_CXX) --sysroot=$(HAIKU_SYSROOT)
  RC := $(HAIKU_TOOLS)/rc/rc
  XRES := $(HAIKU_TOOLS)/xres
  MIMESET := true
  TARGET_OK := 1
  # libcurl's own libraries (ssl, zlib, ...) are on the Renku image, not here
  CROSS_LDFLAGS := -Wl,--allow-shlib-undefined
else ifeq ($(shell uname -s),Haiku)
  RC := rc
  XRES := xres
  MIMESET := mimeset -f
  TARGET_OK := 1
endif

OBJ := objects/renku
override CXXFLAGS += -O2 -Wall -Wextra -Wno-unused-parameter -Icore -Iapp
OBJECTS := $(patsubst %.cpp,$(OBJ)/%.o,$(CORE) $(APP))

all: $(NAME)

ifeq ($(TARGET_OK),1)
$(NAME): $(OBJECTS) $(OBJ)/$(NAME).rsrc
	$(CXX) -o $@ $(OBJECTS) -lbe -lcurl $(CROSS_LDFLAGS)
	$(XRES) -o $@ $(OBJ)/$(NAME).rsrc
	$(MIMESET) $@
else
$(NAME):
	@echo "Build Awase on Renku OS, or on Linux with HAIKU_CROSS=1 (see the top of this Makefile)."
	@false
endif

$(OBJ)/%.o: %.cpp $(wildcard core/*.h app/*.h)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJ)/icon.rdef: tools/make-icon.py
	@mkdir -p $(OBJ)
	python3 tools/make-icon.py $@

$(OBJ)/$(NAME).rsrc: $(NAME).rdef $(OBJ)/icon.rdef
	cat $(NAME).rdef $(OBJ)/icon.rdef > $(OBJ)/all.rdef
	$(RC) -o $@ $(OBJ)/all.rdef

HOST_CORE := core/Pool.cpp core/DemoPool.cpp
LIVE_CORE := $(HOST_CORE) core/Json.cpp core/Sha256.cpp core/Portal.cpp \
	core/Builder.cpp core/LivePool.cpp

test: objects/core_test objects/live_test
	./objects/core_test
	sh tests/run-live-test.sh

objects/core_test: tests/core_test.cpp $(HOST_CORE) $(wildcard core/*.h)
	@mkdir -p objects
	c++ -std=c++11 -O1 -Wall -Wextra -Icore -o $@ tests/core_test.cpp $(HOST_CORE)

objects/live_test: tests/live_test.cpp $(LIVE_CORE) $(wildcard core/*.h)
	@mkdir -p objects
	c++ -std=c++17 -O1 -Wall -Wextra -Wno-unused-parameter -pthread -Icore \
		-o $@ tests/live_test.cpp $(LIVE_CORE) -lcurl

clean:
	rm -rf objects $(NAME)

.PHONY: all test clean
