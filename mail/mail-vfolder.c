/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the program; if not, see <http://www.gnu.org/licenses/>
 *
 *
 * Authors:
 *		Michael Zucchi <notzed@ximian.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 */

#include <config.h>
#include <string.h>

#include <glib.h>

#include <glib/gi18n.h>

#include <libedataserver/e-account-list.h>

#include "e-util/e-alert-dialog.h"
#include "e-util/e-util-private.h"
#include "e-util/e-account-utils.h"

#include "e-mail-backend.h"
#include "e-mail-session.h"
#include "e-mail-folder-utils.h"
#include "em-folder-tree-model.h"
#include "em-utils.h"
#include "em-vfolder-context.h"
#include "em-vfolder-editor.h"
#include "em-vfolder-rule.h"
#include "mail-autofilter.h"
#include "mail-folder-cache.h"
#include "mail-mt.h"
#include "mail-ops.h"
#include "mail-tools.h"
#include "mail-vfolder.h"

#include "e-mail-local.h"
#include "e-mail-store.h"

#define d(x)  /* (printf("%s:%s: ",  G_STRLOC, G_STRFUNC), (x))*/

static EMVFolderContext *context;	/* context remains open all time */
CamelStore *vfolder_store; /* the 1 static vfolder store */

/* lock for accessing shared resources (below) */
G_LOCK_DEFINE_STATIC (vfolder);

static GList *source_folders_remote;	/* list of source folder uri's - remote ones */
static GList *source_folders_local;	/* list of source folder uri's - local ones */
static GHashTable *vfolder_hash;
/* This is a slightly hacky solution to shutting down, we poll this variable in various
   loops, and just quit processing if it is set. */
static volatile gint vfolder_shutdown;	/* are we shutting down? */

static void rule_changed (EFilterRule *rule, CamelFolder *folder);

/* ********************************************************************** */

struct _setup_msg {
	MailMsg base;

	EMailSession *session;
	CamelFolder *folder;
	gchar *query;
	GList *sources_uri;
	GList *sources_folder;
};

static gchar *
vfolder_setup_desc (struct _setup_msg *m)
{
	return g_strdup_printf (
		_("Setting up Search Folder: %s"),
		camel_folder_get_full_name (m->folder));
}

static void
vfolder_setup_exec (struct _setup_msg *m,
                    GCancellable *cancellable,
                    GError **error)
{
	GList *l, *list = NULL;
	CamelFolder *folder;

	camel_vee_folder_set_expression ((CamelVeeFolder *)m->folder, m->query);

	l = m->sources_uri;
	while (l && !vfolder_shutdown) {
		d(printf(" Adding uri: %s\n", (gchar *)l->data));

		/* FIXME Not passing a GCancellable or GError here. */
		folder = e_mail_session_uri_to_folder_sync (
			m->session, l->data, 0, NULL, NULL);
		if (folder != NULL)
			list = g_list_append (list, folder);
		l = l->next;
	}

	l = m->sources_folder;
	while (l && !vfolder_shutdown) {
		g_object_ref (l->data);
		list = g_list_append (list, l->data);
		l = l->next;
	}

	if (!vfolder_shutdown)
		camel_vee_folder_set_folders ((CamelVeeFolder *)m->folder, list);

	l = list;
	while (l) {
		g_object_unref (l->data);
		l = l->next;
	}
	g_list_free (list);
}

static void
vfolder_setup_done (struct _setup_msg *m)
{
}

static void
vfolder_setup_free (struct _setup_msg *m)
{
	GList *l;

	g_object_unref (m->session);
	g_object_unref (m->folder);
	g_free (m->query);

	l = m->sources_uri;
	while (l) {
		g_free (l->data);
		l = l->next;
	}
	g_list_free (m->sources_uri);

	l = m->sources_folder;
	while (l) {
		g_object_unref (l->data);
		l = l->next;
	}
	g_list_free (m->sources_folder);
}

static MailMsgInfo vfolder_setup_info = {
	sizeof (struct _setup_msg),
	(MailMsgDescFunc) vfolder_setup_desc,
	(MailMsgExecFunc) vfolder_setup_exec,
	(MailMsgDoneFunc) vfolder_setup_done,
	(MailMsgFreeFunc) vfolder_setup_free
};

/* sources_uri should be camel uri's */
static gint
vfolder_setup (EMailSession *session,
               CamelFolder *folder,
               const gchar *query,
               GList *sources_uri,
               GList *sources_folder)
{
	struct _setup_msg *m;
	gint id;

	m = mail_msg_new (&vfolder_setup_info);
	m->session = g_object_ref (session);
	m->folder = g_object_ref (folder);
	m->query = g_strdup (query);
	m->sources_uri = sources_uri;
	m->sources_folder = sources_folder;

	id = m->base.seq;
	mail_msg_slow_ordered_push (m);

	return id;
}

/* ********************************************************************** */

struct _adduri_msg {
	MailMsg base;

	EMailSession *session;
	gchar *uri;
	GList *folders;
	gint remove;
};

static gchar *
vfolder_adduri_desc (struct _adduri_msg *m)
{
	EAccount *account;
	CamelStore *store;
	CamelSession *session;
	const gchar *store_name;
	const gchar *uid;
	gchar *folder_name;
	gchar *description;
	gboolean success;

	session = CAMEL_SESSION (m->session);

	success = e_mail_folder_uri_parse (
		session, m->uri, &store, &folder_name, NULL);

	if (!success)
		return NULL;

	uid = camel_service_get_uid (CAMEL_SERVICE (store));
	account = e_get_account_by_uid (uid);

	if (account != NULL)
		store_name = account->name;
	else
		store_name = _("On This Computer");

	description = g_strdup_printf (
		_("Updating Search Folders for '%s' : %s"),
		store_name, folder_name);

	g_object_unref (store);
	g_free (folder_name);

	return description;
}

