/*
 * QEMU live migration
 *
 * Copyright IBM, Corp. 2008
 * Copyright Dell MessageOne 2008
 * Copyright Red Hat, Inc. 2015-2016
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *  Charles Duffy     <charles_duffy@messageone.com>
 *  Daniel P. Berrange <berrange@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "channel.h"
#include "exec.h"
#include "migration.h"
#include "io/channel-command.h"
#include "trace.h"
#include "qemu/cutils.h"

#ifdef WIN32
const char *exec_get_cmd_path(void)
{
    g_autofree char *detected_path = g_new(char, MAX_PATH);
    if (GetSystemDirectoryA(detected_path, MAX_PATH) == 0) {
        warn_report("Could not detect cmd.exe path, using default.");
        return "C:\\Windows\\System32\\cmd.exe";
    }
    pstrcat(detected_path, MAX_PATH, "\\cmd.exe");
    return g_steal_pointer(&detected_path);
}
#endif

/* provides the length of strList */
static int
str_list_length(strList *list)
{
    int len = 0;
    strList *elem;

    for (elem = list; elem != NULL; elem = elem->next) {
        len++;
    }

    return len;
}

static void
init_exec_array(strList *command, char **argv, Error **errp)
{
    int i = 0;
    strList *lst;

    for (lst = command; lst; lst = lst->next) {
        argv[i++] = lst->value;
    }

    argv[i] = NULL;
    return;
}

void exec_start_outgoing_migration(MigrationState *s, strList *command,
                                   Error **errp)
{
    QIOChannel *ioc;

    int length = str_list_length(command);
    g_auto(GStrv) argv = (char **) g_new0(const char *, length + 1);

    init_exec_array(command, argv, errp);
    g_autofree char *new_command = g_strjoinv(" ", (char **)argv);

    trace_migration_exec_outgoing(new_command);
    ioc = QIO_CHANNEL(
        qio_channel_command_new_spawn(
                            (const char * const *) g_steal_pointer(&argv),
                            O_RDWR,
                            errp));
    if (!ioc) {
        return;
    }

    qio_channel_set_name(ioc, "migration-exec-outgoing");
    migration_channel_connect(s, ioc, NULL, NULL);
    object_unref(OBJECT(ioc));
}

static gboolean exec_accept_incoming_migration(QIOChannel *ioc,
                                               GIOCondition condition,
                                               gpointer opaque)
{
    migration_channel_process_incoming(ioc);
    object_unref(OBJECT(ioc));
    return G_SOURCE_REMOVE;
}

void exec_start_incoming_migration(strList *command, Error **errp)
{
    QIOChannel *ioc;

    int length = str_list_length(command);
    g_auto(GStrv) argv = (char **) g_new0(const char *, length + 1);

    init_exec_array(command, argv, errp);
    g_autofree char *new_command = g_strjoinv(" ", (char **)argv);

    trace_migration_exec_incoming(new_command);
    ioc = QIO_CHANNEL(
        qio_channel_command_new_spawn(
                            (const char * const *) g_steal_pointer(&argv),
                            O_RDWR,
                            errp));
    if (!ioc) {
        return;
    }

    qio_channel_set_name(ioc, "migration-exec-incoming");
    qio_channel_add_watch_full(ioc, G_IO_IN,
                               exec_accept_incoming_migration,
                               NULL, NULL,
                               g_main_context_get_thread_default());
}
