/* Evolution calendar - Main page of the task editor dialog
 *
 * Copyright (C) 2001 Ximian, Inc.
 *
 * Authors: Federico Mena-Quintero <federico@ximian.com>
 *          Miguel de Icaza <miguel@ximian.com>
 *          Seth Alves <alves@hungry.com>
 *          JP Rosevear <jpr@ximian.com>
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib.h>
#include <liboaf/liboaf.h>
#include <bonobo/bonobo-control.h>
#include <bonobo/bonobo-widget.h>
#include <bonobo/bonobo-exception.h>
#include <gtk/gtksignal.h>
#include <gtk/gtktogglebutton.h>
#include <gtk/gtkvbox.h>
#include <gtk/gtkwindow.h>
#include <libgnome/gnome-defs.h>
#include <libgnome/gnome-i18n.h>
#include <libgnomeui/gnome-stock.h>
#include <libgnomeui/gnome-dialog-util.h>
#include <glade/glade.h>
#include <gal/e-table/e-cell-combo.h>
#include <gal/e-table/e-cell-text.h>
#include <gal/e-table/e-table-simple.h>
#include <gal/e-table/e-table-scrolled.h>
#include <gal/widgets/e-unicode.h>
#include <gal/widgets/e-popup-menu.h>
#include <gal/widgets/e-gui-utils.h>
#include <widgets/misc/e-dateedit.h>
#include <e-util/e-dialog-widgets.h>
#include <e-destination.h>
#include "Evolution-Addressbook-SelectNames.h"
#include "../component-factory.h"
#include "../e-meeting-attendee.h"
#include "../e-meeting-model.h"
#include "../itip-utils.h"
#include "comp-editor-util.h"
#include "e-delegate-dialog.h"
#include "meeting-page.h"


#define SELECT_NAMES_OAFID "OAFIID:GNOME_Evolution_Addressbook_SelectNames"

enum columns {
	MEETING_ATTENDEE_COL,
	MEETING_MEMBER_COL,
	MEETING_TYPE_COL,
	MEETING_ROLE_COL,
	MEETING_RSVP_COL,
	MEETING_DELTO_COL,
	MEETING_DELFROM_COL,
	MEETING_STATUS_COL,
	MEETING_CN_COL,
	MEETING_LANG_COL,
	MEETING_COLUMN_COUNT
};

/* Private part of the MeetingPage structure */
struct _MeetingPagePrivate {
	/* Lists of attendees */
	GPtrArray *deleted_attendees;

	/* To use in case of cancellation */
	CalComponent *comp;
	
	/* List of identities */
	GList *addresses;
	GList *address_strings;
	gchar *default_address;
	
	/* Glade XML data */
	GladeXML *xml;

	/* Widgets from the Glade file */
	GtkWidget *main;
	GtkWidget *organizer_table;
	GtkWidget *organizer;
	GtkWidget *organizer_lbl;
	GtkWidget *other_organizer;
	GtkWidget *other_organizer_lbl;
	GtkWidget *other_organizer_btn;
	GtkWidget *existing_organizer_table;
	GtkWidget *existing_organizer;
	GtkWidget *existing_organizer_btn;
	GtkWidget *invite;
	
	/* E Table stuff */
	EMeetingModel *model;
	ETableScrolled *etable;
	gint row;
	
	/* For handling who the organizer is */
	gboolean other;
	gboolean existing;
        gboolean updating;
	
	/* For handling the invite button */
        GNOME_Evolution_Addressbook_SelectNames corba_select_names;
};



static void meeting_page_class_init (MeetingPageClass *class);
static void meeting_page_init (MeetingPage *mpage);
static void meeting_page_destroy (GtkObject *object);

static GtkWidget *meeting_page_get_widget (CompEditorPage *page);
static void meeting_page_focus_main_widget (CompEditorPage *page);
static void meeting_page_fill_widgets (CompEditorPage *page, CalComponent *comp);
static void meeting_page_fill_component (CompEditorPage *page, CalComponent *comp);

