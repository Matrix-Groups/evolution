/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* addressbook-component.c
 *
 * Copyright (C) 2003  Ettore Perazzoli
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Author: Ettore Perazzoli <ettore@ximian.com>
 */

/* EPFIXME: Add autocompletion setting.  */


#include <config.h>

#include "addressbook-component.h"

#include "addressbook.h"
#include "new-addressbook.h"

#include "widgets/misc/e-source-selector.h"

#include <bonobo/bonobo-i18n.h>
#include <gtk/gtkscrolledwindow.h>
#include <gtk/gtkmenu.h>
#include <gtk/gtkimage.h>
#include <gtk/gtkimagemenuitem.h>
#include <gtk/gtkmessagedialog.h>
#include <gtk/gtkstock.h>
#include <gconf/gconf-client.h>


#define PARENT_TYPE bonobo_object_get_type ()
static BonoboObjectClass *parent_class = NULL;

struct _AddressbookComponentPrivate {
	GConfClient *gconf_client;
	ESourceList *source_list;
};


/* Utility functions.  */

static void
load_uri_for_selection (ESourceSelector *selector,
			BonoboControl *view_control)
{
	ESource *selected_source = e_source_selector_peek_primary_selection (E_SOURCE_SELECTOR (selector));

	if (selected_source != NULL) {
		char *uri = e_source_get_uri (selected_source);
		bonobo_control_set_property (view_control, NULL, "folder_uri", TC_CORBA_string, uri, NULL);
		g_free (uri);
	}
}

static void
add_popup_menu_item (GtkMenu *menu, const char *label, const char *pixmap,
		     GCallback callback, gpointer user_data)
{
	GtkWidget *item, *image;

	if (pixmap) {
		item = gtk_image_menu_item_new_with_label (label);

		/* load the image */
		image = gtk_image_new_from_file (pixmap);
		if (!image)
			image = gtk_image_new_from_stock (pixmap, GTK_ICON_SIZE_MENU);

		if (image)
			gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), image);
	} else {
		item = gtk_menu_item_new_with_label (label);
	}

	if (callback)
		g_signal_connect (G_OBJECT (item), "activate", callback, user_data);

	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
	gtk_widget_show (item);
}

/* Folder popup menu callbacks */

static void
new_addressbook_cb (GtkWidget *widget, ESourceSelector *selector)
{
	new_addressbook_dialog (GTK_WINDOW (gtk_widget_get_toplevel (widget)));
}

static void
delete_addressbook_cb (GtkWidget *widget, AddressbookComponent *comp)
{
}

/* Callbacks.  */

static void
primary_source_selection_changed_callback (ESourceSelector *selector,
					   BonoboControl *view_control)
{
	load_uri_for_selection (selector, view_control);
}

static void
fill_popup_menu_callback (ESourceSelector *selector, GtkMenu *menu, AddressbookComponent *comp)
{
	add_popup_menu_item (menu, _("New Addressbook"), NULL, G_CALLBACK (new_addressbook_cb), comp);
	add_popup_menu_item (menu, _("Delete"), GTK_STOCK_DELETE, G_CALLBACK (delete_addressbook_cb), comp);
	add_popup_menu_item (menu, _("Rename"), NULL, NULL, NULL);
}

/* Evolution::Component CORBA methods.  */

static void
impl_createControls (PortableServer_Servant servant,
		     Bonobo_Control *corba_sidebar_control,
		     Bonobo_Control *corba_view_control,
		     CORBA_Environment *ev)
{
	AddressbookComponent *addressbook_component = ADDRESSBOOK_COMPONENT (bonobo_object_from_servant (servant));
	GtkWidget *selector;
	GtkWidget *selector_scrolled_window;
	BonoboControl *sidebar_control;
	BonoboControl *view_control;

	selector = e_source_selector_new (addressbook_component->priv->source_list);
	e_source_selector_show_selection (E_SOURCE_SELECTOR (selector), FALSE);
	gtk_widget_show (selector);

	selector_scrolled_window = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (selector_scrolled_window), GTK_SHADOW_IN);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (selector_scrolled_window),
					GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_container_add (GTK_CONTAINER (selector_scrolled_window), selector);
	gtk_widget_show (selector_scrolled_window);

	sidebar_control = bonobo_control_new (selector_scrolled_window);

	view_control = addressbook_new_control ();
	g_signal_connect_object (selector, "primary_selection_changed",
				 G_CALLBACK (primary_source_selection_changed_callback),
				 G_OBJECT (view_control), 0);
	g_signal_connect_object (selector, "fill_popup_menu",
				 G_CALLBACK (fill_popup_menu_callback),
				 G_OBJECT (addressbook_component), 0);
	load_uri_for_selection (E_SOURCE_SELECTOR (selector), view_control);

	*corba_sidebar_control = CORBA_Object_duplicate (BONOBO_OBJREF (sidebar_control), ev);
	*corba_view_control = CORBA_Object_duplicate (BONOBO_OBJREF (view_control), ev);
}

