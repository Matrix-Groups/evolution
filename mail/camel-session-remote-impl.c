/*  
 * Authors: Srinivasa Ragavan <sragavan@novell.com>
 *
 * */

#include <string.h>
#include <glib.h>
#include <glib/gi18n.h>
#include "mail-dbus.h"
#include <camel/camel-session.h>
#include <camel/camel-store.h>
#include <camel/camel.h>

#define CAMEL_SESSION_OBJECT_PATH "/org/gnome/evolution/camel/session"

static gboolean session_setup = FALSE;

GHashTable *store_hash = NULL;
extern CamelSession *session;

static DBusHandlerResult
dbus_listener_message_handler (DBusConnection *connection,
                                    DBusMessage    *message,
                                    void           *user_data);

void 
camel_session_remote_impl_init (void);

static DBusHandlerResult
dbus_listener_message_handler (DBusConnection *connection,
                                    DBusMessage    *message,
                                    void           *user_data)
{
	const char *method = dbus_message_get_member (message);
	DBusMessage *return_val;
	CamelStore *store;
	char *store_not_found = _("Store not found");

  	printf ("D-Bus message: obj_path = '%s' interface = '%s' method = '%s' destination = '%s'\n",
           dbus_message_get_path (message),
           dbus_message_get_interface (message),
           dbus_message_get_member (message),
           dbus_message_get_destination (message));
	
	
	return_val = dbus_message_new_method_return (message);

	if (strcmp(method, "camel_session_construct") == 0) {
		char *storage_path;
		char *session_str;
		gboolean ret;

		ret = dbus_message_get_args(message, NULL,
				DBUS_TYPE_STRING, &session_str,
				DBUS_TYPE_STRING, &storage_path,
				DBUS_TYPE_INVALID);

		camel_session_construct (session, storage_path);
		dbus_message_append_args (return_val, DBUS_TYPE_INVALID);
	
	} else if (strcmp(method, "camel_session_get_password") == 0) {
		char *session_str, *store_hash_key, *domain, *prompt, *item, *err;
		const char *passwd;
		guint32 flags;
		gboolean ret;
		CamelException *ex;

		ex = camel_exception_new ();

		ret = dbus_message_get_args(message, NULL,
				DBUS_TYPE_STRING, &session_str,
				DBUS_TYPE_STRING, &store_hash_key,
				DBUS_TYPE_STRING, &domain,
				DBUS_TYPE_STRING, &prompt,
				DBUS_TYPE_STRING, &item,
				DBUS_TYPE_UINT32, &flags,
				DBUS_TYPE_INVALID);

		store = g_hash_table_lookup (store_hash, store_hash_key);
		if (!store) {
			dbus_message_append_args (return_val, DBUS_TYPE_STRING, "", DBUS_TYPE_STRING, &(store_not_found), DBUS_TYPE_INVALID);
			goto fail;
		}

		camel_exception_init (ex);
		
		passwd = camel_session_get_password (session, (CamelService *)store, domain, prompt, item, flags, ex);

		if (ex)
			err = g_strdup (camel_exception_get_description (ex));
		else
			err = g_strdup ("");
		camel_exception_free (ex);
			
		dbus_message_append_args (return_val, DBUS_TYPE_STRING, &passwd, DBUS_TYPE_STRING, &err, DBUS_TYPE_INVALID);
		g_free (err);
	} else if (strcmp (method, "camel_session_get_storage_path") == 0) {
		char *session_str, *store_hash_key, *storage_path, *err;
		gboolean ret;
		CamelException *ex;

		ex = camel_exception_new ();

		ret = dbus_message_get_args(message, NULL,
				DBUS_TYPE_STRING, &session_str,
				DBUS_TYPE_STRING, &store_hash_key,
				DBUS_TYPE_INVALID);
		
		store = g_hash_table_lookup (store_hash, store_hash_key);
		if (!store) {
			dbus_message_append_args (return_val, DBUS_TYPE_STRING, "", DBUS_TYPE_STRING, &(store_not_found), DBUS_TYPE_INVALID);
			goto fail;
		}

		camel_exception_init (ex);
		
		storage_path = camel_session_get_storage_path (session, (CamelService *)store, ex);

		if (ex)
			err = g_strdup (camel_exception_get_description (ex));
		else
			err = g_strdup ("");

		camel_exception_free (ex);
			
		dbus_message_append_args (return_val, DBUS_TYPE_STRING, &storage_path, DBUS_TYPE_STRING, &err, DBUS_TYPE_INVALID);
		g_free (err);
	} else if (strcmp (method, "camel_session_forget_password") == 0) {
		char *session_str, *store_hash_key, *domain, *item, *err;
		gboolean ret;
		CamelException *ex;

		ex = camel_exception_new ();

		ret = dbus_message_get_args(message, NULL,
				DBUS_TYPE_STRING, &session_str,
				DBUS_TYPE_STRING, &store_hash_key,
				DBUS_TYPE_STRING, &domain,
				DBUS_TYPE_STRING, &item,				
				DBUS_TYPE_INVALID);
		
		store = g_hash_table_lookup (store_hash, store_hash_key);
		if (!store) {
			dbus_message_append_args (return_val, DBUS_TYPE_STRING, &(store_not_found), DBUS_TYPE_INVALID);
			goto fail;
		}

		camel_exception_init (ex);
		
		camel_session_forget_password (session, (CamelService *)store, domain, item, ex);

		if (ex)
			err = g_strdup (camel_exception_get_description (ex));
		else
			err = g_strdup ("");

		camel_exception_free (ex);
			
		dbus_message_append_args (return_val, DBUS_TYPE_STRING, &err, DBUS_TYPE_INVALID);
		g_free (err);
	} else if (strcmp (method, "camel_session_get_service") == 0) {
		char *url_string, *err, *store_url;
		char *store_hash_key;
		CamelProviderType type;
		CamelService *service;
		gboolean ret;
		CamelException *ex;

		ex = camel_exception_new ();

		ret = dbus_message_get_args(message, NULL,
				DBUS_TYPE_STRING, &url_string,
				DBUS_TYPE_INT32, &type,
				DBUS_TYPE_INVALID);
		
		service = camel_session_get_service (session, url_string, type, ex);

		store_url = camel_service_get_url (service);

		store_hash_key = e_dbus_get_store_hash (store_url);

		g_hash_table_insert (store_hash, store_hash_key, service);

		if (ex)
			err = g_strdup (camel_exception_get_description (ex));
		else
			err = g_strdup ("");

		camel_exception_free (ex);
			
		dbus_message_append_args (return_val, DBUS_TYPE_STRING, &store_hash_key, DBUS_TYPE_STRING, &err, DBUS_TYPE_INVALID);
		printf("%s: Success: %s:%s\n", method, store_hash_key, err);
		g_free (err);
	} else if (strcmp (method, "camel_session_alert_user") == 0) {
		char *session_str, *prompt, *err = NULL;
		gboolean ret, cancel, response;
		int alert;

		ret = dbus_message_get_args(message, NULL,
				DBUS_TYPE_STRING, &session_str,
				DBUS_TYPE_INT32, &alert,
				DBUS_TYPE_STRING, &prompt,
				DBUS_TYPE_INT32, &cancel,
				DBUS_TYPE_INVALID);
		
		response = camel_session_alert_user (session, alert, prompt, cancel);

		dbus_message_append_args (return_val, DBUS_TYPE_INT32, &response, DBUS_TYPE_INVALID);
		g_free (err);
	} else if (strcmp (method, "camel_session_build_password_prompt") == 0) {
		gboolean ret;
		char *type, *user, *host, *prompt, *err = NULL;
		
		ret = dbus_message_get_args(message, NULL,
				DBUS_TYPE_STRING, &type,
				DBUS_TYPE_STRING, &user,
				DBUS_TYPE_STRING, &host,
				DBUS_TYPE_INVALID);
		
		prompt = camel_session_build_password_prompt (type, user, host);

		dbus_message_append_args (return_val, DBUS_TYPE_STRING, &prompt, DBUS_TYPE_INVALID);
		g_free (err);
	} else if (strcmp (method, "camel_session_is_online") == 0) {
		gboolean ret, is_online;
		char *session_str, *err = NULL;
		
		ret = dbus_message_get_args(message, NULL,
				DBUS_TYPE_STRING, &session_str,
				DBUS_TYPE_INVALID);
		
		is_online = camel_session_is_online (session);

		dbus_message_append_args (return_val, DBUS_TYPE_INT32, &is_online, DBUS_TYPE_INVALID);
		g_free (err);
	} else if (strcmp (method, "camel_session_set_online") == 0) {
		gboolean ret, set;
		char *session_str, *err = NULL;
		
		ret = dbus_message_get_args(message, NULL,
				DBUS_TYPE_STRING, &session_str,
				DBUS_TYPE_INT32, &set,
				DBUS_TYPE_INVALID);
		
		camel_session_set_online (session, set);

		dbus_message_append_args (return_val, DBUS_TYPE_INVALID);
		g_free (err);
	} else if (strcmp (method, "camel_session_check_junk") == 0) {
		gboolean ret, check_junk;
		char *session_str, *err = NULL;
		
		ret = dbus_message_get_args(message, NULL,
				DBUS_TYPE_STRING, &session_str,
				DBUS_TYPE_INVALID);
		
		check_junk = camel_session_check_junk (session);

		dbus_message_append_args (return_val, DBUS_TYPE_INT32, &check_junk, DBUS_TYPE_INVALID);
		g_free (err);
	} else if (strcmp (method, "camel_session_set_check_junk") == 0) {
		gboolean ret, set_check;
		char *session_str, *err = NULL;
		
		ret = dbus_message_get_args(message, NULL,
				DBUS_TYPE_STRING, &session_str,
				DBUS_TYPE_INT32, &set_check,
				DBUS_TYPE_INVALID);
		
		camel_session_set_online (session, set_check);

		dbus_message_append_args (return_val, DBUS_TYPE_INVALID);
		g_free (err);
	} else if (strcmp (method, "camel_session_get_network_state") == 0) {
		gboolean ret, network_state;
		char *session_str, *err = NULL;
		
		ret = dbus_message_get_args(message, NULL,
				DBUS_TYPE_STRING, &session_str,
				DBUS_TYPE_INVALID);
		
		network_state = camel_session_get_network_state (session);

		dbus_message_append_args (return_val, DBUS_TYPE_INT32, &network_state, DBUS_TYPE_INVALID);
		g_free (err);
	} else if (strcmp (method, "camel_session_set_network_state") == 0) {
		gboolean ret, set_network;
		char *session_str, *err = NULL;
		
		ret = dbus_message_get_args(message, NULL,
				DBUS_TYPE_STRING, &session_str,
				DBUS_TYPE_INT32, &set_network,
				DBUS_TYPE_INVALID);
		
		camel_session_set_online (session, set_network);

		dbus_message_append_args (return_val, DBUS_TYPE_INVALID);
		g_free (err);
	} else
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;


fail:
	dbus_connection_send (connection, return_val, NULL);
	dbus_message_unref (return_val);
	dbus_connection_flush(connection);

	return DBUS_HANDLER_RESULT_HANDLED;

}

void
camel_session_remote_impl_init ()
{
	session_setup = TRUE;
	store_hash = g_hash_table_new (g_str_hash, g_str_equal);
	e_dbus_register_handler (CAMEL_SESSION_OBJECT_PATH, dbus_listener_message_handler, NULL);
}
