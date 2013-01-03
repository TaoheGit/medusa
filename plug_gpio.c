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
 * [ GPIO module ]
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
#include <cf_string.h>
#include <cf_common.h>
#include <cf_pipe.h>
#include <cf_buffer.h>
#include <cf_timer.h>
#include "medusa.h"
#include "mds_log.h"
#include "mds_media.h"

#define MDS_GPIO_ELEM_CLASS_NAME   "GPIO"
#define MDS_MSG_TYPE_JSON  "JSON"

typedef struct file_sink_elem{
    MDS_ELEM_MEMBERS;
    int port;
    int fd;
    enum {GPIO_MODE_INPUT, GPIO_MODE_OUTPUT} mode;
    union {
		struct {
			enum {GPIO_TRIG_RISING, GPIO_TRIG_FALLING, GPIO_TRIG_BOTH} trigger;
			char action[1024];
			CFFdevent valueEvt;
		} _input_;
		struct {
			BOOL phase1State;
			int phase1Period;
			int phase2Period;
			int count;
			BOOL stoppedState;
			CFTimer phase1Timer;
			CFTimer phase2Timer;
		} _output_;
	}_u_;
}MdsGpioElem;
#define u_input	_u_._input_
#define u_output	_u_._output_
#define MDS_GPIO_PLUGIN_NAME       "PLUG_GPIO"
typedef struct mds_file_sink_plug{
    MDS_PLUGIN_MEMBERS;
}MDSGpioPlug;

static int _GpioPlugInit(MDSPlugin* this, MDSServer* svr);
static int _GpioPlugExit(MDSPlugin* this, MDSServer* svr);
MDSGpioPlug gpio = {
    .name = MDS_GPIO_PLUGIN_NAME,
    .init = _GpioPlugInit,
    .exit = _GpioPlugExit
};

static MDSElem* _GpioElemRequested(MDSServer* svr, CFJson* jConf);
static int _GpioElemReleased(MDSElem* elem);
MdsElemClass _GpioClass = {
    .name = MDS_GPIO_ELEM_CLASS_NAME,
    .request = _GpioElemRequested,
    .release = _GpioElemReleased,
};

static int MdsGpioOutputElemStop(MdsGpioElem* giE);
static int __MdsGpioProcess(MDSElem* this, MDSElem* vendor, MdsMsg* msg);
static int __MdsGpioAddAsGuest(MDSElem* this, MDSElem* vendor);
static int __MdsGpioAddAsVendor(MDSElem* this, MDSElem* guestElem);
static int __MdsGpioRemoveAsGuest(MDSElem* this, MDSElem* vendor);
static int __MdsGpioRemoveAsVendor(MDSElem* this, MDSElem* guestElem);
static void phase2OverHndl(CFTimer* tmr, void* data);
void phase1OverHndl(CFTimer* tmr, void* data)
{
	MdsGpioElem *giE;
	const char *vc;
	struct itimerspec ts;
	
	//MDS_DBG("\n");
	assert(data);
	giE = (MdsGpioElem*)data;
	if (giE->u_output.phase1State) {
		vc = "0\n";
	} else {
		vc = "1\n";
	}
	write(giE->fd, vc, strlen(vc));
	memset(&ts, 0, sizeof(ts));
    ts.it_value.tv_sec = (giE->u_output.phase2Period/1000);
    ts.it_value.tv_nsec = (giE->u_output.phase2Period%1000)*1000000;
    ts.it_interval.tv_sec = 0;
    ts.it_interval.tv_nsec = 0;
    CFTimerMod(&giE->u_output.phase2Timer, 
            phase2OverHndl, &ts, NULL);
}

