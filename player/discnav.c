/*
 * This file is part of mpv.
 *
 * mpv is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * mpv is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with mpv.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <limits.h>
#include <pthread.h>
#include <assert.h>

#include "core.h"
#include "command.h"

#include "common/msg.h"
#include "common/common.h"
#include "input/input.h"

#include "stream/discnav.h"

#include "sub/dec_sub.h"
#include "sub/osd.h"

#include "video/mp_image.h"
#include "video/decode/dec_video.h"

struct mp_nav_state {
    struct mp_log *log;

    int nav_still_frame;
    bool nav_eof;
    bool nav_menu;
    bool nav_draining;

    // Accessed by OSD (possibly separate thread)
    // Protected by the given lock
    pthread_mutex_t osd_lock;
    int hi_visible;
    struct mp_rect highlight;
    struct mp_size vidsize;
    struct mp_size subsize;
    struct sub_bitmap *hi_elem;
    struct sub_bitmap *overlays[2];
    struct sub_bitmap outputs[3];
};

static inline bool is_valid_size(struct mp_size size)
{
    return size.w >= 1 && size.h >= 1;
}

static void update_resolution(struct MPContext *mpctx)
{
    struct mp_nav_state *nav = mpctx->nav_state;
    struct mp_size size;
    if (mpctx->d_sub[0])
        sub_control(mpctx->d_sub[0], SD_CTRL_GET_RESOLUTION, &size);
    if (!is_valid_size(size)) {
        struct mp_image_params vid = {0};
        if (mpctx->d_video)
            vid = mpctx->d_video->decoder_output;
        size = vid.size;
    }
    pthread_mutex_lock(&nav->osd_lock);
    nav->vidsize = size;
    pthread_mutex_unlock(&nav->osd_lock);
}

// Send update events and such.
static void update_state(struct MPContext *mpctx)
{
    mp_notify_property(mpctx, "disc-menu-active");
}

// Return 1 if in menu, 0 if in video, or <0 if no navigation possible.
int mp_nav_in_menu(struct MPContext *mpctx)
{
    return mpctx->nav_state ? mpctx->nav_state->nav_menu : -1;
}

// Allocate state and enable navigation features. Must happen before
// initializing cache, because the cache would read data. Since stream_dvdnav is
// in a mode which skips all transitions on reading data (before enabling
// navigation), this would skip some menu screens.
void mp_nav_init(struct MPContext *mpctx)
{
    assert(!mpctx->nav_state);

    // dvdnav is interactive
    if (mpctx->encode_lavc_ctx)
        return;

    struct mp_nav_cmd inp = {MP_NAV_CMD_ENABLE};
    if (stream_control(mpctx->stream, STREAM_CTRL_NAV_CMD, &inp) < 1)
        return;

    mpctx->nav_state = talloc_zero(NULL, struct mp_nav_state);
    mpctx->nav_state->log = mp_log_new(mpctx->nav_state, mpctx->log, "discnav");
    pthread_mutex_init(&mpctx->nav_state->osd_lock, NULL);

    MP_VERBOSE(mpctx->nav_state, "enabling\n");

    mp_input_enable_section(mpctx->input, "discnav", 0);
    mp_input_set_section_mouse_area(mpctx->input, "discnav-menu",
                                    INT_MIN, INT_MIN, INT_MAX, INT_MAX);

    update_state(mpctx);
}

void mp_nav_reset(struct MPContext *mpctx)
{
    struct mp_nav_state *nav = mpctx->nav_state;
    if (!nav)
        return;
    struct mp_nav_cmd inp = {MP_NAV_CMD_RESUME};
    stream_control(mpctx->stream, STREAM_CTRL_NAV_CMD, &inp);
    osd_set_nav_highlight(mpctx->osd, NULL);
    nav->hi_visible = 0;
    nav->nav_menu = false;
    nav->nav_draining = false;
    nav->nav_still_frame = 0;
    mp_input_disable_section(mpctx->input, "discnav-menu");
    stream_control(mpctx->stream, STREAM_CTRL_RESUME_CACHE, NULL);
    update_state(mpctx);
}

void mp_nav_destroy(struct MPContext *mpctx)
{
    osd_set_nav_highlight(mpctx->osd, NULL);
    if (!mpctx->nav_state)
        return;
    mp_input_disable_section(mpctx->input, "discnav");
    mp_input_disable_section(mpctx->input, "discnav-menu");
    pthread_mutex_destroy(&mpctx->nav_state->osd_lock);
    talloc_free(mpctx->nav_state);
    mpctx->nav_state = NULL;
    update_state(mpctx);
}

void mp_nav_user_input(struct MPContext *mpctx, char *command)
{
    struct mp_nav_state *nav = mpctx->nav_state;
    if (!nav)
        return;
    if (strcmp(command, "mouse_move") == 0) {
        struct mp_image_params vid = {0};
        if (mpctx->d_video)
            vid = mpctx->d_video->decoder_output;
        struct mp_nav_cmd inp = {MP_NAV_CMD_MOUSE_POS};
        int x, y;
        mp_input_get_mouse_pos(mpctx->input, &x, &y);
        osd_coords_to_video(mpctx->osd, vid.size.w, vid.size.h, &x, &y);
        inp.u.mouse_pos.x = x;
        inp.u.mouse_pos.y = y;
        stream_control(mpctx->stream, STREAM_CTRL_NAV_CMD, &inp);
    } else {
        struct mp_nav_cmd inp = {MP_NAV_CMD_MENU};
        inp.u.menu.action = command;
        stream_control(mpctx->stream, STREAM_CTRL_NAV_CMD, &inp);
    }
}

void mp_handle_nav(struct MPContext *mpctx)
{
    struct mp_nav_state *nav = mpctx->nav_state;
    if (!nav)
        return;
    while (1) {
        struct mp_nav_event *ev = NULL;
        stream_control(mpctx->stream, STREAM_CTRL_GET_NAV_EVENT, &ev);
        if (!ev)
            break;
        switch (ev->event) {
        case MP_NAV_EVENT_DRAIN: {
            nav->nav_draining = true;
            MP_VERBOSE(nav, "drain requested\n");
            break;
        }
        case MP_NAV_EVENT_RESET_ALL: {
            mpctx->stop_play = PT_RELOAD_DEMUXER;
            MP_VERBOSE(nav, "reload\n");
            // return immediately.
            // other events should be handled after reloaded.
            talloc_free(ev);
            return;
        }
        case MP_NAV_EVENT_RESET: {
            nav->nav_still_frame = 0;
            break;
        }
        case MP_NAV_EVENT_EOF:
            nav->nav_eof = true;
            break;
        case MP_NAV_EVENT_STILL_FRAME: {
            int len = ev->u.still_frame.seconds;
            MP_VERBOSE(nav, "wait for %d seconds\n", len);
            if (len > 0 && nav->nav_still_frame == 0)
                nav->nav_still_frame = len;
            break;
        }
        case MP_NAV_EVENT_MENU_MODE:
            nav->nav_menu = ev->u.menu_mode.enable;
            if (nav->nav_menu) {
                mp_input_enable_section(mpctx->input, "discnav-menu",
                                        MP_INPUT_ON_TOP);
            } else {
                mp_input_disable_section(mpctx->input, "discnav-menu");
            }
            update_state(mpctx);
            break;
        case MP_NAV_EVENT_HIGHLIGHT: {
            pthread_mutex_lock(&nav->osd_lock);
            MP_VERBOSE(nav, "highlight: %d %d %d - %d %d\n",
                       ev->u.highlight.display,
                       ev->u.highlight.sx, ev->u.highlight.sy,
                       ev->u.highlight.ex, ev->u.highlight.ey);
            nav->highlight.start.x = ev->u.highlight.sx;
            nav->highlight.start.y = ev->u.highlight.sy;
            nav->highlight.end.x = ev->u.highlight.ex;
            nav->highlight.end.y = ev->u.highlight.ey;
            nav->hi_visible = ev->u.highlight.display;
            pthread_mutex_unlock(&nav->osd_lock);
            update_resolution(mpctx);
            osd_set_nav_highlight(mpctx->osd, mpctx);
            break;
        }
        case MP_NAV_EVENT_OVERLAY: {
            pthread_mutex_lock(&nav->osd_lock);
            for (int i = 0; i < 2; i++) {
                if (nav->overlays[i])
                    talloc_free(nav->overlays[i]);
                nav->overlays[i] = talloc_steal(nav, ev->u.overlay.images[i]);
            }
            pthread_mutex_unlock(&nav->osd_lock);
            update_resolution(mpctx);
            osd_set_nav_highlight(mpctx->osd, mpctx);
            break;
        }
        default: ; // ignore
        }
        talloc_free(ev);
    }
    update_resolution(mpctx);
    if (mpctx->stop_play == AT_END_OF_FILE) {
        if (nav->nav_still_frame > 0) {
            // gross hack
            mpctx->time_frame += nav->nav_still_frame;
            mpctx->playing_last_frame = true;
            nav->nav_still_frame = -2;
        } else if (nav->nav_still_frame == -2) {
            struct mp_nav_cmd inp = {MP_NAV_CMD_SKIP_STILL};
            stream_control(mpctx->stream, STREAM_CTRL_NAV_CMD, &inp);
        }
    }
    if (nav->nav_draining && mpctx->stop_play == AT_END_OF_FILE) {
        MP_VERBOSE(nav, "execute drain\n");
        struct mp_nav_cmd inp = {MP_NAV_CMD_DRAIN_OK};
        stream_control(mpctx->stream, STREAM_CTRL_NAV_CMD, &inp);
        nav->nav_draining = false;
        stream_control(mpctx->stream, STREAM_CTRL_RESUME_CACHE, NULL);
    }
    // E.g. keep displaying still frames
    if (mpctx->stop_play == AT_END_OF_FILE && !nav->nav_eof)
        mpctx->stop_play = KEEP_PLAYING;
}

// Render "fake" highlights, because using actual dvd sub highlight elements
// is too hard, and would require extra libavcodec to begin with.
// Note: a proper solution would introduce something like
//       SD_CTRL_APPLY_DVDNAV, which would crop the vobsub frame,
//       and apply the current CLUT.
void mp_nav_get_highlight(void *priv, struct mp_osd_res res,
                          struct sub_bitmaps *out_imgs)
{
    struct MPContext *mpctx = priv;
    struct mp_nav_state *nav = mpctx->nav_state;

    pthread_mutex_lock(&nav->osd_lock);

    struct sub_bitmap *sub = nav->hi_elem;
    if (!sub)
        sub = talloc_zero(nav, struct sub_bitmap);

    nav->hi_elem = sub;
    if (!is_valid_size(nav->vidsize)) {
        pthread_mutex_unlock(&nav->osd_lock);
        return;
    }
    if (!mp_size_equals(&nav->vidsize, &nav->subsize)) {
        talloc_free(sub->bitmap);
        sub->bitmap = talloc_array(sub, uint32_t, nav->vidsize.w * nav->vidsize.h);
        memset(sub->bitmap, 0x80, talloc_get_size(sub->bitmap));
        nav->subsize = nav->vidsize;
    }

    out_imgs->num_parts = 0;

    if (nav->hi_visible) {
        sub->start = nav->highlight.start;
        sub->size.w = MPCLAMP(nav->highlight.end.x - sub->start.x, 0, nav->vidsize.w);
        sub->size.h = MPCLAMP(nav->highlight.end.y - sub->start.y, 0, nav->vidsize.h);
        sub->stride = sub->size.w * 4;
        if (sub->size.w > 0 && sub->size.h > 0)
            nav->outputs[out_imgs->num_parts++] = *sub;
    }

    if (nav->overlays[0])
        nav->outputs[out_imgs->num_parts++] = *nav->overlays[0];
    if (nav->overlays[1])
        nav->outputs[out_imgs->num_parts++] = *nav->overlays[1];

    if (out_imgs->num_parts) {
        out_imgs->parts = nav->outputs;
        out_imgs->format = SUBBITMAP_RGBA;
        osd_rescale_bitmaps(out_imgs, nav->vidsize.w, nav->vidsize.h, res, -1);
    }

    pthread_mutex_unlock(&nav->osd_lock);
}
