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
 * [2012-4-5] atomctl IQ support added
 * 
 */


/*
 * [ xmpp module ]
 * 
 * input type: JSON|cmd
 * input example: {
        "todo":"message"
 *      "to":"Kerry"
 *      "msg":"[100, 30], direction, speed"
 * }
 * 
 * output type: JSON
 * output data: {
 *      "from":"Kerry",
 *      "date":"1986-9-12,15:15:15",
 *      "msg":"//debug Motor speed up"
 * }
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
#include <cf_buffer.h>
#include <iksemel.h>
#include <cf_xmpp.h>
#include <cf_timer.h>
#include "medusa.h"
#include "mds_log.h"
#include "plug_cmd.h"
#include "mds_msg_movement.h"

#define MDS_MSG_TYPE_JSON "JSON"
#define MDS_XC_ELEM_CLASS_NAME   "XMPP_CLIENT"
typedef struct XC_elem{
    MDS_ELEM_MEMBERS;
    XC xc;
    int show;
    CFString status;
    CFTimer xcStartTmr;
    BOOL supportAtomCtl;
}MdsXCElem;

#define MDS_XC_PLUGIN_NAME       "PLUG_XMPP"
typedef struct mds_XC_plug{
    MDS_PLUGIN_MEMBERS;
}MDSXCPlug;

static int _XCPlugInit(MDSPlugin* this, MDSServer* svr);
static int _XCPlugExit(MDSPlugin* this, MDSServer* svr);
MDSXCPlug xmpp = {
    .name = MDS_XC_PLUGIN_NAME,
    .init = _XCPlugInit,
    .exit = _XCPlugExit
};

static MDSElem* _XCElemRequested(MDSServer* svr, CFJson* jConf);
static int _XCElemReleased(MDSElem* elem);
MdsElemClass _XCClass = {
    .name = MDS_XC_ELEM_CLASS_NAME,
    .request = _XCElemRequested,
    .release = _XCElemReleased,
};

static void scheduleStart(MdsXCElem* xcEm);

static int XCElemSendPlotIq(MdsXCElem* xcEm, CFJson* jMsg)
{
    int ret = 0;
    const char *to, *plot;
    CFString plotStr;

    CFStringInit(&plotStr, "<plot xmlns=\"org.gnu.plot\">");
    if ((to=CFJsonObjectGetString(jMsg, "to"))
            &&(plot=CFJsonObjectGetString(jMsg, "plot"))) {
        CFStringSafeCat(&plotStr, plot);
        CFStringSafeCat(&plotStr, "</plot>");
        ret = XCSendIq(&xcEm->xc, NULL, to, "set", CFStringGetStr(&plotStr));
    } else {
        ret = -1;
    }
    CFStringExit(&plotStr);
    return ret;
}
/*
Mesage command
==============
{
    "todo": "message"
    "to": "CrazyPandar@gmail.com",
    "msg": "Hello RD2"
}

Presence command
================
{
    "todo": "presense",
    "to":"",    ;Optional
    "show": "available|chat|away|dnd|xa",
    "status": "Enjoy inteligent life!"      ;Optional
}
*/
static int __MdsXCProcess(MDSElem* this, MDSElem* vendor, MdsMsg* msg)
{
    MdsXCElem* xcEm = (MdsXCElem*)this;
    CFJson *jMsg;
    const char *tmpCStr;
    int ret;
    
    if (!strcmp(msg->type, MDS_MSG_TYPE_JSON)) {  /* JSON */
        jMsg = (CFJson*)(MdsCmdMsg*)msg->data;
        tmpCStr = CFJsonObjectGetString(jMsg, "todo");
        if (!strcmp(tmpCStr, "message")) {
            if ((ret=XCSendMsgByJson(&xcEm->xc, jMsg))) {
                MDS_ERR("<%s> Sent failed", CFStringGetStr(&xcEm->name));
            }
        } else if (!strcmp(tmpCStr, "presence")) {
            if ((ret=XCPresenceByJson(&xcEm->xc, jMsg))) {
                MDS_ERR("<%s> Presence failed", CFStringGetStr(&xcEm->name));
            }
        } else {
            MDS_ERR("<%s> todo type: %s not support!\n", CFStringGetStr(&xcEm->name), tmpCStr);
            ret = -1;
        }
        return ret;
    } else if (!strcmp(msg->type, MDS_MSG_TYPE_CMD)){
        const char *jMsgStr;
        CFBuffer *respBuf;
        
        jMsgStr = ((MdsCmdMsg*)msg->data)->cmd;
        respBuf = ((MdsCmdMsg*)msg->data)->respBuf;
        MDS_DBG("jMsgStr:%s\n", jMsgStr);
        jMsg = CFJsonParse(jMsgStr);
        if (!jMsg) {
            CFBufferCp(respBuf, CF_CONST_STR_LEN("Wrong cmd format\n"));
            return 0;
        }
        
        tmpCStr = CFJsonObjectGetString(jMsg, "todo");
        if (!tmpCStr) {
            CFBufferCp(respBuf, CF_CONST_STR_LEN("no todo option in command\n"));
            ret = -1;
        } else {
            if (!strcmp(tmpCStr, "message")) {
                if ((ret=XCSendMsgByJson(&xcEm->xc, jMsg))) {
                    CFBufferCp(respBuf, CF_CONST_STR_LEN("fail\n"));
                } else {
                    CFBufferCp(respBuf, CF_CONST_STR_LEN("ok\n"));
                }
            } else  if (!strcmp(tmpCStr, "presense")) {
                if ((ret=XCPresenceByJson(&xcEm->xc, jMsg))) {
                    CFBufferCp(respBuf, CF_CONST_STR_LEN("fail\n"));
                } else {
                    CFBufferCp(respBuf, CF_CONST_STR_LEN("ok\n"));
                }
			} else  if (!strcmp(tmpCStr, "iq")) {
                if ((ret=XCSendIqByJson(&xcEm->xc, jMsg))) {
                    CFBufferCp(respBuf, CF_CONST_STR_LEN("fail\n"));
                } else {
                    CFBufferCp(respBuf, CF_CONST_STR_LEN("ok\n"));
                }
            } else  if (!strcmp(tmpCStr, "plot")) {
                if ((ret=XCElemSendPlotIq(xcEm, jMsg))) {
                    CFBufferCp(respBuf, CF_CONST_STR_LEN("fail\n"));
                } else {
                    CFBufferCp(respBuf, CF_CONST_STR_LEN("ok\n"));
                }
            } else {
                MDS_ERR("<%s> todo type: %s not support!\n", CFStringGetStr(&xcEm->name), tmpCStr);
                ret = 0;
            }
        }
        
        CFJsonPut(jMsg);
        return ret;
    } else {
        MDS_ERR("<%s> Msg type: %s not support!\n", CFStringGetStr(&xcEm->name), msg->type);
        return -1;
    }
    
}

