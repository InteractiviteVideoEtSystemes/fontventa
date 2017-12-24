/*
 * The contents of this file are subject to the Mozilla Public
 * License Version 1.1 (the "License"); you may not use this file
 * except in compliance with the License. You may obtain a copy of
 * the License at http://www.mozilla.org/MPL/
 * 
 * Software distributed under the License is distributed on an "AS
 * IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
 * implied. See the License for the specific language governing
 * rights and limitations under the License.
 * 
 * The Original Code is MPEG4IP.
 * 
 * The Initial Developer of the Original Code is Cisco Systems Inc.
 * Portions created by Cisco Systems Inc. are
 * Copyright (C) Cisco Systems Inc. 2001-2002.  All Rights Reserved.
 * 
 * Contributor(s): 
 *		Dave Mackie		dmackie@cisco.com
 */

#ifndef __MP4AV_INCLUDED__
#define __MP4AV_INCLUDED__ 

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <mp4v2/mp4v2.h>
#include <inttypes.h>
#include <fcntl.h>

typedef uint8_t  u_int8_t;
typedef uint16_t u_int16_t;
typedef uint32_t u_int32_t;
typedef uint64_t u_int64_t;
typedef unsigned int u_int;
typedef unsigned int uint;

#define OPEN_RDONLY O_RDONLY
#define OPEN_CREAT O_CREAT

#if SIZEOF_LONG == 8
#define MAX_UINT64 -1LU
#define D64F "ld"
#define U64F  "lu"
#define U64 "%lu"
#define X64F "lx"
#define X64  "%lx"
#define TO_D64(a) (a##L)
#define TO_U64(a) (a##LU)
#else
#define MAX_UINT64 -1LLU
#define D64F "lld"
#define U64F  "llu"
#define U64 "%llu"
#define X64F "llx"
#define X64  "%llx"

#define TO_D64(a) (a##LL)
#define TO_U64(a) (a##LLU)
#endif


#ifdef __cplusplus
/* exploit C++ ability of default values for function parameters */
#define DEFAULT_PARM(x)	=x
#else
#define DEFAULT_PARM(x)
#endif
/* MP4 verbosity levels - e.g. MP4SetVerbosity() */
#define MP4_DETAILS_ALL                         0xFFFFFFFF
#define MP4_DETAILS_ERROR                       0x00000001
#define MP4_DETAILS_WARNING                     0x00000002
#define MP4_DETAILS_READ                        0x00000004
#define MP4_DETAILS_WRITE                       0x00000008
#define MP4_DETAILS_FIND                        0x00000010
#define MP4_DETAILS_TABLE                       0x00000020
#define MP4_DETAILS_SAMPLE                      0x00000040
#define MP4_DETAILS_HINT                        0x00000080
#define MP4_DETAILS_ISMA                        0x00000100
#define MP4_DETAILS_EDIT                        0x00000200

#define MP4_DETAILS_READ_ALL            \
        (MP4_DETAILS_READ | MP4_DETAILS_TABLE | MP4_DETAILS_SAMPLE)
#define MP4_DETAILS_WRITE_ALL           \
        (MP4_DETAILS_WRITE | MP4_DETAILS_TABLE | MP4_DETAILS_SAMPLE)

#define CHECK_AND_FREE(a) if ((a) != NULL) { free((void *)(a)); (a) = NULL;}

/* MP4AV library API */
#include "mp4av_aac.h"
#include "mp4av_ac3.h"
#include "mp4av_adts.h"
#include "mp4av_amr.h"
#include "mp4av_mp3.h"
#include "mp4av_mpeg4.h"
#include "mp4av_audio.h"
#include "mp4av_hinters.h"
#include "mp4av_mpeg3.h"

#undef DEFAULT_PARM

typedef void (*error_msg_func_t)(int loglevel,
                                 const char *lib,
                                 const char *fmt,
                                 va_list ap);
typedef void (*lib_message_func_t)(int loglevel,
                                   const char *lib,
                                   const char *fmt,
                                   ...);

#define NUM_ELEMENTS_IN_ARRAY(name) ((sizeof((name))) / (sizeof(*(name))))

#endif /* __MP4AV_INCLUDED__ */ 

