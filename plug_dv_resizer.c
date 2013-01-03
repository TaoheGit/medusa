
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <sys/ioctl.h>
#include <cf_string.h>
#include <cf_common.h>
#include <cf_pipe.h>
#include <cf_buffer.h>
#include "medusa.h"
#include "mds_log.h"
#include "mds_media.h"
#include "mds_tools.h"
#ifdef CONFIG_DM365_IPIPE
#include <media/davinci/imp_previewer.h>
#include <media/davinci/imp_resizer.h>
#include <media/davinci/dm365_ipipe.h>
#endif
#include <xdc/std.h>
#include <ti/sdo/dmai/Dmai.h>
#include <ti/sdo/dmai/BufferGfx.h>
#include <ti/sdo/dmai/Buffer.h>
#include <ti/sdo/dmai/Resize.h>

#define MDS_V4L2_ELEM_CLASS_NAME        "V4L2_ELEM"
#define MDS_DV_RESIZER_CLASS_NAME   "DV_RESIZER"
#define MDS_MSG_TYPE_IMAGE  "Image"

typedef struct DV_RESIZER_elem{
    MDS_ELEM_MEMBERS;
    Buffer_Handle hInBuf;
    Buffer_Handle hOutBuf;
    MdsImgBuf dstImgBuf;
    Resize_Handle hResize;
    BOOL chained;
}MdsDvResizerElem;

#define MDS_DV_RESIZER_PLUGIN_NAME       "PLUG_DV_RESIZER"
typedef struct mds_DV_RESIZER_plug {
    MDS_PLUGIN_MEMBERS;
} MdsDvResizerPlug;

static int _DvResizerPlugInit(MDSPlugin* this, MDSServer* svr);
static int _DvResizerPlugExit(MDSPlugin* this, MDSServer* svr);
MdsDvResizerPlug dv_resizer = {
    .name = MDS_DV_RESIZER_PLUGIN_NAME,
    .init = _DvResizerPlugInit,
    .exit = _DvResizerPlugExit
};

static MDSElem* _DvResizerElemRequested(MDSServer* svr, CFJson* jConf);
static int _DvResizerElemReleased(MDSElem* elem);
MdsElemClass _DvResizerClass = {
    .name = MDS_DV_RESIZER_CLASS_NAME,
    .request = _DvResizerElemRequested,
    .release = _DvResizerElemReleased,
};

static MdsIdStrMap pFmtMap[] = {
    ID_TO_ID_STR(MDS_PIX_FMT_RGB24),
    ID_TO_ID_STR(MDS_PIX_FMT_SBGGR8),
    ID_TO_ID_STR(MDS_PIX_FMT_SBGGR16),
    ID_TO_ID_STR(MDS_PIX_FMT_YUYV),
    ID_TO_ID_STR(MDS_PIX_FMT_UYVY),
    ID_TO_ID_STR(MDS_PIX_FMT_NV12)
};

static ColorSpace_Type MdsPixFmtToDmaiColorSpace(MdsPixFmt fmt)
{
    switch (fmt) {
	case (MDS_PIX_FMT_RGB24):
	    return ColorSpace_RGB888;
	case (MDS_PIX_FMT_UYVY):
	    return ColorSpace_UYVY;
	case (MDS_PIX_FMT_NV12):
	    return ColorSpace_YUV420PSEMI;
	default:
	    return ColorSpace_NOTSET;
    }
}

static int __MdsDvResizerProcess(MDSElem* this, MDSElem* vendor, MdsMsg* msg)
{
    MdsImgBuf* imgBuf;
    MdsDvResizerElem* dvRsz;
    int ret;
    //struct timeval tv;
    void* inUsrPtr;
    Resize_Handle hResize;
    Buffer_Handle hInBuf;
    Buffer_Handle hOutBuf;
    
    if (strcmp(msg->type, MDS_MSG_TYPE_IMAGE)) {
        MDS_ERR("<%s> Msg type: %s not support!\n", CFStringGetStr(&this->name), msg->type);
        return -1;
    }
    dvRsz = (MdsDvResizerElem*)this;
    imgBuf = (MdsImgBuf*)msg->data;
    hResize = dvRsz->hResize;
    hInBuf = dvRsz->hInBuf;
    hOutBuf = dvRsz->hOutBuf;
    inUsrPtr = MdsImgBufGetPtr(imgBuf);
    
    //MDS_DBG("dv resizer processing\n")
    //gettimeofday(&tv, NULL);
    //MDS_DBG("now: %lld-%lld\n", (long long)tv.tv_sec, (long long)tv.tv_usec);

    if (Buffer_setUserPtr(dvRsz->hInBuf, inUsrPtr)<0) {
		MDS_ERR("DvResizer Buffer_setUserPtr failed\n");
		return -1;
    }
    
    if (Resize_execute(dvRsz->hResize, dvRsz->hInBuf, dvRsz->hOutBuf) < 0) {
		MDS_ERR("DvResizer Resize_execute failed\n");
		return -1;
    }
    
    //gettimeofday(&tv, NULL);
    //MDS_DBG("now: %lld-%lld\n", (long long)tv.tv_sec, (long long)tv.tv_usec);
    ret = MDSElemCastMsg((MDSElem*)dvRsz, MDS_MSG_TYPE_IMAGE, &dvRsz->dstImgBuf); 
    return ret;
}