static GNOME_Evolution_CreatableItemTypeList *
impl__get_userCreatableItems (PortableServer_Servant servant,
			      CORBA_Environment *ev)
{
	GNOME_Evolution_CreatableItemTypeList *list = GNOME_Evolution_CreatableItemTypeList__alloc ();

	list->_length  = 2;
	list->_maximum = list->_length;
	list->_buffer  = GNOME_Evolution_CreatableItemTypeList_allocbuf (list->_length);

	CORBA_sequence_set_release (list, FALSE);

	list->_buffer[0].id = "contact";
	list->_buffer[0].description = _("New Contact");
	list->_buffer[0].menuDescription = _("_Contact");
	list->_buffer[0].tooltip = _("Create a new contact");
	list->_buffer[0].menuShortcut = 'c';
	list->_buffer[0].iconName = "evolution-contacts-mini.png";

	list->_buffer[1].id = "contact_list";
	list->_buffer[1].description = _("New Contact List");
	list->_buffer[1].menuDescription = _("Contact _List");
	list->_buffer[1].tooltip = _("Create a new contact list");
	list->_buffer[1].menuShortcut = 'l';
	list->_buffer[1].iconName = "contact-list-16.png";

	return list;
}

static void
impl_requestCreateItem (PortableServer_Servant servant,
			const CORBA_char *item_type_name,
			CORBA_Environment *ev)
{
	/* FIXME: fill me in */

	CORBA_exception_set (ev, CORBA_USER_EXCEPTION, ex_GNOME_Evolution_Component_UnknownType, NULL);
}


/* GObject methods.  */

static void
impl_dispose (GObject *object)
{
	AddressbookComponentPrivate *priv = ADDRESSBOOK_COMPONENT (object)->priv;

	if (priv->source_list != NULL) {
		g_object_unref (priv->source_list);
		priv->source_list = NULL;
	}

	if (priv->gconf_client != NULL) {
		g_object_unref (priv->gconf_client);
		priv->gconf_client = NULL;
	}

	(* G_OBJECT_CLASS (parent_class)->dispose) (object);
}

static void
impl_finalize (GObject *object)
{
	AddressbookComponentPrivate *priv = ADDRESSBOOK_COMPONENT (object)->priv;

	g_free (priv);

	(* G_OBJECT_CLASS (parent_class)->finalize) (object);
}


/* Initialization.  */

static void
addressbook_component_class_init (AddressbookComponentClass *class)
{
	POA_GNOME_Evolution_Component__epv *epv = &class->epv;
	GObjectClass *object_class = G_OBJECT_CLASS (class);

	epv->createControls          = impl_createControls;
	epv->_get_userCreatableItems = impl__get_userCreatableItems;
	epv->requestCreateItem       = impl_requestCreateItem;

	object_class->dispose  = impl_dispose;
	object_class->finalize = impl_finalize;

	parent_class = g_type_class_peek_parent (class);
}

static void
addressbook_component_init (AddressbookComponent *component)
{
	AddressbookComponentPrivate *priv;

	priv = g_new0 (AddressbookComponentPrivate, 1);

	/* EPFIXME: Should use a custom one instead?  Also we should add
	   addressbook_component_peek_gconf_client().  */
	priv->gconf_client = gconf_client_get_default ();

	priv->source_list = e_source_list_new_for_gconf (priv->gconf_client,
							 "/apps/evolution/addressbook/sources");

	component->priv = priv;
}


/* Public API.  */

AddressbookComponent *
addressbook_component_peek (void)
{
	static AddressbookComponent *component = NULL;

	if (component == NULL)
		component = g_object_new (addressbook_component_get_type (), NULL);

	return component;
}

BONOBO_TYPE_FUNC_FULL (AddressbookComponent, GNOME_Evolution_Component, PARENT_TYPE, addressbook_component)
