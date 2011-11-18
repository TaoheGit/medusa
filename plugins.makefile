# COPYRIGHT_CHUNFENG
include config.mk
PROJ_DIR = $(shell pwd)
include libs.mk
CC ?= gcc
CFLAGS ?=
LDFLAGS ?=
OS ?= LINUX32
VER = $(shell cat VERSION)

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

#####################################
TARGET = plug_cmd.so
OBJS = mds_cmd.o
CFLAGS += $(CFLAGS_LIBCHUNFENG) -Wno-unused-label
#####################################

all:$(TARGET)

%.so:%.o $(OBJS)
	$(CC) -shared $(LDFLAGS) $(LDFLAGS_LIBCHUNFENG) -o $@ $^

install:
	mkdir -p $(INSTALL_DIR)/usr/lib/medusa
	cp -af *.so $(INSTALL_DIR)/usr/lib/medusa

	
uninstall:
	rm -rf $(INSTALL_DIR)/usr/lib/medusa

	
clean:
	rm -rf test $(TARGET) *.o

	
.PHONY : all clean install uninstall

