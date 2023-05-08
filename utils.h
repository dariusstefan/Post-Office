#ifndef UTILS_H
#define UTILS_H

#define MAX_CONNECTIONS 32
#define MAX_PAYLOAD_SIZE 1500
#define MAX_TOPIC_SIZE 50
#define MAX_ID_SIZE 10
#define CLIENT_POLLFDS 2
#define MAX_CLIENT_COMMAND_SIZE 100
#define MAX_SERVER_COMMAND_SIZE 10
#define TRASH_SIZE 15
#define INCREMENTAL_POLL_SIZE 10

#define INT_SIZE 5
#define SHORT_REAL_SIZE 2
#define FLOAT_SIZE 6

#define ASSERT(a, b) if((a)) { perror((b)); exit(EXIT_FAILURE); }

int send_all(int sockfd, void *buff, size_t no_bytes, int flags) {
    int rc = 0;
    size_t bytes_remaining = no_bytes;
    size_t bytes_sent = 0;

    while(bytes_remaining != 0) {
        rc = send(sockfd, (char *) buff + bytes_sent, bytes_remaining, flags);
        if (rc < 0)
            return -1;

        bytes_sent += rc;
        bytes_remaining -= rc;
    }

    return bytes_sent;
}

int recv_all(int sockfd, void *buff, size_t no_bytes, int flags) {
    int rc = 0;
    size_t bytes_remaining = no_bytes;
    size_t bytes_sent = 0;

    while(bytes_remaining != 0) {
        rc = recv(sockfd, (char *) buff + bytes_sent, bytes_remaining, flags);

        if (rc < 0)
            return -1;

        if (rc == 0)
            return 0;

        bytes_sent += rc;
        bytes_remaining -= rc;
    }

    return bytes_sent;
}

#endif