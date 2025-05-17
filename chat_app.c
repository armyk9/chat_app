#include <gtk/gtk.h>
#include <gio/gio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

#define BUFFER_SIZE 1024

typedef struct {
    GtkWidget *text_view;
    GtkWidget *entry_name;
    GtkWidget *entry_ip;
    GtkWidget *entry_port;
    GtkWidget *entry_message;
    GtkWidget *radio_client;
    GtkWidget *radio_server;
    GtkWidget *button_connect;
    GtkWidget *button_send;
    GtkWidget *button_listen;
    GtkWidget *button_cancel;
    int socket_fd;
    gboolean connected;
    GThread *net_thread;
} AppWidgets;

gboolean append_text_to_view(gpointer data) {
    AppWidgets *widgets = (AppWidgets *)data;
    const char *msg = g_object_get_data(G_OBJECT(widgets->text_view), "incoming_msg");

    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(widgets->text_view));
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buffer, &end);
    gtk_text_buffer_insert(buffer, &end, msg, -1);
    gtk_text_buffer_insert(buffer, &end, "\n", -1);

    return G_SOURCE_REMOVE;
}

gpointer server_thread(gpointer data) {
    AppWidgets *widgets = (AppWidgets *)data;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    char buffer[BUFFER_SIZE];

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(atoi(gtk_editable_get_text(GTK_EDITABLE(widgets->entry_port))));

    bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr));
    listen(server_fd, 1);
    g_idle_add((GSourceFunc)gtk_widget_set_sensitive, widgets->button_listen);
    gtk_widget_set_sensitive(widgets->button_listen, FALSE);

    g_idle_add((GSourceFunc)gtk_widget_set_sensitive, widgets->button_listen);
    gtk_widget_set_sensitive(widgets->button_listen, FALSE);

    g_object_set_data(G_OBJECT(widgets->text_view), "incoming_msg", "Waiting for client to connect...");
    g_idle_add(append_text_to_view, widgets);

    int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
    widgets->socket_fd = client_fd;
    widgets->connected = TRUE;
    g_idle_add((GSourceFunc)gtk_widget_set_sensitive, widgets->button_send);

    while (1) {
        memset(buffer, 0, BUFFER_SIZE);
        int len = recv(client_fd, buffer, BUFFER_SIZE - 1, 0);
        if (len <= 0) break;

        g_object_set_data(G_OBJECT(widgets->text_view), "incoming_msg", g_strdup(buffer));
        g_idle_add(append_text_to_view, widgets);
    }

    close(client_fd);
    close(server_fd);
    return NULL;
}

gpointer client_thread(gpointer data) {
    AppWidgets *widgets = (AppWidgets *)data;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(atoi(gtk_editable_get_text(GTK_EDITABLE(widgets->entry_port))));
    inet_pton(AF_INET, gtk_editable_get_text(GTK_EDITABLE(widgets->entry_ip)), &server_addr.sin_addr);

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) == 0) {
        widgets->socket_fd = sock;
        widgets->connected = TRUE;
        g_idle_add((GSourceFunc)gtk_widget_set_sensitive, widgets->button_send);
        g_idle_add((GSourceFunc)gtk_widget_set_sensitive, widgets->button_connect);
        g_idle_add((GSourceFunc)gtk_widget_set_sensitive, widgets->button_listen);
        gtk_widget_set_sensitive(widgets->button_connect, FALSE);
        gtk_widget_set_sensitive(widgets->button_listen, FALSE);
        widgets->socket_fd = sock;
        widgets->connected = TRUE;
        g_idle_add((GSourceFunc)gtk_widget_set_sensitive, widgets->button_send);
        g_idle_add((GSourceFunc)gtk_widget_set_sensitive, widgets->button_connect);
        gtk_widget_set_sensitive(widgets->button_connect, FALSE);
    }

    while (1) {
        memset(buffer, 0, BUFFER_SIZE);
        int len = recv(sock, buffer, BUFFER_SIZE - 1, 0);
        if (len <= 0) break;

        g_object_set_data(G_OBJECT(widgets->text_view), "incoming_msg", g_strdup(buffer));
        g_idle_add(append_text_to_view, widgets);
    }

    close(sock);
    return NULL;
}

