//
// Created by jiarui on 5/17/22.
//
/*
 * proxy.c - ICS Web proxy
 *
 *
 */

#include "csapp.h"
#include <stdarg.h>
#include <sys/select.h>
#include <sys/poll.h>

#define DEBUG

#ifdef DEBUG
FILE *logfile;
#endif


typedef enum CONNECTION_STATUS {
    SEND_HEADER,
    SEND_BODY,
    RECEIVE_HEADER,
    RECEIVE_BODY,
} CONNECTION_STATUS_T;

typedef struct connection {
    int client_fd;
    int server_fd;
    size_t size;
    CONNECTION_STATUS_T status;
    ssize_t content_left;
    char *uri;
    struct sockaddr_in addin;
} connection_t;

connection_t *conncetion_init(int client_fd, struct sockaddr_in *addr);


void connection_delete(connection_t *connection);

size_t read_header(int server_fd, int client_fd, long *content_length);

/*
 * Function prototypes
 */
int parse_uri(char *uri, char *hostname, char *path, char *port);

void format_log_entry(char *logstring, struct sockaddr_in *sockaddr, char *uri, size_t size);

size_t receive(int client_fd, rio_t *server_rio);


void *proxy_wrapper(void *args);

void unblock(int fd);

void block(int fd);

/*
 * main - Main routine for the proxy program
 */
int main(int argc, char **argv) {

  signal(SIGPIPE, SIG_IGN);

#ifdef DEBUG
  logfile = fopen("log.txt", "w");
#endif

  /* Check arguments */
  if (argc != 2) {
    fprintf(stderr, "Usage: %s <port number>\n", argv[0]);
    exit(0);
  }
  unsigned long port;
  int errsv = errno;
  errno = 0;
  char *endptr;
  port = strtoul(argv[1], &endptr, 10);
  if (errno) {
    unix_error("Invalid Port");
    exit(errno);
  }
  if (port > 65535) {
    app_error("Invalid Port: should be in [0,65535]\n");
  }

  if (endptr - argv[1] != strlen(argv[1])) {
    app_error("Invalid Port: should be number\n");
  }
  errno = errsv;


  int listen_fd = open_listenfd(argv[1]);
  if (listen_fd < 0) {
    exit(0);
  }

  unblock(listen_fd);

  socklen_t client_len;
  pthread_t tid;
  client_len = sizeof(struct sockaddr_storage);

  connection_t *connection_table[1000];
  struct pollfd plfd[1000] = {0};
  int fds = 1;
  plfd[0].fd = listen_fd;
  plfd[0].events = POLLIN;
  char buffer[MAXLINE];
  while (1) {

    int ret = poll(plfd, fds, -1);
    for (int i = 0; i < fds && ret > 0; ++i) {
      if (plfd[i].revents) {
        --ret;
      }
      if (plfd[i].revents & (POLLERR | POLLNVAL | POLLHUP)) {
        if (i == 0) {
          exit(-1);
        } else {
          int fd = plfd[i].fd;
          if (connection_table[fd] == NULL) {
            plfd[i].fd = 0;
            plfd[i].events = 0;
            continue;
          }
          int client_fd = connection_table[fd]->client_fd;
          int server_fd = connection_table[fd]->server_fd;
          close(client_fd);
          close(server_fd);
          connection_delete(connection_table[fd]);
          connection_table[client_fd] = NULL;
          connection_table[server_fd] = NULL;
          plfd[i].fd = 0;
          plfd[i].events = 0;
        }
      }
      if (i == 0 && plfd[0].revents & POLLIN) {
        struct sockaddr_in *new_addr = malloc(sizeof(struct sockaddr_in));
        int new_connection_fd = Accept(listen_fd, (SA *) new_addr, &client_len);
        connection_t *new_connection = conncetion_init(new_connection_fd, new_addr);
        if (new_connection) {
          connection_table[new_connection->server_fd] = new_connection;
          connection_table[new_connection->client_fd] = new_connection;
          if (connection_table[new_connection_fd]->content_left == 0) {
            int server_fd = new_connection->server_fd;
            int client_fd = new_connection->client_fd;
            unblock(server_fd);
            block(client_fd);
            connection_table[server_fd]->size = read_header(server_fd, client_fd,
                                                            &(connection_table[server_fd]->content_left));
            connection_table[server_fd]->status = RECEIVE_BODY;
            plfd[fds].fd = new_connection->client_fd;
            plfd[fds].events = 0;
            ++fds;
            plfd[fds].fd = new_connection->server_fd;
            plfd[fds].events = POLLIN;
            ++fds;
          } else {
            plfd[fds].fd = new_connection->client_fd;
            plfd[fds].events = POLLIN;
            ++fds;
            plfd[fds].fd = new_connection->server_fd;
            plfd[fds].events = 0;
            ++fds;
          }
        }
      } else if (plfd[i].revents & POLLIN) {
        plfd[i].revents = 0;
        int fd = plfd[i].fd;
        int receive_fd, sender_fd;
        int client_fd = connection_table[fd]->client_fd;
        int server_fd = connection_table[fd]->server_fd;
        size_t content_left = connection_table[fd]->content_left;
        CONNECTION_STATUS_T old_status = connection_table[fd]->status;
        switch (old_status) {
          case SEND_HEADER:
          case SEND_BODY:
            receive_fd = server_fd;
            break;
          case RECEIVE_HEADER:
          case RECEIVE_BODY:
            receive_fd = client_fd;
            break;
          default:
            continue;
        }
        long n;
        do {
          n = read(fd, buffer, content_left > MAXLINE ? MAXLINE : content_left);
#ifdef DEBUG
          printf("%*s", n, buffer);
#endif
          writeEH(receive_fd, buffer, n > content_left ? content_left : n);
          if (n >= content_left) {
            if (old_status == SEND_BODY) {
              unblock(server_fd);
              block(client_fd);
              connection_table[fd]->size = read_header(server_fd, client_fd, &connection_table[fd]->content_left);
              if (connection_table[fd]->content_left == 0) {
                close(client_fd);
                close(server_fd);
                connection_delete(connection_table[fd]);
                connection_table[client_fd] = NULL;
                connection_table[server_fd] = NULL;
                plfd[i].fd = 0;
                plfd[i].events = 0;

              } else {
                plfd[i].events = 0;
                plfd[i + 1].events = POLLIN;
                connection_table[fd]->status = RECEIVE_BODY;
              }


            } else if (old_status == RECEIVE_BODY) {
              Close(server_fd);
              Close(client_fd);
              format_log_entry(buffer, &connection_table[fd]->addin, connection_table[fd]->uri,
                               connection_table[fd]->size);
              printf("%s\n", buffer);

            }
            break;
          }
          connection_table[fd]->content_left -= n;
        } while (n > 0);

      }


    }


  }
}


