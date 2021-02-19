/*
 * Copyright (c) CMU 1993 Computer Science, Speech Group
 *                        Chengxiang Lu and Alex Hauptmann
 * Copyright (c) 2005 Steve Underwood <steveu at coppice.org>
 * Copyright (c) 2009 Kenan Gillet
 * Copyright (c) 2010 Martin Storsjo
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <libavutil/motion_vector.h>
#include <libavformat/avformat.h>
#include "mpegpicture.h"

typedef struct t_mb_info_for_mv{
    int low_delay;
    int mb_width;
    int mb_height;
    int mb_stride;
    uint8_t *mbskip_table;
    int quarter_sample;

    //Picture
    uint32_t *mbtype;          ///< types and macros are defined in mpegutils.h
    int8_t *qscale_table;
    int16_t (*motion_val[2])[2];
}t_mb_info_for_mv;

void set_motion_vector_all(struct MpegEncContext *s, Picture *p, AVFrame *pict, enum AVCodecID id);
void set_motion_vector_core(AVCodecContext *avctx, AVFrame *pict, uint8_t *mbskip_table,
                         uint32_t *mbtype_table, int8_t *qscale_table, int16_t (*motion_val[2])[2],
                         int *low_delay,
                         int mb_width, int mb_height, int mb_stride, int quarter_sample, enum AVCodecID id);
void set_motion_vector(AVCodecContext *avctx, AVFrame *pict, t_mb_info_for_mv *mb_info_mv);
void set_motion_vector_hevc(AVCodecContext *avctx, AVFrame *pict, t_mb_info_for_mv *mb_info_mv);
void set_motion_vector_core_hevc(AVCodecContext *avctx, AVFrame *pict, int16_t (*motion_val[2])[2], int *motion_ref,
                        int quarter_sample, enum AVCodecID id);