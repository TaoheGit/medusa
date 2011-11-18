# COPYRIGHT_CHUNFENG
include config.mk
PROJ_DIR = $(shell pwd)
include libs.mk
CC ?= gcc
CFLAGS ?=
LDFLAGS ?=
OS ?= LINUX32
VER = $(shell cat VERSION)
TARGET = mdsctl

CFLAGS += -I./include -Wall
ifeq ($(OS),WIN32)
CFLAGS += -D_WIN32_
else
endif

ifeq ($(DEBUG),y)
CFLAGS += -g
LDFLAGS += -g
else
CFLAGS += -s
LDFLAGS += -s -O2
endif

################################

OBJS = mdsctl.o mds_cmd.o
CFLAGS += $(CFLAGS_LIBCHUNFENG) -Wno-unused-label

################################

all:$(TARGET)

$(TARGET):$(TARGET).$(VER)
ifeq ($(OS),WIN32)
	mv $^ $@
else
	ln -sf $^ $@
endif

$(TARGET).$(VER):$(OBJS)
	$(CC) $(LDFLAGS) $(LDFLAGS_LIBCHUNFENG) -o $@ $^

install:
	mkdir -p $(INSTALL_DIR)/usr/bin
	cp -af ${TARGET} ${TARGET}.${VER} $(INSTALL_DIR)/usr/bin

	
uninstall:
	rm -rf $(INSTALL_DIR)/usr/bin/${TARGET} $(INSTALL_DIR)/usr/bin/${TARGET}.$(VER)

	
clean:
	rm -rf test $(TARGET)  $(TARGET).${VER} *.o

	
.PHONY : all clean install uninstall

