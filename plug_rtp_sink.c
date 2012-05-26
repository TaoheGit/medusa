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
 * along with self program; if not, write to the Free Software
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
//#define _DEBUG_
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
#include <cf_rtp.h>
#include "medusa.h"
#include "mds_log.h"
#include "mds_media.h"

#define MDS_RTP_SINK_ELEM_CLASS_NAME   "RTP_SINK_ELEM"
#define MDS_MSG_TYPE_IMAGE  "Image"
typedef struct rtp_sink_elem {
    MDS_ELEM_MEMBERS;
    CFRtpSender rtpSender;
    uint32_t HZ;
    struct timespec timestamp;
    BOOL chained;
}MdsRtpSinkElem;

#define MDS_RTP_SINK_PLUGIN_NAME       "PLUG_RTP_SINK"
typedef struct mds_rtp_sink_plug{
    MDS_PLUGIN_MEMBERS;
}MDSRtpSinkPlug;

static int _RtpSinkPlugInit(MDSPlugin* self, MDSServer* svr);
static int _RtpSinkPlugExit(MDSPlugin* self, MDSServer* svr);
MDSRtpSinkPlug rtp_sink = {
    .name = MDS_RTP_SINK_PLUGIN_NAME,
    .init = _RtpSinkPlugInit,
    .exit = _RtpSinkPlugExit
};

static MDSElem* _RtpSinkElemRequested(MDSServer* svr, CFJson* jConf);
static int _RtpSinkElemReleased(MDSElem* elem);
MdsElemClass _RtpSinkClass = {
    .name = MDS_RTP_SINK_ELEM_CLASS_NAME,
    .request = _RtpSinkElemRequested,
    .release = _RtpSinkElemReleased,
};

static int __MdsRtpSinkProcess(MDSElem* self, MDSElem* vendor, MdsMsg* msg)
{
    MdsImgBuf* imgBuf;
    MdsRtpSinkElem* rsem = (MdsRtpSinkElem*)self;
    int imgSize;
    uint8_t *dataPtr;
    struct timespec tm;
    uint32_t ts;
    
    if (!strcmp(msg->type, MDS_MSG_TYPE_IMAGE)) {  /* Image */
        imgBuf = (MdsImgBuf*)msg->data; 
        imgSize = MdsImgBufGetImgBufSize(imgBuf);

        dataPtr = MdsImgBufGetPtr(imgBuf);
        
        if (clock_gettime(CLOCK_MONOTONIC, &tm)) {
        	MDS_ERR("clock_gettime() failed, Can not calculate time\n");
        	return -1;
        }
        ts = tm.tv_sec * rsem->HZ;
        MDS_DBG("ts=%lld, hz=%lld\n", (long long int)ts, (long long int)rsem->HZ);
        ts += (tm.tv_nsec/1000000)*(rsem->HZ/1000);
        MDS_DBG("ts=%lld, tv_nsec=%ld\n", (long long int)ts, tm.tv_nsec);
        CFRtpSenderSendFrame(&rsem->rtpSender, &dataPtr[4], imgSize-4, ts);
    } else {
        MDS_ERR("<%s> Msg type: %s not support!\n", CFStringGetStr(&rsem->name), msg->type);
        return -1;
    }
    return 0;
}

static int __MdsRtpSinkAddAsGuest(MDSElem* self, MDSElem* vendor)
{
    MdsRtpSinkElem* rsem = (MdsRtpSinkElem*)self;

    if (rsem->chained) {
        MDS_ERR("MdsRtpSinkElem can only be chained once!!\n")
        return -1;
    }
    rsem->chained = TRUE;
    return 0;
}

static int __MdsRtpSinkAddAsVendor(MDSElem* self, MDSElem* guestElem)
{
    MDS_ERR("File sink element can not be a vendor\n");
    return -1;
}

static int __MdsRtpSinkRemoveAsGuest(MDSElem* self, MDSElem* vendor)
{
    MdsRtpSinkElem* rsem = (MdsRtpSinkElem*)self;
    if (!rsem->chained) {
        MDS_ERR("MdsRtpSinkElem not chained yet!!\n")
        return -1;
    }

    rsem->chained = FALSE;
    return 0;
}

static int __MdsRtpSinkRemoveAsVendor(MDSElem* self, MDSElem* guestElem)
{
    MDS_ERR("File sink element can not be a vendor\n");
    return -1;
}

