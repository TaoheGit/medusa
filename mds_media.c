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

int MdsImgGetBitsPerPix(MdsPixFmt pixFmt)
{
    switch (pixFmt) {
	case MDS_PIX_FMT_SBGGR8:
	case MDS_PIX_FMT_NV12:
	    return 8;
	case MDS_PIX_FMT_NV16:
	case MDS_PIX_FMT_NV61:
	case MDS_PIX_FMT_YUYV:
	case MDS_PIX_FMT_UYVY:
	case MDS_PIX_FMT_SBGGR16:
	    return 2<<3;
	case MDS_PIX_FMT_H264:
	    return 2<<3;
	case MDS_PIX_FMT_MPEG4:
	    return 2<<3;
	case MDS_PIX_FMT_RGB24:
	    return 3<<3;
	case MDS_PIX_FMT_COUNT:
	    break;
	case MDS_PIX_FMT_INVALID:
	    break;
	default:
	    break;
    }
    return -1;
}

int MdsImgGetImgBufSize(MdsPixFmt pixFmt, int width, int height)
{
    switch (pixFmt) {
	case MDS_PIX_FMT_SBGGR8:
	case MDS_PIX_FMT_NV12:
	    return width*height*1;
	case MDS_PIX_FMT_NV16:
	case MDS_PIX_FMT_NV61:
	case MDS_PIX_FMT_YUYV:
	case MDS_PIX_FMT_UYVY:
	case MDS_PIX_FMT_SBGGR16:
	    return width*height*2;
	case MDS_PIX_FMT_H264:
	    return width*height*2;
	case MDS_PIX_FMT_MPEG4:
	    return width*height*2;
	case MDS_PIX_FMT_RGB24:
	    return width*height*3;
	case MDS_PIX_FMT_COUNT:
	    break;
	case MDS_PIX_FMT_INVALID:
	    break;
	default:
	    break;
    }
    return -1;
}

