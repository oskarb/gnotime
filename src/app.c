/*   GTimeTracker - a time tracker
 *   Copyright (C) 1997,98 Eckehard Berns
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include "config.h"

#include <errno.h>
#include <gnome.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "ctree.h"
#include "ctree-gnome2.h"
#include "cur-proj.h"
#include "gtt.h"
#include "log.h"
#include "menucmd.h"
#include "menus.h"
#include "notes-area.h"
#include "prefs.h"
#include "props-proj.h"
#include "timer.h"
#include "toolbar.h"
#include "util.h"


GttProject *cur_proj = NULL;

ProjTreeWindow *global_ptw;
GtkWidget *window;
GtkWidget *status_bar;

static GtkStatusbar *status_project = NULL;
static GtkStatusbar *status_day_time = NULL;
static GtkWidget *status_timer = NULL;

char *config_shell_start = NULL;
char *config_shell_stop = NULL;

gboolean geom_place_override = FALSE;
gboolean geom_size_override = FALSE;

/* ============================================================= */

void 
update_status_bar(void)
{
	char day_total_str[25];
	static char *old_day_time = NULL;
	static char *old_project = NULL;
	char *s;

	if (!status_bar) return;

	/* Make the little clock item appear/disappear
	 * when the project is started/stopped */
	if (status_timer) 
	{
		if (timer_is_running())
			gtk_widget_show(status_timer);
		else
			gtk_widget_hide(status_timer);
	}
	
	if (!old_day_time) old_day_time = g_strdup("");
	if (!old_project) old_project = g_strdup("");

	/* update timestamp */
	print_hours_elapsed (day_total_str, 25, 
	         gtt_project_list_total_secs_day(), config_show_secs);

	s = g_strdup(day_total_str);
	if (0 != strcmp(s, old_day_time)) 
	{
	   gtk_statusbar_pop(status_day_time, 0);
		gtk_statusbar_push(status_day_time, 0, s);
		g_free(old_day_time);
		old_day_time = s;
	} 
	else 
	{
		g_free(s);
	}
	
	/* Display the project title */
	if (cur_proj) 
	{
		s = g_strdup_printf ("%s - %s", 
		gtt_project_get_title(cur_proj),
		gtt_project_get_desc(cur_proj));
	} 
	else 
	{
		s = g_strdup(_("Timer is not running"));
	}

	if (0 != strcmp(s, old_project)) 
	{
		gtk_statusbar_pop(status_project, 0);
		gtk_statusbar_push(status_project, 0, s);
		g_free(old_project);
		old_project = s;
	} 
	else 
	{
		g_free(s);
	}
}


/* ============================================================= */
/* Handle shell commands */

static void
run_shell_command (GttProject *proj, gboolean do_start)
{
	char *cmd;
	const char *str;
	pid_t pid;

	cmd = (do_start) ? config_shell_start : config_shell_stop;

	if (!cmd) return;

	/* Sometimes, we are called to stop a NULL project.
	 * We don't really want that (its a result be being called twice).
	 */ 
	if (!proj) return;
	
	str = printf_project (cmd, proj);
	pid = fork();
	if (pid == 0) 
	{
		execlp("sh", "sh", "-c", str, NULL);
		g_warning("%s: %d: cur_proj_set: couldn't exec\n", __FILE__, __LINE__);
		exit(1);
	}
	if (pid < 0) 
	{
		g_warning("%s: %d: cur_proj_set: couldn't fork\n", __FILE__, __LINE__);
	}
	g_free ((gchar *) str);

	/* Note that the forked processes might be scheduled by the operating
	 * system 'out of order', if we've made rapid successive calls to this
	 * routine.  So we try to ensure in-order execution by trying to let
	 * the child process at least start running.  And we can do this by
	 * yielding our time-slice ... */
	sched_yield();
}

/* ============================================================= */

void 
cur_proj_set(GttProject *proj)
{
	/* Due to the way the widget callbacks work, 
	 * we may be called recursively ... */
	if (cur_proj == proj) return;

	log_proj(NULL);
	gtt_project_timer_stop (cur_proj);
	run_shell_command (cur_proj, FALSE);
	
	if (proj) 
	{
		cur_proj = proj;
		gtt_project_timer_start (proj); 
		run_shell_command (cur_proj, TRUE);
	}
	else
	{
		cur_proj = NULL;
	}
	log_proj(proj);

	/* update GUI elements */
	menu_set_states();
	if (proj) prop_dialog_set_project(proj);
	update_status_bar();
}


