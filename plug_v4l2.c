#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <errno.h>
#include <linux/videodev.h>
#include <linux/videodev2.h>
#include <cf_string.h>
#include <cf_common.h>
#include "plug_v4l2.h"
#include "mds_media.h"

#define MDS_V4L2_ELEM_CLASS_NAME	"V4L2_ELEM"

typedef struct v4l2_elem{
	MDS_ELEM_MEMBERS;
	int inputIdx;
	v4l2_std_id std;
    struct v4l2_format format;
    struct v4l2_requestbuffers req;
    CFString device;
    int fd;
    struct {void* start, size_t size} *buffers;
    MdsImgBuf* imgBufs;
}MdsV4l2Elem;

#define MDS_V4L2_PLUGIN_NAME	"v4l2"
typedef struct mds_v4l2_plug{
    MDS_PLUGIN_MEMBERS;
}MDSV4l2Plug;

static int V4l2PlugInit(MDSPlugin* this, MDSServer* svr);
static int V4l2PlugExit(MDSPlugin* this, MDSServer* svr);
MDSV4l2Plug v4l2 = {
    .name = MDS_V4L2_PLUGIN_NAME,
    .init = V4l2PlugInit,
    .exit = V4l2PlugExit
};

static MDSElem* _V4l2ElemRequested(MDSServer* svr, CFJson* jConf);
static int _V4l2ElemReleased(MDSServer* svr, MDSElem* elem);
MdsElemClass _V4l2Class = {
	.name = MDS_V4L2_ELEM_CLASS_NAME,
	.request = _V4l2ElemRequested,
	.release = _V4l2ElemReleased,
};

#define ID_TO_ID_STR(_id)   {(unsigned long long)_id, "_id"}
typedef struct {
    unsigned long long id; 
    const char* str
}IdStrMap;

int StrToId(IdStrMap* maps, const char* str, int* id)
{
    int i;
    
    for(i=0; maps[i].str; i++){
        if(!strcmp(str, maps[i].str)){
            *id = maps[i].id;
        }
    }
    CF_ERR("%s not found in id str map.\n", str);
    return -1;
}

const char* IdToStr(IdStrMap* maps, int id)
{   
    int i;
    
    for(i=0; maps[i].str; i++){
        if(id = maps[i].id){
            return maps[i].str;
        }
    }
    CF_ERR("id=%d not found in id str map.\n", id);
    return NULL;
}

static int CFJsonObjectGetIdFromString(json_object* conf, const char* key, IdStrMap* maps, int* id)
{
    const char* tmpC;
    
    if(NULL == (tmpC = json_object_object_get_string(conf, key))){
        CF_ERR("Get config key=%s failed\n", key);
        return -1;
    }
    if(-1 == StrToId(maps, tmpC, id)){
        CF_ERR("%s not support\n", tmpC);
        return -1;
    }    
}

static IdStrMap V4l2StdMaps[] = {
	ID_TO_ID_STR(),
	{-1, NULL}
}

static IdStrMap V4l2BufTypeMaps[]={
    ID_TO_ID_STR(V4L2_BUF_TYPE_VIDEO_CAPTURE),
    {-1, NULL}
};

static IdStrMap V4l2PixFmtMaps[]={
    ID_TO_ID_STR(V4L2_PIX_FMT_UYVY),
    ID_TO_ID_STR(V4L2_PIX_FMT_NV16),
    ID_TO_ID_STR(V4L2_PIX_FMT_NV61)
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

static int MDSV4l2ElemAddAsGuest(MDSElem* this, MDSElem* vendorElem)
{
	/* todo */
}

static int MDSV4l2ElemAddAsVendor(MDSElem* this, MDSElem* guestElem)
{
	/* todo */
}

static int MDSV4l2ElemProcess(MDSCmdSvrDataConn* dataConn, void* usrData)
{
	/* todo */
}

static int MDSV4l2ElemRemoveAsGuest(MDSElem* this, MDSElem* vendorElem)
{
	/* todo */
}

static int MDSCmdV4l2RemoveAsVendor(MDSElem* this, MDSElem* guestElem)
{
	/* todo */
}

int __MdsV4l2ElemInitMemMap(MdsV4l2Elem* vElem)
{
	struct v4l2_buffer buf;
	void *tmpPtr;
	int iForReqBuf = 0;
	
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
				MdsV4l2PixFmtToMdsPixFmt(vElem->format.fmt.pix.pixelformat) 
				vElem->format.fmt.pix.width, 
				vElem->format.fmt.pix.height,
				tmpPtr, buf->length)) {
			munmap(tmpPtr, buf->length);
			MDS_ERR_OUT(ERR_UNQUERY_BUFS);	
		}
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
		munmap(vElem->imgBufs[iForReqBuf].bufPtr, vElem->imgBufs[iForReqBuf].size);
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
		ret |= MdsImgBufExit(&vElem->imgBufs[iForReqBuf]);
		ret |= munmap(vElem->imgBufs[iForReqBuf].bufPtr, vElem->imgBufs[iForReqBuf].size);
	}
	free(vElem->imgBufs);
	return ret;	
}

