/*
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
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
/*
 * File: devices.h
 * Created by: luke <luke@dhcp-45-369>
 * Created on: Mon Feb 24 21:51:18 2003
 */


#include "devices.h"
#include "exec.h"
#include <string.h>
#include "gbcommon.h"
#include <glib/gprintf.h>
#include "preferences.h"


void devices_add_device(const gchar* devicename, const gchar* deviceid, const gchar* devicenode);
void devices_clear_devicedata();
gboolean devices_parse_cdrecord_output(const gchar* buffer, const gchar* busname);
void devices_add_ide_device(const gchar* devicenode, const gchar* devicenodepath);
void devices_add_scsi_device(const gchar* devicenode, const gchar* devicenodepath);


gint deviceadditionindex = 0;


gboolean
devices_init()
{
	GB_LOG_FUNC
	
	gboolean ok = TRUE;
	
	gchar* device = g_strdup_printf(GB_DEVICE_KEY, 1);
	if(!preferences_key_exists(device) || preferences_get_bool(GB_ALWAYS_SCAN))
		ok = devices_probe_busses();
		
	g_free(device);
	
	return ok;
}


void
devices_add_device(const gchar* devicename, const gchar* deviceid, const gchar* devicenode)
{
	GB_LOG_FUNC
	g_return_if_fail(devicename != NULL);
	g_return_if_fail(deviceid != NULL);
	
	++deviceadditionindex;
	
	gchar* devicenamekey = g_strdup_printf(GB_DEVICE_NAME, deviceadditionindex);			
	gchar* deviceidkey = g_strdup_printf(GB_DEVICE_ID, deviceadditionindex);	
	gchar* devicenodekey = g_strdup_printf(GB_DEVICE_NODE, deviceadditionindex);	
	
	preferences_set_string(devicenamekey, devicename);
	preferences_set_string(deviceidkey, deviceid);
	preferences_set_string(devicenodekey, devicenode);
	
	g_free(devicenamekey);
	g_free(deviceidkey);
	g_free(devicenodekey);

	g_message("devices_add_device - Added [%s] [%s] [%s]", devicename, deviceid, devicenode);
}


gchar* 
devices_get_device_config(const gchar* devicekey, const gchar* deviceitem)
{
	GB_LOG_FUNC
	g_return_val_if_fail(devicekey != NULL, NULL);
	
	gchar* deviceitemvalue = NULL;
	gchar* devicekeylabel = preferences_get_string(devicekey);
	if(devicekeylabel != NULL);
	{
		gchar* devitemkey = g_strconcat(GB_BASE_KEY, "/", devicekeylabel, deviceitem, NULL);
		deviceitemvalue = preferences_get_string(devitemkey);		
		g_free(devitemkey);
		g_free(devicekeylabel);	
	}	
	
	return deviceitemvalue;
}


void
devices_populate_optionmenu(GtkWidget* option_menu, const gchar* defaultselect)
{
	GB_LOG_FUNC
	g_return_if_fail(option_menu != NULL);
																																		   
	GtkWidget* menu = gtk_option_menu_get_menu(GTK_OPTION_MENU(option_menu));
	if(menu != NULL)
		gtk_widget_destroy(menu);
	menu = gtk_menu_new();
	gtk_widget_show(menu);
	
	gint index = 0, history = 0;
	GSList* devices = preferences_get_key_subkeys(GB_BASE_KEY);
	GSList* item = devices;	
	while(item)
	{
		gchar* devicekey = (gchar*)item->data;		
		gchar* devicenamekey = g_strconcat(devicekey, GB_DEVICE_NAME_LABEL, NULL);
		gchar* devicename = preferences_get_string(devicenamekey);	
		if(devicename != NULL)
		{
			GtkWidget* menuitem = gtk_menu_item_new_with_label(devicename);
			gtk_widget_show(menuitem);
			gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);
			
			gchar* devkeyid = g_strrstr(devicekey, defaultselect);
			if(devkeyid != NULL)
				history = index;
			
			g_free(devicename);
		}
		
		g_free(devicekey);
		g_free(devicenamekey);		
		
		++index;
		item = item->next;
	}
	
	g_slist_free(devices);
	
	gtk_option_menu_set_menu(GTK_OPTION_MENU(option_menu), menu);	
	gtk_option_menu_set_history(GTK_OPTION_MENU(option_menu), history);
}


void
devices_clear_devicedata()
{
	GB_LOG_FUNC

	deviceadditionindex = 0;

	GSList* devices = preferences_get_key_subkeys(GB_BASE_KEY);
	GSList* item = devices;	
	while(item)
	{
		gchar* devicekey = (gchar*)item->data;				
		gchar* devicenamekey = g_strconcat(devicekey, GB_DEVICE_NAME_LABEL, NULL);
		preferences_delete_key(devicenamekey);
		gchar* deviceidkey = g_strconcat(devicekey, GB_DEVICE_ID_LABEL, NULL);		
		preferences_delete_key(deviceidkey);				
		gchar* devicenodekey = g_strconcat(devicekey, GB_DEVICE_NODE_LABEL, NULL);		
		preferences_delete_key(devicenodekey);
		preferences_delete_key(devicekey);
		
		g_free(deviceidkey);
		g_free(devicenamekey);		
		g_free(devicenodekey);
		g_free(devicekey);
		item = item->next;
	}
	
	g_slist_free(devices);		
}


