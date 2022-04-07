#include "httpserver.h"

unsigned long get_address(char *name) {
  unsigned long result;
  struct addrinfo hints, *info;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  int s = getaddrinfo(name, NULL, &hints, &info);
  if (s) {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
    exit(EXIT_FAILURE);
  }
  result = ((struct sockaddr_in *)info->ai_addr)->sin_addr.s_addr;
  freeaddrinfo(info);
  return result;
}

void send_response(ssize_t client_fd, unsigned short status_code,
                   const char *status_string, ssize_t cl) {
  dprintf(client_fd, "HTTP/1.1 %d %s\r\nContent-Length: %zd\r\n\r\n",
          status_code, status_string, cl);
  return;
}

/*
    parses the HTTP request header and saves pertinent information
    param message: HTTPObject containing current request
    retval: 0 on success, -1 on error
*/
int parse_header(HTTPObject &message) {
  int n = sscanf(message.buf, "%s %s", message.method, message.filename);
  // check that method is either GET or PUT
  // check that filename is 11 chars long (10 required + 1 /)
  if (n != 2 ||
      (strcmp(message.method, "GET") != 0 &&
       strcmp(message.method, "PUT") != 0) ||
      strlen(message.filename) != 11) {
    message.status_code = 400;
    return -1;
  }
  // remove / from filename
  memmove(message.filename, message.filename + 1, strlen(message.filename));
  // check that all chars in filename are alphanumeric
  for (size_t i = 0; i < strlen(message.filename); ++i) {
    if (!isalnum(message.filename[i])) {
      message.status_code = 400;
      return -1;
    }
  }
  if (!strstr(message.buf, "HTTP/1.1\r\n")) {
    // wrong HTTP version
    message.status_code = 400;
    return -1;
  }

  if (strcmp(message.method, "PUT") == 0) {
    char *cl_ptr = strstr(message.buf, "Content-Length:");

    if (cl_ptr) {
      ssize_t content_length;
      n = sscanf(cl_ptr, "Content-Length: %zd", &content_length);
      if (n != 1) {
        message.status_code = 400;
        return -1;
      }
      message.content_length = content_length;
    } else {
      message.content_length = -1;  // no content-length, set to arbitrary -1
    }
  }
  return 0;
}