void phase2OverHndl(CFTimer* tmr, void* data)
{
	MdsGpioElem *giE;
	const char *vc;
	struct timeval tv;
	
	//MDS_DBG("\n");
	assert(data);
	giE = (MdsGpioElem*)data;
	if (giE->u_output.count > 0) {
		giE->u_output.count --;

	} else if (giE->u_output.count == 0) {
		MdsGpioOutputElemStop(giE);
		return;
	}
	if (!giE->u_output.phase1State) {
		vc = "0\n";
	} else {
		vc = "1\n";
	}
	write(giE->fd, vc, strlen(vc));
	memset(&tv, 0, sizeof(tv));
	tv.tv_sec = giE->u_output.phase1Period/1000;
	tv.tv_usec = (giE->u_output.phase1Period%1000)*1000;
	CFTimerModTime(&giE->u_output.phase1Timer, 
            giE->u_output.phase1Period/1000, 
            (giE->u_output.phase1Period%1000)*1000000,
            0, 0);
}

#define MDS_GIO_VALUE_FILE_PATTERN	"/sys/class/gpio/gpio%d/value"
#define MDS_GIO_EDGE_FILE_PATTERN	"/sys/class/gpio/gpio%d/edge"
#define MDS_GIO_DIR_FILE_PATTERN	"/sys/class/gpio/gpio%d/direction"
static int MdsGpioOutputElemInit(MdsGpioElem* giE, MDSServer* svr, const char* name,
			int port,
			BOOL phase1State,
			int phase1Period,
			int phase2Period,
			int count,
			BOOL stoppedState)
{
	char tmpFile[sizeof(MDS_GIO_VALUE_FILE_PATTERN)+10];
	giE->u_output.phase1State = phase1State;
	giE->u_output.phase1Period = phase1Period;
	giE->u_output.phase2Period = phase2Period;
	giE->u_output.count = count;
	giE->u_output.stoppedState = stoppedState;
	giE->port = port;
	giE->mode = GPIO_MODE_OUTPUT;
	
	
	sprintf(tmpFile, MDS_GIO_DIR_FILE_PATTERN, giE->port);
	if (CFStringToFile(tmpFile, "out")) {
		MDS_ERR_OUT(ERR_OUT, "\n");
	}
	
	sprintf(tmpFile, MDS_GIO_VALUE_FILE_PATTERN, giE->port);
	giE->fd = open(tmpFile, O_RDWR);
	if (giE->fd<0) {
		MDS_ERR_OUT(ERR_OUT, "\n");
	}
	
	if (CFTimerInitStopped(&giE->u_output.phase1Timer, "gpio phase 1 over timer", 
            phase1OverHndl, giE, NULL)) {
		MDS_ERR_OUT(ERR_CLOSE_FD, "\n");
	}
	
	if (CFTimerInitStopped(&giE->u_output.phase2Timer, "gpio phase 2 over timer", 
            phase2OverHndl, giE, NULL)) {
		MDS_ERR_OUT(ERR_EXIT_TMR1, "\n");
	}
	phase2OverHndl(&giE->u_output.phase2Timer, giE);
	if (MDSElemInit((MDSElem*)giE, svr, &_GpioClass, name, __MdsGpioProcess,
                    __MdsGpioAddAsGuest, __MdsGpioAddAsVendor,
                    __MdsGpioRemoveAsGuest, __MdsGpioRemoveAsVendor)) {
        MDS_ERR_OUT(ERR_EXIT_TIMER2, "MDSElem init failed: for %s\n", name);
    }
	return 0;
ERR_EXIT_TIMER2:
	CFTimerExit(&giE->u_output.phase2Timer);
ERR_EXIT_TMR1:
	CFTimerExit(&giE->u_output.phase1Timer);
ERR_CLOSE_FD:
	close(giE->fd);
ERR_OUT:
	return -1;
}

static int MdsGpioOutputElemExit(MdsGpioElem* giE)
{
	MDSElemExit((MDSElem*)giE);
	CFTimerExit(&giE->u_output.phase2Timer);
	CFTimerExit(&giE->u_output.phase1Timer);
	close(giE->fd);
	return 0;
}


