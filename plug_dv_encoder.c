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
//#define _DEBUG_
#include "mds_log.h"
#include "mds_media.h"
#include "mds_tools.h"
#include "plug_cmd.h"
#ifdef CONFIG_DM365_IPIPE
#include <media/davinci/imp_previewer.h>
#include <media/davinci/imp_resizer.h>
#include <media/davinci/dm365_ipipe.h>
#endif
#include <xdc/std.h>
#include <ti/sdo/ce/Engine.h>
#include <ti/sdo/ce/CERuntime.h>
#include <ti/sdo/dmai/Dmai.h>
#include <ti/sdo/dmai/BufferGfx.h>
#include <ti/sdo/dmai/Buffer.h>
#include <ti/sdo/dmai/Ccv.h>
#include <ti/sdo/dmai/Cpu.h>
#include <ti/sdo/dmai/Time.h>
#include <ti/sdo/dmai/BufTab.h>
#include <ti/sdo/dmai/Capture.h>
#include <ti/sdo/dmai/Framecopy.h>
#include <ti/sdo/dmai/BufferGfx.h>
#include <ti/sdo/dmai/ce/Venc1.h>
#define MDS_DV_ENCODER_CLASS_NAME   "DV_ENCODER"
#define MDS_MSG_TYPE_IMAGE  "Image"
#define MDS_DEFAULT_IDR_FRAME_INTERVAL  30
/* Align buffers to this cache line size (in bytes)*/
#define BUFSIZEALIGN            128

/* The input buffer height restriction */
#define CODECHEIGHTALIGN       16
#define MAX_CODEC_NAME_SIZE     30
#define MAX_ENGINE_NAME_SIZE    30
#define MAX_FILE_NAME_SIZE      100
typedef struct DV_ENCODER_elem {
    MDS_ELEM_MEMBERS;
    uint32_t frameCount;
    int width;
    int height;
    int bitRate;
    int inPixFmt;
    BOOL cache;
    Buffer_Handle hInBuf;
    Buffer_Handle hOutBuf;
    Cpu_Device             device;
    Engine_Handle          hEngine;
    Venc1_Handle           hVe1;
    VIDENC1_DynamicParams  dynParams;
    BOOL forceIdrFrame;
    int idrFrameInterval;
    BufTab_Handle          hBufTab;
    MdsImgBuf dstImgBuf;
    //Bool  writeReconFrames;
    Char  codecName[MAX_CODEC_NAME_SIZE];
    Char  engineName[MAX_ENGINE_NAME_SIZE];
} MdsDvEncoderElem;

#define MDS_DV_ENCODER_PLUGIN_NAME       "PLUG_DV_ENCODER"
typedef struct mds_dv_encoder_plug {
    MDS_PLUGIN_MEMBERS;
} MdsDvEncoderPlug;

static int _DvEncoderPlugInit(MDSPlugin* self, MDSServer* svr);
static int _DvEncoderPlugExit(MDSPlugin* self, MDSServer* svr);
MdsDvEncoderPlug dv_encoder = {
    .name = MDS_DV_ENCODER_PLUGIN_NAME,
    .init = _DvEncoderPlugInit,
    .exit = _DvEncoderPlugExit
};

