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

#ifndef _MEDUSA_H_
#define _MEDUSA_H_
#include <dlfcn.h>
#include <cf_list.h>
#include <cf_string.h>
#include <cf_fdevent.h>
#include <cf_json.h>

#define MDS_DEFAULT_PLUGINS_DIR "/usr/lib/medusa"
#define MDS_DEFAULT_PID_FILE    "/var/run/medusa.pid"
#define MDS_DEFAULT_LOCK_PORT   (2000)
typedef struct mds_server MDSServer;
typedef struct mds_plugin MDSPlugin;
typedef struct mds_elem MDSElem;

#define MDS_PLUGIN_MEMBERS    \
    void* dlHandl;  \
    const char* name;   \
    int(*init)(MDSPlugin* this, MDSServer* server); \
    int(*exit)(MDSPlugin* this, MDSServer* server); \
    CFListHead list
struct mds_plugin{
    MDS_PLUGIN_MEMBERS;
};

typedef struct mds_elem_class {
        const char* name;
        MDSElem*(*request)(MDSServer* svr, CFJson* jConfStr);
        int(*release)(MDSElem* elem);
}MdsElemClass;

struct mds_server{
    CFJson* gConf;
    CFString* plugDirPath;
    MDSPlugin pluginHead;
    CFFdevents* fdevents;
    CFFdevent  sigFdEvent;
    CFGList* elements;
    CFGList* elemClasses;
};

int MDSServerInit(MDSServer* this, const char* cfgFilePath);
int MDSServerResetToIdle(MDSServer* this);
int MDSServerExit(struct mds_server* this);
int MDSServerLoadPlugins(MDSServer* svr);
int MDSServerRmPlugins(MDSServer* svr);
int MDSServerRun(MDSServer* svr);
MDSElem* MDSServerReuestElem(struct mds_server* this, const char* name, const char* jConfStr);
int MDSServerReleaseElem(struct mds_server* svr, MDSElem* elem);
//int MDSServerChainElems(struct mds_server* svr, MDSElem* elem, ...);
int MDSServerRegistElemClass(MDSServer* svr, MdsElemClass* cls);
int MDSServerAbolishElemClass(MDSServer* svr, MdsElemClass* cls);
MDSElem* MDSServerRequestElem(MDSServer* svr, const char* elemClassName, CFJson* jConf);

#define MDS_ELEM_MEMBERS  \
    MDSServer* server;   \
    MdsElemClass* class;  \
    CFString name;    \
    CFGList* guests;    \
    CFGList* vendors;   \
    int(*process)(MDSElem* this, MDSElem* vendor, void* data);  \
    int(*addAsGuest)(MDSElem* this, MDSElem* vendorElem);       \
    int(*addAsVendor)(MDSElem* this, MDSElem* guestElem); \
    int(*removeAsGuest)(MDSElem* this, MDSElem* vendorElem);    \
    int(*removeAsVendor)(MDSElem* this, MDSElem* guestElem); \
    CFListHead list
struct mds_elem {
    MDS_ELEM_MEMBERS;
};
int MDSElemInit(MDSElem* this, MDSServer* server, MdsElemClass* class, const char* name,
                    int(*process)(MDSElem* this, MDSElem* vendor, void* data),
                    int(*addedAsGuest)(MDSElem* this, MDSElem* vendorElem),
                    int(*addedAsVendor)(MDSElem* this, MDSElem* guestElem),
                    int(*removeAsGuest)(MDSElem* this, MDSElem* vendorElem),
                    int(*removeAsVendor)(MDSElem* this, MDSElem* guestElem));
int MDSElemExit(MDSElem* elem);
#endif

