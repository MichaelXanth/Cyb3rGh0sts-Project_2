#include "httpd.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <signal.h>

#define MAX_CONNECTIONS 1000

static int listenfd;
int * clients;
static void error(char *);
static void startServer(const char *);
static void respond(int);

static int clientfd;

static char *buf;

void serve_forever(const char *PORT) 
{
    struct sockaddr_in clientaddr;
    socklen_t addrlen;
    char c;

    int slot = 0;

    fprintf(stderr, "Server started %shttp://127.0.0.1:%s%s\n", "\033[92m", PORT,"\033[0m");
    
    // create shared memory for client slot array
    clients = mmap(NULL, sizeof(*clients)*MAX_CONNECTIONS, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0);

    // Setting all elements to -1: signifies there is no client connected
    int i;
    for (i = 0; i < MAX_CONNECTIONS; i++)
        clients[i] = -1;
    startServer(PORT);

    // Ignore SIGCHLD to avoid zombie threads
    signal(SIGCHLD, SIG_IGN);

    // ACCEPT connections
    while (1) {
        addrlen = sizeof(clientaddr);
        clients[slot] = accept(listenfd, (struct sockaddr *)&clientaddr, &addrlen);
        
        if (clients[slot] < 0) {
            perror("accept() error");
        } else {
        //    respond(slot);
            if (fork() == 0) { respond(slot); exit(0); }
        }

        while (clients[slot] != -1)
            slot = (slot + 1) % MAX_CONNECTIONS;
    }
}

// start server
void startServer(const char *port) 
{
    struct addrinfo hints, *res, *p;

    // getaddrinfo for host
    memset(&hints, 0, sizeof(hints));

    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags    = AI_PASSIVE;
    
    if (getaddrinfo(NULL, port, &hints, &res) != 0) {
        perror("getaddrinfo() error");
        exit(1);
    }
    
    // socket and bind
    for (p = res; p != NULL; p = p->ai_next) {
        int option = 1;
        listenfd = socket(p->ai_family, p->ai_socktype, 0);
        setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));

        if (listenfd == -1)
            continue;
        
        if (bind(listenfd, p->ai_addr, p->ai_addrlen) == 0)
            break;
    }

    if (p == NULL) {
        perror("socket() or bind()");
        exit(1);
    }

    freeaddrinfo(res);

    // listen for incoming connections
    if (listen(listenfd, 1000000) != 0) {
        perror("listen() error");
        exit(1);
    }
}

// get request header by name
char *request_header(const char *name) {
    header_t *h = reqhdr;
    while (h->name) {
        if (strcmp(h->name, name) == 0)
        return h->value;
        h++;
    }
    return NULL;
}

// get all request headers
header_t *request_headers(void) {
    return reqhdr;
}

// client connection
void respond(int n) 
{
    int rcvd, fd, bytes_read;
    char *ptr;

    buf  = malloc(65535);
    rcvd = recv(clients[n], buf, 65535, 0);


    // receive error
    if (rcvd < 0) 
        fprintf(stderr, ("recv() error\n"));

    // receive socket closed
    else if (rcvd == 0) 
        fprintf(stderr, "Client disconnected upexpectedly.\n");
    
    // message received
    else {
        buf[rcvd] = '\0';

        method = strtok(buf , " \t\r\n");
        uri    = strtok(NULL, " \t");
        prot   = strtok(NULL, " \t\r\n");

        
        fprintf(stderr, "\x1b[32m + [%s] %s\x1b[0m\n", method, uri);

        qs = strchr(uri, '?');

        // split URI
        if (qs) {
            *qs++ = '\0'; 

        // use an empty string
        } else {
            qs = uri - 1; 
        }

        header_t *h = reqhdr;
        char *t, *t2;
        
        while (h < reqhdr + 16) {
            char *k, *v;

            k = strtok(NULL, "\r\n: \t");
            if (!k)
                break;

            v = strtok(NULL, "\r\n");
            while (*v && *v == ' ')
                v++;

            h->name = k;
            h->value = v;
            h++;
            fprintf(stderr, "[H] %s: %s\n", k, v);
            t = v + 1 + strlen(v);
            if (t[1] == '\r' && t[2] == '\n')
                break;
        }
        
        t += 3; // now the *t shall be the beginning of user payload, after \r\n
        t2 = request_header("Content-Length"); // and the related header if there is
        payload = t;
        payload_size = t2 ? atol(t2) : (rcvd - (t - buf));

        // bind clientfd to stdout, making it easier to write
        clientfd = clients[n];
        dup2(clientfd, STDOUT_FILENO);
        close(clientfd);
        setvbuf(stdout, NULL, _IONBF, 0);   // no buffering for fast response

        // call router
        route();

        // tidy up
        fflush(stdout);
        shutdown(STDOUT_FILENO, SHUT_WR);
        close(STDOUT_FILENO);
    }

    // Closing SOCKET
    shutdown(
        clientfd,
        SHUT_RDWR); // All further send and recieve operations are DISABLED...
    close(clientfd);
    clients[n] = -1;
}
