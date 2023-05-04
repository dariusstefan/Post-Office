#ifndef STRUCTS_H
#define STRUCTS_H

#include <stdint.h>
#include <unordered_map>

using namespace std;

#define MAXSIZE 1551
#define MAXBUFSZ 2000

typedef struct {
    char topic[50];
    uint8_t data_type;
    char payload[1500];
} udp_message;

typedef struct {
    uint8_t connected;
    int socket;
    struct sockaddr_in addr;
    unordered_map<string, bool> *topic_sf_map;
} client, *Tclient;

#endif