static MDSElem* _DvEncoderElemRequested(MDSServer* svr, CFJson* jConf);
static int _DvEncoderElemReleased(MDSElem* elem);
MdsElemClass _DvEncoderClass = {
    .name = MDS_DV_ENCODER_CLASS_NAME,
    .request = _DvEncoderElemRequested,
    .release = _DvEncoderElemReleased,
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

/*
{
    "bit_rate":6000000,
    "idr_frame_interval":300
}
*/
static int __MdsDvEncoderProcess(MDSElem* self, MDSElem* vendor, MdsMsg* msg)
{
    MdsImgBuf *imgBuf, *dstImgBuf;
    MdsDvEncoderElem* dvEnc;
    void* inUsrPtr;
    Buffer_Handle hInBuf;
    Buffer_Handle hOutBuf;
    Buffer_Handle hFreeBuf;
    Venc1_Handle           hVe1;
    VIDENC1_Status          encStatus;
    XDAS_Int32              status;
    VIDENC1_Handle          hEncode;
    
    dvEnc = (MdsDvEncoderElem*)self;
    if (!strcmp(msg->type, MDS_MSG_TYPE_IMAGE)) {
        assert(dvEnc);
        imgBuf = (MdsImgBuf*)msg->data;
        dstImgBuf = &dvEnc->dstImgBuf;
        assert(imgBuf);
        hOutBuf = dvEnc->hOutBuf;
        assert(hOutBuf);
        inUsrPtr = MdsImgBufGetPtr(imgBuf);
        assert(inUsrPtr);
        hVe1 = dvEnc->hVe1;
        MDS_DBG("dv encoder processing\n")
    #if 0    
        struct timeval tv;
        gettimeofday(&tv, NULL);
        MDS_DBG("now: %lld-%lld\n", (long long)tv.tv_sec, (long long)tv.tv_usec);
    #endif

        /* Get a buffer for input */
        hInBuf = BufTab_getFreeBuf(dvEnc->hBufTab);
        if (hInBuf == NULL) {
            MDS_ERR_OUT(ERR_DC_ALL, "\n");
        }

        /* Read a yuv input frame */
        if (MdsImgBufGetPixFmt(imgBuf) != dvEnc->inPixFmt) {
            MDS_ERR_OUT(ERR_DC_ALL, "\n");
        }
        if ((Buffer_setUserPtr(hInBuf, inUsrPtr))) {
            MDS_ERR_OUT(ERR_DC_ALL, "DvEncoder Buffer_setUserPtr failed\n");
        }

        if (dvEnc->cache) {
            /*  
            *  To meet xDAIS DMA Rule 7, when input buffers are cached, we 
            *  must writeback the cache into physical memory.  Also, per DMA 
            *  Rule 7, we must invalidate the output buffer from
            *  cache before providing it to any xDAIS algorithm.
            */
            Memory_cacheWbInv(Buffer_getUserPtr(hInBuf),
                                Buffer_getSize(hInBuf));

            /* Per DMA Rule 7, our output buffer cache lines must be cleaned */
            Memory_cacheInv(Buffer_getUserPtr(hOutBuf),
                                Buffer_getSize(hOutBuf));
        }

        /* Make sure the whole buffer is used for input */
        BufferGfx_resetDimensions(hInBuf);

        if (!(dvEnc->frameCount%(dvEnc->idrFrameInterval)) || dvEnc->forceIdrFrame) {
            MDS_DBG("Force IDR Frame\n");
            hEncode = Venc1_getVisaHandle(hVe1);
            /*  force IDR frame */
            encStatus.size = sizeof(VIDENC1_Status);
            encStatus.data.buf = NULL;
            dvEnc->dynParams.forceFrame = IVIDEO_IDR_FRAME;
            status = VIDENC1_control(hEncode, XDM_SETPARAMS, &dvEnc->dynParams, &encStatus);
            if (status != VIDENC1_EOK) {
                MDS_ERR_OUT(ERR_DC_ALL, "XDM_SETPARAMS control failed\n");
            }
            
            /* Encode the video buffer */
            if (Venc1_process(hVe1, hInBuf, hOutBuf) < 0) {
                MDS_ERR_OUT(ERR_DC_ALL, "\n");
            }
            
            /* restore normal frame */
            encStatus.size = sizeof(VIDENC1_Status);
            encStatus.data.buf = NULL;
            dvEnc->dynParams.forceFrame = IVIDEO_NA_FRAME;
            status = VIDENC1_control(hEncode, XDM_SETPARAMS, &dvEnc->dynParams, &encStatus);
            if (status != VIDENC1_EOK) {
                MDS_ERR_OUT(ERR_DC_ALL, "XDM_SETPARAMS control failed\n");
            }
        } else {
            /* Encode the video buffer */
            if (Venc1_process(hVe1, hInBuf, hOutBuf) < 0) {
                MDS_ERR_OUT(ERR_DC_ALL, "\n");
            }
        }

        /* if encoder generated output content, free released buffer */
        if (Buffer_getNumBytesUsed(hOutBuf)>0) {
            /* Get free buffer */
            hFreeBuf = Venc1_getFreeBuf(hVe1);
            /* Free buffer */
            BufTab_freeBuf(hFreeBuf);
        }
        /* if encoder did not generate output content */
        else {
            /* if non B frame sequence */
            /* encoder skipped frame probably exceeding target bitrate */
                printf("Encoder generated 0 size frame\n");
                BufTab_freeBuf(hInBuf);
        }

        if (dvEnc->cache) {
            /* Writeback the outBuf. */
            Memory_cacheWb(Buffer_getUserPtr(hOutBuf), 
                                  Buffer_getSize(hOutBuf));
        }
    #if 0
        gettimeofday(&tv, NULL);
        MDS_DBG("now: %lld-%lld\n", (long long)tv.tv_sec, (long long)tv.tv_usec);
    #endif
        /* Write the encoded frame to the file system */
        if (Buffer_getNumBytesUsed(hOutBuf)) {
            MdsImgBufSetCompressionImgBufSize(dstImgBuf, Buffer_getNumBytesUsed(hOutBuf));
            MDSElemCastMsg((MDSElem*)dvEnc, MDS_MSG_TYPE_IMAGE, dstImgBuf);
        }
        dvEnc->frameCount++;
        return 0;
    } else if (!strcmp(msg->type, MDS_MSG_TYPE_CMD)) {
        const char *jMsgStr;
        CFBuffer *respBuf;
        int tmpInt;
        CFJson *jMsg;
        
        jMsgStr = ((MdsCmdMsg*)msg->data)->cmd;
        respBuf = ((MdsCmdMsg*)msg->data)->respBuf;
        MDS_DBG("jMsgStr:%s\n", jMsgStr);
        jMsg = CFJsonParse(jMsgStr);
        if (!jMsg) {
            CFBufferCp(respBuf, CF_CONST_STR_LEN("Wrong cmd format\n"));
            return 0;
        }
        if (!CFJsonObjectGetInt(jMsg, "bit_rate", &tmpInt) && tmpInt>0) {
            dvEnc->dynParams.targetBitRate = tmpInt;
        }
        if (!CFJsonObjectGetInt(jMsg, "idr_frame_interval", &tmpInt) && tmpInt>0) {
            dvEnc->idrFrameInterval = tmpInt;
        }
        CFBufferCp(respBuf, CF_CONST_STR_LEN("{\"ret\": \"ok\"}"));
        CFJsonPut(jMsg);
        return 0;
    } else {
        MDS_ERR("<%s> Msg type: %s not support!\n", CFStringGetStr(&self->name), msg->type);
        return -1;
    }
ERR_DC_ALL:
    MDSElemDisconnectAll(self);
    return -1;
}

static int __MdsDvEncoderAddAsGuest(MDSElem* self, MDSElem* vendor)
{
    return 0;
}

static int __MdsDvEncoderAddAsVendor(MDSElem* self, MDSElem* guestElem)
{
    return 0;
}

static int __MdsDvEncoderRemoveAsGuest(MDSElem* self, MDSElem* vendor)
{
    return 0;
}

static int __MdsDvEncoderRemoveAsVendor(MDSElem* self, MDSElem* guestElem)
{
    return 0;
}
#define DEFAULT_ENGINE_NAME     "encode"
#define DEFAULT_CODEC_NAME     "h264enc"
/*
{
    "class": "DV_ENCODER",
    "name": "DvEncoder1",
    "width": 640,
    "height": 480,
    "inPixFmt": "MDS_PIX_FMT_NV12",
    "engine": "",
    "codec": "h264enc | mpeg4enc | mpeg4enc_hdvicp | mpeg2enc",
    "idr_frame_interval": 30,   # H.264
    "bit_rate": -1,
    "cache": 1
}
*/
static MDSElem* _DvEncoderElemRequested(MDSServer* svr, CFJson* jConf)
{
    MdsDvEncoderElem* dvEnc;
    const char* tmpCStr;
    MdsPixFmt inFmt, outFmt;
    uint64 tmpUint64;
    int tmpInt;
    int width, height;
    void* bufPtr;
    int inBufSize, outBufSize;
    BufferGfx_Attrs        gfxAttrs  = BufferGfx_Attrs_DEFAULT;
    VIDENC1_Params         params    = Venc1_Params_DEFAULT;
    Buffer_Attrs           bAttrs    = Buffer_Attrs_DEFAULT;
    ColorSpace_Type        colorSpace;
    
    if (!svr || !jConf) {
		MDS_ERR_OUT(ERR_OUT, "\n");
    }
    CERuntime_init();
    Dmai_init();
    if (!(dvEnc = (MdsDvEncoderElem*)malloc(sizeof(MdsDvEncoderElem)))) {
		MDS_ERR_OUT(ERR_OUT, "malloc for MdsDvEncoderElem failed\n");
    }
    
    if (CFJsonObjectGetIdFromString(jConf, "inPixFmt", pFmtMap, &tmpUint64)) {
        MDS_ERR_OUT(ERR_FREE_RICE, "Please specify correct \"inPixFmt\" options for dv_encoder\n");
    }
    inFmt = tmpUint64;
    dvEnc->inPixFmt = inFmt;
    
    MDS_DBG("\n");
    if (CFJsonObjectGetInt(jConf, "width", &width)
            ||CFJsonObjectGetInt(jConf, "height", &height)
            ||width%2
            ||height%2) {
        MDS_ERR_OUT(ERR_FREE_RICE, "Please specify correct \"width\" and \"height\" options for dv_encoder\n");
    }
    dvEnc->width = width;
    dvEnc->height = height;
    MDS_DBG("\n");
    if (!(tmpCStr = CFJsonObjectGetString(jConf, "engine"))) {
        tmpCStr = DEFAULT_ENGINE_NAME;
    }
    strncpy(dvEnc->engineName, tmpCStr, sizeof(dvEnc->engineName));
    dvEnc->engineName[sizeof(dvEnc->engineName)-1] = '\0';
    
    MDS_DBG("\n");
    if (!(tmpCStr = CFJsonObjectGetString(jConf, "codec"))) {
        tmpCStr = DEFAULT_CODEC_NAME;
    }
    strncpy(dvEnc->codecName, tmpCStr, sizeof(dvEnc->codecName));
    dvEnc->codecName[sizeof(dvEnc->codecName)-1] = '\0';
    if (!strcmp(dvEnc->codecName, "h264enc")) {
        outFmt = MDS_PIX_FMT_H264;
        if (CFJsonObjectGetInt(jConf, "idr_frame_interval", &tmpInt)) {
            tmpInt = MDS_DEFAULT_IDR_FRAME_INTERVAL;
        }
        dvEnc->idrFrameInterval = tmpInt;
    } else if (!strcmp(dvEnc->codecName, "mpeg4enc")) {
        outFmt = MDS_PIX_FMT_MPEG4;
    } else if (!strcmp(dvEnc->codecName, "mpeg4enc_hdvicp")) {
        outFmt = MDS_PIX_FMT_MPEG4;
    } else if (!strcmp(dvEnc->codecName, "mpeg2enc")) {
        outFmt = MDS_PIX_FMT_MPEG2;
    } else {
        MDS_ERR_OUT(ERR_FREE_RICE, "Unknown pix fmt\n");
    }
    
    MDS_DBG("\n");
    if (CFJsonObjectGetInt(jConf, "bit_rate", &tmpInt)) {
        tmpInt = -1;
    }
    dvEnc->bitRate = tmpInt;
    
    MDS_DBG("\n");
    if (CFJsonObjectGetInt(jConf, "cache", &tmpInt)) {
        tmpInt = 0;
    }
    if (tmpInt) {
        dvEnc->cache = TRUE;
    } else {
        dvEnc->cache = FALSE;
    }
    
    MDS_DBG("\n");
    if (Cpu_getDevice(NULL, &dvEnc->device) < 0) {
        MDS_ERR_OUT(ERR_FREE_RICE,"\n");
    }
    MDS_DBG("enginName: %s\n", dvEnc->engineName);
    /* Open the codec engine */
    if (!(dvEnc->hEngine = Engine_open(dvEnc->engineName, NULL, NULL))) {
        MDS_DBG("\n");
        MDS_ERR_OUT(ERR_FREE_RICE, "\n");
    }
    
    MDS_DBG("\n");
    /* Set up codec parameters depending on bit rate */
    if (dvEnc->bitRate < 0) {
        /* Variable bit rate */
        params.rateControlPreset = IVIDEO_NONE;

        /* 
         * If variable bit rate use a bogus bit rate value (> 0)
         * since it will be ignored.
         */
        params.maxBitRate        = 2000000;
    } else {
        /* Constant bit rate */
        params.rateControlPreset = IVIDEO_LOW_DELAY;
        params.maxBitRate        = dvEnc->bitRate;
    }
    
    MDS_DBG("\n");
    /* Set up codec parameters depending on device */
    switch (dvEnc->device) {
        case Cpu_Device_DM6467:
            params.inputChromaFormat = XDM_YUV_420SP;
            params.reconChromaFormat = XDM_CHROMA_NA;
            break;
        case Cpu_Device_DM355:
            params.inputChromaFormat = XDM_YUV_422ILE;
            params.reconChromaFormat = XDM_YUV_420P;
            break;            
        case Cpu_Device_DM365:
        case Cpu_Device_DM368:
            params.inputChromaFormat = XDM_YUV_420SP;
            params.reconChromaFormat = XDM_YUV_420SP;
            break;
        case Cpu_Device_DM3730:
            params.rateControlPreset = IVIDEO_STORAGE;
            params.inputChromaFormat = XDM_YUV_422ILE;
            break;
        default:
            params.inputChromaFormat = XDM_YUV_422ILE;
            break;
    }

    params.maxWidth              = dvEnc->width;
    params.maxHeight             = dvEnc->height;

    /* Workaround for SDOCM00068944: h264fhdvenc fails 
       to create codec when params.dataEndianness is 
       set as XDM_BYTE */
    if(dvEnc->device == Cpu_Device_DM6467) {
        if (!strcmp(dvEnc->codecName, "h264fhdvenc")) {
            params.dataEndianness        = XDM_LE_32;
        }
    }

    params.maxInterFrameInterval = 1;
    dvEnc->dynParams = Venc1_DynamicParams_DEFAULT;
    dvEnc->dynParams.targetBitRate      = params.maxBitRate;
    dvEnc->dynParams.inputWidth         = params.maxWidth;
    dvEnc->dynParams.inputHeight        = params.maxHeight;

    /* Create the video encoder */
    if (!(dvEnc->hVe1 = Venc1_create(dvEnc->hEngine, dvEnc->codecName, &params, &dvEnc->dynParams))) {
        MDS_ERR_OUT(ERR_CLOSE_ENGINE, "\n");
    }

    /* Ask the codec how much input data it needs */
    inBufSize = Venc1_getInBufSize(dvEnc->hVe1);

    /* Ask the codec how much space it needs for output data */
    outBufSize = Venc1_getOutBufSize(dvEnc->hVe1);
    MDS_DBG("inBufSize=%d, outBufSize=%d\n", inBufSize, outBufSize);
    if (inBufSize < 0 || outBufSize < 0) {
        MDS_ERR_OUT(ERR_CLOSE_CODEC, "\n");
    }
    /* Which color space to use in the graphics buffers depends on the device */
    colorSpace = ((dvEnc->device == Cpu_Device_DM6467)||
                  (dvEnc->device == Cpu_Device_DM365) ||
                  (dvEnc->device == Cpu_Device_DM368)) ? ColorSpace_YUV420PSEMI :
                                               ColorSpace_UYVY;
    
    if (MdsPixFmtToDmaiColorSpace(dvEnc->inPixFmt) != colorSpace) {
        MDS_ERR_OUT(ERR_CLOSE_CODEC, "Can not support such input pixel fomat\n");
    }
    
    /* Align buffers to cache line boundary */    
    gfxAttrs.bAttrs.memParams.align = bAttrs.memParams.align = BUFSIZEALIGN; 
    
    /* Use cached buffers if requested */    
    if (dvEnc->cache) {
        gfxAttrs.bAttrs.memParams.flags = bAttrs.memParams.flags 
            = Memory_CACHED;
            MDS_DBG("\n");
    }
    
    MDS_DBG("\n");
    gfxAttrs.dim.width      = dvEnc->width;
    gfxAttrs.dim.height     = dvEnc->height;
    if ((dvEnc->device == Cpu_Device_DM6467) 
            || (dvEnc->device == Cpu_Device_DM365)
            || (dvEnc->device == Cpu_Device_DM368)) {
        gfxAttrs.dim.height = Dmai_roundUp(gfxAttrs.dim.height, CODECHEIGHTALIGN);
    }
    gfxAttrs.dim.lineLength = BufferGfx_calcLineLength(dvEnc->width, colorSpace);
    gfxAttrs.colorSpace     = colorSpace;
    gfxAttrs.bAttrs.reference = TRUE;
    MDS_DBG("\n");
    /* Create a table of input buffers of the size requested by the codec */
    dvEnc->hBufTab = BufTab_create(1, 
            Dmai_roundUp(inBufSize, BUFSIZEALIGN),
            BufferGfx_getBufferAttrs(&gfxAttrs));
    if (dvEnc->hBufTab == NULL) {
        MDS_ERR_OUT(ERR_CLOSE_CODEC, "\n");
    }
    MDS_DBG("\n");
    /* Set input buffer table */
    Venc1_setBufTab(dvEnc->hVe1, dvEnc->hBufTab);
    MDS_DBG("\n");
    
    /* Create the output buffer for encoded video data */
    dvEnc->hOutBuf = Buffer_create(Dmai_roundUp(outBufSize, BUFSIZEALIGN), &bAttrs);
    MDS_DBG("\n");
    if (dvEnc->hOutBuf == NULL) {
        MDS_ERR_OUT(ERR_DEL_IN_BUFTAB, "\n");
    }
    if (!(bufPtr = Buffer_getUserPtr(dvEnc->hOutBuf))) {
		MDS_ERR_OUT(ERR_DEL_OUT_BUF, "get usr ptr of output buf failed\n");
    }
    if (MdsImgBufInit(&dvEnc->dstImgBuf, outFmt, width, height, bufPtr, outBufSize)) {
        MDS_ERR_OUT(ERR_DEL_OUT_BUF, "Init MdsImgBuf failed\n");
    }
    
    MDS_DBG("\n");
    dvEnc->frameCount = 0;
    dvEnc->forceIdrFrame = FALSE;
    if (!(tmpCStr=CFJsonObjectGetString(jConf, "name"))
		    || MDSElemInit((MDSElem*)dvEnc, svr, &_DvEncoderClass, tmpCStr, __MdsDvEncoderProcess,
				    __MdsDvEncoderAddAsGuest, __MdsDvEncoderAddAsVendor,
				    __MdsDvEncoderRemoveAsGuest, __MdsDvEncoderRemoveAsVendor)) {
		MDS_ERR_OUT(ERR_EXIT_OUT_IMG_BUF, "MDSElem init failed: for %s\n", tmpCStr);
    }
    return (MDSElem*)dvEnc;
ERR_EXIT_OUT_IMG_BUF:
    MdsImgBufExit(&dvEnc->dstImgBuf);
ERR_DEL_OUT_BUF:
    Buffer_delete(dvEnc->hOutBuf);
ERR_DEL_IN_BUFTAB:
    BufTab_delete(dvEnc->hBufTab);
ERR_CLOSE_CODEC:
    Venc1_delete(dvEnc->hVe1);
ERR_CLOSE_ENGINE:
    Engine_close(dvEnc->hEngine);
ERR_FREE_RICE:
    free(dvEnc);
ERR_OUT:
    return NULL;
}

static int _DvEncoderElemReleased(MDSElem* elem)
{
    int ret = 0;
    MdsDvEncoderElem* dvEnc = (MdsDvEncoderElem*)elem;

    ret |= MDSElemExit(elem);
    MdsImgBufExit(&dvEnc->dstImgBuf);
    Buffer_delete(dvEnc->hOutBuf);
    BufTab_delete(dvEnc->hBufTab);
    Venc1_delete(dvEnc->hVe1);
    Engine_close(dvEnc->hEngine);
    free(dvEnc);
    return ret;
}

static int _DvEncoderPlugInit(MDSPlugin* plg, MDSServer* svr)
{
    MDS_MSG("Initiating plugin: "MDS_DV_ENCODER_PLUGIN_NAME"\n");
    return MDSServerRegistElemClass(svr, &_DvEncoderClass);
}

static int _DvEncoderPlugExit(MDSPlugin* plg, MDSServer* svr)
{
    MDS_MSG("Exiting plugin: "MDS_DV_ENCODER_PLUGIN_NAME"\n");
    return MDSServerAbolishElemClass(svr, &_DvEncoderClass);
}