static int __MdsDvResizerAddAsGuest(MDSElem* this, MDSElem* vendor)
{
    MdsDvResizerElem* dvRsz = (MdsDvResizerElem*)this;

    if (dvRsz->chained) {
	    MDS_ERR("MdsDvResizerElem can only be chained once!!\n")
	    return -1;
    }
    dvRsz->chained = TRUE;
    return 0;
}

static int __MdsDvResizerAddAsVendor(MDSElem* this, MDSElem* guestElem)
{
    return 0;
}

static int __MdsDvResizerRemoveAsGuest(MDSElem* this, MDSElem* vendor)
{
    MdsDvResizerElem* dvRsz = (MdsDvResizerElem*)this;
    
    if (!dvRsz->chained) {
		MDS_ERR("DvResizer Elem not chained\n")
		return -1;
    }
    return 0;
}

static int __MdsDvResizerRemoveAsVendor(MDSElem* this, MDSElem* guestElem)
{
    return 0;
}

/*
{
    "class": "DV_RESIZER",
    "name": "DvResizer1",
    "in_width": 640,
    "in_height": 480,
    "in_pix_fmt": "MDS_PIX_FMT_SBGGR16",
    "out_width": 640,
    "out_height": 480,
    "out_pix_fmt": "MDS_PIX_FMT_NV12"
}
*/
static MDSElem* _DvResizerElemRequested(MDSServer* svr, CFJson* jConf)
{
    MdsDvResizerElem* dvRsz;
    const char* tmpCStr;
    MdsPixFmt inFmt, outFmt;
    uint64 tmpUint64;
    int inWidth, inHeight, outWidth, outHeight;
    void* bufPtr;
    int inBufSize, outBufSize;
    BufferGfx_Attrs gfxAttrs;
    BufferGfx_Dimensions dim;
    Resize_Attrs rszAttrs = Resize_Attrs_DEFAULT;
    
    if (!svr || !jConf) {
		MDS_ERR_OUT(ERR_OUT, "\n");
    }
    Dmai_init();
    if (!(dvRsz = (MdsDvResizerElem*)malloc(sizeof(MdsDvResizerElem)))) {
		MDS_ERR_OUT(ERR_OUT, "malloc for MdsDvResizerElem failed\n");
    }
    if (CFJsonObjectGetIdFromString(jConf, "in_pix_fmt", pFmtMap, &tmpUint64)) {
        MDS_ERR_OUT(ERR_FREE_RICE, "Please specify correct \"in_pix_fmt\" options for dv_resizer\n");
    }
    inFmt = tmpUint64;
    if (CFJsonObjectGetInt(jConf, "in_width", &inWidth)
            ||CFJsonObjectGetInt(jConf, "in_height", &inHeight)
            ||inWidth%2
            ||inHeight%2) {
        MDS_ERR_OUT(ERR_FREE_RICE, "Please specify correct \"in_width\" and \"in_height\" options for dv_resizer\n");
    }
    if ((inBufSize = MdsImgGetImgBufSize(inFmt, inWidth, inHeight)) == -1) {
        MDS_ERR_OUT(ERR_FREE_RICE, "Can not calc input image buf size\n");
    }
    MDS_DBG("input ==> width=%d, height=%d, imgBufSize=%d\n", inWidth, outHeight, inBufSize);

    if (CFJsonObjectGetIdFromString(jConf, "out_pix_fmt", pFmtMap, &tmpUint64)) {
        MDS_ERR_OUT(ERR_FREE_RICE, "Please specify correct \"in_pix_fmt\" options for dv_resizer\n");
    }
    outFmt = tmpUint64;
    if (CFJsonObjectGetInt(jConf, "out_width", &outWidth)
            ||CFJsonObjectGetInt(jConf, "out_height", &outHeight)
            ||outWidth%2
            ||outHeight%2) {
        MDS_ERR_OUT(ERR_FREE_RICE, "Please specify correct \"out_width\" and \"out_height\" options for dv_resizer\n");
    }
    if ((outBufSize = MdsImgGetImgBufSize(outFmt, outWidth, outHeight)) == -1) {
        MDS_ERR_OUT(ERR_FREE_RICE, "Can not calc input image buf size\n");
    }
    MDS_DBG("output ==> width=%d, height=%d, imgBufSize=%d\n", outWidth, outHeight, outBufSize);
    /* Prepare input buf */
    gfxAttrs = BufferGfx_Attrs_DEFAULT;
    gfxAttrs.colorSpace = MdsPixFmtToDmaiColorSpace(inFmt);
    gfxAttrs.bAttrs.reference = TRUE;
    if (!(dvRsz->hInBuf = Buffer_create(inBufSize, BufferGfx_getBufferAttrs(&gfxAttrs)))) {
        MDS_ERR_OUT(ERR_FREE_RICE, "malloc for image buf failed\n");
    }
    dim.x = dim.y =0;
    dim.width = inWidth;
    dim.height = inHeight;
    dim.lineLength = (MdsImgGetBitsPerPix(inFmt)*inWidth)>>3;
    if (BufferGfx_setDimensions(dvRsz->hInBuf, &dim)<0) {
		MDS_ERR_OUT(ERR_FREE_IN_BUF, "set dimension for input buffer failed\n");
    }
    /* Prepare output buf */
    gfxAttrs = BufferGfx_Attrs_DEFAULT;
    gfxAttrs.colorSpace = ColorSpace_YUV420PSEMI;//MdsPixFmtToDmaiColorSpace(outFmt);
    if (!(dvRsz->hOutBuf = Buffer_create(outBufSize, BufferGfx_getBufferAttrs(&gfxAttrs)))) {
        MDS_ERR_OUT(ERR_FREE_IN_BUF, "malloc for image buf failed\n");
    }
    dim.x = dim.y =0;
    dim.width = outWidth;
    dim.height = outHeight;
    dim.lineLength = (MdsImgGetBitsPerPix(outFmt)*outWidth)>>3;
    MDS_DBG("output ==> width=%d, height=%d, imgBufSize=%d\n", outWidth, outHeight, outBufSize);
    if (BufferGfx_setDimensions(dvRsz->hOutBuf, &dim)<0) {
		MDS_ERR_OUT(ERR_FREE_OUT_BUF, "set dimension for input buffer failed\n");
    }
    
    /* Prepare output mds img buf for other element process */
    if (!(bufPtr = Buffer_getUserPtr(dvRsz->hOutBuf))) {
		MDS_ERR_OUT(ERR_FREE_OUT_BUF, "get usr ptr of output buf failed\n");
    }
    if (MdsImgBufInit(&dvRsz->dstImgBuf, outFmt, outWidth, outHeight, bufPtr, outBufSize)) {
        MDS_ERR_OUT(ERR_FREE_OUT_BUF, "Init MdsImgBuf failed\n");
    }
    
    if (!(dvRsz->hResize = Resize_create(&rszAttrs))) {
		MDS_ERR_OUT(ERR_EXIT_IMG_BUF,"Resize_create failed\n");
    }
    if (Resize_config(dvRsz->hResize, dvRsz->hInBuf, dvRsz->hOutBuf)<0) {
		MDS_ERR_OUT(ERR_DEL_RSZ, "Resize_config failed\n");
    }
    if (!(tmpCStr=CFJsonObjectGetString(jConf, "name"))
		    || MDSElemInit((MDSElem*)dvRsz, svr, &_DvResizerClass, tmpCStr, __MdsDvResizerProcess,
				    __MdsDvResizerAddAsGuest, __MdsDvResizerAddAsVendor,
				    __MdsDvResizerRemoveAsGuest, __MdsDvResizerRemoveAsVendor)) {
		MDS_ERR_OUT(ERR_DEL_RSZ, "MDSElem init failed: for %s\n", tmpCStr);
    }
    dvRsz->chained = FALSE;
    return (MDSElem*)dvRsz;
ERR_DEL_RSZ:
    Resize_delete(dvRsz->hResize);
ERR_EXIT_IMG_BUF:
    MdsImgBufExit(&dvRsz->dstImgBuf);
ERR_FREE_OUT_BUF:
    Buffer_delete(dvRsz->hOutBuf);
ERR_FREE_IN_BUF:
    Buffer_delete(dvRsz->hInBuf);
ERR_FREE_RICE:
    free(dvRsz);
ERR_OUT:
    return NULL;
}

static int _DvResizerElemReleased(MDSElem* elem)
{
    int ret = 0;
    MdsDvResizerElem* dvRsz = (MdsDvResizerElem*)elem;

    ret |= MDSElemExit(elem);
    ret |= Resize_delete(dvRsz->hResize);
    MdsImgBufExit(&dvRsz->dstImgBuf);
    ret |= Buffer_delete(dvRsz->hOutBuf);
    ret |= Buffer_delete(dvRsz->hInBuf);
    free(dvRsz);
    return ret;
}

static int _DvResizerPlugInit(MDSPlugin* plg, MDSServer* svr)
{
    MDS_MSG("Initiating plugin: "MDS_DV_RESIZER_PLUGIN_NAME"\n");
    return MDSServerRegistElemClass(svr, &_DvResizerClass);
}

static int _DvResizerPlugExit(MDSPlugin* plg, MDSServer* svr)
{
    MDS_MSG("Exiting plugin: "MDS_DV_RESIZER_PLUGIN_NAME"\n");
    return MDSServerAbolishElemClass(svr, &_DvResizerClass);
}

