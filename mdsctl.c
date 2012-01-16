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
 
#define  _GNU_SOURCE    /* for memrchr */
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <cf_buffer.h>
#include <cf_sigfd.h>
#include <cf_cmd.h>
#include <cf_fdevent.h>
#include "mds_log.h"

typedef struct mds_ctl{
    enum {MDS_CTL_ST_IDLE, MDS_CTL_ST_READ_STDIN, MDS_CTL_ST_CMD_PROC, MDS_CTL_ST_WRITE_STDOUT} status;
    CFFdevents events;
    CFFdevent sigFdEvent;
    CFBuffer stdinBuf;
    CFFdevent stdinEvt;
    CFBuffer stdoutBuf;
    CFFdevent stdoutEvt;
    CFCmdCtl cmdCtl;
}MDSCtl;
int MDSCtlResetToIdle(MDSCtl* this);

int MDSCtlStdoutWriteable(CFFdevents* events, CFFdevent* event, int fd, void* ctx)
{
#define this ((MDSCtl*)ctx)
    MDS_DBG("stdout writeable\n");
    MDS_DBG("stdoutBuf->size=%d\n", CFBufferGetSize(&this->stdoutBuf));
    //CFBufferCp(&this->stdoutBuf, "abcd\n", 5);
    if(CFBufferWrite(fd, &this->stdoutBuf, TRUE)){
        MDS_ERR("\n");
        MDSCtlResetToIdle(this);
        return -1;
    }
    if(!CFBufferGetSize(&this->stdoutBuf)){
        MDS_DBG("\n");
        CFFdeventsDel(&this->events, &this->stdoutEvt);
        CFFdeventsAdd(&this->events, &this->stdinEvt);
        this->status = MDS_CTL_ST_READ_STDIN;
    }
    return 0;
#undef this
}

int MDSCtlRequest(MDSCtl* ctl, const char* elem, const char* cmd)
{
    int ret;
    CFBuffer reqBuf;
    
    CFBufferInit(&reqBuf, 1024);
    CFBufferCp(&reqBuf, elem, strlen(elem)+1);
    CFBufferCat(&reqBuf, cmd, strlen(cmd)+1);
    ret = CFCmdCtlRequest(&ctl->cmdCtl, CFBufferGetPtr(&reqBuf), CFBufferGetSize(&reqBuf), NULL, NULL);
    CFBufferExit(&reqBuf);
    return ret;
}

int MDSCtlStdinReadable(CFFdevents* events, CFFdevent* event, int fd, void* ctx)
{
#define this ((MDSCtl*)ctx)
    char *e, *p, *msg;

    MDS_DBG("stdin readable\n");
    CFBufferRead(fd, &this->stdinBuf, TRUE);
    MDS_DBG("stdinBuf->size=%d\n", CFBufferGetSize(&this->stdinBuf));
    e = CFBufferGetPtr(&this->stdinBuf);
    if (e[CFBufferGetSize(&this->stdinBuf)-1] == '\n') {
        if (CFBufferGetSize(&this->stdinBuf)>1) {
            if ((p=memrchr(e, ';', CFBufferGetSize(&this->stdinBuf)))) {
                MDS_DBG("stdin got ';'\n");
                *p = '\0';
                if ((p = strchr(e, ' '))) {
                    *p = '\0';
                    msg = p+1;
                    MDS_DBG("elem: %s, cmd: %s\n", e, msg);
                    if (!MDSCtlRequest(this, e, msg)) {
                        CFBufferCp(&this->stdoutBuf, CFBufferGetPtr(&this->cmdCtl.response.body), CFBufferGetSize(&this->cmdCtl.response.body));
                    } else {
                        CFBufferCp(&this->stdoutBuf, CF_CONST_STR_LEN("[Error] Request faile\n"));
                    }
                } else {
                    CFBufferCp(&this->stdoutBuf, CF_CONST_STR_LEN("[Error] Wrong request format\n"));
                }
            } else {
                CFBufferCp(&this->stdoutBuf, CF_CONST_STR_LEN("[Error] Wrong request format\n"));
            }
            CFFdeventsDel(&this->events, &this->stdinEvt);
            CFBufferCp(&this->stdinBuf, NULL,  0);
            CFFdeventsAdd(&this->events, &this->stdoutEvt);
            this->status = MDS_CTL_ST_WRITE_STDOUT;
        } else {
            CFBufferCp(&this->stdinBuf, NULL,  0);
        }
    }
    return 0;
#undef this
}

static int MDSCtlSigFdReadable(CFFdevents* events, CFFdevent* event, int fd, void* ctx)
{
    switch(CFSigFdGetNextSignal(fd)){
        case SIGINT:
        case SIGTERM:
            MDSCtlResetToIdle((MDSCtl*)ctx);
            break;
        default:
            MDS_ERR("unknown signal, should not be here\n");
            break;
    }
    return 0;
}

