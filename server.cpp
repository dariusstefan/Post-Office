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
    STATE_CLOSE_CONNECTION,
    STATE_SUBSCRIBE,
    STATE_UNSUBSCRIBE,
    STATE_SEND_STORED,
    STATE_EXIT,
	NUM_STATES
} state_t;

typedef struct {
	int udp_sockfd;
    int listen_tcp_sockfd;
    int recv_tcp_sockfd;
    int no_fds;
    int poll_size;
    struct sockaddr_in serv_addr;
    uint16_t port;
    struct pollfd *poll_fds;
    char stdinbuf[MAX_CLIENT_COMMAND_SIZE];
    uint8_t exit_flag;
    char buffer[MAX_CLIENT_COMMAND_SIZE];
    char id_client[MAX_ID_SIZE];
    unordered_map<string, Tclient> clients;
    unordered_map<int, string> socket_client_map;
    unordered_map<Tmessage, int> buffered_messages;
} instance_data, *instance_data_t;

typedef state_t state_func_t(instance_data_t data);

state_t do_poll(instance_data_t data);

state_t do_check_exit(instance_data_t data);

state_t do_received_udp(instance_data_t data);

state_t do_new_connection(instance_data_t data);

state_t do_received_tcp(instance_data_t data);

state_t do_close_connection(instance_data_t data);

state_t do_subscribe(instance_data_t data);

state_t do_unsubscribe(instance_data_t data);

state_t do_send_stored(instance_data_t data);

state_t do_exit(instance_data_t data);

state_t run_state(state_t cur_state, instance_data_t data);

state_func_t* const state_table[NUM_STATES] = {
	do_poll,
    do_check_exit,
    do_received_udp,
    do_new_connection,
    do_received_tcp,
    do_close_connection,
    do_subscribe,
    do_unsubscribe,
    do_send_stored,
    do_exit
};

state_t run_state(state_t cur_state, instance_data_t data) {
    return state_table[cur_state](data);
};

void send_message(int sockfd, Tmessage new_message);

