#include <getopt.h>
#include <semaphore.h>

#include <utility>

#include "httpserver.h"

#define DEFAULT_NUM_THREADS 4
#define DEFAULT_PORT 80

typedef std::unordered_map<std::string, sem_t> lock_map_t;

sem_t global_lock;

bool redundancy = false;

typedef struct shared_data {
  std::queue<int> client_queue;
  pthread_mutex_t queue_mutex;
  pthread_cond_t cons_cond;
  lock_map_t lock_map;
} shared_data;

lock_map_t init_map() {
  lock_map_t locks;
  DIR *dir;
  struct dirent *ent;
  if (redundancy)
    dir = opendir("./files/copy1");
  else
    dir = opendir(".");

  if (dir != NULL) {
    while ((ent = readdir(dir)) != NULL) {
      sem_t lock;
      sem_init(&lock, 0, 1);
      locks.emplace(std::string(ent->d_name), lock);
    }
    closedir(dir);
  } else {
    // could not open directory
    err(EXIT_FAILURE, "opendir");
  }
  return locks;
}

void *process_request(void *data) {
  const std::unordered_map<unsigned short, const char *> status_strings = {
      {200, "OK"},
      {201, "Created"},
      {400, "Bad Request"},
      {403, "Forbidden"},
      {404, "Not Found"},
      {500, "Internal Server Error"}};  // just a way to look up status string
                                        // for response

  struct shared_data *shared = (struct shared_data *)data;
  while (true) {
    pthread_mutex_lock(&shared->queue_mutex);
    while (shared->client_queue.empty()) {
      printf("Waiting...\n");
      pthread_cond_wait(&shared->cons_cond, &shared->queue_mutex);
    }
    int client_fd = shared->client_queue.front();
    shared->client_queue.pop();
    pthread_mutex_unlock(&shared->queue_mutex);
    printf("[+] %lx handling client %d\n", pthread_self(), client_fd);
    HTTPObject message = {};
    message.client_fd =
        client_fd;  // TODO: This line causes seg fault when no GET request and
                    // Redundancy ON, or when set to anything
    ssize_t fd[4];
    read_request(message, fd, redundancy);
    if (fd[0] < 0) {
      send_response(message.client_fd, message.status_code,
                    status_strings.at(message.status_code), 0);
    } else {
      std::string filename(message.filename);
      bool new_file = false;
      if (shared->lock_map.find(filename) == shared->lock_map.end()) {
        error_check(sem_wait(&global_lock), "sem_wait");
        new_file = true;
      } else {
        sem_wait(&shared->lock_map.at(filename));
      }
      printf("[*] %lx inside critical region\n", pthread_self());

      if (strcmp(message.method, "GET") == 0) {
        send_response(message.client_fd, message.status_code,
                      status_strings.at(message.status_code),
                      message.content_length);
        if (get(fd[0], message) < 0) warn("get()");
      } else if (strcmp(message.method, "PUT") == 0) {
        if (message.content_length != 0)
          if (put(fd, message) < 0) warn("put()");
        send_response(message.client_fd, message.status_code,
                      status_strings.at(message.status_code), 0);
      }
      if (new_file) {
        sem_t lock;
        error_check(sem_init(&lock, 0, 1), "sem_init");
        shared->lock_map.emplace(filename, lock);
        error_check(sem_post(&global_lock), "sem_post");
      } else {
        sem_post(&shared->lock_map.at(filename));
      }
      printf("[*] %lx done with critical region\n", pthread_self());
    }
    close(client_fd);
  }
}

