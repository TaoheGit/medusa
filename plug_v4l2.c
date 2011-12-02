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
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <errno.h>
#include <linux/videodev2.h>
#include <cf_string.h>
#include <cf_common.h>
#include "medusa.h"
#include "mds_log.h"
#include "plug_v4l2.h"
#include "mds_media.h"

#ifndef MAX_V4L2_ELEMS
#define MAX_V4L2_ELEMS  6
#endif
#define MDS_V4L2_ELEM_CLASS_NAME        "V4L2_ELEM"

typedef struct v4l2_elem{
        MDS_ELEM_MEMBERS;
        int inputIdx;
        v4l2_std_id std;
    struct v4l2_format format;
    struct v4l2_requestbuffers req;
    CFString device;
    int fd;
    BOOL streaming;
    MdsImgBuf* imgBufs;
    CFFdevent fdEvent;
}MdsV4l2Elem;

#define MDS_V4L2_PLUGIN_NAME    "v4l2"
typedef struct mds_v4l2_plug{
    MDS_PLUGIN_MEMBERS;
    MdsV4l2Elem* v4l2Elems[MAX_V4L2_ELEMS];
}MDSV4l2Plug;

static int V4l2PlugInit(MDSPlugin* this, MDSServer* svr);
static int V4l2PlugExit(MDSPlugin* this, MDSServer* svr);
MDSV4l2Plug v4l2 = {
    .name = MDS_V4L2_PLUGIN_NAME,
    .init = V4l2PlugInit,
    .exit = V4l2PlugExit
};

static MDSElem* _V4l2ElemRequested(MDSServer* svr, CFJson* jConf);
static int _V4l2ElemReleased(MDSElem* elem);
MdsElemClass _V4l2Class = {
        .name = MDS_V4L2_ELEM_CLASS_NAME,
        .request = _V4l2ElemRequested,
        .release = _V4l2ElemReleased,
};

#define ID_TO_ID_STR(_id)   {(uint64)_id, #_id}
typedef struct {
    uint64 id;
    const char* str;
}IdStrMap;

int StrToId(IdStrMap* maps, const char* str, uint64* id)
{
    int i;

    if (!str) {
        return -1;
    }
    for(i=0; maps[i].str; i++){
        MDS_DBG("%s=%llu\n", maps[i].str, maps[i].id);
        if(!strcmp(str, maps[i].str)){
            *id = maps[i].id;
            return 0;
        }
    }
    MDS_ERR("%s not found in id str map.\n", str);
    return -1;
}

const char* IdToStr(IdStrMap* maps, int id)
{
    int i;

    for(i=0; maps[i].str; i++){
        if(id == maps[i].id){
            return maps[i].str;
        }
    }
    CF_ERR("id=%d not found in id str map.\n", id);
    return NULL;
}

static int CFJsonObjectGetIdFromString(CFJson* conf, const char* key, IdStrMap* maps, uint64* id)
{
    const char* tmpC;

    if(NULL == (tmpC = CFJsonObjectGetString(conf, key))){
        CF_ERR("Get config key=%s failed\n", key);
        return -1;
    }
    if(-1 == StrToId(maps, tmpC, id)){
        CF_ERR("%s not support\n", tmpC);
        return -1;
    }
    return 0;
}

static IdStrMap V4l2StdMaps[] = {
        ID_TO_ID_STR(V4L2_STD_PAL),
        ID_TO_ID_STR(V4L2_STD_NTSC),
        {-1, NULL}
};

static IdStrMap V4l2BufTypeMaps[]={
    ID_TO_ID_STR(V4L2_BUF_TYPE_VIDEO_CAPTURE),
    ID_TO_ID_STR(V4L2_BUF_TYPE_VIDEO_OUTPUT),
    ID_TO_ID_STR(V4L2_BUF_TYPE_VIDEO_OVERLAY),
    ID_TO_ID_STR(V4L2_BUF_TYPE_VBI_CAPTURE),
    ID_TO_ID_STR(V4L2_BUF_TYPE_VBI_OUTPUT),
    ID_TO_ID_STR(V4L2_BUF_TYPE_SLICED_VBI_CAPTURE),
    ID_TO_ID_STR(V4L2_BUF_TYPE_SLICED_VBI_OUTPUT),
    ID_TO_ID_STR(V4L2_BUF_TYPE_VIDEO_OUTPUT_OVERLAY),
    ID_TO_ID_STR(V4L2_BUF_TYPE_PRIVATE),
    {-1, NULL}
};