gboolean
devices_parse_cdrecord_output(const gchar* buffer, const gchar* busname)
{
	GB_LOG_FUNC
	g_return_val_if_fail(buffer != NULL, FALSE);
	g_return_val_if_fail(busname != NULL, FALSE);
	gboolean ok = TRUE;		
		
	gchar** lines = g_strsplit(buffer, "\n", 0);
	gchar** line = lines;
	while(*line != NULL)
	{
		/*	Ignore stuff like my camera which cdrecord detects...
		'OLYMPUS ' 'D-230           ' '1.00' Removable Disk */
		if(strstr(*line, "Removable Disk") == NULL)
		{
			gchar vendor[9], model[17], deviceid[6];
			
			if(sscanf(*line, "\t%5c\t  %*d) '%8c' '%16c'", deviceid, vendor, model) == 3) 
			{
				vendor[8] = '\0'; 
				model[16] = '\0';
				deviceid[5] = '\0';								
				
				gchar* device = NULL;
		
				/* Copy the bus id stuff ie 0,0,0 to the device struct. 
				   If the bus is NULL it's SCSI  */
				if(g_ascii_strncasecmp(busname, "SCSI", 4) == 0)
					device = g_strdup(deviceid);
				else if(g_ascii_strncasecmp(busname, "/dev", 4) == 0)
					device = g_strdup(busname);
				else
					device = g_strconcat(busname, ":", deviceid, NULL);
				
				gchar* displayname = g_strdup_printf("%s %s (%s)", g_strstrip(vendor), 
					g_strstrip(model), device);
								
				devices_add_device(displayname, device, device);
				
				g_free(displayname);
				g_free(device);
			}
		}
		++line;
	}
	
	g_strfreev (lines);
	
	return ok;
}


gboolean 
devices_probe_bus(const gchar* bus)
{
	GB_LOG_FUNC
	gboolean ok = FALSE;
	g_return_val_if_fail(bus != NULL, FALSE);
		
	gchar command[32] = "cdrecord -scanbus";
	if(g_ascii_strncasecmp(bus, "SCSI", 4) != 0)
	{
		strcat(command, " dev=");
		strcat(command, bus);
	}
	
	GString* buffer = exec_run_cmd(command);
	if(buffer == NULL)
		g_critical("devices_probe_bus - Failed to scan the scsi bus");
	else if(!devices_parse_cdrecord_output(buffer->str, bus))	
		g_critical("devices_probe_bus - failed to parse cdrecord output");
	else
		ok = TRUE;
	
	g_string_free(buffer, TRUE);

	return ok;
}


void 
devices_add_ide_device(const gchar* devicenode, const gchar* devicenodepath)
{
	GB_LOG_FUNC
	g_return_if_fail(devicenode != NULL);	
	g_message("devices_add_ide_device - probing [%s]", devicenode);

	gchar* contents = NULL;
	gchar* file = g_strdup_printf("/proc/ide/%s/model", devicenode);
	if(g_file_get_contents(file, &contents, NULL, NULL))
	{
		g_strstrip(contents);
		g_message("devices_add_ide_device - model is [%s]", contents);
		devices_add_device(contents, devicenodepath, devicenodepath);		
		g_free(contents);
	}
	else
	{
		g_critical("Failed to open %s", file);
	}
	g_free(file);
}


void 
devices_add_scsi_device(const gchar* devicenode, const gchar* devicenodepath)
{
	GB_LOG_FUNC
	g_return_if_fail(devicenode != NULL);
	g_message("devices_add_scsi_device - probing [%s]", devicenode);
	
	gchar *devices_contents = NULL, *device_str_contents = NULL;
	if(!g_file_get_contents("/proc/scsi/sg/devices", &devices_contents, NULL, NULL))
	{
		g_critical("Failed to open /proc/scsi/sg/devices");
	}
	else if(!g_file_get_contents("/proc/scsi/sg/device_strs", &device_str_contents, NULL, NULL))
	{
		g_critical("Failed to open /proc/scsi/sg/device_strs");
	}
	else
	{
		const gint scsicdromnum = atoi(&devicenode[strlen(devicenode) - 1]);
		gchar** devices = g_strsplit(devices_contents, "\n", 0);
		gchar** device_strs = g_strsplit(device_str_contents, "\n", 0);		
		gchar** device = devices;
		gchar** device_str = device_strs;
		gint cddevice = 0;
		while(*device != NULL)
		{
			if((strcmp(*device, "<no active device>") != 0) && (strlen(*device) > 0))
			{
				gint scsihost, scsiid, scsilun, scsitype;
				if(sscanf(*device, "%d\t%*d\t%d\t%d\t%d", 
					&scsihost, &scsiid, &scsilun, &scsitype) != 4)
				{
					g_critical("Error reading scsi information from /proc/scsi/sg/devices");
				}			
				/* 5 is the magic number according to lib-nautilus-burn */
				else if(scsitype == 5)
				{
					/* is the device the one we are looking for */
					if(cddevice == scsicdromnum)
					{
						gchar vendor[9], model[17];
						if(sscanf(*device_str, "%8c\t%16c", vendor, model) == 2)
						{
							vendor [8] = '\0';
							g_strstrip(vendor);
							
							model [16] = '\0'; 
							g_strstrip(model);
							
							gchar* modelname = g_strdup_printf("%s %s", vendor, model);
							gchar* deviceid = g_strdup_printf("%d,%d,%d", scsihost, scsiid, scsilun);
							
							devices_add_device(modelname, deviceid, devicenodepath);
						
							g_free(deviceid);
							g_free(modelname);
						}
					}
					++cddevice;
				}
			}
			++device_str;
			++device;
		}
		
		g_strfreev(devices);
		g_strfreev(device_strs);
	}
	
	g_free(devices_contents);
	g_free(device_str_contents);
}