static gint right_click_cb (ETable *etable, gint row, gint col, GdkEvent *event, gpointer data);

static CompEditorPageClass *parent_class = NULL;



/**
 * meeting_page_get_type:
 * 
 * Registers the #MeetingPage class if necessary, and returns the type ID
 * associated to it.
 * 
 * Return value: The type ID of the #MeetingPage class.
 **/
GtkType
meeting_page_get_type (void)
{
	static GtkType meeting_page_type;

	if (!meeting_page_type) {
		static const GtkTypeInfo meeting_page_info = {
			"MeetingPage",
			sizeof (MeetingPage),
			sizeof (MeetingPageClass),
			(GtkClassInitFunc) meeting_page_class_init,
			(GtkObjectInitFunc) meeting_page_init,
			NULL, /* reserved_1 */
			NULL, /* reserved_2 */
			(GtkClassInitFunc) NULL
		};

		meeting_page_type = 
			gtk_type_unique (TYPE_COMP_EDITOR_PAGE,
					 &meeting_page_info);
	}

	return meeting_page_type;
}

/* Class initialization function for the task page */
static void
meeting_page_class_init (MeetingPageClass *class)
{
	CompEditorPageClass *editor_page_class;
	GtkObjectClass *object_class;

	editor_page_class = (CompEditorPageClass *) class;
	object_class = (GtkObjectClass *) class;

	parent_class = gtk_type_class (TYPE_COMP_EDITOR_PAGE);

	editor_page_class->get_widget = meeting_page_get_widget;
	editor_page_class->focus_main_widget = meeting_page_focus_main_widget;
	editor_page_class->fill_widgets = meeting_page_fill_widgets;
	editor_page_class->fill_component = meeting_page_fill_component;
	editor_page_class->set_summary = NULL;
	editor_page_class->set_dates = NULL;

	object_class->destroy = meeting_page_destroy;
}

/* Object initialization function for the task page */
static void
meeting_page_init (MeetingPage *mpage)
{
	MeetingPagePrivate *priv;

	priv = g_new0 (MeetingPagePrivate, 1);
	mpage->priv = priv;

	priv->deleted_attendees = g_ptr_array_new ();

	priv->comp = NULL;
	
	priv->xml = NULL;
	priv->main = NULL;
	priv->invite = NULL;
	
	priv->model = NULL;
	priv->etable = NULL;
	
	priv->updating = FALSE;
}

static void
set_attendees (CalComponent *comp, const GPtrArray *attendees)
{
	GSList *comp_attendees = NULL, *l;
	int i;
	
	for (i = 0; i < attendees->len; i++) {
		EMeetingAttendee *ia = g_ptr_array_index (attendees, i);
		CalComponentAttendee *ca;
		
		ca = e_meeting_attendee_as_cal_component_attendee (ia);
		
		comp_attendees = g_slist_prepend (comp_attendees, ca);
		
	}
	comp_attendees = g_slist_reverse (comp_attendees);
	cal_component_set_attendee_list (comp, comp_attendees);
	
	for (l = comp_attendees; l != NULL; l = l->next)
		g_free (l->data);	
	g_slist_free (comp_attendees);
}

static void
cleanup_attendees (GPtrArray *attendees)
{
	int i;
	
	for (i = 0; i < attendees->len; i++)
		gtk_object_unref (GTK_OBJECT (g_ptr_array_index (attendees, i)));
}