static int __MdsXCAddAsGuest(MDSElem* elem, MDSElem* vendor)
{
    MdsXCElem* xcEm = (MdsXCElem*)elem;
    MDS_DBG("===>__MdsXCAddAsGuest\n");
    MDS_DBG("vendors=%d, guests=%d\n", MDSElemGetVendorCount(elem), MDSElemGetGuestCount(elem));
    if (XCGetStatus(&xcEm->xc) == XC_ST_INITED) {
        if (XCStart(&xcEm->xc, xcEm->show, CFStringGetStr(&xcEm->status))) {
            MDS_ERR("XCStart() failed\n");
            scheduleStart(xcEm);
            return 0;
        }
    }
    MDS_DBG("%s add as guest\n", MDSElemGetName(elem));
    return 0;
}

static int __MdsXCAddAsVendor(MDSElem* elem, MDSElem* guestElem)
{
    MdsXCElem* xcEm = (MdsXCElem*)elem;
    if (XCGetStatus(&xcEm->xc) == XC_ST_INITED) {
        if (XCStart(&xcEm->xc, xcEm->show, CFStringGetStr(&xcEm->status))) {
            MDS_ERR("XCStart() failed\n");
            scheduleStart(xcEm);
            return 0;
        }
    }
    MDS_DBG("%s add as vendor\n", MDSElemGetName(elem));    
    return 0;
}

static int __MdsXCRemoveAsGuest(MDSElem* elem, MDSElem* vendor)
{
    MDS_DBG("===>__MdsXCRemoveAsGuest\n");
    return 0;
}