int MDSCtlInit(MDSCtl* this)
{
    int sigFd;

    if(-1==(sigFd = CFSigFdOpen(SIGINT, SIGTERM))){
        MDS_ERR("\n");
        goto ERR_OUT;
    }
    if(CFFdeventInit(&this->sigFdEvent, sigFd, MDSCtlSigFdReadable, (void*)this, NULL, NULL)){
        MDS_ERR("\n");
        goto ERR_SIGFD_CLOSE;
    }
    if(CFBufferInit(&this->stdinBuf, 1024)){
        MDS_ERR_OUT(ERR_SIGFD_EVT_EXIT, "\n");
    }
    if(CFBufferInit(&this->stdoutBuf, 1024)){
        MDS_ERR_OUT(ERR_STDIN_BUF_EXIT, "\n");
    }
    fcntl(0, F_SETFL, fcntl(0, F_GETFL)|O_NONBLOCK);
    fcntl(1, F_SETFL, fcntl(1, F_GETFL)|O_NONBLOCK);
    if(CFFdeventInit(&this->stdinEvt, 0, MDSCtlStdinReadable, this, NULL, NULL)){
        MDS_ERR_OUT(ERR_STDOUT_BUF_EXIT, "\n");
    }
    if(CFFdeventInit(&this->stdoutEvt, 1, NULL, NULL, MDSCtlStdoutWriteable, this)){
        MDS_ERR_OUT(ERR_STDIN_EVT_EXIT, "\n");
    }
    if(CFFdeventsInit(&this->events)){
        MDS_ERR_OUT(ERR_STDOUT_EVT_EXIT, "\n");
    }
    if(CFCmdCtlInit(&this->cmdCtl, NULL) /* sync request */){
        MDS_ERR_OUT(ERR_EVTS_EXIT, "\n");
    }
    if(CFCmdCtlConnect(&this->cmdCtl, "/tmp/mds_cmd.sock")){
        MDS_ERR_OUT(ERR_CTL_EXIT, "\n");
    }
    this->status = MDS_CTL_ST_IDLE;
    if(CFFdeventsAdd(&this->events, &this->sigFdEvent)){
        MDS_ERR_OUT(ERR_CTL_EXIT, "\n");
    }
    return 0;

ERR_CTL_EXIT:
    CFCmdCtlExit(&this->cmdCtl);
ERR_EVTS_EXIT:
    CFFdeventsExit(&this->events);
ERR_STDOUT_EVT_EXIT:
    CFFdeventExit(&this->stdoutEvt);
ERR_STDIN_EVT_EXIT:
    CFFdeventExit(&this->stdinEvt);
ERR_STDOUT_BUF_EXIT:
    CFBufferExit(&this->stdoutBuf);
ERR_STDIN_BUF_EXIT:
    CFBufferExit(&this->stdinBuf);
ERR_SIGFD_EVT_EXIT:
    CFFdeventExit(&this->sigFdEvent);
ERR_SIGFD_CLOSE:
    CFSigFdClose(sigFd);
ERR_OUT:
    return -1;
}

int MDSCtlExit(MDSCtl* this)
{
    CFSigFdClose(&this->sigFdEvent.fd);
    CFFdeventExit(&this->sigFdEvent);
    CFCmdCtlExit(&this->cmdCtl);
    CFFdeventsExit(&this->events);
    CFFdeventExit(&this->stdoutEvt);
    CFFdeventExit(&this->stdinEvt);
    CFBufferExit(&this->stdoutBuf);
    CFBufferExit(&this->stdinBuf);
    return -1;
}

int MDSCtlResetToIdle(MDSCtl* this)
{
    switch(this->status){
        case  MDS_CTL_ST_READ_STDIN:
            MDS_DBG("\n");
            CFCmdCtlResetToIdle(&this->cmdCtl);
            CFFdeventsDel(&this->events, &this->stdinEvt);
            CFFdeventsDel(&this->events, &this->sigFdEvent);
            break;
        case MDS_CTL_ST_WRITE_STDOUT:
            MDS_DBG("\n");
            CFCmdCtlResetToIdle(&this->cmdCtl);
            CFFdeventsDel(&this->events, &this->stdoutEvt);
            CFFdeventsDel(&this->events, &this->sigFdEvent);
            break;
        case MDS_CTL_ST_IDLE:
            MDS_DBG("\n");
            CFFdeventsDel(&this->events, &this->sigFdEvent);
        default:
            MDS_ERR("Shouldn't come to here\n");
            return -1;
    }
    return 0;
}

int MDSCtlRun(MDSCtl* this, const char* elem, const char* msg)
{
    if (!elem || !msg) {
        if(CFFdeventsAdd(&this->events, &this->stdinEvt)){
            MDS_ERR("\n");
            return -1;
        }
        this->status = MDS_CTL_ST_READ_STDIN;
        return CFFdeventsLoop(&this->events);
    } else {
        MDSCtlRequest(this, elem, msg);
        return (CFBufferGetSize(&this->cmdCtl.response.body)
                !=write(1, CFBufferGetPtr(&this->cmdCtl.response.body), CFBufferGetSize(&this->cmdCtl.response.body)));
    }
}
void usage()
{
    puts("mdsctl <> [<element> <command>]");
}

int main(int argc, char** argv)
{
    static MDSCtl ctl;
    const char *argv1, *argv2;

    if (argc==3) {
        argv1 = argv[1];
        argv2 = argv[2];
    }else if (argc==1) {
        argv1 = NULL;
        argv2 = NULL;
    } else {
        usage();
        MDS_ERR_OUT(ERR_OUT, "\n");
    }
    if(MDSCtlInit(&ctl)){
        MDS_ERR_OUT(ERR_OUT, "\n");
    }
    if(MDSCtlRun(&ctl, argv1, argv2)){
        MDS_ERR_OUT(ERR_MDS_CTL_EXIT, "\n");
    }
    
    MDSCtlExit(&ctl);
    return 0;
ERR_MDS_CTL_EXIT:
    MDSCtlExit(&ctl);
ERR_OUT:
    return -1;
}