/* Destroy handler for the task page */
static void
meeting_page_destroy (GtkObject *object)
{
	MeetingPage *mpage;
	MeetingPagePrivate *priv;
	ETable *real_table;
	char *filename;
	
	g_return_if_fail (object != NULL);
	g_return_if_fail (IS_MEETING_PAGE (object));

	mpage = MEETING_PAGE (object);
	priv = mpage->priv;

	if (priv->comp != NULL)
		gtk_object_unref (GTK_OBJECT (priv->comp));
	
	cleanup_attendees (priv->deleted_attendees);
	g_ptr_array_free (priv->deleted_attendees, FALSE);
	
	itip_addresses_free (priv->addresses);
	g_list_free (priv->address_strings);

	gtk_object_unref (GTK_OBJECT (priv->model));

	/* Save state */
	filename = g_strdup_printf ("%s/config/et-header-meeting-page", 
				    evolution_dir);
	real_table = e_table_scrolled_get_table (priv->etable);
	e_table_save_state (real_table, filename);
	g_free (filename);
	
	if (priv->xml) {
		gtk_object_unref (GTK_OBJECT (priv->xml));
		priv->xml = NULL;
	}

	g_free (priv);
	mpage->priv = NULL;

	if (GTK_OBJECT_CLASS (parent_class)->destroy)
		(* GTK_OBJECT_CLASS (parent_class)->destroy) (object);
}



/* get_widget handler for the task page */
static GtkWidget *
meeting_page_get_widget (CompEditorPage *page)
{
	MeetingPage *mpage;
	MeetingPagePrivate *priv;

	mpage = MEETING_PAGE (page);
	priv = mpage->priv;

	return priv->main;
}

/* focus_main_widget handler for the task page */
static void
meeting_page_focus_main_widget (CompEditorPage *page)
{
	MeetingPage *mpage;
	MeetingPagePrivate *priv;

	mpage = MEETING_PAGE (page);
	priv = mpage->priv;

	gtk_widget_grab_focus (priv->organizer);
}

/* Fills the widgets with default values */
static void
clear_widgets (MeetingPage *mpage)
{
	MeetingPagePrivate *priv;

	priv = mpage->priv;

	gtk_entry_set_text (GTK_ENTRY (GTK_COMBO (priv->organizer)->entry), "");
	gtk_entry_set_text (GTK_ENTRY (priv->other_organizer), "");
	gtk_label_set_text (GTK_LABEL (priv->existing_organizer), _("None"));

	gtk_widget_show (priv->organizer_table);
	gtk_widget_hide (priv->existing_organizer_table);	

	gtk_widget_hide (priv->other_organizer_lbl);
	gtk_widget_hide (priv->other_organizer);

	priv->existing = FALSE;
	priv->other = FALSE;
}

/* fill_widgets handler for the meeting page */
static void
meeting_page_fill_widgets (CompEditorPage *page, CalComponent *comp)
{
	MeetingPage *mpage;
	MeetingPagePrivate *priv;
	CalComponentOrganizer organizer;
	GList *l;
	
	mpage = MEETING_PAGE (page);
	priv = mpage->priv;

	priv->updating = TRUE;
	
	/* Clean out old data */
	if (priv->comp != NULL)
		gtk_object_unref (GTK_OBJECT (priv->comp));
	priv->comp = NULL;
	
	cleanup_attendees (priv->deleted_attendees);

	/* Clean the screen */
	clear_widgets (mpage);

	/* Component for cancellation */
	priv->comp = cal_component_clone (comp);
	
	/* Organizer */
	cal_component_get_organizer (comp, &organizer);
	priv->addresses = itip_addresses_get ();
	for (l = priv->addresses; l != NULL; l = l->next) {
		ItipAddress *a = l->data;
		
		priv->address_strings = g_list_append (priv->address_strings, a->full);
		if (a->default_address)
			priv->default_address = a->full;
	}
	gtk_combo_set_popdown_strings (GTK_COMBO (priv->organizer), priv->address_strings);

	if (organizer.value != NULL) {
		const gchar *strip = itip_strip_mailto (organizer.value);
		gchar *s, *string;

		gtk_widget_hide (priv->organizer_table);
		gtk_widget_show (priv->existing_organizer_table);
		gtk_widget_hide (priv->invite);
		
		if (organizer.cn != NULL)
			string = g_strdup_printf ("%s <%s>", organizer.cn, strip);
		else
			string = g_strdup (strip);
		s = e_utf8_to_gtk_string (priv->existing_organizer, string);
		gtk_label_set_text (GTK_LABEL (priv->existing_organizer), s);
		g_free (s);
		g_free (string);

		priv->existing = TRUE;
	} else {
		gtk_widget_hide (priv->other_organizer_lbl);
		gtk_widget_hide (priv->other_organizer);

		e_dialog_editable_set (GTK_COMBO (priv->organizer)->entry, priv->default_address);
	}

	priv->updating = FALSE;
}