static int __MdsXCRemoveAsVendor(MDSElem* elem, MDSElem* guest)
{
    MDS_DBG("===>__MdsXCRemoveAsVendor\n");
    return 0;
}

static int XCElemMsgHook(void* pXc, ikspak *pak)
{
    MdsXCElem *xcEm = pXc;
    iks *msgX;
    const char *body;
    CFJson *jMsg;
    
    MDS_DBG("===>XCElemMsgHook()\n");
    msgX = pak->x;
    body = iks_find_cdata(msgX, "body");
    if (body) {
        MDS_MSG("Got message:\n%s\n", body);
        jMsg = CFJsonParse(body);
        if (jMsg) {
            MDS_DBG("Got JSON message\n");
            MDSElemCastMsg((MDSElem*)xcEm, MDS_MSG_TYPE_JSON, jMsg);
            CFJsonPut(jMsg);
        }
    }
    return IKS_FILTER_EAT;
}

/*
<move>forward | backward | spin_left | spin_right | turn_left | turn_right</move>
            <speed>0-100</speed>
            <radius>100</radius>
            <distance>100</distance>
            <last>1000</last>
 * */
typedef struct AtomCtlIq {
    const char* todo;
    union {
        const char* query;
        const char* move;
    }u_todo;
    const char* result;
}AtomCtlIq;

static int XCElemSendAtomCtlIq(MdsXCElem* xcEm,
        const char* to,
        const char* id,
        const char* type,
        AtomCtlIq* ctlIq)
{
    CFString str;
    
    if (CFStringInit(&str, "<atomctl xmlns='com.ecovacs.atomctl'>")) {
        MDS_ERR_OUT(ERR_OUT, "\n");
    }
    if (ctlIq->todo) {
        CFStringSafeCat(&str, "<todo>");
        CFStringSafeCat(&str, ctlIq->todo);
        CFStringSafeCat(&str, "</todo>");

        if (!strcmp(ctlIq->todo, "query")) {
            if (ctlIq->u_todo.query) {
                CFStringSafeCat(&str, "<query>");
                CFStringSafeCat(&str, ctlIq->u_todo.query);
                CFStringSafeCat(&str, "</query>");
            }
        } else if (!strcmp(ctlIq->todo, "move")) {
            if (ctlIq->u_todo.move) {
                CFStringSafeCat(&str, "<move>");
                CFStringSafeCat(&str, ctlIq->u_todo.move);
                CFStringSafeCat(&str, "</move>");
            }
        }
    }
    if (ctlIq->result) {
        CFStringSafeCat(&str, "<result>");
        CFStringSafeCat(&str, ctlIq->result);
        CFStringSafeCat(&str, "</result>");
    }
    CFStringSafeCat(&str, "</atomctl>");
    if (XCSendIq(&xcEm->xc, to, type, id, CFStringGetStr(&str))) {
        MDS_ERR_OUT(ERR_EXIT_STR, "\n");
    }
    CFStringExit(&str);
    return 0;
    
ERR_EXIT_STR:
    CFStringExit(&str);
ERR_OUT:
    return -1;
}

