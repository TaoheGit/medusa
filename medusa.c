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
 
#include <getopt.h>
#include <signal.h>
#include <assert.h>
#include <cf_std.h>
#include <cf_json.h>
#include <cf_string.h>
#include <cf_common.h>
#include <cf_sigfd.h>
#include <cf_errno.h>
#include "medusa.h"
#include "mds_log.h"

static int MDSServerExitEventLoop(MDSServer* this);
static int MDSSigFdReadable(CFFdevents* events, CFFdevent* event, int fd, void* ctx)
{
    switch(CFSigFdGetNextSignal(fd)){
        case SIGINT:
        case SIGTERM:
            MDSServerExitEventLoop((MDSServer*)ctx);
            break;
        default:
            MDS_ERR("unknown signal, should not be here\n");
            break;
    }
    return 0;
}

int MDSServerInit(MDSServer* this, const char* cfgFile)
{
    CFString* gConfigString;
    const char* tmpCStr;
    int sigFd;

    memset(this, 0, sizeof(MDSServer));
    this->elements = NULL;
    this->elemClasses = NULL;
    this->fdevents = cf_fdevents_new();
    if(!this->fdevents){
        MDS_ERR("\n");
        goto ERR_OUT;
    }
    if(-1==(sigFd = CFSigFdOpen(SIGINT, SIGTERM, -1))){
        MDS_ERR("\n");
        goto ERR_FREE_FDEVENTS;
    }
    if(CFFdeventInit(&this->sigFdEvent, sigFd, MDSSigFdReadable, (void*)this, NULL, NULL)){
        MDS_ERR("\n");
        goto ERR_CLOSE_SIG_FD;
    }
    if(CFFdeventsAdd(this->fdevents, &this->sigFdEvent)){
        MDS_ERR("\n");
        goto ERR_SIG_FDEVENT_EXIT;
    }
    gConfigString = cf_string_new("");
    if(!gConfigString){
        MDS_ERR("\n");
        goto ERR_DEL_SIG_FDEVENT;
    }
    if(cf_file_to_string(gConfigString, cfgFile)){
        MDS_ERR("Read global json config file: %s failed\n", cfgFile);
        goto ERR_FREE_CONF_STRING;
    }
    this->gConf = CFJsonParse(cf_string_get_str(gConfigString));
    if(!this->gConf){
        MDS_ERR("Parse global json config file: %s failed\n", cfgFile);
        goto ERR_FREE_CONF_STRING;
    }
    if(!(tmpCStr = CFJsonObjectGetString(this->gConf, "plugins_dir"))){
        this->plugDirPath = cf_string_new(MDS_DEFAULT_PLUGINS_DIR);
    }else{
        this->plugDirPath = cf_string_new(tmpCStr);
    }
    if(!this->plugDirPath){
        MDS_ERR("\n");
        goto ERR_PUT_GCONF;
    }
    MDS_DBG("Plugin dir: %s\n", CFStringGetStr(this->plugDirPath));
    if(MDSServerLoadPlugins(this)){
        MDS_ERR("Load plugins failed\n");
        goto ERR_FREE_SVR_PLUG_PATH;
    }
    assert(this->elemClasses);
    cf_string_free(gConfigString);
    return 0;
ERR_FREE_SVR_PLUG_PATH:
    cf_string_free(this->plugDirPath);
ERR_PUT_GCONF:
    CFJsonPut(this->gConf);
ERR_FREE_CONF_STRING:
    cf_string_free(gConfigString);
ERR_DEL_SIG_FDEVENT:
    CFFdeventsDel(this->fdevents, &this->sigFdEvent);
ERR_SIG_FDEVENT_EXIT:
    CFFdeventExit(&this->sigFdEvent);
ERR_CLOSE_SIG_FD:
    CFSigFdClose(sigFd);
ERR_FREE_FDEVENTS:
    CFFdeventsFree(this->fdevents);
ERR_OUT:
    return -1;
}

