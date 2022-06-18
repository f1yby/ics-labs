/*
 * proxy.c - ICS Web proxy
 *
 *
 */

#include "csapp.h"
#include <stdarg.h>
#include <sys/select.h>

//#define DEBUG

#ifdef DEBUG
FILE *logfile;
#endif

void unblock(int fd);

typedef struct fd_with_addr {
    int fd;
    struct sockaddr_in addr;
} fd_with_addr_t;

void handle_pipe(int sig) {
}

/*
 * Function prototypes
 */
int parse_uri(char *uri, char *hostname, char *path, char *port);

void format_log_entry(char *logstring, struct sockaddr_in *sockaddr, char *uri, size_t size);

size_t receive(int client_fd, int server_fd);


void *proxy_wrapper(void *args);

void proxy(fd_with_addr_t client_fd_addr);

/*
 * main - Main routine for the proxy program
 */
int main(int argc, char **argv) {

  struct sigaction sa;
  sa.sa_handler = handle_pipe;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sigaction(SIGPIPE, &sa, NULL);

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
  socklen_t client_len;
  pthread_t tid;
  client_len = sizeof(struct sockaddr_storage);


  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setschedpolicy(&attr, SCHED_FIFO);


  while (1) {

    fd_with_addr_t *client_fd_addr = malloc(sizeof(fd_with_addr_t));
    client_fd_addr->fd = Accept(listen_fd, (SA *) &client_fd_addr->addr, &client_len);
    Pthread_create(&tid, NULL, proxy_wrapper, client_fd_addr);
#ifdef DEBUG
    fprintf(logfile, "CONNECTED:\n");
#endif
  }

  exit(0);
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

size_t receive(int client_fd, int server_fd) {
  char buffer[MAXLINE];
  ssize_t n, size = 0, content_length = 0;

  while ((n = readline(server_fd, buffer, MAXLINE)) > 0) {
    size += n;
    if (strncasecmp(buffer, "Content-Length: ", 14) == 0) {
      content_length = strtoul(&buffer[15], NULL, 10);
    }
#ifdef DEBUG
    fprintf(logfile, "%*s", n, buffer);

#endif
    if (!Rio_writenEH(client_fd, buffer, n)) {
      return 0;
    }

    if (!strncmp(buffer, "\r\n", 2))
      break;
  }
  if (n <= 0) {
    return 0;
  }
  size += content_length;

#ifdef DEBUG
  clock_t c = clock();
  fprintf(logfile, "IN");
#endif
//    unblock(server_rio->rio_fd);
  while (content_length > 0) {
//    if ((n = Rio_readlinebEH(server_rio, buffer, content_length + 1 > MAXLINE ? MAXLINE : content_length + 1)) <= 0) {
//      if ((n = Rio_readnbEH(server_rio, buffer, 1)) <= 0) {

    if ((n = read(server_fd, buffer, content_length > MAXLINE ? MAXLINE : content_length)) <= 0) {
      return 0;
    }
#ifdef DEBUG
//   fprintf(logfile, "%*s", n, buffer);
    fprintf(logfile, "%zu %ld\n ", n, clock() - c);
    fflush(logfile);
#endif
    if (!Rio_writenEH(client_fd, buffer, n)) {
      return 0;
    }
    content_length -= n;
  }


  return size;
}

void *proxy_wrapper(void *args) {

  Pthread_detach(Pthread_self());
  fd_with_addr_t *client_fd_addr = (fd_with_addr_t *) args;
  proxy(*client_fd_addr);
  Close(client_fd_addr->fd);
  free(client_fd_addr);
#ifdef DEBUG
  fprintf(logfile, "Exit\n");
#endif

  return NULL;
}


void proxy(fd_with_addr_t client_fd_addr) {

  int client_fd = client_fd_addr.fd;


  size_t n;
  char buffer[MAXLINE];
  char request_line[MAXLINE];


  n = readline(client_fd, request_line, MAXLINE);
  if (n <= 0) {
    return;
  }
  char *method;
  char host[MAXLINE];
  char path[MAXLINE];
  char port[MAXLINE];

  request_line[n] = 0;
  char *uri = strchr(request_line, ' ');
  if (uri == NULL) {
    return;
  }
  uri[0] = 0;
  ++uri;
  method = request_line;
  char *protocol = strchr(uri, ' ');
  if (protocol == NULL) {
    return;
  }
  protocol[0] = 0;

  ++protocol;

  parse_uri(uri, host, path, port);

  int server_fd = open_clientfd(host, port);

  if (server_fd < 0) {
    return;
  }
  sprintf(buffer, "%s /%s %s", method, path, protocol);
  if (!Rio_writenEH(server_fd, buffer, strlen(buffer))) {
    Close(server_fd);
    return;
  }

#ifdef DEBUG
  fprintf(logfile, "%s", buffer);
#endif
  unsigned long content_length = 0;
//Request Header
  while ((n = readline(client_fd, buffer, MAXLINE)) > 0) {
    if (!Rio_writenEH(server_fd, buffer, n)) {
      Close(server_fd);
      return;
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
//Request Body
  while (content_length > 0) {
    n = read(client_fd, buffer, content_length > MAXLINE ? MAXLINE : content_length);
//    n = Rio_readnbEH(&rio, buffer, 1);
    if (n < 0) {
      Close(server_fd);
      return;
    }
#ifdef DEBUG
    fprintf(logfile, "%*s", n, buffer);
#endif
    if (!Rio_writenEH(server_fd, buffer, n)) {
      Close(server_fd);
      return;
    }
    content_length -= n;

  }
#ifdef DEBUG
  fflush(logfile);
#endif
  unsigned size = receive(client_fd, server_fd);
  Close(server_fd);
#ifdef DEBUG
  fflush(logfile);
#endif
  if (size == 0) {
    return;
  }
  format_log_entry(buffer, &client_fd_addr.addr, uri, size);

  printf("%s\n", buffer);
#ifdef DEBUG
  fprintf(logfile, "%s\n", buffer);
#endif


}

void unblock(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}