static int MdsGpioOutputElemStart(MdsGpioElem* giE)
{
	phase2OverHndl(&giE->u_output.phase2Timer, giE);
	return 0;
}

static int MdsGpioOutputElemStop(MdsGpioElem* giE)
{
	const char* vc;
	
	if (!giE->u_output.stoppedState) {
		vc = "0\n";
	} else {
		vc = "1\n";
	}
	CFTimerCancel(&giE->u_output.phase2Timer);
	CFTimerCancel(&giE->u_output.phase1Timer);
	write(giE->fd, vc, strlen(vc));
	return 0;
}

static int MdsGpioOutputElemReset(MdsGpioElem* giE,
			BOOL phase1State,
			int phase1Period,
			int phase2Period,
			int count,
			BOOL stoppedState)
{
	if (MdsGpioOutputElemStop(giE)) {
		return -1;
	}
	giE->u_output.phase1State = phase1State;
	giE->u_output.phase1Period = phase1Period;
	giE->u_output.phase2Period = phase2Period;
	giE->u_output.count = count;
	giE->u_output.stoppedState = stoppedState;
	
	return MdsGpioOutputElemStart(giE);
}

static int MdsGpioOutputElemParseStateConfig(CFJson* jConf, 
			BOOL *phase1State,
			int *phase1Period,
			int *phase2Period,
			int *count,
			BOOL *stoppedState)
{
	int i, tmpInt;
	
	i = 0;
	CFJsonArrayForeach(jConf, tmpJConf) {
		if (!CFJsonIntGet(tmpJConf, &tmpInt)) {
			switch (i) {
				case 0:
					if (tmpInt)
						*phase1State = TRUE;
					else
						*phase1State = FALSE;
					break;
				case 1:
					*phase1Period = tmpInt;
					break;
				case 2:
					*phase2Period = tmpInt;
					break;
				case 3:
					if (tmpInt)
						*stoppedState = TRUE;
					else
						*stoppedState = FALSE;
					break;
				case 4:
					*count = tmpInt;
					break;
			}
			i++;
			if (i == 5) 
				break;
		} else {
			break;
		}
	}
	if (i == 5) {
		return 0;
	}
	return -1;
}

static int _gioValueReadable(CFFdevents* events, CFFdevent* event, int fd, void* data)
{
	MdsGpioElem *giE;
	char tmpStr[10];
	int readed;
	const char *trig;
	CFJson *jMsg;
	int val;
	
	MDS_DBG("\n");
	assert(data);
	giE = (MdsGpioElem*)data;
	lseek(giE->fd, 0, SEEK_SET);
	readed = read(giE->fd, tmpStr, sizeof(tmpStr));
	if ((readed < 0 && errno != EAGAIN) || readed == 0) {
		MDS_DBG("\n");
		MDSElemDisconnectAllGuests((MDSElem*)giE);
		return 0;
	} else if (readed > 0) {
		if (*tmpStr == '1') {
			val = 1;
			setenv("MDS_GPIO_VALUE", "1", 1);
		} else if (*tmpStr == '0') {
			setenv("MDS_GPIO_VALUE", "0", 1);
			val = 0;
		} else {
			MDSElemDisconnectAllGuests((MDSElem*)giE);
			return 0;
		}
	} else {
		MDSElemDisconnectAllGuests((MDSElem*)giE);
		return 0;
	}
	if (giE->u_input.trigger == GPIO_TRIG_FALLING) {
		trig = "falling";
	} else if (giE->u_input.trigger == GPIO_TRIG_RISING) {
		trig = "rising";
	} else if (giE->u_input.trigger == GPIO_TRIG_BOTH) {
		trig = "both";
	} else {
		MDSElemDisconnectAllGuests((MDSElem*)giE);
		return 0;
	}
	setenv("MDS_GPIO_TRIGGER", trig, 1);
	jMsg = CFJsonObjectNew();
	if (!jMsg) {
		MDSElemDisconnectAllGuests((MDSElem*)giE);
		return 0;
	} 
	if(CFJsonObjectAddInt(jMsg, "port", giE->port)
			||CFJsonObjectAddString(jMsg, "trigger", trig)
			|| CFJsonObjectAddInt(jMsg, "value", val)) {
		CFJsonPut(jMsg);
		MDSElemDisconnectAllGuests((MDSElem*)giE);
		return 0;
	}
	MDSElemCastMsg((MDSElem*)giE, MDS_MSG_TYPE_JSON, jMsg);
	CFJsonPut(jMsg);
	if (giE->u_input.action[0])
		system(giE->u_input.action);
	return 0;
}

