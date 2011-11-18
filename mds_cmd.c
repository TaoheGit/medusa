#define _GNU_SOURCE             /* for accept4 */
#include <sys/types.h>          /* See NOTES */
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/un.h>
#include <cf_std.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include "mds_log.h"
#define MDS_CMD_ERR CF_ERR
#define MDS_CMD_DBG CF_DBG
#include "mds_cmd.h"
int MDSCmdReqInit(MDSCmdReq* this)
{
    return CFBufferInit(&this->body, 512);
}

int MDSCmdReqExit(MDSCmdReq* this)
{
    return CFBufferExit(&this->body);
}

int MDSCmdReqChksum(MDSCmdReq* req)
{
    /* TODO */
    return 0;
}

BOOL MDSCmdReqChksumOK(MDSCmdReq* req)
{
    /* TODO */
    return TRUE;    
}

int MDSCmdRespInit(MDSCmdResp* this)
{
    return CFBufferInit(&this->body, 512);
}

int MDSCmdRespExit(MDSCmdResp* this)
{

    return CFBufferExit(&this->body);
}

int MDSCmdRespChksum(MDSCmdResp* this)
{
    /* TODO */
    return 0;
}

BOOL MDSCmdRespChksumOK(MDSCmdResp* this)
{
    /* TODO */
    return TRUE;
}

int MDSCmdReqCalcBodySize(const char* element, int reqDataLen)
{
    return strlen(element)+1+reqDataLen;
}

int CFAsyncWrite(int fd, const void* buf, int bufLen, int* writed)
{
    int writedThisTime = 0;
    writedThisTime = write(fd, buf+*writed, bufLen-*writed);
    if(writedThisTime == -1){
        if(errno == EAGAIN || errno == EINTR){
            return 0;
        }else{
            MDS_CMD_ERR("Async write failed: %s\n", strerror(errno));
            return -1;
        }
    }else{
        *writed += writedThisTime;
        return 0;
    }
}

int CFAsyncRead(int fd, void* buf, int bufLen, int* readed)
{
    int readedThisTime = 0;
    readedThisTime = read(fd, buf+*readed, bufLen-*readed);
    if(readedThisTime == -1){
        if(errno == EAGAIN || errno == EINTR){
            return 0;
        }else{
            MDS_CMD_ERR("Async read failed: %s\n", strerror(errno));
            return -1;
        }
    }else if(readedThisTime == 0){
        return -1;    /* she closed conn */
    }else{
        *readed += readedThisTime;
        return 0;
    }
}

int CFConnectToUnixSocket(const char* unixSockPath, int* fd, mode_t oFlag)
{
    int sockFd;
    struct sockaddr_un addr;
    
    sockFd = socket(AF_UNIX, SOCK_STREAM, 0);
    if(sockFd == -1){
        MDS_CMD_ERR("Can not initial unix socket for cmd element\n");
        return -1;
    }
    memset(&addr, 0, sizeof(struct sockaddr_un));
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, unixSockPath);
    
    if(-1 == connect(sockFd, (struct sockaddr*)&addr, offsetof(struct sockaddr_un, sun_path)+strlen(addr.sun_path))){
        MDS_CMD_ERR("\n");
        goto ERR_CLOSE_SOCK;
    }
    if(-1==fcntl(sockFd, F_SETFL, oFlag|(fcntl(sockFd, F_GETFL)))){
        MDS_CMD_ERR("\n");
        goto ERR_CLOSE_SOCK;
    }
    *fd = sockFd;
    return 0;
    
ERR_CLOSE_SOCK:
    close(sockFd);
    return -1;
}