int __MdsV4l2ElemInitUserPtr(MdsV4l2Elem* vElem)
{
	/* todo */
}

int __MdsV4l2ElemExitUserPtr(MdsV4l2Elem* vElem)
{
	/* todo */
}

int __MdsV4l2ElemInit(MdsV4l2Elem* vElem)
{
	int idx;
	struct v4l2_format fmt;
	
	if ((vElem->fd = open(CFStringGetStr(vElem->devFile), O_RDONLY)) < 0) {
		MDS_ERR_OUT(ERR_OUT, "Open V4L2 device: %s failed\n", CFStringGetStr(vElem->devFile));	
	}
	if ((-1 == ioctl(vElem->fd, VIDIOC_S_INPUT, &vElem->inputIdx)) 
			|| (-1 == ioctl(vElem->fd, VIDIOC_G_INPUT, &idx))
			|| (idx != &vElem->inputIdx)) {
		MDS_ERR_OUT(ERR_CLOSE_FD, "Set V4L2 input index failed\n");
	}
	if ((-1 == ioctl(vElem->fd, VIDIOC_S_STD, &vElem->std))
			|| (-1 == ioctl(vElem->fd, VIDIOC_G_STD, &idx))
			|| (idx != &vElem->std)) {
		MDS_ERR_OUT(ERR_CLOSE_FD, "Set V4L2 standard failed\n");
	}
	if ((-1 == ioctl(vElem->fd, VIDIOC_S_FMT, &vElem->format))
			|| (-1 == ioctl(vElem->fd, VIDIOC_G_FMT, &fmt))
			|| (fmt.type != vElem->type)
			|| (fmt.pix.width != vElem->pix.width)
			|| (fmt.pix.height != vElem->pix.height)
			|| (fmt.pixelformat != vElem->pix.pixelformat)) {
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
	return 0;
ERR_CLOSE_FD:
	close(vElem->fd);
ERR_OUT:
	return -1;
}

int __MdsV4l2ElemEixt(MdsV4l2Elem* vElem)
{
	int ret;
	
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
	return 0;
}

/*
{
    "name" = "composit0",
    "device" = "/dev/video0",
    "input" = 1,
    "standard" = "",
    "format" = {
        "type" = "V4L2_BUF_TYPE_VIDEO_CAPTURE",
        "width" = 640,
        "height" = 480,
        "pixelformat" = "V4L2_PIX_FMT_UYVY",
        "field" = "V4L2_FIELD_INTERLACED"
    },
    "req_bufs" = {
        "count" = 4,
        "type" = "V4L2_BUF_TYPE_VIDEO_CAPTURE",
        "memory" = "V4L2_MEMORY_MMAP"
    }
}
*/
int MdsV4l2ElemInitByJConf(MdsV4l2Elem* vElem, MDSServer* svr, CFJson* jConf)
{
	const char* tmpStr;
	CFJson* tmpConf;
	
    /* device */
    if(NULL == (tmpStr = CFJsonObjectGetString(jConf, "device"))){
        MDS_ERR_OUT(ERR_OUT, "Please set v4l2 device name correctly in configuration\n");
    }
    CFStringSafeCp(&vElem->device, tmpStr);
    tmpConf = CFJsonObjectGet(jConf, "format");
    if(!tmpConf){
        MDS_ERR_OUT(ERR_NAME_STR_EXIT, "Get v4l2 format failed\n");
    }
    memset(&vElem->format, 0, sizeof(struct v4l2_format));
    /* format.type */
    if(-1 == CFJsonObjectGetIdFromString(tmpConf, "type", V4l2BufTypeMaps, &vElem->format.type)){
        MDS_ERR_OUT(ERR_NAME_STR_EXIT, "Get v4l2 format.type failed\n");
    }
    /* format.pixelformat */
    if(-1 == CFJsonObjectGetIdFromString(tmpConf, "pixelformat", V4l2PixFmtMaps, &vElem->format.pix.pixelformat)){
        CF_ERR("Get v4l2 format.pixelformat failed\n");
        return NULL;
    }
    /* format.field */
    if(-1 == CFJsonObjectGetIdFromString(tmpConf, "field", V4l2, &vElem->format.pix.field)){
        MDS_MSG("Get v4l2 format.field failed, using default: V4L2_FIELD_ANY\n");
    }
    /* format.width */
    if(-1 == CFJsonObjectGetIdFromString(tmpConf, "width", &vElem->format.pix.width)){
        MDS_ERR_OUT(ERR_NAME_STR_EXIT, "Get v4l2 format.width failed\n");
    }
    /* format.height */
    if(-1 == CFJsonObjectGetIdFromString(tmpConf, "height", &vElem->format.pix.height)){
        MDS_ERR_OUT(ERR_NAME_STR_EXIT, "Get v4l2 format.height failed\n");
    }
    tmpConf = CFJsonObjectGet(jConf, "req_bufs");
    if(!tmpConf){
        MDS_ERR_OUT(ERR_NAME_STR_EXIT, "Get v4l2 req_bufs failed\n");
    }
    memset(&vElem->req, 0, sizeof(vElem->req));
    /* req_bufs.type */ 
    if(-1 == CFJsonObjectGetIdFromString(tmpConf, "type", V4l2BufTypeMaps, &vElem->req.type)){
        MDS_ERR_OUT(ERR_NAME_STR_EXIT, "Get v4l2 req_bufs.type failed\n");
    }
    /* req_bufs.memory */
    if(-1 == CFJsonObjectGetIdFromString(tmpConf, "memory", V4l2BufMemMaps, &vElem->req.memory)){
        MDS_ERR_OUT(ERR_NAME_STR_EXIT, "Get v4l2 req_bufs.memory failed\n");
    } 
    /* req_bufs.count */
    if(-1 == CFJsonObjectGetIdFromString(tmpConf, "count", &vElem->req.count)){
        MDS_ERR_OUT(ERR_NAME_STR_EXIT, 	"Get v4l2 req_bufs.count failed\n");
    }
    if (-1 == __MdsV4l2ElemInit(vElem)) {
    	MDS_ERR_OUT(ERR_NAME_STR_EXIT, "Init v4l2 device failed\n");
    }
    if (-1 == MDSElemInit(vElem, svr, _V4l2Class, CFStringGetStr(vElem->device),
    		__MDSV4l2ElemProcess, __MDSV4l2ElemAddedAsGuest, __MDSV4l2ElemAddedAsVendor)) {
    	MDS_ERR_OUT(ERR_EXIT_V4L2_ELEM);		
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
	
	ret |= MDSElemExit((MDSElem*)vElem);
	ret |= __MdsV4l2ElemEixt(vElem);
	ret |= CFStringExit(vElem->device);
	return ret;
}

V4l2Sourcer* MdsV4l2ElemNewByJConf(MDSServer* svr, json_object* conf)
{
    const char* device;
    struct v4l2_format format;
    struct v4l2_requestbuffers req;
    json_object* tmpConf;
    json_object* itemConf;
    const char* tmpC;
    int tmpInt;
    V4l2Elem* ret;

    if(!cf_is_chr_file(device)){
        CF_ERR("%s not a character device file\n", device);
        return NULL;
    }
    if(!ret = (MdsV4l2Elem*)malloc(sizeof(MdsV4l2Elem))){
        CF_ERR(strerror(errno));
        return NULL;
    }
    ret.fd = open(device, O_RDWR);
    if(ret.fd < 0){
        CF_ERR("open device %s failed: %s\n", device);
        goto ERR_OUT_FREE;
    }
    switch(req.memory){
        case V4L2_MEMORY_MMAP:
            
            break;
        default:
            CF_ERR("doesn't support such stream I/O method\n");
            goto ERR_OUT_FREE;
    }
    return ret;
ERR_OUT_FREE:
    free(ret);
ERR_OUT:
    return NULL;
    
}

int MdsV4l2ElemDestroy(MdsV4l2Elem* vElem)
{
	int ret;
	
	ret |= MdsV4l2ElemExit(vElem);
	free(vElem);
	return ret;
}



static MDSElem* _V4l2ElemRequested(MDSServer* svr, CFJson* jConf)
{
	if (!svr || !jConf) {
		MDS_ERR_OUT(ERR_OUT, "\n");
	}
	return MdsV4l2ElemNewByJConf(svr, jConf);
ERR_OUT:
	return NULL;
}

static int _V4l2ElemReleased(MDSServer* svr, MDSElem* elem)
{
	if (!svr || !elem) {
		MDS_ERR_OUT(ERR_OUT, "\n");
	}
	return MdsV4l2ElemDestroy((MdsV4l2Elem*)elem);
ERR_OUT:
	return -1;
}

static int V4l2PlugInit(MDSPlugin* plg, MDSServer* svr)
{
    MP_CMD_MSG("Initiating plugin: "MDS_V4L2_PLUGIN_NAME"\n");
	return MDSServerRegistElemClass(svr, &_V4l2Class);
}

static int V4l2PlugExit(MDSPlugin* plg, MDSServer* svr)
{
    MP_CMD_MSG("Exiting plugin: "MDS_V4L2_PLUGIN_NAME"\n");
    
	return MDSServerAbolishElemClass(svr, &_V4l2Class);
}

