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

#include "config.h"
#include "common/msg.h"
#include "options/options.h"

#include "video/img_format.h"
#include "video/mp_image.h"
#include "vf.h"

#include "options/m_option.h"

static const struct vf_priv_s {
    struct mp_extend crop;
} vf_priv_dflt = {
    { { -1,-1 }, { -1,-1 } }
};

//===========================================================================//

static int config(struct vf_instance *vf,
        struct mp_size size, struct mp_size dsize,
        unsigned int flags, unsigned int outfmt)
{
    struct mp_extend *crop = &vf->priv->crop;
    // calculate the missing parameters:
    if(crop->size.w<=0 || crop->size.w>size.w) crop->size.w=size.w;
    if(crop->size.h<=0 || crop->size.h>size.h) crop->size.h=size.h;
    if(crop->start.x<0) crop->start.x=(size.w-crop->size.w)/2;
    if(crop->start.y<0) crop->start.y=(size.h-crop->size.h)/2;
    // rounding:

    struct mp_imgfmt_desc fmt = mp_imgfmt_get_desc(outfmt);

    crop->start.x = MP_ALIGN_DOWN(crop->start.x, fmt.align_x);
    crop->start.y = MP_ALIGN_DOWN(crop->start.y, fmt.align_y);

    // check:
    if(crop->size.w+crop->start.x>size.w ||
       crop->size.h+crop->start.y>size.h){
        MP_WARN(vf, "[CROP] Bad position/size.w/size.h - cropped area outside of the original!\n");
        return 0;
    }
    vf_rescale_dsize(&dsize, size, crop->size);
    return vf_next_config(vf,crop->size,dsize,flags,outfmt);
}

static struct mp_image *filter(struct vf_instance *vf, struct mp_image *mpi)
{
    mp_image_crop(mpi, vf->priv->crop.start.x, vf->priv->crop.start.y,
                  vf->priv->crop.start.x + vf->priv->crop.size.w,
                  vf->priv->crop.start.y + vf->priv->crop.size.h);
    return mpi;
}

static int query_format(struct vf_instance *vf, unsigned int fmt)
{
    if (!IMGFMT_IS_HWACCEL(fmt))
        return vf_next_query_format(vf, fmt);
    return 0;
}

static int vf_open(vf_instance_t *vf){
    vf->config=config;
    vf->filter=filter;
    vf->query_format=query_format;
    MP_INFO(vf, "Crop: %d x %d, %d ; %d\n",
    vf->priv->crop.size.w,
    vf->priv->crop.size.h,
    vf->priv->crop.start.x,
    vf->priv->crop.start.y);
    return 1;
}

#define OPT_BASE_STRUCT struct vf_priv_s
static const m_option_t vf_opts_fields[] = {
    OPT_INT("w", crop.size.w, M_OPT_MIN, .min = 0),
    OPT_INT("h", crop.size.h, M_OPT_MIN, .min = 0),
    OPT_INT("x", crop.start.x, M_OPT_MIN, .min = -1),
    OPT_INT("y", crop.start.y, M_OPT_MIN, .min = -1),
    {0}
};

const vf_info_t vf_info_crop = {
    .description = "cropping",
    .name = "crop",
    .open = vf_open,
    .priv_size = sizeof(struct vf_priv_s),
    .priv_defaults = &vf_priv_dflt,
    .options = vf_opts_fields,
};

//===========================================================================//
