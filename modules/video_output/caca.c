/*****************************************************************************
 * caca.c: Color ASCII Art video output plugin using libcaca
 *****************************************************************************
 * Copyright (C) 2003 VideoLAN
 * $Id: caca.c,v 1.2 2003/11/22 15:53:18 sam Exp $
 *
 * Authors: Sam Hocevar <sam@zoy.org>
 *
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA.
 *****************************************************************************/

/*****************************************************************************
 * Preamble
 *****************************************************************************/
#include <errno.h>                                                 /* ENOMEM */
#include <stdlib.h>                                                /* free() */
#include <string.h>                                            /* strerror() */

#include <caca.h>

#include <vlc/vlc.h>
#include <vlc/vout.h>
#include <vlc/intf.h>
#include <vlc_keys.h>

/*****************************************************************************
 * Local prototypes
 *****************************************************************************/
static int  Create    ( vlc_object_t * );
static void Destroy   ( vlc_object_t * );

static int  Init      ( vout_thread_t * );
static void End       ( vout_thread_t * );
static int  Manage    ( vout_thread_t * );
static void Render    ( vout_thread_t *, picture_t * );
static void Display   ( vout_thread_t *, picture_t * );

/*****************************************************************************
 * Module descriptor
 *****************************************************************************/
#define MODE_TEXT N_("dithering mode")
#define MODE_LONGTEXT N_("Choose the libcaca dithering mode")

static char *mode_list[] = { "none", "ordered", "random" };
static char *mode_list_text[] = { N_("No dithering"), N_("Ordered dithering"),
                                  N_("Random dithering") };

vlc_module_begin();
    add_category_hint( N_("Dithering"), NULL, VLC_FALSE );
    add_string( "caca-dithering", "ordered", NULL, MODE_TEXT,
                MODE_LONGTEXT, VLC_FALSE );
        change_string_list( mode_list, mode_list_text, 0 );
    set_description( _("colour ASCII art video output") );
    set_capability( "video output", 12 );
    set_callbacks( Create, Destroy );
vlc_module_end();

/*****************************************************************************
 * vout_sys_t: libcaca video output method descriptor
 *****************************************************************************
 * This structure is part of the video output thread descriptor.
 * It describes the libcaca specific properties of an output thread.
 *****************************************************************************/
struct vout_sys_t
{
    struct caca_bitmap *p_bitmap;
};

/*****************************************************************************
 * Create: allocates libcaca video output thread
 *****************************************************************************
 * This function initializes libcaca vout method.
 *****************************************************************************/
static int Create( vlc_object_t *p_this )
{
    vout_thread_t *p_vout = (vout_thread_t *)p_this;
    enum caca_dithering dither = CACA_DITHER_ORDERED;
    vlc_value_t val;

    /* Allocate structure */
    p_vout->p_sys = malloc( sizeof( vout_sys_t ) );
    if( p_vout->p_sys == NULL )
    {
        msg_Err( p_vout, "out of memory" );
        return VLC_ENOMEM;
    }

    if( caca_init() )
    {
        msg_Err( p_vout, "cannot initialize libcaca" );
        free( p_vout->p_sys );
        return VLC_EGENERIC;
    }

    var_Create( p_vout, "caca-dithering", VLC_VAR_STRING | VLC_VAR_DOINHERIT );
    var_Get( p_vout, "caca-dithering", &val );
    if( val.psz_string )
    {
        if( !strcmp( val.psz_string, "none" ) )
        {
            dither = CACA_DITHER_NONE;
        }
        else if( !strcmp( val.psz_string, "random" ) )
        {
            dither = CACA_DITHER_RANDOM;
        }
        free( val.psz_string );
    }
    caca_set_dithering( dither );

    p_vout->pf_init = Init;
    p_vout->pf_end = End;
    p_vout->pf_manage = Manage;
    p_vout->pf_render = Render;
    p_vout->pf_display = Display;

    return VLC_SUCCESS;
}

/*****************************************************************************
 * Init: initialize libcaca video output thread
 *****************************************************************************/
