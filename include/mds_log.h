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

#ifndef _MDS_LOG_H_
#define _MDS_LOG_H_
#include <cf_log.h>

#ifdef _DEBUG_
#define MDS_DBG CF_DBG
#else
#define MDS_DBG(...)
#endif
#define MDS_MSG CF_MSG
#define MDS_ERR CF_ERR
#define MDS_ERR_OUT CF_ERR_OUT
#endif

