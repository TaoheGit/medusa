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
CFLAGS += -s -O2
LDFLAGS += -s -O2
endif

###################################################
TARGET = plug_cmd.so plug_v4l2.so plug_rtp_sink.so plug_file_sink.so 
TARGET += plug_raw_img_soft_conv.so plug_xmpp.so plug_gpio.so
TARGET += plug_input.so
OBJS = 
CFLAGS += $(CFLAGS_LIBCHUNFENG) -Wno-unused-label


ifeq ($(SUPPORT_DAVINCI_VPFE), yes)
CFLAGS += -D_DAVINCI_VPFE_
endif

ifeq ($(SUPPORT_DV_DMAI), yes)
TARGET += plug_dv_resizer.so plug_dv_previewer.so plug_dv_encoder.so
ifeq ($(CONFIG_DM365_IPIPE), yes)
DV_DMAI_CFLAGS += -DCONFIG_DM365_IPIPE
endif
endif

ifeq ($(ENABLE_XMPP), yes)
LDFLAGS += $(LDFLAGS_IKSEMEL) $(LDFLAGS_GNUTLS) $(LDFLAGS_GMP)
endif
CXXFLAGS = $(CFLAGS)
###################################################
plug_dv_resizer.so:plug_dv_resizer.o dv_plug_cfg.o
	$(CC) -shared -symbolic -lpthread $(LDFLAGS_LIBCHUNFENG)  $(LDFLAGS) -o $@ $^ $(DV_DMAI_LDFLAGS)

plug_dv_resizer.o:plug_dv_resizer.c
	$(CC) -c $(CFLAGS) $(DV_DMAI_CFLAGS) -o $@ $^

dv_plug_cfg.o:dv_plug_cfg.c
	$(CC) -c $(CFLAGS) $(DV_DMAI_CFLAGS) -o $@ $^

plug_dv_previewer.so:plug_dv_previewer.o dv_plug_cfg.o
	$(CC) -shared -symbolic -lpthread $(LDFLAGS_LIBCHUNFENG)  $(LDFLAGS) -o $@ $^ $(DV_DMAI_LDFLAGS)

plug_dv_previewer.o:plug_dv_previewer.c
	$(CC) -c $(CFLAGS) $(DV_DMAI_CFLAGS) -o $@ $^
    
plug_dv_encoder.so:plug_dv_encoder.o dv_plug_cfg.o
	$(CC) -shared -symbolic -lpthread $(LDFLAGS_LIBCHUNFENG)  $(LDFLAGS) -o $@ $^ $(DV_DMAI_LDFLAGS)

plug_dv_encoder.o:plug_dv_encoder.c
	$(CC) -c $(CFLAGS) $(DV_DMAI_CFLAGS) -o $@ $^
###################################################
all:$(TARGET)

%.so:%.o $(OBJS)
	$(CC) -shared $(LDFLAGS) $(LDFLAGS_LIBCHUNFENG) -o $@ $^

install:
	mkdir -p $(prefix)/lib/medusa
	cp -af *.so $(prefix)/lib/medusa
	
uninstall:
	rm -rf $(prefix)/lib/medusa

	
clean:
	rm -rf test $(TARGET) *.o

	
.PHONY : all clean install uninstall

