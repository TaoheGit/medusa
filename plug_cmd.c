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
#include <cf_fdevent.h>
#include <cf_json.h>
#include <cf_cmd.h>
#include "medusa.h"
#include "plug_cmd.h"
#include "mds_log.h"

#define MDS_CMD_PLUGIN_NAME "PLUG_CMD"
#define MDS_CMD_ELEM_CLASS_NAME  "CMD"
#define MDS_CMD_DEF_MAX_CONNS   (100)
typedef struct mds_cmd_elem MDSCmdElem;
struct mds_cmd_elem{
    MDS_ELEM_MEMBERS;
    CFCmdSvr cmdSvr;
};

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

static void UsageToBuffer(CFBuffer* buf)
{
    CFBufferCat(buf, CF_CONST_STR_LEN("Commands list:\n"));
    CFBufferCat(buf, CF_CONST_STR_LEN("help\tdisplay this help message\n"));
    CFBufferCat(buf, CF_CONST_STR_LEN("ping\t\n"));
    CFBufferCat(buf, CF_CONST_STR_LEN("request_elem <class name> [JSON config]\t\n"));
    CFBufferCat(buf, CF_CONST_STR_LEN("release_elem <element name>\t\n"));
    CFBufferCat(buf, CF_CONST_STR_LEN("list_elems\t\n"));
    CFBufferCat(buf, CF_CONST_STR_LEN("elem_info <element name>\t\n"));
}

