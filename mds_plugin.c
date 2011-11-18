#include <dlfcn.h>  /* -ldl */
#include <dirent.h>
#include <errno.h>
#include <cf_std.h>
#include "mds_log.h"
#define MDS_PLUG_ERR    CF_ERR
#define MDS_PLUG_DBG    CF_DBG
#define MDS_PLUG_MSG    CF_MSG
#include <cf_string.h>
#include <cf_common.h>
#include "medusa.h"

static int load_plugin(MDSServer* svr, const char* plugPath, const char* plugName)
{
    void* dlHndl;
    MDSPlugin* plugin;
    
    dlHndl = dlopen(plugPath, RTLD_LAZY);

    if(!dlHndl){
        MDS_PLUG_ERR("Open plugin: %s failed, %s\n", plugName, dlerror());
        goto ERR_OUT;
    }
    plugin = dlsym(dlHndl, plugName);
    if(dlerror()){
        MDS_PLUG_ERR("Can not find plugin symbol, which should be part of dl file name. Like plug_plugName.so");
        goto ERR_DLCLOSE;
    }
    plugin->dlHandl = dlHndl;
    cf_list_init(&plugin->list);
    if(plugin->init(plugin, svr)){
        MDS_PLUG_ERR("Init plugin: %s failed\n", plugName);
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
        MDS_PLUG_ERR("Can not find plugin_dir entry in config file\n");
        goto ERR_OUT;
    }
    plugDir = opendir(cf_string_get_str(svr->plugDirPath));
    if(!plugDir){
        MDS_PLUG_ERR("Open plugin directory:%s failed: %s\n", cf_string_get_str(svr->plugDirPath), strerror(errno));
        goto ERR_OUT;
    }
    dlPathStr = cf_string_new("");
    if(!dlPathStr){
        MDS_PLUG_ERR("\n");
        goto ERR_CLOSEDIR;
    }
    cf_list_init(&svr->pluginHead.list);
    while((dent = readdir(plugDir))){
        char *p;
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
            MDS_PLUG_MSG("plugin: %s load failed!\n", dent->d_name);
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

