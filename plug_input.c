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
 * [ INPUT module ]
 * 
 * input type: JSON
 * input data: JSON
 * 
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <linux/input.h>
#include "medusa.h"
#include "mds_log.h"
#include "mds_media.h"
#include "mds_msg_input.h"
#define MDS_INPUT_DEV_PATH_LEN	256
#define MDS_INPUT_ELEM_CLASS_NAME   "INPUT"

typedef struct file_sink_elem{
    MDS_ELEM_MEMBERS;
    char dev[MDS_INPUT_DEV_PATH_LEN+1];
    char action[1024];
   	int fd;
   	CFFdevent rdEvt;
}MdsInputElem;

#define MDS_INPUT_PLUGIN_NAME       "PLUG_INPUT"
typedef struct mds_file_sink_plug{
    MDS_PLUGIN_MEMBERS;
}MDSInputPlug;

static int _InputPlugInit(MDSPlugin* this, MDSServer* svr);
static int _InputPlugExit(MDSPlugin* this, MDSServer* svr);
MDSInputPlug input = {
    .name = MDS_INPUT_PLUGIN_NAME,
    .init = _InputPlugInit,
    .exit = _InputPlugExit
};

static MDSElem* _InputElemRequested(MDSServer* svr, CFJson* jConf);
static int _InputElemReleased(MDSElem* elem);
MdsElemClass _InputClass = {
    .name = MDS_INPUT_ELEM_CLASS_NAME,
    .request = _InputElemRequested,
    .release = _InputElemReleased,
};

static int __MdsInputProcess(MDSElem* this, MDSElem* vendor, MdsMsg* msg);
static int __MdsInputAddAsGuest(MDSElem* this, MDSElem* vendor);
static int __MdsInputAddAsVendor(MDSElem* this, MDSElem* guestElem);
static int __MdsInputRemoveAsGuest(MDSElem* this, MDSElem* vendor);
static int __MdsInputRemoveAsVendor(MDSElem* this, MDSElem* guestElem);

static int _inputReadable(CFFdevents* events, CFFdevent* event, int fd, void* data)
{
	MdsInputElem *giE;
	struct input_event ie;
	char tmpCStr[100];
	
	MDS_DBG("\n");
	assert(data);
	giE = (MdsInputElem*)data;
	if (sizeof(ie) != read(fd, &ie, sizeof(ie))) {
		MDS_ERR("read input device:%s failed\n", giE->dev);
		MDSElemDisconnectAllGuests((MDSElem*)giE);
		return 0;
	}
	MDSElemCastMsg((MDSElem*)giE, MDS_MSG_TYPE_INPUT, &ie);
	if (giE->action[0]) {
		sprintf(tmpCStr, "%lu", (unsigned long)ie.type);
		setenv("INPUT_EVENT_TYPE", tmpCStr, 1);
		
		sprintf(tmpCStr, "%lu", (unsigned long)ie.code);
		setenv("INPUT_EVENT_CODE", tmpCStr, 1);
		
		sprintf(tmpCStr, "%lu", (unsigned long)ie.value);
		setenv("INPUT_EVENT_VALUE", tmpCStr, 1);
		system(giE->action);
	}
	return 0;
}

