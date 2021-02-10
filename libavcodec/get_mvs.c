/*
 * Mpeg video formats-related defines and utility functions
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

#include <stdint.h>

#include "libavutil/common.h"
#include "libavutil/frame.h"
#include "libavutil/pixdesc.h"
#include "libavutil/motion_vector.h"
#include "libavutil/avassert.h"

#include "avcodec.h"
#include "mpegutils.h"
#include "mpegvideo.h"
#include "get_mvs.h"

static int add_mb(AVMotionVector *mb, uint32_t mb_type,
                  int dst_x, int dst_y,
                  int motion_x, int motion_y, int motion_scale,
                  int direction)
{
    mb->w = IS_8X8(mb_type) || IS_8X16(mb_type) ? 8 : 16;
    mb->h = IS_8X8(mb_type) || IS_16X8(mb_type) ? 8 : 16;
    mb->motion_x = motion_x;
    mb->motion_y = motion_y;
    mb->motion_scale = motion_scale;
    mb->dst_x = dst_x;
    mb->dst_y = dst_y;
    mb->src_x = dst_x + motion_x / motion_scale;
    mb->src_y = dst_y + motion_y / motion_scale;
    mb->source = direction ? 1 : -1;
    mb->flags = 0; // XXX: does mb_type contain extra information that could be exported here?
    return 1;
}

static int add_mb_vp8(AVMotionVector *mb,
                  int dst_x, int dst_y,
                  int motion_x, int motion_y, int motion_scale,
                  int direction)
{
    mb->w = 8;
    mb->h = 8;
    mb->motion_x = motion_x;
    mb->motion_y = motion_y;
    mb->motion_scale = motion_scale;
    mb->dst_x = dst_x;
    mb->dst_y = dst_y;
    mb->src_x = dst_x + motion_x / motion_scale;
    mb->src_y = dst_y + motion_y / motion_scale;
    mb->source = direction ? 1 : -1;
    mb->flags = 0; // XXX: does mb_type contain extra information that could be exported here?
    return 1;
}

void set_motion_vector_core(AVCodecContext *avctx, AVFrame *pict, uint8_t *mbskip_table,
                         uint32_t *mbtype_table, int8_t *qscale_table, int16_t (*motion_val[2])[2],
                         int *low_delay,
                         int mb_width, int mb_height, int mb_stride, int quarter_sample, enum AVCodecID id)
{
    if ((avctx->export_side_data & AV_CODEC_EXPORT_DATA_MVS) && (mbtype_table || id == AV_CODEC_ID_VP8) && motion_val[0]) {
        const int shift = 1 + quarter_sample;
        const int scale = 1 << shift;
        const int mv_sample_log2 = avctx->codec_id == AV_CODEC_ID_H264 || avctx->codec_id == AV_CODEC_ID_SVQ3 ? 2 : 1;
        const int mv_stride      = (mb_width << mv_sample_log2) +
                                   (avctx->codec->id == AV_CODEC_ID_H264 ? 0 : 1);
        int mb_x, mb_y, mbcount = 0;

        /* size is width * height * 2 * 4 where 2 is for directions and 4 is
         * for the maximum number of MB (4 MB in case of IS_8x8) */
        AVMotionVector *mvs = av_malloc_array(mb_width * mb_height, 2 * 4 * sizeof(AVMotionVector));
        if (!mvs) {
            av_log(NULL, AV_LOG_ERROR, "mvs is NULL\n");
            return;
        }

        if (id == AV_CODEC_ID_VP8)
        {
            int direction = 0;      //we just only need luma motion vector in vp8.
            int i = 0;
            for (mb_y = 0; mb_y < mb_height; mb_y++) {
                for (mb_x = 0; mb_x < mb_width; mb_x++) {
                    for (i = 0; i < 4; i++) {
                        int sx = mb_x * 16 + 4 + 8 * (i & 1);
                        int sy = mb_y * 16 + 4 + 8 * (i >> 1);
                        int xy = (mb_x * 2 + (i & 1)) +
                                (mb_y * 2 + (i >> 1)) * mb_width;
#define BLOCK_X_VP8 (2 * mb_x + (k & 1))
#define BLOCK_Y_VP8 (2 * mb_y + (k >> 1))
                        int mx = motion_val[direction][xy][0];
                        int my = motion_val[direction][xy][1];
                        mbcount += add_mb_vp8(mvs + mbcount, sx, sy, mx, my, scale, direction);
                    }
                }
            }
        } else {
            for (mb_y = 0; mb_y < mb_height; mb_y++) {
                for (mb_x = 0; mb_x < mb_width; mb_x++) {
                    int i, direction, mb_type = mbtype_table[mb_x + mb_y * mb_stride];
                    for (direction = 0; direction < 2; direction++) {
                        if ( id != AV_CODEC_ID_VC1 && (!USES_LIST(mb_type, direction))) {
                            av_log(NULL, AV_LOG_ERROR, "continue\n");
                            continue;
                        }
                        if (IS_8X8(mb_type)) {
                            for (i = 0; i < 4; i++) {
                                int sx = mb_x * 16 + 4 + 8 * (i & 1);
                                int sy = mb_y * 16 + 4 + 8 * (i >> 1);
                                int xy = (mb_x * 2 + (i & 1) +
                                        (mb_y * 2 + (i >> 1)) * mv_stride) << (mv_sample_log2 - 1);
                                int mx = motion_val[direction][xy][0];
                                int my = motion_val[direction][xy][1];
                                mbcount += add_mb(mvs + mbcount, mb_type, sx, sy, mx, my, scale, direction);
                            }
                        } else if (IS_16X8(mb_type)) {
                            for (i = 0; i < 2; i++) {
                                int sx = mb_x * 16 + 8;
                                int sy = mb_y * 16 + 4 + 8 * i;
                                int xy = (mb_x * 2 + (mb_y * 2 + i) * mv_stride) << (mv_sample_log2 - 1);
                                int mx = motion_val[direction][xy][0];
                                int my = motion_val[direction][xy][1];

                                if (IS_INTERLACED(mb_type))
                                    my *= 2;

                                mbcount += add_mb(mvs + mbcount, mb_type, sx, sy, mx, my, scale, direction);
                            }
                        } else if (IS_8X16(mb_type)) {
                            for (i = 0; i < 2; i++) {
                                int sx = mb_x * 16 + 4 + 8 * i;
                                int sy = mb_y * 16 + 8;
                                int xy = (mb_x * 2 + i + mb_y * 2 * mv_stride) << (mv_sample_log2 - 1);
                                int mx = motion_val[direction][xy][0];
                                int my = motion_val[direction][xy][1];

                                if (IS_INTERLACED(mb_type))
                                    my *= 2;

                                mbcount += add_mb(mvs + mbcount, mb_type, sx, sy, mx, my, scale, direction);
                            }
                        } else {
                            int sx = mb_x * 16 + 8;
                            int sy = mb_y * 16 + 8;
                            int xy = (mb_x + mb_y * mv_stride) << mv_sample_log2;
                            int mx = motion_val[direction][xy][0];
                            int my = motion_val[direction][xy][1];
                            mbcount += add_mb(mvs + mbcount, mb_type, sx, sy, mx, my, scale, direction);
                        }
                    }
                }
            }
        }


        if (mbcount) {
            AVFrameSideData *sd;

            av_log(avctx, AV_LOG_DEBUG, "Adding %d MVs info to frame %d\n", mbcount, avctx->frame_number);
            sd = av_frame_new_side_data(pict, AV_FRAME_DATA_MOTION_VECTORS, mbcount * sizeof(AVMotionVector));
            if (!sd) {
                av_log(NULL, AV_LOG_ERROR, "av_frame_new_side_data failed.\n");
                av_freep(&mvs);
                return;
            }
            memcpy(sd->data, mvs, mbcount * sizeof(AVMotionVector));
        }

        av_freep(&mvs);
    }
}
void set_motion_vector_all(MpegEncContext *s, Picture *p, AVFrame *pict, enum AVCodecID id)
{
    set_motion_vector_core(s->avctx, pict, s->mbskip_table, p->mb_type,
                         p->qscale_table, p->motion_val, &s->low_delay,
                         s->mb_width, s->mb_height, s->mb_stride, s->quarter_sample, id);
}

