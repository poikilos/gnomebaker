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
 /*
 * File: filebrowser.c
 * Created by: luke_biddell@yahoo.com
 * Created on: Sun Mar 21 20:02:51 2004
 */

#include "filebrowser.h"
#include "gnomebaker.h"
#include "gbcommon.h"
#include "preferences.h"
#include <libgnomevfs/gnome-vfs-mime-utils.h>
								   

/* callback id, so we can block it! */
gulong sel_changed_id;

enum /* Directory Tree */
{
    DT_COL_ICON = 0,
    DT_COL_NAME,
    DT_NUM_COLS
};

enum /* FileList */
{
    FL_COL_ICON = 0,
    FL_COL_NAME,
	FL_COL_TYPE,
	FL_COL_SIZE,
    FL_NUM_COLS
};

enum
{
    TARGET_STRING,
    TARGET_URL
};

static GtkTargetEntry targetentries[] = 
{
    {"STRING", 0, TARGET_STRING},
    {"text/plain", 0, TARGET_STRING},
    {"text/uri-list", 0, TARGET_URL},
};


static const gchar *ROOT_LABEL = "Filesystem";
static const gchar *HOME_LABEL = "Home";
static const gchar *EMPTY_LABEL = "(empty)";


gboolean
filebrowser_on_button_pressed(GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
	GB_LOG_FUNC
	g_return_val_if_fail(widget != NULL, FALSE);

	/* look for a right click */	
	if(event->button == 3)
	{
		GtkWidget* menu = gtk_menu_new();		
		GtkTreeView* view = GTK_TREE_VIEW(widget);
				
		if(GTK_IS_TREE_STORE(gtk_tree_view_get_model(view)))
		{
			GtkWidget* menuitem = gtk_menu_item_new_with_label("Add directory");	
			g_signal_connect(menuitem, "activate",
				(GCallback)gnomebaker_on_add_dir, widget);	
			gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);
		}
		else
		{
			GtkWidget* menuitem = gtk_menu_item_new_with_label("Add file(s)");	
			g_signal_connect(menuitem, "activate",
				(GCallback)gnomebaker_on_add_files, widget);	
			gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);
		}
			
		gtk_widget_show_all(menu);
	
		/* Note: event can be NULL here when called. However,
		 *  gdk_event_get_time() accepts a NULL argument */
		gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL,
					   (event != NULL) ? event->button : 0,
					   gdk_event_get_time((GdkEvent*)event));
		return TRUE;
	}
	else
	{
		return FALSE;
	}
}


void 
filebrowser_on_show_hidden_changed(GConfClient *client,
                                   guint cnxn_id,
                                   GConfEntry *entry,
                                   gpointer user_data)
{
	GB_LOG_FUNC
	filebrowser_refresh();
}


GString* 
filebrowser_expand_path(GtkTreeModel* model, GtkTreeIter* iter)
{
	GB_LOG_FUNC
	g_return_val_if_fail(model != NULL, NULL);
	g_return_val_if_fail(iter != NULL, NULL);
	
	GValue value = { 0 };
	gtk_tree_model_get_value(model, iter, DT_COL_NAME, &value);
	const gchar *val = g_value_get_string(&value);
	g_return_val_if_fail(val != NULL, NULL);

	GString *fullpath = g_string_new("");
	g_return_val_if_fail(fullpath != NULL, NULL);

	/* Have a look at the selection value, if it's the root label then we
	   append / rather than "Filesystem" as that would make an invalid path */
	if(g_ascii_strcasecmp(val, ROOT_LABEL) == 0)
		g_string_append(fullpath, "/");
	else if(g_ascii_strcasecmp(val, HOME_LABEL) == 0)
		g_string_append(fullpath, g_get_home_dir());
	else
		g_string_append(fullpath, val);
	
	g_value_unset(&value);	
	
	GB_DECLARE_STRUCT(GtkTreeIter, parent);
	GtkTreeIter *current = gtk_tree_iter_copy(iter);
	g_return_val_if_fail(current != NULL, NULL);

	/* Now work our way up the ancestors building the path */
	while(current != NULL && gtk_tree_model_iter_parent(model, &parent, current))
	{
		GValue dir = { 0 };		
		gtk_tree_model_get_value(model, &parent, DT_COL_NAME, &dir);
		const gchar *subdir = val = g_value_get_string(&dir);
		if(subdir != NULL)		
		{
			g_string_insert(fullpath, 0, "/");
						
			/* If this isn't the "Filesystem" label then append the name */
			if(g_ascii_strcasecmp(subdir, HOME_LABEL) == 0)
				g_string_insert(fullpath, 0, g_get_home_dir());		
			else if(g_ascii_strcasecmp(subdir, ROOT_LABEL) != 0)
				g_string_insert(fullpath, 0, subdir);
		}
		else
		{
			g_critical("subdir from GValue is NULL");
		}
		
		g_value_unset(&dir);
		gtk_tree_iter_free(current);
		current = gtk_tree_iter_copy(&parent);
	}

	if(current != NULL)
		gtk_tree_iter_free(current);	
	
	/*g_message( "filebrowser_expand_path - returning [%s]", fullpath->str);*/
	
	return fullpath;
}