static IdStrMap V4l2PixFmtMaps[]={
    /* Packed RGB formats */
    ID_TO_ID_STR(V4L2_PIX_FMT_RGB332),
    ID_TO_ID_STR(V4L2_PIX_FMT_RGB444),
    ID_TO_ID_STR(V4L2_PIX_FMT_RGB555),
    ID_TO_ID_STR(V4L2_PIX_FMT_RGB565),
    ID_TO_ID_STR(V4L2_PIX_FMT_RGB555X),
    ID_TO_ID_STR(V4L2_PIX_FMT_RGB565X),
    ID_TO_ID_STR(V4L2_PIX_FMT_BGR24),
    ID_TO_ID_STR(V4L2_PIX_FMT_RGB24),
    ID_TO_ID_STR(V4L2_PIX_FMT_BGR32),
    ID_TO_ID_STR(V4L2_PIX_FMT_RGB32),
    /* Bayer RGB formats */
    ID_TO_ID_STR(V4L2_PIX_FMT_SBGGR8),
    ID_TO_ID_STR(V4L2_PIX_FMT_SGBRG8),
    ID_TO_ID_STR(V4L2_PIX_FMT_SGRBG8),
    ID_TO_ID_STR(V4L2_PIX_FMT_SBGGR16),
    /* Packed YUV formats */
    ID_TO_ID_STR(V4L2_PIX_FMT_YUV444),
    ID_TO_ID_STR(V4L2_PIX_FMT_YUV555),
    ID_TO_ID_STR(V4L2_PIX_FMT_YUV565),
    ID_TO_ID_STR(V4L2_PIX_FMT_YUV32),
    /* Grey-scale image */
    ID_TO_ID_STR(V4L2_PIX_FMT_GREY),
    ID_TO_ID_STR(V4L2_PIX_FMT_Y16),
    /* YUV formats */
    ID_TO_ID_STR(V4L2_PIX_FMT_UYVY),
    ID_TO_ID_STR(V4L2_PIX_FMT_YUYV),
    ID_TO_ID_STR(V4L2_PIX_FMT_VYUY),
    ID_TO_ID_STR(V4L2_PIX_FMT_YVYU),
    ID_TO_ID_STR(V4L2_PIX_FMT_Y41P),
    ID_TO_ID_STR(V4L2_PIX_FMT_YVU420),
    ID_TO_ID_STR(V4L2_PIX_FMT_YVU410),
    ID_TO_ID_STR(V4L2_PIX_FMT_YUV422P),
    ID_TO_ID_STR(V4L2_PIX_FMT_YUV411P),
    ID_TO_ID_STR(V4L2_PIX_FMT_NV12),
    ID_TO_ID_STR(V4L2_PIX_FMT_NV21),
    ID_TO_ID_STR(V4L2_PIX_FMT_NV16),
    ID_TO_ID_STR(V4L2_PIX_FMT_NV61),
    /* Compressed Image Formats */
    ID_TO_ID_STR(V4L2_PIX_FMT_JPEG),
    ID_TO_ID_STR(V4L2_PIX_FMT_MPEG),

    {-1, NULL}
};

static IdStrMap V4l2BufMemMaps[]={
    ID_TO_ID_STR(V4L2_MEMORY_MMAP),
    {-1, NULL}
};

static IdStrMap V4l2FieldMaps[] = {
        ID_TO_ID_STR(V4L2_FIELD_ANY),
        {-1, NULL}
};

static int __MDSV4l2ElemAddAsGuest(MDSElem* this, MDSElem* vendorElem)
{
        MdsV4l2Elem* vElem;
        int ret, i;
        struct v4l2_buffer buf;
        int t = V4L2_BUF_TYPE_VIDEO_OUTPUT;

        vElem = (MdsV4l2Elem*)this;
        assert(vElem);
        switch (vElem->format.type) {
                case V4L2_BUF_TYPE_VIDEO_CAPTURE:
                    MDS_ERR("V4l2 Capture element shouldn't be added as guest!!\n");
                    ret = -1;
                        break;
                case V4L2_BUF_TYPE_VIDEO_OUTPUT:
                    switch (vElem->req.memory) {
                        case V4L2_MEMORY_MMAP:
                                for (i = 0; i < vElem->req.count; i++) {
                                buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                                buf.index = i;
                                buf.memory = V4L2_MEMORY_MMAP;
                                if ((ret = ioctl(vElem->fd, VIDIOC_QBUF, &buf))) {
                                        MDS_ERR("VIDIOC_QBUF\n");
                                        break;
                                }
                        }
                            if (!ret) {
                                if ((ret=ioctl(vElem->fd, VIDIOC_STREAMON, &t))) {
                                    MDS_ERR("VIDIOC_STREAMON\n");
                                }
                            }
                            break;
                        case V4L2_MEMORY_USERPTR:
                            /* todo */
                            break;
                        default:
                            MDS_ERR("Not implemented yet\n");
                        ret = -1;
                        break;
                    }
                    break;
                 default:
                    MDS_ERR("Not implemented yet\n");
                    ret = -1;
                    break;
        }
        return ret;
}

