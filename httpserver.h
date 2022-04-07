#ifndef __HTTPSERVER_H__
#define __HTTPSERVER_H__

#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <queue>
#include <string>
#include <unordered_map>

#define BUFFER_SIZE 4096

typedef struct HTTPObject {
  char method[4];
  char filename[12];
  ssize_t content_length;
  unsigned short status_code;
  char buf[BUFFER_SIZE];
  ssize_t data_in_buf;
  ssize_t client_fd;
} HTTPObject;

unsigned long get_address(char *name);

ssize_t *read_request(HTTPObject &message, ssize_t *fd, bool redundancy);

bool fdcmp(ssize_t fd1, ssize_t fd2);

int get(ssize_t fd, HTTPObject &message);

int put(ssize_t *fd, HTTPObject &message);

void send_response(ssize_t client_fd, unsigned short status_code,
                   const char *status_string, ssize_t cl);

void error_check(int retval, const char *func_name);

#endif