void 
filebrowser_populate(GtkTreeModel* treemodel,
					GtkTreeIter* iter, 
					GtkTreeView* treeview,
					GtkTreeView* fileview)
{
	GB_LOG_FUNC
	g_return_if_fail(treemodel != NULL);
	g_return_if_fail(iter != NULL);
	g_return_if_fail(treeview != NULL);
	/*g_return_if_fail(fileview != NULL);*/

	gnomebaker_show_busy_cursor(TRUE);
	
	/* Now get the full path for the selection */
	GString* fullpath = filebrowser_expand_path(treemodel, iter);
	g_return_if_fail(fullpath != NULL);	
	
	/* Get the directory tree and unref it's model so it sorts faster 		
	GtkTreeView* dirtree = gtk_tree_selection_get_tree_view(selection);
	g_object_ref(treemodel);
	gtk_tree_view_set_model(dirtree, NULL);*/
	
	/* Get the right hand file list, disconnect it from the view so it 
	   sorts quicker and clear out any existing files */
	GtkTreeModel* filemodel = NULL;
	if(fileview != NULL)
	{
		filemodel = gtk_tree_view_get_model(fileview);		
		g_object_ref(filemodel);
		gtk_tree_view_set_model(fileview, NULL);
		gtk_list_store_clear(GTK_LIST_STORE(filemodel));		
	}
	
	/* We may have already selected this directory in the tree before so
	   if the iter already has children then we wont add it to the dir tree */
	gboolean addtotree = TRUE;
	GB_DECLARE_STRUCT(GtkTreeIter, empty);
	if(gtk_tree_model_iter_nth_child(treemodel, &empty, iter, 0))
	{
		GValue value = { 0 };
		gtk_tree_model_get_value(treemodel, &empty, DT_COL_NAME, &value);
		
		if(g_ascii_strcasecmp(g_value_get_string(&value), EMPTY_LABEL) != 0)			
			addtotree = FALSE;
		g_value_unset(&value);	
	}	
	
	gboolean showhidden = preferences_get_bool(GB_SHOWHIDDEN);

	/* Now we open the directory specified by the full path */
	GError *err = NULL;
	GDir *dir = g_dir_open(fullpath->str, 0, &err);

	if(dir != NULL)
	{
		/* loop around reading the files in the directory */
		const gchar *name = g_dir_read_name(dir);			
		while(name != NULL)
		{
			/* First, check if we want to add this entry! (Hidden file/dir) */
			if (showhidden == FALSE && name[0] == '.')
			{
				name = g_dir_read_name(dir);
				continue;
			}

			/* build up the full path to the name */
			gchar* fullname = g_build_filename(fullpath->str, name, NULL);
	
			/*g_print("fullname is [%s]\n", fullname);*/
			
			GB_DECLARE_STRUCT(struct stat, s);
			if(stat(fullname, &s) == 0)
			{
				/* see if the name is actually a directory */
				if((s.st_mode & S_IFDIR))
				{
					/* It's a directory so if it doesn't already have children
					   then we add it */
					if(addtotree)
					{
						GB_DECLARE_STRUCT(GtkTreeIter, sibling);
						gtk_tree_store_insert_after
							(GTK_TREE_STORE(treemodel), &sibling, iter, NULL);

						gtk_tree_store_set(GTK_TREE_STORE(treemodel), &sibling,
							DT_COL_ICON, GTK_STOCK_OPEN, DT_COL_NAME, name, -1);

						GB_DECLARE_STRUCT(GtkTreeIter, siblingchild);
						gtk_tree_store_insert_after
							(GTK_TREE_STORE(treemodel), &siblingchild, &sibling, NULL);

						gtk_tree_store_set(GTK_TREE_STORE(treemodel), &siblingchild,
							DT_COL_ICON, "", DT_COL_NAME, EMPTY_LABEL, -1);
					}
				}
				/* It's a file */
				else if((s.st_mode & S_IFREG) && /*(name[0] != '.') && */(filemodel != NULL))
				{
#ifdef __linux__					
					/* We stored the right hand file list as user data when 
					   when we set up the directory tree selection changed func */					
					GB_DECLARE_STRUCT(GtkTreeIter, iterRight);					
					gtk_list_store_append(GTK_LIST_STORE(filemodel), &iterRight);
					gchar* mime = gnome_vfs_get_mime_type(fullname);
					gtk_list_store_set(GTK_LIST_STORE(filemodel), &iterRight, 
						FL_COL_ICON, GTK_STOCK_DND, FL_COL_NAME, name,
						FL_COL_TYPE, mime, FL_COL_SIZE, s.st_size, -1);
					g_free(mime);
#else
					/* BSD users have reported crashes here so they get a special
					version with extra debugging info so maybe I'll be able to fix it */
					GB_DECLARE_STRUCT(GtkTreeIter, iterRight);					
					gtk_list_store_append(GTK_LIST_STORE(filemodel), &iterRight);
					
					g_print("*** iter is [%s]\n", gtk_list_store_iter_is_valid(
						GTK_LIST_STORE(filemodel), &iterRight) ? "valid" : "invalid");
					
					GValue val = {0};
					g_value_init(&val, G_TYPE_STRING);
					g_value_set_string(&val, GTK_STOCK_DND);
					
					g_print("*** model [%p]\n", filemodel);
					
					gtk_list_store_set_value(GTK_LIST_STORE(filemodel), &iterRight, FL_COL_ICON, &val);					
					g_value_set_string(&val, name);
					gtk_list_store_set_value(GTK_LIST_STORE(filemodel), &iterRight, FL_COL_NAME, &val);
					
					g_print("*** name [%s]\n", name);
					
					gchar* mime = gnome_vfs_get_mime_type(fullname);
					g_value_set_string(&val, mime);
					
					gtk_list_store_set_value(GTK_LIST_STORE(filemodel), &iterRight, FL_COL_TYPE, &val);					
					
					g_print("*** mime [%s]\n", mime);					
					g_free(mime);
					
					g_value_unset(&val);
					g_value_init(&val, G_TYPE_ULONG);
					g_value_set_ulong(&val, s.st_size);
					gtk_list_store_set_value(GTK_LIST_STORE(filemodel), &iterRight, FL_COL_SIZE, &val);
					g_value_unset(&val);
					
					g_print("*** size [%ld]\n", s.st_size);
#endif					
				}
			}
			else
			{
				g_warning("Stat of file [%s] failed", fullname);
			}

			g_free(fullname);
			name = g_dir_read_name(dir);
		}

		g_dir_close(dir);
	}
	
	/* Connect the left hand dir tree view back to its model 		
	gtk_tree_view_set_model(dirtree, treemodel);
	g_object_unref(treemodel);*/

	/* Connect the right hand file list view back to its model */
	if(fileview != NULL)
	{
		gtk_tree_view_set_model(fileview, filemodel);
		g_object_unref(filemodel);
	}

	/* Finally we remove the empty child. We don't do it until
		we have add other children otherwise the tree attempts 
	    to collapse */
	if(addtotree && (empty.stamp != 0))
		gtk_tree_store_remove(GTK_TREE_STORE(treemodel), &empty);

	g_string_free(fullpath, TRUE);	
	
	gnomebaker_show_busy_cursor(FALSE);
}