/* fill_component handler for the meeting page */
static void
meeting_page_fill_component (CompEditorPage *page, CalComponent *comp)
{
	MeetingPage *mpage;
	MeetingPagePrivate *priv;
	CalComponentOrganizer organizer = {NULL, NULL, NULL, NULL};
	
	mpage = MEETING_PAGE (page);
	priv = mpage->priv;

	if (!priv->existing) {
		gchar *addr = NULL, *cn = NULL;
		GList *l;
		
		if (priv->other) {
			addr = e_dialog_editable_get (priv->other_organizer);
		} else {
			gchar *str = e_dialog_editable_get (GTK_COMBO (priv->organizer)->entry);
			for (l = priv->addresses; l != NULL; l = l->next) {
				ItipAddress *a = l->data;
				
				if (!strcmp (a->full, str)) {
					addr = g_strdup (a->address);
					cn = g_strdup (a->name);
				}
			}
			g_free (str);
		}
		
		if (addr == NULL || strlen (addr) == 0) {		
			g_free (addr);
			g_free (cn);		
			return;
		} else {
			gchar *tmp;
			
			tmp = addr;
			addr = g_strdup_printf ("MAILTO:%s", addr);
			g_free (tmp);
		}
	
		organizer.value = addr;
		organizer.cn = cn;
		cal_component_set_organizer (comp, &organizer);
		g_free (addr);
		g_free (cn);
	}

	set_attendees (comp, e_meeting_model_get_attendees (priv->model));
}



/* Gets the widgets from the XML file and returns if they are all available. */
static gboolean
get_widgets (MeetingPage *mpage)
{
	MeetingPagePrivate *priv;

	priv = mpage->priv;

#define GW(name) glade_xml_get_widget (priv->xml, name)

	priv->main = GW ("meeting-page");
	if (!priv->main)
		return FALSE;

	gtk_widget_ref (priv->main);
	gtk_widget_unparent (priv->main);

	priv->organizer_table = GW ("organizer-table");
	priv->organizer = GW ("organizer");
	priv->organizer_lbl = GW ("organizer-label");
	priv->other_organizer = GW ("other-organizer");
	priv->other_organizer_lbl = GW ("other-organizer-label");
	priv->other_organizer_btn = GW ("other-organizer-button");
	priv->existing_organizer_table = GW ("existing-organizer-table");
	priv->existing_organizer = GW ("existing-organizer");
	priv->existing_organizer_btn = GW ("existing-organizer-button");
	priv->invite = GW ("invite");
	
#undef GW

	return (priv->invite
		&& priv->organizer_table
		&& priv->organizer
		&& priv->organizer_lbl
		&& priv->other_organizer
		&& priv->other_organizer_lbl
		&& priv->other_organizer_btn
		&& priv->existing_organizer_table
		&& priv->existing_organizer
		&& priv->existing_organizer_btn);
}

static void
duplicate_error (void)
{
	GtkWidget *dlg = gnome_error_dialog (_("That person is already attending the meeting!"));
	gnome_dialog_run_and_close (GNOME_DIALOG (dlg));
}