static void
vfolder_adduri_exec (struct _adduri_msg *m,
                     GCancellable *cancellable,
                     GError **error)
{
	GList *l;
	CamelFolder *folder = NULL;
	MailFolderCache *folder_cache;

	if (vfolder_shutdown)
		return;

	folder_cache = e_mail_session_get_folder_cache (m->session);

	/* we dont try lookup the cache if we are removing it, its no longer there */

	if (!m->remove &&
	    !mail_folder_cache_get_folder_from_uri (folder_cache, m->uri, &folder)) {
		g_warning (
			"Folder '%s' disappeared while I was "
			"adding/removing it to/from my vfolder", m->uri);
		return;
	}

	if (folder == NULL)
		folder = e_mail_session_uri_to_folder_sync (
			m->session, m->uri, 0, cancellable, error);

	if (folder != NULL) {
		l = m->folders;
		while (l && !vfolder_shutdown) {
			if (m->remove)
				camel_vee_folder_remove_folder (
					CAMEL_VEE_FOLDER (l->data), folder);
			else
				camel_vee_folder_add_folder ((CamelVeeFolder *)l->data, folder);
			l = l->next;
		}
		g_object_unref (folder);
	}
}

static void
vfolder_adduri_done (struct _adduri_msg *m)
{
}

static void
vfolder_adduri_free (struct _adduri_msg *m)
{
	g_object_unref (m->session);
	g_list_foreach (m->folders, (GFunc)g_object_unref, NULL);
	g_list_free (m->folders);
	g_free (m->uri);
}

static MailMsgInfo vfolder_adduri_info = {
	sizeof (struct _adduri_msg),
	(MailMsgDescFunc) vfolder_adduri_desc,
	(MailMsgExecFunc) vfolder_adduri_exec,
	(MailMsgDoneFunc) vfolder_adduri_done,
	(MailMsgFreeFunc) vfolder_adduri_free
};

/* uri should be a camel uri */
static gint
vfolder_adduri (EMailSession *session,
                const gchar *uri,
                GList *folders,
                gint remove)
{
	struct _adduri_msg *m;
	gint id;

	m = mail_msg_new (&vfolder_adduri_info);
	m->session = g_object_ref (session);
	m->folders = folders;
	m->uri = g_strdup (uri);
	m->remove = remove;

	id = m->base.seq;
	mail_msg_slow_ordered_push (m);

	return id;
}

/* ********************************************************************** */

static GList *
mv_find_folder (GList *l, EMailSession *session, const gchar *uri)
{
	CamelSession *camel_session = CAMEL_SESSION (session);

	while (l) {
		if (e_mail_folder_uri_equal (camel_session, l->data, uri))
			break;
		l = l->next;
	}
	return l;
}

static gint
uri_is_ignore (EMailSession *session, const gchar *uri)
{
	EAccountList *accounts;
	EAccount *account;
	EIterator *iter;
	CamelSession *camel_session;
	const gchar *local_drafts_uri;
	const gchar *local_outbox_uri;
	const gchar *local_sent_uri;
	gint found = FALSE;

	local_drafts_uri =
		e_mail_local_get_folder_uri (E_MAIL_LOCAL_FOLDER_DRAFTS);
	local_outbox_uri =
		e_mail_local_get_folder_uri (E_MAIL_LOCAL_FOLDER_OUTBOX);
	local_sent_uri =
		e_mail_local_get_folder_uri (E_MAIL_LOCAL_FOLDER_SENT);

	camel_session = CAMEL_SESSION (session);

	if (e_mail_folder_uri_equal (camel_session, local_outbox_uri, uri))
		return TRUE;

	if (e_mail_folder_uri_equal (camel_session, local_sent_uri, uri))
		return TRUE;

	if (e_mail_folder_uri_equal (camel_session, local_drafts_uri, uri))
		return TRUE;

	accounts = e_get_account_list ();
	iter = e_list_get_iterator (E_LIST (accounts));

	while (!found && e_iterator_is_valid (iter)) {
		/* XXX EIterator misuses const. */
		account = (EAccount *) e_iterator_get (iter);

		if (account->sent_folder_uri != NULL)
			found = e_mail_folder_uri_equal (
				camel_session, uri,
				account->sent_folder_uri);

		if (!found && account->drafts_folder_uri != NULL)
			found = e_mail_folder_uri_equal (
				camel_session, uri,
				account->drafts_folder_uri);

		if (found)
			break;

		e_iterator_next (iter);
	}

	g_object_unref (iter);

	return found;
}

/* so special we never use it */
static gint
uri_is_spethal (CamelStore *store, const gchar *uri)
{
	CamelURL *url;
	gint res;

	/* This is a bit of a hack, but really the only way it can be done at the moment. */

	if ((store->flags & (CAMEL_STORE_VTRASH|CAMEL_STORE_VJUNK)) == 0)
		return FALSE;

	url = camel_url_new (uri, NULL);
	if (url == NULL)
		return TRUE;

	/* don't use strcasecmp here */
	if (url->fragment) {
		res = (((store->flags & CAMEL_STORE_VTRASH)
			&& strcmp (url->fragment, CAMEL_VTRASH_NAME) == 0)
		       || ((store->flags & CAMEL_STORE_VJUNK)
			   && strcmp (url->fragment, CAMEL_VJUNK_NAME) == 0));
	} else {
		res = url->path
			&& (((store->flags & CAMEL_STORE_VTRASH)
			     && strcmp(url->path, "/" CAMEL_VTRASH_NAME) == 0)
			    || ((store->flags & CAMEL_STORE_VJUNK)
				&& strcmp(url->path, "/" CAMEL_VJUNK_NAME) == 0));
	}

	camel_url_free (url);

	return res;
}