int MDSServerExitEventLoop(MDSServer* this)
{
    CFFdeventsDelAll(this->fdevents);
    return 0;
}

int MDSServerExit(MDSServer* this)
{
        int sigFd;

        MDSServerRmPlugins(this);
    cf_string_free(this->plugDirPath);
    if(this->gConf)
        CFJsonPut(this->gConf);
    sigFd = CFFdeventGetFd(&this->sigFdEvent);
    CFFdeventExit(&this->sigFdEvent);
    CFSigFdClose(sigFd);
    CFFdeventsFree(this->fdevents);
    return 0;
}

void usage()
{
    printf("Usage: %s <-f PLUG_DIR>\n", "medusa");
}

int MDSElemInit(MDSElem* this, MDSServer* server, MdsElemClass* class, const char* name,
                    int(*process)(MDSElem* this, MDSElem* vendor, void* data),
                    int(*addedAsGuest)(MDSElem* this, MDSElem* vendorElem),
                    int(*addedAsVendor)(MDSElem* this, MDSElem* guestElem),
                    int(*removeAsGuest)(MDSElem* this, MDSElem* vendorElem),
                    int(*removeAsVendor)(MDSElem* this, MDSElem* guestElem))
{
        if (!this || !server || !class || !name || !process || !addedAsGuest || !addedAsVendor) {
                MDS_ERR_OUT(ERR_OUT, "\n");
        }
    this->server = server;
    this->class = class;
    CFStringInit(&this->name, name);
    this->vendors = NULL;
    this->guests = NULL;
    this->process = process;
    this->addAsGuest = addedAsGuest;
    this->addAsVendor = addedAsVendor;
    this->removeAsGuest = removeAsGuest;
    this->removeAsVendor = removeAsVendor;
    //this->server->elements = CFGListAppend(this->server->elements, this);
    return 0;
ERR_OUT:
    return -1;
}

void MDSElemDelGuest(MDSElem* elem, MDSElem* guest)
{
        CFGListForeach(elem->guests, node) {
                if ((MDSElem*)node->data == guest) {
                        elem->guests= CFGListDel(elem->guests, node);
                        break;
                }
        }
}

void MDSElemDelVendor(MDSElem* elem, MDSElem* vendor)
{
        CFGListForeach(elem->vendors, node) {
                if ((MDSElem*)node->data == vendor) {
                        elem->vendors= CFGListDel(elem->vendors, node);
                        break;
                }
        }
}

int MDSElemExit(MDSElem* elem)
{
        CFStringExit(&elem->name);
        elem->class = NULL;
        elem->server = NULL;
        return 0;
}

int MDSServerReleaseElem(MDSServer* svr, MDSElem* elem)
{
        int ret;
        MDSElem *guest, *vendor;

        if (svr != elem->server) {
                return -1;
        }
        CFGListForeach(elem->guests, node){
        elem->guests =  CFGListDel(elem->guests, node);
        guest = (MDSElem*)node->data;
        MDSElemDelVendor(guest, elem);
        if (!guest->vendors && !guest->guests && guest != elem) {
                MDSServerReleaseElem(guest->server, guest);
        }
    }
    {
                CFGListForeach(elem->vendors, node){
                    elem->vendors = CFGListDel(elem->vendors, node);
                    vendor = (MDSElem*)node->data;
                    MDSElemDelGuest(vendor, elem);
                    if (!vendor->vendors && !vendor->guests && vendor != elem) {
                        MDSServerReleaseElem(vendor->server, vendor);
                    }
                }
    }
        ret = elem->class->release(elem);
        {
                CFGListForeach(svr->elements, node) {
                        if ((MDSElem*)node->data == elem) {
                                svr->elements = CFGListDel(svr->elements, node);
                                break;
                        }
                }
        }
        return ret;
}

int MDSElemReleaseSelf(MDSElem* elem)
{
    return MDSServerReleaseElem(elem->server, elem);
}

