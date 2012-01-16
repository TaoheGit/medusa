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
 
/*
 * [ V4L2 module ]
 * 
 * input type: Image
 * input data: MDSImg
 * 
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <cf_string.h>
#include <cf_common.h>
#include <cf_pipe.h>
#include <cf_buffer.h>
#include "medusa.h"
#include "mds_log.h"
#include "mds_media.h"

#define MDS_V4L2_ELEM_CLASS_NAME        "V4L2_ELEM"
#define MDS_FILE_SINK_ELEM_CLASS_NAME   "FILE_SINK_ELEM"
#define MDS_RAW_IMG_CONV_ELEM_CLASS_NAME   "RAW_IMG_CONV_ELEM"
#define MDS_MSG_TYPE_IMAGE  "Image"
typedef struct file_sink_elem{
    MDS_ELEM_MEMBERS;
    int fd;
    CFFdevent fdEvent;
    CFBuffer bufs[4];
    CFPipe emptyPipe;
    CFPipe fullPipe;
    int writed;
    BOOL chained;
}MdsFileSinkElem;

#define MDS_FILE_SINK_PLUGIN_NAME       "PLUG_FILE_SINK"
typedef struct mds_file_sink_plug{
    MDS_PLUGIN_MEMBERS;
}MDSFileSinkPlug;

static int _FileSinkPlugInit(MDSPlugin* this, MDSServer* svr);
static int _FileSinkPlugExit(MDSPlugin* this, MDSServer* svr);
MDSFileSinkPlug file_sink = {
    .name = MDS_FILE_SINK_PLUGIN_NAME,
    .init = _FileSinkPlugInit,
    .exit = _FileSinkPlugExit
};

static MDSElem* _FileSinkElemRequested(MDSServer* svr, CFJson* jConf);
static int _FileSinkElemReleased(MDSElem* elem);
MdsElemClass _FileSinkClass = {
    .name = MDS_FILE_SINK_ELEM_CLASS_NAME,
    .request = _FileSinkElemRequested,
    .release = _FileSinkElemReleased,
};

static int __MdsFileSinkProcess(MDSElem* this, MDSElem* vendor, MdsMsg* msg)
{
    MdsImgBuf* imgBuf;
    MdsFileSinkElem* fem = (MdsFileSinkElem*)this;
    CFBuffer* buf;
    int imgSize;
    
    if (!strcmp(msg->type, MDS_MSG_TYPE_IMAGE)) {  /* Image */
        imgBuf = (MdsImgBuf*)msg->data; 
        imgSize = MdsImgBufGetImgBufSize(imgBuf);
        if ((buf = CFPipePop(&fem->emptyPipe))) {
            //MDS_DBG("buf=%llx, bufSize=%d, imgBuf->bufPtr=%x, imgSize=%d\n",
            //              (unsigned long long)buf, CFBufferGetSize(buf),
            //              (unsigned long long)MdsImgBufGetPtr(imgBuf),  imgSize);
            CFBufferCp(buf, MdsImgBufGetPtr(imgBuf), imgSize);
            if (!CFPipePush(&fem->fullPipe, buf)) {
                    return CFFdeventsAddIfNotAdded(this->server->fdevents, &fem->fdEvent);
            }
            return -1;
        } else {
            MDS_MSG("No available empty buf for output\n");
            return 0;
        }
    } else {
        MDS_ERR("<%s> Msg type: %s not support!\n", CFStringGetStr(&fem->name), msg->type);
        return -1;
    }
}

static int __MdsFileSinkAddAsGuest(MDSElem* this, MDSElem* vendor)
{
    MdsFileSinkElem* fem = (MdsFileSinkElem*)this;
    int i;

    if (fem->chained) {
        MDS_ERR("MdsFileSinkElem can only be chained once!!\n")
        return -1;
    }
    MDS_DBG("%s add as guest\n", MDSElemGetName(this));
    for (i=0; i<CF_ARRAY_SIZE(fem->bufs); i++) {
        CFPipePush(&fem->emptyPipe, &fem->bufs[i]);
    }
    fem->writed = 0;
    fem->chained = TRUE;
    return 0;
}