static int __MDSV4l2ElemAddAsVendor(MDSElem* this, MDSElem* guestElem)
{
        MdsV4l2Elem* vElem;
        int ret = 0;
        struct v4l2_buffer buf;
        enum v4l2_buf_type t = 0;

        vElem = (MdsV4l2Elem*)this;
        assert(vElem);
        CF_CLEAR(&buf);
        switch (vElem->format.type) {
                case V4L2_BUF_TYPE_VIDEO_CAPTURE:
                    switch (vElem->req.memory) {
                        case V4L2_MEMORY_MMAP:
                            if (!vElem->guests) {       /* start streaming only first time being added as vendor */
                                t = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                                if ((ret=ioctl(vElem->fd, VIDIOC_STREAMON, &t))) {
                                    MDS_ERR("VIDIOC_STREAMON\n");
                                }
                                CFFdeventsAdd(vElem->server->fdevents, &vElem->fdEvent);
                            }
                            break;
                        case V4L2_MEMORY_USERPTR:
                            /* todo */
                            break;
                        default:
                            MDS_ERR("Not implemented yet\n");
                        ret = -1;
                        break;
                    }
                    break;
                case V4L2_BUF_TYPE_VIDEO_OUTPUT:
                    MDS_ERR("V4l2 output element shouldn't be added as vendor!!\n");
                    ret = -1;
                        break;
        default:
                    MDS_ERR("Not implemented yet\n");
            ret = -1;
            break;
        }
        return ret;
}

static int __MDSV4l2ElemProcess(MDSElem* this, MDSElem* vendor, void* data)
{
        MdsV4l2Elem* vElem;
        int ret = 0;
        struct v4l2_buffer buf;
        MdsImgBuf* vImg;

        vElem = (MdsV4l2Elem*)this;
        assert(vElem);
        switch (vElem->format.type) {
                case V4L2_BUF_TYPE_VIDEO_CAPTURE:
                    MDS_ERR("V4l2 Capture element can not process anything!!\n");
                    ret = -1;
                        break;
                case V4L2_BUF_TYPE_VIDEO_OUTPUT:
                    vImg = (MdsImgBuf*)data;
                    assert(vImg);
                    switch (vElem->req.memory) {
                        case V4L2_MEMORY_MMAP:
                        if (vImg->pixFmt != vElem->imgBufs[0].pixFmt) {
                            MDS_ERR("V4l2 output image failed: format isn't acceptable\n'");
                            ret = -1;
                            break;
                        }
                    buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
                        buf.memory = V4L2_MEMORY_MMAP;
                        if ((ret = ioctl(vElem->fd, VIDIOC_DQBUF, &buf))) {
                            if (errno==EAGAIN) {
                                MDS_MSG("V4l2 output EAGAIN\n");
                                ret = 0;
                            } else {
                                ret = -1;
                                    MDS_ERR("VIDIOC_DQBUF\n");
                                }
                                break;
                        }
                        if (buf.index<0 || buf.index>=vElem->req.count) {
                            ret = -1;
                            MDS_ERR("Wired!!");
                            break;
                        }
                    memcpy(vElem->imgBufs[buf.index].bufPtr, vImg->bufPtr,
                            MdsImgGetImgSize(vElem->imgBufs[buf.index].pixFmt,
                                    vElem->imgBufs[buf.index].width,
                                    vElem->imgBufs[buf.index].height));
                    if ((ret=ioctl(vElem->fd, VIDIOC_QBUF, &buf))) {
                        MDS_ERR("VIDIOC_QBUF\n");
                    }
                            break;
                        case V4L2_MEMORY_USERPTR:
                            /* todo */
                            //break;
                        default:
                            MDS_ERR("Not implemented yet\n");
                        ret = -1;
                        break;
                    }
                    break;
        default:
                    MDS_ERR("Not implemented yet\n");
            ret = -1;
            break;
        }
        return ret;
}

