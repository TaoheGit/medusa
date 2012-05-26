# COPYRIGHT_CHUNFENG
include config.mk
PROJ_DIR = $(shell pwd)
include libs.mk
CC ?= gcc
CFLAGS ?=
LDFLAGS ?=
OS ?= LINUX32
VER = $(shell cat VERSION)
TARGET = medusa

CFLAGS += -I./include -Wall
ifeq ($(OS),WIN32)
CFLAGS += -D_WIN32_
else
endif

ifeq ($(DEBUG),y)
CFLAGS += -g -O3
LDFLAGS += -g
else
CFLAGS += -s -O3
LDFLAGS += -s -O3
endif

################################

OBJS = medusa.o mds_plugin.o mds_media.o mds_tools.o
CFLAGS += -Wno-unused-label

LDFLAGS += $(LDFLAGS_LIBCHUNFENG)
################################

all:$(TARGET)

$(TARGET):$(TARGET).$(VER)
ifeq ($(OS),WIN32)
	mv $^ $@
else
	ln -sf $^ $@
endif

$(TARGET).$(VER):$(OBJS)
	$(CC) -rdynamic $(LDFLAGS) -ldl -o $@ $^

install:
	mkdir -p $(prefix)/bin
	cp -af ${TARGET} ${TARGET}.${VER} $(prefix)/bin
	
uninstall:
	rm -rf $(prefix)/bin/${TARGET} $(prefix)/bin/${TARGET}.$(VER)

clean:
	rm -rf test $(TARGET)  $(TARGET).${VER} *.o
	
.PHONY : all clean install uninstall