/*
    parameters:
    if events=NULL, means following request is syncronized.
*/
int MDSCmdCtlInit(MDSCmdCtl* this, CFFdevents* events)
{
    if(CFStringInit(&this->unixSockPath, "")){
        MDS_CMD_ERR("\n");
        goto ERR_OUT;
    }
    if(events){
        this->async = TRUE;
        this->events = events;
    }else{
        this->async = FALSE;
        if(!(this->events = CFFdeventsNew())){
            MDS_CMD_ERR("\n");
            goto ERR_EXIT_UNIX_SOCK_STR;              
        }
    }
    if(MDSCmdReqInit(&this->request)){
        MDS_CMD_ERR("\n");
        goto ERR_FDEVENTS_FREE;
    }
    if(MDSCmdRespInit(&this->response)){
        MDS_CMD_ERR("\n");
        goto ERR_REQ_EXIT;
    }
    this->status = MDS_CMD_CTL_ST_IDLE;
    return 0;
    
//ERR_RESP_EXIT:
//    MDSCmdRespExit(&this->response);
ERR_REQ_EXIT:
    MDSCmdReqExit(&this->request);
ERR_FDEVENTS_FREE:
    if(!events){
        CFFdeventsFree(this->events);
    }
ERR_EXIT_UNIX_SOCK_STR:
    CFStringExit(&this->unixSockPath);
ERR_OUT:
    return -1;
}

static int MDSCmdCtlWritable(CFFdevents* events, CFFdevent* event, int fd, void* data);
static int MDSCmdCtlReadable(CFFdevents* events, CFFdevent* event, int fd, void* data);

int MDSCmdCtlConnect(MDSCmdCtl* this, const char* unixSockPath)
{   
    if(this->status != MDS_CMD_CTL_ST_IDLE){
        MDS_CMD_ERR("\n");
        return -1;
    }
    CFStringSafeCp(&this->unixSockPath, unixSockPath);
    if(CFConnectToUnixSocket(unixSockPath, &this->fd, O_NONBLOCK)){
        MDS_CMD_ERR("Connect unix socket: %s failed\n", unixSockPath);
        goto ERR_OUT;
    }
    if(CFFdeventInit(&this->readEvt, this->fd, MDSCmdCtlReadable, (void*)this, NULL, NULL)){
        MDS_CMD_ERR("\n");
        goto ERR_CLOSE_FD;
    }
    if(CFFdeventInit(&this->writeEvt, this->fd, NULL, NULL, MDSCmdCtlWritable, (void*)this)){
        MDS_CMD_ERR("\n");
        goto ERR_RD_EVT_EXIT;
    }
    this->status = MDS_CMD_CTL_ST_CONNECTED_IDLE;
    return 0;
ERR_RD_EVT_EXIT:
    CFFdeventExit(&this->readEvt);
ERR_CLOSE_FD:
    close(this->fd);
ERR_OUT:
    return -1;
}

int MDSCmdCtlRequest(MDSCmdCtl* this, const char* element, const char* reqData, int reqDataLen, int(*processResponse)(MDSCmdCtl* this))
{
    if(this->status != MDS_CMD_CTL_ST_CONNECTED_IDLE){
        MDS_CMD_ERR("\n");
        return -1;
    }
    this->processResponse = processResponse;
    /* prepare for request. */
    memcpy(this->request.version,  MDS_CMD_PROTOCL_VER, MDS_CMD_PROTOCOL_VER_LEN);
    this->request.bodySize = strlen(element)+1;
    CFBufferCp(&this->request.body, element, this->request.bodySize);
    this->request.bodySize += reqDataLen;
    CFBufferCat(&this->request.body, reqData, reqDataLen);
    MDSCmdReqChksum(&this->request);
    this->reqResult = MDS_CTL_REQ_RESULT_ERROR;
    this->tmpWrited = 0;
    CFFdeventsAdd(this->events, &this->writeEvt);
    this->status = MDS_CMD_CTL_ST_WRITE_REQ_VER;    
    if(!this->async){   /* sync request */
       return CFFdeventsLoop(this->events);
    }
    return 0;
}

