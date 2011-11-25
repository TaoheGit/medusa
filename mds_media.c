/* COPYRIGHT_CHUNFENG */
#include <string.h>
#include "mds_log.h"
#include "mds_media.h"

static struct  {int width; int height;} _VideoStdResAry[MDS_VID_STD_COUNT] = {
    [MDS_VID_STD_CIF] = {352, 288},
    [MDS_VID_STD_SIF_NTSC] = {352, 240},
    [MDS_VID_STD_SIF_PAL] = {352, 288},
    [MDS_VID_STD_D1_NTSC] = {720, 480},
    [MDS_VID_STD_D1_PAL] = {720, 576},
    [MDS_VID_STD_QVGA] = {320, 240},
    [MDS_VID_STD_VGA] = {640, 480},
    [MDS_VID_STD_480P] = {720, 480},
    [MDS_VID_STD_576P] = {720, 576},
    [MDS_VID_STD_720P] = {1280, 720},
    [MDS_VID_STD_720P_60] = {1280, 720},
    [MDS_VID_STD_720P_50] = {1280, 720},
    [MDS_VID_STD_720P_30] = {1280, 720},
    [MDS_VID_STD_1080I] = {1920, 1080},
    [MDS_VID_STD_1080I_30] = {1920, 1080},
    [MDS_VID_STD_1080I_25] = {1920, 1080},
    [MDS_VID_STD_1080I_60] = {1920, 1080},
    [MDS_VID_STD_1080P] = {1920, 1080},
    [MDS_VID_STD_1080P_30] = {1920, 1080},
    [MDS_VID_STD_1080P_25] = {1920, 1080},
    [MDS_VID_STD_1080P_24] = {1920, 1080},
    [MDS_VID_STD_1080P_60] = {1920, 1080},
    [MDS_VID_STD_1080P_50] = {1920, 1080}
};

int MdsVidStdGetWidth(MdsVidStd std)
{
	if (std<0 || std>MDS_VID_STD_COUNT) {
		return -1;
	}
	return _VideoStdResAry[std].width;
}

int MdsVidStdGetHeight(MdsVidStd std)
{
	if (std<0 || std>MDS_VID_STD_COUNT) {
		return -1;
	}
	return _VideoStdResAry[std].height;
}

MdsVidStd MdsVidGetStdByRes(int width, int height)
{
	int i;
	
	for (i=0; i<MDS_VID_STD_COUNT; i++) {
		if (_VideoStdResAry[i].width == width && _VideoStdResAry[i].height == height) {
			return i;
		}
	}
	return -1;
}

int MdsImgGetImgSize(MdsPixFmt pixFmt, int width, int height)
{
	switch (pixFmt) {
		case MDS_PIX_FMT_NV16:
        case MDS_PIX_FMT_NV61:
        case MDS_PIX_FMT_YUYV:
			return width*height*2;
        case MDS_PIX_FMT_H264:
            return width*height*2;
        case MDS_PIX_FMT_MPEG4:
            return width*height*2;
        case MDS_PIX_FMT_COUNT:
            break;
		case MDS_PIX_FMT_INVALID:
		    break;
	}
	return -1;
}

static uint32 _v4l2_mds_pix_fmt_map[MDS_PIX_FMT_COUNT] = {
	[MDS_PIX_FMT_NV16] =  V4L2_PIX_FMT_NV16,   /* YUV422 */
	[MDS_PIX_FMT_NV61] =  V4L2_PIX_FMT_NV61,   /* YUV422 */
	[MDS_PIX_FMT_YUYV] = V4L2_PIX_FMT_YUYV
};

MdsPixFmt MdsV4l2PixFmtToMdsPixFmt(uint32 v4l2PixFmt)
{
	int i;
	
	for (i=0; i<MDS_PIX_FMT_COUNT; i++) {
		if (_v4l2_mds_pix_fmt_map[i] == v4l2PixFmt) {
			return i;
		}
	}
	return MDS_PIX_FMT_INVALID;
}

uint32 MdsMdsPixFmtToV4l2PixFmt(MdsPixFmt mdsPixFmt)
{
	if (mdsPixFmt<0 || mdsPixFmt>MDS_PIX_FMT_COUNT) {
		return 0;
	} else {
		return _v4l2_mds_pix_fmt_map[mdsPixFmt];
	}
}

int MdsImgBufInit(MdsImgBuf* buf, MdsPixFmt pixFmt, int width, int height, void* bufPtr, int bufSize)
{	
	if (pixFmt<0 || pixFmt>MDS_PIX_FMT_COUNT 
			|| buf->width<0 || buf->height<0
			|| !bufPtr || bufSize <0) {
		MDS_ERR_OUT(ERR_OUT, "\n");
	}
	buf->pixFmt = pixFmt;
	buf->width = width;
	buf->height = height;
	buf->bufPtr = bufPtr;
	buf->bufSize = bufSize;
	return 0;
ERR_OUT:
	return -1;
}

void MdsImgBufExit(MdsImgBuf* buf)
{
    return;
}

MdsImgBuf* MdsImgBufNew(MdsPixFmt pixFmt, int width, int height, void* bufPtr, int bufSize)
{
	MdsImgBuf* buf;
	if (!(buf=malloc(sizeof(*buf)))) {
		MDS_ERR_OUT(ERR_OUT, "\n");
	}
	if (MdsImgBufInit(buf, pixFmt, width, height, bufPtr, bufSize)) {
		MDS_ERR_OUT(ERR_FREE_BUF, "\n");
	}
ERR_FREE_BUF:
	free(buf);
ERR_OUT:
	return NULL;
}

void MdsImgBufFree(MdsImgBuf* buf)
{
    free(buf);
}