// if GET, returns fd of opened file. if PUT, returns fd of newly created file
// if error, returns -1
ssize_t *read_request(HTTPObject &message, ssize_t *fd, bool redundancy) {
  ssize_t bytes_read = 0;
  char *full_header = strstr(message.buf, "\r\n\r\n");
  while (full_header == nullptr) {
    bytes_read += recv(message.client_fd, message.buf, BUFFER_SIZE, 0);
    full_header = strstr(message.buf, "\r\n\r\n");
  }
  if (parse_header(message) == -1) {
    fd[0] = -1;
    return fd;
  }
  char adjusted_path[20] = "./files/";
  strcat(adjusted_path, message.filename);
  char path_1[30] = "./files/copy1/";
  char path_2[30] = "./files/copy2/";
  char path_3[30] = "./files/copy3/";
  strcat(path_1, message.filename);
  strcat(path_2, message.filename);
  strcat(path_3, message.filename);
  if (strcmp(message.method, "GET") == 0) {
    if (redundancy) {                     // Redundancy turned on
      fd[1] = open(path_1, O_RDONLY);  // Open all 3 copies to compare
      fd[2] = open(path_2, O_RDONLY);
      fd[3] = open(path_3, O_RDONLY);
      if (fdcmp(fd[1], fd[2]) ||
          fdcmp(fd[1],
                fd[3])) {  // If copy1 == copy2 or copy1 == copy3, return copy1
        close(fd[1]);
        close(fd[2]);
        close(fd[3]);
        fd[0] = open(path_1, O_RDONLY);
      } else if (fdcmp(fd[2], fd[3])) {  // If copy2 == copy3, return copy2
        close(fd[1]);
        close(fd[2]);
        close(fd[3]);
        fd[0] = open(path_2, O_RDONLY);
      } else {  // Else, all 3 copies are corrupted/different, so return status
                // code 500
        close(fd[1]);
        close(fd[2]);
        close(fd[3]);
        message.status_code = 500;
        fd[0] = -1;
        return fd;
      }
    } else {  // No redundancy so just use filename in main directory
      fd[0] = open(adjusted_path, O_RDONLY);
    }
    if (fd[0] < 0) {
      if (errno == EACCES)
        message.status_code = 403;
      else if (errno == ENOENT)
        message.status_code = 404;
      else
        message.status_code = 500;
      fd[0] = -1;
      return fd;
    } else {
      struct stat statbuf;
      fstat(fd[0], &statbuf);
      message.content_length =
          statbuf.st_size;  // get content-length for response
      message.status_code = 200;
    }
    memset(message.buf, 0, BUFFER_SIZE);  // reset buffer
  } else {                                // PUT request
    ssize_t file_exists;
    if (redundancy) {
      file_exists = access(path_1, F_OK);
      fd[0] = 0;  // Setting base file descriptor 0 tells put method if redundancy
                // is on or off.
      fd[1] = open(path_1, O_CREAT | O_WRONLY | O_TRUNC,
                  0644);  // Copy new/updated file to copy1,2,3 folders
      fd[2] = open(path_2, O_CREAT | O_WRONLY | O_TRUNC,
                  0644);  // because regardless of redundancy on or off,
      fd[3] = open(path_3, O_CREAT | O_WRONLY | O_TRUNC,
                  0644);  // all files must still be copied.
    } else {
      file_exists = access(adjusted_path, F_OK);
      fd[0] = open(adjusted_path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    }
    if (fd[1] < 0) {
      if (errno == EACCES)
        message.status_code = 403;
      else if (errno == ENOENT)
        message.status_code = 404;
      else
        message.status_code = 500;
      fd[0] = -1;
      return fd;
    } else {
      if (file_exists == 0) {
        message.status_code = 200;  // Valid PUT, modified existing content
      } else {
        message.status_code = 201;  // Valid PUT, new content created
      }
    }
    // just reset header, so we don't overwrite other data
    int header_len = full_header - message.buf + 4;
    // if we recv more than just the header, we need to keep track of that
    if (header_len < bytes_read) message.data_in_buf = bytes_read - header_len;
    memmove(message.buf, message.buf + header_len, BUFFER_SIZE - header_len);
  }

  return fd;  // don't forget to close fd
}

// Takes 2 file descriptors and returns if they are the same or not
bool fdcmp(ssize_t fd1, ssize_t fd2) {
  struct stat statbuf1;
  struct stat statbuf2;
  fstat(fd1, &statbuf1);
  fstat(fd2, &statbuf2);
  if (statbuf1.st_size != statbuf2.st_size) return false;
  ssize_t n;
  ssize_t m;
  char buf1[BUFFER_SIZE];
  char buf2[BUFFER_SIZE];
  memset(&buf1, 0, BUFFER_SIZE);
  memset(&buf2, 0, BUFFER_SIZE);
  while ((n = read(fd1, buf1, BUFFER_SIZE)) != 0) {
    if (n < 0) return false;
    m = read(fd2, buf2, BUFFER_SIZE);
    if (m < 0) return false;
    if (strcmp(buf1, buf2) == 0)
      return true;
    else
      return false;
  }
  return false;
}

int get(ssize_t fd, HTTPObject &message) {
  ssize_t n;
  while ((n = read(fd, message.buf, BUFFER_SIZE)) != 0) {
    if (n < 0) {
      message.status_code = 500;
      return -1;
    }
    if (send(message.client_fd, message.buf, n, 0) < 0) {
      message.status_code = 500;
      return -1;
    }
  }
  close(fd);
  return 0;
}

int put(ssize_t *fd, HTTPObject &message) {
  ssize_t n;
  ssize_t total_bytes = 0;
  if (message.data_in_buf) {
    if (write(fd[1], message.buf, message.data_in_buf) < 0) {
      message.status_code = 500;
      return -1;
    }
  }
  while ((n = recv(message.client_fd, message.buf, BUFFER_SIZE, 0)) != 0) {
    if (n < 0) {
      message.status_code = 500;
      return -1;
    }
    total_bytes += n;
    for (ssize_t i = 0; i < 4; i++) {
      if (fd[i] == 0) {  // Mark for no redunancy
        continue;
      }
      if (write(fd[i], message.buf, n) <
          0) {  // Write to all 3, 4 if redundancy off, file descriptors
        message.status_code = 500;
        return -1;
      }
    }
    if (total_bytes == message.content_length) break;
  }
  close(fd[0]);
  close(fd[1]);
  close(fd[2]);
  close(fd[3]);
  return 0;
}

void error_check(int retval, const char *func_name) {
  if (retval < 0) err(EXIT_FAILURE, "%s", func_name);
}