int MDSCmdCtlWritable(CFFdevents* events, CFFdevent* event, int fd, void* data)
{
#define this ((MDSCmdCtl*)data)
    switch(this->status){
        case MDS_CMD_CTL_ST_WRITE_REQ_VER:
            MDS_CMD_DBG("==>write version\n");
            if(CFAsyncWrite(fd, this->request.version, MDS_CMD_PROTOCOL_VER_LEN, &this->tmpWrited)){
                MDS_CMD_ERR("\n");
                goto ERR_TO_IDLE;
            }
            MDS_CMD_DBG("write version==>\n");
            if(MDS_CMD_PROTOCOL_VER_LEN == this->tmpWrited){
                this->tmpWrited = 0;
                this->status = MDS_CMD_CTL_ST_WRITE_REQ_BODY_SIZE;
            }else{
                break;
            }
        case MDS_CMD_CTL_ST_WRITE_REQ_BODY_SIZE:
            this->request.bodySize = htonl(this->request.bodySize);
            if(CFAsyncWrite(fd, &this->request.bodySize, sizeof(uint32), &this->tmpWrited)){
                MDS_CMD_ERR("\n");
                this->request.bodySize = ntohl(this->request.bodySize);
                goto ERR_TO_IDLE;
            }
            this->request.bodySize = ntohl(this->request.bodySize);
            if(sizeof(uint32)==this->tmpWrited){
                this->tmpWrited = 0;
                this->status = MDS_CMD_CTL_ST_WRITE_REQ_BODY;
            }else{
                break;
            }
        case MDS_CMD_CTL_ST_WRITE_REQ_BODY:
            if(CFAsyncWrite(fd, CFBufferGetPtr(&this->request.body), this->request.bodySize, &this->tmpWrited)){
                MDS_CMD_ERR("\n");
                goto ERR_TO_IDLE;
            }
            if(this->request.bodySize == this->tmpWrited){
                this->tmpWrited = 0;
                this->status = MDS_CMD_CTL_ST_WRITE_REQ_CHKSUM;
            }else{
                break;
            }
        case MDS_CMD_CTL_ST_WRITE_REQ_CHKSUM:
            MDS_CMD_DBG("==>write checksum\n");
            this->request.chksum = htons(this->request.chksum);
            if(CFAsyncWrite(fd, &this->request.chksum, sizeof(uint16), &this->tmpWrited)){
                MDS_CMD_ERR("\n");
                this->request.chksum = ntohs(this->request.chksum);
                goto ERR_TO_IDLE;
            }
            this->request.chksum = ntohs(this->request.chksum);
            if(sizeof(uint16)==this->tmpWrited){
                CFFdeventsDel(this->events, &this->writeEvt);
                this->tmpReaded = 0;
                CFFdeventsAdd(this->events, &this->readEvt);
                this->status = MDS_CMD_CTL_ST_READ_RESP_VER;
            }
            MDS_CMD_DBG("write checksum==>\n");
            break;
        default:
            MDS_CMD_ERR("Should not come to here\n");
            goto ERR_TO_IDLE;
    }
    return 0;
ERR_TO_IDLE:
    MDSCmdCtlResetToIdle(this);
    return -1;    
}

