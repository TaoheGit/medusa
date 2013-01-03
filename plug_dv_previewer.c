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
 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <sys/ioctl.h>
#include <cf_string.h>
#include <cf_common.h>
#include <cf_pipe.h>
#include <cf_buffer.h>
#include "medusa.h"
#include "mds_log.h"
#include "mds_media.h"
#include "mds_tools.h"
#ifdef CONFIG_DM365_IPIPE
#include <media/davinci/imp_previewer.h>
#include <media/davinci/imp_resizer.h>
#include <media/davinci/dm365_ipipe.h>
#endif
#include <xdc/std.h>
#include <ti/sdo/dmai/Dmai.h>
#include <ti/sdo/dmai/BufferGfx.h>
#include <ti/sdo/dmai/Buffer.h>

#define DV_PREVIEWER_DEV "/dev/davinci_previewer"
#define MDS_DV_PREVIEWER_CLASS_NAME   "DV_PREVIEWER"
#define MDS_MSG_TYPE_IMAGE  "Image"

typedef struct DV_PREVIEWER_elem{
    MDS_ELEM_MEMBERS;
    Buffer_Handle hOutBuf;
    MdsImgBuf dstImgBuf;
    int prevFd;
    struct {
        int width;
        int height;
        MdsPixFmt pixFmt;
    } input;
    struct {
        int enLUT:1;
        int enOTF:1;
        int enNf1:1;
        int enNf2:1;
        int enGic:1;
        int enWb:1;
        int enCfa:1;
        int enRgb2Rgb1:1;
        int enRgb2Rgb2:1;
        int enCar:1;
        int enCgs:1;
        int enGamma:1;
        int en3dLut:1;
        int enRgb2Yuv:1;
        int enGbce:1;
        int enYuv422Conv:1;
        int enLumAdj:1;
        int enYee:1;
    } output;
    int enLog:1;
    BOOL chained;
}MdsDvPreviewerElem;

#define MDS_DV_PREVIEWER_PLUGIN_NAME       "PLUG_DV_PREVIEWER"
typedef struct mds_DV_PREVIEWER_plug{
    MDS_PLUGIN_MEMBERS;
}MdsDvPreviewerPlug;

static int _DvPreviewerPlugInit(MDSPlugin* this, MDSServer* svr);
static int _DvPreviewerPlugExit(MDSPlugin* this, MDSServer* svr);
MdsDvPreviewerPlug dv_previewer = {
    .name = MDS_DV_PREVIEWER_PLUGIN_NAME,
    .init = _DvPreviewerPlugInit,
    .exit = _DvPreviewerPlugExit
};

static MDSElem* _DvPreviewerElemRequested(MDSServer* svr, CFJson* jConf);
static int _DvPreviewerElemReleased(MDSElem* elem);
MdsElemClass _DvPreviewerClass = {
    .name = MDS_DV_PREVIEWER_CLASS_NAME,
    .request = _DvPreviewerElemRequested,
    .release = _DvPreviewerElemReleased,
};

static MdsIdStrMap pFmtMap[] = {
    ID_TO_ID_STR(MDS_PIX_FMT_RGB24),
    ID_TO_ID_STR(MDS_PIX_FMT_SBGGR8),
    ID_TO_ID_STR(MDS_PIX_FMT_SBGGR16),
    ID_TO_ID_STR(MDS_PIX_FMT_YUYV),
    ID_TO_ID_STR(MDS_PIX_FMT_UYVY),
    ID_TO_ID_STR(MDS_PIX_FMT_NV12)
};

static enum imp_pix_formats MdsPixFmtToImpPixFmt(MdsPixFmt fmt)
{
    switch (fmt) {
	case (MDS_PIX_FMT_RGB24):
        MDS_DBG("\n");
	    return IPIPE_RGB888;
	case (MDS_PIX_FMT_UYVY):
    MDS_DBG("\n");
	    return IPIPE_UYVY;
	case (MDS_PIX_FMT_NV12):
    MDS_DBG("\n");
	    return IPIPE_YUV420SP;
    case MDS_PIX_FMT_SBGGR8:
    MDS_DBG("\n");
        return IPIPE_BAYER_8BIT_PACK;
    case MDS_PIX_FMT_SBGGR16:
    MDS_DBG("\n");
        return IPIPE_BAYER;
	default:
	    return -1;
    }
}

static ColorSpace_Type MdsPixFmtToDmaiColorSpace(MdsPixFmt fmt)
{
    switch (fmt) {
	case (MDS_PIX_FMT_RGB24):
	    return ColorSpace_RGB888;
	case (MDS_PIX_FMT_UYVY):
	    return ColorSpace_UYVY;
	case (MDS_PIX_FMT_NV12):
	    return ColorSpace_YUV420PSEMI;
	default:
	    return ColorSpace_NOTSET;
    }
}
static int __MdsDvPreviewerProcess(MDSElem* this, MDSElem* vendor, MdsMsg* msg)
{
    MdsImgBuf* imgBuf;
    MdsDvPreviewerElem* dvPrev;
    int ret;
    struct timeval tv;
    void* inUsrPtr;
    Buffer_Handle hOutBuf;
    struct imp_convert convert;
    
    if (strcmp(msg->type, MDS_MSG_TYPE_IMAGE)) {
        MDS_ERR("<%s> Msg type: %s not support!\n", CFStringGetStr(&this->name), msg->type);
        return -1;
    }
    dvPrev = (MdsDvPreviewerElem*)this;
    assert(dvPrev);
    imgBuf = (MdsImgBuf*)msg->data;
    assert(imgBuf);
    hOutBuf = dvPrev->hOutBuf;
    assert(hOutBuf);
    inUsrPtr = MdsImgBufGetPtr(imgBuf);
    assert(inUsrPtr);
    
    //MDS_DBG("dv previewer processing\n")
    gettimeofday(&tv, NULL);
    //MDS_DBG("now: %lld-%lld\n", (long long)tv.tv_sec, (long long)tv.tv_usec);

    bzero(&convert,sizeof(convert));
	convert.in_buff.buf_type = IMP_BUF_IN;
	convert.in_buff.index = -1;
	convert.in_buff.offset = (uint32_t)inUsrPtr;
	convert.in_buff.size = MdsImgBufGetImgBufSize(imgBuf);
	convert.out_buff1.buf_type = IMP_BUF_OUT1;
	convert.out_buff1.index = -1;
	convert.out_buff1.offset = (uint32_t)Buffer_getUserPtr(dvPrev->hOutBuf);
	convert.out_buff1.size = Buffer_getSize(dvPrev->hOutBuf);
#if 0
	if (second_out_en) {
		convert.out_buff2.buf_type = IMP_BUF_OUT2;
		convert.out_buff2.index = 0;
		convert.out_buff2.offset = buf_out2[0].offset;
		convert.out_buff2.size = buf_out2[0].size;
    }
#endif
	if (ioctl(dvPrev->prevFd, PREV_PREVIEW, &convert) < 0) {
		MDS_ERR("Error in doing preview\n");
		return -1;
	}
    
    gettimeofday(&tv, NULL);
    //MDS_DBG("now: %lld-%lld\n", (long long)tv.tv_sec, (long long)tv.tv_usec);
    ret = MDSElemCastMsg((MDSElem*)dvPrev, MDS_MSG_TYPE_IMAGE, &dvPrev->dstImgBuf);
    return ret;
}

struct pix_defect {
	short x_pos;
	short y_pos;
};

#define NUM_DPS 10
#define DEFECT_PIX_VAL 0x3FF
struct pix_defect pix_defect_tbl[NUM_DPS] = {
#if 0
		{360, 60},
		{500, 60},
		{360, 80},
		{500, 80},
		{360, 100},
		{500, 100},
		{360, 120},
		{500, 120},
		{360, 140},
		{500, 140}
#endif
		{0, 20},
		{1, 20},
		{2, 20},
		{3, 20},
		{4, 20},
		{5, 20},
		{6, 20},
		{7, 20},
		{8, 20},
		{9, 20}
};
static struct prev_lutdpc dpc;
static struct ipipe_lutdpc_entry dpc_table[NUM_DPS];

/***********************************************************************
 *  OTF DPC config params 
 ***********************************************************************/
/* 0 - min-max, 1 - min-max2 */
static int otf_min_max;
/* applicable only for mix-max2 */
/* 0 - otf_2.0, 1 - otf 3.0 */
static int otf_20_30;
static struct prev_otfdpc otfdpc;

/***********************************************************************
 * NF config params
 ***********************************************************************/
static struct prev_nf nf_params = {
	.en = 1,
	.gr_sample_meth = IPIPE_NF_BOX,
	.shft_val = 0,
	.spread_val = 3,
	.apply_lsc_gain = 0,
	.thr = { 120, 130, 135, 140, 150, 160, 170, 200 },
	.str = { 16, 16, 15, 15, 15, 15, 15, 15 },
	.edge_det_min_thr = 0,
	.edge_det_max_thr = 2047
};