static int __MDSV4l2ElemRemoveAsGuest(MDSElem* this, MDSElem* vendorElem)
{
        MdsV4l2Elem* vElem;
        int ret = 0;
        int t = V4L2_BUF_TYPE_VIDEO_OUTPUT;

        vElem = (MdsV4l2Elem*)this;
        assert(vElem);
        switch (vElem->format.type) {
                case V4L2_BUF_TYPE_VIDEO_CAPTURE:
                    MDS_ERR("V4l2 Capture element shouldn't be deleted as guest!!\n");
                    ret = -1;
                        break;
                case V4L2_BUF_TYPE_VIDEO_OUTPUT:
                    switch (vElem->req.memory) {
                        case V4L2_MEMORY_MMAP:
                        /* Here type of device to be streamed off is required to be passed */
                        if ((ret = ioctl(vElem->fd, VIDIOC_STREAMOFF, &t))) {
                                MDS_ERR("VIDIOC_STREAMOFF\n");
                        }
                            break;
                        case V4L2_MEMORY_USERPTR:
                            /* todo */
                            break;
                default:
                            MDS_ERR("Not implemented yet\n");
                    ret = -1;
                    break;
                    }
                    break;
        default:
                    MDS_ERR("Not implemented yet\n");
            ret = -1;
            break;
        }
        return ret;
}

static int __MDSV4l2RemoveAsVendor(MDSElem* this, MDSElem* guestElem)
{
        MdsV4l2Elem* vElem;
        int ret;
        int t = V4L2_BUF_TYPE_VIDEO_CAPTURE;

        vElem = (MdsV4l2Elem*)this;
        assert(vElem);
        switch (vElem->format.type) {
                case V4L2_BUF_TYPE_VIDEO_CAPTURE:
                    switch (vElem->req.memory) {
                        case V4L2_MEMORY_MMAP:
                                if (!vElem->guests || CFGListGetNext(vElem->guests) == vElem->guests) { /* only one guest and will be removed */
                                        CFFdeventsDel(vElem->server->fdevents, &vElem->fdEvent);
                                    if ((ret=ioctl(vElem->fd, VIDIOC_STREAMOFF, &t))) {
                                        MDS_ERR("VIDIOC_STREAMOFF\n");
                                    }
                        }
                            break;
                        case V4L2_MEMORY_USERPTR:
                            /* todo */
                            break;
                default:
                            MDS_ERR("Not implemented yet\n");
                    ret = -1;
                    break;
                    }
                    break;
                case V4L2_BUF_TYPE_VIDEO_OUTPUT:
                    MDS_ERR("V4l2 output element shouldn't be deleted as vendor!!\n");
                    ret = -1;
                        break;
        default:
                    MDS_ERR("Not implemented yet\n");
            ret = -1;
            break;
        }
        return ret;
}

int __MdsV4l2CapMmapReadable(CFFdevents* events, CFFdevent* event, int fd, void* data)
{
    MdsV4l2Elem* vElem;
    struct v4l2_buffer buf;
    int ret;

    vElem = (MdsV4l2Elem*)data;
    assert(vElem);
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        /* Dequeue buffer
         * VIDIOC_DQBUF ioctl de-queues a captured buffer from driver.
         * This call can be blocking or non blocking. For blocking call, it
         * blocks untill a capture frame is available. For non-blocking call,
         * it returns instantaneously with success or error depending on
         * captured buffer is available or not.
         */
        MDS_DBG("DQBUF\n");
        if ((ret = ioctl(vElem->fd, VIDIOC_DQBUF, &buf))) {
                MDS_ERR_OUT(ERR_OUT, "VIDIOC_DQBUF\n");
        }
        MDS_DBG("PROCESS\n");
    CFGListForeach(vElem->guests, node) {
        MDSElem* guestElem;

        guestElem = (MDSElem*)node->data;
        assert(guestElem);
        ret |= guestElem->process(guestElem, (MDSElem*)vElem, &vElem->imgBufs[buf.index]);
    }
    if ((ret=ioctl(vElem->fd, VIDIOC_QBUF, &buf))) {
        MDS_ERR_OUT(ERR_OUT, "VIDIOC_QBUF\n");
    }
    MDS_DBG("QBUF\n");
    return ret;
ERR_OUT:
    return ret;
}

int __MdsV4l2OutputMmapReadable(CFFdevents* events, CFFdevent* event, int fd, void* data)
{
    MdsV4l2Elem* vElem;

    vElem = (MdsV4l2Elem*)data;
    assert(vElem);
    /* todo */
    return 0;
}