void
filebrowser_sel_changed(
    GtkTreeSelection * selection,
    gpointer userdata)
{
	GB_LOG_FUNC
	g_return_if_fail(selection != NULL);
	g_return_if_fail(userdata != NULL);
	
    GtkTreeModel *treemodel = NULL;
    GB_DECLARE_STRUCT(GtkTreeIter, iter);
    /* Block the signal so that it doesn't trigger recursively (saving CPU cycles here :-))*/
    g_signal_handler_block (selection, sel_changed_id);

    /* The selection in the dir tree has changed so get that selection */
    if(gtk_tree_selection_get_selected(selection, &treemodel, &iter))
    {
		GtkTreeView* dirtree = gtk_tree_selection_get_tree_view(selection);
		filebrowser_populate(treemodel, &iter, dirtree, GTK_TREE_VIEW(userdata));		
    }
    g_signal_handler_unblock (selection, sel_changed_id);
}


void        
filebrowser_on_tree_expanding(GtkTreeView *treeview,
				GtkTreeIter *iter,
				GtkTreePath *path,
				gpointer user_data)
{
	GB_LOG_FUNC
	g_return_if_fail(treeview != NULL);
	g_return_if_fail(iter != NULL);
	
	GtkTreeModel* treemodel = gtk_tree_view_get_model(treeview);
	g_return_if_fail(treemodel != NULL);
	
	filebrowser_populate(treemodel, iter, treeview, NULL);
}