/**
 * mail_vfolder_add_uri:
 * @session: an #EMailSession
 * @store: a #CamelStore containing the uri
 * @curi: an email uri to be added/removed
 * @remove: Whether the uri should be removed or added
 *
 * Called when a new uri becomes (un)available.  If @store is not a
 * CamelVeeStore, the uri is added/removed from the list of cached source
 * folders.  Then each vfolder rule is checked to see if the specified uri
 * matches a source of the rule.  It builds a list of vfolders that use (or
 * would use) the specified uri as a source.  It then adds (or removes) this uri
 * to (from) those vfolders via camel_vee_folder_add/remove_folder() but does
 * not modify the actual filters or write changes to disk.
 *
 * NOTE: This function must be called from the main thread.
 */
static void
mail_vfolder_add_uri (EMailSession *session,
                      CamelStore *store,
                      const gchar *curi,
                      gint remove)
{
	EFilterRule *rule;
	const gchar *source;
	CamelVeeFolder *vf;
	CamelProvider *provider;
	GList *folders = NULL, *link;
	gint remote;
	gint is_ignore;
	gchar *uri;

	provider = camel_service_get_provider (CAMEL_SERVICE (store));
	remote = (provider->flags & CAMEL_PROVIDER_IS_REMOTE) != 0;

	uri = em_uri_from_camel (curi);
	if (uri_is_spethal (store, curi)) {
		g_free (uri);
		return;
	}

	g_return_if_fail (mail_in_main_thread ());

	is_ignore = uri_is_ignore (session, curi);

	G_LOCK (vfolder);

/*	d(printf("%s uri to check: %s\n", remove?"Removing":"Adding", uri)); */

	/* maintain the source folders lists for changed rules later on */
	if (CAMEL_IS_VEE_STORE (store)) {
		is_ignore = TRUE;
	} else if (remove) {
		if (remote) {
			if ((link = mv_find_folder (source_folders_remote, session, curi)) != NULL) {
				g_free (link->data);
				source_folders_remote = g_list_remove_link (source_folders_remote, link);
			}
		} else {
			if ((link = mv_find_folder (source_folders_local, session, curi)) != NULL) {
				g_free (link->data);
				source_folders_local = g_list_remove_link (source_folders_local, link);
			}
		}
	} else if (!is_ignore) {
		/* we ignore drafts/sent/outbox here */
		if (remote) {
			if (mv_find_folder (source_folders_remote, session, curi) == NULL)
				source_folders_remote = g_list_prepend (source_folders_remote, g_strdup (curi));
		} else {
			if (mv_find_folder (source_folders_local, session, curi) == NULL)
				source_folders_local = g_list_prepend (source_folders_local, g_strdup (curi));
		}
	}

	if (context == NULL)
		goto done;

	rule = NULL;
	while ((rule = e_rule_context_next_rule ((ERuleContext *)context, rule, NULL))) {
		gint found = FALSE;

		if (!rule->name) {
			d(printf("invalid rule (%p): rule->name is set to NULL\n", rule));
			continue;
		}
		/* Don't auto-add any sent/drafts folders etc,
		 * they must be explictly listed as a source. */
		if (rule->source
		    && !is_ignore
		    && ((((EMVFolderRule *)rule)->with == EM_VFOLDER_RULE_WITH_LOCAL && !remote)
			|| (((EMVFolderRule *)rule)->with == EM_VFOLDER_RULE_WITH_REMOTE_ACTIVE && remote)
			|| (((EMVFolderRule *)rule)->with == EM_VFOLDER_RULE_WITH_LOCAL_REMOTE_ACTIVE)))
			found = TRUE;

		source = NULL;
		while (!found && (source = em_vfolder_rule_next_source (
				(EMVFolderRule *)rule, source))) {
			gchar *csource;
			csource = em_uri_to_camel (source);
			found = e_mail_folder_uri_equal (
				CAMEL_SESSION (session), curi, csource);
			g_free (csource);
		}

		if (found) {
			vf = g_hash_table_lookup (vfolder_hash, rule->name);
			if (!vf) {
				g_warning ("vf is NULL for %s\n", rule->name);
				continue;
			}
			g_object_ref (vf);
			folders = g_list_prepend (folders, vf);
		}
	}

done:
	G_UNLOCK (vfolder);

	if (folders != NULL)
		vfolder_adduri (session, curi, folders, remove);

	g_free (uri);
}

/**
 * mail_vfolder_uri_available:
 * @session: an #EMailSession
 * @store: a #CamelStore containing the uri
 * @uri: uri of a folder that became available
 *
 * Adds @uri to the list of folders searched if any vfolder source matches the
 * uri.  This function has a transient effect and does not permanently modify
 * the vfolder filter rules on disk.
 */
static void
mail_vfolder_notify_uri_available (EMailSession *session,
                                   CamelStore *store,
                                   const gchar *uri)
{
	mail_vfolder_add_uri (session, store, uri, FALSE);
}

/**
 * mail_vfolder_uri_available:
 * @session: an #EMailSession
 * @store: a #CamelStore containing the uri
 * @uri: uri of a folder that became unavailable
 *
 * Removes @uri from the list of folders searched if any vfolder source matches the
 * uri.  This function has a transient effect and does not permanently modify
 * the vfolder filter rules on disk.
 */
static void
mail_vfolder_notify_uri_unavailable (EMailSession *session,
                                     CamelStore *store,
                                     const gchar *uri)
{
	mail_vfolder_add_uri (session, store, uri, TRUE);
}

/**
 * mail_vfolder_delete_uri:
 * @backend: an #EMailBackend
 * @store: a #CamelStore containing the uri
 * @curi: an email uri that has been deleted
 *
 * Looks through all vfolder rules to see if @curi is listed as a source for any
 * vfolder rules.  If the uri is found in the source for any rule, it is removed
 * and the user is alerted to the fact that the vfolder rules have been updated.
 * The new vfolder rules are written to disk.
 *
 * XXX: It doesn't appear that the changes to the vfolder rules are sent down to
 * the camel level, however. So the actual vfolders will not change behavior
 * until evolution is restarted (?)
 *
 * NOTE: This function must be called from the main thread.
 */