static void
invite_entry_changed (BonoboListener    *listener,
		      char              *event_name,
		      CORBA_any         *arg,
		      CORBA_Environment *ev,
		      gpointer           data)
{
	MeetingPage *mpage = data;
	MeetingPagePrivate *priv;
	Bonobo_Control corba_control;
	GtkWidget *control_widget;
	EDestination **destv;
	char *string = NULL, *section;
	int i;
	
	priv = mpage->priv;

	section = BONOBO_ARG_GET_STRING (arg);
	
	g_message ("event: \"%s\", section \"%s\"", event_name, section);

	corba_control = GNOME_Evolution_Addressbook_SelectNames_getEntryBySection (priv->corba_select_names, section, ev);
	control_widget = bonobo_widget_new_control_from_objref (corba_control, CORBA_OBJECT_NIL);

	bonobo_widget_get_property (BONOBO_WIDGET (control_widget), "destinations", &string, NULL);
	destv = e_destination_importv (string);
	if (destv == NULL)
		return;
	
	for (i = 0; destv[i] != NULL; i++) {
		EMeetingAttendee *ia;
		const char *name, *address;
		
		name = e_destination_get_name (destv[i]);		
		address = e_destination_get_email (destv[i]);
		
		if (e_meeting_model_find_attendee (priv->model, address, NULL) == NULL) {
			ia = e_meeting_model_add_attendee_with_defaults (priv->model);

			e_meeting_attendee_set_address (ia, g_strdup_printf ("MAILTO:%s", address));
			if (!strcmp (section, _("Chair Persons")))
				e_meeting_attendee_set_role (ia, ICAL_ROLE_CHAIR);
			else if (!strcmp (section, _("Required Participants")))
				e_meeting_attendee_set_role (ia, ICAL_ROLE_REQPARTICIPANT);
			else if (!strcmp (section, _("Optional Participants")))
				e_meeting_attendee_set_role (ia, ICAL_ROLE_OPTPARTICIPANT);
			else if (!strcmp (section, _("Non-Participants")))
				e_meeting_attendee_set_role (ia, ICAL_ROLE_NONPARTICIPANT);
			e_meeting_attendee_set_cn (ia, g_strdup (name));
		}
	}
	e_destination_freev (destv);
}

static void
add_section (GNOME_Evolution_Addressbook_SelectNames corba_select_names, const char *name, int limit)
{
	CORBA_Environment ev;

	CORBA_exception_init (&ev);

	if (limit != 0)
		GNOME_Evolution_Addressbook_SelectNames_addSectionWithLimit (corba_select_names,
									     name, name, limit, &ev);
	else
		GNOME_Evolution_Addressbook_SelectNames_addSection (corba_select_names,
								    name, name, &ev);

	CORBA_exception_free (&ev);
}

static gboolean
get_select_name_dialog (MeetingPage *mpage) 
{
	MeetingPagePrivate *priv;
	const char *sections[] = {_("Chair Persons"), _("Required Participants"), _("Optional Participants"), _("Non-Participants")};
	CORBA_Environment ev;
	
	priv = mpage->priv;

	if (priv->corba_select_names != CORBA_OBJECT_NIL) {
		Bonobo_Control corba_control;
		GtkWidget *control_widget;
		int i;
		
		CORBA_exception_init (&ev);
		for (i = 0; i < 4; i++) {			
			corba_control = GNOME_Evolution_Addressbook_SelectNames_getEntryBySection (priv->corba_select_names, sections[i], &ev);
			if (BONOBO_EX (&ev)) {
				CORBA_exception_free (&ev);
				return FALSE;				
			}
			
			control_widget = bonobo_widget_new_control_from_objref (corba_control, CORBA_OBJECT_NIL);
			
			bonobo_widget_set_property (BONOBO_WIDGET (control_widget), "text", "", NULL);		
		}
		CORBA_exception_free (&ev);

		return TRUE;
	}
	
	CORBA_exception_init (&ev);

	priv->corba_select_names = oaf_activate_from_id (SELECT_NAMES_OAFID, 0, NULL, &ev);

	add_section (priv->corba_select_names, sections[0], 0);
	add_section (priv->corba_select_names, sections[1], 0);
	add_section (priv->corba_select_names, sections[2], 0);
	add_section (priv->corba_select_names, sections[3], 0);

	bonobo_event_source_client_add_listener (priv->corba_select_names,
						 invite_entry_changed,
						 "GNOME/Evolution:changed:model",
						 NULL, mpage);
	
	if (BONOBO_EX (&ev)) {
		CORBA_exception_free (&ev);
		return FALSE;
	}

	CORBA_exception_free (&ev);

	return TRUE;
}

