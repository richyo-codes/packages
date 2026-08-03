// Copyright 2013 The Flutter Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/file_selector_linux/file_selector_plugin.h"

#include <flutter_linux/flutter_linux.h>
#include <gtk/gtk.h>

#if defined(FLUTTER_LINUX_GTK4)
#include <gio/gio.h>
#endif

#include "file_selector_plugin_private.h"
#include "messages.g.h"

// Error codes.
#if !defined(FLUTTER_LINUX_GTK4)
const char kBadArgumentsError[] = "Bad Arguments";
#endif
const char kNoScreenError[] = "No Screen";

struct _FlFileSelectorPlugin {
  GObject parent_instance;

  FlPluginRegistrar* registrar;
};

G_DEFINE_TYPE(FlFileSelectorPlugin, fl_file_selector_plugin, G_TYPE_OBJECT)

// Converts a type group received from Flutter into a GTK file filter.
static GtkFileFilter* type_group_to_filter(FfsPlatformTypeGroup* group) {
  g_autoptr(GtkFileFilter) filter = gtk_file_filter_new();

  const gchar* label = ffs_platform_type_group_get_label(group);
  gtk_file_filter_set_name(filter, label);

  FlValue* extensions = ffs_platform_type_group_get_extensions(group);
  for (size_t i = 0; i < fl_value_get_length(extensions); i++) {
    FlValue* v = fl_value_get_list_value(extensions, i);
    const gchar* pattern = fl_value_get_string(v);
    gtk_file_filter_add_pattern(filter, pattern);
  }
  FlValue* mime_types = ffs_platform_type_group_get_mime_types(group);
  for (size_t i = 0; i < fl_value_get_length(mime_types); i++) {
    FlValue* v = fl_value_get_list_value(mime_types, i);
    const gchar* pattern = fl_value_get_string(v);
    gtk_file_filter_add_mime_type(filter, pattern);
  }

  return GTK_FILE_FILTER(g_object_ref(filter));
}

#if defined(FLUTTER_LINUX_GTK4)

// GTK4's file dialog API is asynchronous, while the Pigeon API used by this
// plugin is synchronous. This context lets the handler wait in a nested main
// loop until the dialog completes.
struct FileDialogResult {
  GMainLoop* loop;
  FfsFileSelectorApiShowFileChooserResponse* response = nullptr;
};

static GListModel* type_groups_to_filters(
    FfsPlatformFileChooserOptions* options) {
  GListStore* filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
  FlValue* type_groups =
      ffs_platform_file_chooser_options_get_allowed_file_types(options);
  if (type_groups != nullptr) {
    for (size_t i = 0; i < fl_value_get_length(type_groups); i++) {
      FlValue* type_group = fl_value_get_list_value(type_groups, i);
      g_autoptr(GtkFileFilter) filter =
          type_group_to_filter(FFS_PLATFORM_TYPE_GROUP(
              fl_value_get_custom_value_object(type_group)));
      if (filter != nullptr) {
        g_list_store_append(filters, filter);
      }
    }
  }
  return G_LIST_MODEL(filters);
}

static void append_file_path(FlValue* filenames, GFile* file) {
  g_autofree gchar* path = g_file_get_path(file);
  if (path != nullptr) {
    fl_value_append_take(filenames, fl_value_new_string(path));
  }
}

static void handle_file_dialog_open(GObject* source_object,
                                    GAsyncResult* result, gpointer user_data) {
  FileDialogResult* dialog_result = static_cast<FileDialogResult*>(user_data);
  g_autoptr(GError) error = nullptr;
  g_autoptr(FlValue) filenames = fl_value_new_list();
  GFile* file = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(source_object),
                                            result, &error);
  if (file != nullptr) {
    append_file_path(filenames, file);
    g_object_unref(file);
  }
  dialog_result->response =
      ffs_file_selector_api_show_file_chooser_response_new(filenames);
  g_main_loop_quit(dialog_result->loop);
}