/***********************************************************************
 * GIC config params
 ***********************************************************************/
static struct prev_gic gic_params = {
	.en = 1,
	.gain = 128,
	.gic_alg = IPIPE_GIC_ALG_ADAPT_GAIN, /* IPIPE_GIC_ALG_CONST_GAIN */
	.thr_sel = IPIPE_GIC_THR_REG,
	.thr  = 512,
	.slope = 512,
	.apply_lsc_gain = 0,
	.wt_fn_type = IPIPE_GIC_WT_FN_TYP_DIF,
};	

/***********************************************************************
 * WB config params
 ***********************************************************************/
static struct prev_wb wb_params = {
	.ofst_r = 0,
	.ofst_gr = 0,
	.ofst_gb = 0,
	.ofst_b = 0,
#if 0
	.gain_r = { 1 , 511 },
	.gain_gr = { 1, 511 },
	.gain_gb = { 1, 511 },
	.gain_b = { 1, 511 }
#endif
	.gain_r = { 2 , 0 },
	.gain_gr = {2, 0 },
	.gain_gb = { 2, 0 },
	.gain_b = { 2, 0 }
};


/***********************************************************************
 * CFA config params
 ***********************************************************************/
static struct prev_cfa cfa_params = {
	//.alg = IPIPE_CFA_ALG_2DIRAC,
	.alg = IPIPE_CFA_ALG_2DIRAC_DAA,
	// for 2DIR
	.hp_mix_thr_2dir = 30,
	.hp_mix_slope_2dir = 10,
	.hpf_thr_2dir = 1024,
	.hpf_slp_2dir = 0,
	.dir_thr_2dir = 4,
	.dir_slope_2dir = 10, 
	.nd_wt_2dir = 16,
	// for DAA
	.hue_fract_daa = 24,
	.edge_thr_daa = 25,
	.thr_min_daa = 27,
	.thr_slope_daa = 20,
	.slope_min_daa = 50,
	.slope_slope_daa = 40,
	.lp_wt_daa = 16
};


/***********************************************************************
 * RGB2RGB - 1/2 config params
 ***********************************************************************/
static struct prev_rgb2rgb rgb2rgb_1_params = {
#if 0
	.coef_rr = { 1, 0 },
	.coef_gr = { 0, 0 },
	.coef_br = { 0, 0 },
	.coef_rg = { 0, 0 },
	.coef_gg = { 1, 0 },
	.coef_bg = { 0, 0},
	.coef_rb = { 0, 0},
	.coef_gb = { 0, 0},
	.coef_bb = { 1, 0},
#endif
#if 1
	.coef_rr = { 2, 0 },
	.coef_gr = { 1, 0 },
	.coef_br = { 1, 0 },
	.coef_rg = { 1, 0 },
	.coef_gg = { 2, 0 },
	.coef_bg = { 1, 0},
	.coef_rb = { 1, 0},
	.coef_gb = { 1, 0},
	.coef_bb = { 2, 0},
#endif
	.out_ofst_r = 0,
	.out_ofst_g = 0,
	.out_ofst_b = 0
};


/***********************************************************************
 * Gamma config params
 ***********************************************************************/
/* 0 - gamma ram, 1 - gamma rom */
static char gama_rom = 0;

/* Gamma table R/G/B */
#if 1
static struct ipipe_gamma_entry gamma_table[MAX_SIZE_GAMMA];
#define GAMMA_TABLE_FILE "Gamma_Table.txt"
#else
static struct ipipe_gamma_entry gamma_table[MAX_SIZE_GAMMA] = {
 {0,    9}, {9,    9}, {18,    9}, {27,    9}, {36,   9}, {45,   9}, {54,    9}, {63,    9},
 {72,   9}, {81,   8}, {87,    8}, {95,    8}, {103,  8}, {110,  7}, {118,   7}, {125,   7},
 {131,  7}, {138,  6}, {144,   6}, {150,   6}, {156,  6}, {162,  6}, {168,   5}, {173,   5},
 {178,  5}, {173,  5}, {178,   5}, {184,   5}, {189,  5}, {194,  5}, {199,   5}, {204,   5},
 {208,  5}, {213,  5}, {218,   4}, {222,   4}, {226,  4}, {231,  4}, {235,   4}, {239,   4},
 {243,  4}, {248,  4}, {252,   4}, {256,   4}, {259,  4}, {263,  4}, {267,   4}, {271,   4},
 {275,  4}, {278,  4}, {282,   4}, {286,   4}, {289,  4}, {293,  3}, {296,   3}, {300,   3},
 {303,  3}, {307,  3}, {310,   3}, {313,   3}, {317,  3}, {320,  3}, {323,   3}, {326,   3},
 {329,  3}, {333,  3}, {336,   3}, {339,   3}, {342,  3}, {345,  3}, {348,   3}, {351,   3},
 {354,  3}, {357,  3}, {360,   3}, {363,   3}, {365,  3}, {368,  3}, {371,   3}, {374,   3},
 {377,  3}, {380,  3}, {382,   3}, {385,   3}, {388,  3}, {390,  3}, {393,   3}, {396,   3},
 {398,  3}, {401,  3}, {404,   3}, {406,   3}, {409,  3}, {411,  3}, {414,   3}, {417,   3},
 {419,  3}, {422,  2}, {424,   2}, {427,   2}, {429,  2}, {431,  2}, {434,   2}, {436,   2},
 {439,  2}, {441,  2}, {444,   2}, {446,   2}, {448,  2}, {451,  2}, {453,   2}, {455,   2},
 {458,  2}, {460,  2}, {462,   2}, {464,   2}, {467,  2}, {469,  2}, {471,   2}, {473,   2},
 {476,  2}, {478,  2}, {480,   2}, {482,   2}, {485,  2}, {487,  2}, {489,   2}, {491,   2},
 {493,  2}, {495,  2}, {497,   2}, {500,   2}, {502,  2}, {504,  2}, {506,   2}, {508,   2},
 {510,  2}, {512,  2}, {514,   2}, {516,   2}, {518,  2}, {520,  2}, {522,   2}, {524,   2},
 {526,  2}, {528,  2}, {530,   2}, {532,   2}, {534,  2}, {536,  2}, {538,   2}, {540,   2},
 {542,  2}, {544,  2}, {546,   2}, {548,   2}, {550,  2}, {552,  2}, {554,   2}, {556,   2},
 {558,  2}, {559,  2}, {561,   2}, {563,   2}, {565,  2}, {567,  2}, {569,   2}, {571,   2},
 {573,  2}, {574,  2}, {576,   2}, {578,   2}, {580,  2}, {582,  2}, {583,   2}, {585,   2},
 {587,  2}, {589,  2}, {591,   2}, {592,   2}, {594,  2}, {596,  2}, {598,   2}, {600,   2},
 {601,  2}, {603,  2}, {605,   2}, {607,   2}, {608,  2}, {610,  2}, {612,   2}, {613,   2},
 {615,  2}, {617,  2}, {619,   2}, {620,   2}, {622,  2}, {624,  2}, {625,   2}, {627,   2},
 {629,  2}, {630,  2}, {632,   2}, {634,   2}, {635,  2}, {637,  2}, {639,   2}, {640,   2},
 {642,  2}, {644,  2}, {645,   2}, {647,   2}, {649,  2}, {650,  2}, {652,   2}, {653,   2},
 {655,  2}, {657,  2}, {658,   2}, {660,   2}, {661,  2}, {663,  2}, {665,   2}, {666,   2},
 {668,  2}, {669,  2}, {671,   2}, {672,   2}, {674,  2}, {676,  2}, {677,   2}, {679,   2},
 {680,  2}, {682,  2}, {683,   2}, {685,   2}, {686,  2}, {688,  2}, {689,   2}, {691,   2},
 {692,  2}, {694,  2}, {695,   2}, {697,   2}, {698,  2}, {700,  1}, {701,   1}, {703,   1},
 {704,  1}, {706,  1}, {707,   1}, {709,   1}, {710,  1}, {712,  1}, {713,   1}, {715,   1},
 {716,  1}, {718,  1}, {719,   1}, {721,   1}, {722,  1}, {724,  1}, {725,   1}, {726,   1},
 {728,  1}, {729,  1}, {731,   1}, {732,   1}, {734,  1}, {735,  1}, {736,   1}, {738,   1},
 {739,  1}, {741,  1}, {742,   1}, {743,   1}, {745,  1}, {746,  1}, {748,   1}, {749,   1},
 {750,  1}, {752,  1}, {753,   1}, {755,   1}, {756,  1}, {757,  1}, {759,   1}, {760,   1},
 {762,  1}, {763,  1}, {764,   1}, {766,   1}, {767,  1}, {768,  1}, {770,   1}, {771,   1},
 {772,  1}, {774,  1}, {775,   1}, {776,   1}, {778,  1}, {779,  1}, {780,   1}, {782,   1},
 {783,  1}, {784,  1}, {786,   1}, {787,   1}, {788,  1}, {790,  1}, {791,   1}, {792,   1},
 {794,  1}, {795,  1}, {796,   1}, {798,   1}, {799,  1}, {800,  1}, {802,   1}, {803,   1},
 {804,  1}, {805,  1}, {807,   1}, {809,   1}, {811,  1}, {812,  1}, {813,   1}, {814,   1},
 {816,  1}, {817,  1}, {818,   1}, {820,   1}, {821,  1}, {822,  1}, {823,   1}, {825,   1},
 {826,  1}, {827,  1}, {828,   1}, {830,   1}, {831,  1}, {832,  1}, {833,   1}, {835,   1},
 {836,  1}, {837,  1}, {838,   1}, {840,   1}, {841,  1}, {842,  1}, {843,   1}, {844,   1},
 {846,  1}, {847,  1}, {848,   1}, {849,   1}, {851,  1}, {852,  1}, {853,   1}, {854,   1},
 {855,  1}, {857,  1}, {858,   1}, {859,   1}, {860,  1}, {861,  1}, {863,   1}, {864,   1},
 {865,  1}, {866,  1}, {867,   1}, {869,   1}, {870,  1}, {871,  1}, {872,   1}, {873,   1},
 {875,  1}, {876,  1}, {877,   1}, {878,   1}, {879,  1}, {881,  1}, {882,   1}, {883,   1},
 {884,  1}, {885,  1}, {886,   1}, {888,   1}, {889,  1}, {890,  1}, {891,   1}, {892,   1},
 {893,  1}, {894,  1}, {896,   1}, {897,   1}, {898,  1}, {899,  1}, {900,   1}, {901,   1},
 {903,  1}, {904,  1}, {905,   1}, {906,   1}, {907,  1}, {908,  1}, {909,   1}, {910,   1},
 {912,  1}, {913,  1}, {914,   1}, {915,   1}, {916,  1}, {917,  1}, {918,   1}, {920,   1},
 {921,  1}, {922,  1}, {923,   1}, {924,   1}, {925,  1}, {926,  1}, {927,   1}, {928,   1},
 {930,  1}, {931,  1}, {932,   1}, {933,   1}, {934,  1}, {935,  1}, {936,   1}, {937,   1},
 {938,  1}, {939,  1}, {941,   1}, {942,   1}, {943,  1}, {944,  1}, {945,   1}, {946,   1},
 {947,  1}, {948,  1}, {949,   1}, {950,   1}, {951,  1}, {952,  1}, {954,   1}, {955,   1},
 {956,  1}, {957,  1}, {958,   1}, {959,   1}, {960,  1}, {961,  1}, {962,   1}, {963,   1},
 {964,  1}, {965,  1}, {966,   1}, {967,   1}, {969,  1}, {970,  1}, {971,   1}, {972,   1},
 {973,  1}, {974,  1}, {975,   1}, {976,   1}, {977,  1}, {978,  1}, {979,   1}, {980,   1},
 {981,  1}, {982,  1}, {983,   1}, {984,   1}, {985,  1}, {986,  1}, {987,   1}, {988,   1},
 {989,  1}, {990,  1}, {992,   1}, {993,   1}, {994,  1}, {995,  1}, {996,   1}, {997,   1},
 {998,  1}, {999,  1}, {1000,  1}, {1001,  1}, {1002, 1}, {1003, 1}, {1004,  1}, {1005,  1},
 {1006, 1}, {1007, 1}, {1008,  1}, {1009,  1}, {1010, 1}, {1011, 1}, {1012,  1}, {1013,  1},
 {1014, 1}, {1015, 1}, {1016,  1}, {1017,  1}, {1018, 1}, {1019, 1}, {1020,  1}, {1021,  1},
};
#endif