void block(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, flags | ~O_NONBLOCK);
}


/*
 * parse_uri - URI parser
 *
 * Given a URI from an HTTP proxy GET request (i.e., a URL), extract
 * the host name, path name, and port.  The memory for hostname and
 * pathname must already be allocated and should be at least MAXLINE
 * bytes. Return -1 if there are any problems.
 */
int parse_uri(char *uri, char *hostname, char *pathname, char *port) {
  char *hostbegin;
  char *hostend;
  char *pathbegin;
  int len;

  if (strncasecmp(uri, "http://", 7) != 0) {
    hostname[0] = '\0';
    return -1;
  }

  /* Extract the host name */
  hostbegin = uri + 7;
  hostend = strpbrk(hostbegin, " :/\r\n\0");
  if (hostend == NULL)
    return -1;
  len = hostend - hostbegin;
  strncpy(hostname, hostbegin, len);
  hostname[len] = '\0';

  /* Extract the port number */
  if (*hostend == ':') {
    char *p = hostend + 1;
    while (isdigit(*p))
      *port++ = *p++;
    *port = '\0';
  } else {
    strcpy(port, "80");
  }

  /* Extract the path */
  pathbegin = strchr(hostbegin, '/');
  if (pathbegin == NULL) {
    pathname[0] = '\0';
  } else {
    pathbegin++;
    strcpy(pathname, pathbegin);
  }

  return 0;
}

/*
 * format_log_entry - Create a formatted log entry in logstring.
 *
 * The inputs are the socket address of the requesting client
 * (sockaddr), the URI from the request (uri), the number of bytes
 * from the server (size).
 */
