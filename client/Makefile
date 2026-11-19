# Awase, the client: the app for Renku OS (and Haiku), VitruvianOS and Linux
# (Cosmoe), and awase-tui, the terminal client.
#
#   make                  the app, for whichever platform builds it:
#                           Haiku/Renku natively;
#                           HAIKU_CROSS=1 on Linux for Renku arm64 (. the env.sh of
#                             tools/haiku-arm64/make-sysroot.sh) or x86_64 (. the env.sh
#                             of tools/make-sysroot.sh);
#                           VitruvianOS (pkg-config vos, or tools/vos-sdk's env.sh);
#                           Linux with Cosmoe under COSMOE_ROOT.
#   make test             the core's unit tests, with the host compiler
#   make tui              awase-tui, the terminal client, for Linux
#
# Contract the packaging scripts rely on (beos-app-platforms): the app at ./$(NAME),
# its compiled resources at obj/$(NAME).rsrc (obj/$(NAME).rsrc.o on Cosmoe), and
# CXXFLAGS/LDFLAGS from the command line added to ours.
#
# Copyright 2026, Ian Dumych, idumych@gmail.com.
# SPDX-License-Identifier: MIT

NAME := Awase
RDEF := $(NAME).rdef
CXX ?= g++
CXXFLAGS ?= -O2
override CXXFLAGS += -std=c++17 -Wall -Wextra -Wno-unused-parameter \
	-Wno-multichar -Wno-deprecated-copy -Icore -Iapp
UNAME := $(shell uname -s)
COSMOE_ROOT ?= /usr
ifeq ($(HAIKU_CROSS)$(filter Haiku,$(UNAME)),)
VOS := $(shell pkg-config --exists vos 2>/dev/null && echo 1)
endif

# The x86 build VM (QEMU with NVMM): the app only, not awase-tui
VM_CORE := core/Download.cpp core/VmHost.cpp core/VmRunner.cpp core/VmSetup.cpp \
	core/VmService.cpp
CORE := core/Pool.cpp core/DemoPool.cpp core/Json.cpp core/Sha256.cpp \
	core/Portal.cpp core/Builder.cpp core/LivePool.cpp core/Thermal.cpp \
	$(VM_CORE)
APP := app/App.cpp app/ConnectWindow.cpp app/Ink.cpp app/LimitsView.cpp \
	app/MainWindow.cpp \
	app/Panels.cpp app/PoolView.cpp app/Settings.cpp app/VmWindow.cpp
# Beyond the Haiku API, on every platform: libcurl for the coordinator's API
APP_LIBS := -lcurl

ifneq ($(HAIKU_CROSS),)
  PLATFORM := haiku
  ifneq ($(HAIKU_ARM64),)
    # Renku arm64: tools/haiku-arm64/make-sysroot.sh's env.sh sets CXX and the flags
    # The lite build: arm64 has no NVMM, so no x86 VM (AWASE_NO_VM)
    override CXXFLAGS += $(HAIKU_CROSS_CFLAGS) -DAWASE_NO_VM
    override LDFLAGS += $(HAIKU_CROSS_LDFLAGS)
    RC := rc
    XRES := xres
  else
    # Renku x86_64 from a Renku build tree: tools/make-sysroot.sh's env.sh
    CXX := $(HAIKU_CXX) --sysroot=$(HAIKU_SYSROOT)
    RC := $(HAIKU_TOOLS)/rc/rc
    XRES := $(HAIKU_TOOLS)/xres
  endif
  # libcurl's own libraries (ssl, zlib, ...) are on the system, not in the sysroot
  override LDFLAGS += -Wl,--allow-shlib-undefined
  LIBS := -lbe -lnetwork $(APP_LIBS)
  MIMESET := true
  RESOURCES = $(XRES) -o $@ obj/$(NAME).rsrc
else ifeq ($(UNAME),Haiku)
  PLATFORM := haiku
  RC := rc
  LIBS := -lbe -lnetwork $(APP_LIBS)
  MIMESET := mimeset -f
  RESOURCES = xres -o $@ obj/$(NAME).rsrc
else ifneq ($(VOS),)
  # VitruvianOS: the Haiku API on Linux. Its resource tools want absolute paths, and it
  # keeps the signature as attributes too (build-deb.sh ships them, as dpkg drops them).
  PLATFORM := vos
  RC := rc
  override CXXFLAGS += $(shell pkg-config --cflags vos) -DAPP_VOS -fPIC \
	-Wno-deprecated-copy
  LIBS := $(shell pkg-config --libs vos) $(APP_LIBS)
  MIMESET := true
  RESOURCES = xres -o $(CURDIR)/$@ $(CURDIR)/obj/$(NAME).rsrc \
	&& resattr -O -o $(CURDIR)/$@ $(CURDIR)/obj/$(NAME).rsrc