static struct prev_gamma gamma_params = {
	.bypass_r = 0,
	.bypass_b = 0,
	.bypass_g = 0,
	.tbl_sel = IPIPE_GAMMA_TBL_ROM,
};
/***********************************************************************
 * 3D LUT config params
 ***********************************************************************/
#define RED 	0
#define GREEN	1
#define BLUE	2

#define TABLE_3D_R_FILE "/etc/table_3D_R.txt"
#define TABLE_3D_G_FILE "/etc/table_3D_G.txt"
#define TABLE_3D_B_FILE "/etc/table_3D_B.txt"
static struct ipipe_3d_lut_entry d3_lut_table[MAX_SIZE_3D_LUT];
static struct prev_3d_lut d3_lut_params = {
	.en = 1,
	.table = d3_lut_table
};

/***********************************************************************
 * RGB2YUV config params
 ***********************************************************************/
static struct prev_rgb2yuv rgb2yuv_params = {
	// S12Q8
	.coef_ry = { 0, 77},
	//.coef_ry = { 1, 250},
	.coef_gy = { 0, 150},
	//.coef_gy = { 1, 150},
	.coef_by = { 0, 29},
	.coef_rcb = { ((-43 & 0xF00) >> 8), (-43 & 0xFF)},
	.coef_gcb = { ((-85 & 0xF00) >> 8), (-85 & 0xFF)},
	.coef_bcb = { 0, 128},
	.coef_rcr = { 0, 128},
	.coef_gcr = { ((-107 & 0xF00) >> 8), (-107 & 0xFF)},
	.coef_bcr = { ((-21 & 0xF00) >> 8), (-21 & 0xFF)},
	// S11
	.out_ofst_y = 0,
	.out_ofst_cb = 128,
	.out_ofst_cr = 128	
};
/***********************************************************************
 * GBCE config params
 ***********************************************************************/
unsigned short gbce_table[MAX_SIZE_GBCE_LUT];
static struct prev_gbce gbce_params = {
	.en = 1,
	.type = IPIPE_GBCE_Y_VAL_TBL,
	//.type = IPIPE_GBCE_GAIN_TBL,
};
#define GBCE_TABLE_FILE "GBCETable.txt"

/***********************************************************************
 * YUV 422 conversion config params
 ***********************************************************************/
static struct prev_yuv422_conv yuv422_params = {
	.en_chrom_lpf = 1,
	.chrom_pos = IPIPE_YUV422_CHR_POS_COSITE
	//.chrom_pos = IPIPE_YUV422_CHR_POS_CENTRE 
};

/***********************************************************************
 * Luminance adjustment config params
 ***********************************************************************/
static struct prev_lum_adj lum_adj_params = {
	.brightness = 55,
	.contrast = 16
};

/***********************************************************************
 * Edge Enhancement config params
 ***********************************************************************/
static short yee_table[MAX_SIZE_YEE_LUT];
static struct prev_yee yee_params = {
	.en = 1,
	//.en_halo_red = 1,
	.en_halo_red = 0,
	//.merge_meth = IPIPE_YEE_ABS_MAX,
	.merge_meth = IPIPE_YEE_EE_ES,
	//.hpf_shft = 5,
	.hpf_shft = 10,
	// S10
	.hpf_coef_00 = 84,
	.hpf_coef_01 = (-8 & 0x3FF),
	.hpf_coef_02 = (-4 & 0x3FF),
	.hpf_coef_10 = (-8 & 0x3FF),
	.hpf_coef_11 = (-4 & 0x3FF),
	.hpf_coef_12 = (-2 & 0x3FF),
	.hpf_coef_20 = (-4 & 0x3FF),
	.hpf_coef_21 = (-2 & 0x3FF),
	.hpf_coef_22 = (-1 & 0x3FF),
	//.yee_thr = 12,
	.yee_thr = 20,
	.es_gain = 128,
	.es_thr1 = 768,
	.es_thr2 = 32,
	.es_gain_grad = 32,
	.es_ofst_grad = 0 
};
#define YEE_TABLE_FILE "/etc/EE_Table.txt"

/***********************************************************************
 * CAR config params
 ***********************************************************************/
static struct prev_car car_params = {
	.en = 1,
	//.meth = IPIPE_CAR_CHR_GAIN_CTRL,
	//.meth = IPIPE_CAR_MED_FLTR,
	.meth = IPIPE_CAR_DYN_SWITCH,
	.gain1 = {255, 5, 128},
	.gain2 = {255, 12, 128},
	.hpf = IPIPE_CAR_HPF_Y,
	.hpf_thr = 32,
	.hpf_shft = 0,
	.sw0 = 255,
	.sw1 = 192	
};
/***********************************************************************
 * CGS config params
 ***********************************************************************/
static struct prev_cgs cgs_params = {
	.en = 1,
	.h_thr = 106,
	.h_slope = 100,
	.h_shft = 0,
	.h_min = 50
};