/* This is called when any field is changed; it notifies upstream. */
static void
field_changed_cb (GtkWidget *widget, gpointer data)
{
	MeetingPage *mpage;
	MeetingPagePrivate *priv;
	
	mpage = MEETING_PAGE (data);
	priv = mpage->priv;
	
	if (!priv->updating)
		comp_editor_page_notify_changed (COMP_EDITOR_PAGE (mpage));
}

/* Function called to make the organizer other than the user */
static void
other_clicked_cb (GtkWidget *widget, gpointer data) 
{
	MeetingPage *mpage;
	MeetingPagePrivate *priv;
	
	mpage = MEETING_PAGE (data);
	priv = mpage->priv;

	gtk_widget_hide (priv->organizer_lbl);
	gtk_widget_hide (priv->organizer);
	gtk_widget_hide (priv->other_organizer_btn);
	gtk_widget_show (priv->other_organizer_lbl);
	gtk_widget_show (priv->other_organizer);

	priv->other = TRUE;
}

/* Function called to change the organizer */
static void
change_clicked_cb (GtkWidget *widget, gpointer data) 
{
	MeetingPage *mpage;
	MeetingPagePrivate *priv;
	
	mpage = MEETING_PAGE (data);
	priv = mpage->priv;

	gtk_widget_show (priv->organizer_table);
	gtk_widget_hide (priv->existing_organizer_table);
	gtk_widget_show (priv->invite);

	gtk_combo_set_popdown_strings (GTK_COMBO (priv->organizer), priv->address_strings);
	e_dialog_editable_set (GTK_COMBO (priv->organizer)->entry, priv->default_address);

	priv->existing = FALSE;
}

/* Function called to invite more people */
static void
invite_cb (GtkWidget *widget, gpointer data) 
{
	MeetingPage *mpage;
	MeetingPagePrivate *priv;
	CORBA_Environment ev;
	
	mpage = MEETING_PAGE (data);
	priv = mpage->priv;
	
	if (!get_select_name_dialog (mpage))
		return;
	
	CORBA_exception_init (&ev);

	GNOME_Evolution_Addressbook_SelectNames_activateDialog (
		priv->corba_select_names, _("Required Participants"), &ev);

	CORBA_exception_free (&ev);
}

/* Hooks the widget signals */
static void
init_widgets (MeetingPage *mpage)
{
	MeetingPagePrivate *priv;

	priv = mpage->priv;

	/* Organizer */
	gtk_signal_connect (GTK_OBJECT (GTK_COMBO (priv->organizer)->entry), "changed",
			    GTK_SIGNAL_FUNC (field_changed_cb), mpage);

	gtk_signal_connect (GTK_OBJECT (priv->other_organizer_btn), "clicked",
			    GTK_SIGNAL_FUNC (other_clicked_cb), mpage);
	gtk_signal_connect (GTK_OBJECT (priv->existing_organizer_btn), "clicked",
			    GTK_SIGNAL_FUNC (change_clicked_cb), mpage);

	/* Invite button */
	gtk_signal_connect (GTK_OBJECT (priv->invite), "clicked", 
			    GTK_SIGNAL_FUNC (invite_cb), mpage);
}

