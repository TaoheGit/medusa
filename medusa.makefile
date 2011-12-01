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
CFLAGS += -g
LDFLAGS += -g
else
CFLAGS += -s
LDFLAGS += -s -O2
endif

################################

OBJS = medusa.o mds_plugin.o mds_media.o
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
	$(CC) -rdynamic $(LDFLAGS) $(LDFLAGS_LIBCHUNFENG) -ldl -o $@ $^

install:
	mkdir -p $(prefix)/bin
	cp -af ${TARGET} ${TARGET}.${VER} $(prefix)/bin
	install -D examples/medusa.conf $(prefix)/etc/medusa.conf
	
uninstall:
	rm -rf $(prefix)/bin/${TARGET} $(prefix)/bin/${TARGET}.$(VER)

clean:
	rm -rf test $(TARGET)  $(TARGET).${VER} *.o
	
.PHONY : all clean install uninstall

