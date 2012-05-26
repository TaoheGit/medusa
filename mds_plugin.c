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
#include <dlfcn.h>  /* -ldl */
#include <dirent.h>
#include <errno.h>
#include <cf_std.h>
#include "mds_log.h"
#include <cf_string.h>
#include <cf_common.h>
#include "medusa.h"

static int load_plugin(MDSServer* svr, const char* plugPath, const char* plugName)
{
    void* dlHndl;
    MDSPlugin* plugin;

    dlHndl = dlopen(plugPath, RTLD_LAZY);

    if(!dlHndl){
        MDS_ERR("Open plugin: %s failed, %s\n", plugName, dlerror());
        goto ERR_OUT;
    }
    plugin = dlsym(dlHndl, MDS_PLUG_INIT_SYMBOLY_STR);
    if(dlerror()){
        MDS_DBG("Can not find plugin symbol: "MDS_PLUG_INIT_SYMBOLY_STR" in %s\n", plugPath);
		plugin = dlsym(dlHndl, plugName);
		if(dlerror()){
		    MDS_ERR("Can not find plugin symbol in %s\n", plugPath);
		    goto ERR_DLCLOSE;
		}
    }
    plugin->dlHandl = dlHndl;
    cf_list_init(&plugin->list);
    if(plugin->init(plugin, svr)){
        MDS_ERR("Init plugin: %s failed\n", plugName);
        goto ERR_DLCLOSE;
    }
    cf_list_insert_pre(&svr->pluginHead.list, &plugin->list);
    return 0;
ERR_DLCLOSE:
    dlclose(dlHndl);
ERR_OUT:
    return -1;
}

#define MDS_PLUG_PREFIX "plug_"
#define MAX_PLUGIN_NAME_LEN (255)
int MDSServerLoadPlugins(MDSServer* svr)
{
    struct dirent *dent;
    DIR *plugDir;
    CFString *dlPathStr;
    char plugName[MAX_PLUGIN_NAME_LEN];

    if(!svr->plugDirPath){
        MDS_ERR("Can not find plugin_dir entry in config file\n");
        goto ERR_OUT;
    }
    plugDir = opendir(cf_string_get_str(svr->plugDirPath));
    if(!plugDir){
        MDS_ERR("Open plugin directory:%s failed: %s\n", cf_string_get_str(svr->plugDirPath), strerror(errno));
        goto ERR_OUT;
    }
    dlPathStr = cf_string_new("");
    if(!dlPathStr){
        MDS_ERR("\n");
        goto ERR_CLOSEDIR;
    }
    cf_list_init(&svr->pluginHead.list);
    while((dent = readdir(plugDir))){
        char *p;
        
        MDS_DBG("dent->d_name=%s\n", dent->d_name);
        if(1 != sscanf(dent->d_name, MDS_PLUG_PREFIX"%s", plugName))
            continue;
        p = strchr(plugName, '.');
        if(strcmp(p, ".so"))
            continue;
        *p = '\0';

        cf_string_safe_cp(dlPathStr, cf_string_get_str(svr->plugDirPath));
        cf_string_safe_cat(dlPathStr, "/");
        cf_string_safe_cat(dlPathStr, dent->d_name);
        if(load_plugin(svr, cf_string_get_str(dlPathStr), plugName)){
            MDS_MSG("plugin: %s load failed!\n", dent->d_name);
        }
    }

    cf_string_free(dlPathStr);
    closedir(plugDir);
    return 0;

ERR_FREE_PTH_STR:
    cf_string_free(dlPathStr);
ERR_CLOSEDIR:
    closedir(plugDir);
ERR_OUT:
    return -1;
}

int MDSServerRmPlugins(MDSServer* this)
{
    CFListContainerForeachSafe(&this->pluginHead, tmpPlug, MDSPlugin, list){
        tmpPlug->exit(tmpPlug, this);
        dlclose(tmpPlug->dlHandl);
        dlerror();
    }
    return 0;
}