static int Init( vout_thread_t *p_vout )
{
    int i_index;
    picture_t *p_pic = NULL;

    I_OUTPUTPICTURES = 0;

    p_vout->output.i_chroma = VLC_FOURCC('R','V','3','2');
    p_vout->output.i_width = p_vout->render.i_width;
    p_vout->output.i_height = p_vout->render.i_height;
    p_vout->output.i_aspect = p_vout->render.i_aspect;

    p_vout->output.i_rmask = 0x00ff0000;
    p_vout->output.i_gmask = 0x0000ff00;
    p_vout->output.i_bmask = 0x000000ff;

    /* Create the libcaca bitmap */
    p_vout->p_sys->p_bitmap =
        caca_create_bitmap( 32,
                            p_vout->output.i_width,
                            p_vout->output.i_height,
                            4 * ((p_vout->output.i_width + 15) & ~15),
                            p_vout->output.i_rmask,
                            p_vout->output.i_gmask,
                            p_vout->output.i_bmask );
    if( !p_vout->p_sys->p_bitmap )
    {
        msg_Err( p_vout, "could not create libcaca bitmap" );
        return VLC_EGENERIC;
    }

    /* Find an empty picture slot */
    for( i_index = 0 ; i_index < VOUT_MAX_PICTURES ; i_index++ )
    {
        if( p_vout->p_picture[ i_index ].i_status == FREE_PICTURE )
        {
            p_pic = p_vout->p_picture + i_index;
            break;
        }
    }

    if( p_pic == NULL )
    {
        return VLC_EGENERIC;
    }

    /* Allocate the picture */
    p_pic->p->i_lines = p_vout->output.i_height;
    p_pic->p->i_pitch = 4 * ((p_vout->output.i_width + 15) & ~15);
    p_pic->p->i_pixel_pitch = 4;
    p_pic->p->i_visible_pitch = 4 * p_vout->output.i_width;
    p_pic->i_planes = 1;
    p_pic->p->p_pixels = malloc( p_pic->p->i_pitch * p_pic->p->i_lines );

    p_pic->i_status = DESTROYED_PICTURE;
    p_pic->i_type   = DIRECT_PICTURE;

    PP_OUTPUTPICTURE[ I_OUTPUTPICTURES ] = p_pic;
    I_OUTPUTPICTURES++;

    return VLC_SUCCESS;
}

/*****************************************************************************
 * End: terminate libcaca video output thread
 *****************************************************************************/
static void End( vout_thread_t *p_vout )
{
    caca_free_bitmap( p_vout->p_sys->p_bitmap );
}

/*****************************************************************************
 * Destroy: destroy libcaca video output thread
 *****************************************************************************
 * Terminate an output method created by AaCreateOutputMethod
 *****************************************************************************/
static void Destroy( vlc_object_t *p_this )
{
    vout_thread_t *p_vout = (vout_thread_t *)p_this;

    caca_end();
    free( p_vout->p_sys );
}

/*****************************************************************************
 * Manage: handle libcaca events
 *****************************************************************************
 * This function should be called regularly by video output thread. It manages
 * console events. It returns a non null value on error.
 *****************************************************************************/
static int Manage( vout_thread_t *p_vout )
{
    int event;
    vlc_value_t val;

    while(( event = caca_get_event() ))
    {
        if( event & CACA_EVENT_KEY_PRESS )
        {
            switch( event & 0xffff )
            {
            case 'q':
                val.i_int = KEY_MODIFIER_CTRL | 'q';
                break;
            case ' ':
                val.i_int = KEY_SPACE;
                break;
            default:
                continue;
            }
        }

        var_Set( p_vout->p_vlc, "key-pressed", val );
    }

    return VLC_SUCCESS;
}

/*****************************************************************************
 * Render: render previously calculated output
 *****************************************************************************/
static void Render( vout_thread_t *p_vout, picture_t *p_pic )
{
    caca_clear();
    caca_blit( 0, 0, caca_get_width() - 1, caca_get_height() - 1,
               p_vout->p_sys->p_bitmap, p_pic->p->p_pixels );
}

/*****************************************************************************
 * Display: displays previously rendered output
 *****************************************************************************/
static void Display( vout_thread_t *p_vout, picture_t *p_pic )
{
    caca_refresh();
}

