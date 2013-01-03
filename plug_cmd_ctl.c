//#define _DEBUG_
#include <string.h>
#include <linux/input.h>
#include <cf_timer.h>
#include <cf_buffer.h>
#include <cf_cmd.h>
#include "medusa.h"
#include "mds_log.h"
#include "mds_msg_cmd_ctl.h"

#define MDS_CMD_CTL_PLUG_NAME    "cmd_ctl"
#define MDS_CMD_CTL_ELEM_CLASS_NAME    "CmdCtl"

typedef struct mds_cmd_ctl_elem {
    MDS_ELEM_MEMBERS;
}MdsCmdCtlElem;

static MDSElem* _CmdCtlElemRequested(MDSServer* svr, CFJson* jConf);
static int _CmdCtlElemReleased(MDSElem* elem);
static int _CmdCtlAddedAsGuest(MDSElem* self, MDSElem* vendor);
static int _CmdCtlAddedAsVendor(MDSElem* self, MDSElem* guestElem);
static int _CmdCtlRemoveAsGuest(MDSElem* self, MDSElem* vendor);
static int _CmdCtlRemoveAsVendor(MDSElem* self, MDSElem* guestElem);
static int _CmdCtlProcess(MDSElem* self, MDSElem* vendor, MdsMsg* msg);

static MdsElemClass _cmd_ctl_class = {
    .name = MDS_CMD_CTL_ELEM_CLASS_NAME,
    .request = _CmdCtlElemRequested,
    .release = _CmdCtlElemReleased,
    .process = _CmdCtlProcess,
    .addedAsGuest = _CmdCtlAddedAsGuest,
    .addedAsVendor = _CmdCtlAddedAsVendor,
    .removeAsGuest = _CmdCtlRemoveAsGuest,
    .removeAsVendor = _CmdCtlRemoveAsVendor
};

static int _MdsCmdCtlPlugInit(MDSPlugin* plg, MDSServer* svr)
{
    MDS_MSG("Initiating plugin: "MDS_CMD_CTL_PLUG_NAME"\n");
    return MDSServerRegistElemClass(svr, &_cmd_ctl_class);
}

static int _MdsCmdCtlPlugExit(MDSPlugin* plg, MDSServer* svr)
{
    MDS_MSG("Exiting plugin: "MDS_CMD_CTL_PLUG_NAME"\n");

    return MDSServerAbolishElemClass(svr, &_cmd_ctl_class);
}

MDS_PLUGIN("Chunfeng Zhang [CrazyPandar@gmail.com]", 
        MDS_CMD_CTL_PLUG_NAME, 
        "CMD client element",
        _MdsCmdCtlPlugInit,
        _MdsCmdCtlPlugExit,
        0, 1, 0);

static MdsCmdCtlElem CmdCtlElem;

static MDSElem* _CmdCtlElemRequested(MDSServer* svr, CFJson* jConf)
{
    const char *name;
    
    if (!(name = CFJsonObjectGetString(jConf, "name"))) {
		MDS_ERR_OUT(ERR_OUT, "set name in CmdCtl class config\n");
	}

    if (MDSElemInit((MDSElem*)&CmdCtlElem, svr, &_cmd_ctl_class, name, NULL,
                    NULL, NULL, NULL, NULL)) {
        MDS_ERR_OUT(ERR_OUT, "MDSElem init failed: for %s\n", name);
    }
    return (MDSElem*)&CmdCtlElem;

ERR_OUT:
    return NULL;
}

static int _CmdCtlElemReleased(MDSElem* elem)
{
    MdsCmdCtlElem *mcce;
    
    mcce = (MdsCmdCtlElem*)elem;
    MDSElemExit(elem);
    return 0;
}

static int _CmdCtlAddedAsGuest(MDSElem* self, MDSElem* vendor)
{
    return 0;
}