static int MdsInputElemInit(MdsInputElem* giE, MDSServer* svr, const char* name,
			const char* dev, const char* action)
{
	if (!giE || !svr || !name || !dev) {
		MDS_ERR_OUT(ERR_OUT, "\n");
	}
	strncpy(giE->dev, dev, sizeof(giE->dev));
	giE->dev[sizeof(giE->dev)-1]='\0';
	
	if (action) {
		strncpy(giE->action, action, sizeof(giE->action));
		giE->action[sizeof(giE->action)-1]='\0';
	} else {
		giE->action[0] = '\0';
	}
	
	giE->fd = open(dev, O_RDONLY|O_NONBLOCK);
	if (giE->fd < 0) {
		MDS_ERR_OUT(ERR_OUT, "\n");
	}
	if (CFFdeventInit(&giE->rdEvt, giE->fd, 
			_inputReadable, giE, 
			NULL, NULL, 
			NULL, NULL)) {
		MDS_ERR_OUT(ERR_CLOSE_FD, "\n");
	}
	if (CFFdeventsAdd(svr->fdevents, &giE->rdEvt)) {
		MDS_ERR_OUT(ERR_EXIT_EVT, "\n");
	}
	if (MDSElemInit((MDSElem*)giE, svr, &_InputClass, name, __MdsInputProcess,
                    __MdsInputAddAsGuest, __MdsInputAddAsVendor,
                    __MdsInputRemoveAsGuest, __MdsInputRemoveAsVendor)) {
        MDS_ERR_OUT(ERR_RM_FDEVT, "MDSElem init failed: for %s\n", name);
    }
    return 0;
ERR_RM_FDEVT:
	CFFdeventsDel(svr->fdevents, &giE->rdEvt);
ERR_EXIT_EVT:
	CFFdeventExit(&giE->rdEvt);
ERR_CLOSE_FD:
	close(giE->fd);
ERR_OUT:
	return -1;
}

int MdsInputElemExit(MdsInputElem* giE)
{
	CFFdeventsDel(giE->server->fdevents, &giE->rdEvt);
	CFFdeventExit(&giE->rdEvt);
	close(giE->fd);
	MDSElemExit((MDSElem*)giE);
	return 0;
}

/*
 * {
 * "state":[0, 500, 500, 1, -1]
 * }
 */
static int __MdsInputProcess(MDSElem* this, MDSElem* vendor, MdsMsg* msg)
{
	return 0;
}

static int __MdsInputAddAsGuest(MDSElem* this, MDSElem* vendor)
{
    return 0;
}

static int __MdsInputAddAsVendor(MDSElem* this, MDSElem* guestElem)
{
    return 0;
}

static int __MdsInputRemoveAsGuest(MDSElem* this, MDSElem* vendor)
{

    return 0;
}

static int __MdsInputRemoveAsVendor(MDSElem* this, MDSElem* guestElem)
{
	return 0;
}

/*
    {
        "class": "INPUT",
        "name": "WPS_BTN",
        "dev": "/dev/input/event0",
        "action": "echo input:65"
    }
*/
static MDSElem* _InputElemRequested(MDSServer* svr, CFJson* jConf)
{
    MdsInputElem* giE;
    const char *dev, *action;
    const char *name;
    
    if (!svr || !jConf) {
        MDS_ERR_OUT(ERR_OUT, "\n");
    }
    if (!(giE = (MdsInputElem*)malloc(sizeof(MdsInputElem)))) {
        MDS_ERR_OUT(ERR_OUT, "malloc for MdsInputElem failed\n");
    }

    if (!(dev=CFJsonObjectGetString(jConf, "dev")) || !*dev) {
		MDS_ERR_OUT(ERR_FREE_FEM, "set mode in INPUT class config\n");
	}

	action = CFJsonObjectGetString(jConf, "action");

	if (!(name = CFJsonObjectGetString(jConf, "name"))) {
		MDS_ERR_OUT(ERR_FREE_FEM, "set name in INPUT class config\n");
	}
	if (MdsInputElemInit(giE, svr, name, dev, action)) {
		MDS_ERR_OUT(ERR_FREE_FEM, "MDSElem init failed: for %s\n", name);
	}

    return (MDSElem*)giE;

ERR_FREE_FEM:
    free(giE);
ERR_OUT:
    return NULL;
}

static int _InputElemReleased(MDSElem* elem)
{
    int ret = 0;
    
    MdsInputElem *giE = (MdsInputElem*)elem;
    
	ret |= MdsInputElemExit(giE);
    free(giE);
    return ret;
}

static int _InputPlugInit(MDSPlugin* plg, MDSServer* svr)
{
    MDS_MSG("Initiating plugin: "MDS_INPUT_PLUGIN_NAME"\n");
    return MDSServerRegistElemClass(svr, &_InputClass);
}

static int _InputPlugExit(MDSPlugin* plg, MDSServer* svr)
{
    MDS_MSG("Exiting plugin: "MDS_INPUT_PLUGIN_NAME"\n");

    return MDSServerAbolishElemClass(svr, &_InputClass);
}