static uint32 _v4l2_mds_pix_fmt_map[MDS_PIX_FMT_COUNT] = {
    [MDS_PIX_FMT_NV16] =  V4L2_PIX_FMT_NV16,   /* YUV422 */
    [MDS_PIX_FMT_NV61] =  V4L2_PIX_FMT_NV61,   /* YUV422 */
    [MDS_PIX_FMT_YUYV] = V4L2_PIX_FMT_YUYV,
    [MDS_PIX_FMT_UYVY] = V4L2_PIX_FMT_UYVY,
    [MDS_PIX_FMT_SBGGR8] = V4L2_PIX_FMT_SBGGR8, 
    [MDS_PIX_FMT_SBGGR16] = V4L2_PIX_FMT_SBGGR16,
    [MDS_PIX_FMT_RGB24] = V4L2_PIX_FMT_RGB24,
    [MDS_PIX_FMT_NV12] = V4L2_PIX_FMT_NV12,
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


int MdsImgConvFmtSbggr8ToRgb24(uint8_t* dst, const uint8_t* src, int width, int height)
{
    /* all blue place */
    /* row 1 */

    int i, j;
    int idx, dIdx;
    int lineEndIdx, leftColEndIdx, rightColEndIdx;
    int tmp;
    int dWidth;
    int leftColEndIdxP1;

    if (!dst || !src || width<4 || height<4) {
        MDS_ERR("\n");
        return -1;
    }
#if 1
    dWidth = width<<1;
    lineEndIdx = width - 1;
    leftColEndIdx = width*(height-1);
    rightColEndIdx = width*height-1;
    leftColEndIdxP1 = leftColEndIdx+1;
    
    /* top left pixel B */    
    dst[0] = src[0+width+1];
    tmp = src[1];
    tmp += src[width];
    dst[0+1] = tmp>>1;
    dst[0+2] = src[0];
    
    /* top right pixel G */
    idx = lineEndIdx;
    dst[idx*3] = src[idx+width];
    dst[idx*3+1] = src[idx];
    dst[idx*3+2] = src[idx-1];
    
    /* bottom left pixel G */
    idx = leftColEndIdx;
    dst[idx*3] = src[idx+1];
    dst[idx*3+1] = src[idx];
    dst[idx*3+2] = src[idx-width];
    
    /* bottome right pixel R */
    idx = rightColEndIdx;
    dst[idx*3] = src[idx];
    tmp = src[idx-1];
    tmp += src[idx-width];
    dst[idx*3+1] = tmp>>1;
    dst[idx*3+2] = src[idx-width-1];
    
    /* top side */
    for (i=1; i<lineEndIdx; i+=2) {
        /* G */
        dst[i] = src[i+width];
        dst[i+1] = src[i];
        tmp = src[i-1];
        tmp += src[i+1];
        dst[i+2] = tmp>>1;
        
        /* B */
        idx = i+1;
        dIdx = idx*3;
        tmp = src[idx-1+width];
        tmp += src[idx+1+width];
        dst[dIdx] = tmp>>1;
        tmp = src[idx-1];
        tmp += src[idx+1];
        tmp += src[idx+width];
        tmp += src[idx+1];
        dst[dIdx+1] = tmp>>2;
        dst[dIdx+2] = src[idx];
    }
    /* left side */
    for (i=width; i<leftColEndIdx; i+=dWidth) {
        /* G */
        dIdx = i*3;
        dst[dIdx] = src[i+1];
        dst[dIdx+1] = src[i];
        tmp = src[i-width];
        tmp += src[i+width];
        dst[dIdx+2] = tmp>>1;
        /* B */
        idx = i+width;
        dIdx = idx*3;
        tmp = src[idx+1-width];
        tmp += src[idx+1+width];
        dst[dIdx] = tmp>>1;
        tmp = src[idx-width];
        tmp += src[idx+width];
        tmp += src[idx+1];
        tmp += src[idx+1];
        dst[dIdx+1] = tmp>>2;
        dst[dIdx+2] = src[idx];
    }
    
    /* right side */
    for (i=width+width-1; i<rightColEndIdx; i+=dWidth) {
        /* R */
        dIdx = i*3;
        dst[dIdx] = src[i];
        tmp = src[i-width];
        tmp += src[i+width];
        tmp += src[i-1];
        tmp += src[i-1];
        dst[dIdx+1] = tmp>>2;
        tmp = src[i-1-width];
        tmp += src[i-1+width];
        dst[dIdx+2] = tmp>>1;
        
        /* G */
        idx = i+width;
        dIdx = idx*3;
        tmp = src[idx-width];
        tmp += src[idx+width];
        dst[dIdx] = tmp>>1;
        dst[dIdx+1] = src[idx];
        dst[dIdx+2] = src[idx-1];
    }
    
    /* buttom side */
    for (i=leftColEndIdxP1; i<rightColEndIdx; i+=2) {
        /* R */
        dIdx = i*3;
        dst[dIdx] = src[i];
        tmp = src[i-1];
        tmp += src[i+1];
        tmp += src[i-width];
        tmp += src[i+1];
        dst[dIdx+1] = tmp>>2;
        tmp = src[i-width-1];
        tmp += src[i-width+1];
        dst[dIdx+2] = tmp>>1;
        /* G */
        idx = i+1;
        dIdx = idx*3;
        tmp = src[idx-1];
        tmp += src[idx+1];
        dst[dIdx] = tmp>>1;
        dst[dIdx+1] = src[idx];
        dst[dIdx+2] = src[idx-width];
    }
    
    /* center */
    for (i=width+1; i<leftColEndIdxP1; i+=dWidth) {
        int lEnd = i+width-2;
        for (j=i; j<lEnd; j+=2) {
            /* R */
            dIdx = j*3;
            dst[dIdx] = src[j];
            tmp = src[j-1];
            tmp += src[j+1];
            tmp += src[j-width];
            tmp += src[j+width];
            dst[dIdx+1] = tmp>>2;
            tmp = src[j-width-1];
            tmp += src[j-width+1];
            tmp += src[j+width-1];
            tmp += src[j+width+1];;
            dst[dIdx+2] = tmp>>2;
            
            /* G */
            idx = j+1;
            dIdx = idx*3;
            tmp = src[idx-1];
            tmp += src[idx+1];
            dst[dIdx] = tmp>>1;
            dst[dIdx+1] = src[idx];
            tmp = src[idx-width];
            tmp += src[idx+width];
            dst[dIdx+2] = tmp>>1;
        }
        lEnd = i+width+width-2;
        for (j=i+width; j<lEnd; j+=2) {
            /* G */
            dIdx = j*3;
            tmp = src[j-width];
            tmp += src[j+width];
            dst[dIdx] = tmp>>1;
            dst[dIdx+1] = src[j];
            tmp = src[j-1];
            tmp += src[j+1];
            dst[dIdx+2] = tmp>>1;
            
            /* B */
            idx = j+1;
            dIdx = idx*3;
            tmp = src[idx-width-1];
            tmp += src[idx-width+1];
            tmp += src[idx+width-1];
            tmp += src[idx+width+1];
            dst[dIdx] = tmp>>2;
            tmp = src[idx-1];
            tmp += src[idx+1];
            tmp += src[idx-width];
            tmp += src[idx+width];
            dst[dIdx+1] = tmp>>2;
            dst[dIdx+2] = src[idx];
        }
    }
#endif
    return 0;
}

int MdsImgConvFmt(void* dst, void* src, MdsPixFmt dstFmt, MdsPixFmt srcFmt, int width, int height)
{
    if (srcFmt == MDS_PIX_FMT_SBGGR8 && dstFmt == MDS_PIX_FMT_RGB24) {
        return MdsImgConvFmtSbggr8ToRgb24(dst, src, width, height);
    } else {
        MDS_ERR("This colorspace convertion not supported yet!! Please fix me\n");
        return -1;
    }
}

int MdsImgBufConvFmt(MdsImgBuf* dst, MdsImgBuf* src)
{
    if (dst->width-src->width || dst->height-src->height) { 
        MDS_ERR("\n");
        return -1;
    }
    return MdsImgConvFmt(dst->bufPtr, src->bufPtr, dst->pixFmt, src->pixFmt, dst->width, dst->height);
}
