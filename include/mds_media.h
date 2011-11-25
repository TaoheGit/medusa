/* COPYRIGHT_CHUNFENG */
#include <linux/videodev.h>
#include <linux/videodev2.h>
#include <cf_common.h>
#ifndef _MDS_MEDIA_H_
#define _MDS_MEDIA_H_
typedef enum {
    MDS_VID_STD_INVALID = -1,
    MDS_VID_STD_CIF = 0, //{352, 288},
    MDS_VID_STD_SIF_NTSC,   //{352, 240}
    MDS_VID_STD_SIF_PAL,    //{352, 288}
    MDS_VID_STD_D1_NTSC,    //{720, 480}
    MDS_VID_STD_D1_PAL, //{720, 576}
    MDS_VID_STD_QVGA,   //{320, 240}
    MDS_VID_STD_VGA,    //{640, 480}
    MDS_VID_STD_480P, //{720, 480}
    MDS_VID_STD_576P, //{720, 576}
    MDS_VID_STD_720P, //{1280, 720}
    MDS_VID_STD_720P_60, //{1280, 720}
    MDS_VID_STD_720P_50, //{1280, 720}
    MDS_VID_STD_720P_30, //{1280, 720}
    MDS_VID_STD_1080I, //{1920, 1080}
    MDS_VID_STD_1080I_30, //{1920, 1080}
    MDS_VID_STD_1080I_25, //{1920, 1080}
    MDS_VID_STD_1080I_60, //{1920, 1080}
    MDS_VID_STD_1080P, //{1920, 1080}
    MDS_VID_STD_1080P_30, //{1920, 1080}
    MDS_VID_STD_1080P_25, //{1920, 1080}
    MDS_VID_STD_1080P_24, //{1920, 1080}
    MDS_VID_STD_1080P_60, //{1920, 1080}
    MDS_VID_STD_1080P_50, //{1920, 1080}
	MDS_VID_STD_CUSTOM,
    MDS_VID_STD_COUNT 
} MdsVidStd;

int MdsVidStdGetWidth(MdsVidStd std);
int MdsVidStdGetHeight(MdsVidStd std);
MdsVidStd MdsVidGetStdByRes(int width, int height);

typedef enum {
	MDS_PIX_FMT_INVALID  = -1,
	MDS_PIX_FMT_NV16,   /* YUV422 */
	MDS_PIX_FMT_NV61,   /* YUV422 */
	MDS_PIX_FMT_YUYV,
	MDS_PIX_FMT_H264,
	MDS_PIX_FMT_MPEG4,
	MDS_PIX_FMT_COUNT
}MdsPixFmt;
MdsPixFmt MdsV4l2PixFmtToMdsPixFmt(uint32 v4l2PixFmt);
uint32 MdsMdsPixFmtToV4l2PixFmt(MdsPixFmt mdsPixFmt);

typedef struct mds_img_buf MdsImgBuf;
struct mds_img_buf{
	MdsPixFmt pixFmt;
	int width;
	int height;
	void* bufPtr;
	int bufSize;	/* Maybe bigger than actual size needed */
};
int MdsImgBufInit(MdsImgBuf* buf, MdsPixFmt pixFmt, int width, int height, 
		void* bufPtr, int bufSize);
MdsImgBuf* MdsImgBufNew(MdsPixFmt pixFmt, int width, int height, 
		void* bufPtr, int bufSize);
#define MdsImgBufSetBufPtr(__buf, __bufPtr)  do {(__buf)->bufPtr =  (__bufPtr)}while(0)
#define MdsImgBufGetPtr(__buf) ((__buf)->bufPtr)
void MdsImgBufExit(MdsImgBuf* buf);
void MdsImgBufFree(MdsImgBuf* buf);
int MdsImgGetImgSize(MdsPixFmt pixFmt, int width, int height);
#define MdsImgBufGetImgSize(__buf) (MdsImgGetImgSize((__buf)->pixFmt, (__buf)->width, (__buf)->height))
#define MdsImgBufGetBufSize(__buf) ((__buf)->bufSize)
#endif