static int XCElemCastMoveMsg(MdsXCElem* xcEm, 
        const char* action, const char* speed, 
        const char* last, const char* distance, 
        const char* radius)
{
    MdsMvmtCmd mvCmd;
    
    speed ? (mvCmd.speed = strtol(speed, NULL, 10)) : (mvCmd.speed= -1);
    last ? (mvCmd.last = strtol(last, NULL, 10)) : (mvCmd.last=-1);
    distance ? (mvCmd.distance=strtol(distance, NULL, 10)) : (mvCmd.distance = -1);
    radius ? (mvCmd.radius=strtol(radius, NULL, 10)) : (mvCmd.radius = -1);
    if (!strcmp(action, "forward")) {
        mvCmd.type = MDS_MV_CMD_FORWARD;
        if (mvCmd.speed == -1 || (mvCmd.last == -1 && mvCmd.distance == -1)) {
            return -1;
        }
        return MDSElemCastMsg((MDSElem*)xcEm, MDS_MSG_TYPE_MOVEMENT, &mvCmd);
    } else if (!strcmp(action, "backward")) {
        mvCmd.type = MDS_MV_CMD_BACKWARD;
        if (mvCmd.speed == -1 || (mvCmd.last == -1 && mvCmd.distance == -1)) {
            return -1;
        }
        return MDSElemCastMsg((MDSElem*)xcEm, MDS_MSG_TYPE_MOVEMENT, &mvCmd);
    } else if (!strcmp(action, "spin_left")) {
        mvCmd.type = MDS_MV_CMD_SPIN_LEFT;
        if (mvCmd.speed == -1 || (mvCmd.last == -1 && mvCmd.distance == -1)) {
            return -1;
        }
        return MDSElemCastMsg((MDSElem*)xcEm, MDS_MSG_TYPE_MOVEMENT, &mvCmd);
    } else if (!strcmp(action, "spin_right")) {
        mvCmd.type = MDS_MV_CMD_SPIN_RIGHT;
        if (mvCmd.speed == -1 || (mvCmd.last == -1 && mvCmd.distance == -1)) {
            return -1;
        }
        return MDSElemCastMsg((MDSElem*)xcEm, MDS_MSG_TYPE_MOVEMENT, &mvCmd);
    } else if (!strcmp(action, "turn_left")) {
        mvCmd.type = MDS_MV_CMD_TURN_LEFT;
        if (mvCmd.speed == -1 || mvCmd.radius == -1 || (mvCmd.last == -1 && mvCmd.distance == -1)) {
            return -1;
        }
        return MDSElemCastMsg((MDSElem*)xcEm, MDS_MSG_TYPE_MOVEMENT, &mvCmd);
    } else if (!strcmp(action, "turn_right")) {
        mvCmd.type = MDS_MV_CMD_TURN_RIGHT;
        if (mvCmd.speed == -1 || mvCmd.radius == -1 || (mvCmd.last == -1 && mvCmd.distance == -1)) {
            return -1;
        }
        return MDSElemCastMsg((MDSElem*)xcEm, MDS_MSG_TYPE_MOVEMENT, &mvCmd);
    } else if (!strcmp(action, "stop")) {
        mvCmd.type = MDS_MV_CMD_STOP;
        return MDSElemCastMsg((MDSElem*)xcEm, MDS_MSG_TYPE_MOVEMENT, &mvCmd);
    } else if (!strcmp(action, "brake")) {
        mvCmd.type = MDS_MV_CMD_BRAKE;
        return MDSElemCastMsg((MDSElem*)xcEm, MDS_MSG_TYPE_MOVEMENT, &mvCmd);
    } else {
        MDS_ERR("\n");
        return -1;
    }
    return -1;
}

static int XCElemIqGetHook(void* pXc, ikspak *pak)
{
    MdsXCElem *xcEm;
    iks *msgX, *atomctl;
    const char *tmpCStr;
    const char *id, *from;
    AtomCtlIq ctlIq;
    
    MDS_DBG("===>XCElemIqResultHook()\n");
    xcEm = (MdsXCElem*)pXc;
    msgX = pak->x;
    
    MDS_DBG("iq(get)->\n");
    if (!(from = iks_find_attrib(msgX, "from"))
            || !(id = iks_find_attrib(msgX, "id"))) {
        return IKS_FILTER_PASS;
    }
    
    /* atomctl */
    if ((atomctl = iks_find_with_attrib(msgX, "atomctl", "xmlns", "com.ecovacs.atomctl"))) {
        MDS_DBG("atomctl->\n");
        if ((tmpCStr=iks_find_cdata(atomctl, "todo"))) {
            MDS_DBG("todo->%s\n", tmpCStr);
        }
        
        if (!strcmp(tmpCStr, "query")) {    /* query */
            if ((tmpCStr = iks_find_cdata(atomctl, "query"))) {
                MDS_DBG("query->%s\n", tmpCStr)
                /* are_you_an_atom */
                if (!strcmp(tmpCStr, "are_you_an_atom")) {
                    if (xcEm->supportAtomCtl) {
                        tmpCStr = "yes";
                    } else {
                        tmpCStr = "no";
                    }
                    memset(&ctlIq, 0, sizeof(ctlIq));
                    ctlIq.todo = "query";
                    ctlIq.u_todo.query = "are_you_an_atom";
                    ctlIq.result = tmpCStr;
                    XCElemSendAtomCtlIq(xcEm, from, id, "result", &ctlIq);
                    return IKS_FILTER_EAT;
                }
            }
        } else if (!strcmp(tmpCStr, "move")) {  /* move */
            if ((tmpCStr = iks_find_cdata(atomctl, "move"))
                    && (tmpCStr = iks_find_cdata(atomctl, "move"))
                            ) {
                const char *speed, *radius, *distance, *last;
                
                speed = iks_find_cdata(atomctl, "speed");
                last = iks_find_cdata(atomctl, "last");
                distance = iks_find_cdata(atomctl, "distance");
                radius = iks_find_cdata(atomctl, "radius");
                memset(&ctlIq, 0, sizeof(ctlIq));
                ctlIq.todo = "move";
                ctlIq.u_todo.move = tmpCStr;
                if (XCElemCastMoveMsg(xcEm, tmpCStr, speed, last, distance, radius)) {
                    ctlIq.result = "fail";
                } else {
                    ctlIq.result = "ok";
                }
                
                XCElemSendAtomCtlIq(xcEm, from, id, "result", &ctlIq);
                return IKS_FILTER_EAT;
            }
        }
    }
    
    return IKS_FILTER_PASS;
}