void set_motion_vector_core_hevc(AVCodecContext *avctx, AVFrame *pict, int16_t (*motion_val[2])[2],
                        int quarter_sample, enum AVCodecID id)
{
    if ((avctx->export_side_data & AV_CODEC_EXPORT_DATA_MVS) && motion_val[0]) {
        const int shift = 1 + quarter_sample;
        const int scale = 1 << shift;
        int blk8x8_x, blk8x8_y, mbcount = 0;
        int blk8x8_num_x = avctx->width/8;
        int blk8x8_num_y = avctx->height/8;
        /* size is width * height * 2 * 4 where 2 is for directions and 4 is
         * for the maximum number of MB (4 MB in case of IS_8x8) */
        AVMotionVector *mvs = av_malloc_array(blk8x8_num_x * blk8x8_num_y, 2 * sizeof(AVMotionVector));
        if (!mvs) {
            av_log(NULL, AV_LOG_ERROR, "mvs is NULL\n");
            return;
        }

        if (id == AV_CODEC_ID_HEVC)
        {
            int direction = 0;      //we just only need luma motion vector in HEVC.
            int i = 0;
            for (blk8x8_y = 0; blk8x8_y < blk8x8_num_y; blk8x8_y++) {
                for (blk8x8_x = 0; blk8x8_x < blk8x8_num_x; blk8x8_x++) {
                    int sx = blk8x8_x * 8;
                    int sy = blk8x8_y * 8;
                    int xy = blk8x8_x + (blk8x8_y * blk8x8_num_x);

                    int mx = motion_val[direction][xy][0];
                    int my = motion_val[direction][xy][1];
                    mbcount += add_mb_vp8(mvs + mbcount, sx, sy, mx, my, scale, direction);
                }
            }
        }


        if (mbcount) {
            AVFrameSideData *sd;

            av_log(avctx, AV_LOG_DEBUG, "Adding %d MVs info to frame %d\n", mbcount, avctx->frame_number);
            sd = av_frame_new_side_data(pict, AV_FRAME_DATA_MOTION_VECTORS, mbcount * sizeof(AVMotionVector));
            if (!sd) {
                av_log(NULL, AV_LOG_ERROR, "av_frame_new_side_data failed.\n");
                av_freep(&mvs);
                return;
            }
            memcpy(sd->data, mvs, mbcount * sizeof(AVMotionVector));
        }

        av_freep(&mvs);
    }
}

