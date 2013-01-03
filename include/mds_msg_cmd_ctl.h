#ifndef _MDS_MSG_CMD_CTL_
#define _MDS_MSG_CMD_CTL_

#define MDS_MSG_TYPE_CMD_CTL   "cmdCtl"
#define MDS_DBG_CMD_CTL "MDS_DBG_CMD_CTL"

typedef struct mds_cmd_ctl_cmd MdsCmdCtlCmd;

struct mds_cmd_ctl_cmd {
    const char* path;
    const char* data;
    
    int dataLen;
    int(*processResponse)(CFBuffer* respBuf, void* userPtr, BOOL error);
    void *userPtr;
};


static int MDSElemCastCmdCtlMsg(MDSElem* self, const char* path,
        const char* data, int len, 
        int(*processResponse)(CFBuffer* respBuf, void* userPtr, BOOL),
        void *userPtr)
{
    MdsCmdCtlCmd mccc;
    
    mccc.path = path;
    mccc.data = data;
    mccc.dataLen = len;
    mccc.processResponse = processResponse;
    mccc.userPtr = userPtr;
    CFEnvDbg(MDS_DBG_CMD_CTL, "<CmdCtl> send to %s:\n%s\n", path, data);
    return MDSElemCastMsg(self, MDS_MSG_TYPE_CMD_CTL, &mccc);
}


#endif