int main(int argc, char *argv[]) {
	instance_data data;

    setvbuf(stdout, NULL, _IONBF, BUFSIZ);

    if (argc != 2) {
        printf("\n Usage: %s <port>\n", argv[0]);
        return 1;
    }

    int rc = sscanf(argv[1], "%hu", &data.port);
    ASSERT(rc != 1, "port read failed");

    data.poll_fds = (struct pollfd *) malloc(INCREMENTAL_POLL_SIZE * sizeof(struct pollfd));
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

    data.poll_size = INCREMENTAL_POLL_SIZE;
    data.no_fds = 3;

    data.exit_flag = 0;

    state_t cur_state = STATE_POLL;

    while (!data.exit_flag) {
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
    memset(&new_msg.payload, 0, MAX_PAYLOAD_SIZE);

    int rc = recvfrom(data->udp_sockfd, &new_msg, sizeof(udp_message), 0, (struct sockaddr *)&client_addr, &clen);
    ASSERT(rc < 0, "recv from udp failed");

    Tmessage message_for_subs = new message;
    message_for_subs->udp_client_addr = client_addr;
    message_for_subs->data_type = new_msg.data_type;
    memcpy(message_for_subs->topic, new_msg.topic, sizeof(new_msg.topic));
    memcpy(message_for_subs->payload, new_msg.payload, sizeof(new_msg.payload));
    
    int i = 0;
    for (auto client : data->clients) {
        auto iterator = client.second->topic_sf_map->find(message_for_subs->topic);

        if (iterator != client.second->topic_sf_map->end()) {
            if (client.second->connected) {
                send_message(client.second->socket, message_for_subs);
            } else {
                if (iterator->second == true) {
                    data->buffered_messages[message_for_subs] = ++i;
                    client.second->stored_messages->push_back(message_for_subs);
                }
            }
        }
    }

    if (i == 0)
        delete message_for_subs;

    return STATE_POLL;
}

state_t do_new_connection(instance_data_t data) {
    struct sockaddr_in client_addr;
    socklen_t clen = sizeof(client_addr);
    int newsockfd = accept(data->listen_tcp_sockfd, (struct sockaddr *)&client_addr, &clen);
    ASSERT(newsockfd < 0, "accept failed");

    int enable = 1;
    setsockopt(newsockfd, IPPROTO_TCP, TCP_NODELAY, &enable, sizeof(int));

    int rc = recv(newsockfd, data->id_client, MAX_ID_SIZE, 0);
    ASSERT(rc < 0, "received id client failed");

    auto iterator = data->clients.find(data->id_client);

    if (iterator != data->clients.end()) {
        if (iterator->second->connected == 1) {
            close(newsockfd);
            std::cout << "Client " << data->id_client << " already connected." << std::endl;
            return STATE_POLL;
        } else {
            iterator->second->connected = 1;
            iterator->second->socket = newsockfd;
            iterator->second->addr = client_addr;

            if (data->no_fds == data->poll_size) {
                struct pollfd *new_poll = (struct pollfd *) realloc(data->poll_fds, (data->poll_size + INCREMENTAL_POLL_SIZE) * sizeof(struct pollfd));
                ASSERT(new_poll == NULL, "realloc of poll failed");

                data->poll_fds = new_poll;
                data->poll_size += INCREMENTAL_POLL_SIZE;
            }

            data->poll_fds[data->no_fds].fd = newsockfd;
            data->poll_fds[data->no_fds++].events = POLLIN;

            data->socket_client_map[newsockfd] = data->id_client;

            std::cout << "New client " << data->id_client;
            std::cout << " connected from " << inet_ntoa(client_addr.sin_addr) << ":" << ntohs(client_addr.sin_port);
            std::cout << "." << std::endl;

            data->recv_tcp_sockfd = newsockfd;

            return STATE_SEND_STORED;
        }
    } else {
        Tclient new_client = new client;
        ASSERT(!new_client, "new client alloc failed");

        new_client->connected = 1;
        new_client->socket = newsockfd;
        new_client->addr = client_addr;
        new_client->topic_sf_map = new unordered_map<string, bool>;
        new_client->stored_messages = new vector<Tmessage>;

        if (data->no_fds == data->poll_size) {
            struct pollfd *new_poll = (struct pollfd *) realloc(data->poll_fds, (data->poll_size + INCREMENTAL_POLL_SIZE) * sizeof(struct pollfd));
            ASSERT(new_poll == NULL, "realloc of poll failed");

            data->poll_fds = new_poll;
            data->poll_size += INCREMENTAL_POLL_SIZE;
        }

        data->poll_fds[data->no_fds].fd = newsockfd;
        data->poll_fds[data->no_fds++].events = POLLIN;

        data->clients[data->id_client] = new_client;
        data->socket_client_map[newsockfd] = data->id_client;

        std::cout << "New client " << data->id_client;
        std::cout << " connected from " << inet_ntoa(client_addr.sin_addr) << ":" << ntohs(client_addr.sin_port);
        std::cout << "." << std::endl;

        return STATE_POLL;
    }
}

state_t do_received_tcp(instance_data_t data) {
    memset(data->buffer, 0, sizeof(data->buffer));

    int rc = recv(data->recv_tcp_sockfd, data->buffer, sizeof(data->buffer), 0);
    ASSERT(rc < 0, "recv from tcp client failed");

    if (rc == 0) {
        return STATE_CLOSE_CONNECTION;
    }

    if (strncmp(data->buffer, "subscribe", strlen("subscribe")) == 0)
        return STATE_SUBSCRIBE;

    if (strncmp(data->buffer, "unsubscribe", strlen("unsubscribe")) == 0)
        return STATE_UNSUBSCRIBE;

    return STATE_POLL;
}

state_t do_close_connection(instance_data_t data) {
    auto iterator = data->socket_client_map.find(data->recv_tcp_sockfd);
    if (iterator == data->socket_client_map.end())
        ASSERT(1, "received from disconnected client");

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

state_t do_subscribe(instance_data_t data) {
    char trash[TRASH_SIZE];
    char topic[MAX_TOPIC_SIZE];
    uint8_t sf;

    auto iterator = data->socket_client_map.find(data->recv_tcp_sockfd);

    int rc = sscanf(data->buffer, "%s %s %hhu", trash, topic, &sf);
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

    return STATE_POLL;
}

state_t do_unsubscribe(instance_data_t data) {
    char trash[TRASH_SIZE];
    char topic[MAX_TOPIC_SIZE];

    auto iterator = data->socket_client_map.find(data->recv_tcp_sockfd);

    int rc = sscanf(data->buffer, "%s %s", trash, topic);
    ASSERT(rc != 2, "topic read failed");

    auto iterator2 = data->clients.find(iterator->second);
    if (iterator2 == data->clients.end())
        ASSERT(1, "received unsubscribe from disconnected client");

    auto iterator3 = iterator2->second->topic_sf_map->find(topic);
    if (iterator3 == iterator2->second->topic_sf_map->end())
        return STATE_POLL;
        
    iterator2->second->topic_sf_map->erase(iterator3);

    return STATE_POLL;
}

state_t do_send_stored(instance_data_t data) {
    auto client = data->clients.find(data->id_client);

    if (client == data->clients.end())
        ASSERT(1, "no client with this id");

    for (auto curr_message : *(client->second->stored_messages)) {
        send_message(data->recv_tcp_sockfd, curr_message);

        auto iterator = data->buffered_messages.find(curr_message);

        if (iterator == data->buffered_messages.end())
            ASSERT(1, "message not found buffered");

        iterator->second--;

        if (iterator->second == 0) {
            delete iterator->first;
            data->buffered_messages.erase(iterator);
        }
    }

    client->second->stored_messages->clear();

    return STATE_POLL;
}

state_t do_exit(instance_data_t data) {
    data->exit_flag = 1;
    for (int i = 1; i < data->no_fds; i++)
        close(data->poll_fds[i].fd);
    
    for (auto client : data->clients) {
        delete client.second->topic_sf_map;
        delete client.second->stored_messages;
        delete client.second;
    }

    for (auto stored_message : data->buffered_messages)
        delete stored_message.first;

    free(data->poll_fds);

    data->buffered_messages.clear();
    data->clients.clear();
    data->socket_client_map.clear();

    return STATE_EXIT;
}

void send_message(int sockfd, Tmessage new_message) {
    int rc = 0;
    switch (new_message->data_type) {
        case 0:
            rc = send_all(sockfd, new_message, sizeof(message) - MAX_PAYLOAD_SIZE + INT_SIZE, 0);
            ASSERT(rc < 0, "send int to tcp client failed");
            break;
        case 1:
            rc = send_all(sockfd, new_message, sizeof(message) - MAX_PAYLOAD_SIZE + SHORT_REAL_SIZE, 0);
            ASSERT(rc < 0, "send short real to tcp client failed");
            break;
        case 2:
            rc = send_all(sockfd, new_message, sizeof(message) - MAX_PAYLOAD_SIZE + FLOAT_SIZE, 0);
            ASSERT(rc < 0, "send float to tcp client failed");
            break;
        case 3:
            rc = send_all(sockfd, new_message, sizeof(message), 0);
            ASSERT(rc < 0, "send string to tcp client failed");
            break;
        default:
            break;
    }
    return;
}
