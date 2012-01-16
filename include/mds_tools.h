#ifndef _MDS_TOOLS_H_
#define _MDS_TOOLS_H_
#include "cf_std.h"
#include "cf_common.h"
#include "cf_json.h"

#define ID_TO_ID_STR(_id)   {(uint64)_id, #_id}
typedef struct {
    uint64 id;
    const char* str;
}MdsIdStrMap;

int MdsStrToId(MdsIdStrMap* maps, const char* str, uint64* id);
const char* MdsIdToStr(MdsIdStrMap* maps, int id);
int CFJsonObjectGetIdFromString(CFJson* conf, const char* key, MdsIdStrMap* maps, uint64* id);
#endif