static int __MdsFileSinkAddAsVendor(MDSElem* this, MDSElem* guestElem)
{
    MDS_ERR("File sink element can not be a vendor\n");
    return -1;
}

static int __MdsFileSinkRemoveAsGuest(MDSElem* this, MDSElem* vendor)
{
    CFBuffer* buf;

    MdsFileSinkElem* fem = (MdsFileSinkElem*)this;
    if (!fem->chained) {
        MDS_ERR("MdsFileSinkElem can only be chained once!!\n")
        return -1;
    }
    if (!strcmp(vendor->class->name, MDS_V4L2_ELEM_CLASS_NAME)) {
        while ((buf=CFPipePop(&fem->fullPipe))) {
            CFPipePush(&fem->emptyPipe, buf);
        }
    }
    CFFdeventsDel(fem->server->fdevents, &fem->fdEvent);
    fem->writed = 0;
    fem->chained = FALSE;
    return 0;
}

static int __MdsFileSinkRemoveAsVendor(MDSElem* this, MDSElem* guestElem)
{
    MDS_ERR("File sink element can not be a vendor\n");
    return -1;
}

int _MdsFileSinkFdWriteable(CFFdevents* events, CFFdevent* event, int fd, void* data)
{
    CFBuffer* buf;
    int bufSize;
    MdsFileSinkElem* fem = (MdsFileSinkElem*)data;
    void* bufPtr;
    int writed;
    
    if ((buf = CFPipeGetData(&fem->fullPipe))) {
        bufSize = CFBufferGetSize(buf);
        bufPtr = CFBufferGetPtr(buf);

        if (CFAsyncWrite(fem->fd, bufPtr, bufSize, &fem->writed, &writed) < 0) {  /* regular file not support non block IO!! */
            MDS_DBG("Write failed: %s\n", strerror(errno));
            MDSServerReleaseElem(fem->server, (MDSElem*)fem);
            return -1;
        }
        
        //MDS_DBG("fd=%d, buf=%d, bufSize=%d, writed=%d\n", fem->fd, (int)buf, bufSize, (int)fem->writed);
    } else {
        MDS_ERR("should not be here");
        CFFdeventsDel(fem->server->fdevents, &fem->fdEvent);
        return 0;
    }
    if (fem->writed == bufSize) {
        //MDS_DBG("\n");
        fem->writed = 0;
        buf = CFPipePop(&fem->fullPipe);
        //MDS_DBG("\n");
        CFPipePush(&fem->emptyPipe, buf);
        //MDS_DBG("\n");
    }
    if (!CFPipeGetData(&fem->fullPipe)) {   /* no data available, remove fdEvent */
        MDS_DBG("\n");
        CFFdeventsDel(fem->server->fdevents, &fem->fdEvent);
    }
    //MDS_DBG("\n");
    return 0;
}