static void scheduleStart(MdsXCElem* xcEm)
{
    struct timeval tv;
    
    MDS_DBG("Will retry login XMPP account: %s@%s in 5 seconds\n", XCGetUser(&xcEm->xc), XCGetDomain(&xcEm->xc));
    memset(&tv, 0, sizeof(tv));
    tv.tv_sec = 5;
    CFTimerAdd(&xcEm->xcStartTmr, &tv);
}

static void startTimerHndl(void* data)
{
    MdsXCElem *xcEm;
    
    xcEm = (MdsXCElem*)data;
    MDS_DBG("\n");
    if (XCStart(&xcEm->xc, xcEm->show, CFStringGetStr(&xcEm->status))) {
        MDS_ERR("\n");
        scheduleStart(xcEm);
    }
}

static void xcErrHndl(XC* xc, XCConnErr err)
{
    MDS_DBG("==>xcErrHndl, err=%d\n", err);
    MdsXCElem* xcEm;
    
    xcEm = container_of(xc, MdsXCElem, xc);
    switch (err) {
        case XC_CONN_ERR_AUTH:
            if (XCStop(xc) || XCExit(xc)) {
                MDS_ERR("\n");
            }
        break;
        case XC_CONN_ERR_READ:
            if (XCStop(xc)) {
                MDS_ERR("\n");
            }
            scheduleStart(xcEm);
        break;
        case XC_CONN_ERR_WRITE:
            if (XCStop(xc)) {
                MDS_ERR("\n");
            }
            scheduleStart(xcEm);
        break;
    }
}