gboolean
devices_probe_busses()
{
	GB_LOG_FUNC

	gboolean ok = FALSE;

	devices_clear_devicedata();	
		
#ifdef __linux__	
	
	static const gchar* const drivelabel = "drive name:\t\t";
	gchar* ptr = NULL;

	gchar* contents = NULL;
	if(!g_file_get_contents("/proc/sys/dev/cdrom/info", &contents, NULL, NULL))
	{
		g_critical("Failed to open /proc/sys/dev/cdrom/info");
	}
	else if((ptr = strstr(contents, drivelabel)) == NULL)
	{
		g_critical("Failed to find 'drive name:' in /proc/sys/dev/cdrom/info");
	}
	else
	{
		ok = TRUE;
		ptr += strlen(drivelabel);			
		gchar* lineend = strstr(ptr, "\n");
		if(lineend != NULL)
		{
			gchar* line = g_strndup(ptr, lineend - ptr);
			gchar** devices = g_strsplit_set(line, "\t ", 0);				
			gchar** device = devices;
			while((*device != NULL) && (strlen(*device) > 0))
			{
				gchar* devicenodepath = g_strdup_printf("/dev/%s", *device);
				g_message("devices_probe_busses - found device [%s]", *device);
				if((*device)[0] == 'h')
					devices_add_ide_device(*device, devicenodepath);
				else
					devices_add_scsi_device(*device, devicenodepath);						
				g_free(devicenodepath);
				++device;
			}
			
			g_strfreev(devices);				
			g_free(line);
		}
	}
			
	g_free(contents);
		
#else	

	devices_probe_bus("SCSI");	
	devices_probe_bus("ATAPI");	
	devices_probe_bus("ATA");	

#endif
		
	return ok;
}






	/* Get the kernel version so that we can figure out what to scan 
	gfloat version = 0.0;
	FILE* file = fopen("/proc/sys/kernel/osrelease", "r");
	if(file == NULL || fscanf(file, "%f.%*d-%*d-%*d", &version) != 1) 
	{
		version = 0.0;
		g_critical("Error getting the kernel version, we will now scan everything.");
	}
	fclose(file);
	
	g_message("kernel version is [%f]", version);
		
	if(version > 2.4 || version == 0.0)
	{	
		const gchar deviceletters[] = "abcdefghijklmnopqrstuvwxyz";
		gint i = 0;
		for(; i < strlen(deviceletters); i++)
		{
			gchar* devlabel = g_strdup_printf("/dev/hd%c", deviceletters[i]);
			ok = devices_probe_bus(devlabel);
			g_free(devlabel);
		}
		
		i = 0;
		for(; i <= 16; i++)
		{
			gchar* devlabel = g_strdup_printf("/dev/sg%d", i);
			ok = devices_probe_bus(devlabel);
			g_free(devlabel);
		}
	}*/
	
/*	
	devices_new_devicedata("'SAMSUNG ' 'CD-ROM SC-152L  ' 'C100' Removable CD-ROM", "1,0,0", "ATA");	
	devices_new_devicedata("'YAMAHA  ' 'CRW6416S        ' '1.0c' Removable CD-ROM", "0,0,0", NULL);
	devices_new_devicedata("'_NEC    ' 'DVD_RW ND-3500AG' '2.16' Removable CD-ROM", "1,1,0", "/dev/hdd");
	devices_new_devicedata("'SONY    ' 'CD-RW  CRX215E1 ' 'SYS2' Removable CD-ROM", "0,1,0", NULL);
	devices_new_devicedata("'ATAPI   ' 'DVD DUAL 8X4X12 ' 'B3IC' Removable CD-ROM", "1,0,0", NULL);
	'AOPEN   ' 'CD-RW CRW1232PRO' '1.00' Removable CD-ROM
*/