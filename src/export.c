/*   GTimeTracker - a time tracker
 *   Copyright (C) 1997,98 Eckehard Berns
 *   Copyright (C) 2001,2002 Linas Vepstas <linas@linas.org>
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

/*
#include <config.h>
#include <gnome.h>
#include <string.h>

#include "app.h"
#include "ctree.h"
#include "cur-proj.h"
#include "dialog.h"
#include "err-throw.h"
#include "file-io.h"
#include "gtt.h"
#include "menucmd.h"
#include "menus.h"
#include "prefs.h"
#include "proj.h"
#include "props-proj.h"
#include "timer.h"
#include "xml-gtt.h"
*/

/* Project data export */

/* ======================================================= */

typedef struct export_format_s export_format_t;

struct export_format_s 
{
	GtkFileSelection *picker;    /* URI picker (file selection) */
	const char       *uri;       /* aka filename */
	FILE             *fp;        /* file handle */
};

static export_format_t *
export_format_new (void)
{
	export_format_t * rc;
	rc = g_new (export_format_t, 1);
	rc->picker = NULL;
	rc->uri = NULL;
	return rc;
}

/* ======================================================= */

static char *
get_time (int secs)
{
	/* Translators: This is a "time format", that is
	 * format on how to print the elapsed time with
	 * hours:minutes:seconds. */
	return g_strdup_printf (_("%d:%02d:%02d"),
				secs / (60*60),
				(secs / 60) % 60,
				secs % 60);

}

/* ======================================================= */
/* The default format for this exporter is tab-delimited;
 * however, by adding an export type to the export format, 
 * one could support lots of different formats.
 */

static gint
export_projects (export_format_t *xp)
{
	GList *node;


	/* XXX default tab delimited */
	/* Translators: this is the header of a table separated file,
	 * it should really be all ASCII, or at least not multibyte,
	 * I don't think most spreadsheets would handle that well. */
	fprintf (xp->fp, "Title\tDescription\tTotal time\tTime today\n");

	for (node = gtt_get_project_list(); node; node = node->next) 
	{
		GttProject *prj = node->data;
		char *total_time, *time_today;
		if (!gtt_project_get_title(prj)) continue;
		total_time = get_time (gtt_project_total_secs_ever(prj));
		time_today = get_time (gtt_project_total_secs_day(prj));
		fprintf (xp->fp, "%s\t%s\t%s\t%s\n",
			 gtt_sure_string (gtt_project_get_title(prj)),
			 gtt_sure_string (gtt_project_get_desc(prj)),
			 total_time,
			 time_today);
		g_free (total_time);
		g_free (time_today);
	}

	return 0;
}

/* ======================================================= */

static void
export_really (GtkWidget *widget, export_format_t *xp)
{
	gboolean rc;

	xp->uri = gtk_file_selection_get_filename (xp->picker);

	if (0 == access (xp->uri, F_OK)) 
	{
		GtkWidget *w;
		char *s;

		s = g_strdup_printf (_("File %s exists, overwrite?"),
				     filename);
		w = gnome_question_dialog_parented (s, NULL, NULL,
						    GTK_WINDOW (fsel));
		g_free (s);

		if (gnome_dialog_run (GNOME_DIALOG (w)) != 0)
			return;
	}

	xp->fp = fopen (xp->uri, "w");
	if (NULL == xp->fp)
	{
		GtkWidget *w = gnome_error_dialog (_("File could not be opened"));
		gnome_dialog_set_parent (GNOME_DIALOG (w), GTK_WINDOW (xp->picker));
		return;
	}
	
	rc = export_projects (xp);
	if (rc)
	{
		GtkWidget *w = gnome_error_dialog (_("Error occured during export"));
		gnome_dialog_set_parent (GNOME_DIALOG (w), GTK_WINDOW (xp->picker));
		return;
	}

	fclose (xp->fp);
	gtk_widget_destroy (GTK_WIDGET (xp->picker));
}

/* ======================================================= */

void
export_file_picker (GtkWidget *widget, gpointer data)
{
	static export_format_t *xp = NULL;
	GtkWidget *dialog;

	if (xp && xp->picker) 
	{
		dialog = GTK_WIDGET(xp->picker);
		gtk_widget_show_now (dialog);
		gdk_window_raise (dialog->window);
		return;
	}

	dialog = gtk_file_selection_new (_("Tab-Delimited Export"));
	gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (window));

	xp = export_format_new ();
	xp->picker = GTK_FILE_SELECTION (dialog);

	gtk_signal_connect (GTK_OBJECT (dialog), "destroy",
			    GTK_SIGNAL_FUNC (gtk_widget_destroyed),
			    &dialog);

	/* XXX is destroy really the right thing here ?? */
	gtk_signal_connect_object (GTK_OBJECT (xp->picker->cancel_button), "clicked",
				   GTK_SIGNAL_FUNC (gtk_widget_destroy),
				   GTK_OBJECT (xp->picker));

	gtk_signal_connect (GTK_OBJECT (xp->picker->ok_button), "clicked",
			    GTK_SIGNAL_FUNC (export_really),
			    xp);

	gtk_widget_show (dialog);
}

/* ======================= END OF FILE ======================= */