else ifneq ($(wildcard $(COSMOE_ROOT)/include/os/app/Application.h),)
  # Plain Linux with Cosmoe (the Haiku API as a library on X11/Wayland)
  PLATFORM := cosmoe
  COSMOE_DIRS := config os os/add-ons/graphics os/app os/game os/interface \
	os/kernel os/locale os/media os/net os/storage os/support os/translation \
	os/be_apps/Tracker private/interface private/shared private/support posix
  override CXXFLAGS += -I$(COSMOE_ROOT)/include \
	$(addprefix -I$(COSMOE_ROOT)/include/,$(COSMOE_DIRS)) -DAPP_COSMOE -fPIC
  # libbe finds Cosmoe's add-ons and data next to itself, so the AppImage works
  # wherever it is mounted
  COSMOE_RPATH ?= $$ORIGIN/../lib
  RC := LD_LIBRARY_PATH=$(COSMOE_ROOT)/lib $(COSMOE_ROOT)/bin/rc
  LIBS := -L$(COSMOE_ROOT)/lib -lbe $(APP_LIBS) -Wl,-rpath,'$(COSMOE_RPATH)'
  MIMESET := true
  # Cosmoe reads resources from a .cosmoe_rsrc section linked into the app
  EXTRA_OBJS := obj/$(NAME).rsrc.o
  RESOURCES = true
else
  PLATFORM := none
endif

OBJ := obj/$(PLATFORM)
OBJECTS := $(patsubst %.cpp,$(OBJ)/%.o,$(CORE) $(APP))

all: $(NAME)

ifeq ($(PLATFORM),none)
$(NAME):
	@echo "Awase needs Renku OS or Haiku, HAIKU_CROSS=1 with a sysroot, VitruvianOS,"
	@echo "or Linux with Cosmoe under COSMOE_ROOT=$(COSMOE_ROOT) (see the top of this Makefile)."
	@false
else
$(NAME): $(OBJECTS) $(EXTRA_OBJS) obj/$(NAME).rsrc
	$(CXX) $(LDFLAGS) -o $@ $(OBJECTS) $(EXTRA_OBJS) $(LIBS)
	$(RESOURCES)
	$(MIMESET) $@
endif

$(OBJ)/%.o: %.cpp $(wildcard core/*.h app/*.h)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

obj/icon.rdef: tools/make-icon.py
	@mkdir -p obj
	python3 tools/make-icon.py $@

obj/$(NAME).rsrc: $(RDEF) obj/icon.rdef
	cat $(RDEF) obj/icon.rdef > obj/all.rdef
	$(RC) -o $(CURDIR)/$@ $(CURDIR)/obj/all.rdef

obj/$(NAME).rsrc.o: obj/$(NAME).rsrc
	cd obj && ld -r -b binary $(NAME).rsrc -o $(NAME).rsrc.o
	objcopy --rename-section .data=.cosmoe_rsrc \
		--add-section .note.GNU-stack=/dev/null \
		--set-section-flags .note.GNU-stack=noload,readonly $@

hpkg:
	sh packaging/build-hpkg.sh

HOST_CORE := core/Pool.cpp core/DemoPool.cpp
# socket() is in libnetwork on Haiku, in libc elsewhere
HOST_NETWORK := $(if $(filter Haiku,$(UNAME)),-lnetwork)
LIVE_CORE := $(HOST_CORE) core/Json.cpp core/Sha256.cpp core/Portal.cpp \
	core/Builder.cpp core/LivePool.cpp core/Thermal.cpp

test: objects/core_test objects/live_test objects/vm_test
	./objects/core_test
	sh tests/run-live-test.sh
	sh tests/run-vm-test.sh

objects/core_test: tests/core_test.cpp $(HOST_CORE) $(wildcard core/*.h)
	@mkdir -p objects
	c++ -std=c++11 -O1 -Wall -Wextra -Icore -o $@ tests/core_test.cpp $(HOST_CORE)

objects/live_test: tests/live_test.cpp $(LIVE_CORE) $(wildcard core/*.h)
	@mkdir -p objects
	c++ -std=c++17 -O1 -Wall -Wextra -Wno-unused-parameter -pthread -Icore \
		-o $@ tests/live_test.cpp $(LIVE_CORE) -lcurl

objects/vm_test: tests/vm_test.cpp $(LIVE_CORE) $(VM_CORE) $(wildcard core/*.h)
	@mkdir -p objects
	c++ -std=c++17 -O1 -Wall -Wextra -Wno-unused-parameter -pthread -Icore \
		-o $@ tests/vm_test.cpp $(LIVE_CORE) $(VM_CORE) -lcurl $(HOST_NETWORK)

# The terminal client for Linux servers: awase-tui
tui: awase-tui

awase-tui: tui/main.cpp $(LIVE_CORE) $(wildcard core/*.h)
	c++ -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter -pthread -Icore \
		-o $@ tui/main.cpp $(LIVE_CORE) -lcurl -lncursesw

clean:
	rm -rf obj objects $(NAME) awase-tui

.PHONY: all hpkg test tui clean
