ifneq ($(LIBS_INCLUDED), yes)
LIBS_INCLUDED=yes
#gtk+3.0
CFLAGS_GTK = $(shell pkg-config --cflags gtk+-3.0)
LDFLAGS_GTK = $(shell pkg-config --libs gtk+-3.0)

#nautilus
CFLAGS_NAUTILUS	= "-I/usr/include/nautilus"
LDFLAGS_NAUTILUS = ""

# libblkid
LDFLAGS_BLKID = -lblkid

# libikssemel
LDFLAGS_IKSEMEL = -liksemel

# libgnutls
LDFLAGS_GNUTLS = -lgnutls

#gmp
LDFLAGS_GMP = -lgmp

#nettle
LDFLAGS_NETTLE = -lnettle -lhogweed

#libchunfeng
LDFLAGS_LIBCHUNFENG = -lchunfeng

#libfcgi
LDFLAGS_FCGI = -lfcgi

# TI davinci dmai
DV_DMAI_CFLAGS = -march=armv5t \
-I"$(DV_CE_DIR)/examples" \
-I"$(DV_DMAI_DIR)/packages" \
-I"$(DV_CE_DIR)/packages" \
-I"$(DV_FC_DIR)/packages" \
-I"$(DV_XDAIS_DIR)/packages" \
-I"$(DV_LINUX_UTILS_DIR)/packages" \
-I"$(DV_CODECS_DM365_DIR)/packages" -I"/packages" -I"/soc/packages" \
-I"$(DV_XDC_TOOLS_DIR)/packages" \
-Dxdc_target_types__="gnu/targets/arm/std.h" -Dxdc_target_name__=GCArmv5T