int MDSCmdCtlReadable(CFFdevents* events, CFFdevent* event, int fd, void* data)
{
#define this ((MDSCmdCtl*)data)
    switch(this->status){
        case MDS_CMD_CTL_ST_READ_RESP_VER:
            MDS_CMD_DBG("==>client read version\n");
            if(CFAsyncRead(fd, this->response.version, MDS_CMD_PROTOCOL_VER_LEN, &this->tmpReaded)){
                MDS_CMD_ERR("\n");
                goto ERR_TO_IDLE;
            }
            MDS_CMD_DBG("client read version==>\n");
            if(this->tmpReaded == MDS_CMD_PROTOCOL_VER_LEN){
                if(memcmp(this->response.version, MDS_CMD_PROTOCL_VER, MDS_CMD_PROTOCOL_VER_LEN)){
                    MDS_CMD_ERR("\n");
                    goto ERR_TO_IDLE;
                }
                this->tmpReaded = 0;
                this->status = MDS_CMD_CTL_ST_READ_RESP_BODY_SIZE;
            }else{
                break;
            }
        case MDS_CMD_CTL_ST_READ_RESP_BODY_SIZE:
            if(CFAsyncRead(fd, &this->response.bodySize, sizeof(uint32), &this->tmpReaded)){
                MDS_CMD_ERR("\n");
                goto ERR_TO_IDLE;
            }
            if(this->tmpReaded ==  sizeof(uint32)){
                this->response.bodySize = ntohl(this->response.bodySize);
                MDS_CMD_DBG("client read bodySize=%lu\n", this->response.bodySize);
                this->tmpReaded = 0;
                CFBufferCp(&this->response.body, NULL, this->response.bodySize);
                this->status = MDS_CMD_CTL_ST_READ_RESP_BODY;
            }else{
                break;
            }
        case MDS_CMD_CTL_ST_READ_RESP_BODY:
            if(CFAsyncRead(fd, CFBufferGetPtr(&this->response.body), this->response.bodySize, &this->tmpReaded)){
                MDS_CMD_ERR("\n");
                goto ERR_TO_IDLE;;
            }
            if(this->tmpReaded == this->response.bodySize){
                this->tmpReaded = 0;
                this->status = MDS_CMD_CTL_ST_READ_RESP_CHKSUM;
            }else{
                break;
            }
        case MDS_CMD_CTL_ST_READ_RESP_CHKSUM:
            if(CFAsyncRead(fd,  &this->response.chksum, sizeof(uint16), &this->tmpReaded)){
                MDS_CMD_ERR("\n");
                goto ERR_TO_IDLE;;
            }
            if(this->tmpReaded == sizeof(uint16)){
                this->response.chksum = ntohs(this->response.chksum);
                if(!MDSCmdRespChksumOK(&this->response)){
                    MDS_CMD_ERR("\n");
                    goto ERR_TO_IDLE;
                }
                CFFdeventsDel(this->events, &this->readEvt);
                this->status = MDS_CMD_CTL_ST_CONNECTED_IDLE;
                if(this->async)
                    this->processResponse(this);
            }
            break;
        default:
            MDS_CMD_ERR("Should not come to here\n");
            goto ERR_TO_IDLE;
    }
    return 0;
ERR_TO_IDLE:
    MDSCmdCtlResetToIdle(this);
    return -1;
#undef this
}

int MDSCmdCtlResetToIdle(MDSCmdCtl* this)
{
    switch(this->status){
        case MDS_CMD_CTL_ST_IDLE:
            goto OUT;
        case MDS_CMD_CTL_ST_CONNECTED_IDLE:
            goto OUT_FDEVENT_EXIT;
        case MDS_CMD_CTL_ST_WRITE_REQ_VER:
        case MDS_CMD_CTL_ST_WRITE_REQ_BODY_SIZE:
        case MDS_CMD_CTL_ST_WRITE_REQ_BODY:
        case MDS_CMD_CTL_ST_WRITE_REQ_CHKSUM:
            CFFdeventsDel(this->events, &this->writeEvt);
            goto OUT_FDEVENT_EXIT;
            break;
        case MDS_CMD_CTL_ST_READ_RESP_VER:
        case MDS_CMD_CTL_ST_READ_RESP_BODY_SIZE:
        case MDS_CMD_CTL_ST_READ_RESP_BODY:
        case MDS_CMD_CTL_ST_READ_RESP_CHKSUM:
            CFFdeventsDel(this->events, &this->readEvt);
            goto OUT_FDEVENT_EXIT;
            break;
        default:
            MDS_CMD_ERR("Shouldn't be here\n");
            return -1;
    }
OUT_FDEVENT_EXIT:
    CFFdeventExit(&this->writeEvt);
    CFFdeventExit(&this->readEvt);
    close(this->fd);
OUT:
    this->status = MDS_CMD_CTL_ST_IDLE;
    return 0;
}

int MDSCmdCtlExit(MDSCmdCtl* this)
{
    MDSCmdCtlResetToIdle(this);
    MDSCmdRespExit(&this->response);
    MDSCmdReqExit(&this->request);
    CFStringExit(&this->unixSockPath);
    if(!this->async){
        CFFdeventsFree(this->events);
    }
    return 0;
}