static int MdsGpioInputElemInit(MdsGpioElem* giE, MDSServer* svr, const char* name,
			int port, int trigger, const char* action)
{
	char tmpFile[sizeof(MDS_GIO_DIR_FILE_PATTERN)+50];
	
	giE->u_input.trigger = trigger;
	if (action) {
		strncpy(giE->u_input.action, action, sizeof(giE->u_input.action));
		giE->u_input.action[sizeof(giE->u_input.action)-1] = '\0';
	} else {
		giE->u_input.action[0] = '\0';
	}
	
	giE->port = port;
	giE->mode = GPIO_MODE_INPUT;
	
	sprintf(tmpFile, MDS_GIO_DIR_FILE_PATTERN, giE->port);
	if (CFStringToFile(tmpFile, "in")) {
		MDS_ERR_OUT(ERR_OUT, "\n");
	}
	sprintf(tmpFile, MDS_GIO_EDGE_FILE_PATTERN, giE->port);
	if (trigger == GPIO_TRIG_RISING) {
		CFStringToFile(tmpFile, "rising");
	} else if (trigger == GPIO_TRIG_FALLING) {
		CFStringToFile(tmpFile, "falling");
	} else if (trigger == GPIO_TRIG_BOTH) {
		CFStringToFile(tmpFile, "both");
	} else {
		MDS_ERR_OUT(ERR_OUT, "\n");
	}
	sprintf(tmpFile, MDS_GIO_VALUE_FILE_PATTERN, giE->port);
	giE->fd = open(tmpFile, O_RDWR|O_NONBLOCK);
	if (giE->fd < 0) {
		MDS_ERR_OUT(ERR_OUT, "\n");
	}
	if (CFFdeventInit(&giE->u_input.valueEvt, giE->fd, "GpioInTrigEvt", 
			NULL, NULL, 
			NULL, NULL, 
			_gioValueReadable, giE)) {
		MDS_ERR_OUT(ERR_CLOSE_FD, "\n");
	}
	if (CFFdeventsAdd(svr->fdevents, &giE->u_input.valueEvt)) {
		MDS_ERR_OUT(ERR_EXIT_EVT, "\n");
	}
	if (MDSElemInit((MDSElem*)giE, svr, &_GpioClass, name, __MdsGpioProcess,
                    __MdsGpioAddAsGuest, __MdsGpioAddAsVendor,
                    __MdsGpioRemoveAsGuest, __MdsGpioRemoveAsVendor)) {
        MDS_ERR_OUT(ERR_RM_FDEVT, "MDSElem init failed: for %s\n", name);
    }
    return 0;
ERR_RM_FDEVT:
	CFFdeventsDel(svr->fdevents, &giE->u_input.valueEvt);
ERR_EXIT_EVT:
	CFFdeventExit(&giE->u_input.valueEvt);
ERR_CLOSE_FD:
	close(giE->fd);
ERR_OUT:
	return -1;
}

int MdsGpioInputElemExit(MdsGpioElem* giE)
{
	CFFdeventsDel(giE->server->fdevents, &giE->u_input.valueEvt);
	CFFdeventExit(&giE->u_input.valueEvt);
	close(giE->fd);
	MDSElemExit((MDSElem*)giE);
	return 0;
}

