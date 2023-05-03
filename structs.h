#ifndef STRUCTS_H
#define STRUCTS_H

#include <stdint.h>

#define MAXSIZE 1551

typedef struct {
    char topic[50];
    uint8_t data_type;
    char payload[1500];
} udp_message;

#endif