int MDSCmdSvrDataConnDoProcess(MDSCmdSvrDataConn* dConn)
{
    CFFdeventsDel(dConn->cmdSvr->events, &dConn->readEvt);
    dConn->cmdSvr->processRequest(dConn, dConn->cmdSvr->usrData);
    MDSCmdSvrDataConnResponse(dConn);
    return 0;
}

int MDSCmdSvrDataConnRequest(MDSCmdSvrDataConn* this)
{
    MDS_CMD_DBG("\n");
    if(this->status != MDS_CMD_SVR_DATA_CONN_ST_IDLE){
        MDS_CMD_ERR("Shouldn't be here\n");
        MDSCmdSvrDataConnFree(this);
        return -1;
    }
    this->tmpReaded = 0;
    this->status = MDS_CMD_SVR_DATA_CONN_ST_READ_REQ_VER;
    CFFdeventsAdd(this->cmdSvr->events, &this->readEvt);
    return 0;
}

int MDSCmdSvrDataConnResponse(MDSCmdSvrDataConn* this)
{
    if(this->status != MDS_CMD_SVR_DATA_CONN_ST_PROCESS){
        MDS_CMD_ERR("Shouldn't be here\n");
        MDSCmdSvrDataConnFree(this);
        return -1;
    }
    memcpy(this->response.version, MDS_CMD_PROTOCL_VER, MDS_CMD_PROTOCOL_VER_LEN);
    this->response.bodySize = CFBufferGetSize(&this->response.body);
    MDSCmdRespChksum(&this->response);
    this->tmpWrited = 0;
    this->status = MDS_CMD_SVR_DATA_CONN_ST_WRITE_RESP_VER;
    CFFdeventsAdd(this->cmdSvr->events, &this->writeEvt);
    
    return 0;
}

int MDSCmdSvrDataConnReadable(CFFdevents* events, CFFdevent* event, int fd, void* data)
{   
#define this ((MDSCmdSvrDataConn*)data)
    switch(this->status){
        
        case MDS_CMD_SVR_DATA_CONN_ST_READ_REQ_VER:
            MDS_CMD_DBG("server read version\n");
            if(CFAsyncRead(fd, this->request.version, MDS_CMD_PROTOCOL_VER_LEN, &this->tmpReaded)){
                MDS_CMD_ERR("\n");
                MDSCmdSvrDataConnFree(this);
                break;
            }
            if(this->tmpReaded == MDS_CMD_PROTOCOL_VER_LEN){
                if(memcmp(this->request.version, MDS_CMD_PROTOCL_VER, MDS_CMD_PROTOCOL_VER_LEN)){
                    MDS_CMD_ERR("\n");
                    MDSCmdSvrDataConnFree(this);
                    break;                    
                }
                this->tmpReaded = 0;
                this->status = MDS_CMD_SVR_DATA_CONN_ST_READ_REQ_BODY_SIZE;
            }else{
                break;
            }
        case MDS_CMD_SVR_DATA_CONN_ST_READ_REQ_BODY_SIZE:
            MDS_CMD_DBG("server read body size\n");
            if(CFAsyncRead(fd, &this->request.bodySize, sizeof(uint32), &this->tmpReaded)){
                MDS_CMD_ERR("\n");
                MDSCmdSvrDataConnFree(this);
                break;
            }
            if(this->tmpReaded ==  sizeof(uint32)){
                this->request.bodySize = ntohl(this->request.bodySize);
                MDS_CMD_DBG("server got body size = %lu\n", this->request.bodySize);
                this->tmpReaded = 0;
                this->status = MDS_CMD_SVR_DATA_CONN_ST_READ_REQ_BODY;
            }else{
                break;
            }
        case MDS_CMD_SVR_DATA_CONN_ST_READ_REQ_BODY:
            MDS_CMD_DBG("server read body\n");
            if(CFAsyncRead(fd, CFBufferGetPtr(&this->request.body), this->request.bodySize, &this->tmpReaded)){
                MDS_CMD_ERR("\n");
                MDSCmdSvrDataConnFree(this);
                break;
            }
            if(this->tmpReaded == this->request.bodySize){
                this->tmpReaded = 0;
                this->status = MDS_CMD_SVR_DATA_CONN_ST_READ_REQ_CHKSUM;
            }else{
                break;
            }            
        case MDS_CMD_SVR_DATA_CONN_ST_READ_REQ_CHKSUM:
            MDS_CMD_DBG("server read checksum\n");
            if(CFAsyncRead(fd, &this->request.chksum, sizeof(uint16), &this->tmpReaded)){
                MDS_CMD_ERR("\n");
                MDSCmdSvrDataConnFree(this);
                break;
            }
            if(this->tmpReaded == sizeof(uint16)){
                this->request.chksum = ntohs(this->request.chksum);
                if(!MDSCmdReqChksumOK(&this->request)){
                    MDS_CMD_ERR("\n");
                    MDSCmdSvrDataConnFree(this);
                    break;
                }
                this->tmpReaded = 0;
                this->status = MDS_CMD_SVR_DATA_CONN_ST_PROCESS;
            }else{
                break;
            }
        case MDS_CMD_SVR_DATA_CONN_ST_PROCESS:
            MDS_CMD_DBG("server do process\n");
            MDSCmdSvrDataConnDoProcess(this);
            break;
        default:
            MDS_CMD_ERR("Shouldn't be here\n");
            MDSCmdSvrDataConnFree(this);
            break;
    }
    return 0;
#undef this
}