static int __MdsDvPreviewerAddAsGuest(MDSElem* this, MDSElem* vendor)
{
    MdsDvPreviewerElem* dvPrev = (MdsDvPreviewerElem*)this;
	struct prev_channel_config prev_chan_config;
	struct prev_single_shot_config prev_ss_config; // single shot mode configuration
    struct prev_cap cap;
	struct prev_module_param mod_params;
    int i;
    
    if (dvPrev->chained) {
	    MDS_ERR("MdsDvPreviewerElem can only be chained once!!\n");
	    return -1;
    }
    MDS_MSG("Setting default configuration in previewer\n");
    prev_chan_config.oper_mode = IMP_MODE_SINGLE_SHOT;
    prev_chan_config.len = 0;
    prev_chan_config.config = NULL; /* to set defaults in driver */
    if (ioctl(dvPrev->prevFd, PREV_S_CONFIG, &prev_chan_config) < 0) {
        MDS_ERR_OUT(ERR_OUT, "Error in setting default configuration\n");
    }
    MDS_MSG("default configuration setting in previewer successfull\n");
    prev_chan_config.oper_mode = IMP_MODE_SINGLE_SHOT;
    prev_chan_config.len = sizeof(struct prev_single_shot_config);
    prev_chan_config.config = &prev_ss_config;

    if (ioctl(dvPrev->prevFd, PREV_G_CONFIG, &prev_chan_config) < 0) {
        MDS_ERR_OUT(ERR_OUT, "Error in getting configuration from driver\n");
    }
    prev_chan_config.oper_mode = IMP_MODE_SINGLE_SHOT;
    prev_chan_config.len = sizeof(struct prev_single_shot_config);
    prev_chan_config.config = &prev_ss_config;
    prev_ss_config.input.image_width = dvPrev->input.width;
    prev_ss_config.input.image_height = dvPrev->input.height;
    prev_ss_config.input.ppln= dvPrev->input.width * 1.5;
    prev_ss_config.input.lpfr = dvPrev->input.height + 10;
    prev_ss_config.input.pix_fmt = MdsPixFmtToImpPixFmt(dvPrev->input.pixFmt);
    if (prev_ss_config.input.pix_fmt == IPIPE_BAYER) {
        MDS_DBG("previewer input: IPIPE_BAYER\n");
        prev_ss_config.input.data_shift = IPIPEIF_5_1_BITS11_0;
    }
    
    if (ioctl(dvPrev->prevFd, PREV_S_CONFIG, &prev_chan_config) < 0) {
        MDS_ERR_OUT(ERR_OUT, "Error in setting default configuration\n");
    }
    
    cap.index = 0;
    while (ioctl(dvPrev->prevFd, PREV_ENUM_CAP, &cap) == 0) {
        /* for each of the tuning module set defaults */
        strcpy(mod_params.version, cap.version);
        /* first setup defaults */
        mod_params.module_id = cap.module_id;
        mod_params.param = NULL;
        if (ioctl(dvPrev->prevFd, PREV_S_PARAM, &mod_params) < 0) {
            MDS_ERR_OUT(ERR_OUT, "Error in setting default PREV_S_PARAM for dpc\n");
        }
        MDS_DBG("Default values set for %s\n", cap.module_name);
        switch (cap.module_id) {
            case PREV_LUTDPC:
            {
                mod_params.len = sizeof(struct prev_lutdpc);
                /* enable defect pixel correction */
                if (dvPrev->output.enLUT) {
                    dpc.en = 1;
                    dpc.repl_white = 0;
                    for (i = 0; i < NUM_DPS; i++) {
                        dpc_table[i].horz_pos = pix_defect_tbl[i].x_pos;
                        dpc_table[i].vert_pos = pix_defect_tbl[i].y_pos;
                        dpc_table[i].method = IPIPE_DPC_REPL_BY_DOT;  /* or IPIPE_DPC_CL */
                    }
                    dpc.table = &dpc_table[0];
                    dpc.dpc_size = NUM_DPS;
                    mod_params.param = &dpc;
                    if (ioctl(dvPrev->prevFd,PREV_S_PARAM, &mod_params) < 0) {
                        MDS_ERR_OUT(ERR_OUT, "Error in PREV_S_PARAM for dpc\n");
                    } else {
                        MDS_MSG("Success in setting dpc params\n");
                    }
                }
                break;
            }
            case PREV_OTFDPC:
            {
                if (dvPrev->output.enOTF) {
                    MDS_MSG("Doing OTF pixel correction\n");
                    otfdpc.en = 1;
                    mod_params.len = sizeof(struct prev_otfdpc);
                    if (!otf_min_max) {
                        MDS_MSG("choosing Min-Max OTF detection method\n");	
                        otfdpc.det_method = IPIPE_DPC_OTF_MIN_MAX;
                        /* Set the maximum value for Min max detection
                         * threshold
                         */
                        otfdpc.alg_cfg.dpc_2_0.corr_thr.r = 0x300;
                        otfdpc.alg_cfg.dpc_2_0.corr_thr.gr = 0x300;
                        otfdpc.alg_cfg.dpc_2_0.corr_thr.gb = 0x300;
                        otfdpc.alg_cfg.dpc_2_0.corr_thr.b = 0x300;
                    }
                    else {
                        MDS_MSG("choosing Min-Max2 OTF detection method\n");	
                        otfdpc.det_method = IPIPE_DPC_OTF_MIN_MAX2;
                        if (otf_20_30) {	
                            MDS_MSG("Choosing DPC 3.0 alg\n");
                            otfdpc.alg = IPIPE_OTFDPC_3_0;
                            otfdpc.alg_cfg.dpc_3_0.act_adj_shf = 1;
                            otfdpc.alg_cfg.dpc_3_0.det_thr = 0x100;
                            otfdpc.alg_cfg.dpc_3_0.det_slp = 0x20;
                            otfdpc.alg_cfg.dpc_3_0.det_thr_min = 0x300;
                            otfdpc.alg_cfg.dpc_3_0.det_thr_max = 0x310;
                            otfdpc.alg_cfg.dpc_3_0.corr_thr = 0x100;
                            otfdpc.alg_cfg.dpc_3_0.corr_slp = 0x20;
                            otfdpc.alg_cfg.dpc_3_0.corr_thr_min = 0x300;
                            otfdpc.alg_cfg.dpc_3_0.corr_thr_max = 0x310;
                        }
                        else {
                            MDS_MSG("Choosing DPC 2.0 alg\n");
                            otfdpc.alg_cfg.dpc_2_0.corr_thr.r = 0x300;
                            otfdpc.alg = IPIPE_OTFDPC_2_0;
                            otfdpc.alg_cfg.dpc_2_0.det_thr.r = 0x300;
                            otfdpc.alg_cfg.dpc_2_0.det_thr.gr = 0x300;
                            otfdpc.alg_cfg.dpc_2_0.det_thr.gb = 0x300;
                            otfdpc.alg_cfg.dpc_2_0.det_thr.b = 0x300;
                            otfdpc.alg_cfg.dpc_2_0.corr_thr.r = 0x300;
                            otfdpc.alg_cfg.dpc_2_0.corr_thr.gr = 0x300;
                            otfdpc.alg_cfg.dpc_2_0.corr_thr.gb = 0x300;
                            otfdpc.alg_cfg.dpc_2_0.corr_thr.b = 0x300;
                        }
                    }
                    mod_params.param = &otfdpc;
                    if (ioctl(dvPrev->prevFd,PREV_S_PARAM, &mod_params) < 0) {
                        MDS_ERR_OUT(ERR_OUT, "Error in setting OTF pexel correction\n");
                    } else {
                        MDS_MSG("Success in setting otfdpc params\n");
                    }
                }
                break;
            }

            case PREV_NF1:
            case PREV_NF2:
            {
                mod_params.param = &nf_params;
                mod_params.len = sizeof(struct prev_nf);
                
                if (dvPrev->output.enNf1 || dvPrev->output.enNf2) {
                    MDS_MSG("nf_params.en = %d\n", nf_params.en);
                    MDS_MSG("nf_params.gr_sample_meth = %d\n", nf_params.gr_sample_meth);
                    MDS_MSG("nf_params.shft_val = %d\n", nf_params.shft_val);
                    MDS_MSG("nf_params.spread_val = %d\n", nf_params.spread_val);
                    MDS_MSG("nf_params.apply_lsc_gain = %d\n", nf_params.apply_lsc_gain);
                    MDS_MSG("nf_params.edge_det_min_thr = %d\n", nf_params.edge_det_min_thr);
                    MDS_MSG("nf_params.edge_det_max_thr = %d\n", nf_params.edge_det_max_thr);
                    MDS_MSG("thr vals:"); 
                    for (i = 0; i < IPIPE_NF_STR_TABLE_SIZE; i++) {
                        MDS_MSG("%d,",nf_params.thr[i]);
                    }
                    MDS_MSG("\nstr vals:"); 
                    for (i = 0; i < IPIPE_NF_STR_TABLE_SIZE; i++) {
                        MDS_MSG("%d,",nf_params.str[i]);
                    }
                    MDS_MSG("\n");
                } 
                if (dvPrev->output.enNf1 && cap.module_id == PREV_NF1) {
                    if (ioctl(dvPrev->prevFd,PREV_S_PARAM, &mod_params) < 0) {
                        MDS_ERR_OUT(ERR_OUT, "Error in PREV_S_PARAM for NF1\n");
                    } else {
                        MDS_MSG("Success in setting NF1 params\n");
                    }
                }
                if (dvPrev->output.enNf2 && cap.module_id == PREV_NF2) {
                    if (ioctl(dvPrev->prevFd,PREV_S_PARAM, &mod_params) < 0) {
                        MDS_ERR_OUT(ERR_OUT, "Error in PREV_S_PARAM for NF2\n");
                    } else {
                        MDS_MSG("Success in setting NF2 params\n");
                    }
                }
                break;
            }

            case PREV_GIC:
            {
                mod_params.len = sizeof(struct prev_gic);
                mod_params.param = &gic_params;
                if (dvPrev->output.enGic) {
                    MDS_MSG("gic_params.en = %d\n", gic_params.en);
                    MDS_MSG("gic_params.gic_alg = %d\n", gic_params.gic_alg);
                    MDS_MSG("gic_params.gain = %d\n", gic_params.gain);
                    MDS_MSG("gic_params.thr_sel = %d\n", gic_params.thr_sel);
                    MDS_MSG("gic_params.thr = %d\n", gic_params.thr);
                    MDS_MSG("gic_params.slope = %d\n", gic_params.slope);
                    MDS_MSG("gic_params.apply_lsc_gain = %d\n", gic_params.apply_lsc_gain);
                    MDS_MSG("gic_params.wt_fn_type = %d\n", gic_params.wt_fn_type);

                    if (ioctl(dvPrev->prevFd,PREV_S_PARAM, &mod_params) < 0) {
                        MDS_ERR_OUT(ERR_OUT, "Error in PREV_S_PARAM for GIC\n");
                        return -1;
                    } else {
                        MDS_MSG("Success in setting GIC params\n");
                    }
                }
                break;
            }

            case PREV_WB:
            {
                mod_params.len = sizeof(struct prev_wb);
                mod_params.param = &wb_params;
                if (dvPrev->output.enWb) {
                    if (dvPrev->enLog) {
                        MDS_MSG("wb_params.ofst_r = %d\n", wb_params.ofst_r);
                        MDS_MSG("wb_params.ofst_gr = %d\n", wb_params.ofst_gr);
                        MDS_MSG("wb_params.ofst_gb = %d\n", wb_params.ofst_gb);
                        MDS_MSG("wb_params.ofst_b = %d\n", wb_params.ofst_b);
                        MDS_MSG("wb_params.gain_r = {%d:%d}\n",
                            wb_params.gain_r.integer,
                            wb_params.gain_r.decimal);
                        MDS_MSG("wb_params.gain_gr = {%d:%d}\n",
                            wb_params.gain_gr.integer,
                            wb_params.gain_gr.decimal);
                        MDS_MSG("wb_params.gain_gb = {%d:%d}\n",
                            wb_params.gain_gb.integer,
                            wb_params.gain_gb.decimal);
                        MDS_MSG("wb_params.gain_b = {%d:%d}\n",
                            wb_params.gain_b.integer,
                            wb_params.gain_b.decimal);
                    }
                    if (ioctl(dvPrev->prevFd,PREV_S_PARAM, &mod_params) < 0) {
                        MDS_ERR_OUT(ERR_OUT, "Error in PREV_S_PARAM for WB\n");
                    } else {
                        MDS_MSG("Success in setting WB params\n");
                    }
                }
                break;
            }
            case PREV_CFA:
            {
                mod_params.len = sizeof(struct prev_cfa);
                mod_params.param = &cfa_params;
                if (dvPrev->output.enCfa) {
                    MDS_MSG("cfa_params.alg = %d\n", cfa_params.alg);
                    MDS_MSG("cfa_params.hpf_thr_2dir = %d\n",
                            cfa_params.hpf_thr_2dir);
                    MDS_MSG("cfa_params.hpf_slp_2dir = %d\n",
                            cfa_params.hpf_slp_2dir);
                    MDS_MSG("cfa_params.hp_mix_thr_2dir = %d\n",
                            cfa_params.hp_mix_thr_2dir);
                    MDS_MSG("cfa_params.hp_mix_slope_2dir = %d\n",
                            cfa_params.hp_mix_slope_2dir);
                    MDS_MSG("cfa_params.dir_thr_2dir = %d\n",
                            cfa_params.dir_thr_2dir);
                    MDS_MSG("cfa_params.dir_slope_2dir = %d\n",
                            cfa_params.dir_slope_2dir);
                    MDS_MSG("cfa_params.nd_wt_2dir = %d\n",
                            cfa_params.nd_wt_2dir);
                    MDS_MSG("cfa_params.hue_fract_daa = %d\n",
                            cfa_params.hue_fract_daa);
                    MDS_MSG("cfa_params.edge_thr_daa = %d\n",
                            cfa_params.edge_thr_daa);
                    MDS_MSG("cfa_params.thr_min_daa = %d\n",
                            cfa_params.thr_min_daa);
                    MDS_MSG("cfa_params.thr_slope_daa = %d\n",
                            cfa_params.thr_slope_daa);
                    MDS_MSG("cfa_params.slope_min_daa = %d\n",
                            cfa_params.slope_min_daa);
                    MDS_MSG("cfa_params.slope_slope_daa = %d\n",
                            cfa_params.slope_slope_daa);
                    MDS_MSG("cfa_params.lp_wt_daa = %d\n",
                            cfa_params.lp_wt_daa);
                    
                    if (ioctl(dvPrev->prevFd,PREV_S_PARAM, &mod_params) < 0) {
                        perror("Error in PREV_S_PARAM for CFA\n");
                    } else {
                        MDS_MSG("Success in setting CFA params\n");
                    }

                }
                break;
            }

            case PREV_RGB2RGB_1:
            {
                mod_params.len = sizeof(struct prev_rgb2rgb);
                mod_params.param = &rgb2rgb_1_params;
                if (dvPrev->output.enRgb2Rgb1) {
                    if (dvPrev->enLog) {
                        MDS_MSG("rgb2rgb_1_params.coef_rr = {%d, %d}\n",
                            rgb2rgb_1_params.coef_rr.integer,
                            rgb2rgb_1_params.coef_rr.decimal);
                        MDS_MSG("rgb2rgb_1_params.coef_gr = {%d, %d}\n",
                            rgb2rgb_1_params.coef_gr.integer,
                            rgb2rgb_1_params.coef_gr.decimal);
                        MDS_MSG("rgb2rgb_1_params.coef_br = {%d, %d}\n",
                            rgb2rgb_1_params.coef_br.integer,
                            rgb2rgb_1_params.coef_br.decimal);
                        MDS_MSG("rgb2rgb_1_params.coef_rg = {%d, %d}\n",
                            rgb2rgb_1_params.coef_rg.integer,
                            rgb2rgb_1_params.coef_rg.decimal);
                        MDS_MSG("rgb2rgb_1_params.coef_gg = {%d, %d}\n",
                            rgb2rgb_1_params.coef_gg.integer,
                            rgb2rgb_1_params.coef_gg.decimal);
                        MDS_MSG("rgb2rgb_1_params.coef_bg = {%d, %d}\n",
                            rgb2rgb_1_params.coef_bg.integer,
                            rgb2rgb_1_params.coef_bg.decimal);
                        MDS_MSG("rgb2rgb_1_params.coef_rb = {%d, %d}\n",
                            rgb2rgb_1_params.coef_rb.integer,
                            rgb2rgb_1_params.coef_rb.decimal);
                        MDS_MSG("rgb2rgb_1_params.coef_gb = {%d, %d}\n",
                            rgb2rgb_1_params.coef_gb.integer,
                            rgb2rgb_1_params.coef_gb.decimal);
                        MDS_MSG("rgb2rgb_1_params.coef_bb = {%d, %d}\n",
                            rgb2rgb_1_params.coef_bb.integer,
                            rgb2rgb_1_params.coef_bb.decimal);
                        MDS_MSG("rgb2rgb_1_params.out_ofst_r= %d\n",
                            rgb2rgb_1_params.out_ofst_r);
                        MDS_MSG("rgb2rgb_1_params.out_ofst_g= %d\n",
                            rgb2rgb_1_params.out_ofst_g);
                        MDS_MSG("rgb2rgb_1_params.out_ofst_b= %d\n",
                            rgb2rgb_1_params.out_ofst_b);
                    }
                    if (ioctl(dvPrev->prevFd,PREV_S_PARAM, &mod_params) < 0) {
                        MDS_ERR_OUT(ERR_OUT, "Error in PREV_S_PARAM for RGB2RGB-1\n");
                    } else {
                        MDS_MSG("Success in setting RGB2RGB-1 params\n");
                    }

                }
                
                break;
            }
            case PREV_GAMMA:
            {
                mod_params.len = sizeof(struct prev_gamma);
                mod_params.param = &gamma_params;
                if (dvPrev->output.enGamma) {
                    if (!gama_rom) {
                        gamma_params.tbl_sel = IPIPE_GAMMA_TBL_RAM;
                        gamma_params.table_r = gamma_table;
                        gamma_params.table_g = gamma_table;
                        gamma_params.table_b = gamma_table;
                    }
                        
                    if (dvPrev->enLog) {
                        if (!gama_rom){
                            MDS_MSG("Selected RAM table for Gamma\n");
                        } else {
                            MDS_MSG("Selected ROM table for Gamma\n");
                        }
                    
                        MDS_MSG("gamma_params.bypass_r = %d\n",
                            gamma_params.bypass_r);
                        MDS_MSG("gamma_params.bypass_g = %d\n",
                            gamma_params.bypass_g);
                        MDS_MSG("gamma_params.bypass_b = %d\n",
                            gamma_params.bypass_b);
                        MDS_MSG("gamma_params.tbl_size = %d\n",
                            gamma_params.tbl_size);
                        MDS_MSG("gamma_params.tbl_sel = %d\n",
                            gamma_params.tbl_sel);
                    }
                    if (ioctl(dvPrev->prevFd,PREV_S_PARAM, &mod_params) < 0) {
                        MDS_ERR_OUT(ERR_OUT, "Error in PREV_S_PARAM for GAMMA\n");
                    } else {
                        MDS_MSG("Success in setting Gamma params\n");
                    }

                }
                break;
            }

            case PREV_RGB2RGB_2:
            {
                mod_params.len = sizeof(struct prev_rgb2rgb);
                mod_params.param = &rgb2rgb_1_params;
                if (dvPrev->output.enRgb2Rgb2) {
                    if (dvPrev->enLog) {
                        MDS_MSG("rgb2rgb_2_params.coef_rr = {%d, %d}\n",
                            rgb2rgb_1_params.coef_rr.integer,
                            rgb2rgb_1_params.coef_rr.decimal);
                        MDS_MSG("rgb2rgb_2_params.coef_gr = {%d, %d}\n",
                            rgb2rgb_1_params.coef_gr.integer,
                            rgb2rgb_1_params.coef_gr.decimal);
                        MDS_MSG("rgb2rgb_2_params.coef_br = {%d, %d}\n",
                            rgb2rgb_1_params.coef_br.integer,
                            rgb2rgb_1_params.coef_br.decimal);
                        MDS_MSG("rgb2rgb_2_params.coef_rg = {%d, %d}\n",
                            rgb2rgb_1_params.coef_rg.integer,
                            rgb2rgb_1_params.coef_rg.decimal);
                        MDS_MSG("rgb2rgb_2_params.coef_gg = {%d, %d}\n",
                            rgb2rgb_1_params.coef_gg.integer,
                            rgb2rgb_1_params.coef_gg.decimal);
                        MDS_MSG("rgb2rgb_2_params.coef_bg = {%d, %d}\n",
                            rgb2rgb_1_params.coef_bg.integer,
                            rgb2rgb_1_params.coef_bg.decimal);
                        MDS_MSG("rgb2rgb_2_params.coef_rb = {%d, %d}\n",
                            rgb2rgb_1_params.coef_rb.integer,
                            rgb2rgb_1_params.coef_rb.decimal);
                        MDS_MSG("rgb2rgb_2_params.coef_gb = {%d, %d}\n",
                            rgb2rgb_1_params.coef_gb.integer,
                            rgb2rgb_1_params.coef_gb.decimal);
                        MDS_MSG("rgb2rgb_2_params.coef_bb = {%d, %d}\n",
                            rgb2rgb_1_params.coef_bb.integer,
                            rgb2rgb_1_params.coef_bb.decimal);
                        MDS_MSG("rgb2rgb_2_params.out_ofst_r= %d\n",
                            rgb2rgb_1_params.out_ofst_r);
                        MDS_MSG("rgb2rgb_2_params.out_ofst_g= %d\n",
                            rgb2rgb_1_params.out_ofst_g);
                        MDS_MSG("rgb2rgb_2_params.out_ofst_b= %d\n",
                            rgb2rgb_1_params.out_ofst_b);
                    }
                    if (ioctl(dvPrev->prevFd,PREV_S_PARAM, &mod_params) < 0) {
                        MDS_ERR_OUT(ERR_OUT, "Error in PREV_S_PARAM for RGB2RGB-2\n");
                    } else {
                        MDS_MSG("Success in setting RGB2RGB-2 params\n");
                    }
                }
                break;
            }	
            case PREV_3D_LUT:
            {
                mod_params.len = sizeof(struct prev_3d_lut);
                mod_params.param = &d3_lut_params;
                if (dvPrev->output.en3dLut) {
                    if (dvPrev->enLog)
                        MDS_MSG("Enabled 3D lut \n");
                    if (ioctl(dvPrev->prevFd,PREV_S_PARAM, &mod_params) < 0) {
                        MDS_ERR_OUT(ERR_OUT, "Error in PREV_S_PARAM for 3D-LUT\n");
                    } else
                        MDS_MSG("Success in setting 3D-LUT params\n");
                }

                break;
            }
            case PREV_RGB2YUV:
            {
                mod_params.len = sizeof(struct prev_rgb2yuv);
                mod_params.param = &rgb2yuv_params;
                if (dvPrev->output.enRgb2Yuv) {
                    if (dvPrev->enLog) {
                        MDS_MSG("Enabled RGB2YUV params setting \n");
                        MDS_MSG("rgb2yuv_params.coef_ry = {0x%x, 0x%x}\n",
                            rgb2yuv_params.coef_ry.integer,
                            rgb2yuv_params.coef_ry.decimal);
                        MDS_MSG("rgb2yuv_params.coef_gy = {0x%x, 0x%x}\n",
                            rgb2yuv_params.coef_gy.integer,
                            rgb2yuv_params.coef_gy.decimal);
                        MDS_MSG("rgb2yuv_params.coef_by = {0x%x, 0x%x}\n",
                            rgb2yuv_params.coef_by.integer,
                            rgb2yuv_params.coef_by.decimal);
                        MDS_MSG("rgb2yuv_params.coef_rcb = {0x%x, 0x%x}\n",
                            rgb2yuv_params.coef_rcb.integer,
                            rgb2yuv_params.coef_rcb.decimal);
                        MDS_MSG("rgb2yuv_params.coef_gcb = {0x%x, 0x%x}\n",
                            rgb2yuv_params.coef_gcb.integer,
                            rgb2yuv_params.coef_gcb.decimal);
                        MDS_MSG("rgb2yuv_params.coef_bcb = {0x%x, 0x%x}\n",
                            rgb2yuv_params.coef_bcb.integer,
                            rgb2yuv_params.coef_bcb.decimal);
                        MDS_MSG("rgb2yuv_params.coef_rcr = {0x%x, 0x%x}\n",
                            rgb2yuv_params.coef_rcr.integer,
                            rgb2yuv_params.coef_rcr.decimal);
                        MDS_MSG("rgb2yuv_params.coef_gcr = {0x%x, 0x%x}\n",
                            rgb2yuv_params.coef_gcr.integer,
                            rgb2yuv_params.coef_gcr.decimal);
                        MDS_MSG("rgb2yuv_params.coef_bcr = {0x%x, 0x%x}\n",
                            rgb2yuv_params.coef_bcr.integer,
                            rgb2yuv_params.coef_bcr.decimal);
                        MDS_MSG("rgb2yuv_params.out_ofst_y = %x\n",
                            rgb2yuv_params.out_ofst_y);
                        MDS_MSG("rgb2yuv_params.out_ofst_cb = %x\n",
                            rgb2yuv_params.out_ofst_cb);
                        MDS_MSG("rgb2yuv_params.out_ofst_cr = %x\n",
                            rgb2yuv_params.out_ofst_cr);
                    }
                    if (ioctl(dvPrev->prevFd,PREV_S_PARAM, &mod_params) < 0) {
                        MDS_ERR_OUT(ERR_OUT, "Error in PREV_S_PARAM for RGB2YUV\n");
                    } else
                        MDS_MSG("Success in setting RGB2YUV params\n");
                }

                break;
            }
            case PREV_GBCE:
            {
                mod_params.len = sizeof(struct prev_gbce);
                mod_params.param = &gbce_params;
                if (dvPrev->output.enGbce) {
                    gbce_params.table = gbce_table;
                    if (dvPrev->enLog) {
                        MDS_MSG("Enabled GBCE\n");
                        MDS_MSG("gbce_params.type =  %d\n",
                            gbce_params.type);
                    }
                    if (ioctl(dvPrev->prevFd,PREV_S_PARAM, &mod_params) < 0) {
                        MDS_ERR_OUT(ERR_OUT, "Error in PREV_S_PARAM for GBCE\n");
                    } else
                        MDS_MSG("Success in setting GBCE params\n");
                }
                break;
            }
            case PREV_YUV422_CONV:
            {
                mod_params.len = sizeof(struct prev_yuv422_conv);
                mod_params.param = &yuv422_params;
                if (dvPrev->output.enYuv422Conv) {
                    if (dvPrev->enLog) {
                        MDS_MSG("Enabled YUV422 conversion params\n");
                        MDS_MSG("yuv422_params.en_chrom_lpf =  %d\n",
                            yuv422_params.en_chrom_lpf);
                        MDS_MSG("yuv422_params.chrom_pos =  %d\n",
                            yuv422_params.chrom_pos);
                    }
                    if (ioctl(dvPrev->prevFd,PREV_S_PARAM, &mod_params) < 0) {
                        MDS_ERR_OUT(ERR_OUT, "Error in PREV_S_PARAM for YUV422 conv\n");
                    } else
                        MDS_MSG("Success in setting YUV 422 conv params\n");
                }
                break;
            }
            case PREV_LUM_ADJ:
            {
                mod_params.len = sizeof(struct prev_lum_adj);
                mod_params.param = &lum_adj_params;
                if (dvPrev->output.enLumAdj) {
                    if (dvPrev->enLog) {
                        MDS_MSG("Enabled Lum. Adj. params\n");
                        MDS_MSG("lum_adj_params.brightness =  %d\n",
                            lum_adj_params.brightness);
                        MDS_MSG("lum_adj_params.contrast =  %d\n",
                            lum_adj_params.contrast);
                    }
                    if (ioctl(dvPrev->prevFd,PREV_S_PARAM, &mod_params) < 0) {
                        MDS_ERR_OUT(ERR_OUT, "Error in PREV_S_PARAM for Lum. Adj.\n");
                    } else
                        MDS_MSG("Success in setting Lum. Adj. params\n");
                }
                break;
            }
            case PREV_YEE:
            {
                mod_params.len = sizeof(struct prev_yee);
                mod_params.param = &yee_params;
                if (dvPrev->output.enYee) {
                    yee_params.table = yee_table;
                    if (dvPrev->enLog) {
                        MDS_MSG("yee_params.en = %d\n",
                            yee_params.en);
                        MDS_MSG("yee_params.en_halo_red = %d\n",
                            yee_params.en_halo_red);
                        MDS_MSG("yee_params.merge_meth = %d\n",
                            yee_params.merge_meth);
                        MDS_MSG("yee_params.hpf_shft = %d\n",
                            yee_params.hpf_shft);
                        MDS_MSG("yee_params.hpf_coef_00 = %d\n",
                            yee_params.hpf_coef_00);
                        MDS_MSG("yee_params.hpf_coef_01 = %d\n",
                            yee_params.hpf_coef_01);
                        MDS_MSG("yee_params.hpf_coef_02 = %d\n",
                            yee_params.hpf_coef_02);
                        MDS_MSG("yee_params.hpf_coef_10 = %d\n",
                            yee_params.hpf_coef_10);
                        MDS_MSG("yee_params.hpf_coef_11 = %d\n",
                            yee_params.hpf_coef_11);
                        MDS_MSG("yee_params.hpf_coef_12 = %d\n",
                            yee_params.hpf_coef_12);
                        MDS_MSG("yee_params.hpf_coef_20 = %d\n",
                            yee_params.hpf_coef_20);
                        MDS_MSG("yee_params.hpf_coef_21 = %d\n",
                            yee_params.hpf_coef_21);
                        MDS_MSG("yee_params.hpf_coef_22 = %d\n",
                            yee_params.hpf_coef_22);
                        MDS_MSG("yee_params.yee_thr = %d\n",
                            yee_params.yee_thr);
                        MDS_MSG("yee_params.es_gain = %d\n",
                            yee_params.es_gain);
                        MDS_MSG("yee_params.es_thr1 = %d\n",
                            yee_params.es_thr1);
                        MDS_MSG("yee_params.es_thr2 = %d\n",
                            yee_params.es_thr2);
                        MDS_MSG("yee_params.es_gain_grad= %d\n",
                            yee_params.es_gain_grad);
                        MDS_MSG("yee_params.es_ofst_grad = %d\n",
                            yee_params.es_ofst_grad);
                    }
                    if (ioctl(dvPrev->prevFd,PREV_S_PARAM, &mod_params) < 0) {
                        MDS_ERR_OUT(ERR_OUT, "Error in PREV_S_PARAM for EE.\n");
                    } else
                        MDS_MSG("Success in setting EE params\n");

                }
                break;
            }
            case PREV_CAR:
            {
                mod_params.len = sizeof(struct prev_car);
                mod_params.param = &car_params;
                if (dvPrev->output.enCar) {
                    if (dvPrev->enLog) {
                        MDS_MSG("car_params.en = %d\n",
                            car_params.en);
                        MDS_MSG("car_params.meth = %d\n",
                            car_params.meth);
                        MDS_MSG("car_params.gain1.gain = %d\n",
                            car_params.gain1.gain);
                        MDS_MSG("car_params.gain1.shft = %d\n",
                            car_params.gain1.shft);
                        MDS_MSG("car_params.gain1.gain_min = %d\n",
                            car_params.gain1.gain_min);
                        MDS_MSG("car_params.gain2.gain = %d\n",
                            car_params.gain2.gain);
                        MDS_MSG("car_params.gain2.shft = %d\n",
                            car_params.gain2.shft);
                        MDS_MSG("car_params.gain2.gain_min = %d\n",
                            car_params.gain2.gain_min);
                        MDS_MSG("car_params.hpf = %d\n",
                            car_params.hpf);
                        MDS_MSG("car_params.hpf_thr = %d\n",
                            car_params.hpf_thr);
                        MDS_MSG("car_params.hpf_shft = %d\n",
                            car_params.hpf_shft);
                        MDS_MSG("car_params.sw0 = %d\n",
                            car_params.sw0);
                        MDS_MSG("car_params.sw1 = %d\n",
                            car_params.sw1);
                    }
                    if (ioctl(dvPrev->prevFd, PREV_S_PARAM, &mod_params) < 0) {
                        MDS_ERR_OUT(ERR_OUT, "Error in PREV_S_PARAM for CAR.\n");
                    } else
                        MDS_MSG("Success in setting CAR params\n");
                }
                break;
            }

            case PREV_CGS:
            {
                mod_params.len = sizeof(struct prev_cgs);
                mod_params.param = &cgs_params;
                if (dvPrev->output.enCgs) {
                    if (dvPrev->enLog) {
                        MDS_MSG("cgs_params.en = %d\n",
                            cgs_params.en);
                        MDS_MSG("cgs_params.h_thr = %d\n",
                            cgs_params.h_thr);
                        MDS_MSG("cgs_params.h_slope = %d\n",
                            cgs_params.h_slope);
                        MDS_MSG("cgs_params.h_shft = %d\n",
                            cgs_params.h_shft);
                        MDS_MSG("cgs_params.h_min = %d\n",
                            cgs_params.h_min);
                    }
                    if (ioctl(dvPrev->prevFd,PREV_S_PARAM, &mod_params) < 0) {
                        MDS_ERR_OUT(ERR_OUT, "Error in PREV_S_PARAM for CGS.\n");
                    } else
                        MDS_MSG("Success in setting CGS params\n");
                }
                break;
            }
            
            default:
            // unknown module
            ;
        } 
        cap.index++;
    }
    dvPrev->chained = TRUE;
    return 0;
ERR_OUT:
    return -1;
}