#if 0
#define COLOR(theta, r) \
                u = (int)(128 + r * cos(theta * M_PI / 180)); \
                v = (int)(128 + r * sin(theta * M_PI / 180));

static int set_mb_context(AVMotionVector *mb, uint32_t mb_type,
                  int dst_x, int dst_y,
                  int motion_x, int motion_y, int motion_scale,
                  int direction)
{
    mb->w = IS_8X8(mb_type) || IS_8X16(mb_type) ? 8 : 16;
    mb->h = IS_8X8(mb_type) || IS_16X8(mb_type) ? 8 : 16;
    mb->motion_x = motion_x;
    mb->motion_y = motion_y;
    mb->motion_scale = motion_scale;
    mb->dst_x = dst_x;
    mb->dst_y = dst_y;
    mb->src_x = dst_x + motion_x / motion_scale;
    mb->src_y = dst_y + motion_y / motion_scale;
    mb->source = direction ? 1 : -1;
    mb->flags = 0; // XXX: does mb_type contain extra information that could be exported here?
    return 1;
}

static int set_mv_side_data(AVCodecContext *avctx, AVFrame *pict, uint8_t *mbskip_table,
                         uint32_t *mbtype_table, int8_t *qscale_table, int16_t (*motion_val[2])[2],
                         int *low_delay,
                         int mb_width, int mb_height, int mb_stride, int quarter_sample)
{
    const int shift = 1 + quarter_sample;
    const int scale = 1 << shift;
    const int mv_sample_log2 = avctx->codec_id == AV_CODEC_ID_H264 || avctx->codec_id == AV_CODEC_ID_SVQ3 ? 2 : 1;
    const int mv_stride      = (mb_width << mv_sample_log2) +
                                (avctx->codec->id == AV_CODEC_ID_H264 ? 0 : 1);
    int mb_x, mb_y, mbcount = 0;

    /* size is width * height * 2 * 4 where 2 is for directions and 4 is
        * for the maximum number of MB (4 MB in case of IS_8x8) */
    AVMotionVector *mvs = av_malloc_array(mb_width * mb_height, 2 * 4 * sizeof(AVMotionVector));
    if (!mvs)
        return 0;

    for (mb_y = 0; mb_y < mb_height; mb_y++) {
        for (mb_x = 0; mb_x < mb_width; mb_x++) {
            int i, direction, mb_type = mbtype_table[mb_x + mb_y * mb_stride];
            for (direction = 0; direction < 2; direction++) {
                if (!USES_LIST(mb_type, direction))
                    continue;
                if (IS_8X8(mb_type)) {
                    for (i = 0; i < 4; i++) {
                        int sx = mb_x * 16 + 4 + 8 * (i & 1);
                        int sy = mb_y * 16 + 4 + 8 * (i >> 1);
                        int xy = (mb_x * 2 + (i & 1) +
                                    (mb_y * 2 + (i >> 1)) * mv_stride) << (mv_sample_log2 - 1);
                        int mx = motion_val[direction][xy][0];
                        int my = motion_val[direction][xy][1];
                        mbcount += set_mb_context(mvs + mbcount, mb_type, sx, sy, mx, my, scale, direction);
                    }
                } else if (IS_16X8(mb_type)) {
                    for (i = 0; i < 2; i++) {
                        int sx = mb_x * 16 + 8;
                        int sy = mb_y * 16 + 4 + 8 * i;
                        int xy = (mb_x * 2 + (mb_y * 2 + i) * mv_stride) << (mv_sample_log2 - 1);
                        int mx = motion_val[direction][xy][0];
                        int my = motion_val[direction][xy][1];

                        if (IS_INTERLACED(mb_type))
                            my *= 2;

                        mbcount += set_mb_context(mvs + mbcount, mb_type, sx, sy, mx, my, scale, direction);
                    }
                } else if (IS_8X16(mb_type)) {
                    for (i = 0; i < 2; i++) {
                        int sx = mb_x * 16 + 4 + 8 * i;
                        int sy = mb_y * 16 + 8;
                        int xy = (mb_x * 2 + i + mb_y * 2 * mv_stride) << (mv_sample_log2 - 1);
                        int mx = motion_val[direction][xy][0];
                        int my = motion_val[direction][xy][1];

                        if (IS_INTERLACED(mb_type))
                            my *= 2;

                        mbcount += set_mb_context(mvs + mbcount, mb_type, sx, sy, mx, my, scale, direction);
                    }
                } else {
                        int sx = mb_x * 16 + 8;
                        int sy = mb_y * 16 + 8;
                        int xy = (mb_x + mb_y * mv_stride) << mv_sample_log2;
                        int mx = motion_val[direction][xy][0];
                        int my = motion_val[direction][xy][1];
                        mbcount += set_mb_context(mvs + mbcount, mb_type, sx, sy, mx, my, scale, direction);
                }
            }
        }
    }

    if (mbcount) {
        AVFrameSideData *pSideData;

        av_log(avctx, AV_LOG_DEBUG, "Adding %d MVs info to frame %d\n", mbcount, avctx->frame_number);
        // copy mvs to AVFrameSideData.
        pSideData = av_frame_new_side_data(pict, AV_FRAME_DATA_MOTION_VECTORS, mbcount * sizeof(AVMotionVector));
        if (!pSideData) {
            av_freep(&mvs);
            return 0;
        }
        memcpy(pSideData->data, mvs, mbcount * sizeof(AVMotionVector));
    }
    av_freep(&mvs);

    return mbcount;
}