int MDSCmdSvrDataConnWriteable(CFFdevents* events, CFFdevent* event, int fd, void* data)
{
#define this ((MDSCmdSvrDataConn*)data)
    switch(this->status){
        case MDS_CMD_SVR_DATA_CONN_ST_WRITE_RESP_VER:
            if(CFAsyncWrite(fd, this->response.version, MDS_CMD_PROTOCOL_VER_LEN, &this->tmpWrited)){
                MDS_CMD_ERR("\n");
                goto ERR_FREE_DATA_CONN;
            }
            if(MDS_CMD_PROTOCOL_VER_LEN == this->tmpWrited){
                this->tmpWrited = 0;
                this->status = MDS_CMD_SVR_DATA_CONN_ST_WRITE_RESP_BODY_SIZE;
            }else{
                break;
            }
        case MDS_CMD_SVR_DATA_CONN_ST_WRITE_RESP_BODY_SIZE:
            this->response.bodySize = htonl(this->response.bodySize);
            if(CFAsyncWrite(fd, &this->response.bodySize, sizeof(uint32), &this->tmpWrited)){
                MDS_CMD_ERR("\n");
                this->response.bodySize = ntohl(this->response.bodySize);
                goto ERR_FREE_DATA_CONN;
            }
            this->response.bodySize = ntohl(this->response.bodySize);
            if(sizeof(uint32)==this->tmpWrited){
                this->tmpWrited = 0;
                this->status = MDS_CMD_SVR_DATA_CONN_ST_WRITE_RESP_BODY;
            }else{
                break;
            }
        case MDS_CMD_SVR_DATA_CONN_ST_WRITE_RESP_BODY:
            if(CFAsyncWrite(fd, CFBufferGetPtr(&this->response.body), this->response.bodySize, &this->tmpWrited)){
                MDS_CMD_ERR("\n");
                goto ERR_FREE_DATA_CONN;
            }
            if(this->response.bodySize == this->tmpWrited){
                this->tmpWrited = 0;
                this->status = MDS_CMD_SVR_DATA_CONN_ST_WRITE_RESP_CHKSUM;
            }else{
                break;
            }
        case MDS_CMD_SVR_DATA_CONN_ST_WRITE_RESP_CHKSUM:
            this->response.chksum = htons(this->response.chksum);
            if(CFAsyncWrite(fd, &this->response.chksum, sizeof(uint16), &this->tmpWrited)){
                MDS_CMD_ERR("\n");
                this->response.chksum = ntohs(this->response.chksum);
                goto ERR_FREE_DATA_CONN;
            }
            this->response.chksum = ntohs(this->response.chksum);
            if(sizeof(uint16)==this->tmpWrited){
                this->tmpWrited = 0;
                CFFdeventsDel(this->cmdSvr->events, &this->writeEvt);
                this->status = MDS_CMD_SVR_DATA_CONN_ST_IDLE;
                MDSCmdSvrDataConnRequest(this);
            }
            break;           
        default:
            MDS_CMD_ERR("Shouldn't be here\n");
            goto ERR_FREE_DATA_CONN;
        break;
    }
    return 0;
ERR_FREE_DATA_CONN:
    MDSCmdSvrDataConnFree(this);
    return -1;
#undef this
}

