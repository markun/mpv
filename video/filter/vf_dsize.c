/*
 * This file is part of MPlayer.
 *
 * MPlayer is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * MPlayer is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with MPlayer; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <limits.h>

#include "config.h"
#include "common/msg.h"
#include "options/m_option.h"

#include "video/img_format.h"
#include "video/mp_image.h"
#include "vf.h"

struct vf_priv_s {
    struct mp_size size;
    int method; // aspect method, 0 -> downscale, 1-> upscale. +2 -> original aspect.
    int round;
    float aspect;
};

static int config(struct vf_instance *vf,
    struct mp_size size, struct mp_size dsize,
    unsigned int flags, unsigned int outfmt)
{
    struct mp_size psize = vf->priv->size;
    if (vf->priv->aspect < 0.001) { // did the user input aspect or psize.w,psize.h params
        if (psize.w == 0) psize.w = dsize.w;
        if (psize.h == 0) psize.h = dsize.h;
        if (psize.w == -1) psize.w = size.w;
        if (psize.h == -1) psize.h = size.h;
        if (psize.w == -2) psize.w = psize.h * (double)dsize.w / dsize.h;
        if (psize.w == -3) psize.w = psize.h * (double)size.w / size.h;
        if (psize.h == -2) psize.h = psize.w * (double)dsize.h / dsize.w;
        if (psize.h == -3) psize.h = psize.w * (double)size.h / size.w;
        if (vf->priv->method > -1) {
            double aspect = (vf->priv->method & 2) ? ((double)size.h / size.w) : ((double)dsize.h / dsize.w);
            if ((psize.h > psize.w * aspect) ^ (vf->priv->method & 1)) {
                psize.h = psize.w * aspect;
            } else {
                psize.w = psize.h / aspect;
            }
        }
        if (vf->priv->round > 1) { // round up
            psize.w += (vf->priv->round - 1 - (psize.w - 1) % vf->priv->round);
            psize.h += (vf->priv->round - 1 - (psize.h - 1) % vf->priv->round);
        }
        dsize = psize;
    } else {
        if (vf->priv->aspect * size.h > size.w) {
            dsize.w = size.h * vf->priv->aspect + .5;
            dsize.h = size.h;
        } else {
            dsize.h = size.w / vf->priv->aspect + .5;
            dsize.w = size.w;
        }
    }
    return vf_next_config(vf, size, dsize, flags, outfmt);
}

static int vf_open(vf_instance_t *vf)
{
    vf->config = config;
    return 1;
}

#define OPT_BASE_STRUCT struct vf_priv_s
const vf_info_t vf_info_dsize = {
    .description = "reset displaysize/aspect",
    .name = "dsize",
    .open = vf_open,
    .priv_size = sizeof(struct vf_priv_s),
    .priv_defaults = &(const struct vf_priv_s){
        .aspect = 0.0,
        .size = { -1, -1 },
        .method = -1,
        .round = 1,
    },
    .options = (const struct m_option[]){
        OPT_INTRANGE("size.w", size.w, 0, -3, INT_MAX),
        OPT_INTRANGE("size.h", size.h, 0, -3, INT_MAX),
        OPT_INTRANGE("method", method, 0, -1, 3),
        OPT_INTRANGE("round", round, 0, 0, 9999),
        OPT_FLOAT("aspect", aspect, CONF_RANGE, .min = 0, .max = 10),
        {0}
    },
};