/*
{
    "name": "fileSink1",
    "file": "./output.raw"
}
*/
static MDSElem* _FileSinkElemRequested(MDSServer* svr, CFJson* jConf)
{
    MdsFileSinkElem* fem;
    const char* tmpCStr;
    int iFBuf;

    if (!svr || !jConf) {
        MDS_ERR_OUT(ERR_OUT, "\n");
    }
    if (!(fem = (MdsFileSinkElem*)malloc(sizeof(MdsFileSinkElem)))) {
        MDS_ERR_OUT(ERR_OUT, "malloc for MdsFileSinkElem failed\n");
    }
    if (!(tmpCStr=CFJsonObjectGetString(jConf, "file"))
                    || (fem->fd = open(tmpCStr, O_TRUNC|O_CREAT|O_WRONLY|O_NONBLOCK, 0644))<0) {
        MDS_ERR_OUT(ERR_FREE_FEM, "Open %s failed: %s\n", tmpCStr, strerror(errno));
    }
    MDS_DBG("fd=%d\n", fem->fd);
    if (CFFdeventInit(&fem->fdEvent, fem->fd, NULL, NULL, _MdsFileSinkFdWriteable, fem)) {
        MDS_ERR_OUT(ERR_CLOSE_FD, "CFFdeventInit failed\n");
    }
    for (iFBuf=0; iFBuf<CF_ARRAY_SIZE(fem->bufs); iFBuf++) {
        MDS_DBG("buf[i]=%d\n", (int)&fem->bufs[iFBuf]);
        if (CFBufferInit(&fem->bufs[iFBuf], 1024*1024)) {
            MDS_ERR_OUT(ERR_EXIT_BUFS, "Buffer init failed: buf[%d]\n", iFBuf);
        }
    }
    if (CFPipeInit(&fem->emptyPipe, CF_ARRAY_SIZE(fem->bufs))) {
        MDS_ERR_OUT(ERR_EXIT_BUFS, "CFPipe init failed\n");
    }
    if (CFPipeInit(&fem->fullPipe, CF_ARRAY_SIZE(fem->bufs))) {
        MDS_ERR_OUT(ERR_EXIT_EMPTY_PIPE, "CFPipe init failed\n");
    }
    if (!(tmpCStr=CFJsonObjectGetString(jConf, "name"))
            || MDSElemInit((MDSElem*)fem, svr, &_FileSinkClass, tmpCStr, __MdsFileSinkProcess,
                    __MdsFileSinkAddAsGuest, __MdsFileSinkAddAsVendor,
                    __MdsFileSinkRemoveAsGuest, __MdsFileSinkRemoveAsVendor)) {
        MDS_ERR_OUT(ERR_EXIT_FULL_PIPE, "MDSElem init failed: for %s\n", tmpCStr);
    }
    fem->writed = 0;
    fem->chained = FALSE;
    return (MDSElem*)fem;

ERR_EXIT_FULL_PIPE:
    CFPipeExit(&fem->fullPipe);
ERR_EXIT_EMPTY_PIPE:
    CFPipeExit(&fem->emptyPipe);
ERR_EXIT_BUFS:
    for (iFBuf--; iFBuf>=0; iFBuf--) {
        CFBufferExit(&fem->bufs[iFBuf]);
    }
ERR_EXIT_FDEVT:
    CFFdeventExit(&fem->fdEvent);
ERR_CLOSE_FD:
    close(fem->fd);
ERR_FREE_FEM:
    free(fem);
ERR_OUT:
    return NULL;
}

static int _FileSinkElemReleased(MDSElem* elem)
{
    int ret = 0;
    MdsFileSinkElem* fem = (MdsFileSinkElem*)elem;
    int iFBuf = CF_ARRAY_SIZE(fem->bufs);

    ret |= CFPipeExit(&fem->fullPipe);
    ret |= CFPipeExit(&fem->emptyPipe);
    for (iFBuf--; iFBuf>=0; iFBuf--) {
        ret |= CFBufferExit(&fem->bufs[iFBuf]);
    }
    CFFdeventsDel(fem->server->fdevents, &fem->fdEvent);
    ret |= CFFdeventExit(&fem->fdEvent);
    MDS_DBG("Closing fileSink fd\n");
    close(fem->fd);
    ret |= MDSElemExit(elem);
    free(fem);
    return ret;
}

static int _FileSinkPlugInit(MDSPlugin* plg, MDSServer* svr)
{
    MDS_MSG("Initiating plugin: "MDS_FILE_SINK_PLUGIN_NAME"\n");
    return MDSServerRegistElemClass(svr, &_FileSinkClass);
}

static int _FileSinkPlugExit(MDSPlugin* plg, MDSServer* svr)
{
    MDS_MSG("Exiting plugin: "MDS_FILE_SINK_PLUGIN_NAME"\n");

    return MDSServerAbolishElemClass(svr, &_FileSinkClass);
}