MDSCmdSvrDataConn* MDSCmdSvrDataConnNew(MDSCmdSvr* svr, int fd)
{
    MDSCmdSvrDataConn* newConn;

    if(!(newConn = malloc(sizeof(MDSCmdSvrDataConn)))){
        MDS_CMD_ERR("\n");
        return NULL;
    }
    if(MDSCmdSvrDataConnInit(newConn, svr, fd)){
        MDS_CMD_ERR("\n");
        free(newConn);
        return NULL;        
    }

    return newConn;
}

int MDSCmdSvrDataConnInit(MDSCmdSvrDataConn* this, MDSCmdSvr* svr, int fd)
{
    this->fd = fd;
    if(MDSCmdReqInit(&this->request)){
        MDS_CMD_ERR("\n");
        return -1;
    }
    if(MDSCmdRespInit(&this->response)){
        MDS_CMD_ERR("\n");
        return -1;
    }
    CFFdeventInit(&this->readEvt, fd, MDSCmdSvrDataConnReadable, (void*)this, NULL, NULL);
    CFFdeventInit(&this->writeEvt, fd, NULL, NULL, MDSCmdSvrDataConnWriteable, (void*)this);
    this->status = MDS_CMD_SVR_DATA_CONN_ST_IDLE;
    this->cmdSvr = svr;
    CFListInit(&this->list);
    MDS_CMD_DBG("1=0x%x, 2=0x%x\n", (unsigned int)&svr->dataConnHead.list, (unsigned int)&this->list);
    CFListInsertPre(&svr->dataConnHead.list, &this->list);   /* Add data connection to server */
    MDS_CMD_DBG("\n");    
    return 0;
}

int MDSCmdSvrDataConnExit(MDSCmdSvrDataConn* this)
{
    /* TODO: process status */
    switch(this->status){
        case MDS_CMD_SVR_DATA_CONN_ST_IDLE:
            break;
        case MDS_CMD_SVR_DATA_CONN_ST_READ_REQ_VER:
        case MDS_CMD_SVR_DATA_CONN_ST_READ_REQ_BODY_SIZE:
        case MDS_CMD_SVR_DATA_CONN_ST_READ_REQ_BODY:
        case MDS_CMD_SVR_DATA_CONN_ST_READ_REQ_CHKSUM:
            CFFdeventsDel(this->cmdSvr->events, &this->readEvt);
            break;
        case MDS_CMD_SVR_DATA_CONN_ST_PROCESS:
            break;
        case MDS_CMD_SVR_DATA_CONN_ST_WRITE_RESP_VER:
        case MDS_CMD_SVR_DATA_CONN_ST_WRITE_RESP_BODY_SIZE:
        case MDS_CMD_SVR_DATA_CONN_ST_WRITE_RESP_BODY:
            CFFdeventsDel(this->cmdSvr->events, &this->writeEvt);
            break;
        default:
            MDS_CMD_ERR("\n");
            break;
    }
    this->status = MDS_CMD_SVR_DATA_CONN_ST_IDLE;
    close(this->fd);
    this->fd = -1;
    this->cmdSvr = NULL;
    CFListDel(&this->list);
    CFListInit(&this->list);
    CFFdeventExit(&this->writeEvt);
    CFFdeventExit(&this->readEvt);
    MDSCmdRespExit(&this->response);
    MDSCmdReqExit(&this->request);
    return 0;
}

