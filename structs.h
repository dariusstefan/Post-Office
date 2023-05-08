#ifndef STRUCTS_H
#define STRUCTS_H

#include <stdint.h>
#include <unordered_map>
#include <vector>
#include "utils.h"

using namespace std;

typedef struct __attribute__ ((packed)) {
    char topic[MAX_TOPIC_SIZE];
    uint8_t data_type;
    char payload[MAX_PAYLOAD_SIZE];
} udp_message;

typedef struct __attribute__ ((packed)) {
    struct sockaddr_in udp_client_addr;
    char topic[MAX_TOPIC_SIZE + 1];
    uint8_t data_type;
    char payload[MAX_PAYLOAD_SIZE + 1];
} message, *Tmessage;

typedef struct {
    uint8_t connected;
    int socket;
    struct sockaddr_in addr;
    unordered_map<string, bool> *topic_sf_map;
    vector<Tmessage> *stored_messages;
} client, *Tclient;

#endif