int make_redundant() {
  if (mkdir("./files/copy1", 0777) < 0 || mkdir("./files/copy2", 0777) < 0 ||
      mkdir("./files/copy3", 0777) < 0) {
    return -1;
  }
  DIR *dir;
  struct dirent *ent;
  if ((dir = opendir("./files")) != NULL) {
    while ((ent = readdir(dir)) != NULL) {
      if (strcmp(ent->d_name, ".") != 0 && strcmp(ent->d_name, "..") != 0 &&
          strcmp(ent->d_name, "copy1") != 0 &&
          strcmp(ent->d_name, "copy2") != 0 && strcmp(ent->d_name, "copy3")) {
        ssize_t src_fd = open(ent->d_name, O_RDONLY);
        char filePath1[26] = "./files/copy1/";
        char filePath2[26] = "./files/copy2/";
        char filePath3[26] = "./files/copy3/";
        ssize_t copy1_fd =
            open(strcat(filePath1, ent->d_name), O_CREAT | O_WRONLY, 0644);
        ssize_t copy2_fd =
            open(strcat(filePath2, ent->d_name), O_CREAT | O_WRONLY, 0644);
        ssize_t copy3_fd =
            open(strcat(filePath3, ent->d_name), O_CREAT | O_WRONLY, 0644);
        ssize_t n;
        char srcBuf[BUFFER_SIZE];
        while ((n = read(src_fd, srcBuf, BUFFER_SIZE)) != 0) {
          if (n < 0) {
            warn("%s", ent->d_name);
            break;
          }
          if (write(copy1_fd, srcBuf, n) < 0 ||
              write(copy2_fd, srcBuf, n) < 0 || write(copy3_fd, srcBuf, n) < 0)
            return -1;
        }
      }
    }
    closedir(dir);
    return 0;
  } else {
    // could not open directory
    err(EXIT_FAILURE, "Could not make server data redundant\n");
    return -1;
  }
}

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "Usage: %s <address> [port] [-N # of threads] [-r]\n",
            argv[0]);
    exit(EXIT_FAILURE);
  }
  int opt;
  int num_threads = DEFAULT_NUM_THREADS;
  while ((opt = getopt(argc, argv, "N:r")) != -1) {
    switch (opt) {
      case 'N':
        num_threads = atoi(optarg);
        break;
      case 'r':
        redundancy = true;
        break;

      default:
        return EXIT_FAILURE;
    }
  }

  if (num_threads < 1) {
    fprintf(stderr, "Number of threads must be an integer >= 1\n");
    return EXIT_FAILURE;
  }

  mkdir("files", 0777); // create directory to save files

  if (redundancy) {
    if (make_redundant() < 0)
      fprintf(stderr,
              "Failed to make source files redundant, or redundancy folders "
              "already exist\n");
  }

  unsigned short port = DEFAULT_PORT;
  if (optind < argc - 1) port = atoi(argv[optind + 1]);
  if (port < 80) {
    fprintf(stderr, "Port number must be >= 80\n");
    return EXIT_FAILURE;
  }

  struct sockaddr_in server_addr;
  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(port);
  server_addr.sin_addr.s_addr = get_address(argv[optind]);
  socklen_t addrlen = sizeof(server_addr);

  int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
  error_check(socket_fd, "socket()");

  int enable = 1;
  int ret =
      setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
  error_check(ret, "setsockopt()");
  ret = bind(socket_fd, (struct sockaddr *)&server_addr, addrlen);
  error_check(ret, "bind()");
  ret = listen(socket_fd, 500);
  error_check(ret, "listen()");

  struct sockaddr client_addr;
  socklen_t client_addrlen = sizeof(client_addr);

  printf("[*] Starting server at %s:%d with %d threads and redundancy %s\n",
         argv[optind], port, num_threads, redundancy ? "ON" : "OFF");

  shared_data common_data = {};
  error_check(pthread_mutex_init(&common_data.queue_mutex, NULL),
              "pthread_mutex_init");
  error_check(pthread_cond_init(&common_data.cons_cond, NULL),
              "pthread_cond_init");
  common_data.lock_map = init_map();

  error_check(sem_init(&global_lock, 0, 1), "sem_init");

  std::vector<pthread_t> threads(num_threads);

  for (int i = 0; i < num_threads; ++i) {
    error_check(
        pthread_create(&threads[i], NULL, &process_request, &common_data),
        "pthread_create()");
  }
  while (true) {
    int client_fd = accept(socket_fd, &client_addr, &client_addrlen);
    printf("accept: %d\n", client_fd);
    if (client_fd < 0) {
      warn("accept()");
      continue;
    }
    error_check(pthread_mutex_lock(&common_data.queue_mutex),
                "pthread_mutex_lock");
    common_data.client_queue.push(client_fd);
    error_check(pthread_cond_signal(&common_data.cons_cond),
                "pthread_cond_signal");
    error_check(pthread_mutex_unlock(&common_data.queue_mutex),
                "pthread_mutex_unlock");
  }
  close(socket_fd);

  error_check(pthread_mutex_destroy(&common_data.queue_mutex),
              "pthread_mutex_destroy");
  error_check(pthread_cond_destroy(&common_data.cons_cond),
              "pthread_cond_destroy");
  for (auto &x : common_data.lock_map)
    error_check(sem_destroy(&x.second), "sem_destroy");
  error_check(sem_destroy(&global_lock), "sem_destroy");

  return EXIT_SUCCESS;
}