int MDSServerConnectElems(MDSElem* vendor, MDSElem* guestElem)
{
    if (vendor->addAsVendor(vendor, guestElem)) {
        MDS_ERR_OUT(ERR_OUT, "%s add as vendor failed\n", CFStringGetStr(&vendor->name))
    }
    vendor->guests = CFGListAppend(vendor->guests, guestElem);
        if (guestElem->addAsGuest(guestElem, vendor)) {
            MDS_ERR_OUT(ERR_RM_AS_VENDOR, "%s add as guest failed\n", CFStringGetStr(&guestElem->name))
        }
        guestElem->vendors = CFGListAppend(guestElem->vendors, vendor);
        return 0;
ERR_RM_AS_GUEST:
    guestElem->removeAsGuest(guestElem, vendor);
ERR_RM_AS_VENDOR:
    vendor->removeAsVendor(vendor, guestElem);
ERR_OUT:
    return -1;
}

int MDSServerDisConnectElems(MDSElem* elem, MDSElem* guestElem)
{
        int ret = 0;

        CFGListForeach(elem->guests, node){
                if ((MDSElem*)node->data == guestElem) {
                        if (guestElem->removeAsGuest(guestElem, elem)) {
                                ret |= -1;
                                MDS_ERR("remove element: %s as guest failed\n", CFStringGetStr(&guestElem->name));
                        }

                        if (elem->removeAsVendor(elem, guestElem)) {
                            ret |= -1;
                                MDS_ERR("remove element: %s as vendor failed\n", CFStringGetStr(&elem->name));
                        }
                        elem->guests = CFGListDel(elem->guests, node);
                        if (elem->guests == NULL && elem->vendors == NULL) {
                            ret |= MDSElemReleaseSelf(elem);
                        }
                        guestElem->vendors = CFGListDel(guestElem->vendors, node);
                        if (guestElem->vendors == NULL && guestElem->guests == NULL) {
                                ret |= MDSElemReleaseSelf(guestElem);
                        }
                        break;
                }
    }
    return ret;
}

int MDSServerRun(MDSServer* svr)
{
    return cf_fdevents_trigger(svr->fdevents);
}

int MDSServerRegistElemClass(MDSServer* svr, MdsElemClass* cls)
{
        if (!cls || !cls->request || !cls->release) {
                MDS_ERR_OUT(ERR_OUT, "\n");
        }
        MDS_DBG("Registering elemClass: %s\n", cls->name);
        svr->elemClasses = CFGListAppend(svr->elemClasses, cls);
        assert(svr->elemClasses);
        return 0;
ERR_OUT:
        return -1;
}

int MDSServerAbolishElemClass(MDSServer* svr, MdsElemClass* cls)
{
        if (!svr || !cls) {
                return -1;
        }

        CFGListForeach(svr->elemClasses, node) {
                if (node->data == (void*)cls) {
                        MDS_DBG("Abolishing elemClass: %s\n", cls->name);
                        MDS_DBG("elems=%x, next=%x, CNext=%x, node=%x\n",
                                        (unsigned int)&svr->elemClasses->list,
                                        (unsigned int)svr->elemClasses->list.next,
                                        (unsigned int)CFGListGetNext(svr->elemClasses),
                                        (unsigned int)node);
                        svr->elemClasses = CFGListDel(svr->elemClasses, node);
                        MDS_DBG("\n");
                }
        }
        return 0;
}

MDSElem* MDSServerRequestElem(MDSServer* svr, const char* elemClassName, CFJson* jConf)
{
        MdsElemClass *cls;
        MDSElem *ret;

        MDS_DBG("Requesting elemClass: %s\n", elemClassName);
        CFGListForeach(svr->elemClasses, node) {
                cls = (MdsElemClass*)(node->data);
                if (!strcmp(cls->name, elemClassName)) {
                        MDS_DBG("Found elemClass: %s\n", elemClassName);
                        ret = cls->request(svr, jConf);
                        assert(ret);
                        if (ret) {
                                svr->elements = CFGListAppend(svr->elements, ret);
                        }
                        return ret;
                }
        }
        return NULL;
}