/*
 * {
 * "state":[0, 500, 500, 1, -1]
 * }
 */
static int __MdsGpioProcess(MDSElem* this, MDSElem* vendor, MdsMsg* msg)
{
    CFJson* jConf;
    MdsGpioElem* giE = (MdsGpioElem*)this;
    int phase1Period = 1000, phase2Period = 1000, count = -1;
    BOOL phase1State = FALSE, stoppedState = TRUE;
    
    if (giE->mode != GPIO_MODE_OUTPUT) {
		return -1;
	}
    if (!strcmp(msg->type, MDS_MSG_TYPE_JSON)) {  /* Json */
        jConf = (CFJson*)msg->data; 
        
        if (!(jConf = CFJsonObjectGet(jConf, "state"))) {
			return -1;
		}
		if (MdsGpioOutputElemParseStateConfig(jConf, 
				&phase1State, &phase1Period, &phase2Period, 
				&count, &stoppedState)) {
			return -1;
		}
		return MdsGpioOutputElemReset(giE,  phase1State, phase1Period, phase2Period, count, stoppedState);;
    } else {
        MDS_ERR("<%s> Msg type: %s not support!\n", CFStringGetStr(&giE->name), msg->type);
        return -1;
    }
}

static int __MdsGpioAddAsGuest(MDSElem* this, MDSElem* vendor)
{
    MdsGpioElem* giE = (MdsGpioElem*)this;
	
	if (giE->mode == GPIO_MODE_INPUT) {
		MDS_ERR("Input GPIO element can not be a guest\n");
		return -1;
	} else if (giE->mode == GPIO_MODE_OUTPUT) {
		return 0;
	} else {
		return -1;
	}
}

static int __MdsGpioAddAsVendor(MDSElem* this, MDSElem* guestElem)
{
    MdsGpioElem* giE = (MdsGpioElem*)this;
	
	if (giE->mode == GPIO_MODE_INPUT) {
		return 0;
	} else if (giE->mode == GPIO_MODE_OUTPUT) {
		MDS_ERR("Output GPIO element can not be a vendor\n");
		return -1;
	} else {
		return -1;
	}
}

static int __MdsGpioRemoveAsGuest(MDSElem* this, MDSElem* vendor)
{
    MdsGpioElem* giE = (MdsGpioElem*)this;
	
	if (giE->mode == GPIO_MODE_INPUT) {
		MDS_ERR("Input GPIO element can not be a guest\n");
		return -1;
	} else if (giE->mode == GPIO_MODE_OUTPUT) {
		return 0;
	} else {
		return -1;
	}
    return 0;
}

static int __MdsGpioRemoveAsVendor(MDSElem* this, MDSElem* guestElem)
{
    MdsGpioElem* giE = (MdsGpioElem*)this;
	
	if (giE->mode == GPIO_MODE_INPUT) {
		return 0;
	} else if (giE->mode == GPIO_MODE_OUTPUT) {
		MDS_ERR("Output GPIO element can not be a vendor\n");
		return -1;
	} else {
		return -1;
	}
}

