#include "mds_log.h"
#include "mds_tools.h"

int MdsStrToId(MdsIdStrMap* maps, const char* str, uint64* id)
{
    int i;

    if (!str) {
        return -1;
    }
    for(i=0; maps[i].str; i++){
        MDS_DBG("%s=%llu\n", maps[i].str, maps[i].id);
        if(!strcmp(str, maps[i].str)){
            *id = maps[i].id;
            return 0;
        }
    }
    MDS_ERR("%s not found in id str map.\n", str);
    return -1;
}

const char* MdsIdToStr(MdsIdStrMap* maps, int id)
{
    int i;

    for(i=0; maps[i].str; i++){
        if(id == maps[i].id){
            return maps[i].str;
        }
    }
    CF_ERR("id=%d not found in id str map.\n", id);
    return NULL;
}

int CFJsonObjectGetIdFromString(CFJson* conf, const char* key, MdsIdStrMap* maps, uint64* id)
{
    const char* tmpC;

    if(NULL == (tmpC = CFJsonObjectGetString(conf, key))){
        CF_ERR("Get config key=%s failed\n", key);
        return -1;
    }
    if(-1 == MdsStrToId(maps, tmpC, id)){
        CF_ERR("%s not support\n", tmpC);
        return -1;
    }
    return 0;
}
