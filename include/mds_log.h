#ifndef _MDS_LOG_H_
#define _MDS_LOG_H_
#include <cf_log.h>

#ifdef _DEBUG_
#define MDS_DBG	CF_DBG
#else
#define MDS_DBG(...)  
#endif
#define MDS_MSG CF_MSG
#define MDS_ERR CF_ERR
#define MDS_ERR_OUT CF_ERR_OUT
#endif

