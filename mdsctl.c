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
#include <string.h>
#include <signal.h>
#include <cf_buffer.h>
#include <cf_sigfd.h>
#include "mds_cmd.h"
#include "mds_log.h"
#define MDSCTL_ERR  CF_ERR
#define MDSCTL_MSG  MESSAGE_LOG
#define MDSCTL_DBG  CF_DBG
#define MDSCTL_ERR_TO(_ERR_LINE_, ...)  {CF_ERR(__VA_ARGS__);goto _ERR_LINE_;}

#include <cf_fdevent.h>

typedef struct mds_ctl{
    enum {MDS_CTL_ST_IDLE, MDS_CTL_ST_READ_STDIN, MDS_CTL_ST_CMD_PROC, MDS_CTL_ST_WRITE_STDOUT} status;
    CFFdevents events;
    CFFdevent sigFdEvent;
    CFBuffer stdinBuf;
    CFFdevent stdinEvt;
    CFBuffer stdoutBuf;
    CFFdevent stdoutEvt;
    MDSCmdCtl cmdCtl;
}MDSCtl;
int MDSCtlResetToIdle(MDSCtl* this);

int MDSCtlStdoutWriteable(CFFdevents* events, CFFdevent* event, int fd, void* ctx)
{
#define this ((MDSCtl*)ctx)
    MDSCTL_DBG("stdout writeable\n");
    MDSCTL_DBG("stdoutBuf->size=%d\n", CFBufferGetSize(&this->stdoutBuf));
    //CFBufferCp(&this->stdoutBuf, "abcd\n", 5);
    if(CFBufferWrite(fd, &this->stdoutBuf, TRUE)){
        MDSCTL_ERR("\n");
        MDSCtlResetToIdle(this);
        return -1;
    }
    if(!CFBufferGetSize(&this->stdoutBuf)){
        MDSCTL_DBG("\n");
        CFFdeventsDel(&this->events, &this->stdoutEvt);
        CFFdeventsAdd(&this->events, &this->stdinEvt);
        this->status = MDS_CTL_ST_READ_STDIN;
    }
    return 0;
#undef this
}

int MDSCtlStdinReadable(CFFdevents* events, CFFdevent* event, int fd, void* ctx)
{
#define this ((MDSCtl*)ctx)
    char *e, *p;

    MDSCTL_DBG("stdin readable\n");
    CFBufferRead(fd, &this->stdinBuf, TRUE);
    MDSCTL_DBG("stdinBuf->size=%d\n", CFBufferGetSize(&this->stdinBuf));
    e = CFBufferGetPtr(&this->stdinBuf);
    if((p=memrchr(e, ';', CFBufferGetSize(&this->stdinBuf)))){
        MDSCTL_DBG("stdin got ';'\n");
        *p = '\0';
        p = strchr(e, ' ');
        *p = '\0';
        p++;
        CFFdeventsDel(&this->events, &this->stdinEvt);
        MDSCTL_DBG("e: %s, p: %s", e, p);
        MDSCmdCtlRequest(&this->cmdCtl, e, p, strlen(p)+1, NULL);
        CFBufferCp(&this->stdoutBuf, CFBufferGetPtr(&this->cmdCtl.response.body), CFBufferGetSize(&this->cmdCtl.response.body));
        CFBufferCp(&this->stdinBuf, NULL,  0);
        CFFdeventsAdd(&this->events, &this->stdoutEvt);
        this->status = MDS_CTL_ST_WRITE_STDOUT;
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
            MDSCTL_ERR("unknown signal, should not be here\n");
            break;
    }
    return 0;
}

int MDSCtlInit(MDSCtl* this)
{
    int sigFd;

    if(-1==(sigFd = CFSigFdOpen(SIGINT, SIGTERM))){
        MDSCTL_ERR("\n");
        goto ERR_OUT;
    }
    if(CFFdeventInit(&this->sigFdEvent, sigFd, MDSCtlSigFdReadable, (void*)this, NULL, NULL)){
        MDSCTL_ERR("\n");
        goto ERR_SIGFD_CLOSE;
    }
    if(CFBufferInit(&this->stdinBuf, 1024)){
        MDSCTL_ERR_TO(ERR_SIGFD_EVT_EXIT, "\n");
    }
    if(CFBufferInit(&this->stdoutBuf, 1024)){
        MDSCTL_ERR_TO(ERR_STDIN_BUF_EXIT, "\n");
    }
    fcntl(0, F_SETFL, fcntl(0, F_GETFL)|O_NONBLOCK);
    fcntl(1, F_SETFL, fcntl(1, F_GETFL)|O_NONBLOCK);
    if(CFFdeventInit(&this->stdinEvt, 0, MDSCtlStdinReadable, this, NULL, NULL)){
        MDSCTL_ERR_TO(ERR_STDOUT_BUF_EXIT, "\n");
    }
    if(CFFdeventInit(&this->stdoutEvt, 1, NULL, NULL, MDSCtlStdoutWriteable, this)){
        MDSCTL_ERR_TO(ERR_STDIN_EVT_EXIT, "\n");
    }
    if(CFFdeventsInit(&this->events)){
        MDSCTL_ERR_TO(ERR_STDOUT_EVT_EXIT, "\n");
    }
    if(MDSCmdCtlInit(&this->cmdCtl, NULL) /* sync request */){
        MDSCTL_ERR_TO(ERR_EVTS_EXIT, "\n");
    }
    if(MDSCmdCtlConnect(&this->cmdCtl, "/tmp/mds_cmd.sock")){
        MDSCTL_ERR_TO(ERR_CTL_EXIT, "\n");
    }
    this->status = MDS_CTL_ST_IDLE;
    if(CFFdeventsAdd(&this->events, &this->sigFdEvent)){
        MDSCTL_ERR_TO(ERR_CTL_EXIT, "\n");
    }
    return 0;

ERR_CTL_EXIT:
    MDSCmdCtlExit(&this->cmdCtl);
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
    MDSCmdCtlExit(&this->cmdCtl);
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
            MDSCmdCtlResetToIdle(&this->cmdCtl);
            CFFdeventsDel(&this->events, &this->stdinEvt);
            CFFdeventsDel(&this->events, &this->sigFdEvent);
            break;
        case MDS_CTL_ST_WRITE_STDOUT:
            MDSCmdCtlResetToIdle(&this->cmdCtl);
            CFFdeventsDel(&this->events, &this->stdoutEvt);
            CFFdeventsDel(&this->events, &this->sigFdEvent);
            break;
        case MDS_CTL_ST_IDLE:
            CFFdeventsDel(&this->events, &this->sigFdEvent);
        default:
            MDSCTL_ERR("Shouldn't come to here\n");
            return -1;
    }
    return 0;
}

int MDSCtlRun(MDSCtl* this)
{
    if(CFFdeventsAdd(&this->events, &this->stdinEvt)){
        MDSCTL_ERR("\n");
        return -1;
    }
    this->status = MDS_CTL_ST_READ_STDIN;
    return CFFdeventsLoop(&this->events);
}

int main(int argc, char** argv)
{
    static MDSCtl ctl;

    if(MDSCtlInit(&ctl)){
        MDSCTL_ERR_TO(ERR_OUT, "\n");
    }
    if(MDSCtlRun(&ctl)){
        MDSCTL_ERR_TO(ERR_MDS_CTL_EXIT, "\n");
    }
    MDSCtlExit(&ctl);
    return 0;
ERR_MDS_CTL_EXIT:
    MDSCtlExit(&ctl);
ERR_OUT:
    return -1;
}