int __MdsV4l2ElemInitMemMap(MdsV4l2Elem* vElem)
{
        struct v4l2_buffer buf;
        void *tmpPtr;
        int iForReqBuf = 0;
        int i;

        vElem->imgBufs = calloc(sizeof(MdsImgBuf), vElem->req.count);
        if (!vElem->imgBufs) {
                MDS_ERR_OUT(ERR_OUT, "\n");
        }
        /* 1) request bufs; */
        if (ioctl(vElem->fd, VIDIOC_REQBUFS, &vElem->req)) {
                MDS_ERR_OUT(ERR_FREE_MDS_IMG_BUF, "v4l2 request buffers failed\n")
        }
        /* 2) query bufs information */
        for (iForReqBuf=0; iForReqBuf<vElem->req.count; iForReqBuf++) {
                buf.type = vElem->req.type;
                buf.memory = vElem->req.memory;
                buf.index = iForReqBuf;
                if (ioctl(vElem->fd, VIDIOC_QUERYBUF, &buf)) {
                        MDS_ERR_OUT(ERR_UNQUERY_BUFS, "\n");
                }
                tmpPtr = mmap(NULL, buf.length,
                                PROT_READ | PROT_WRITE, MAP_SHARED,
                                vElem->fd, buf.m.offset);
                if (!tmpPtr) {
                        MDS_ERR_OUT(ERR_UNQUERY_BUFS, "\n")
                }
                if (MdsImgBufInit(&vElem->imgBufs[iForReqBuf],
                                MdsV4l2PixFmtToMdsPixFmt(vElem->format.fmt.pix.pixelformat) ,
                                vElem->format.fmt.pix.width,
                                vElem->format.fmt.pix.height,
                                tmpPtr, buf.length)) {
                        munmap(tmpPtr, buf.length);
                        MDS_ERR_OUT(ERR_UNQUERY_BUFS, "Image buf init failed\n");
                }
                MDS_MSG("V4L2 wanted size=%d, got size=%d\n",
                                MdsImgGetImgSize(MdsV4l2PixFmtToMdsPixFmt(vElem->format.fmt.pix.pixelformat),
                                                vElem->format.fmt.pix.width,
                                                vElem->format.fmt.pix.height),
                            buf.length);
        }
        /* 3) enqueue bufs */
        for (i=0; i<vElem->req.count; i++) {
                buf.type = vElem->req.type;
                buf.memory = vElem->req.memory;
                buf.index = i;
                if (ioctl(vElem->fd, VIDIOC_QBUF, &buf)) {
                        MDS_ERR_OUT(ERR_UNQUERY_BUFS, "v4l2 queue buf failed during initiating\n")
                }
        }
        return 0;
ERR_UNQUERY_BUFS:
        for (iForReqBuf--; iForReqBuf>=0; iForReqBuf--) {
                MdsImgBufExit(&vElem->imgBufs[iForReqBuf]);
                munmap(vElem->imgBufs[iForReqBuf].bufPtr, vElem->imgBufs[iForReqBuf].bufSize);
        }
ERR_FREE_MDS_IMG_BUF:
        free(vElem->imgBufs);
ERR_OUT:
        return -1;
}

int __MdsV4l2ElemExitMemMap(MdsV4l2Elem* vElem)
{
        int iForReqBuf = vElem->req.count;
        int ret;

        for (iForReqBuf--; iForReqBuf>=0; iForReqBuf--) {
                MdsImgBufExit(&vElem->imgBufs[iForReqBuf]);
                ret |= munmap(vElem->imgBufs[iForReqBuf].bufPtr, vElem->imgBufs[iForReqBuf].bufSize);
        }
        free(vElem->imgBufs);
        return ret;
}

int __MdsV4l2ElemInitUserPtr(MdsV4l2Elem* vElem)
{
        /* todo */
        return 0;
}


int __MdsV4l2ElemExitUserPtr(MdsV4l2Elem* vElem)
{
        /* todo */
        return 0;
}