static int __MdsDvPreviewerAddAsVendor(MDSElem* this, MDSElem* guestElem)
{
    return 0;
}

static int __MdsDvPreviewerRemoveAsGuest(MDSElem* this, MDSElem* vendor)
{
    MdsDvPreviewerElem* dvPrev = (MdsDvPreviewerElem*)this;
    
    if (!dvPrev->chained) {
	MDS_ERR("DvPreviewer Elem not chained\n")
	return -1;
    }
    return 0;
}

static int __MdsDvPreviewerRemoveAsVendor(MDSElem* this, MDSElem* guestElem)
{
    return 0;
}

/*
{
    "class": "DV_PREVIEWER",
    "name": "DvPreviewer1",
    "input": {
        "width": 640,
        "height": 480,
        "pix_fmt": "MDS_PIX_FMT_SBGGR16"
    },
    "output":{
        "enRgb2Yuv": true
    }
}
*/

static MDSElem* _DvPreviewerElemRequested(MDSServer* svr, CFJson* jConf)
{
    MdsDvPreviewerElem* dvPrev;
    const char* tmpCStr;
    MdsPixFmt inFmt, outFmt;
    uint64 tmpUint64;
    int width, height;
    void* bufPtr;
    int inBufSize, outBufSize;
    BufferGfx_Attrs gfxAttrs;
    BufferGfx_Dimensions dim;
    CFJson *inConf, *outConf = NULL;
    int tmpInt;
	unsigned long oper_mode;
    
    if (!svr || !jConf) {
        MDS_ERR_OUT(ERR_OUT, "\n");
    }
    Dmai_init();
    if (!(dvPrev = (MdsDvPreviewerElem*)malloc(sizeof(MdsDvPreviewerElem)))) {
        MDS_ERR_OUT(ERR_OUT, "malloc for MdsDvPreviewerElem failed\n");
    }
    if (!(inConf=CFJsonObjectGet(jConf, "input"))) {
        MDS_ERR_OUT(ERR_FREE_RICE, "No input in config!!\n");
    }
    if (CFJsonObjectGetIdFromString(inConf, "pix_fmt", pFmtMap, &tmpUint64)) {
        MDS_ERR_OUT(ERR_FREE_RICE, "Please specify correct \"input::pix_fmt\" options for dv_previewer\n");
    }
    inFmt = tmpUint64;
    if (CFJsonObjectGetInt(inConf, "width", &width)
            ||CFJsonObjectGetInt(inConf, "height", &height)
            ||width%2
            ||height%2) {
        MDS_ERR_OUT(ERR_FREE_RICE, "Please specify correct \"input::width\" and \"input::height\" options for dv_previewer\n");
    }
    if ((inBufSize = MdsImgGetImgBufSize(inFmt, width, height)) == -1) {
        MDS_ERR_OUT(ERR_FREE_RICE, "Can not calc input image buf size\n");
    }
    dvPrev->input.width = width;
    dvPrev->input.height = height;
    dvPrev->input.pixFmt = inFmt;
    MDS_DBG("input ==> width=%d, height=%d, imgBufSize=%d\n", width, height, inBufSize);
    
    if (!CFJsonObjectGetInt(jConf, "enLog", &tmpInt)) {
        if (!tmpInt) {
            dvPrev->enLog = 0;
        } else {
            dvPrev->enLog = 1;
        }
    }    
    if (!(outConf=CFJsonObjectGet(jConf, "output"))) {
        MDS_ERR_OUT(ERR_FREE_RICE, "No output found in config\n");
    }
    
#define SET_TRUE_FALSE_FROM_CONFIG(__item) \
    if (!CFJsonObjectGetInt(outConf, #__item, &tmpInt)) { \
        if (!tmpInt) { \
            dvPrev->output.__item = 0; \
        } else { \
            dvPrev->output.__item = 1; \
        } \
    }

    SET_TRUE_FALSE_FROM_CONFIG(enLUT);
    SET_TRUE_FALSE_FROM_CONFIG(enOTF);
    SET_TRUE_FALSE_FROM_CONFIG(enNf1);
    SET_TRUE_FALSE_FROM_CONFIG(enNf2);
    SET_TRUE_FALSE_FROM_CONFIG(enGic);
    SET_TRUE_FALSE_FROM_CONFIG(enWb);
    SET_TRUE_FALSE_FROM_CONFIG(enCfa);
    SET_TRUE_FALSE_FROM_CONFIG(enRgb2Rgb1);
    SET_TRUE_FALSE_FROM_CONFIG(enRgb2Rgb2);
    SET_TRUE_FALSE_FROM_CONFIG(enCar);
    SET_TRUE_FALSE_FROM_CONFIG(enCgs);
    SET_TRUE_FALSE_FROM_CONFIG(enGamma);
    SET_TRUE_FALSE_FROM_CONFIG(en3dLut);
    SET_TRUE_FALSE_FROM_CONFIG(enRgb2Yuv);
    SET_TRUE_FALSE_FROM_CONFIG(enGbce);
    SET_TRUE_FALSE_FROM_CONFIG(enYuv422Conv);
    SET_TRUE_FALSE_FROM_CONFIG(enLumAdj);
    SET_TRUE_FALSE_FROM_CONFIG(enYee);
    /* Prepare output buf */
    gfxAttrs = BufferGfx_Attrs_DEFAULT;
    outFmt = MDS_PIX_FMT_UYVY;
    gfxAttrs.colorSpace = MdsPixFmtToDmaiColorSpace(outFmt);
    outBufSize = MdsImgGetImgBufSize(outFmt, width, height);
    if (!(dvPrev->hOutBuf = Buffer_create(outBufSize, BufferGfx_getBufferAttrs(&gfxAttrs)))) {
        MDS_ERR_OUT(ERR_FREE_RICE, "Create image buf failed\n");
    }
    dim.x = dim.y =0;
    dim.width = width;
    dim.height = height;
    dim.lineLength = (MdsImgGetBitsPerPix(outFmt)*width)>>3;
    if (BufferGfx_setDimensions(dvPrev->hOutBuf, &dim)<0) {
        MDS_ERR_OUT(ERR_FREE_OUT_BUF, "set dimension for output buffer failed\n");
    }
    /* Prepare output mds img buf for other element process */
    if (!(bufPtr = Buffer_getUserPtr(dvPrev->hOutBuf))) {
        MDS_ERR_OUT(ERR_FREE_OUT_BUF, "get usr ptr of output buf failed\n");
    }
    if (MdsImgBufInit(&dvPrev->dstImgBuf, outFmt, width, height, bufPtr, outBufSize)) {
        MDS_ERR_OUT(ERR_FREE_OUT_BUF, "Init MdsImgBuf failed\n");
    }
    dvPrev->prevFd = open(DV_PREVIEWER_DEV, O_RDWR);
	if(dvPrev->prevFd < 0) {
		MDS_ERR_OUT(ERR_EXIT_IMG_BUF, "Cannot open previewer device\n");
	}
    oper_mode = IMP_MODE_SINGLE_SHOT;
	if (ioctl(dvPrev->prevFd, PREV_S_OPER_MODE, &oper_mode) < 0) {
		MDS_ERR_OUT(ERR_CLOSE_PREV_FD, "Can't get operation mode\n");
	}
	if (ioctl(dvPrev->prevFd, PREV_G_OPER_MODE, &oper_mode) < 0) {
		MDS_ERR_OUT(ERR_CLOSE_PREV_FD, "Can't get operation mode\n");
	}
    if (oper_mode == IMP_MODE_SINGLE_SHOT) {
		MDS_MSG("Operating mode changed successfully to single shot in previewer\n");
    } else {
		MDS_ERR_OUT(ERR_CLOSE_PREV_FD, "failed to set mode to single shot in previewer\n");
	}
    if (!(tmpCStr=CFJsonObjectGetString(jConf, "name"))
		    || MDSElemInit((MDSElem*)dvPrev, svr, &_DvPreviewerClass, tmpCStr, __MdsDvPreviewerProcess,
				    __MdsDvPreviewerAddAsGuest, __MdsDvPreviewerAddAsVendor,
				    __MdsDvPreviewerRemoveAsGuest, __MdsDvPreviewerRemoveAsVendor)) {
        MDS_ERR_OUT(ERR_CLOSE_PREV_FD, "MDSElem init failed: for %s\n", tmpCStr);
    }
    dvPrev->chained = FALSE;
    return (MDSElem*)dvPrev;
ERR_CLOSE_PREV_FD:
    close(dvPrev->prevFd);
ERR_EXIT_IMG_BUF:
    MdsImgBufExit(&dvPrev->dstImgBuf);
ERR_FREE_OUT_BUF:
    Buffer_delete(dvPrev->hOutBuf);
ERR_FREE_RICE:
    free(dvPrev);
ERR_OUT:
    return NULL;
}

static int _DvPreviewerElemReleased(MDSElem* elem)
{
    int ret = 0;
    MdsDvPreviewerElem* dvPrev = (MdsDvPreviewerElem*)elem;
    
    ret |= MDSElemExit(elem);
    close(dvPrev->prevFd);
    MdsImgBufExit(&dvPrev->dstImgBuf);
    ret |= Buffer_delete(dvPrev->hOutBuf);
    free(dvPrev);
    return ret;
}

static int _DvPreviewerPlugInit(MDSPlugin* plg, MDSServer* svr)
{
    MDS_MSG("Initiating plugin: "MDS_DV_PREVIEWER_PLUGIN_NAME"\n");
    return MDSServerRegistElemClass(svr, &_DvPreviewerClass);
}

static int _DvPreviewerPlugExit(MDSPlugin* plg, MDSServer* svr)
{
    MDS_MSG("Exiting plugin: "MDS_DV_PREVIEWER_PLUGIN_NAME"\n");
    return MDSServerAbolishElemClass(svr, &_DvPreviewerClass);
}