MDSElem* MDSServerFindElemByName(MDSServer* svr, const char* elemName)
{
        MDSElem *elem;

        CFGListForeach(svr->elements, elemNode) {
                elem = elemNode->data;
                if (!strcmp(CFStringGetStr(&elem->name), elemName)) {
                        MDS_DBG("Found element: %s\n", elemName);
                        return elem;
                }
        }
        return NULL;
}

int MDSServerChainElemsByName(MDSServer* svr, const char* vendorElemName, const char* guestElemName)
{
        MDSElem *vendorElem, *guestElem;
        int ret;

        if ((vendorElem=MDSServerFindElemByName(svr, vendorElemName))
                        &&(guestElem=MDSServerFindElemByName(svr, guestElemName))) {
                        if (!(ret=MDSServerConnectElems(vendorElem, guestElem))) {
                                MDS_MSG("%s===>%s\n", vendorElemName, guestElemName);
                                return 0;
                        } else {
                                return ret;
                        }
        }
        return -1;
}

int MDSServerReleaseAllElems(MDSServer* svr)
{
        int ret = 0;


        while (svr->elements) {
                MDSElem *elem;
                CFGList *node;
                MDS_DBG("\n");
                if ((node=CFGListGetTail(svr->elements)) && (elem=node->data)) {
                        MDS_MSG("Releasing element: %s\n", CFStringGetStr(&elem->name));
                        ret |= elem->class->release(elem);
/*                      MDS_DBG("elems=%x, next=%x, CNext=%x, node=%x\n", */
/*                                      (unsigned int)&svr->elements->list, */
/*                                      (unsigned int)svr->elements->list.next, */
/*                                      (unsigned int)CFGListGetNext(svr->elements), */
/*                                      (unsigned int)node);*/
                        svr->elements = CFGListDel(svr->elements, node);
                        MDS_DBG("elems=%x\n", (unsigned int)svr->elements);
                }
        }
        return ret;
}

static int MDSGetPidFileAndLockPort(const char* cfgFile, CFString* pidFileStr, int* lockPort)
{
    CFString* gConfigString;
    const char* tmpCStr;
    CFJson *gConf;

    gConfigString = cf_string_new("");
    if(!gConfigString){
        MDS_ERR("\n");
        goto ERR_OUT;
    }
    if(cf_file_to_string(gConfigString, cfgFile)){
        MDS_ERR("Read global json config file: %s failed\n", cfgFile);
        goto ERR_FREE_CONF_STRING;
    }
    gConf = CFJsonParse(cf_string_get_str(gConfigString));
    if(!gConf){
        MDS_ERR("Parse json config file: %s failed\n", cfgFile);
        goto ERR_FREE_CONF_STRING;
    }

    if (!(tmpCStr = CFJsonObjectGetString(gConf, "pid_file"))) {
        MDS_MSG("Use default pid file: "MDS_DEFAULT_PID_FILE"\n");
        CFStringSafeCp(pidFileStr, MDS_DEFAULT_PID_FILE);
    } else {
        CFStringSafeCp(pidFileStr, tmpCStr);
        MDS_MSG("PID file: %s\n", CFStringGetStr(pidFileStr));
    }

    if (CFJsonObjectGetInt(gConf, "lock_port", lockPort)) {
        MDS_MSG("Use default lock port: %d\n", MDS_DEFAULT_LOCK_PORT);
        *lockPort = MDS_DEFAULT_LOCK_PORT;
    } else {
        MDS_MSG("lock port: %d\n", *lockPort);
    }

    CFJsonPut(gConf);
    cf_string_free(gConfigString);
    return 0;

ERR_PUT_GCONF:
    CFJsonPut(gConf);
ERR_FREE_CONF_STRING:
    cf_string_free(gConfigString);
ERR_OUT:
    return -1;
}

