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
 
#include <sys/un.h>
#include <errno.h>
#include <sys/types.h>
#define _GNU_SOURCE
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <assert.h>
#include <cf_std.h>
#include <cf_common.h>
#include "mds_log.h"
#define MP_CMD_DBG  CF_DBG
#define MP_CMD_ERR  CF_ERR
#define MP_CMD_MSG  CF_MSG
#include <cf_fdevent.h>
#include <cf_json.h>
#include "mds_cmd.h"

#define MDS_CMD_PLUGIN_NAME "cmd"
#define MDS_CMD_ELEM_CLASS_NAME  "CMD"
#define MDS_CMD_DEF_MAX_CONNS   (100)
typedef struct mds_cmd_elem{
    MDS_ELEM_MEMBERS;
    MDSCmdSvr cmdSvr;
}MDSCmdElem;

typedef struct mds_cmd_plug{
    MDS_PLUGIN_MEMBERS;
}MDSCmdPlug;

static int CmdPlugInit(MDSPlugin* this, MDSServer* svr);
static int CmdPlugExit(MDSPlugin* this, MDSServer* svr);
static MDSElem* _CmdElemRequested(MDSServer* svr, CFJson* jConf);
static int _CmdElemReleased(MDSElem* elem);
MdsElemClass _CmdClass = {
        .name = MDS_CMD_ELEM_CLASS_NAME,
        .request = _CmdElemRequested,
        .release = _CmdElemReleased
};

MDSCmdPlug cmd = {
    .name = MDS_CMD_PLUGIN_NAME,
    .init = CmdPlugInit,
    .exit = CmdPlugExit
};

int MDSCmdElemProcess(MDSElem* this, MDSElem* vendor, void* data)
{
    MDSCmdSvrDataConn* dataConn;

    dataConn = data;
    if(!strcmp(vendor->class->name, MDS_CMD_ELEM_CLASS_NAME)){
        CFString tmpStr;
        CFStringInit(&tmpStr, "");
        MDS_DBG("request=%x, size=%lu\n",
                        (unsigned int)CFBufferGetPtr(&dataConn->request.body),
                dataConn->request.bodySize);
        CFStringSafeCpN(&tmpStr, CFBufferGetPtr(&dataConn->request.body),
                dataConn->request.bodySize);
        MP_CMD_DBG("CMD element Got cmd: %s\n", CFStringGetStr(&tmpStr));
        CFBufferCp(&dataConn->response.body, "pong\n", sizeof("pong\n")-1);
        dataConn->response.bodySize = sizeof("pong\n")-1;
        CFStringExit(&tmpStr);
    }else{
        MP_CMD_ERR("Cmd can not process such info, vendor class: %s\n", vendor->class->name);
        return -1;
    }
    return 0;
}

static int MDSCmdConnProcessRequest(MDSCmdSvrDataConn* dataConn, void* usrData)
{
    MDSElem *cmdElem;
    MDSElem *elem;
    int ret = 0;
    CFString tmpStr;
    BOOL found = FALSE;

    CFStringInit(&tmpStr, "");
    cmdElem = usrData;
    CFStringSafeCpN(&tmpStr, CFBufferGetPtr(&dataConn->request.body), dataConn->request.bodySize);
    CFGListForeach(cmdElem->guests, node) {
        elem = node->data;
        if (!strcmp(CFStringGetStr(&tmpStr), CFStringGetStr(&elem->name))) {
            found = TRUE;
            ret |= elem->process(elem, cmdElem, dataConn);
            break;
        }
    }
    if (!found) {
        CFBufferCp(&dataConn->response.body, CONST_STR_LEN("unknown element\n"));
        dataConn->response.bodySize += sizeof("unknown element\n")-1;
    }
    CFStringExit(&tmpStr);
    return ret;
}

static int MDSCmdElemAddedAsGuest(MDSElem* this, MDSElem* vendorElem)
{
        return 0;
}

static int MDSCmdElemAddedAsVendor(MDSElem* this, MDSElem* guestElem)
{
        return 0;
}

static int MDSCmdElemRemoveAsGuest(MDSElem* this, MDSElem* vendorElem)
{
        return 0;
}

