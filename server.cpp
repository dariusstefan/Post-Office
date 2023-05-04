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
#include <unordered_map>

using namespace std;

typedef enum {
	STATE_POLL,
    STATE_CHECK_EXIT,
    STATE_RECEIVED_UDP,
    STATE_NEW_CONNECTION,
    STATE_RECEIVED_TCP,
    STATE_EXIT,
	NUM_STATES
} state_t;

typedef struct {
	int udp_sockfd;
    int listen_tcp_sockfd;
    int recv_tcp_sockfd;
    int no_fds;
    struct sockaddr_in serv_addr;
    uint16_t port;
    struct pollfd *poll_fds;
    char stdinbuf[10];
    uint8_t exit_flag;
    unordered_map<string, Tclient> clients;
    unordered_map<int, string> socket_client_map;
} instance_data, *instance_data_t;

typedef state_t state_func_t(instance_data_t data);

state_t do_poll(instance_data_t data);

state_t do_check_exit(instance_data_t data);

state_t do_received_udp(instance_data_t data);

state_t do_new_connection(instance_data_t data);

state_t do_received_tcp(instance_data_t data);

state_t do_exit(instance_data_t data);

state_t run_state(state_t cur_state, instance_data_t data);

state_func_t* const state_table[NUM_STATES] = {
	do_poll,
    do_check_exit,
    do_received_udp,
    do_new_connection,
    do_received_tcp,
    do_exit
};

state_t run_state(state_t cur_state, instance_data_t data) {
    return state_table[cur_state](data);
};