/*
{
    "class": "RTP_SINK_ELEM",
    "name": "RtpSink1",
    "dest_addr": "192.168.2.101",
    "dest_port": 5004,
    "udp_mtu": 1024,        //optional
    "pt": 96,               //optional
    "payload": 0,           //0:H264
    "SSRC": 123456789,       //optional
    "HZ":90000
}
*/
#define MDS_RTP_SINK_DEFAULT_UDP_MTU    1024
#define MDS_RTP_SINK_DEFAULT_PT 96
#define MDS_RTP_SINK_DEFAULT_SSRC   (CFRandom())
#define MDS_RTP_SINK_DEFAULT_HZ 90000
static MDSElem* _RtpSinkElemRequested(MDSServer* svr, CFJson* jConf)
{
    MdsRtpSinkElem* rsem;
    const char *tmpCStr;
    int tmpInt, udpMtu, payload, ssrc;
    uint8_t pt;
    in_addr_t destAddr;
    in_port_t destPort;
    
    if (!svr || !jConf) {
        MDS_ERR_OUT(ERR_OUT, "\n");
    }
    if (!(rsem = (MdsRtpSinkElem*)malloc(sizeof(MdsRtpSinkElem)))) {
        MDS_ERR_OUT(ERR_OUT, "malloc for MdsRtpSinkElem failed\n");
    }
    
    MDS_DBG("\n");
    if (!(tmpCStr=CFJsonObjectGetString(jConf, "dest_addr"))) {
        MDS_ERR_OUT(ERR_FREE_FEM, "No dest_addr in config\n");
    }
    destAddr = inet_addr(tmpCStr);
    
    if (CFJsonObjectGetInt(jConf, "dest_port", &tmpInt)) {
        MDS_ERR_OUT(ERR_FREE_FEM, "No dest_port in config\n");
    }
    destPort = htons((uint16_t)tmpInt);
    MDS_DBG("\n");
    if (CFJsonObjectGetInt(jConf, "udp_mtu", &udpMtu)) {
        udpMtu = MDS_RTP_SINK_DEFAULT_UDP_MTU;
    }
    MDS_DBG("\n");
    if (CFJsonObjectGetInt(jConf, "pt", &tmpInt)) {
        pt = MDS_RTP_SINK_DEFAULT_PT;
    }
    pt = tmpInt;
    MDS_DBG("\n");
    if (CFJsonObjectGetInt(jConf, "payload", &payload)) {
         MDS_ERR_OUT(ERR_FREE_FEM, "No payload in config\n");
    }
    MDS_DBG("\n");
    if (CFJsonObjectGetInt(jConf, "SSRC", &ssrc)) {
        ssrc = MDS_RTP_SINK_DEFAULT_SSRC;
    }
    MDS_DBG("%x\n", (int)CFRtpSenderInit);
	
	if (CFJsonObjectGetInt(jConf, "HZ", &tmpInt) || tmpInt <= 0) {
        rsem->HZ = MDS_RTP_SINK_DEFAULT_HZ;
    } else {
    	rsem->HZ = tmpInt;
    }
	if (clock_gettime(CLOCK_MONOTONIC, &rsem->timestamp)) {
		MDS_ERR_OUT(ERR_FREE_FEM, "clock_gettime() failed, can not get linear time\n");
	}
	
    CFRtpDebug();
    MDS_DBG("\n");

    if (CFRtpSenderInit(&rsem->rtpSender, 
            destAddr, destPort, 
            udpMtu, pt, payload, ssrc,
            MDSServerGetFdevents(svr))) {
        MDS_ERR_OUT(ERR_FREE_FEM, "Init RTP sender failed\n");
    }

    MDS_DBG("\n");
    if (!(tmpCStr=CFJsonObjectGetString(jConf, "name"))
            || MDSElemInit((MDSElem*)rsem, svr, &_RtpSinkClass, tmpCStr, __MdsRtpSinkProcess,
                    __MdsRtpSinkAddAsGuest, __MdsRtpSinkAddAsVendor,
                    __MdsRtpSinkRemoveAsGuest, __MdsRtpSinkRemoveAsVendor)) {
        MDS_ERR_OUT(ERR_EXIT_RTP_SENDER, "MDSElem init failed: for %s\n", tmpCStr);
    }
    
    rsem->chained = FALSE;
    
    return (MDSElem*)rsem;
ERR_EXIT_RTP_SENDER:
    CFRtpSenderExit(&rsem->rtpSender);
ERR_FREE_FEM:
    free(rsem);
ERR_OUT:
    return NULL;
}

static int _RtpSinkElemReleased(MDSElem* elem)
{
    int ret = 0;
    MdsRtpSinkElem* rsem = (MdsRtpSinkElem*)elem;

    ret |= CFRtpSenderExit(&rsem->rtpSender);
    free(rsem);
    return ret;
}

static int _RtpSinkPlugInit(MDSPlugin* plg, MDSServer* svr)
{
    MDS_MSG("Initiating plugin: "MDS_RTP_SINK_PLUGIN_NAME"\n");
    return MDSServerRegistElemClass(svr, &_RtpSinkClass);
}

static int _RtpSinkPlugExit(MDSPlugin* plg, MDSServer* svr)
{
    MDS_MSG("Exiting plugin: "MDS_RTP_SINK_PLUGIN_NAME"\n");

    return MDSServerAbolishElemClass(svr, &_RtpSinkClass);
}