static void
popup_delegate_cb (GtkWidget *widget, gpointer data) 
{
	MeetingPage *mpage = MEETING_PAGE (data);
	MeetingPagePrivate *priv;
	EDelegateDialog *edd;
	GtkWidget *dialog;
	EMeetingAttendee *ia;
	char *address = NULL, *name = NULL;
	
	priv = mpage->priv;

	ia = e_meeting_model_find_attendee_at_row (priv->model, priv->row);

	/* Show dialog. */
	edd = e_delegate_dialog_new (NULL, itip_strip_mailto (e_meeting_attendee_get_delto (ia)));
	dialog = e_delegate_dialog_get_toplevel (edd);

	if (gnome_dialog_run_and_close (GNOME_DIALOG (dialog)) == 0){
		EMeetingAttendee *ic;
		
		name = e_delegate_dialog_get_delegate_name (edd);
		address = e_delegate_dialog_get_delegate (edd);

		/* Make sure we can add the new delegatee person */
		if (e_meeting_model_find_attendee (priv->model, address, NULL) != NULL) {
			duplicate_error ();
			goto cleanup;
		}
		
		/* Update information for attendee */
		if (e_meeting_attendee_is_set_delto (ia)) {
			EMeetingAttendee *ib;
			
			ib = e_meeting_model_find_attendee (priv->model, itip_strip_mailto (e_meeting_attendee_get_delto (ia)), NULL);
			if (ib != NULL) {
				gtk_object_ref (GTK_OBJECT (ib));
				g_ptr_array_add (priv->deleted_attendees, ib);
				
				e_meeting_model_remove_attendee (priv->model, ib);
			}			
		}
		e_meeting_attendee_set_delto (ia, g_strdup_printf ("MAILTO:%s", address));

		/* Construct delegatee information */
		ic = e_meeting_model_add_attendee_with_defaults (priv->model);
		
 		e_meeting_attendee_set_address (ic, g_strdup_printf ("MAILTO:%s", address));
		e_meeting_attendee_set_delfrom (ic, g_strdup (e_meeting_attendee_get_address (ia)));
		e_meeting_attendee_set_cn (ic, g_strdup (name));
	}

 cleanup:
	g_free (name);
	g_free (address);
	gtk_object_unref (GTK_OBJECT (edd));
}

static void
popup_delete_cb (GtkWidget *widget, gpointer data) 
{
	MeetingPage *mpage = MEETING_PAGE (data);
	MeetingPagePrivate *priv;
	EMeetingAttendee *ia;
	int pos = 0;
	
	priv = mpage->priv;

	ia = e_meeting_model_find_attendee_at_row (priv->model, priv->row);

	/* If this was a delegatee, no longer delegate */
	if (e_meeting_attendee_is_set_delfrom (ia)) {
		EMeetingAttendee *ib;
		
		ib = e_meeting_model_find_attendee (priv->model, e_meeting_attendee_get_delfrom (ia), &pos);
		if (ib != NULL)
			e_meeting_attendee_set_delto (ib, NULL);
	}
	
	/* Handle deleting all attendees in the delegation chain */	
	while (ia != NULL) {
		EMeetingAttendee *ib = NULL;

		gtk_object_ref (GTK_OBJECT (ia));
		g_ptr_array_add (priv->deleted_attendees, ia);
		e_meeting_model_remove_attendee (priv->model, ia);

		if (e_meeting_attendee_get_delto (ia) != NULL)
			ib = e_meeting_model_find_attendee (priv->model, e_meeting_attendee_get_delto (ia), NULL);
		ia = ib;
	}
}

enum {
	CAN_DELEGATE = 2,
	CAN_DELETE = 4
};

static EPopupMenu context_menu[] = {
	{ N_("_Delegate To..."),              NULL,
	  GTK_SIGNAL_FUNC (popup_delegate_cb),NULL,  CAN_DELEGATE },

	E_POPUP_SEPARATOR,

	{ N_("_Delete"),                      GNOME_STOCK_MENU_TRASH,
	  GTK_SIGNAL_FUNC (popup_delete_cb),  NULL,  CAN_DELETE },
	
	E_POPUP_TERMINATOR
};