static void
mail_vfolder_delete_uri (EMailBackend *backend,
                         CamelStore *store,
                         const gchar *curi)
{
	EFilterRule *rule;
	EMailSession *session;
	const gchar *source;
	CamelVeeFolder *vf;
	GString *changed;
	guint changed_count;
	gchar *uri;
	GList *link;

	g_return_if_fail (E_IS_MAIL_BACKEND (backend));
	g_return_if_fail (CAMEL_IS_STORE (store));
	g_return_if_fail (curi != NULL);

	if (uri_is_spethal (store, curi))
		return;

	uri = em_uri_from_camel (curi);

	d(printf ("Deleting uri to check: %s\n", uri));

	g_return_if_fail (mail_in_main_thread ());

	session = e_mail_backend_get_session (backend);

	changed_count = 0;
	changed = g_string_new ("");

	G_LOCK (vfolder);

	if (context == NULL)
		goto done;

	/* see if any rules directly reference this removed uri */
	rule = NULL;
	while ((rule = e_rule_context_next_rule ((ERuleContext *) context, rule, NULL))) {

		if (!rule->name) {
			d(printf("invalid rule (%p): rule->name is set to NULL\n", rule));
			continue;
		}

		source = NULL;
		while ((source = em_vfolder_rule_next_source ((EMVFolderRule *) rule, source))) {
			gchar *csource = em_uri_to_camel (source);

			/* Remove all sources that match, ignore changed events though
			   because the adduri call above does the work async */
			if (e_mail_folder_uri_equal (CAMEL_SESSION (session), curi, csource)) {
				vf = g_hash_table_lookup (vfolder_hash, rule->name);
				if (!vf) {
					g_warning ("vf is NULL for %s\n", rule->name);
					continue;
				}
				g_signal_handlers_disconnect_matched (
					rule, G_SIGNAL_MATCH_FUNC |
					G_SIGNAL_MATCH_DATA, 0, 0, NULL,
					rule_changed, vf);
				em_vfolder_rule_remove_source ((EMVFolderRule *)rule, source);
				g_signal_connect (rule, "changed", G_CALLBACK(rule_changed), vf);
				if (changed_count == 0) {
					g_string_append (changed, rule->name);
				} else {
					if (changed_count == 1) {
						g_string_prepend (changed, "    ");
						g_string_append (changed, "\n");
					}
					g_string_append_printf (changed, "    %s\n", rule->name);
				}
				changed_count++;
				source = NULL;
			}
			g_free (csource);
		}
	}

done:
	if ((link = mv_find_folder (source_folders_remote, session, curi)) != NULL) {
		g_free (link->data);
		source_folders_remote = g_list_remove_link (source_folders_remote, link);
	}

	if ((link = mv_find_folder (source_folders_local, session, curi)) != NULL) {
		g_free (link->data);
		source_folders_local = g_list_remove_link (source_folders_local, link);
	}

	G_UNLOCK (vfolder);

	if (changed_count > 0) {
		const gchar *config_dir;
		gchar *user, *info;

		info = g_strdup_printf (ngettext (
			/* Translators: The first %s is name of the affected
			 * search folder(s), the second %s is the URI of the
			 * removed folder. For more than one search folder is
			 * each of them on a separate line, with four spaces
			 * in front of its name, without quotes. */
			"The Search Folder \"%s\" has been modified to "
			"account for the deleted folder\n\"%s\".",
			"The following Search Folders\n%s have been modified "
			"to account for the deleted folder\n\"%s\".",
			changed_count), changed->str, uri);
		e_mail_backend_submit_alert (
			backend, "mail:vfolder-updated", info, NULL);
		g_free (info);

		config_dir = mail_session_get_config_dir ();
		user = g_build_filename (config_dir, "vfolders.xml", NULL);
		e_rule_context_save ((ERuleContext *) context, user);
		g_free (user);
	}

	g_string_free (changed, TRUE);

	g_free (uri);
}

/* called when a uri is renamed in a store */
static void
mail_vfolder_rename_uri (CamelStore *store, const gchar *cfrom, const gchar *cto)
{
	EFilterRule *rule;
	const gchar *source;
	CamelVeeFolder *vf;
	CamelSession *session;
	gint changed = 0;
	gchar *from, *to;

	d(printf("vfolder rename uri: %s to %s\n", cfrom, cto));

	if (context == NULL || uri_is_spethal (store, cfrom) || uri_is_spethal (store, cto))
		return;

	g_return_if_fail (mail_in_main_thread ());

	session = camel_service_get_session (CAMEL_SERVICE (store));

	from = em_uri_from_camel (cfrom);
	to = em_uri_from_camel (cto);

	G_LOCK (vfolder);

	/* see if any rules directly reference this removed uri */
	rule = NULL;
	while ((rule = e_rule_context_next_rule ((ERuleContext *)context, rule, NULL))) {
		source = NULL;
		while ((source = em_vfolder_rule_next_source ((EMVFolderRule *)rule, source))) {
			gchar *csource = em_uri_to_camel (source);

			/* Remove all sources that match, ignore changed events though
			   because the adduri call above does the work async */
			if (e_mail_folder_uri_equal (session, cfrom, csource)) {
				vf = g_hash_table_lookup (vfolder_hash, rule->name);
				if (!vf) {
					g_warning ("vf is NULL for %s\n", rule->name);
					continue;
				}
				g_signal_handlers_disconnect_matched (
					rule, G_SIGNAL_MATCH_FUNC |
					G_SIGNAL_MATCH_DATA, 0, 0, NULL,
					rule_changed, vf);
				em_vfolder_rule_remove_source ((EMVFolderRule *)rule, source);
				em_vfolder_rule_add_source ((EMVFolderRule *)rule, to);
				g_signal_connect(rule, "changed", G_CALLBACK(rule_changed), vf);
				changed++;
				source = NULL;
			}
			g_free (csource);
		}
	}

	G_UNLOCK (vfolder);

	if (changed) {
		const gchar *config_dir;
		gchar *user;

		d(printf("Vfolders updated from renamed folder\n"));
		config_dir = mail_session_get_config_dir ();
		user = g_build_filename (config_dir, "vfolders.xml", NULL);
		e_rule_context_save ((ERuleContext *)context, user);
		g_free (user);
	}

	g_free (from);
	g_free (to);
}

