#include <sys/socket.h>
#include <arpa/inet.h>
#include <poll.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "structs.h"
#include <iostream>

int main(int argc, char *argv[]) {
    setvbuf(stdout, NULL, _IONBF, BUFSIZ);

    int udp_sockfd, tcp_sockfd, no_fds = 2;
    struct sockaddr_in serv_addr;
    socklen_t socket_len = sizeof(struct sockaddr_in);

    if (argc != 2) {
        printf("\n Usage: %s <port>\n", argv[0]);
        return 1;
    }

    uint16_t port;
    int rc = sscanf(argv[1], "%hu", &port);
    if (rc != 1) {
        perror("Given port is invalid");
        exit(EXIT_FAILURE);
    }

    struct pollfd *poll_fds = (struct pollfd *) malloc(100 * sizeof(struct pollfd));
    if (!poll_fds) {
        perror("poll array creation failed");
        exit(EXIT_FAILURE);
    }

    if ((udp_sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("udp socket creation failed");
        exit(EXIT_FAILURE);
    }

    if ((tcp_sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("tcp socket creation failed");
        exit(EXIT_FAILURE);
    }

    int enable = 1;
    if (setsockopt(udp_sockfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0)
    perror("setsockopt(SO_REUSEADDR) failed");

    enable = 1;
    if (setsockopt(tcp_sockfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0)
    perror("setsockopt(SO_REUSEADDR) failed");

    memset(&serv_addr, 0, socket_len);
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    serv_addr.sin_addr.s_addr = INADDR_ANY;

    rc = bind(udp_sockfd, (const struct sockaddr *)&serv_addr, sizeof(serv_addr));
    if (rc < 0) {
        perror("bind udp socket failed");
        exit(EXIT_FAILURE);
    }

    rc = bind(tcp_sockfd, (const struct sockaddr *)&serv_addr, sizeof(serv_addr));
    if (rc < 0) {
        perror("bind tcp socket failed");
        exit(EXIT_FAILURE);
    }

    poll_fds[0].fd = STDIN_FILENO;
    poll_fds[0].events = POLLIN;

    poll_fds[1].fd = udp_sockfd;
    poll_fds[1].events = POLLIN;

    std::cout << "SERVER INITIALISED AND WILL START...\n";

    char stdinbuf[10];

    while (1) {
        rc = poll(poll_fds, no_fds, -1);
        if (rc < 0) {
            perror("poll failed");
            exit(EXIT_FAILURE);
        }

        for (int i = 0; i < no_fds; i++) {
            if (poll_fds[i].revents & POLLIN) {
                if (poll_fds[i].fd == STDIN_FILENO) {
                    fgets(stdinbuf, sizeof(stdinbuf), stdin);
                    if (strcmp(stdinbuf, "exit\n") == 0) {
                        close(udp_sockfd);
                        close(tcp_sockfd);
                        return 0;
                    }
                }

                if (poll_fds[i].fd == udp_sockfd) {
                    struct sockaddr_in client_addr;
                    socklen_t clen = sizeof(client_addr);

                    udp_message new_msg;
                    memset(&new_msg.payload, 0, 1500);

                    rc = recvfrom(udp_sockfd, &new_msg, sizeof(udp_message), 0, (struct sockaddr *)&client_addr, &clen);
                    if (rc < 0) {
                        perror("recv from udp failed");
                        exit(EXIT_FAILURE);
                    }

                    printf("[Server received %d bytes] topic: %s data_type: %u payload: %s\n", rc, new_msg.topic, new_msg.data_type, new_msg.payload);
                }
            }
        }
    }

    return 0;
}