static void handle_file_dialog_open_multiple(GObject* source_object,
                                             GAsyncResult* result,
                                             gpointer user_data) {
  FileDialogResult* dialog_result = static_cast<FileDialogResult*>(user_data);
  g_autoptr(GError) error = nullptr;
  g_autoptr(FlValue) filenames = fl_value_new_list();
  g_autoptr(GListModel) files = gtk_file_dialog_open_multiple_finish(
      GTK_FILE_DIALOG(source_object), result, &error);
  if (files != nullptr) {
    for (guint i = 0; i < g_list_model_get_n_items(files); i++) {
      g_autoptr(GFile) file = G_FILE(g_list_model_get_item(files, i));
      append_file_path(filenames, file);
    }
  }
  dialog_result->response =
      ffs_file_selector_api_show_file_chooser_response_new(filenames);
  g_main_loop_quit(dialog_result->loop);
}

static void handle_file_dialog_folder(GObject* source_object,
                                      GAsyncResult* result,
                                      gpointer user_data) {
  FileDialogResult* dialog_result = static_cast<FileDialogResult*>(user_data);
  g_autoptr(GError) error = nullptr;
  g_autoptr(FlValue) filenames = fl_value_new_list();
  GFile* file = gtk_file_dialog_select_folder_finish(
      GTK_FILE_DIALOG(source_object), result, &error);
  if (file != nullptr) {
    append_file_path(filenames, file);
    g_object_unref(file);
  }
  dialog_result->response =
      ffs_file_selector_api_show_file_chooser_response_new(filenames);
  g_main_loop_quit(dialog_result->loop);
}

static void handle_file_dialog_multiple_folders(GObject* source_object,
                                                GAsyncResult* result,
                                                gpointer user_data) {
  FileDialogResult* dialog_result = static_cast<FileDialogResult*>(user_data);
  g_autoptr(GError) error = nullptr;
  g_autoptr(FlValue) filenames = fl_value_new_list();
  g_autoptr(GListModel) files = gtk_file_dialog_select_multiple_folders_finish(
      GTK_FILE_DIALOG(source_object), result, &error);
  if (files != nullptr) {
    for (guint i = 0; i < g_list_model_get_n_items(files); i++) {
      g_autoptr(GFile) file = G_FILE(g_list_model_get_item(files, i));
      append_file_path(filenames, file);
    }
  }
  dialog_result->response =
      ffs_file_selector_api_show_file_chooser_response_new(filenames);
  g_main_loop_quit(dialog_result->loop);
}

static void handle_file_dialog_save(GObject* source_object,
                                    GAsyncResult* result, gpointer user_data) {
  FileDialogResult* dialog_result = static_cast<FileDialogResult*>(user_data);
  g_autoptr(GError) error = nullptr;
  g_autoptr(FlValue) filenames = fl_value_new_list();
  GFile* file = gtk_file_dialog_save_finish(GTK_FILE_DIALOG(source_object),
                                            result, &error);
  if (file != nullptr) {
    append_file_path(filenames, file);
    g_object_unref(file);
  }
  dialog_result->response =
      ffs_file_selector_api_show_file_chooser_response_new(filenames);
  g_main_loop_quit(dialog_result->loop);
}