int main(int argc, char *argv[]) {
	instance_data data;

    setvbuf(stdout, NULL, _IONBF, BUFSIZ);

    if (argc != 2) {
        printf("\n Usage: %s <port>\n", argv[0]);
        return 1;
    }

    int rc = sscanf(argv[1], "%hu", &data.port);
    ASSERT(rc != 1, "port read failed");

    data.poll_fds = (struct pollfd *) malloc(100 * sizeof(struct pollfd));
    ASSERT(!data.poll_fds, "poll array creation failed");

    data.udp_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    ASSERT(data.udp_sockfd < 0, "udp socket creation failed");

    data.listen_tcp_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT(data.listen_tcp_sockfd < 0, "tcp socket creation failed");

    int enable = 1;
    if (setsockopt(data.udp_sockfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0)
        perror("setsockopt(SO_REUSEADDR) failed");

    enable = 1;
    if (setsockopt(data.listen_tcp_sockfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0)
        perror("setsockopt(SO_REUSEADDR) failed");

    memset(&data.serv_addr, 0, sizeof(struct sockaddr_in));
    data.serv_addr.sin_family = AF_INET;
    data.serv_addr.sin_port = htons(data.port);
    data.serv_addr.sin_addr.s_addr = INADDR_ANY;

    rc = bind(data.udp_sockfd, (const struct sockaddr *)&data.serv_addr, sizeof(data.serv_addr));
    ASSERT(rc < 0, "bind udp socket failed");

    rc = bind(data.listen_tcp_sockfd, (const struct sockaddr *)&data.serv_addr, sizeof(data.serv_addr));
    ASSERT(rc < 0, "bind tcp socket failed");

    rc = listen(data.listen_tcp_sockfd, MAX_CONNECTIONS);
    ASSERT(rc < 0, "listen failed");

    data.poll_fds[0].fd = STDIN_FILENO;
    data.poll_fds[0].events = POLLIN;

    data.poll_fds[1].fd = data.udp_sockfd;
    data.poll_fds[1].events = POLLIN;

    data.poll_fds[2].fd = data.listen_tcp_sockfd;
    data.poll_fds[2].events = POLLIN;

    data.no_fds = 3;

    data.exit_flag = 0;

    state_t cur_state = STATE_POLL;

    while (!data.exit_flag) {
        for (auto client : data.clients) {
            std::cout << client.first << " topics: " << std::endl;
            for (auto topic : *(client.second->topic_sf_map))
                std::cout << topic.first << " " << topic.second << std::endl;
        }
        cur_state = run_state(cur_state, &data);
    }

    return 0;
}

state_t do_poll(instance_data_t data) {
    int rc = poll(data->poll_fds, data->no_fds, -1);
    ASSERT(rc < 0, "poll failed");

    for (int i = 0; i < data->no_fds; i++) {
        if (data->poll_fds[i].revents & POLLIN) {
            if (data->poll_fds[i].fd == STDIN_FILENO)
                return STATE_CHECK_EXIT;

            if (data->poll_fds[i].fd == data->udp_sockfd)
                return STATE_RECEIVED_UDP;

            if (data->poll_fds[i].fd == data->listen_tcp_sockfd)
                return STATE_NEW_CONNECTION;

            data->recv_tcp_sockfd = data->poll_fds[i].fd;
            return STATE_RECEIVED_TCP;
        }
    }

    return STATE_POLL;
}

state_t do_check_exit(instance_data_t data) {
    fgets(data->stdinbuf, sizeof(data->stdinbuf), stdin);
    if (strcmp(data->stdinbuf, "exit\n") == 0) {
        return STATE_EXIT;
    } else {
        memset(data->stdinbuf, 0, sizeof(data->stdinbuf));
        return STATE_POLL;
    }
}

state_t do_received_udp(instance_data_t data) {
    struct sockaddr_in client_addr;
    socklen_t clen = sizeof(client_addr);
    udp_message new_msg;
    memset(&new_msg.payload, 0, 1500);

    int rc = recvfrom(data->udp_sockfd, &new_msg, sizeof(udp_message), 0, (struct sockaddr *)&client_addr, &clen);
    ASSERT(rc < 0, "recv from udp failed");

    return STATE_POLL;
}

state_t do_new_connection(instance_data_t data) {
    struct sockaddr_in client_addr;
    socklen_t clen = sizeof(client_addr);
    int newsockfd = accept(data->listen_tcp_sockfd, (struct sockaddr *)&client_addr, &clen);
    ASSERT(newsockfd < 0, "accept failed");

    int enable = 1;
    setsockopt(newsockfd, IPPROTO_TCP, TCP_NODELAY, &enable, sizeof(int));

    char id_client[10];
    int rc = recv(newsockfd, id_client, 10, 0);
    ASSERT(rc < 0, "received id client failed");

    auto iterator = data->clients.find(id_client);

    if (iterator != data->clients.end()) {
        if (iterator->second->connected == 1) {
            close(newsockfd);
            std::cout << "Client " << id_client << " already connected." << std::endl;
            return STATE_POLL;

        } else {
            iterator->second->connected = 1;
            iterator->second->socket = newsockfd;
            iterator->second->addr = client_addr;

            data->poll_fds[data->no_fds].fd = newsockfd;
            data->poll_fds[data->no_fds++].events = POLLIN;

            data->socket_client_map[newsockfd] = id_client;

            std::cout << "New client " << id_client;
            std::cout << " connected from " << inet_ntoa(client_addr.sin_addr) << ":" << ntohs(client_addr.sin_port);
            std::cout << "." << std::endl;

            return STATE_POLL;
        }
    } else {
        Tclient new_client = (Tclient) malloc(sizeof(client));
        ASSERT(!new_client, "new client alloc failed");

        new_client->connected = 1;
        new_client->socket = newsockfd;
        new_client->addr = client_addr;
        new_client->topic_sf_map = new unordered_map<string, bool>;

        data->poll_fds[data->no_fds].fd = newsockfd;
        data->poll_fds[data->no_fds++].events = POLLIN;

        data->clients[id_client] = new_client;
        data->socket_client_map[newsockfd] = id_client;

        std::cout << "New client " << id_client;
        std::cout << " connected from " << inet_ntoa(client_addr.sin_addr) << ":" << ntohs(client_addr.sin_port);
        std::cout << "." << std::endl;

        return STATE_POLL;
    }
}

state_t do_received_tcp(instance_data_t data) {
    char buffer[100];
    int rc = recv(data->recv_tcp_sockfd, buffer, sizeof(buffer), 0);
    ASSERT(rc < 0, "recv from tcp client failed");

    auto iterator = data->socket_client_map.find(data->recv_tcp_sockfd);
    if (iterator == data->socket_client_map.end())
        ASSERT(1, "received from disconnected client");

    if (rc == 0) {
        std::cout << "Client " << iterator->second << " disconnected." << std::endl;

        int i = 0;
        for (; i < data->no_fds; i++)
            if (data->poll_fds[i].fd == data->recv_tcp_sockfd)
                break;

        for (; i < data->no_fds - 1; i++) {
            data->poll_fds[i].fd = data->poll_fds[i + 1].fd;
            data->poll_fds[i].events = data->poll_fds[i + 1].events;
            data->poll_fds[i].revents = data->poll_fds[i + 1].revents;
        }

        auto iterator2 = data->clients.find(iterator->second);
        if (iterator2 == data->clients.end())
            ASSERT(1, "received from disconnected client");

        iterator2->second->connected = 0;

        data->no_fds--;
        data->socket_client_map.erase(iterator);

        return STATE_POLL;
    }

    if (strncmp(buffer, "subscribe", strlen("subscribe")) == 0) {
        char trash[15];
        char topic[50];
        uint8_t sf;

        int rc = sscanf(buffer, "%s %s %hhu", trash, topic, &sf);
        ASSERT(rc != 3, "topic and sf read failed");

        auto iterator2 = data->clients.find(iterator->second);
        if (iterator2 == data->clients.end())
            ASSERT(1, "received subscribe from disconnected client");

        auto iterator3 = iterator2->second->topic_sf_map->find(topic);
        if (iterator3 != iterator2->second->topic_sf_map->end())
            return STATE_POLL;
        if (sf)
            iterator2->second->topic_sf_map->insert({topic, true});
        else
            iterator2->second->topic_sf_map->insert({topic, false});
    }

    if (strncmp(buffer, "unsubscribe", strlen("unsubscribe")) == 0) {
        char trash[15];
        char topic[50];

        int rc = sscanf(buffer, "%s %s", trash, topic);
        ASSERT(rc != 2, "topic read failed");

        auto iterator2 = data->clients.find(iterator->second);
        if (iterator2 == data->clients.end())
            ASSERT(1, "received unsubscribe from disconnected client");

        auto iterator3 = iterator2->second->topic_sf_map->find(topic);
        if (iterator3 == iterator2->second->topic_sf_map->end())
            return STATE_POLL;
        
        iterator2->second->topic_sf_map->erase(iterator3);
    }

    return STATE_POLL;
}

state_t do_exit(instance_data_t data) {
    data->exit_flag = 1;
    close(data->udp_sockfd);
    close(data->listen_tcp_sockfd);

    return STATE_EXIT;
}
