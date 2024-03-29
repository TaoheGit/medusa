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
 
#ifndef _PLUG_CMD_H_
#define _PLUG_CMD_H_

#define MDS_MSG_TYPE_CMD    "CMD"
#include <cf_buffer.h>
typedef struct mds_cmd_msg {
    char* cmd;
    int cmdLen;
    CFBuffer* reqBuf;
    CFBuffer* respBuf;
}MdsCmdMsg;
#define MDS_DBG_CMD_SVR "MDS_DBG_CMD_SVR"
#endif