static int MDSCmdElemRemoveAsVendor(MDSElem* this, MDSElem* guestElem)
{
        return 0;
}

int MDSCmdElemInit(MDSCmdElem* this, MDSServer* svr, const char* name, const char* unixSockPath, int maxConns)
{
    MP_CMD_DBG("\n");
    if(MDSCmdSvrInit(&this->cmdSvr, unixSockPath, maxConns, MDSCmdConnProcessRequest, this, svr->fdevents)){
        MP_CMD_ERR("\n");
        goto ERR_OUT;
    }
    MP_CMD_DBG("\n");
    if(MDSElemInit((MDSElem*)this, svr,
                &_CmdClass, name,
                MDSCmdElemProcess,
                MDSCmdElemAddedAsGuest, MDSCmdElemAddedAsVendor,
                MDSCmdElemRemoveAsGuest, MDSCmdElemRemoveAsVendor)){
        MP_CMD_ERR("\n");
        goto ERR_CMD_SVR_EXIT;
    }
    MP_CMD_DBG("%s:%d\n", __FILE__, __LINE__);

    return 0;
ERR_ELEM_EXIT:
    MDSElemExit((MDSElem*)this);
ERR_CMD_SVR_EXIT:
    MDSCmdSvrExit(&this->cmdSvr);
ERR_OUT:
    return -1;
}

int MDSCmdElemExit(MDSCmdElem* this)
{
    MDSElemExit((MDSElem*)this);
    MDSCmdSvrExit(&this->cmdSvr);
    return 0;
}

/*
{
        name:"cmd0",
        unix_socket_path:"/tmp/medusa/cmd0.sock",
        max_connections:10
}
*/
static int MDSCmdElemInitByJconf(MDSCmdElem* cElem, MDSServer* svr, CFJson* conf)
{
    const char* unixSockPath;
    int maxConns;
    const char* name;

    if(!(name=CFJsonObjectGetString(conf, "name"))){
        MP_CMD_ERR("\n");
        goto ERR_OUT;
    }
    MP_CMD_MSG("Initiating CMD element: %s\n", name);
    if(!(unixSockPath=CFJsonObjectGetString(conf, "unix_socket_path"))){
        MP_CMD_ERR("\n");
        goto ERR_OUT;
    }
    if(CFJsonObjectGetInt(conf, "max_connections", &maxConns)){
        maxConns = MDS_CMD_DEF_MAX_CONNS;
    }
    MP_CMD_DBG("\n");
    if(MDSCmdElemInit(cElem, svr, name, unixSockPath, maxConns)){
        MP_CMD_ERR("\n");
        goto ERR_OUT;
    }
    MP_CMD_DBG("%s:%d\n", __FILE__, __LINE__);
    return 0;
ERR_OUT:
    return -1;
}

static MDSElem* _CmdElemRequested(MDSServer* svr, CFJson* jConf)
{
        MDSElem* ret;

        if (!(ret = (MDSElem*)malloc(sizeof(MDSCmdElem)))) {
                MDS_ERR_OUT(ERR_OUT, "\n");
        }
        if (MDSCmdElemInitByJconf((MDSCmdElem*)ret, svr, jConf)) {
                MDS_ERR_OUT(ERR_FREE, "\n")
        }
        return ret;
ERR_FREE:
        free(ret);
ERR_OUT:
        return NULL;
}

static int _CmdElemReleased(MDSElem* elem)
{
    MDS_DBG("\n");
    assert(elem);
        MDSCmdElemExit((MDSCmdElem*)elem);
        MDS_DBG("\n");
        free(elem);
        MDS_DBG("\n");
        return 0;
}

static int CmdPlugInit(MDSPlugin* plg, MDSServer* svr)
{
    MP_CMD_MSG("Initiating plug_cmd\n");
        return MDSServerRegistElemClass(svr, &_CmdClass);
}

static int CmdPlugExit(MDSPlugin* plg, MDSServer* svr)
{
    MP_CMD_MSG("Exiting plug_cmd\n");

        return MDSServerAbolishElemClass(svr, &_CmdClass);
}