int MDSCmdElemProcess(MDSElem* this, MDSElem* vendor, MdsMsg* msg)
{
    MdsCmdMsg* cmdMsg;
    const char* cmdStrPtr;
    CFBuffer* respBuf;
    const char *tmpCStr;

    cmdMsg = msg->data;
    cmdStrPtr = cmdMsg->cmd;
    CFBufferCat(cmdMsg->reqBuf, "\0", 1);	/* make sure cmdStrPtr end with \0 */
    respBuf = cmdMsg->respBuf;

    if(!strcmp(msg->type, MDS_MSG_TYPE_CMD)) {
        MDS_DBG("CMD element Got cmd: %s\n", cmdStrPtr);
        if (!strcmp(cmdStrPtr, "help")) {
            CFBufferCp(respBuf, CF_CONST_STR_LEN(""));
            UsageToBuffer(respBuf);
        } else if (!strncmp(cmdStrPtr, "ping", sizeof("ping")-1)) {
            CFBufferCp(respBuf, CF_CONST_STR_LEN("pong\n"));
        } else if (!strncmp(cmdStrPtr, "request_elem", sizeof("request_elem")-1)) {
        	char *className, *jConfStr;
            CFJson *jConf;
            if ((tmpCStr = strblank(cmdStrPtr))
            		&& (className = strnblank(tmpCStr))
            		&& (jConfStr = strblank(className))
            		&& (!(*jConfStr = '\0'))) {
            	jConfStr++;
            	MDS_DBG("className:%s\n", className);
            	MDS_DBG("jConfStr:%s\n", jConfStr);
            	if ((jConf = CFJsonParse(jConfStr))) {
                    if (MDSServerRequestElem(this->server, className, jConf)) {
                        CFBufferCp(respBuf, CF_CONST_STR_LEN("ok\n"));
                    } else {
                        CFBufferCp(respBuf, CF_CONST_STR_LEN("fail: request failed\n"));
                    }
                    CFJsonPut(jConf);
            	} else {
            		CFBufferCp(respBuf, CF_CONST_STR_LEN("fail: wrong json format\n"));
            	}
            } else {
            	CFBufferCp(respBuf, CF_CONST_STR_LEN("fail\n"));
            }
        } else if (!strncmp(cmdStrPtr, "release_elem", sizeof("release_elem")-1)) {
        	MDSElem* elem;

            if ((tmpCStr = strblank(cmdStrPtr))
                    && (tmpCStr = strnblank(tmpCStr))
                    && (elem = MDSServerFindElemByName(this->server, tmpCStr))) {
                MDSServerReleaseElem(this->server, elem);
            } else {
        	    CFBufferCp(respBuf, CF_CONST_STR_LEN("fail: no such element refed by server\n"));
        	}
        } else if (!strncmp(cmdStrPtr, "list_elems", sizeof("list_elems")-1)) {
            CFBufferCp(respBuf, CF_CONST_STR_LEN("[Name(Class)]\n"));
            CFGListForeach(this->server->elements, node) {
                tmpCStr = CFStringGetStr(&((MDSElem*)(node->data))->name);
                CFBufferCat(respBuf, tmpCStr, strlen(tmpCStr));
                CFBufferCat(respBuf, CF_CONST_STR_LEN("("));
                tmpCStr = ((MDSElem*)(node->data))->class->name;
                CFBufferCat(respBuf, tmpCStr, strlen(tmpCStr));
                CFBufferCat(respBuf, CF_CONST_STR_LEN(")\n"));
            }
        } else if (!strncmp(cmdStrPtr, "elem_info", sizeof("elem_info")-1)) {
            MDSElem* elem;

            if ((tmpCStr = strblank(cmdStrPtr))
                    && (tmpCStr = strnblank(tmpCStr))
                    && (elem = MDSServerFindElemByName(this->server, tmpCStr))) {
                CFBufferCp(respBuf, CF_CONST_STR_LEN("[Vendors]\n"));
                {CFGListForeach(elem->vendors, node) {
                    tmpCStr = CFStringGetStr(&((MDSElem*)(node->data))->name);
                    CFBufferCat(respBuf, tmpCStr, strlen(tmpCStr));
                    CFBufferCat(respBuf, CF_CONST_STR_LEN("\n"));
                }}
                CFBufferCat(respBuf, CF_CONST_STR_LEN("[Guests]\n"));
                {CFGListForeach(elem->guests, node) {
                    tmpCStr = CFStringGetStr(&((MDSElem*)(node->data))->name);
                    CFBufferCat(respBuf, tmpCStr, strlen(tmpCStr));
                    CFBufferCat(respBuf, CF_CONST_STR_LEN("\n"));
                }}
            } else {
                CFBufferCp(respBuf, CF_CONST_STR_LEN("No such element.\n"));
            }
        } else if (!strncmp(cmdStrPtr, "connect", sizeof("connect")-1)) {
            char *guest, *vendor, *e;

            vendor = strblank(cmdStrPtr);
            vendor = strnblank(vendor);
            guest = strblank(vendor);
            *guest = '\0';
            guest = strnblank(guest+1);
            if ((e = strblank(guest))) {
                *e='\0';
            }
            MDS_DBG("do connect: %s ==> %s\n", vendor, guest);
            if (MDSServerConnectElemsByName(this->server, vendor, guest)) {
                CFBufferCp(respBuf, CF_CONST_STR_LEN("Fail\n"));
            } else {
                CFBufferCp(respBuf, CF_CONST_STR_LEN("OK\n"));
            }
        } else if (!strncmp(cmdStrPtr, "disconnect", sizeof("disconnect")-1)) {
            char *guest, *vendor, *e;

            vendor = strblank(cmdStrPtr);
            vendor = strnblank(vendor);
            guest = strblank(vendor);
            *guest = '\0';
            guest = strnblank(guest+1);
            if ((e = strblank(guest))) {
                *e='\0';
            }
            MDS_DBG("do disconnect: %s =X=> %s\n", vendor, guest);
            if (MDSServerDisConnectElemsByName(this->server, vendor, guest)) {
                CFBufferCp(respBuf, CF_CONST_STR_LEN("Fail\n"));
            } else {
                CFBufferCp(respBuf, CF_CONST_STR_LEN("OK\n"));
            }
            MDS_DBG("\n");
        } else {
            CFBufferCp(respBuf, CF_CONST_STR_LEN("No such command.\n"));
            UsageToBuffer(respBuf);
        }
    } else {
        MDS_ERR("Cmd can not process such info, vendor class: %s\n", vendor->class->name);
        return -1;
    }
    return 0;
}

