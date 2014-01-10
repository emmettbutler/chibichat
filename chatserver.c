#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define PORT "9034"   // port we're listening on
#define MAXUSERS 100

int sendall(char *buf, int fromsock);
int prepare_client_buffer(char *buf, int fromsock);
int get_nick(int sockfd, char *buffer);
int set_nick(int sockfd, char *nick);
int do_command(char *buf, int sockfd);

char nicknames[MAXUSERS][256];
fd_set master;    // master file descriptor list
int fdmax;        // maximum file descriptor number
int listener;     // listening socket descriptor

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int main(void)
{
    fd_set read_fds;  // temp file descriptor list for select()

    int newfd;        // newly accept()ed socket descriptor
    struct sockaddr_storage remoteaddr; // client address
    socklen_t addrlen;

    char buf[512];    // buffer for client data
    memset(buf, '\0', sizeof(buf));
    int nbytes;

    char remoteIP[INET6_ADDRSTRLEN];

    int yes = 1;        // for setsockopt() SO_REUSEADDR, below
    int i, j, rv;

    struct addrinfo hints, *ai, *p;

    for(i = 0; i < sizeof(nicknames); i++){
        memset(nicknames, '\0', 256*10);
    }

    FD_ZERO(&master);    // clear the master and temp sets
    FD_ZERO(&read_fds);

    // get us a socket and bind it
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    if ((rv = getaddrinfo(NULL, PORT, &hints, &ai)) != 0) {
        fprintf(stderr, "tinychat: %s\n", gai_strerror(rv));
        exit(1);
    }

    for(p = ai; p != NULL; p = p->ai_next) {
        listener = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (listener < 0) {
            continue;
        }

        // lose the pesky "address already in use" error message
        setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));

        if (bind(listener, p->ai_addr, p->ai_addrlen) < 0) {
            close(listener);
            continue;
        }

        break;
    }

    // if we got here, it means we didn't get bound
    if (p == NULL) {
        fprintf(stderr, "tinychat: failed to bind\n");
        exit(2);
    }

    freeaddrinfo(ai); // all done with this

    // listen
    if (listen(listener, 10) == -1) {
        perror("listen");
        exit(3);
    }

    // add the listener to the master set
    FD_SET(listener, &master);

    // keep track of the biggest file descriptor
    fdmax = listener; // so far, it's this one

    // main loop
    for(;;) {
        read_fds = master; // copy it
        if (select(fdmax+1, &read_fds, NULL, NULL, NULL) == -1) {
            perror("select");
            exit(4);
        }

        // run through the existing connections looking for data to read
        for(i = 0; i <= fdmax; i++) {
            if (FD_ISSET(i, &read_fds)) { // we got one!!
                if (i == listener) {
                    // handle new connections
                    addrlen = sizeof remoteaddr;
                    newfd = accept(listener,
                        (struct sockaddr *)&remoteaddr,
                        &addrlen);

                    if (newfd == -1) {
                        perror("accept");
                    } else {
                        FD_SET(newfd, &master); // add to master set
                        if (newfd > fdmax) {    // keep track of the max
                            fdmax = newfd;
                        }
                        printf("tinychat: new connection from %s on socket %d\n",
                            inet_ntop(remoteaddr.ss_family,
                                get_in_addr((struct sockaddr*)&remoteaddr),
                                remoteIP, INET6_ADDRSTRLEN),
                            newfd);
                        char greeting[100];
                        char nick[256];
                        get_nick(newfd, nick);
                        sprintf(greeting, "Hello. Your name is %s\nType '\\nick <new_name>' to change your name\n", nick);
                        char mess[256] = {0};
                        sprintf(mess, "New user joined the server.\n");
                        sendall(mess, -1);
                        send(newfd, greeting, strlen(greeting), 0);
                    }
                } else {
                    // handle data from a client
                    if ((nbytes = recv(i, buf, sizeof buf, 0)) <= 0) {
                        // got error or connection closed by client
                        if (nbytes == 0) {
                            // connection closed
                            printf("tinychat: socket %d hung up\n", i);
                        } else {
                            perror("recv");
                        }
                        close(i); // bye!
                        FD_CLR(i, &master); // remove from master set
                    } else {
                        int res = do_command(buf, i);
                        if (res == -1) {
                            sendall(buf, i);
                        }
                        memset(buf, 0, sizeof(buf));
                    }
                }
            }
        }
    }
    return 0;
}

int sendall(char *buf, int fromsock){
    int j, nbytes;
    nbytes = prepare_client_buffer(buf, fromsock);
    for(j = 0; j <= fdmax; j++) {
        if (FD_ISSET(j, &master)) {
            if (j != listener && j != fromsock) {
                if (send(j, buf, nbytes, 0) == -1) {
                    perror("send");
                }
            }
        }
    }
}

int prepare_client_buffer(char *buf, int fromsock) {
    int i;
    char leader[100];
    char nickname[256] = {0};
    char *temp = (char *)malloc(strlen(buf)+1);
    memset(temp, 0, sizeof(temp));
    memcpy(temp, buf, strlen(buf));
    temp[strlen(buf)] = '\0';
    memset(buf, 0, sizeof(buf));
    if (fromsock > 0) {
        get_nick(fromsock, nickname);
        sprintf(leader, "%s: ", nickname);
    } else {
        sprintf(leader, "Server: ");
    }
    memcpy(buf, leader, strlen(leader));
    for (i = 0; i < strlen(temp); i++) {
        buf[strlen(leader)+i] = temp[i];
    }
    buf[strlen(buf)] = '\0';
    free(temp);
    return strlen(buf);
}

int get_nick(int sockfd, char *buffer) {
    if (strlen(nicknames[sockfd]) == 0) {
        strcpy(buffer, "empty");
    } else {
        strcpy(buffer, nicknames[sockfd]);
    }
    return 0;
}

int set_nick(int sockfd, char *nick){
    strcpy(nicknames[sockfd], nick);
    return 0;
}

int do_command(char *buf, int sockfd){
    if (buf[0] != '\\') {
        return -1;
    }
    int in_nick = 0, in_command = 1, j = 0;
    char this_nick[256] = {0}, oldnick[256] = {0};
    get_nick(sockfd, oldnick);
    if (strstr(buf, "\\nick") != NULL) {
        char *cr = buf;
        while (*cr != '\n') {
            if (*cr == '\\' && !in_command) {
                in_command = 1;
            }
            if (*cr == ' ' && in_command && !in_nick) {
                in_command = 0;
            }
            if (in_nick || (*cr != ' ' && !in_command && !in_nick)) {
                in_nick = 1;
                this_nick[j++] = *cr;
            }
            cr++;
        }
        this_nick[strlen(this_nick)-1] = '\0';
        printf("tinychat: Setting nickname to %s\n", this_nick);
        char mess[256] = {0};
        sprintf(mess, "%s changed nickname to %s\n", oldnick, this_nick);
        sendall(mess, -1);
        set_nick(sockfd, this_nick);
        return 1;
    }
    return 0;
}