void set_motion_vector_hevc(AVCodecContext *avctx, AVFrame *pict, t_mb_info_for_mv *mb_info_mv)
{
    if (avctx == NULL || pict == NULL || mb_info_mv == NULL)
    {
        return;
    }

    uint8_t *mbskip_table = mb_info_mv->mbskip_table;
    uint32_t *mbtype_table = mb_info_mv->mbtype;
    int8_t *qscale_table = mb_info_mv->qscale_table;
    int16_t (*motion_val[2])[2];//
    motion_val[0] = mb_info_mv->motion_val[0];
    motion_val[1] = mb_info_mv->motion_val[1];
    int *low_delay = &mb_info_mv->low_delay;
    int mb_width = mb_info_mv->mb_width;
    int mb_height = mb_info_mv->mb_height;
    int mb_stride = mb_info_mv->mb_stride;
    int quarter_sample = mb_info_mv->quarter_sample;

    if (!(avctx->export_side_data & AV_CODEC_EXPORT_DATA_MVS)){
        av_log(NULL, AV_LOG_ERROR, "set_motion_vector failed, please av_dict_set flag2 first.");
        return;
    }
    if (mbtype_table == NULL || motion_val[0] == NULL)
    {
        av_log(NULL, AV_LOG_ERROR, "set_motion_vector failed, NULL pointer detected.");
        return;
    }

    int mbcount = 0;
    mbcount = set_mv_side_data(avctx, pict, mbskip_table, mbtype_table, qscale_table, motion_val,low_delay,
                         mb_width, mb_height, mb_stride, quarter_sample);

    if (mbcount <= 0)
    {
        return;
    }

    /* TODO: export all the following to make them accessible for users (and filters) */
    if (avctx->hwaccel || !mbtype_table)
        return;

    int mb_y;
    int i, ret;
    int h_chroma_shift, v_chroma_shift, block_height;
    const int mv_sample_log2 = avctx->codec_id == AV_CODEC_ID_H264 || avctx->codec_id == AV_CODEC_ID_SVQ3 ? 2 : 1;
    const int mv_stride      = (mb_width << mv_sample_log2) + (avctx->codec->id == AV_CODEC_ID_H264 ? 0 : 1);

    if (low_delay)
        *low_delay = 0; // needed to see the vectors without trashing the buffers

    ret = av_pix_fmt_get_chroma_sub_sample (avctx->pix_fmt, &h_chroma_shift, &v_chroma_shift);
    if (ret)
        return;

    av_frame_make_writable(pict);

    pict->opaque = NULL;
    block_height = 16 >> v_chroma_shift;

    for (mb_y = 0; mb_y < mb_height; mb_y++) {
        int mb_x;
        for (mb_x = 0; mb_x < mb_width; mb_x++) {
            const int mb_index = mb_x + mb_y * mb_stride;
            if (motion_val[0]) {
                int mb_type = mbtype_table[mb_index];
                uint64_t u,v;
                int y;

                u = v = 128;
                if (IS_PCM(mb_type)) {
                    COLOR(120, 48)
                } else if ((IS_INTRA(mb_type) && IS_ACPRED(mb_type)) ||
                            IS_INTRA16x16(mb_type)) {
                    COLOR(30, 48)
                } else if (IS_INTRA4x4(mb_type)) {
                    COLOR(90, 48)
                } else if (IS_DIRECT(mb_type) && IS_SKIP(mb_type)) {
                    // COLOR(120, 48)
                } else if (IS_DIRECT(mb_type)) {
                    COLOR(150, 48)
                } else if (IS_GMC(mb_type) && IS_SKIP(mb_type)) {
                    COLOR(170, 48)
                } else if (IS_GMC(mb_type)) {
                    COLOR(190, 48)
                } else if (IS_SKIP(mb_type)) {
                    // COLOR(180, 48)
                } else if (!USES_LIST(mb_type, 1)) {
                    COLOR(240, 48)
                } else if (!USES_LIST(mb_type, 0)) {
                    COLOR(0, 48)
                } else {
                    av_assert2(USES_LIST(mb_type, 0) && USES_LIST(mb_type, 1));
                    COLOR(300,48)
                }

                u *= 0x0101010101010101ULL;
                v *= 0x0101010101010101ULL;
                for (y = 0; y < block_height; y++) {
                    *(uint64_t *)(pict->data[1] + 8 * mb_x +
                                    (block_height * mb_y + y) * pict->linesize[1]) = u;
                    *(uint64_t *)(pict->data[2] + 8 * mb_x +
                                    (block_height * mb_y + y) * pict->linesize[2]) = v;
                }

                // segmentation
                if (IS_8X8(mb_type) || IS_16X8(mb_type)) {
                    *(uint64_t *)(pict->data[0] + 16 * mb_x + 0 +
                                    (16 * mb_y + 8) * pict->linesize[0]) ^= 0x8080808080808080ULL;
                    *(uint64_t *)(pict->data[0] + 16 * mb_x + 8 +
                                    (16 * mb_y + 8) * pict->linesize[0]) ^= 0x8080808080808080ULL;
                }
                if (IS_8X8(mb_type) || IS_8X16(mb_type)) {
                    for (y = 0; y < 16; y++)
                        pict->data[0][16 * mb_x + 8 + (16 * mb_y + y) *
                                        pict->linesize[0]] ^= 0x80;
                }
                if (IS_8X8(mb_type) && mv_sample_log2 >= 2) {
                    int dm = 1 << (mv_sample_log2 - 2);
                    for (i = 0; i < 4; i++) {
                        int sx = mb_x * 16 + 8 * (i & 1);
                        int sy = mb_y * 16 + 8 * (i >> 1);
                        int xy = (mb_x * 2 + (i & 1) +
                                    (mb_y * 2 + (i >> 1)) * mv_stride) << (mv_sample_log2 - 1);
                        // FIXME bidir
                        int32_t *mv = (int32_t *) &motion_val[0][xy];
                        if (mv[0] != mv[dm] ||
                            mv[dm * mv_stride] != mv[dm * (mv_stride + 1)])
                            for (y = 0; y < 8; y++)
                                pict->data[0][sx + 4 + (sy + y) * pict->linesize[0]] ^= 0x80;
                        if (mv[0] != mv[dm * mv_stride] || mv[dm] != mv[dm * (mv_stride + 1)])
                            *(uint64_t *)(pict->data[0] + sx + (sy + 4) *
                                            pict->linesize[0]) ^= 0x8080808080808080ULL;
                    }
                }

                if (IS_INTERLACED(mb_type) &&
                    avctx->codec->id == AV_CODEC_ID_H264) {
                    // hmm
                }
            }
            if (mbskip_table)
                mbskip_table[mb_index] = 0;
        }
    }

}