static FfsFileSelectorApiShowFileChooserResponse* handle_show_file_chooser(
    FfsPlatformFileChooserActionType type,
    FfsPlatformFileChooserOptions* options, gpointer user_data) {
  FlFileSelectorPlugin* self = FL_FILE_SELECTOR_PLUGIN(user_data);
  FlView* view = fl_plugin_registrar_get_view(self->registrar);
  if (view == nullptr) {
    return ffs_file_selector_api_show_file_chooser_response_new_error(
        kNoScreenError, nullptr, nullptr);
  }

  GtkWindow* window = GTK_WINDOW(gtk_widget_get_root(GTK_WIDGET(view)));
  g_autoptr(GtkFileDialog) dialog = gtk_file_dialog_new();
  gtk_file_dialog_set_title(
      dialog,
      type == FILE_SELECTOR_LINUX_PLATFORM_FILE_CHOOSER_ACTION_TYPE_SAVE
          ? "Save File"
      : type ==
              FILE_SELECTOR_LINUX_PLATFORM_FILE_CHOOSER_ACTION_TYPE_CHOOSE_DIRECTORY
          ? "Choose Directory"
          : "Open File");
  const gchar* accept_label =
      ffs_platform_file_chooser_options_get_accept_button_label(options);
  if (accept_label != nullptr)
    gtk_file_dialog_set_accept_label(dialog, accept_label);
  const gchar* current_folder =
      ffs_platform_file_chooser_options_get_current_folder_path(options);
  if (current_folder != nullptr) {
    g_autoptr(GFile) folder = g_file_new_for_path(current_folder);
    gtk_file_dialog_set_initial_folder(dialog, folder);
  }
  const gchar* current_name =
      ffs_platform_file_chooser_options_get_current_name(options);
  if (current_name != nullptr)
    gtk_file_dialog_set_initial_name(dialog, current_name);
  // GtkFileDialog does not expose GTK3's create-folders property. GTK4's
  // native dialog controls folder creation itself, so there is no equivalent
  // option to apply here.
  g_autoptr(GListModel) filters = type_groups_to_filters(options);
  if (g_list_model_get_n_items(filters) > 0)
    gtk_file_dialog_set_filters(dialog, filters);

  const gboolean* select_multiple =
      ffs_platform_file_chooser_options_get_select_multiple(options);
  gboolean multiple = select_multiple != nullptr && *select_multiple;
  FileDialogResult dialog_result = {g_main_loop_new(nullptr, FALSE), nullptr};
  if (type == FILE_SELECTOR_LINUX_PLATFORM_FILE_CHOOSER_ACTION_TYPE_OPEN) {
    if (multiple) {
      gtk_file_dialog_open_multiple(dialog, window, nullptr,
                                    handle_file_dialog_open_multiple,
                                    &dialog_result);
    } else {
      gtk_file_dialog_open(dialog, window, nullptr, handle_file_dialog_open,
                           &dialog_result);
    }
  } else if (
      type ==
      FILE_SELECTOR_LINUX_PLATFORM_FILE_CHOOSER_ACTION_TYPE_CHOOSE_DIRECTORY) {
    if (multiple) {
      gtk_file_dialog_select_multiple_folders(
          dialog, window, nullptr, handle_file_dialog_multiple_folders,
          &dialog_result);
    } else {
      gtk_file_dialog_select_folder(dialog, window, nullptr,
                                    handle_file_dialog_folder, &dialog_result);
    }
  } else {
    gtk_file_dialog_save(dialog, window, nullptr, handle_file_dialog_save,
                         &dialog_result);
  }
  g_main_loop_run(dialog_result.loop);
  g_main_loop_unref(dialog_result.loop);
  return dialog_result.response;
}

#else

// Creates a GtkFileChooserNative for the given method call details.
static GtkFileChooserNative* create_dialog(
    GtkWindow* window, GtkFileChooserAction action, const gchar* title,
    const gchar* default_confirm_button_text,
    FfsPlatformFileChooserOptions* options) {
  const gchar* confirm_button_text =
      ffs_platform_file_chooser_options_get_accept_button_label(options);
  if (confirm_button_text == nullptr) {
    confirm_button_text = default_confirm_button_text;
  }

  g_autoptr(GtkFileChooserNative) dialog =
      GTK_FILE_CHOOSER_NATIVE(gtk_file_chooser_native_new(
          title, window, action, confirm_button_text, "_Cancel"));

  const gboolean* select_multiple =
      ffs_platform_file_chooser_options_get_select_multiple(options);
  if (select_multiple != nullptr) {
    gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(dialog),
                                         *select_multiple);
  }

  const gchar* current_folder =
      ffs_platform_file_chooser_options_get_current_folder_path(options);
  if (current_folder != nullptr) {
    gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog),
                                        current_folder);
  }

  const gchar* current_name =
      ffs_platform_file_chooser_options_get_current_name(options);
  if (current_name != nullptr) {
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), current_name);
  }

  FlValue* type_groups =
      ffs_platform_file_chooser_options_get_allowed_file_types(options);
  if (type_groups != nullptr) {
    for (size_t i = 0; i < fl_value_get_length(type_groups); i++) {
      FlValue* type_group = fl_value_get_list_value(type_groups, i);
      GtkFileFilter* filter = type_group_to_filter(FFS_PLATFORM_TYPE_GROUP(
          fl_value_get_custom_value_object(type_group)));
      if (filter == nullptr) {
        return nullptr;
      }
      gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);
    }
  }

  const gboolean* create_folders =
      ffs_platform_file_chooser_options_get_create_folders(options);
  if (create_folders != nullptr) {
    gtk_file_chooser_set_create_folders(GTK_FILE_CHOOSER(dialog),
                                        *create_folders);
  }

  return GTK_FILE_CHOOSER_NATIVE(g_object_ref(dialog));
}