void
filebrowser_foreach_fileselection(GtkTreeModel *filemodel,
								  GtkTreePath *path,
								  GtkTreeIter *iter,
								  gpointer userdata)
{
	GB_LOG_FUNC
	g_return_if_fail(filemodel != NULL);
	g_return_if_fail(iter != NULL);
	
	GString* dragpath = (GString*)userdata;
	g_return_if_fail(dragpath != NULL);
	
	/* The drag get is for the file list so we first get the filename
	   from the list and then we get the dir tree so we can build the
	   rest of the path */	
	GValue value = { 0 };
	gtk_tree_model_get_value(filemodel, iter, FL_COL_NAME, &value);
	const gchar *val = g_value_get_string(&value);	
	
	/* now we have the filename we can get the directory tree and fully
		expand the path */
	GtkTreeView* view = (GtkTreeView*)g_object_get_data(G_OBJECT(filemodel), "dirtreeview");
	g_return_if_fail(view != NULL);	
	
	/* Now expand the path in the dir tree */
	GtkTreeModel* treemodel = NULL;
	GB_DECLARE_STRUCT(GtkTreeIter, diriter);
	gtk_tree_selection_get_selected(gtk_tree_view_get_selection(view), &treemodel, &diriter);
	GString* fullpath = filebrowser_expand_path(treemodel, &diriter);
	g_return_if_fail(fullpath != NULL);
	
	if(g_ascii_strcasecmp(fullpath->str, "/") != 0)
		g_string_append(fullpath, "/");
	
	g_string_append(fullpath, val);
	g_string_append(fullpath, "\n");
	
	g_string_append(dragpath, fullpath->str);
	
	g_string_free(fullpath, TRUE);
	g_value_unset(&value);	
}


void 
filebrowser_refresh (void)
{
	GB_LOG_FUNC
	GladeXML *xml = gnomebaker_getxml();
	g_return_if_fail(xml != NULL);
	GtkWidget *dirtree = glade_xml_get_widget(xml, widget_browser_dirtree);
	g_return_if_fail(dirtree != NULL);
	GtkWidget *filelist =  glade_xml_get_widget(xml, widget_browser_filelist);
	g_return_if_fail(filelist != NULL);
	GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(dirtree));
	g_return_if_fail(sel != NULL);
	
	g_signal_emit_by_name (sel, "changed", sel, filelist);
}


void
filebrowser_on_drag_data_get (GtkWidget * widget,
			      GdkDragContext * context,
			      GtkSelectionData * selection_data,
			      guint info, guint time, gpointer data)
{
	GB_LOG_FUNC
	g_return_if_fail(widget != NULL);
	g_return_if_fail(selection_data != NULL);
	
	GtkTreeView *view = GTK_TREE_VIEW (widget);	
	GString* file = NULL;
	
	/* This function handles the drag get for the list and the tree, so first
	   we see if the treeview's model is a list store or a tree store */
	if(GTK_IS_LIST_STORE(gtk_tree_view_get_model(view)))
	{	
		file = g_string_new("");		
		GtkTreeSelection* sel = gtk_tree_view_get_selection(view);
		gtk_tree_selection_selected_foreach(sel, filebrowser_foreach_fileselection, file);
	}
	else
	{				
		/* Expand the path in the dir tree */
		GtkTreeModel* treemodel = NULL;
		GB_DECLARE_STRUCT(GtkTreeIter, diriter);
		gtk_tree_selection_get_selected(gtk_tree_view_get_selection(view), &treemodel, &diriter);
		file = filebrowser_expand_path(treemodel, &diriter);
		g_return_if_fail(file != NULL);
	}
	
	g_message("selection data is %s\n", file->str);
	
	/* Set the fully built path(s) as the selection data */
	gtk_selection_data_set(selection_data, selection_data->target, 8, file->str,
		strlen(file->str) * sizeof(gchar));
	
	g_string_free(file, TRUE);	
}