static void on_connect_clicked(GtkButton *button, gpointer user_data) {
    AppWidgets *widgets = (AppWidgets *)user_data;
    if (!widgets->connected) {
        widgets->net_thread = g_thread_new("client", client_thread, widgets);
    }
}

static void on_listen_clicked(GtkButton *button, gpointer user_data) {
    AppWidgets *widgets = (AppWidgets *)user_data;
    if (!widgets->connected) {
        widgets->net_thread = g_thread_new("server", server_thread, widgets);
        gtk_widget_set_sensitive(GTK_WIDGET(button), FALSE);
    }
}

static void on_send_clicked(GtkButton *button, gpointer user_data) {
    AppWidgets *widgets = (AppWidgets *)user_data;
    if (!widgets->connected) return;

    const gchar *msg = gtk_editable_get_text(GTK_EDITABLE(widgets->entry_message));
    const gchar *name = gtk_editable_get_text(GTK_EDITABLE(widgets->entry_name));

    if (strlen(msg) > 0) {
        char full_msg[BUFFER_SIZE];
        snprintf(full_msg, BUFFER_SIZE, "%s: %s", name, msg);
        send(widgets->socket_fd, full_msg, strlen(full_msg), 0);

        GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(widgets->text_view));
        GtkTextIter end;
        gtk_text_buffer_get_end_iter(buffer, &end);
        gtk_text_buffer_insert(buffer, &end, full_msg, -1);
        gtk_text_buffer_insert(buffer, &end, "\n", -1);

        gtk_editable_set_text(GTK_EDITABLE(widgets->entry_message), "");
    }
}

static gboolean on_entry_key_press(GtkEventControllerKey *controller, guint keyval, guint keycode, GdkModifierType state, gpointer user_data) {
    if (keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter) {
        on_send_clicked(NULL, user_data);
        return TRUE;
    }
    return FALSE;
}

static void on_cancel_clicked(GtkButton *button, gpointer user_data) {
    AppWidgets *widgets = (AppWidgets *)user_data;
    if (widgets->connected)
        close(widgets->socket_fd);
    gtk_window_close(GTK_WINDOW(gtk_widget_get_ancestor(GTK_WIDGET(button), GTK_TYPE_WINDOW)));
}

static void on_mode_toggled(GtkToggleButton *button, gpointer user_data) {
    AppWidgets *widgets = (AppWidgets *)user_data;
    gboolean is_client = gtk_check_button_get_active(GTK_CHECK_BUTTON(widgets->radio_client));

    gtk_widget_set_sensitive(widgets->entry_ip, is_client);
    gtk_widget_set_sensitive(widgets->button_connect, is_client);
    gtk_widget_set_sensitive(widgets->button_listen, !is_client);
    gtk_widget_set_sensitive(widgets->button_send, FALSE);
}

