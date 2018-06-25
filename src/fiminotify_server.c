#define _GNU_SOURCE

#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#define PORT 8080
#define LISTEN_BACKLOG 50

#include "lib_fiminotify.h"

int main(int argc, char *argv[]) {
    int sockfd, sock, read_size;
    struct sockaddr_in target_addr;
    int opt = 1;
    int addr_len = sizeof(target_addr);
    unsigned char buffer[4096];

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == EOF) {
        errexit("socket failed");
    }
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
    while ((read_size = read(sock, buffer, sizeof(buffer))) > 0) {
        printf("%s", buffer);
        memset(&buffer, 0, sizeof(buffer));
    }

    //join_namespace(target_pid, target_ns);
    //start_inotify_watcher(target_pathc, target_paths, target_event_mask);

    return 0;
}