static int _CmdCtlAddedAsVendor(MDSElem* self, MDSElem* guestElem)
{
    return 0;
}

static int _CmdCtlRemoveAsGuest(MDSElem* self, MDSElem* vendor)
{
    return 0;
}

static int _CmdCtlRemoveAsVendor(MDSElem* self, MDSElem* guestElem)
{
    return 0;
}

struct cmd_ctl_msg {
    int(*processResponse)(CFBuffer* respBuf, void* userPtr, BOOL error);
    void *userPtr;
};

static CFCmdCtl* _processResponse(CFCmdCtl* ctlReq, CFBuffer* respBuf, void* userPtr)
{
    struct cmd_ctl_msg *ccm;
    const char *data;
    
    ccm = (struct cmd_ctl_msg*)userPtr;
    MDS_DBG("CFCmdCtl->fd=%d\n", ctlReq->fd);
    if ((data = CFBufferGetPtr(respBuf))) {
        CFBufferCat(respBuf, "\0", 1);
        CFEnvDbg(MDS_DBG_CMD_CTL, "<CmdCtl> Response:\n%s\n", data);
    }
    if (ccm->processResponse) {
        if (ctlReq->status == CF_CMD_CTL_ST_PROCESS_RESP) {
            ccm->processResponse(respBuf, ccm->userPtr, FALSE);
        } else {
            MDS_ERR("CmdCtlRequest Failed\n");
            ccm->processResponse(respBuf, ccm->userPtr, TRUE);
        }
    }
    MDS_DBG("\n");
    free(ccm);
    CFCmdCtlDestory(ctlReq);
    return NULL;
}

static int _CmdCtlProcess(MDSElem* self, MDSElem* vendor, MdsMsg* msg)
{
    MdsCmdCtlElem *mcce;
    MdsCmdCtlCmd *mccc;
    CFCmdCtl *ccc;
    
    
    mcce = (MdsCmdCtlElem*)self;
    
    if (!strcmp(msg->type, MDS_MSG_TYPE_CMD_CTL)) {
        mccc = (MdsCmdCtlCmd*)msg->data;
        if ((ccc = CFCmdCtlNew(MDSServerGetFdevents(MDSElemGetServer(self))))) {
            if (!CFCmdCtlConnect(ccc, mccc->path)) {
                struct cmd_ctl_msg *ccm = malloc(sizeof(struct cmd_ctl_msg));
                
                if (ccm) {
                    ccm->userPtr = mccc->userPtr;
                    ccm->processResponse = mccc->processResponse;
                    MDS_DBG("%x, %d\n", mccc->data, mccc->dataLen);
                    if (!CFCmdCtlRequest(ccc, (void*)mccc->data, mccc->dataLen, 
                            _processResponse, ccm)) {
                    } else {
                        MDS_ERR("CFCmdCtlRequest() failed\n");
                        if (mccc->processResponse) {
                            mccc->processResponse(NULL, ccm->userPtr, TRUE);
                        }
                        free(ccm);
                        CFCmdCtlDestory(ccc);
                    }
                } else {
                    MDS_ERR("ccm malloc() failed\n");
                    if (mccc->processResponse) {
                        mccc->processResponse(NULL, mccc->userPtr, TRUE);
                    }
                    CFCmdCtlDestory(ccc);
                }
            } else {
                CFEnvDbg(MDS_DBG_CMD_CTL, "CFCmdCtlConnect(, %s) failed\n", mccc->path);
                if (mccc->processResponse) {
                    mccc->processResponse(NULL, mccc->userPtr, TRUE);
                }
                CFCmdCtlDestory(ccc);
            }
        } else {
            MDS_ERR("CFCmdCtlNew() failed\n");
            if (mccc->processResponse) {
                mccc->processResponse(NULL, mccc->userPtr, TRUE);
            }
        }
    } else {
            MDS_DBG("unknown cmd type: %s\n", msg->type);
    }
    return 0;
}
