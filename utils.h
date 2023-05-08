#ifndef UTILS_H
#define UTILS_H

#define MAX_CONNECTIONS 32
#define MAX_PAYLOAD_SIZE 1500
#define MAX_TOPIC_SIZE 50
#define MAX_ID_SIZE 11
#define CLIENT_POLLFDS 2
#define MAX_CLIENT_COMMAND_SIZE 100
#define MAX_SERVER_COMMAND_SIZE 10
#define TRASH_SIZE 15
#define INCREMENTAL_POLL_SIZE 10

#define ASSERT(a, b) if((a)) { perror((b)); exit(EXIT_FAILURE); }

#endif