/* handle context menu over message-list */
static gint
right_click_cb (ETable *etable, gint row, gint col, GdkEvent *event, gpointer data)
{
	MeetingPage *mpage = MEETING_PAGE (data);
	MeetingPagePrivate *priv;
	GtkMenu *menu;
	int enable_mask = 0, hide_mask = 0;

	priv = mpage->priv;

	priv->row = row;

	menu = e_popup_menu_create (context_menu, enable_mask, hide_mask, data);
	e_auto_kill_popup_menu_on_hide (menu);
	
	gtk_menu_popup (menu, NULL, NULL, NULL, NULL,
			event->button.button, event->button.time);

	return TRUE;
}



/**
 * meeting_page_construct:
 * @mpage: An task details page.
 * 
 * Constructs an task page by loading its Glade data.
 * 
 * Return value: The same object as @mpage, or NULL if the widgets could not 
 * be created.
 **/
MeetingPage *
meeting_page_construct (MeetingPage *mpage, EMeetingModel *emm)
{
	MeetingPagePrivate *priv;
	ETable *real_table;
	gchar *filename;
	
	priv = mpage->priv;

	priv->xml = glade_xml_new (EVOLUTION_GLADEDIR 
				   "/meeting-page.glade", NULL);
	if (!priv->xml) {
		g_message ("meeting_page_construct(): "
			   "Could not load the Glade XML file!");
		return NULL;
	}

	if (!get_widgets (mpage)) {
		g_message ("meeting_page_construct(): "
			   "Could not find all widgets in the XML file!");
		return NULL;
	}
	
	/* The etable displaying attendees and their status */
	gtk_object_ref (GTK_OBJECT (emm));
	priv->model = emm;

	filename = g_strdup_printf ("%s/config/et-header-meeting-page", evolution_dir);
	priv->etable = e_meeting_model_etable_from_model (priv->model, 
							  EVOLUTION_ETSPECDIR "/meeting-page.etspec", 
							  filename);
	g_free (filename);

	real_table = e_table_scrolled_get_table (priv->etable);
	gtk_signal_connect (GTK_OBJECT (real_table),
			    "right_click", GTK_SIGNAL_FUNC (right_click_cb), mpage);

	gtk_widget_show (GTK_WIDGET (priv->etable));
	gtk_box_pack_start (GTK_BOX (priv->main), GTK_WIDGET (priv->etable), TRUE, TRUE, 2);
	
	/* Init the widget signals */
	init_widgets (mpage);

	return mpage;
}

/**
 * meeting_page_new:
 * 
 * Creates a new task details page.
 * 
 * Return value: A newly-created task details page, or NULL if the page could
 * not be created.
 **/
MeetingPage *
meeting_page_new (EMeetingModel *emm)
{
	MeetingPage *mpage;

	mpage = gtk_type_new (TYPE_MEETING_PAGE);
	if (!meeting_page_construct (mpage, emm)) {
		gtk_object_unref (GTK_OBJECT (mpage));
		return NULL;
	}

	return mpage;
}

/**
 * meeting_page_get_cancel_comp:
 * @mpage: 
 * 
 * 
 * 
 * Return value: 
 **/
CalComponent *
meeting_page_get_cancel_comp (MeetingPage *mpage)
{
	MeetingPagePrivate *priv;

	g_return_val_if_fail (mpage != NULL, NULL);
	g_return_val_if_fail (IS_MEETING_PAGE (mpage), NULL);

	priv = mpage->priv;

	if (priv->deleted_attendees == NULL)
		return NULL;
	
	set_attendees (priv->comp, priv->deleted_attendees);
	
	return cal_component_clone (priv->comp);
}