DV_DMAI_LDFLAGS = \
    $(DV_DMAI_DIR)/packages/ti/sdo/dmai/lib/dmai_linux_dm365.a470MV \
    $(DV_CE_DIR)/packages/ti/sdo/ce/image1/lib/release/imgdec1.av5T \
    $(DV_CE_DIR)/packages/ti/sdo/ce/image1/lib/release/imgenc1.av5T \
    $(DV_CE_DIR)/packages/ti/sdo/ce/image/lib/release/image.av5T \
    $(DV_CE_DIR)/packages/ti/sdo/ce/video2/lib/release/viddec2.av5T \
    $(DV_CE_DIR)/packages/ti/sdo/ce/video1/lib/release/viddec1.av5T \
    $(DV_CE_DIR)/packages/ti/sdo/ce/video1/lib/release/videnc1.av5T \
    $(DV_CE_DIR)/packages/ti/sdo/ce/video/lib/release/video.av5T \
    $(DV_CE_DIR)/packages/ti/sdo/ce/audio1/lib/release/auddec1.av5T \
    $(DV_CE_DIR)/packages/ti/sdo/ce/audio1/lib/release/audenc1.av5T \
    $(DV_CE_DIR)/packages/ti/sdo/ce/audio/lib/release/audio.av5T \
    $(DV_CE_DIR)/packages/ti/sdo/ce/speech1/lib/release/sphdec1.av5T \
    $(DV_CE_DIR)/packages/ti/sdo/ce/speech1/lib/release/sphenc1.av5T \
    $(DV_CE_DIR)/packages/ti/sdo/ce/speech/lib/release/speech.av5T \
    $(DV_CE_DIR)/packages/ti/sdo/ce/lib/release/ce.av5T \
    $(DV_CE_DIR)/packages/ti/sdo/ce/alg/lib/release/Algorithm_noOS.av5T \
    $(DV_CE_DIR)/packages/ti/sdo/ce/alg/lib/release/alg.av5T \
    $(DV_CE_DIR)/packages/ti/sdo/ce/ipc/linux/lib/release/ipc_linux.av5T \
    $(DV_CE_DIR)/packages/ti/sdo/ce/osal/linux/lib/release/osal_linux_470.av5T \
    $(DV_FC_DIR)/packages/ti/sdo/fc/acpy3/lib/release/acpy3.a470MV \
    $(DV_FC_DIR)/packages/ti/sdo/fc/dman3/lib/release/dman3Cfg.a470MV \
    $(DV_CE_DIR)/packages/ti/sdo/ce/node/lib/release/node.av5T \
    $(DV_CE_DIR)/packages/ti/sdo/ce/utils/xdm/lib/release/XdmUtils.av5T \
    $(DV_CODECS_DM365_DIR)/packages/ti/sdo/codecs/mpeg4enc/lib/libmp4enc.a \
    $(DV_FC_DIR)/packages/ti/sdo/fc/vicpsync/lib/release/vicpsync.av5T \
    $(DV_FC_DIR)/packages/ti/sdo/fc/ires/vicp/lib/release/vicp.av5T \
    $(DV_FC_DIR)/packages/ti/sdo/fc/ires/grouputils/lib/release/grouputils.av5T \
    $(DV_FC_DIR)/packages/ti/sdo/fc/ires/edma3chan/lib/release/edma3Chan.av5T \
    $(DV_FC_DIR)/packages/ti/sdo/fc/edma3/lib/release/edma3.av5T \
    $(DV_FC_DIR)/packages/ti/sdo/fc/rman/lib/release/rman.av5T \
    $(DV_FC_DIR)/packages/ti/sdo/fc/ires/nullresource/lib/release/nullres.av5T \
    $(DV_CODECS_DM365_DIR)/packages/ti/sdo/codecs/mpeg2enc/lib/mpeg2venc_ti_arm926.a \
    $(DV_CODECS_DM365_DIR)/packages/ti/sdo/codecs/mpeg2enc/lib/dma_ti_dm365.a \
    $(DV_CODECS_DM365_DIR)/packages/ti/sdo/codecs/mpeg4enc_hdvicp/lib/mpeg4venc_ti_arm926.a \
    $(DV_CODECS_DM365_DIR)/packages/ti/sdo/codecs/mpeg4enc_hdvicp/lib/dma_ti_dm365.a \
    $(DV_CODECS_DM365_DIR)/packages/ti/sdo/codecs/h264enc/lib/h264venc_ti_arm926.a \
    $(DV_CODECS_DM365_DIR)/packages/ti/sdo/codecs/h264enc/lib/h264v_ti_dma_dm365.a \
    $(DV_FC_DIR)/packages/ti/sdo/fc/hdvicpsync/lib/release/hdvicpsync.av5T \
    $(DV_FC_DIR)/packages/ti/sdo/utils/trace/lib/release/gt.av5T \
    $(DV_FC_DIR)/packages/ti/sdo/fc/ires/hdvicp/lib/release/hdvicp.av5T \
    $(DV_LINUX_UTILS_DIR)/packages/ti/sdo/linuxutils/vicp/lib/vicp.a470MV \
    $(DV_FC_DIR)/packages/ti/sdo/fc/ires/memtcm/lib/release/memtcm.av5T \
    $(DV_FC_DIR)/packages/ti/sdo/fc/utils/lib/release/rmm.av5T \
    $(DV_FC_DIR)/packages/ti/sdo/fc/utils/lib/release/smgr.av5T \
    $(DV_FC_DIR)/packages/ti/sdo/fc/utils/lib/release/rmmp.av5T \
    $(DV_FC_DIR)/packages/ti/sdo/fc/utils/lib/release/smgrmp.av5T \
    $(DV_FC_DIR)/packages/ti/sdo/fc/utils/lib/release/shm.av5T \
    $(DV_FC_DIR)/packages/ti/sdo/fc/ires/addrspace/lib/release/addrspace.av5T \
    $(DV_FC_DIR)/packages/ti/sdo/fc/memutils/lib/release/memutils.av5T \
    $(DV_LINUX_UTILS_DIR)/packages/ti/sdo/linuxutils/cmem/lib/cmem.a470MV \
    $(DV_LINUX_UTILS_DIR)/packages/ti/sdo/linuxutils/edma/lib/edma.a470MV \
    $(DV_XDC_TOOLS_DIR)/packages/gnu/targets/arm/rtsv5T/lib/gnu.targets.arm.rtsv5T.av5T

endif