void set_motion_vector(AVCodecContext *avctx, AVFrame *pict, t_mb_info_for_mv *mb_info_mv)
{
    if (avctx == NULL || pict == NULL || mb_info_mv == NULL)
    {
        return;
    }

    uint8_t *mbskip_table = mb_info_mv->mbskip_table;
    uint32_t *mbtype_table = mb_info_mv->mbtype;
    int8_t *qscale_table = mb_info_mv->qscale_table;
    int16_t (*motion_val[2])[2];//
    motion_val[0] = mb_info_mv->motion_val[0];
    motion_val[1] = mb_info_mv->motion_val[1];
    int *low_delay = &mb_info_mv->low_delay;
    int mb_width = mb_info_mv->mb_width;
    int mb_height = mb_info_mv->mb_height;
    int mb_stride = mb_info_mv->mb_stride;
    int quarter_sample = mb_info_mv->quarter_sample;

    if (!(avctx->export_side_data & AV_CODEC_EXPORT_DATA_MVS)){
        av_log(NULL, AV_LOG_ERROR, "set_motion_vector failed, please av_dict_set flag2 first.");
        return;
    }
    if (mbtype_table == NULL || motion_val[0] == NULL)
    {
        av_log(NULL, AV_LOG_ERROR, "set_motion_vector failed, NULL pointer detected.");
        return;
    }

    int mbcount = 0;
    mbcount = set_mv_side_data(avctx, pict, mbskip_table, mbtype_table, qscale_table, motion_val,low_delay,
                         mb_width, mb_height, mb_stride, quarter_sample);

    if (mbcount <= 0)
    {
        return;
    }

    /* TODO: export all the following to make them accessible for users (and filters) */
    if (avctx->hwaccel || !mbtype_table)
        return;

    int mb_y;
    int i, ret;
    int h_chroma_shift, v_chroma_shift, block_height;
    const int mv_sample_log2 = avctx->codec_id == AV_CODEC_ID_H264 || avctx->codec_id == AV_CODEC_ID_SVQ3 ? 2 : 1;
    const int mv_stride      = (mb_width << mv_sample_log2) + (avctx->codec->id == AV_CODEC_ID_H264 ? 0 : 1);

    if (low_delay)
        *low_delay = 0; // needed to see the vectors without trashing the buffers

    ret = av_pix_fmt_get_chroma_sub_sample (avctx->pix_fmt, &h_chroma_shift, &v_chroma_shift);
    if (ret)
        return;

    av_frame_make_writable(pict);

    pict->opaque = NULL;
    block_height = 16 >> v_chroma_shift;

    for (mb_y = 0; mb_y < mb_height; mb_y++) {
        int mb_x;
        for (mb_x = 0; mb_x < mb_width; mb_x++) {
            const int mb_index = mb_x + mb_y * mb_stride;
            if (motion_val[0]) {
                int mb_type = mbtype_table[mb_index];
                uint64_t u,v;
                int y;

                u = v = 128;
                if (IS_PCM(mb_type)) {
                    COLOR(120, 48)
                } else if ((IS_INTRA(mb_type) && IS_ACPRED(mb_type)) ||
                            IS_INTRA16x16(mb_type)) {
                    COLOR(30, 48)
                } else if (IS_INTRA4x4(mb_type)) {
                    COLOR(90, 48)
                } else if (IS_DIRECT(mb_type) && IS_SKIP(mb_type)) {
                    // COLOR(120, 48)
                } else if (IS_DIRECT(mb_type)) {
                    COLOR(150, 48)
                } else if (IS_GMC(mb_type) && IS_SKIP(mb_type)) {
                    COLOR(170, 48)
                } else if (IS_GMC(mb_type)) {
                    COLOR(190, 48)
                } else if (IS_SKIP(mb_type)) {
                    // COLOR(180, 48)
                } else if (!USES_LIST(mb_type, 1)) {
                    COLOR(240, 48)
                } else if (!USES_LIST(mb_type, 0)) {
                    COLOR(0, 48)
                } else {
                    av_assert2(USES_LIST(mb_type, 0) && USES_LIST(mb_type, 1));
                    COLOR(300,48)
                }

                u *= 0x0101010101010101ULL;
                v *= 0x0101010101010101ULL;
                for (y = 0; y < block_height; y++) {
                    *(uint64_t *)(pict->data[1] + 8 * mb_x +
                                    (block_height * mb_y + y) * pict->linesize[1]) = u;
                    *(uint64_t *)(pict->data[2] + 8 * mb_x +
                                    (block_height * mb_y + y) * pict->linesize[2]) = v;
                }

                // segmentation
                if (IS_8X8(mb_type) || IS_16X8(mb_type)) {
                    *(uint64_t *)(pict->data[0] + 16 * mb_x + 0 +
                                    (16 * mb_y + 8) * pict->linesize[0]) ^= 0x8080808080808080ULL;
                    *(uint64_t *)(pict->data[0] + 16 * mb_x + 8 +
                                    (16 * mb_y + 8) * pict->linesize[0]) ^= 0x8080808080808080ULL;
                }
                if (IS_8X8(mb_type) || IS_8X16(mb_type)) {
                    for (y = 0; y < 16; y++)
                        pict->data[0][16 * mb_x + 8 + (16 * mb_y + y) *
                                        pict->linesize[0]] ^= 0x80;
                }
                if (IS_8X8(mb_type) && mv_sample_log2 >= 2) {
                    int dm = 1 << (mv_sample_log2 - 2);
                    for (i = 0; i < 4; i++) {
                        int sx = mb_x * 16 + 8 * (i & 1);
                        int sy = mb_y * 16 + 8 * (i >> 1);
                        int xy = (mb_x * 2 + (i & 1) +
                                    (mb_y * 2 + (i >> 1)) * mv_stride) << (mv_sample_log2 - 1);
                        // FIXME bidir
                        int32_t *mv = (int32_t *) &motion_val[0][xy];
                        if (mv[0] != mv[dm] ||
                            mv[dm * mv_stride] != mv[dm * (mv_stride + 1)])
                            for (y = 0; y < 8; y++)
                                pict->data[0][sx + 4 + (sy + y) * pict->linesize[0]] ^= 0x80;
                        if (mv[0] != mv[dm * mv_stride] || mv[dm] != mv[dm * (mv_stride + 1)])
                            *(uint64_t *)(pict->data[0] + sx + (sy + 4) *
                                            pict->linesize[0]) ^= 0x8080808080808080ULL;
                    }
                }

                if (IS_INTERLACED(mb_type) &&
                    avctx->codec->id == AV_CODEC_ID_H264) {
                    // hmm
                }
            }
            if (mbskip_table)
                mbskip_table[mb_index] = 0;
        }
    }

}
#endif

