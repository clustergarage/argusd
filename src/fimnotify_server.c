#define _GNU_SOURCE
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/socket.h>
#include <unistd.h>

#include "amp.h"
#include "fimnotify.h"

#define PORT 8080
#define LISTEN_BACKLOG 50

int main(int argc, char *argv[]) {
    int sockfd, sock, read_size;
    struct sockaddr_in target_addr;
    int opt = 1;
    int addr_len = sizeof(target_addr);
    unsigned char buffer[4096];

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == EOF) {
        errexit("socket failed");
    }
    //if (fcntl(sockfd, F_SETFL, O_NONBLOCK) == EOF) {
    //    errexit("fcntl");
    //}
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        errexit("setsockopt");
    }
    target_addr.sin_family = AF_INET;
    target_addr.sin_addr.s_addr = INADDR_ANY;
    target_addr.sin_port = htons(PORT);
    memset(&(target_addr.sin_zero), '\0', 8);

    if (bind(sockfd, (struct sockaddr *)&target_addr, sizeof(target_addr)) == EOF) {
        errexit("bind failed");
    }
    if (listen(sockfd, LISTEN_BACKLOG) == EOF) {
        errexit("listen");
    }

    if ((sock = accept(sockfd, (struct sockaddr *)&target_addr, (socklen_t *)&addr_len)) == EOF) {
        errexit("accept");
    }

    pid_t target_pid;
    char *target_ns;
    char *target_paths[32] = {NULL};
    int target_pathc = 0;
    uint32_t target_event_mask;

    while ((read_size = read(sock, buffer, sizeof(buffer))) > 0) {
        amp_t msg = {0};
        amp_decode(&msg, (char *)&buffer);

        char *arg;
        int i;
        for (i = 0; i < msg.argc; ++i) {
            arg = amp_decode_arg(&msg);
            switch (i) {
            case 0:
                target_pid = strtoul(arg, NULL, 10);
                break;
            case 1:
                target_ns = arg;
                break;
            case 2:
                target_event_mask = strtoul(arg, NULL, 16);
                break;
            default:
                target_paths[target_pathc++] = arg;
                break;
            }
        }

        printf("[server] Joining namespace...\n");
        join_namespace(target_pid, target_ns);

        printf("[server] Starting inotify watcher...\n");
        start_inotify_watcher(target_pathc, target_paths, target_event_mask);

        memset(&buffer, 0, sizeof(buffer));
    }

    return 0;
}

