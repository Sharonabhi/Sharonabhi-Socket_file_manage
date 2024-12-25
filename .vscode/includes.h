#ifndef INCLUDES_H
#define INCLUDES_H

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#define PORT 8080           // Server listening port number
#define BUFFER_SIZE 1024    // Buffer size
#define CONTENT_SIZE 36635  // File content size
#define SERVER_ADDR "127.0.0.1"

// User structure
typedef struct {
    char name[256];     // Name
    char group[50];     // Group affiliation
} User;

// Client request structure
typedef struct {
    User user;                  // User data
    char command[BUFFER_SIZE];  // Command
} ClientRequest;

// Server response structure
typedef struct {
    char status[256];           // Server status, e.g., action success
    char content[BUFFER_SIZE];  // Response content
} Response;

#endif