int __MdsV4l2ElemInit(MdsV4l2Elem* vElem)
{
        int idx;
        struct v4l2_format fmt;
        const char* tmpCStr;

        tmpCStr = CFStringGetStr(&vElem->device);
    if(!tmpCStr || !CFFileIsCharDevice(tmpCStr)){
        MDS_ERR_OUT(ERR_OUT, "%s not a character device file\n", tmpCStr);
    }
        if ((vElem->fd = open(tmpCStr, O_RDWR|O_NONBLOCK)) < 0) {
                MDS_ERR_OUT(ERR_OUT, "Open V4L2 device: %s failed\n", tmpCStr);
        }
        if ((-1 == ioctl(vElem->fd, VIDIOC_S_INPUT, &vElem->inputIdx))
                        || (-1 == ioctl(vElem->fd, VIDIOC_G_INPUT, &idx))
                        || (idx != vElem->inputIdx)) {
                MDS_ERR_OUT(ERR_CLOSE_FD, "Set V4L2 input index failed\n");
        }
        if (vElem->std != V4L2_STD_UNKNOWN) {
                if ((-1 == ioctl(vElem->fd, VIDIOC_S_STD, &vElem->std))
                                || (-1 == ioctl(vElem->fd, VIDIOC_G_STD, &idx))
                                || (idx != vElem->std)) {
                        MDS_ERR_OUT(ERR_CLOSE_FD, "Set V4L2 standard failed\n");
                }
        }
        memset(&fmt, 0, sizeof(fmt));
        fmt.type = vElem->format.type;  /* This is must!! */
        if ((-1 == ioctl(vElem->fd, VIDIOC_S_FMT, &vElem->format))
                        || (-1 == ioctl(vElem->fd, VIDIOC_G_FMT, &fmt))
                        || (fmt.type != vElem->format.type)
                        || (fmt.fmt.pix.width != vElem->format.fmt.pix.width)
                        || (fmt.fmt.pix.height != vElem->format.fmt.pix.height)
                        || (fmt.fmt.pix.pixelformat != vElem->format.fmt.pix.pixelformat)) {
                        perror(NULL);
                MDS_DBG("V4L2 format to set: type=%d, width=%d, height=%d, pixelformat=%d\n",
                                vElem->format.type, vElem->format.fmt.pix.width, vElem->format.fmt.pix.height, vElem->format.fmt.pix.pixelformat);
                MDS_DBG("V4L2 Set format result: type=%d, width=%d, height=%d, pixelformat=%d\n",
                                fmt.type, fmt.fmt.pix.width, fmt.fmt.pix.height, fmt.fmt.pix.pixelformat);
                MDS_ERR_OUT(ERR_CLOSE_FD, "Set V4L2 format failed\n");
        }
        switch (vElem->req.memory) {
                case V4L2_MEMORY_MMAP:
                        if (-1 == __MdsV4l2ElemInitMemMap(vElem)) {
                                MDS_ERR_OUT(ERR_CLOSE_FD, "\n");
                        }
                        break;
                case V4L2_MEMORY_USERPTR:
                        if (-1 == __MdsV4l2ElemInitUserPtr(vElem)) {
                                MDS_ERR_OUT(ERR_CLOSE_FD, "\n");
                        }
                        break;
                case V4L2_MEMORY_OVERLAY:
                        MDS_ERR_OUT(ERR_CLOSE_FD, "Fix me: V4L2_MEMORY_OVERLAY not implemented yet!!");
                        break;

        }
        switch (vElem->format.type) {
            case V4L2_BUF_TYPE_VIDEO_CAPTURE:
                switch (vElem->req.memory) {
                    case V4L2_MEMORY_MMAP:
                        CFFdeventInit(&vElem->fdEvent, vElem->fd,
                                __MdsV4l2CapMmapReadable, vElem,
                                NULL, NULL);
                        break;
                    case V4L2_MEMORY_USERPTR:
                        /* todo */
                        break;
                default:
                            MDS_ERR_OUT(ERR_CLOSE_FD, "Not implemented yet\n");
                    break;
                }
                break;
            case V4L2_BUF_TYPE_VIDEO_OUTPUT:
                switch (vElem->req.memory) {
                    case V4L2_MEMORY_MMAP:
                        CFFdeventInit(&vElem->fdEvent, vElem->fd,
                                __MdsV4l2OutputMmapReadable, vElem,
                                NULL, NULL);
                        break;
                    case V4L2_MEMORY_USERPTR:
                        break;
                default:
                            MDS_ERR_OUT(ERR_CLOSE_FD, "Not implemented yet\n");
                    break;
                }
                break;
        default:
            MDS_ERR_OUT(ERR_CLOSE_FD, "Not implemented yet\n");
            break;
        }
        vElem->streaming = FALSE;
        return 0;
ERR_CLOSE_FD:
        close(vElem->fd);
ERR_OUT:
        return -1;
}

int __MdsV4l2ElemEixt(MdsV4l2Elem* vElem)
{
        int ret = 0;

        CFFdeventsDel(vElem->server->fdevents, &vElem->fdEvent);
        CFFdeventExit(&vElem->fdEvent);
        switch (vElem->req.memory) {
                case V4L2_MEMORY_MMAP:
                        ret |= __MdsV4l2ElemExitMemMap(vElem);
                        break;
                case V4L2_MEMORY_USERPTR:
                        ret |= __MdsV4l2ElemExitUserPtr(vElem);
                        break;
                case V4L2_MEMORY_OVERLAY:
                        ret |= -1;
                        break;
        }
        close(vElem->fd);
        return ret;
}

