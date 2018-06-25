#define _GNU_SOURCE
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/socket.h>

#include "amp.h"
#include "commander.h"
#include "fimnotify.h"

#define PORT 8080
#define DEFAULT_NS "mnt"

static pid_t target_pid = -1;
static char *target_ns = DEFAULT_NS;
static char *target_paths[32] = {NULL};
static unsigned int target_pathc = 0;
static uint32_t target_event_mask;

static void cmd_pid(command_t *self) {
    target_pid = strtoul(self->arg, NULL, 10);
}

static void cmd_ns(command_t *self) {
    char buffer[128];
    snprintf(buffer, 128, "%s", self->arg);
    target_ns = buffer;
}

static void cmd_path(command_t *self) {
    char buffer[1024];
    snprintf(buffer, 1024, "%s", self->arg);
    target_paths[target_pathc++] = buffer;
}

static void cmd_event(command_t *self) {
    if (strcmp(self->arg, "all") == 0)         target_event_mask |= IN_ALL_EVENTS;
    else if (strcmp(self->arg, "access") == 0) target_event_mask |= IN_ACCESS;
    else if (strcmp(self->arg, "modify") == 0) target_event_mask |= IN_MODIFY;
    else if (strcmp(self->arg, "attrib") == 0) target_event_mask |= IN_ATTRIB;
    else if (strcmp(self->arg, "open") == 0)   target_event_mask |= IN_OPEN;
    else if (strcmp(self->arg, "close") == 0)  target_event_mask |= IN_CLOSE;
    else if (strcmp(self->arg, "create") == 0) target_event_mask |= IN_CREATE;
    else if (strcmp(self->arg, "delete") == 0) target_event_mask |= IN_DELETE;
    else if (strcmp(self->arg, "move") == 0)   target_event_mask |= IN_MOVE;
}

static void cmd_mask_add(command_t *self) {
    target_event_mask |= IN_MASK_ADD;
}

int main(int argc, char *argv[]) {
    int i;
    command_t cmd;
    command_init(&cmd, argv[0], "0.0.1");
    command_option(&cmd, "-p", "--pid <pid>", "target PID to watch", cmd_pid);
    command_option(&cmd, "-n", "--ns [namespace]", "target namespace {ipc|net|mnt|pid|user|uts}", cmd_ns);
    command_option(&cmd, "-t", "--path <path>", "target watch path(s)", cmd_path);
    command_option(&cmd, "-e", "--event [event]", "event to watch {access|modify|attrib|open|close|create|delete|move|all}", cmd_event);
    command_option(&cmd, "  ", "--mask-add", "if watch instance exists, add events to existing watch mask", cmd_mask_add);
    command_parse(&cmd, argc, argv);
    //printf("additional args:\n");
    //for (i = 1; i < cmd.argc; ++i) {
    //  printf("  - '%s'\n", cmd.argv[i]);
    //}
    command_free(&cmd);

    // client server

    int sock;
    struct sockaddr_in server_addr;

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) == EOF) {
        errexit("socket failed");
    }
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr) == EOF) {
        errexit("inet_pton");
    }
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) == EOF) {
        errexit("connect");
    }

    // encode amp message

    int len = target_pathc + 3;
    char **args = calloc(len, sizeof(char *));
    unsigned char buffer[1024];
    char *amp_message;

    args[0] = (char *)calloc(1024, sizeof(char));
    sprintf(args[0], "%d", target_pid);
    args[1] = target_ns;
    args[2] = (char *)calloc(1024, sizeof(char));
    sprintf(args[2], "0x%08x", target_event_mask);
    for (i = 0; i < target_pathc; ++i) {
        args[i + 3] = target_paths[i];
    }
    amp_message = amp_encode(args, len);

    send(sock, amp_message, len, 0);

    return 0; }