GList *
mail_vfolder_get_sources_local (void)
{
	return source_folders_local;
}

GList *
mail_vfolder_get_sources_remote (void)
{
	return source_folders_remote;
}

/* ********************************************************************** */

static void context_rule_added (ERuleContext *ctx, EFilterRule *rule);

static void
rule_add_sources (EMailSession *session,
                  GList *l,
                  GList **sources_folderp,
                  GList **sources_urip)
{
	GList *sources_folder = *sources_folderp;
	GList *sources_uri = *sources_urip;
	MailFolderCache *folder_cache;
	CamelFolder *newfolder;

	folder_cache = e_mail_session_get_folder_cache (session);

	while (l) {
		gchar *curi = em_uri_to_camel (l->data);

		if (mail_folder_cache_get_folder_from_uri (
			folder_cache, curi, &newfolder)) {
			if (newfolder)
				sources_folder = g_list_append (sources_folder, newfolder);
			else
				sources_uri = g_list_append (sources_uri, g_strdup (curi));
		}
		g_free (curi);
		l = l->next;
	}

	*sources_folderp = sources_folder;
	*sources_urip = sources_uri;
}

static void
rule_changed (EFilterRule *rule, CamelFolder *folder)
{
	EMailSession *session;
	GList *sources_uri = NULL, *sources_folder = NULL;
	GString *query;
	const gchar *full_name;

	full_name = camel_folder_get_full_name (folder);
	session = em_vfolder_rule_get_session (EM_VFOLDER_RULE (rule));

	/* if the folder has changed name, then add it, then remove the old manually */
	if (strcmp (full_name, rule->name) != 0) {
		gchar *oldname;

		gpointer key;
		gpointer oldfolder;

		G_LOCK (vfolder);
		d(printf("Changing folder name in hash table to '%s'\n", rule->name));
		if (g_hash_table_lookup_extended (vfolder_hash, full_name, &key, &oldfolder)) {
			g_hash_table_remove (vfolder_hash, key);
			g_free (key);
			g_hash_table_insert (vfolder_hash, g_strdup (rule->name), folder);
			G_UNLOCK (vfolder);
		} else {
			G_UNLOCK (vfolder);
			g_warning("couldn't find a vfolder rule in our table? %s", full_name);
		}

		oldname = g_strdup (full_name);
		/* FIXME Not passing a GCancellable or GError. */
		camel_store_rename_folder_sync (
			vfolder_store, oldname, rule->name, NULL, NULL);
		g_free (oldname);
	}

	d(printf("Filter rule changed? for folder '%s'!!\n", folder->name));

	/* find any (currently available) folders, and add them to the ones to open */
	rule_add_sources (
		session, ((EMVFolderRule *)rule)->sources,
		&sources_folder, &sources_uri);

	G_LOCK (vfolder);
	if (((EMVFolderRule *)rule)->with ==
			EM_VFOLDER_RULE_WITH_LOCAL ||
			((EMVFolderRule *)rule)->with ==
			EM_VFOLDER_RULE_WITH_LOCAL_REMOTE_ACTIVE)
		rule_add_sources (
			session, source_folders_local,
			&sources_folder, &sources_uri);
	if (((EMVFolderRule *)rule)->with ==
			EM_VFOLDER_RULE_WITH_REMOTE_ACTIVE ||
			((EMVFolderRule *)rule)->with ==
			EM_VFOLDER_RULE_WITH_LOCAL_REMOTE_ACTIVE)
		rule_add_sources (
			session, source_folders_remote,
			&sources_folder, &sources_uri);
	G_UNLOCK (vfolder);

	query = g_string_new("");
	e_filter_rule_build_code (rule, query);

	vfolder_setup (session, folder, query->str, sources_uri, sources_folder);

	g_string_free (query, TRUE);
}

static void
context_rule_added (ERuleContext *ctx,
                    EFilterRule *rule)
{
	CamelFolder *folder;

	d(printf("rule added: %s\n", rule->name));

	/* this always runs quickly */
	/* FIXME Not passing a GCancellable or GError. */
	folder = camel_store_get_folder_sync (
		vfolder_store, rule->name, 0, NULL, NULL);
	if (folder) {
		g_signal_connect(rule, "changed", G_CALLBACK(rule_changed), folder);

		G_LOCK (vfolder);
		g_hash_table_insert (vfolder_hash, g_strdup (rule->name), folder);
		G_UNLOCK (vfolder);

		rule_changed (rule, folder);
	}
}

static void
context_rule_removed (ERuleContext *ctx,
                      EFilterRule *rule)
{
	gpointer key, folder = NULL;

	d(printf("rule removed; %s\n", rule->name));

	/* TODO: remove from folder info cache? */

	G_LOCK (vfolder);
	if (g_hash_table_lookup_extended (vfolder_hash, rule->name, &key, &folder)) {
		g_hash_table_remove (vfolder_hash, key);
		g_free (key);
	}
	G_UNLOCK (vfolder);

	/* FIXME Not passing a GCancellable  or GError. */
	camel_store_delete_folder_sync (
		vfolder_store, rule->name, NULL, NULL);
	/* this must be unref'd after its deleted */
	if (folder)
		g_object_unref ((CamelFolder *) folder);
}

