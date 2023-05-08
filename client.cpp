#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "structs.h"
#include "utils.h"
#include <iostream>
#include <iomanip>

typedef enum {
	STATE_CONNECT,
    STATE_POLL,
    STATE_RECEIVED_FROM_SERVER,
    STATE_CHECK_STDIN,
    STATE_EXIT,
	NUM_STATES
} state_t;

typedef struct {
    int sockfd;
    int no_fds;
    struct sockaddr_in serv_addr;
    uint16_t server_port;
    char ip_server[20];
    struct pollfd poll_fds[CLIENT_POLLFDS];
    char id_client[MAX_ID_SIZE];
    char stdinbuf[MAX_CLIENT_COMMAND_SIZE];
    uint8_t exit_flag;
} instance_data, *instance_data_t;

typedef state_t state_func_t(instance_data_t data);

state_t do_connect(instance_data_t data);

state_t do_poll(instance_data_t data);

state_t do_received_from_server(instance_data_t data);

state_t do_check_stdin(instance_data_t data);

state_t do_exit(instance_data_t data);

state_t run_state(state_t cur_state, instance_data_t data);

state_func_t* const state_table[NUM_STATES] = {
	do_connect,
    do_poll,
    do_received_from_server,
    do_check_stdin,
    do_exit
};

state_t run_state(state_t cur_state, instance_data_t data) {
    return state_table[cur_state](data);
};

int main(int argc, char *argv[]) {
	instance_data data;

    setvbuf(stdout, NULL, _IONBF, BUFSIZ);
    
    if (argc != 4) {
        printf("\n Usage: %s <id_client> <ip_server> <port_server>\n", argv[0]);
        return 1;
    }

    int rc = sscanf(argv[1], "%s", data.id_client);
    ASSERT(rc != 1, "id client read failed");

    rc = sscanf(argv[2], "%s", data.ip_server);
    ASSERT(rc != 1, "ip server read failed");

    rc = sscanf(argv[3], "%hu", &data.server_port);
    ASSERT(rc != 1, "port read failed");

    data.sockfd = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT(data.sockfd < 0, "tcp socket creation failed");

    int enable = 1;
    rc = setsockopt(data.sockfd, IPPROTO_TCP, TCP_NODELAY, &enable, sizeof(int));
    ASSERT(rc < 0, "setsockopt(TCP_NODELAY) failed");

    memset(&data.serv_addr, 0, sizeof(struct sockaddr_in));
    data.serv_addr.sin_family = AF_INET;
    data.serv_addr.sin_port = htons(data.server_port);
    rc = inet_pton(AF_INET, data.ip_server, &data.serv_addr.sin_addr.s_addr);
    ASSERT(rc <= 0, "inet_pton failed");

    data.poll_fds[0].fd = STDIN_FILENO;
    data.poll_fds[0].events = POLLIN;

    data.poll_fds[1].fd = data.sockfd;
    data.poll_fds[1].events = POLLIN;

    data.no_fds = CLIENT_POLLFDS;

    data.exit_flag = 0;

    state_t cur_state = STATE_CONNECT;

    while (!data.exit_flag) {
        cur_state = run_state(cur_state, &data);
    }

    return 0;
}

state_t do_connect(instance_data_t data) {
    int rc = connect(data->sockfd, (struct sockaddr *)&data->serv_addr, sizeof(data->serv_addr));
    ASSERT(rc < 0, "connect failed");

    rc = send(data->sockfd, data->id_client, sizeof(data->id_client), 0);
    ASSERT(rc < 0, "send id client failed");

    return STATE_POLL;
}

state_t do_poll(instance_data_t data) {
    int rc = poll(data->poll_fds, data->no_fds, -1);
    ASSERT(rc < 0, "poll failed");

    for (int i = 0; i < data->no_fds; i++) {
        if (data->poll_fds[i].revents & POLLIN) {
            if (data->poll_fds[i].fd == STDIN_FILENO)
                return STATE_CHECK_STDIN;

            if (data->poll_fds[i].fd == data->sockfd)
                return STATE_RECEIVED_FROM_SERVER;
        }
    }

    return STATE_POLL;
}

state_t do_received_from_server(instance_data_t data) {
    message new_message;
    memset(&new_message, 0, sizeof(message));

    int rc = recv(data->sockfd, &new_message, sizeof(message), 0);
    ASSERT(rc < 0, "receive from server failed");

    if (rc == 0)
        return STATE_EXIT;

    if (rc < sizeof(message))
        return STATE_POLL;

    if (new_message.data_type > 3)
        return STATE_POLL;

    std::cout << inet_ntoa(new_message.udp_client_addr.sin_addr);
    std::cout << ":" << ntohs(new_message.udp_client_addr.sin_port);
    std::cout << " - " << new_message.topic;

    char sign;
    unsigned int value;
    unsigned short short_real;
    unsigned char exponent;
    double real;
    int pow = 1;

    switch (new_message.data_type) {
        case 0:
            std::cout << " - INT - ";
            sign = new_message.payload[0];
            memcpy(&value, new_message.payload + 1, sizeof(unsigned int));
            value = ntohl(value);

            if (!sign)
                std::cout << value << std::endl;
            else
                std::cout << "-" << value << std::endl;
            break;
        case 1:
            std::cout << " - SHORT_REAL - ";
            memcpy(&short_real, new_message.payload, sizeof(unsigned short));
            short_real = ntohs(short_real);

            std::cout << short_real / 100 << ".";

            if (short_real % 100 < 10)
                std::cout << "0" << short_real % 100 << std::endl;
            else
                std::cout << short_real % 100 << std::endl;
            break;
        case 2:
            std::cout << " - FLOAT - ";
            sign = new_message.payload[0];
            memcpy(&value, new_message.payload + 1, sizeof(unsigned int));
            value = ntohl(value);
            exponent = new_message.payload[5];

            for (int i = 0; i < exponent; i++)
                pow *= 10;
            
            real = (double) value / pow;

            if(sign)
                std::cout << "-";

            printf("%.11g\n", real);
            break;
        case 3:
            std::cout << " - STRING - " << new_message.payload << std::endl;
            break;
    }
    return STATE_POLL;
}

state_t do_check_stdin(instance_data_t data) {
    memset(data->stdinbuf, 0, sizeof(data->stdinbuf));
    fgets(data->stdinbuf, sizeof(data->stdinbuf), stdin);
    if (strcmp(data->stdinbuf, "exit\n") == 0) {
        return STATE_EXIT;
    }

    if (strncmp(data->stdinbuf, "subscribe", strlen("subscribe")) == 0) {
        int rc = send(data->sockfd, data->stdinbuf, strlen(data->stdinbuf), 0);
        ASSERT(rc < 0, "send subscribe to server failed");

        std::cout << "Subscribed to topic." << std::endl;

        return STATE_POLL;
    }

    if (strncmp(data->stdinbuf, "unsubscribe", strlen("unsubscribe")) == 0) {
        int rc = send(data->sockfd, data->stdinbuf, strlen(data->stdinbuf), 0);
        ASSERT(rc < 0, "send unsubscribe to server failed");
        
        std::cout << "Unsubscribed from topic." << std::endl;

        return STATE_POLL;
    }

    return STATE_POLL;
}

state_t do_exit(instance_data_t data) {
    data->exit_flag = 1;
    close(data->sockfd);

    return STATE_EXIT;
}