static void activate(GtkApplication *app, gpointer user_data) {
    GtkWidget *window, *vbox, *scroll, *text_view;
    GtkWidget *entry_name, *entry_ip, *entry_port, *entry_msg;
    GtkWidget *radio_client, *radio_server, *button_connect, *button_send, *button_listen, *button_cancel;
    GtkWidget *hbox_name, *hbox_ip, *hbox_mode, *hbox_btn;

    AppWidgets *widgets = g_malloc0(sizeof(AppWidgets));

    window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "TCP Chat App");
    gtk_window_set_default_size(GTK_WINDOW(window), 400, 450);

    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_window_set_child(GTK_WINDOW(window), vbox);

    text_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(text_view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text_view), GTK_WRAP_WORD_CHAR);
    scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), text_view);
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_box_append(GTK_BOX(vbox), scroll);

    hbox_name = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    entry_name = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry_name), "Your Name");
    gtk_box_append(GTK_BOX(hbox_name), entry_name);
    gtk_box_append(GTK_BOX(vbox), hbox_name);

    hbox_ip = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    entry_ip = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry_ip), "Destination IP");
    entry_port = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry_port), "Port");
    gtk_box_append(GTK_BOX(hbox_ip), entry_ip);
    gtk_box_append(GTK_BOX(hbox_ip), entry_port);
    gtk_box_append(GTK_BOX(vbox), hbox_ip);

    hbox_mode = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    radio_client = gtk_check_button_new_with_label("Client");
    radio_server = gtk_check_button_new_with_label("Server");
    gtk_check_button_set_group(GTK_CHECK_BUTTON(radio_server), GTK_CHECK_BUTTON(radio_client));
    gtk_check_button_set_active(GTK_CHECK_BUTTON(radio_client), TRUE);
    gtk_box_append(GTK_BOX(hbox_mode), radio_client);
    gtk_box_append(GTK_BOX(hbox_mode), radio_server);
    gtk_box_append(GTK_BOX(vbox), hbox_mode);

    entry_msg = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry_msg), "Type your message...");
    gtk_box_append(GTK_BOX(vbox), entry_msg);

    GtkEventController *controller = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(controller, GTK_PHASE_CAPTURE);
    g_signal_connect(controller, "key-pressed", G_CALLBACK(on_entry_key_press), widgets);
    gtk_widget_add_controller(entry_msg, controller);

    hbox_btn = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    button_connect = gtk_button_new_with_label("Connect");
    button_send = gtk_button_new_with_label("Send");
    button_listen = gtk_button_new_with_label("Listen");
    button_cancel = gtk_button_new_with_label("Cancel");

    gtk_widget_set_hexpand(button_connect, TRUE);
    gtk_widget_set_hexpand(button_send, TRUE);
    gtk_widget_set_hexpand(button_listen, TRUE);
    gtk_widget_set_hexpand(button_cancel, TRUE);

    gtk_box_append(GTK_BOX(hbox_btn), button_connect);
    gtk_box_append(GTK_BOX(hbox_btn), button_send);
    gtk_box_append(GTK_BOX(hbox_btn), button_listen);
    gtk_box_append(GTK_BOX(hbox_btn), button_cancel);
    gtk_box_append(GTK_BOX(vbox), hbox_btn);

    widgets->text_view = text_view;
    widgets->entry_name = entry_name;
    widgets->entry_ip = entry_ip;
    widgets->entry_port = entry_port;
    widgets->entry_message = entry_msg;
    widgets->radio_client = radio_client;
    widgets->radio_server = radio_server;
    widgets->button_connect = button_connect;
    widgets->button_send = button_send;
    widgets->button_listen = button_listen;
    widgets->button_cancel = button_cancel;
    widgets->connected = FALSE;

    gtk_widget_set_sensitive(button_connect, TRUE);
    gtk_widget_set_sensitive(button_listen, TRUE);
    gtk_widget_set_sensitive(button_send, FALSE);
    gtk_widget_set_sensitive(button_listen, FALSE);

    g_signal_connect(button_connect, "clicked", G_CALLBACK(on_connect_clicked), widgets);
    g_signal_connect(button_send, "clicked", G_CALLBACK(on_send_clicked), widgets);
    g_signal_connect(button_listen, "clicked", G_CALLBACK(on_listen_clicked), widgets);
    g_signal_connect(button_cancel, "clicked", G_CALLBACK(on_cancel_clicked), widgets);
    g_signal_connect(radio_client, "toggled", G_CALLBACK(on_mode_toggled), widgets);
    g_signal_connect(radio_server, "toggled", G_CALLBACK(on_mode_toggled), widgets);

    gtk_window_present(GTK_WINDOW(window));
}

int main(int argc, char **argv) {
    GtkApplication *app;
    int status;

    app = gtk_application_new(NULL, G_APPLICATION_FLAGS_NONE);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
