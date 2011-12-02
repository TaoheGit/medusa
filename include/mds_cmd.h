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

#ifndef _MDS_CMD_H_
#define _MDS_CMD_H_
#include <cf_common.h>
#include <cf_fdevent.h>
#include <cf_string.h>
#include <cf_buffer.h>
#include "medusa.h"

#define MDS_CMD_PROTOCL_VER "MDSCmd/0.1"
#define MDS_CMD_PROTOCOL_VER_LEN    sizeof(MDS_CMD_PROTOCL_VER)
typedef struct mds_cmd_request{

    unsigned char version[MDS_CMD_PROTOCOL_VER_LEN];  /* Also used for  maigc */
    uint32 bodySize;
    CFBuffer body; /* pointer to packet body, body begins with guest's name(NULL terminated string)*/
    uint16 chksum;
}MDSCmdReq;
int MDSCmdReqInit(MDSCmdReq* req);
int MDSCmdReqExit(MDSCmdReq* req);
int MDSCmdReqChksum(MDSCmdReq* req);
BOOL MDSCmdReqChksumOK(MDSCmdReq* req);

typedef struct mds_cmd_response{
    unsigned char version[MDS_CMD_PROTOCOL_VER_LEN];  /* Also used for  magic */
    uint32 bodySize;
    CFBuffer body;
    uint16 chksum;
}MDSCmdResp;
int MDSCmdRespInit(MDSCmdResp*);
int MDSCmdRespExit(MDSCmdResp*);
int MDSCmdRespChksum(MDSCmdResp*);
BOOL MDSCmdRespChksumOK(MDSCmdResp*);
BOOL MDSCmdRespIsHeadValid(MDSCmdResp* head);

typedef struct mds_cmd_ctl{
    BOOL async;
    CFString unixSockPath;
    int fd;
    int(*processResponse)(struct mds_cmd_ctl* this);

    CFFdevent readEvt;
    MDSCmdReq request;
    int tmpReaded;
    CFFdevent writeEvt;
    MDSCmdResp response;
    int tmpWrited;

    CFFdevents* events;
    enum{
        MDS_CMD_CTL_ST_IDLE,
        MDS_CMD_CTL_ST_CONNECTED_IDLE,
        MDS_CMD_CTL_ST_WRITE_REQ_VER,
        MDS_CMD_CTL_ST_WRITE_REQ_BODY_SIZE,
        MDS_CMD_CTL_ST_WRITE_REQ_BODY,
        MDS_CMD_CTL_ST_WRITE_REQ_CHKSUM,
        MDS_CMD_CTL_ST_READ_RESP_VER,
        MDS_CMD_CTL_ST_READ_RESP_BODY_SIZE,
        MDS_CMD_CTL_ST_READ_RESP_BODY,
        MDS_CMD_CTL_ST_READ_RESP_CHKSUM,
        MDS_CMD_CTL_ST_PROCESS_RESP
    } status;
    enum{
        MDS_CTL_REQ_RESULT_OK,
        MDS_CTL_REQ_RESULT_ERROR
    } reqResult;
}MDSCmdCtl;
int MDSCmdCtlInit(MDSCmdCtl* this, CFFdevents* events);
int MDSCmdCtlConnect(MDSCmdCtl* ctl, const char* unixSockPath);
int MDSCmdCtlRequest(MDSCmdCtl* ctl, const char* element, const char* reqData, int reqDataLen,     int(*processResponse)(MDSCmdCtl* this));
int MDSCmdCtlExit(MDSCmdCtl* ctl);
int MDSCmdCtlResetToIdle(MDSCmdCtl* this);

typedef struct mds_cmd_svr MDSCmdSvr;
typedef struct mds_cmd_svr_sock_conn{
    MDSCmdSvr* cmdSvr;
    const char* unixSockPath;  /* unix socket path */
    int fd;
    CFFdevent readEvt;
}MDSCmdSvrSockConn;

typedef struct mds_cmd_svr_data_conn{
    MDSCmdSvr* cmdSvr;
    int fd;

    CFFdevent readEvt;
    MDSCmdReq request;
    int tmpReaded;
    CFFdevent writeEvt;
    MDSCmdResp response;
    int tmpWrited;
    enum{
        MDS_CMD_SVR_DATA_CONN_ST_IDLE,
        MDS_CMD_SVR_DATA_CONN_ST_READ_REQ_VER,
        MDS_CMD_SVR_DATA_CONN_ST_READ_REQ_BODY_SIZE,
        MDS_CMD_SVR_DATA_CONN_ST_READ_REQ_BODY,
        MDS_CMD_SVR_DATA_CONN_ST_READ_REQ_CHKSUM,
        MDS_CMD_SVR_DATA_CONN_ST_PROCESS,
        MDS_CMD_SVR_DATA_CONN_ST_WRITE_RESP_VER,
        MDS_CMD_SVR_DATA_CONN_ST_WRITE_RESP_BODY_SIZE,
        MDS_CMD_SVR_DATA_CONN_ST_WRITE_RESP_BODY,
        MDS_CMD_SVR_DATA_CONN_ST_WRITE_RESP_CHKSUM

    } status;

    CFListHead list;
}MDSCmdSvrDataConn;
MDSCmdSvrDataConn* MDSCmdSvrDataConnNew(MDSCmdSvr* svr, int fd);
int MDSCmdSvrDataConnInit(MDSCmdSvrDataConn* this, MDSCmdSvr* svr, int fd);
int MDSCmdSvrDataConnFree(MDSCmdSvrDataConn* this);
int MDSCmdSvrDataConnExit(MDSCmdSvrDataConn* this);


struct mds_cmd_svr{
        void* usrData;
        MDSCmdSvrSockConn       sockConn;
        MDSCmdSvrDataConn dataConnHead;
    int(*processRequest)(MDSCmdSvrDataConn* dataConn, void* usrData);
    CFFdevents* events;
};
int MDSCmdSvrInit(MDSCmdSvr* this, const char* unixSockPath, int maxDataConns,
                int(*processRequest)(MDSCmdSvrDataConn* dataConn, void* usrData), void* usrData,
                CFFdevents* events);
int MDSCmdSvrExit(MDSCmdSvr* this);
int MDSCmdSvrDataConnResponse(MDSCmdSvrDataConn* this);
#endif