void format_log_entry(char *logstring, struct sockaddr_in *sockaddr,
                      char *uri, size_t size) {
  time_t now;
  char time_str[MAXLINE];
  char host[INET_ADDRSTRLEN];

  /* Get a formatted time string */
  now = time(NULL);
  strftime(time_str, MAXLINE, "%a %d %b %Y %H:%M:%S %Z", localtime(&now));

  if (inet_ntop(AF_INET, &sockaddr->sin_addr, host, sizeof(host)) == NULL)
    unix_error("Convert sockaddr_in to string representation failed\n");

  /* Return the formatted log entry string */
  sprintf(logstring, "%s: %s %s %zu", time_str, host, uri, size);
}

connection_t *conncetion_init(int client_fd, struct sockaddr_in *addr) {

  rio_t rio;

  Rio_readinitb(&rio, client_fd);


  size_t n;
  char buffer[MAXLINE];
  char request_line[MAXLINE];


  n = Rio_readlinebEH(&rio, request_line, MAXLINE);
  if (n <= 0) {
    Close(client_fd);
    return NULL;
  }
  char *method;
  char host[MAXLINE];
  char path[MAXLINE];
  char port[MAXLINE];

  request_line[n] = 0;
  char *uri = strchr(request_line, ' ');
  if (uri == NULL) {
    Close(client_fd);
    return NULL;
  }
  uri[0] = 0;
  ++uri;
  method = request_line;
  char *protocol = strchr(uri, ' ');
  if (protocol == NULL) {
    Close(client_fd);
    return NULL;
  }
  protocol[0] = 0;
  char *urisv = malloc(protocol - uri + 1);
  urisv[protocol - uri] = 0;
  strncpy(urisv, uri, protocol - uri);
  ++protocol;

  parse_uri(uri, host, path, port);

  int server_fd = Open_clientfd(host, port);

  sprintf(buffer, "%s /%s %s", method, path, protocol);
#ifdef DEBUG
  printf("%*s", n, buffer);
#endif
  if (!Rio_writenEH(server_fd, buffer, strlen(buffer))) {
    Close(server_fd);
    Close(client_fd);
    return NULL;
  }

#ifdef DEBUG

  fprintf(logfile, "%s", buffer);
#endif
  unsigned long content_length = 0;
//Request Header
  while ((n = Rio_readlinebEH(&rio, buffer, MAXLINE)) != 0) {
#ifdef DEBUG
    printf("%*s", n, buffer);
#endif
    if (!Rio_writenEH(server_fd, buffer, n)) {
      Close(client_fd);
      Close(server_fd);
      return NULL;
    }
#ifdef DEBUG
    fprintf(logfile, "%s", buffer);
#endif
    if (strncasecmp("Content-Length: ", buffer, 14) == 0) {
      content_length = strtoul(&buffer[15], NULL, 10);
    }
    if (strncmp(buffer, "\r\n", 2) == 0) {
      break;
    }

  }
  unblock(client_fd);
  unblock(server_fd);
  connection_t *connection = malloc(sizeof(connection_t));
  connection->client_fd = client_fd;
  connection->server_fd = server_fd;
  connection->uri = urisv;
  connection->content_left = content_length;
  connection->size = 0;
  connection->status = SEND_BODY;
  return connection;

}

void unblock(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void connection_delete(connection_t *connection) {
  free(connection->uri);
  free(connection);
}

size_t read_header(int server_fd, int client_fd, long *content_length) {
  char buffer[MAXLINE];
  size_t n, size = 0;
  *content_length = 0;
  rio_t server_rio;
  rio_readinitb(&server_rio, server_fd);

  while ((n = Rio_readlinebEH(&server_rio, buffer, MAXLINE)) > 0) {
    size += n;
    if (strncasecmp(buffer, "Content-Length: ", 14) == 0) {
      *content_length = strtol(&buffer[15], NULL, 10);
    }
#ifdef DEBUG
    fprintf(logfile, "%*s", n, buffer);
#endif
#ifdef DEBUG
    printf("%*s", n, buffer);
#endif
    if (!Rio_writenEH(client_fd, buffer, n)) {
      return 0;
    }
#ifdef DEBUG
    fprintf(logfile, "%*s", n, buffer);
#endif

    if (!strncmp(buffer, "\r\n", 2))
      break;
  }
  if (n <= 0) {
    return 0;
  }
  size += *content_length;
  return size;
}