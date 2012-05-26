/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 * 
 */
 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <cf_string.h>
#include <cf_common.h>
#include <cf_pipe.h>
#include <cf_buffer.h>
#include "medusa.h"
#include "mds_log.h"
#include "mds_media.h"
#include "mds_tools.h"

#define MDS_V4L2_ELEM_CLASS_NAME        "V4L2_ELEM"
#define MDS_RAW_IMG_CONV_ELEM_CLASS_NAME   "RAW_IMG_CONV_ELEM"
#define MDS_MSG_TYPE_IMAGE  "Image"
typedef struct RAW_IMG_CONV_elem{
    MDS_ELEM_MEMBERS;
    int fd;
    MdsImgBuf dstImgBuf;
    BOOL chained;
}MdsRawImgConvElem;

#define MDS_RAW_IMG_CONV_PLUGIN_NAME       "PLUG_RAW_IMG_CONV"
typedef struct mds_RAW_IMG_CONV_plug{
    MDS_PLUGIN_MEMBERS;
}MdsRawImgConvPlug;

static int _RawImgConvPlugInit(MDSPlugin* this, MDSServer* svr);
static int _RawImgConvPlugExit(MDSPlugin* this, MDSServer* svr);
MdsRawImgConvPlug raw_img_soft_conv = {
    .name = MDS_RAW_IMG_CONV_PLUGIN_NAME,
    .init = _RawImgConvPlugInit,
    .exit = _RawImgConvPlugExit
};

static MDSElem* _RawImgConvElemRequested(MDSServer* svr, CFJson* jConf);
static int _RawImgConvElemReleased(MDSElem* elem);
MdsElemClass _RawImgConvClass = {
    .name = MDS_RAW_IMG_CONV_ELEM_CLASS_NAME,
    .request = _RawImgConvElemRequested,
    .release = _RawImgConvElemReleased,
};

static MdsIdStrMap pFmtMap[] = {
    ID_TO_ID_STR(MDS_PIX_FMT_RGB24),
    ID_TO_ID_STR(MDS_PIX_FMT_SBGGR8),
    ID_TO_ID_STR(MDS_PIX_FMT_YUYV),
    ID_TO_ID_STR(MDS_PIX_FMT_UYVY),
    ID_TO_ID_STR(MDS_PIX_FMT_NV12)
};

static int __MdsRawImgConvProcess(MDSElem* this, MDSElem* vendor, MdsMsg* msg)
{
    MdsImgBuf* imgBuf = (MdsImgBuf*)msg->data;
    MdsRawImgConvElem* rice = (MdsRawImgConvElem*)this;
    int ret;
    struct timeval tv;
    
    if (!strcmp(msg->type, MDS_MSG_TYPE_IMAGE)) {
	    MDS_DBG("Converting Raw Image Frame\n")
        gettimeofday(&tv, NULL);
        MDS_DBG("now: %lld-%lld\n", (long long)tv.tv_sec, (long long)tv.tv_usec);
        if (MdsImgBufConvFmt(&rice->dstImgBuf, imgBuf)) {
            MDS_ERR("Convert Raw Image failed\n");
            return -1;
        }
        gettimeofday(&tv, NULL);
        MDS_DBG("now: %lld-%lld\n", (long long)tv.tv_sec, (long long)tv.tv_usec);
        ret = 0;
        MDSElemCastMsg((MDSElem*)rice, MDS_MSG_TYPE_IMAGE, &rice->dstImgBuf);
        return ret;
    } else {
	    return 0;
    }
}

static int __MdsRawImgConvAddAsGuest(MDSElem* this, MDSElem* vendor)
{
    MdsRawImgConvElem* rice = (MdsRawImgConvElem*)this;

    if (rice->chained) {
	    MDS_ERR("MdsRawImgConvElem can only be chained once!!\n")
	    return -1;
    }
    rice->chained = TRUE;
    return 0;
}

static int __MdsRawImgConvAddAsVendor(MDSElem* this, MDSElem* guestElem)
{
    return 0;
}

static int __MdsRawImgConvRemoveAsGuest(MDSElem* this, MDSElem* vendor)
{
    MdsRawImgConvElem* rice = (MdsRawImgConvElem*)this;
    
    if (!rice->chained) {
	    MDS_ERR("Elem not chained\n")
	    return -1;
    }

    rice->chained = FALSE;
    return 0;
}