int MDSCmdSvrDataConnFree(MDSCmdSvrDataConn* this)
{
    MDSCmdSvrDataConnExit(this);
    free(this);
    return 0;
}

static int MDSCmdSvrSockReadable(CFFdevents* events, CFFdevent* event, int fd, void* context)
{
    int connFd;
    MDSCmdSvrDataConn* dConn;
    
    if((connFd = accept(event->fd, NULL, NULL)) == -1){
        MDS_CMD_ERR("accept fd on unix socket failed\n");
        return -1;
    }
    MDS_CMD_DBG("\n");
    if(!(dConn = MDSCmdSvrDataConnNew(((MDSCmdSvrSockConn*)context)->cmdSvr, connFd))){    /* new nata connection */
        MDS_CMD_ERR("\n");
        return -1;
    }
    MDS_CMD_DBG("\n");
    MDSCmdSvrDataConnRequest(dConn);
    return 0;
}

int MDSCmdSvrInit(MDSCmdSvr* this, const char* unixSockPath, int maxDataConns, 
		int(*processRequest)(MDSCmdSvrDataConn* dataConn, void* usrData), void* usrData,
		CFFdevents* events)
{
    struct sockaddr_un addr;
    
    MDS_CMD_DBG("\n");
    this->sockConn.cmdSvr = this;
    this->sockConn.unixSockPath = unixSockPath;
    this->sockConn.fd = socket(AF_UNIX, SOCK_STREAM, 0);
    MDS_CMD_DBG("\n");
    if(this->sockConn.fd == -1){
        MDS_CMD_ERR("Can not initial unix socket for cmd element\n");
        goto ERR_OUT;
    }
    MDS_CMD_DBG("\n");
    memset(&addr, 0, sizeof(struct sockaddr_un));
    addr.sun_family = AF_UNIX;
    remove(this->sockConn.unixSockPath);
    strcpy(addr.sun_path, this->sockConn.unixSockPath);
    if(bind(this->sockConn.fd, (struct sockaddr* )&addr, offsetof(struct sockaddr_un, sun_path)+strlen(addr.sun_path))){
        MDS_CMD_ERR("bind unix socket for cmd interace failed\n");
        goto ERR_CLOSE_SOCK_FD;
    }
    if (-1 == listen(this->sockConn.fd, maxDataConns)) {
		MDS_CMD_ERR("listen failed: %s", strerror(errno));
        goto ERR_CLOSE_SOCK_FD;
	}
    if(CFFdeventInit(&this->sockConn.readEvt, 
    this->sockConn.fd, MDSCmdSvrSockReadable, (void*)(&this->sockConn), NULL, NULL)){
        MDS_CMD_ERR("Can not initial unix socket for cmd element\n");
        goto ERR_CLOSE_SOCK_FD;
    }
    this->events = events;
    this->processRequest = processRequest;
    MDS_CMD_DBG("\n");
    CFFdeventsAdd(this->events, &this->sockConn.readEvt);
    MDS_CMD_DBG("\n");
    MDSCmdSvrDataConnInit(&this->dataConnHead, this, -1);
    this->usrData = usrData;
    MDS_CMD_DBG("%s:%d\n", __FILE__, __LINE__);
    return 0;
ERR_CLOSE_SOCK_FD:
    close(this->sockConn.fd);
ERR_OUT:
    return -1;
}

int MDSCmdSvrExit(MDSCmdSvr* this)
{
    CFListContainerForeachSafe(&this->dataConnHead, tmpDConn, MDSCmdSvrDataConn, list){
        MDSCmdSvrDataConnFree(tmpDConn);
    }
    CFFdeventsDel(this->events, &this->sockConn.readEvt);
    close(this->sockConn.fd);
    remove(this->sockConn.unixSockPath);
    return 0;
}