/*
    {
        "class": "GPIO",
        "name": "WPS_BTN",
        "port": 1,
        "mode": "input",
        "trigger": "rising",
        "action": "echo rising"
    },
    {
        "class": "GPIO",
        "name": "wifi_led",
        "port": 2,
        "mode": "output",
        "state": [0,500,500,1, -1]
    }
*/
static MDSElem* _GpioElemRequested(MDSServer* svr, CFJson* jConf)
{
    MdsGpioElem* giE;
    const char* tmpCStr;
	int port;
	int phase1Period = 1000, phase2Period = 1000, count = -1;
    BOOL phase1State = TRUE, stoppedState = FALSE;
    CFJson *tmpJConf;
    const char *name;
    
    if (!svr || !jConf) {
        MDS_ERR_OUT(ERR_OUT, "\n");
    }
    if (!(giE = (MdsGpioElem*)malloc(sizeof(MdsGpioElem)))) {
        MDS_ERR_OUT(ERR_OUT, "malloc for MdsGpioElem failed\n");
    }
    if (CFJsonObjectGetInt(jConf, "port", &port)) {
		MDS_ERR_OUT(ERR_FREE_FEM, "set port in GPIO class config\n");
	}
    if (!(tmpCStr=CFJsonObjectGetString(jConf, "mode"))) {
		MDS_ERR_OUT(ERR_FREE_FEM, "set mode in GPIO class config\n");
	}
	if (!(name = CFJsonObjectGetString(jConf, "name"))) {
		MDS_ERR_OUT(ERR_FREE_FEM, "set name in GPIO class config\n");
	}
	if (!strcmp(tmpCStr, "input")) {
		int trig;
		
		tmpCStr = CFJsonObjectGetString(jConf, "trigger");
		if (!tmpCStr) {
			MDS_ERR_OUT(ERR_FREE_FEM, "set right 'trigger' in GPIO class config\n");
		}
		if (*tmpCStr == 'r') {
			trig = GPIO_TRIG_RISING;
		} else if (*tmpCStr == 'f') {
			trig = GPIO_TRIG_FALLING;
		} else if (*tmpCStr == 'b') {
			trig = GPIO_TRIG_BOTH;
		} else {
			MDS_ERR_OUT(ERR_FREE_FEM, "GPIO->trigger : rising|falling|both\n");
		}
		tmpCStr = CFJsonObjectGetString(jConf, "action");
		if (MdsGpioInputElemInit(giE, svr, name, port, trig, tmpCStr)) {
			MDS_ERR_OUT(ERR_FREE_FEM, "MDSElem init failed: for %s\n", name);
		}
	} else if (!strcmp(tmpCStr, "output")) {
		if (!(tmpJConf = CFJsonObjectGet(jConf, "state"))) {
			MDS_ERR_OUT(ERR_FREE_FEM, "set state in GPIO class config\n");
		}
		if (MdsGpioOutputElemParseStateConfig(tmpJConf, 
					&phase1State, &phase1Period, &phase2Period, 
					&count, &stoppedState)) {
			MDS_ERR_OUT(ERR_FREE_FEM, "wrong state set in GPIO config\n");
		}
		if (MdsGpioOutputElemInit(giE, svr, name, port, 
				phase1State, phase1Period, phase2Period, 
				count, stoppedState)) {
			MDS_ERR_OUT(ERR_FREE_FEM, "MDSElem init failed: for %s\n", name);
		}
	} else {
		MDS_ERR_OUT(ERR_FREE_FEM, "GPIO->mode only accepts 'input' or 'output'\n");
	}
    return (MDSElem*)giE;

ERR_FREE_FEM:
    free(giE);
ERR_OUT:
    return NULL;
}

static int _GpioElemReleased(MDSElem* elem)
{
    int ret = 0;
    
    MdsGpioElem *giE = (MdsGpioElem*)elem;
    
    if (giE->mode == GPIO_MODE_OUTPUT)
		ret |= MdsGpioOutputElemExit(giE);
	else {
		ret |= MdsGpioInputElemExit(giE);
	}
    free(giE);
    return ret;
}

static int _GpioPlugInit(MDSPlugin* plg, MDSServer* svr)
{
    MDS_MSG("Initiating plugin: "MDS_GPIO_PLUGIN_NAME"\n");
    return MDSServerRegistElemClass(svr, &_GpioClass);
}

static int _GpioPlugExit(MDSPlugin* plg, MDSServer* svr)
{
    MDS_MSG("Exiting plugin: "MDS_GPIO_PLUGIN_NAME"\n");

    return MDSServerAbolishElemClass(svr, &_GpioClass);
}