static int MDSCmdConnProcessRequest(CFCmdSvrDataConn* dc, CFBuffer* reqBuf, CFBuffer* respBuf, void* usrData)
{
    MDSElem *cmdElem;
    int ret = 0;
    MdsCmdMsg msg;
    const char* to;
    
    if (dc->status == CF_CMD_SVR_DATA_CONN_ST_READ_REQ_VER) {
        MDS_DBG("CmdClient closed conn\n!");
        return 0;
    } else if (dc->status != CF_CMD_SVR_DATA_CONN_ST_PROCESS) {
        MDS_ERR("MDSCmdConnProcessRequest(), status!=CF_CMD_SVR_DATA_CONN_ST_PROCESS\n");
        return -1;
    }
    cmdElem = usrData;
    msg.cmd = memchr(CFBufferGetPtr(reqBuf), '\0', CFBufferGetSize(reqBuf));
    msg.cmd ++;
    msg.cmdLen = CFBufferGetSize(reqBuf) - (msg.cmd - (char*)CFBufferGetPtr(reqBuf));
    msg.reqBuf = reqBuf;
    msg.respBuf = respBuf;
    to = CFBufferGetPtr(reqBuf);
    if (!MDSServerFindElemByName(cmdElem->server, to)) {
        CFBufferCp(respBuf, CONST_STR_LEN("unknown element:"));
        CFBufferCat(respBuf, to, strlen(to));
        CFBufferCat(respBuf, CONST_STR_LEN("\n"));
    } else {
        CFEnvDbg(MDS_DBG_CMD_SVR, "CMD_SVR_REQ: %s\n", msg.cmd);
        ret = MDSElemSendMsg(cmdElem, to, MDS_MSG_TYPE_CMD, &msg);
    }
    if (!CFBufferGetSize(respBuf)) {
		CFBufferCp(respBuf, CF_CONST_STR_LEN("unknown error\n"));
	}
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
    MDS_DBG("\n");
    if(CFCmdSvrInit(&this->cmdSvr, unixSockPath, maxConns, MDSCmdConnProcessRequest, this, svr->fdevents)){
        MDS_ERR("\n");
        goto ERR_OUT;
    }
    MDS_DBG("\n");
    if(MDSElemInit((MDSElem*)this, svr,
                &_CmdClass, name,
                MDSCmdElemProcess,
                MDSCmdElemAddedAsGuest, MDSCmdElemAddedAsVendor,
                MDSCmdElemRemoveAsGuest, MDSCmdElemRemoveAsVendor)){
        MDS_ERR("\n");
        goto ERR_CMD_SVR_EXIT;
    }
    MDS_DBG("%s:%d\n", __FILE__, __LINE__);

    return 0;
ERR_ELEM_EXIT:
    MDSElemExit((MDSElem*)this);
ERR_CMD_SVR_EXIT:
    CFCmdSvrExit(&this->cmdSvr);
ERR_OUT:
    return -1;
}

int MDSCmdElemExit(MDSCmdElem* this)
{
    MDSElemExit((MDSElem*)this);
    CFCmdSvrExit(&this->cmdSvr);
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
        MDS_ERR("\n");
        goto ERR_OUT;
    }
    MDS_MSG("Initiating CMD element: %s\n", name);
    if(!(unixSockPath=CFJsonObjectGetString(conf, "unix_socket_path"))){
        MDS_ERR("\n");
        goto ERR_OUT;
    }
    if(CFJsonObjectGetInt(conf, "max_connections", &maxConns)){
        maxConns = MDS_CMD_DEF_MAX_CONNS;
    }
    MDS_DBG("\n");
    if(MDSCmdElemInit(cElem, svr, name, unixSockPath, maxConns)){
        MDS_ERR("\n");
        goto ERR_OUT;
    }
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
    MDS_MSG("Initiating "MDS_CMD_PLUGIN_NAME"\n");
    return MDSServerRegistElemClass(svr, &_CmdClass);
}

static int CmdPlugExit(MDSPlugin* plg, MDSServer* svr)
{
    MDS_MSG("Exiting "MDS_CMD_PLUGIN_NAME"\n");
    return MDSServerAbolishElemClass(svr, &_CmdClass);
}