static void
store_folder_deleted_cb (CamelStore *store,
                         CamelFolderInfo *info)
{
	EFilterRule *rule;
	gchar *user;

	d(printf("Folder deleted: %s\n", info->name));
	store = store;

	/* Warning not thread safe, but might be enough */

	G_LOCK (vfolder);

	/* delete it from our list */
	rule = e_rule_context_find_rule ((ERuleContext *)context, info->full_name, NULL);
	if (rule) {
		const gchar *config_dir;

		/* We need to stop listening to removed events,
		 * otherwise we'll try and remove it again. */
		g_signal_handlers_disconnect_matched (
			context, G_SIGNAL_MATCH_FUNC | G_SIGNAL_MATCH_DATA,
			0, 0, NULL, context_rule_removed, context);
		e_rule_context_remove_rule ((ERuleContext *)context, rule);
		g_object_unref (rule);
		g_signal_connect(context, "rule_removed", G_CALLBACK(context_rule_removed), context);

		config_dir = mail_session_get_config_dir ();
		user = g_build_filename (config_dir, "vfolders.xml", NULL);
		e_rule_context_save ((ERuleContext *)context, user);
		g_free (user);
	} else {
		g_warning("Cannot find rule for deleted vfolder '%s'", info->name);
	}

	G_UNLOCK (vfolder);
}

static void
store_folder_renamed_cb (CamelStore *store,
                         const gchar *old_name,
                         CamelFolderInfo *info)
{
	EFilterRule *rule;
	gchar *user;

	gpointer key, folder;

	/* This should be more-or-less thread-safe */

	d(printf("Folder renamed to '%s' from '%s'\n", info->full_name, old_name));

	/* Folder is already renamed? */
	G_LOCK (vfolder);
	d(printf("Changing folder name in hash table to '%s'\n", info->full_name));
	if (g_hash_table_lookup_extended (vfolder_hash, old_name, &key, &folder)) {
		const gchar *config_dir;

		g_hash_table_remove (vfolder_hash, key);
		g_free (key);
		g_hash_table_insert (vfolder_hash, g_strdup (info->full_name), folder);

		rule = e_rule_context_find_rule ((ERuleContext *)context, old_name, NULL);
		if (!rule) {
			G_UNLOCK (vfolder);
			g_warning ("Rule shouldn't be NULL\n");
			return;
		}

		g_signal_handlers_disconnect_matched (
			rule, G_SIGNAL_MATCH_FUNC|G_SIGNAL_MATCH_DATA,
			0, 0, NULL, rule_changed, folder);
		e_filter_rule_set_name (rule, info->full_name);
		g_signal_connect(rule, "changed", G_CALLBACK(rule_changed), folder);

		config_dir = mail_session_get_config_dir ();
		user = g_build_filename (config_dir, "vfolders.xml", NULL);
		e_rule_context_save ((ERuleContext *)context, user);
		g_free (user);

		G_UNLOCK (vfolder);
	} else {
		G_UNLOCK (vfolder);
		g_warning("couldn't find a vfolder rule in our table? %s", info->full_name);
	}
}

static void
folder_available_cb (MailFolderCache *cache,
                     CamelStore *store,
                     const gchar *uri,
                     EMailSession *session)
{
	mail_vfolder_notify_uri_available (session, store, uri);
}

static void
folder_unavailable_cb (MailFolderCache *cache,
                       CamelStore *store,
                       const gchar *uri,
                       EMailSession *session)
{
	mail_vfolder_notify_uri_unavailable (session, store, uri);
}

static void
folder_deleted_cb (MailFolderCache *cache,
                   CamelStore *store,
                   const gchar *uri,
                   EMailBackend *backend)
{
	mail_vfolder_delete_uri (backend, store, uri);
}

static void
folder_renamed_cb (MailFolderCache *cache,
                   CamelStore *store,
                   const gchar *olduri,
                   const gchar *newuri,
                   gpointer user_data)
{
	mail_vfolder_rename_uri (store, olduri, newuri);
}

