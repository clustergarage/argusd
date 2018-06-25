#define _GNU_SOURCE
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/socket.h>

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
    //char *pch, *end_pch, *pct, *end_pct;

    while ((read_size = read(sock, buffer, sizeof(buffer))) > 0) {
		amp_t msg = {0};
		amp_decode(&msg, buffer);

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
				target_event_mask = atoi(arg);
				break;
			default:
                target_paths[target_pathc++] = arg;
				break;
			}
		}

		/*
        pch = strtok_r(buffer, " ", &end_pch);
        int i;
        while (pch != NULL) {
            if (i == 0) {
                target_pid = strtoul(pch, NULL, 10);
            } else if (i == 1) {
                pct = strtok_r(pch, ",", &end_pct);
                while (pct != NULL) {
                    if (strcmp(pct, "all") == 0)          target_event_mask |= IN_ALL_EVENTS;
                    else if (strcmp(pct, "access") == 0)  target_event_mask |= IN_ACCESS;
                    else if (strcmp(pct, "modify") == 0)  target_event_mask |= IN_MODIFY;
                    else if (strcmp(pct, "attrib") == 0)  target_event_mask |= IN_ATTRIB;
                    else if (strcmp(pct, "open") == 0)    target_event_mask |= IN_OPEN;
                    else if (strcmp(pct, "close") == 0)   target_event_mask |= IN_CLOSE;
                    else if (strcmp(pct, "create") == 0)  target_event_mask |= IN_CREATE;
                    else if (strcmp(pct, "delete") == 0)  target_event_mask |= IN_DELETE;
                    else if (strcmp(pct, "move") == 0)    target_event_mask |= IN_MOVE;
                    else if (strcmp(pct, "maskadd") == 0) target_event_mask |= IN_MASK_ADD;
                    pct = strtok_r(NULL, ",", &end_pct);
                }
                //target_event_mask = (uint32_t)pch[0] << 24 |
                //    (uint32_t)pch[1] << 16 |
                //    (uint32_t)pch[2] << 8  |
                //    (uint32_t)pch[3];
            } else {
                size_t len = strlen(pch);
                if (len > 0 && pch[len-1] == '\n') pch[--len] = '\0';
                len = strlen(pch);
                if (len > 0 && pch[len-1] == '\r') pch[--len] = '\0';
                target_paths[target_pathc++] = pch;
            }
            pch = strtok_r(NULL, " ", &end_pch);
            ++i;
        }
		*/

        printf("[server] Joining namespace...\n");
        join_namespace(target_pid, target_ns);

        printf("[server] Starting inotify watcher...\n");
        start_inotify_watcher(target_pathc, target_paths, target_event_mask);

		// @TODO: handle multiple blocking events

        memset(&buffer, 0, sizeof(buffer));
    }

    return 0;
}