int main(int argc, char** argv)
{
    int opt;
    int lockPort;
    CFString pidFileStr;
    const char *cfgFile, *pidFile;
    static MDSServer server;
    CFJson *elems;
    CFString tmpStr, tmpStr2;
        CFJson *chains;

    if (CFStringInit(&tmpStr, "")) {
        MDS_ERR_OUT(ERR_OUT, "\n");
    }
    if (CFStringInit(&tmpStr2, "")) {
        MDS_ERR_OUT(ERR_EXIT_TMP_STR, "\n");
    }
    while((opt = getopt(argc, argv, "f:"))!=-1){
        switch(opt){
            case 'f':
                cfgFile = optarg;
                break;
            default :
                usage();
                goto ERR_EXIT_TMP_STR2;
        }
    }
    if(CFStringInit(&pidFileStr, "")){
        MDS_ERR_OUT(ERR_EXIT_TMP_STR2, "\n");
    }
    if(MDSGetPidFileAndLockPort(cfgFile, &pidFileStr, &lockPort)){
        MDS_ERR_OUT(ERR_EXIT_TMP_STR2, "\n");
    }
    if(!(pidFile=CFStringGetStr(&pidFileStr)) || !*pidFile){
        MDS_ERR_OUT(ERR_EXIT_PID_STR, "\n");
    }

#if 0
    if(CFDaemon(pidFile, lockPort)){
       if(CFErrno == CF_ERR_FORCE_EXTI){
            MDS_MSG("Medusa exit manually.\n");
            CFStringExit(&pidFileStr);
            return 0;
        }else if(CFErrno == CF_ERR_RUNNING){
            MDS_ERR_OUT(ERR_EXIT_PID_STR, "Medusa already running. Exit ...\n");
        }else{
            MDS_ERR_OUT(ERR_EXIT_PID_STR, "Daemon error. Exit ...\n");
        }
    }
#endif

    CFPrintSysInfo(1);
    MDS_DBG("cfgFile: %s\n", cfgFile);
    if(MDSServerInit(&server, cfgFile)){
        MDS_ERR("Init server failed\n");
        goto ERR_EXIT_PID_STR;
    }
    if ((elems=CFJsonObjectGet(server.gConf, "elements"))) {
        MDS_DBG("\n");
        const char* cls;
        CFJsonForeach(elems, elemConf) {
                if ((cls = CFJsonObjectGetString(elemConf, "class"))) {
                        MDS_DBG("Requesting element: %s\n", cls);
                        MDSServerRequestElem(&server, cls, elemConf);
                }
        }
    }
    if ((chains=CFJsonObjectGet(server.gConf, "chains"))) {
        CFJsonArrayForeach(chains, chain) {
                CFJsonArrayForeach(chain, elemNameObj) {
                        CFJson* nextElemNameObj;
                        const char *vName, *gName;
                        if ((vName=CFJsonStringGet(elemNameObj))
                                        && (nextElemNameObj=CFJsonNext(elemNameObj))
                                        && (gName=CFJsonStringGet(nextElemNameObj))) {
                                MDSServerChainElemsByName(&server, vName, gName);
                        }
                }
        }
    }
    MDSServerRun(&server);
    MDS_DBG("\n");
    MDSServerReleaseAllElems(&server);
    MDS_DBG("\n");
    MDSServerExit(&server);
    CFStringExit(&pidFileStr);
    CFStringExit(&tmpStr2);
    CFStringExit(&tmpStr);
    return 0;

ERR_EXIT_PID_STR:
    CFStringExit(&pidFileStr);
ERR_EXIT_TMP_STR2:
        CFStringExit(&tmpStr2);
ERR_EXIT_TMP_STR:
        CFStringExit(&tmpStr);
ERR_OUT:
    return -1;
}