void
filebrowser_setup_tree(
    GtkTreeView * dirtree,
    GtkTreeView * filelist)
{
	GB_LOG_FUNC
	g_return_if_fail(dirtree != NULL);
	g_return_if_fail(filelist != NULL);
	
	/* Create the tree store for the dir tree */
    GtkTreeStore *store = gtk_tree_store_new(DT_NUM_COLS, G_TYPE_STRING, G_TYPE_STRING);
    gtk_tree_view_set_model(dirtree, GTK_TREE_MODEL(store));
	gtk_tree_sortable_set_sort_column_id(
		GTK_TREE_SORTABLE(store), DT_COL_NAME, GTK_SORT_ASCENDING);
    g_object_unref(store);
		
	/* One column which has an icon renderer and a text renderer packed in */
    GtkTreeViewColumn *col = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title(col, "Filesystem");
	/*gtk_tree_view_column_set_sort_column_id(col, DT_COL_NAME);
	gtk_tree_view_column_set_sort_order(col, GTK_SORT_ASCENDING);*/
    GtkCellRenderer *renderer = gtk_cell_renderer_pixbuf_new();
    gtk_tree_view_column_pack_start(col, renderer, FALSE);
    gtk_tree_view_column_set_attributes(col, renderer, "stock-id", DT_COL_ICON, NULL);

    renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_column_pack_start(col, renderer, TRUE);
    gtk_tree_view_column_set_attributes(col, renderer, "text", DT_COL_NAME, NULL);
    gtk_tree_view_append_column(dirtree, col);
	    
	/* Enable the file list as a drag source */	
    gtk_drag_source_set(GTK_WIDGET(dirtree), GDK_BUTTON1_MASK, targetentries,
		3, GDK_ACTION_COPY | GDK_ACTION_MOVE | GDK_ACTION_LINK);

	g_signal_connect(dirtree, "drag_data_get", 
		G_CALLBACK(filebrowser_on_drag_data_get), store);

	/* Add in a root file system label as the base of our tree */
	GB_DECLARE_STRUCT(GtkTreeIter, rootiter);
    gtk_tree_store_append(store, &rootiter, NULL);
    gtk_tree_store_set(store, &rootiter, DT_COL_ICON, GTK_STOCK_HARDDISK, DT_COL_NAME, ROOT_LABEL, -1);		
	
	/* Add in a home label as the base of our tree */
	GB_DECLARE_STRUCT(GtkTreeIter, homeiter);
    gtk_tree_store_append(store, &homeiter, NULL);
	const gchar* username = g_get_user_name();
	HOME_LABEL = g_strdup_printf("%s's home", username);
    gtk_tree_store_set(store, &homeiter, DT_COL_ICON, GTK_STOCK_HOME, DT_COL_NAME, HOME_LABEL, -1);
	
		
	/* now give the right hand file list a reference to the left hand dir tree.
       We do this so when we drag a file from the right hand list we can fully
	   expand the path */
	g_object_set_data(G_OBJECT(gtk_tree_view_get_model(filelist)), "dirtreeview", dirtree);

	/* Set the selection mode of the dir tree */
	GtkTreeSelection *selection = gtk_tree_view_get_selection(dirtree);
	gtk_tree_selection_set_mode(selection, GTK_SELECTION_BROWSE);

	/* Connect up the changed signal so we can populate the file list according
	   to the selection in the dir tree */
	sel_changed_id =g_signal_connect((gpointer) selection, "changed",
		G_CALLBACK(filebrowser_sel_changed), filelist);
				
	g_signal_connect((gpointer) dirtree, "row-expanded",
		G_CALLBACK(filebrowser_on_tree_expanding), filelist);
		
	/* connect the signal to handle right click */
	g_signal_connect (G_OBJECT(dirtree), "button-press-event",
        G_CALLBACK(filebrowser_on_button_pressed), NULL);
		
	/* Force the populate of the filesystem node so that it shows an expander. We
	   must do this before we force selection of the home dir so that the list
	   correctly shows the contents of the home dir. */
	filebrowser_populate(GTK_TREE_MODEL(store), &rootiter, dirtree, filelist);
	
	/* force the selection of the home node so we initially populate the home tree and file list */
	gtk_tree_selection_select_iter(selection, &homeiter);
	GtkTreePath* path = gtk_tree_model_get_path(GTK_TREE_MODEL(store), &homeiter);
	gtk_tree_view_expand_row(dirtree, path, FALSE);
	gtk_tree_path_free(path);	
	
	
	preferences_register_notify(GB_SHOWHIDDEN, filebrowser_on_show_hidden_changed);
}