void
vfolder_load_storage (EMailBackend *backend)
{
	/* lock for loading storage, it is safe to call it more than once */
	G_LOCK_DEFINE_STATIC (vfolder_hash);

	CamelService *service;
	const gchar *key;
	const gchar *data_dir;
	const gchar *config_dir;
	gchar *user, *storeuri;
	EFilterRule *rule;
	MailFolderCache *folder_cache;
	EMailSession *session;
	gchar *xmlfile;
	GConfClient *client;

	g_return_if_fail (E_IS_MAIL_BACKEND (backend));

	G_LOCK (vfolder_hash);

	if (vfolder_hash) {
		/* we have already initialized */
		G_UNLOCK (vfolder_hash);
		return;
	}

	vfolder_hash = g_hash_table_new (g_str_hash, g_str_equal);

	G_UNLOCK (vfolder_hash);

	data_dir = mail_session_get_data_dir ();
	config_dir = mail_session_get_config_dir ();
	session = e_mail_backend_get_session (backend);

	/* first, create the vfolder store, and set it up */
	storeuri = g_strdup_printf("vfolder:%s/vfolder", data_dir);
	service = camel_session_add_service (
		CAMEL_SESSION (session), "vfolder",
		storeuri, CAMEL_PROVIDER_STORE, NULL);
	if (service != NULL)
		camel_service_connect_sync (service, NULL);
	else {
		g_warning("Cannot open vfolder store - no vfolders available");
		return;
	}

	g_return_if_fail (CAMEL_IS_STORE (service));

	vfolder_store = CAMEL_STORE (service);

	g_signal_connect (
		service, "folder-deleted",
		G_CALLBACK (store_folder_deleted_cb), backend);

	g_signal_connect (
		service, "folder-renamed",
		G_CALLBACK (store_folder_renamed_cb), NULL);

	/* load our rules */
	user = g_build_filename (config_dir, "vfolders.xml", NULL);
	context = em_vfolder_context_new (session);

	xmlfile = g_build_filename (EVOLUTION_PRIVDATADIR, "vfoldertypes.xml", NULL);
	if (e_rule_context_load ((ERuleContext *)context,
			       xmlfile, user) != 0) {
		g_warning("cannot load vfolders: %s\n", ((ERuleContext *)context)->error);
	}
	g_free (xmlfile);
	g_free (user);

	g_signal_connect(context, "rule_added", G_CALLBACK(context_rule_added), context);
	g_signal_connect(context, "rule_removed", G_CALLBACK(context_rule_removed), context);

	/* load store to mail component */
	e_mail_store_add (session, vfolder_store, _("Search Folders"));

	/* and setup the rules we have */
	rule = NULL;
	while ((rule = e_rule_context_next_rule ((ERuleContext *)context, rule, NULL))) {
		if (rule->name) {
			d(printf("rule added: %s\n", rule->name));
			context_rule_added ((ERuleContext *)context, rule);
		} else {
			d(printf("invalid rule (%p) encountered: rule->name is NULL\n", rule));
		}
	}

	g_free (storeuri);

	/* reenable the feature if required */
	client = gconf_client_get_default ();
	key = "/apps/evolution/mail/display/enable_vfolders";
	if (!gconf_client_get_bool (client, key, NULL))
		gconf_client_set_bool (client, key, TRUE, NULL);
	g_object_unref (client);

	folder_cache = e_mail_session_get_folder_cache (session);

	g_signal_connect (
		folder_cache, "folder-available",
		G_CALLBACK (folder_available_cb), session);
	g_signal_connect (
		folder_cache, "folder-unavailable",
		G_CALLBACK (folder_unavailable_cb), session);
	g_signal_connect (
		folder_cache, "folder-deleted",
		G_CALLBACK (folder_deleted_cb), backend);
	g_signal_connect (
		folder_cache, "folder-renamed",
		G_CALLBACK (folder_renamed_cb), NULL);
}

void
vfolder_revert (void)
{
	const gchar *config_dir;
	gchar *user;

	d(printf("vfolder_revert\n"));
	config_dir = mail_session_get_config_dir ();
	user = g_build_filename (config_dir, "vfolders.xml", NULL);
	e_rule_context_revert ((ERuleContext *)context, user);
	g_free (user);
}

void
vfolder_edit (EShellView *shell_view)
{
	EShellBackend *shell_backend;
	EShellWindow *shell_window;
	EMailBackend *backend;
	GtkWidget *dialog;
	const gchar *config_dir;
	gchar *filename;

	g_return_if_fail (E_IS_SHELL_VIEW (shell_view));

	shell_backend = e_shell_view_get_shell_backend (shell_view);
	shell_window = e_shell_view_get_shell_window (shell_view);

	config_dir = e_shell_backend_get_config_dir (shell_backend);
	filename = g_build_filename (config_dir, "vfolders.xml", NULL);

	backend = E_MAIL_BACKEND (shell_backend);

	vfolder_load_storage (backend);

	dialog = em_vfolder_editor_new (context);
	gtk_window_set_title (
		GTK_WINDOW (dialog), _("Search Folders"));
	gtk_window_set_transient_for (
		GTK_WINDOW (dialog), GTK_WINDOW (shell_window));

	switch (gtk_dialog_run (GTK_DIALOG (dialog))) {
		case GTK_RESPONSE_OK:
			e_rule_context_save ((ERuleContext *) context, filename);
			break;
		default:
			e_rule_context_revert ((ERuleContext *) context, filename);
			break;
	}

	gtk_widget_destroy (dialog);
}

static void
edit_rule_response (GtkWidget *w, gint button, gpointer data)
{
	if (button == GTK_RESPONSE_OK) {
		const gchar *config_dir;
		gchar *user;
		EFilterRule *rule = g_object_get_data (G_OBJECT (w), "rule");
		EFilterRule *orig = g_object_get_data (G_OBJECT (w), "orig");

		e_filter_rule_copy (orig, rule);
		config_dir = mail_session_get_config_dir ();
		user = g_build_filename (config_dir, "vfolders.xml", NULL);
		e_rule_context_save ((ERuleContext *)context, user);
		g_free (user);
	}

	gtk_widget_destroy (w);
}

void
vfolder_edit_rule (EMailBackend *backend,
                   const gchar *uri)
{
	GtkWidget *w;
	GtkDialog *gd;
	GtkWidget *container;
	EFilterRule *rule, *newrule;
	CamelURL *url;

	g_return_if_fail (E_IS_MAIL_BACKEND (backend));
	g_return_if_fail (uri != NULL);

	url = camel_url_new (uri, NULL);
	if (url && url->fragment
	    && (rule = e_rule_context_find_rule (
	    (ERuleContext *) context, url->fragment, NULL))) {
		g_object_ref (G_OBJECT (rule));
		newrule = e_filter_rule_clone (rule);

		w = e_filter_rule_get_widget ((EFilterRule *)newrule, (ERuleContext *)context);

		gd = (GtkDialog *)gtk_dialog_new_with_buttons (
			_("Edit Search Folder"), NULL,
			GTK_DIALOG_DESTROY_WITH_PARENT,
			GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
			GTK_STOCK_OK, GTK_RESPONSE_OK, NULL);

		gtk_container_set_border_width (GTK_CONTAINER (gd), 6);

		container = gtk_dialog_get_content_area (gd);
		gtk_box_set_spacing (GTK_BOX (container), 6);

		gtk_dialog_set_default_response (gd, GTK_RESPONSE_OK);
		g_object_set (gd, "resizable", TRUE, NULL);
		gtk_window_set_default_size (GTK_WINDOW (gd), 500, 500);
		gtk_box_pack_start (GTK_BOX (container), w, TRUE, TRUE, 0);
		gtk_widget_show ((GtkWidget *)gd);
		g_object_set_data_full (
			G_OBJECT (gd), "rule", newrule,
			(GDestroyNotify) g_object_unref);
		g_object_set_data_full (
			G_OBJECT (gd), "orig", rule,
			(GDestroyNotify) g_object_unref);
		g_signal_connect (
			gd, "response",
			G_CALLBACK (edit_rule_response), NULL);
		gtk_widget_show ((GtkWidget *)gd);
	} else {
		/* TODO: we should probably just create it ... */
		e_mail_backend_submit_alert (
			backend, "mail:vfolder-notexist", uri, NULL);
	}

	if (url)
		camel_url_free (url);
}