/*
{
        "class": "V4L2_ELEM",
    "name" : "composit0",
    "device" : "/dev/video0",
    "input" : 1,
    "standard" : "",
    "format" : {
        "type" : "V4L2_BUF_TYPE_VIDEO_CAPTURE",
        "width" : 640,
        "height" : 480,
        "pixelformat" : "V4L2_PIX_FMT_UYVY",
        "field" : "V4L2_FIELD_INTERLACED"
    },
    "req_bufs" : {
        "count" : 4,
        "type" : "V4L2_BUF_TYPE_VIDEO_CAPTURE",
        "memory" : "V4L2_MEMORY_MMAP"
    }
}
*/
int MdsV4l2ElemInitByJConf(MdsV4l2Elem* vElem, MDSServer* svr, CFJson* jConf)
{
        const char* tmpStr;
        CFJson* tmpConf;
        int tmpInt;
        uint64 tmpInt64;

    /* device */
    if(NULL == (tmpStr = CFJsonObjectGetString(jConf, "device"))){
        MDS_ERR_OUT(ERR_OUT, "Please set v4l2 device name correctly in configuration\n");
    }
    CFStringInit(&vElem->device, tmpStr);
    if (CFJsonObjectGetInt(jConf, "input", &tmpInt)) {
        MDS_ERR_OUT(ERR_NAME_STR_EXIT, "Get v4l2 input idx failed\n");
    }
    vElem->inputIdx = tmpInt;
    if (CFJsonObjectGetIdFromString(jConf, "standard", V4l2StdMaps, &tmpInt64)) {
        MDS_ERR("Get v4l2 standard failed\n");
        vElem->std = V4L2_STD_UNKNOWN;
    } else {
        vElem->std = tmpInt64;
    }
    tmpConf = CFJsonObjectGet(jConf, "format");
    if(!tmpConf){
        MDS_ERR_OUT(ERR_NAME_STR_EXIT, "Get v4l2 format failed\n");
    }
    memset(&vElem->format, 0, sizeof(struct v4l2_format));
    /* format.type */
    if(-1 == CFJsonObjectGetIdFromString(tmpConf, "type", V4l2BufTypeMaps, &tmpInt64)){
        MDS_ERR_OUT(ERR_NAME_STR_EXIT, "Get v4l2 format.type failed\n");
    }
    vElem->format.type = tmpInt64;
    /* format.pixelformat */
    if(-1 == CFJsonObjectGetIdFromString(tmpConf, "pixelformat", V4l2PixFmtMaps, &tmpInt64)){
        MDS_ERR_OUT(ERR_NAME_STR_EXIT, "Get v4l2 format.pixelformat failed\n");
    }
    vElem->format.fmt.pix.pixelformat = tmpInt64;
    /* format.field */
    if(-1 == CFJsonObjectGetIdFromString(tmpConf, "field", V4l2FieldMaps, &tmpInt64)){
        MDS_MSG("Get v4l2 format.field failed, using default: V4L2_FIELD_ANY\n");
        vElem->format.fmt.pix.field = V4L2_FIELD_ANY;
    } else {
        vElem->format.fmt.pix.field = tmpInt64;
    }
    /* format.width */
    if(-1 == CFJsonObjectGetInt(tmpConf, "width", &tmpInt)){
        MDS_ERR_OUT(ERR_NAME_STR_EXIT, "Get v4l2 format.width failed\n");
    }
    vElem->format.fmt.pix.width = tmpInt;
    /* format.height */
    if(-1 == CFJsonObjectGetInt(tmpConf, "height", &tmpInt)){
        MDS_ERR_OUT(ERR_NAME_STR_EXIT, "Get v4l2 format.height failed\n");
    }
    vElem->format.fmt.pix.height = tmpInt;
    tmpConf = CFJsonObjectGet(jConf, "req_bufs");
    if(!tmpConf){
        MDS_ERR_OUT(ERR_NAME_STR_EXIT, "Get v4l2 req_bufs failed\n");
    }
    memset(&vElem->req, 0, sizeof(vElem->req));
    /* req_bufs.type */
    if(-1 == CFJsonObjectGetIdFromString(tmpConf, "type", V4l2BufTypeMaps, &tmpInt64)){
        MDS_ERR_OUT(ERR_NAME_STR_EXIT, "Get v4l2 req_bufs.type failed\n");
    }
    vElem->req.type = tmpInt64;
    /* req_bufs.memory */
    if(-1 == CFJsonObjectGetIdFromString(tmpConf, "memory", V4l2BufMemMaps, &tmpInt64)){
        MDS_ERR_OUT(ERR_NAME_STR_EXIT, "Get v4l2 req_bufs.memory failed\n");
    }
    vElem->req.memory = tmpInt64;
    /* req_bufs.count */
    if(-1 == CFJsonObjectGetInt(tmpConf, "count", &tmpInt)){
        MDS_ERR_OUT(ERR_NAME_STR_EXIT,  "Get v4l2 req_bufs.count failed\n");
    }
    vElem->req.count = tmpInt;
    if (-1 == __MdsV4l2ElemInit(vElem)) {
        MDS_ERR_OUT(ERR_NAME_STR_EXIT, "Init v4l2 device failed\n");
    }
    if (!(tmpStr=CFJsonObjectGetString(jConf, "name"))) {
        tmpStr = CFStringGetStr(&vElem->device);
    }
    if (-1 == MDSElemInit((MDSElem*)vElem, svr, &_V4l2Class, tmpStr,
                __MDSV4l2ElemProcess,
                __MDSV4l2ElemAddAsGuest, __MDSV4l2ElemAddAsVendor,
                __MDSV4l2ElemRemoveAsGuest, __MDSV4l2RemoveAsVendor)) {
        MDS_ERR_OUT(ERR_EXIT_V4L2_ELEM, "Init MdsElem failed\n");
    }
    return 0;
ERR_EXIT_V4L2_ELEM:
        __MdsV4l2ElemEixt(vElem);
ERR_NAME_STR_EXIT:
        CFStringExit(&vElem->device);
ERR_OUT:
        return -1;
}