void
filebrowser_setup_list(
    GtkTreeView * filelist)
{
	GB_LOG_FUNC
	g_return_if_fail(filelist != NULL);
	
	/* Create the list store for the file list */
    GtkListStore *store = gtk_list_store_new(
		FL_NUM_COLS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_ULONG);
    gtk_tree_view_set_model(filelist, GTK_TREE_MODEL(store));
	gtk_tree_sortable_set_sort_column_id(
		GTK_TREE_SORTABLE(store), FL_COL_NAME, GTK_SORT_ASCENDING);
    g_object_unref(store);

	/* First column which has an icon renderer and a text renderer packed in */
    GtkTreeViewColumn *col = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title(col, "File");
    GtkCellRenderer *renderer = gtk_cell_renderer_pixbuf_new();
    gtk_tree_view_column_pack_start(col, renderer, FALSE);
    gtk_tree_view_column_set_attributes(col, renderer, "stock-id", FL_COL_ICON, NULL);

    renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_column_pack_start(col, renderer, TRUE);
    gtk_tree_view_column_set_attributes(col, renderer, "text", FL_COL_NAME, NULL);
    gtk_tree_view_append_column(filelist, col);
	
	/* second column to add the mime type to */
	col = gtk_tree_view_column_new();
	gtk_tree_view_column_set_title(col, "Type");
	renderer = gtk_cell_renderer_text_new();
	gtk_tree_view_column_pack_start(col, renderer, TRUE);
    gtk_tree_view_column_set_attributes(col, renderer, "text", FL_COL_TYPE, NULL);
    gtk_tree_view_append_column(filelist, col);
	
	/* third column to add the size to */
	col = gtk_tree_view_column_new();
	gtk_tree_view_column_set_title(col, "Size");
	renderer = gtk_cell_renderer_text_new();
	gtk_tree_view_column_pack_start(col, renderer, TRUE);
    gtk_tree_view_column_set_attributes(col, renderer, "text", FL_COL_SIZE, NULL);
    gtk_tree_view_append_column(filelist, col);

	/* Set the selection mode of the file list */
    gtk_tree_selection_set_mode(gtk_tree_view_get_selection(filelist),
		GTK_SELECTION_MULTIPLE);

	/* Enable the file list as a drag source */	
    gtk_drag_source_set(GTK_WIDGET(filelist), GDK_BUTTON1_MASK, targetentries,
		3, GDK_ACTION_COPY | GDK_ACTION_MOVE | GDK_ACTION_LINK);

	/* Connect the function to handle the drag data */
    g_signal_connect(filelist, "drag_data_get",
		G_CALLBACK(filebrowser_on_drag_data_get), store);
		
	/* connect the signal to handle right click */
	g_signal_connect (G_OBJECT(filelist), "button-press-event",
        G_CALLBACK(filebrowser_on_button_pressed), NULL);
}


void
filebrowser_new()
{
	GB_LOG_FUNC
	
	GtkTreeView* dirtree = 
		GTK_TREE_VIEW(glade_xml_get_widget(gnomebaker_getxml(), widget_browser_dirtree));
	GtkTreeView* filelist = 
		GTK_TREE_VIEW(glade_xml_get_widget(gnomebaker_getxml(), widget_browser_filelist));		

	g_return_if_fail(dirtree != NULL);
	g_return_if_fail(filelist != NULL);

	filebrowser_setup_list(filelist);
    filebrowser_setup_tree(dirtree, filelist);
    gtk_widget_show_all(GTK_WIDGET(dirtree));
}
