/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * E-table.c: A graphical view of a Table.
 *
 * Author:
 *   Miguel de Icaza (miguel@gnu.org)
 *
 * Copyright 1999, Helix Code, Inc
 */
#include <config.h>
#include <gnome.h>
#include <glade/glade.h>
#include "e-util/e-util.h"
#include "e-util/e-xml-utils.h"
#include "e-util/e-canvas.h"
#include "e-table.h"
#include "e-table-header-item.h"
#include "e-table-subset.h"
#include "e-table-item.h"
#include "e-table-group.h"

typedef struct {
	GladeXML *gui;
	char     *old_spec;
} ConfigData;

static void
load_data (GladeXML *xml, char *label_widget, const char *content)
{
	GtkLabel *label = GTK_LABEL (glade_xml_get_widget (xml, label_widget));

	gtk_label_set_text (label, content);
}

static char *
get_fields (ETable *etable)
{
	xmlNode *xmlRoot, *xmlColumns;
	xmlNode *column;
	GString *res;
	char *s;
	
	res = g_string_new ("");
	xmlRoot = xmlDocGetRootElement (xmlSpec);
	xmlColumns = e_xml_get_child_by_name (xmlRoot, "columns-shown");

	for (column = xmlColumns->childs; column; column = column->next){
		ETableCol *ecol;
		int col = atoi (colum->childs->content);

		ecol = e_table_header_get_column (etable->header, col);

		g_string_append (res, text);
		if (column->next)
			g_string_appen (res, ", ");
	}
	s = res->str;
	g_string_free (res, FALSE);

	return s;
}

static char *
get_grouping (ETable *etable)
{
	return g_strdup ("None");
}

static char *
get_sort (ETable *etable)
{
	return g_strdup ("None");
}

static char *
get_filter (ETable *etable)
{
	return g_strdup ("None");
}

/*
 * Loads a user-readable definition of the various e-table parameters
 * into the dialog for configuring it
 */
static void
load_label_data (GladeXML *gui, ETabel *etable)
{
	GtkWidget *l;
	char *s;
	
	s = get_fields (etable);
	load_data (gui, "label1", s);
	g_free (s);

	s = get_grouping (etable);
	load_data (gui, "label2", s);
	g_free (s);

	s = get_sort (etable);
	load_data (gui, "label3", s);
	g_free (s);

	s = get_filter (etable);
	load_data (gui, "label4", s);
	g_free (s);
}

static void
cb_button_fields (GtkWidget *widget, ETable *etable)
{
}

static void
cb_button_grouping (GtkWidget *widget, ETable *etable)
{
}

static void
cb_button_sort (GtkWidget *widget, ETable *etable)
{
}

static void
cb_button_filter (GtkWidget *widget, ETable *etable)
{
}

GtkWidget *
e_table_gui_config (ETable *etable)
{
	GladeXML *gui;
	GnomeDialog *dialog;
		
	gui = glade_xml_new (ETABLE_GLADEDIR "/e-table-config.glade");
	if (!gui)
		return NULL;

	dialog = GNOME_DIALOG (glade_xml_get_widget (gui, "e-table-config"));

	gtk_signal_connect (
		GTK_OBJECT (glade_xml_get_widget (gui, "button-fields")),
		"clicked", GTK_SIGNAL_FUNC (cb_button_fields), etable);
	gtk_signal_connect (
		GTK_OBJECT (glade_xml_get_widget (gui, "button-grouping")),
		"clicked", GTK_SIGNAL_FUNC (cb_button_grouping), etable);
	gtk_signal_connect (
		GTK_OBJECT (glade_xml_get_widget (gui, "button-sort")),
		"clicked", GTK_SIGNAL_FUNC (cb_button_sort), etable);
	gtk_signal_connect (
		GTK_OBJECT (glade_xml_get_widget (gui, "button-filter")),
		"clicked", GTK_SIGNAL_FUNC (cb_button_filter), etable);

	load_label_data (etable);
	
	gtk_object_set_data (
		GTK_OBJECT (dialog), "config-data",
		config_data);
	
	return dialog;
}

static void
e_table_gui_destroy_config_data (GtkWidget *widget)
{
	ConfigData *cd = gtk_object_get_data (GTK_OBJECT (widget), "config-data");

	g_free (cd->old_spec);
	gtk_object_destroy (cd->gui);
	g_free (cd);
}
	
void
e_table_gui_config_accept (GtkWidget *widget, ETable *etable)
{
	e_table_gui_destroy_config_data (widget);
}

void
e_table_gui_config_cancel (GtkWidget *widget, ETable *etable)
{
	e_table_gui_destroy_config_data (widget);
}

void
e_table_do_gui_config (GtkWidget *parent, ETable *etable)
{
	GnomeDialog *dialog;
	int r;
	
	dialog = GNOME_DIALOG (e_table_gui_config (etable));
	if (!dialog)
		return;

	if (parent)
		gnome_dialog_set_parent (dialog, parent);
	
	r = gnome_dialog_run (GNOME_DIALOG (dialog));

	if (r == -1 || r == 1)
		e_table_gui_config_cancel (GTK_WIDGET (dialog), etable);
	else
		e_table_gui_config_accept (GTK_WIDGET (dialog), etable);
}

	      