int MdsV4l2ElemExit(MdsV4l2Elem* vElem)
{
        int ret = 0;

        ret |= __MdsV4l2ElemEixt(vElem);
        ret |= CFStringExit(&vElem->device);
        ret |= MDSElemExit((MDSElem*)vElem);
        return ret;
}

MdsV4l2Elem* MdsV4l2ElemNewByJConf(MDSServer* svr, CFJson* jConf)
{
    MdsV4l2Elem* ret;

    if(!(ret = (MdsV4l2Elem*)malloc(sizeof(MdsV4l2Elem)))){
        MDS_ERR_OUT(ERR_OUT, "Malloc failed: %s\n", strerror(errno));
    }
    if (MdsV4l2ElemInitByJConf(ret, svr, jConf)) {
        MDS_ERR_OUT(ERR_OUT_FREE, "Init v4l2 element failed\n");
    }
    return ret;
ERR_OUT_FREE:
    free(ret);
ERR_OUT:
    return NULL;

}

int MdsV4l2ElemDestroy(MdsV4l2Elem* vElem)
{
        int ret = 0;

        ret |= MdsV4l2ElemExit(vElem);
        free(vElem);
        return ret;
}

static MDSElem* _V4l2ElemRequested(MDSServer* svr, CFJson* jConf)
{
        MDSElem *ret;
        int i;
        const char* tmpCStr;

        errno = -EINVAL;
        if (!svr || !jConf) {
                MDS_ERR_OUT(ERR_OUT, "\n");
        }
        tmpCStr = CFJsonObjectGetString(jConf, "device");
        if (!tmpCStr) {
            MDS_ERR_OUT(ERR_OUT, "\n");
        }
        for (i=0; i<MAX_V4L2_ELEMS; i++) {
            if (v4l2.v4l2Elems[i]
                    && !strcmp(CFStringGetStr(&((MdsV4l2Elem*)v4l2.v4l2Elems[i])->device), tmpCStr)) {
                MDS_MSG("The v4l2 device: %s has been requested already.", tmpCStr);
                return (MDSElem*)v4l2.v4l2Elems[i];
            }
        }
        ret = (MDSElem*)MdsV4l2ElemNewByJConf(svr, jConf);
        if (!ret) {
                MDS_ERR_OUT(ERR_OUT, "\n");
        }
        for (i=0; i<MAX_V4L2_ELEMS; i++) {
                if (!v4l2.v4l2Elems[i]) {
                    v4l2.v4l2Elems[i] = (MdsV4l2Elem*)ret;
                    break;
                }
        }
        return ret;
ERR_OUT:
        return NULL;
}

static int _V4l2ElemReleased(MDSElem* elem)
{
    int i;

        assert(elem);
        for (i=0; i<MAX_V4L2_ELEMS; i++) {
            if (v4l2.v4l2Elems[i] == (MdsV4l2Elem*)elem) {
                v4l2.v4l2Elems[i] = NULL;
                break;
            }
        }
        return MdsV4l2ElemDestroy((MdsV4l2Elem*)elem);
}

static int V4l2PlugInit(MDSPlugin* plg, MDSServer* svr)
{
    MDS_MSG("Initiating plugin: "MDS_V4L2_PLUGIN_NAME"\n");
        return MDSServerRegistElemClass(svr, &_V4l2Class);
}

static int V4l2PlugExit(MDSPlugin* plg, MDSServer* svr)
{
    MDS_MSG("Exiting plugin: "MDS_V4L2_PLUGIN_NAME"\n");

        return MDSServerAbolishElemClass(svr, &_V4l2Class);
}