/* ============================================================= */

void app_new(int argc, char *argv[], const char *geometry_string)
{
	GtkWidget *ctree;
	GtkWidget *vbox;
	GtkWidget *widget;
	gint x, y, w, h;

	window = gnome_app_new(GTT_APP_NAME, GTT_APP_TITLE " " VERSION);
	gtk_window_set_wmclass(GTK_WINDOW(window),
			       GTT_APP_NAME, GTT_APP_PROPER_NAME);

	/* 485 x 272 seems to be a good size to default to */
	gtk_window_set_default_size(GTK_WINDOW(window), 485, 272);
	gtk_window_set_policy(GTK_WINDOW(window), TRUE, TRUE, FALSE);
	
	/* build menus */
	menus_create(GNOME_APP(window));

	/* build toolbar */
	widget = build_toolbar();
	gtk_widget_show(widget);
	gnome_app_set_toolbar(GNOME_APP(window), GTK_TOOLBAR(widget));
	
	/* container holds status bar, main ctree widget */
	vbox = gtk_vbox_new(FALSE, 0);
	
	/* build statusbar */
	status_bar = gtk_hbox_new(FALSE, 0);
	gtk_box_set_homogeneous (GTK_BOX(status_bar), FALSE);
	gtk_widget_show(status_bar);
	gtk_box_pack_end(GTK_BOX(vbox), status_bar, FALSE, FALSE, 2);
	
	/* put elapsed time into statusbar */
	status_day_time = GTK_STATUSBAR(gtk_statusbar_new());
	gtk_statusbar_set_has_resize_grip (status_day_time, FALSE);
	gtk_widget_show(GTK_WIDGET(status_day_time));
	gtk_statusbar_push(status_day_time, 0, _("00:00"));

	/* XXX hack alert: the timer box is not being correctly sized;
	 * I suspect this is either a gtk bug or a usage bug.
	 * So instead I set the width to 50 pixels, and hope the 
	 * font size can live with this. */
	gtk_widget_set_size_request (GTK_WIDGET(status_day_time), 50, -1);
	gtk_box_pack_start(GTK_BOX(status_bar), GTK_WIDGET(status_day_time),
			   FALSE, TRUE, 1);
	
	/* put project name into statusbar */
	status_project = GTK_STATUSBAR(gtk_statusbar_new());
	gtk_widget_show(GTK_WIDGET(status_project));
	
	gtk_statusbar_push(status_project, 0, _("Timer is not running"));
	gtk_box_pack_start(GTK_BOX(status_bar), GTK_WIDGET(status_project),
			   TRUE, TRUE, 1);

	/* put timer icon into statusbar */
	status_timer = gtk_image_new_from_stock (GNOME_STOCK_TIMER, 
			GTK_ICON_SIZE_MENU);
	gtk_widget_show(status_timer);
	gtk_box_pack_end(GTK_BOX(status_bar), GTK_WIDGET(status_timer),
			 FALSE, FALSE, 1);

	/* create the main columned tree for showing projects */
	global_ptw = ctree_new();
	ctree = ctree_get_widget(global_ptw);
	gtk_box_pack_end(GTK_BOX(vbox), ctree->parent, TRUE, TRUE, 0);

#ifdef LATER
	/* create the notes area */
	// XXXX
	notes_area_init();
	{ GtkWidget *nar = notes_area_get_widget ();
       gtk_widget_reparent (nar, vbox);  // ???
	    gtk_box_pack_end(GTK_BOX(vbox), nar, TRUE, TRUE, 0);
	}
#endif
	

	/* we are done building it, make it visible */
	gtk_widget_show(vbox);
	gnome_app_set_contents(GNOME_APP(window), vbox);

	if (!geometry_string) return;
	
	if (gtk_window_parse_geometry(GTK_WINDOW(window),geometry_string))
	{
		geom_size_override=TRUE;
	}
	else 
	{
		gnome_app_error(GNOME_APP(window),
			_("Couldn't understand geometry (position and size)\n"
			  " specified on command line"));
	}
}

void 
app_show (void)
{
	if (!GTK_WIDGET_MAPPED(window)) 
	{
		gtk_widget_show(window);
	}
}



void
app_quit(GtkWidget *w, gpointer data)
{
	save_properties ();
	save_projects ();

	gtk_main_quit();
}

/* ============================== END OF FILE ===================== */
