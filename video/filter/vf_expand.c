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
#include <stdbool.h>

#include <libavutil/common.h>

#include "config.h"
#include "common/msg.h"
#include "options/options.h"

#include "video/img_format.h"
#include "video/mp_image.h"
#include "vf.h"

#include "video/memcpy_pic.h"

#include "options/m_option.h"

static struct vf_priv_s {
    // These four values are a backup of the values parsed from the command line.
    // This is necessary so that we do not get a mess upon filter reinit due to
    // e.g. aspect changes and with only aspect specified on the command line,
    // where we would otherwise use the values calculated for a different aspect
    // instead of recalculating them again.
    struct mp_extend cfg_exp;
    struct mp_extend exp;
    double aspect;
    int round;
} const vf_priv_dflt = {
  { { -1, -1 }, {-1, -1} },
  { { -1, -1 }, {-1, -1} },
  0.,
  1,
};

//===========================================================================//

static int config(struct vf_instance *vf,
        struct mp_size size, struct mp_size dsize,
        unsigned int flags, unsigned int outfmt)
{
    struct mp_extend *exp = &vf->priv->exp;
    *exp = vf->priv->cfg_exp;
    // calculate the missing parameters:
#if 0
    if(exp->size.w<size.w) exp->size.w=size.w;
    if(exp->size.h<size.h) exp->size.h=size.h;
#else
    if ( exp->size.w == -1 ) exp->size.w=size.w;
      else if (exp->size.w < -1 ) exp->size.w=size.w - exp->size.w;
        else if ( exp->size.w<size.w ) exp->size.w=size.w;
    if ( exp->size.h == -1 ) exp->size.h=size.h;
      else if ( exp->size.h < -1 ) exp->size.h=size.h - exp->size.h;
        else if( exp->size.h<size.h ) exp->size.h=size.h;
#endif
    if (vf->priv->aspect) {
        float adjusted_aspect = vf->priv->aspect;
        adjusted_aspect *= ((double)size.w/size.h) / ((double)dsize.w/dsize.h);
        if (exp->size.h < exp->size.w / adjusted_aspect) {
            exp->size.h = exp->size.w / adjusted_aspect + 0.5;
        } else {
            exp->size.w = exp->size.h * adjusted_aspect + 0.5;
        }
    }
    if (vf->priv->round > 1) { // round up.
        exp->size.w = (1 + (exp->size.w - 1) / vf->priv->round) * vf->priv->round;
        exp->size.h = (1 + (exp->size.h - 1) / vf->priv->round) * vf->priv->round;
    }

    if(exp->start.x<0 || exp->start.x+size.w>exp->size.w) exp->start.x=(exp->size.w-size.w)/2;
    if(exp->start.y<0 || exp->start.y+size.h>exp->size.h) exp->start.y=(exp->size.h-size.h)/2;

    struct mp_imgfmt_desc fmt = mp_imgfmt_get_desc(outfmt);

    exp->start.x = MP_ALIGN_DOWN(exp->start.x, fmt.align_x);
    exp->start.y = MP_ALIGN_DOWN(exp->start.y, fmt.align_y);

    vf_rescale_dsize(&dsize, size, exp->size);

    return vf_next_config(vf,exp->size,dsize,flags,outfmt);
}

static struct mp_image *filter(struct vf_instance *vf, struct mp_image *mpi)
{
    int e_x = vf->priv->exp.start.x, e_y = vf->priv->exp.start.y;
    int e_w = vf->priv->exp.size.w, e_h = vf->priv->exp.size.h;

    if (e_x == 0 && e_y == 0 && e_w == mpi->size.w && e_h == mpi->size.h)
        return mpi;

    struct mp_image *dmpi = vf_alloc_out_image(vf);
    if (!dmpi)
        return NULL;
    mp_image_copy_attributes(dmpi, mpi);

    struct mp_image cropped = *dmpi;
    mp_image_crop(&cropped, e_x, e_y, e_x + mpi->size.w, e_y + mpi->size.h);
    mp_image_copy(&cropped, mpi);

    int e_x2 = e_x + MP_ALIGN_DOWN(mpi->size.w, mpi->fmt.align_x);
    int e_y2 = e_y + MP_ALIGN_DOWN(mpi->size.h, mpi->fmt.align_y);

    // top border (over the full width)
    mp_image_clear(dmpi, 0, 0, e_w, e_y);
    // bottom border (over the full width)
    mp_image_clear(dmpi, 0, e_y2, e_w, e_h);
    // left
    mp_image_clear(dmpi, 0, e_y, e_x, e_y2);
    // right
    mp_image_clear(dmpi, e_x2, e_y, e_w, e_y2);

    talloc_free(mpi);
    return dmpi;
}

static int query_format(struct vf_instance *vf, unsigned int fmt)
{
    if (!IMGFMT_IS_HWACCEL(fmt))
        return vf_next_query_format(vf, fmt);
    return 0;
}

static int vf_open(vf_instance_t *vf){
    vf->config=config;
    vf->query_format=query_format;
    vf->filter=filter;
    MP_INFO(vf, "Expand: %d x %d, %d ; %d, aspect: %f, round: %d\n",
    vf->priv->cfg_exp.size.w,
    vf->priv->cfg_exp.size.h,
    vf->priv->cfg_exp.start.x,
    vf->priv->cfg_exp.start.y,
    vf->priv->aspect,
    vf->priv->round);
    return 1;
}

#define OPT_BASE_STRUCT struct vf_priv_s
static const m_option_t vf_opts_fields[] = {
    OPT_INT("w", cfg_exp.size.w, 0),
    OPT_INT("h", cfg_exp.size.h, 0),
    OPT_INT("x", cfg_exp.start.x, M_OPT_MIN, .min = -1),
    OPT_INT("y", cfg_exp.start.y, M_OPT_MIN, .min = -1),
    OPT_DOUBLE("aspect", aspect, M_OPT_MIN, .min = 0),
    OPT_INT("round", round, M_OPT_MIN, .min = 1),
    {0}
};

const vf_info_t vf_info_expand = {
    .description = "expanding",
    .name = "expand",
    .open = vf_open,
    .priv_size = sizeof(struct vf_priv_s),
    .priv_defaults = &vf_priv_dflt,
    .options = vf_opts_fields,
};

//===========================================================================//