/*
{
    "name": "Rd1XmppClient"
    "username": "rd1.tek",
    "domain": "gmail.com",
    "password": "rd1.tekrd1.tek",
    "server": "talk.google.com",
    "atomctl": 1
}
*/
static MDSElem* _XCElemRequested(MDSServer* svr, CFJson* jConf)
{
    MdsXCElem* xcEm;
    const char *name, *user, *password, *domain, *server, *resource;
    const char *presence, *status;
    iksfilter *filter;
    int tmpInt;
    
    MDS_DBG("===>_XCElemRequested()\n");
    if (!svr || !jConf) {
        MDS_ERR_OUT(ERR_OUT, "\n");
    }
    if (!(xcEm = (MdsXCElem*)malloc(sizeof(MdsXCElem)))) {
        MDS_ERR_OUT(ERR_OUT, "malloc for MdsXCElem failed\n");
    }
    MDS_DBG("\n");
    if (CFJsonObjectGetInt(jConf, "atomctl", &tmpInt) || !tmpInt) {
        xcEm->supportAtomCtl = FALSE;
    } else {
        xcEm->supportAtomCtl = TRUE;
    }
    if (CFStringInit(&xcEm->status, "")) {
        MDS_ERR_OUT(ERR_OUT, "Init status string failed\n");
    }
    if (!(name=CFJsonObjectGetString(jConf, "name"))
            ||!(user=CFJsonObjectGetString(jConf, "username"))
            || !(domain=CFJsonObjectGetString(jConf, "domain"))
            || !(password=CFJsonObjectGetString(jConf, "password"))
            || !(server=CFJsonObjectGetString(jConf, "server"))) {
        MDS_ERR_OUT(ERR_EXIT_STATUS, "error parsing config for xmpp client element\n");
    }
    MDS_DBG("\n");
    resource = CFJsonObjectGetString(jConf, "resource");
    MDS_DBG("\n");
    presence=CFJsonObjectGetString(jConf, "presence");
    status=CFJsonObjectGetString(jConf, "status");
    if ((xcEm->show=XCGetPresenceShowIdFromString(presence)) == -1) {
        xcEm->show = XC_PRECENSE_AVAILABLE;
    }
    if (status) {
        CFStringSafeCp(&xcEm->status, status);
    }
    if (XCInit(&xcEm->xc, user, domain, password, server, resource, IKS_JABBER_PORT, svr->fdevents,
            xcErrHndl)) {
        MDS_ERR_OUT(ERR_EXIT_STATUS, "XCInit failed");
    }
    MDS_DBG("\n");
    if (!(filter=XCGetFilter(&xcEm->xc))) {
        MDS_ERR_OUT(ERR_EXIT_XC, "\n");
    }
    MDS_DBG("\n");
    
    /* Message hook */
    iks_filter_add_rule(filter, XCElemMsgHook, xcEm, 
            IKS_RULE_TYPE, IKS_PAK_MESSAGE,
            IKS_RULE_DONE);
    
    /* IQ get hook */
    iks_filter_add_rule(filter, XCElemIqGetHook, xcEm, 
            IKS_RULE_TYPE, IKS_PAK_IQ,
            IKS_RULE_SUBTYPE, IKS_TYPE_GET,
            IKS_RULE_DONE);
    MDS_DBG("startTimerHndl=%x, xcEm=%x\n", (int)startTimerHndl, (int)xcEm);
    
    if (CFTimerInit(&xcEm->xcStartTmr, startTimerHndl, xcEm)) {
        MDS_ERR_OUT(ERR_EXIT_XC, "\n");
    }
    if (MDSElemInit((MDSElem*)xcEm, svr, &_XCClass, name, __MdsXCProcess,
                    __MdsXCAddAsGuest, __MdsXCAddAsVendor,
                    __MdsXCRemoveAsGuest, __MdsXCRemoveAsVendor)) {
        MDS_ERR_OUT(ERR_EXIT_TMR, "MDSElem init failed: for %s\n", name);
    }
    MDS_DBG("\n");
    return (MDSElem*)xcEm;
ERR_EXIT_TMR:
    CFTimerExit(&xcEm->xcStartTmr);
ERR_EXIT_XC:
    XCExit(&xcEm->xc);
ERR_EXIT_STATUS:
    CFStringExit(&xcEm->status);
ERR_FREE_XCM:
    free(xcEm);
ERR_OUT:
    return NULL;
}

static int _XCElemReleased(MDSElem* elem)
{
    int ret = 0;
    MdsXCElem* xcEm = (MdsXCElem*)elem;
    if (XCGetStatus(&xcEm->xc) != XC_ST_INIT
            && XCGetStatus(&xcEm->xc) != XC_ST_INITED) {
        MDS_DBG("Doing XCStop()\n");
        XCStop(&xcEm->xc);
    }
    CFTimerExit(&xcEm->xcStartTmr);
    XCExit(&xcEm->xc);
    free(xcEm);
    return ret;
}

static int _XCPlugInit(MDSPlugin* plg, MDSServer* svr)
{
    MDS_MSG("Initiating plugin: "MDS_XC_PLUGIN_NAME"\n");
    return MDSServerRegistElemClass(svr, &_XCClass);
}

static int _XCPlugExit(MDSPlugin* plg, MDSServer* svr)
{
    MDS_MSG("Exiting plugin: "MDS_XC_PLUGIN_NAME"\n");

    return MDSServerAbolishElemClass(svr, &_XCClass);
}