static void
new_rule_clicked (GtkWidget *w, gint button, gpointer data)
{
	if (button == GTK_RESPONSE_OK) {
		const gchar *config_dir;
		gchar *user;
		EFilterRule *rule = g_object_get_data((GObject *)w, "rule");
		EAlert *alert = NULL;

		if (!e_filter_rule_validate (rule, &alert)) {
			e_alert_run_dialog (GTK_WINDOW (w), alert);
			g_object_unref (alert);
			return;
		}

		if (e_rule_context_find_rule ((ERuleContext *)context, rule->name, rule->source)) {
			e_alert_run_dialog_for_args (
				GTK_WINDOW (w), "mail:vfolder-notunique",
				rule->name, NULL);
			return;
		}

		g_object_ref (rule);
		e_rule_context_add_rule ((ERuleContext *)context, rule);
		config_dir = mail_session_get_config_dir ();
		user = g_build_filename (config_dir, "vfolders.xml", NULL);
		e_rule_context_save ((ERuleContext *)context, user);
		g_free (user);
	}

	gtk_widget_destroy (w);
}

static void
new_rule_changed_cb (EFilterRule *rule, GtkDialog *dialog)
{
	g_return_if_fail (rule != NULL);
	g_return_if_fail (dialog != NULL);

	gtk_dialog_set_response_sensitive (dialog, GTK_RESPONSE_OK, rule->parts != NULL);
}

EFilterPart *
vfolder_create_part (const gchar *name)
{
	return e_rule_context_create_part ((ERuleContext *)context, name);
}

/* clones a filter/search rule into a matching vfolder rule
 * (assuming the same system definitions) */
EFilterRule *
vfolder_clone_rule (EMailSession *session, EFilterRule *in)
{
	EFilterRule *rule;
	xmlNodePtr xml;

	rule = em_vfolder_rule_new (session);

	xml = e_filter_rule_xml_encode (in);
	e_filter_rule_xml_decode (rule, xml, (ERuleContext *)context);
	xmlFreeNodeList (xml);

	return rule;
}

/* adds a rule with a gui */
void
vfolder_gui_add_rule (EMVFolderRule *rule)
{
	GtkWidget *w;
	GtkDialog *gd;
	GtkWidget *container;

	w = e_filter_rule_get_widget ((EFilterRule *)rule, (ERuleContext *)context);

	gd = (GtkDialog *)gtk_dialog_new_with_buttons (
		_("New Search Folder"),
		NULL,
		GTK_DIALOG_DESTROY_WITH_PARENT,
		GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
		GTK_STOCK_OK, GTK_RESPONSE_OK, NULL);

	gtk_dialog_set_default_response (gd, GTK_RESPONSE_OK);
	gtk_container_set_border_width (GTK_CONTAINER (gd), 6);

	container = gtk_dialog_get_content_area (gd);
	gtk_box_set_spacing (GTK_BOX (container), 6);

	g_object_set (gd, "resizable", TRUE, NULL);
	gtk_window_set_default_size (GTK_WINDOW (gd), 500, 500);
	gtk_box_pack_start (GTK_BOX (container), w, TRUE, TRUE, 0);
	gtk_widget_show ((GtkWidget *)gd);
	g_object_set_data_full(G_OBJECT(gd), "rule", rule, (GDestroyNotify)g_object_unref);
	g_signal_connect(rule, "changed", G_CALLBACK (new_rule_changed_cb), gd);
	new_rule_changed_cb ((EFilterRule*)rule, gd);
	g_signal_connect(gd, "response", G_CALLBACK(new_rule_clicked), NULL);
	gtk_widget_show ((GtkWidget *)gd);
}

void
vfolder_gui_add_from_message (EMailSession *session,
                              CamelMimeMessage *message,
                              gint flags,
                              CamelFolder *folder)
{
	EMVFolderRule *rule;

	g_return_if_fail (CAMEL_IS_MIME_MESSAGE (message));

	rule = (EMVFolderRule*) em_vfolder_rule_from_message (
		context, message, flags, folder);
	vfolder_gui_add_rule (rule);
}

void
vfolder_gui_add_from_address (EMailSession *session,
                              CamelInternetAddress *addr,
                              gint flags,
                              CamelFolder *folder)
{
	EMVFolderRule *rule;

	g_return_if_fail (addr != NULL);

	rule = (EMVFolderRule*)em_vfolder_rule_from_address (
		context, addr, flags, folder);
	vfolder_gui_add_rule (rule);
}

static void
vfolder_foreach_cb (gpointer key, gpointer data, gpointer user_data)
{
	CamelFolder *folder = CAMEL_FOLDER (data);

	if (folder)
		g_object_unref (folder);

	g_free (key);
}

void
mail_vfolder_shutdown (void)
{
	vfolder_shutdown = 1;

	if (vfolder_hash) {
		g_hash_table_foreach (vfolder_hash, vfolder_foreach_cb, NULL);
		g_hash_table_destroy (vfolder_hash);
		vfolder_hash = NULL;
	}

	if (vfolder_store) {
		g_object_unref (vfolder_store);
		vfolder_store = NULL;
	}

	if (context) {
		g_object_unref (context);
		context = NULL;
	}
}