GtkFileChooserNative* create_dialog_of_type(
    GtkWindow* window, FfsPlatformFileChooserActionType type,
    FfsPlatformFileChooserOptions* options) {
  switch (type) {
    case FILE_SELECTOR_LINUX_PLATFORM_FILE_CHOOSER_ACTION_TYPE_OPEN:
      return create_dialog(window, GTK_FILE_CHOOSER_ACTION_OPEN, "Open File",
                           "_Open", options);
    case FILE_SELECTOR_LINUX_PLATFORM_FILE_CHOOSER_ACTION_TYPE_CHOOSE_DIRECTORY:
      return create_dialog(window, GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
                           "Choose Directory", "_Open", options);
    case FILE_SELECTOR_LINUX_PLATFORM_FILE_CHOOSER_ACTION_TYPE_SAVE:
      return create_dialog(window, GTK_FILE_CHOOSER_ACTION_SAVE, "Save File",
                           "_Save", options);
  }
  return nullptr;
}

// Shows the requested dialog type.
static FfsFileSelectorApiShowFileChooserResponse* handle_show_file_chooser(
    FfsPlatformFileChooserActionType type,
    FfsPlatformFileChooserOptions* options, gpointer user_data) {
  FlFileSelectorPlugin* self = FL_FILE_SELECTOR_PLUGIN(user_data);

  FlView* view = fl_plugin_registrar_get_view(self->registrar);
  if (view == nullptr) {
    return ffs_file_selector_api_show_file_chooser_response_new_error(
        kNoScreenError, nullptr, nullptr);
  }
  GtkWindow* window = GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(view)));

  g_autoptr(GtkFileChooserNative) dialog =
      create_dialog_of_type(window, type, options);

  if (dialog == nullptr) {
    return ffs_file_selector_api_show_file_chooser_response_new_error(
        kBadArgumentsError, "Unable to create dialog from arguments", nullptr);
  }
  return show_file_chooser(GTK_FILE_CHOOSER_NATIVE(dialog),
                           gtk_native_dialog_run);
}

FfsFileSelectorApiShowFileChooserResponse* show_file_chooser(
    GtkFileChooserNative* dialog, gint (*run_dialog)(GtkNativeDialog*)) {
  gint response = run_dialog(GTK_NATIVE_DIALOG(dialog));
  g_autoptr(FlValue) result = fl_value_new_list();
  if (response == GTK_RESPONSE_ACCEPT) {
    g_autoptr(GSList) filenames =
        gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER(dialog));
    for (GSList* link = filenames; link != nullptr; link = link->next) {
      g_autofree gchar* filename = static_cast<gchar*>(link->data);
      fl_value_append_take(result, fl_value_new_string(filename));
    }
  }

  return ffs_file_selector_api_show_file_chooser_response_new(result);
}

#endif

static void fl_file_selector_plugin_dispose(GObject* object) {
  FlFileSelectorPlugin* self = FL_FILE_SELECTOR_PLUGIN(object);

  ffs_file_selector_api_clear_method_handlers(
      fl_plugin_registrar_get_messenger(self->registrar), nullptr);
  g_clear_object(&self->registrar);

  G_OBJECT_CLASS(fl_file_selector_plugin_parent_class)->dispose(object);
}

static void fl_file_selector_plugin_class_init(
    FlFileSelectorPluginClass* klass) {
  G_OBJECT_CLASS(klass)->dispose = fl_file_selector_plugin_dispose;
}

static void fl_file_selector_plugin_init(FlFileSelectorPlugin* self) {}

FlFileSelectorPlugin* fl_file_selector_plugin_new(
    FlPluginRegistrar* registrar) {
  FlFileSelectorPlugin* self = FL_FILE_SELECTOR_PLUGIN(
      g_object_new(fl_file_selector_plugin_get_type(), nullptr));

  self->registrar = FL_PLUGIN_REGISTRAR(g_object_ref(registrar));

  static FfsFileSelectorApiVTable api_vtable = {
      .show_file_chooser = handle_show_file_chooser,
  };
  ffs_file_selector_api_set_method_handlers(
      fl_plugin_registrar_get_messenger(registrar), nullptr, &api_vtable,
      g_object_ref(self), g_object_unref);

  return self;
}

void file_selector_plugin_register_with_registrar(
    FlPluginRegistrar* registrar) {
  FlFileSelectorPlugin* plugin = fl_file_selector_plugin_new(registrar);
  g_object_unref(plugin);
}
