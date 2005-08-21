/***************************************************************************
 *            media.h
 *
 *  Sun Feb  6 21:47:13 2005
 *  Copyright  2005  Christoffer Sørensen
 *  Email gnomebaker@curo.dk
 ****************************************************************************/

/*
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
 
#ifndef _MEDIA_H_
#define _MEDIA_H_

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "gst/gst.h"

typedef struct 
{
	GstElement* pipeline;
	GstElement* decoder;
	GstElement* converter;
	GstElement* scale;
	GstElement* encoder;
	GstElement* dest;
	GstElement* last_element;
} MediaPipeline;

typedef enum
{
	INSTALLED = 0,
	NOT_INSTALLED
} PluginStatus;

typedef struct
{
	GString *mimetype;
	GString *gst_plugin_name;
	PluginStatus status;	
} PluginInfo;

 
typedef struct 
{
    PluginStatus status;
    gchar* mimetype;
    GString* artist;
    GString* album;
    GString* title; 
    gulong duration;
    size_t filesize;
    gulong bitrate;
    GString* formattedduration;
} MediaInfo;
 
 
void media_convert_to_wav(MediaPipeline* gstdata, gchar* file, gchar* convertedfile);
void media_query_progress_bytes(GstElement* element,gint64* pos,gint64* len);
void media_start_playing(GstElement* element);
void media_pause_playing(GstElement* element);
void media_cleanup(GstElement* element);
void media_register_plugins(void);
MediaInfo* media_get_info(const gchar* mediafile);
void media_connect_error_callback(GstElement* element,void * func);
void media_connect_eos_callback(GstElement* element,void * func);
void media_start_iteration(GstElement* pipeline);
gint64 media_calculate_track_length(const gchar* filename);
void media_info_delete(MediaInfo* self);

#endif	/* _MEDIA_H_ */