static int __MdsRawImgConvRemoveAsVendor(MDSElem* this, MDSElem* guestElem)
{
    return 0;
}

/*
{
    "class": "RAW_IMG_CONV_ELEM",
    "name": "RawImgConv1",
    "width": 640,
    "height": 480,
    "src_pix_fmt": "MDS_PIX_FMT_SBGGR8",
    "dst_pix_fmt": "MDS_PIX_FMT_RGB24"
}
*/
static MDSElem* _RawImgConvElemRequested(MDSServer* svr, CFJson* jConf)
{
    MdsRawImgConvElem* rice;
    const char* tmpCStr;
    MdsPixFmt fmt;
    uint64 tmpUint64;
    int width, height;
    void* bufPtr;
    int bufSize;
    
    if (!svr || !jConf) {
	    MDS_ERR_OUT(ERR_OUT, "\n");
    }
    if (!(rice = (MdsRawImgConvElem*)malloc(sizeof(MdsRawImgConvElem)))) {
	    MDS_ERR_OUT(ERR_OUT, "malloc for MdsRawImgConvElem failed\n");
    }
    if (CFJsonObjectGetIdFromString(jConf, "dst_pix_fmt", pFmtMap, &tmpUint64)) {
        MDS_ERR_OUT(ERR_FREE_RICE, "Please specify correct \"dst_pix_fmt\" options in element config\n");
    } 
    fmt = tmpUint64;
    if (CFJsonObjectGetInt(jConf, "width", &width)
            ||CFJsonObjectGetInt(jConf, "height", &height)
            ||width%2
            ||height%2) {
        MDS_ERR_OUT(ERR_FREE_RICE, "Please specify correct \"width\" and \"height\" options in element config\n");
    }
    if ((bufSize = MdsImgGetImgBufSize(fmt, width, height)) == -1) {
        MDS_ERR_OUT(ERR_FREE_RICE, "Can not calc image buf size\n");
    }
    MDS_DBG("BUF->width=%d, height=%d, size=%d\n", width, height, bufSize);
    if (!(bufPtr = malloc(bufSize))) {
        MDS_ERR_OUT(ERR_FREE_RICE, "malloc for image buf failed\n");
    }
    if (MdsImgBufInit(&rice->dstImgBuf, fmt, width, height, bufPtr, bufSize)) {
        MDS_ERR_OUT(ERR_FREE_BUF, "Init MdsImgBuf failed\n")
    }
    if (!(tmpCStr=CFJsonObjectGetString(jConf, "name"))
		    || MDSElemInit((MDSElem*)rice, svr, &_RawImgConvClass, tmpCStr, __MdsRawImgConvProcess,
				    __MdsRawImgConvAddAsGuest, __MdsRawImgConvAddAsVendor,
				    __MdsRawImgConvRemoveAsGuest, __MdsRawImgConvRemoveAsVendor)) {
		    MDS_ERR_OUT(ERR_EXIT_IMG_BUF, "MDSElem init failed: for %s\n", tmpCStr);
    }
    rice->chained = FALSE;
    return (MDSElem*)rice;
ERR_EXIT_IMG_BUF:
    MdsImgBufExit(&rice->dstImgBuf);
ERR_FREE_BUF:
    free(bufPtr);
ERR_FREE_RICE:
    free(rice);
ERR_OUT:
    return NULL;
}

static int _RawImgConvElemReleased(MDSElem* elem)
{
    int ret = 0;
    MdsRawImgConvElem* rice = (MdsRawImgConvElem*)elem;

    free(MdsImgBufGetPtr(&rice->dstImgBuf));
    MdsImgBufExit(&rice->dstImgBuf);
    free(rice);
    return ret;
}

static int _RawImgConvPlugInit(MDSPlugin* plg, MDSServer* svr)
{
    MDS_MSG("Initiating plugin: "MDS_RAW_IMG_CONV_PLUGIN_NAME"\n");
    return MDSServerRegistElemClass(svr, &_RawImgConvClass);
}

static int _RawImgConvPlugExit(MDSPlugin* plg, MDSServer* svr)
{
    MDS_MSG("Exiting plugin: "MDS_RAW_IMG_CONV_PLUGIN_NAME"\n");
    return MDSServerAbolishElemClass(svr, &_RawImgConvClass);
}

