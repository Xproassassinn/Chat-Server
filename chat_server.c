/*
 * Copyright 2014 - 2019
 *
 * Author:      Yorick de Wid
 * Description: Simple chatroom in C
 * Version:     1.4
 */

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>
#include <signal.h>

#define MAX_CLIENTS 100

static unsigned int cli_count = 0;
static int uid = 10;

/* Client structure */
typedef struct {
    struct sockaddr_in addr; /* Client remote address */
    int connfd;              /* Connection file descriptor */
    int uid;                 /* Client unique identifier */
    char name[32];           /* Client name */
} client_t;

client_t *clients[MAX_CLIENTS];

/* Add client to queue */
void queue_add(client_t *cl){
    int i;
    for (i = 0; i < MAX_CLIENTS; ++i) {
        if (!clients[i]) {
            clients[i] = cl;
            return;
        }
    }
}

/* Delete client from queue */
void queue_delete(int uid){
    int i;
    for (i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i]) {
            if (clients[i]->uid == uid) {
                clients[i] = NULL;
                return;
            }
        }
    }
}

/* Send message to all clients but the sender */
void send_message(char *s, int uid){
    int i;
    for (i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i]) {
            if (clients[i]->uid != uid) {
                if (write(clients[i]->connfd, s, strlen(s)) < 0) {
                    perror("Write to descriptor failed");
                    exit(-1);
                }
            }
        }
    }
}

/* Send message to all clients */
void send_message_all(char *s){
    int i;
    for (i = 0; i <MAX_CLIENTS; ++i){
        if (clients[i]) {
            if (write(clients[i]->connfd, s, strlen(s)) < 0) {
                perror("Write to descriptor failed");
                exit(-1);
            }
        }
    }
}

/* Send message to sender */
void send_message_self(const char *s, int connfd){
    if (write(connfd, s, strlen(s)) < 0) {
        perror("Write to descriptor failed");
        exit(-1);
    }
}

/* Send message to client */
void send_message_client(char *s, int uid){
    int i;
    for (i = 0; i < MAX_CLIENTS; ++i){
        if (clients[i]) { 
            if (clients[i]->uid == uid) {
                if (write(clients[i]->connfd, s, strlen(s))<0) {
                    perror("Write to descriptor failed");
                    exit(-1);
                }
            }
        }
    }
}

/* Send list of active clients */
void send_active_clients(int connfd){
    int i;
    char s[64];
    for (i = 0; i < MAX_CLIENTS; ++i){
        if (clients[i]) {
            sprintf(s, "<< [%d] %s\r\n", clients[i]->uid, clients[i]->name);
            send_message_self(s, connfd);
        }
    }
}

/* Strip CRLF */
void strip_newline(char *s){
    while (*s != '\0') {
        if (*s == '\r' || *s == '\n') {
            *s = '\0';
        }
        s++;
    }
}

/* Print ip address */
void print_client_addr(struct sockaddr_in addr){
    printf("%d.%d.%d.%d",
        addr.sin_addr.s_addr & 0xFF,
        (addr.sin_addr.s_addr & 0xFF00)>>8,
        (addr.sin_addr.s_addr & 0xFF0000)>>16,
        (addr.sin_addr.s_addr & 0xFF000000)>>24);
}

/* Handle all communication with the client */
void *handle_client(void *arg){
    char buff_out[2048];
    char buff_in[1024];
    int rlen;

    cli_count++;
    client_t *cli = (client_t *)arg;

    printf("<< accept ");
    print_client_addr(cli->addr);
    printf(" referenced by %d\n", cli->uid);

    sprintf(buff_out, "<< %s has joined\r\n", cli->name);
    send_message_all(buff_out);

    send_message_self("<< see /help for assistance\r\n", cli->connfd);

    /* Receive input from client */
    while ((rlen = read(cli->connfd, buff_in, sizeof(buff_in)-1)) > 0) {
        buff_in[rlen] = '\0';
        buff_out[0] = '\0';
        strip_newline(buff_in);

        /* Ignore empty buffer */
        if (!strlen(buff_in)) {
            continue;
        }

        /* Special options */
        if (buff_in[0] == '/') {
            char *command, *param;
            command = strtok(buff_in," ");
            if (!strcmp(command, "/quit")) {
                break;
            } else if (!strcmp(command, "/ping")) {
                send_message_self("<< pong\r\n", cli->connfd);
            } else if (!strcmp(command, "/nick")) {
                param = strtok(NULL, " ");
                if (param) {
                    char *old_name = strdup(cli->name);
                    strcpy(cli->name, param);
                    sprintf(buff_out, "<< %s is now known as %s\r\n", old_name, cli->name);
                    free(old_name);
                    send_message_all(buff_out);
                } else {
                    send_message_self("<< name cannot be null\r\n", cli->connfd);
                }
            } else if (!strcmp(command, "/msg")) {
                param = strtok(NULL, " ");
                if (param) {
                    int uid = atoi(param);
                    param = strtok(NULL, " ");
                    if (param) {
                        sprintf(buff_out, "[PM][%s]", cli->name);
                        while (param != NULL) {
                            strcat(buff_out, " ");
                            strcat(buff_out, param);
                            param = strtok(NULL, " ");
                        }
                        strcat(buff_out, "\r\n");
                        send_message_client(buff_out, uid);
                    } else {
                        send_message_self("<< message cannot be null\r\n", cli->connfd);
                    }
                } else {
                    send_message_self("<< reference cannot be null\r\n", cli->connfd);
                }
            } else if(!strcmp(command, "/list")) {
                sprintf(buff_out, "<< clients %d\r\n", cli_count);
                send_message_self(buff_out, cli->connfd);
                send_active_clients(cli->connfd);
            } else if (!strcmp(command, "/help")) {
                strcat(buff_out, "<< /quit     Quit chatroom\r\n");
                strcat(buff_out, "<< /ping     Server test\r\n");
                strcat(buff_out, "<< /nick     <name> Change nickname\r\n");
                strcat(buff_out, "<< /msg      <reference> <message> Send private message\r\n");
                strcat(buff_out, "<< /list     Show active clients\r\n");
                strcat(buff_out, "<< /help     Show help\r\n");
                send_message_self(buff_out, cli->connfd);
            } else {
                send_message_self("<< unknown command\r\n", cli->connfd);
            }
        } else {
            /* Send message */
            snprintf(buff_out, sizeof(buff_out), "[%s] %s\r\n", cli->name, buff_in);
            send_message(buff_out, cli->uid);
        }
    }

    /* Close connection */
    sprintf(buff_out, "<< %s has left\r\n", cli->name);
    send_message_all(buff_out);
    close(cli->connfd);

    /* Delete client from queue and yield thread */
    queue_delete(cli->uid);
    printf("<< quit ");
    print_client_addr(cli->addr);
    printf(" referenced by %d\n", cli->uid);
    free(cli);
    cli_count--;
    pthread_detach(pthread_self());

    return NULL;
}

int main(int argc, char *argv[]){
    int listenfd = 0, connfd = 0;
    struct sockaddr_in serv_addr;
    struct sockaddr_in cli_addr;
    pthread_t tid;

    /* Socket settings */
    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(5000);

    /* Ignore pipe signals */
    signal(SIGPIPE, SIG_IGN);

    /* Bind */
    if (bind(listenfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Socket binding failed");
        return 1;
    }

    /* Listen */
    if (listen(listenfd, 10) < 0) {
        perror("Socket listening failed");
        return 1;
    }

    printf("<[ SERVER STARTED ]>\n");

    /* Accept clients */
    while (1) {
        socklen_t clilen = sizeof(cli_addr);
        connfd = accept(listenfd, (struct sockaddr*)&cli_addr, &clilen);

        /* Check if max clients is reached */
        if ((cli_count + 1) == MAX_CLIENTS) {
            printf("<< max clients reached\n");
            printf("<< reject ");
            print_client_addr(cli_addr);
            printf("\n");
            close(connfd);
            continue;
        }

        /* Client settings */
        client_t *cli = (client_t *)malloc(sizeof(client_t));
        cli->addr = cli_addr;
        cli->connfd = connfd;
        cli->uid = uid++;
        sprintf(cli->name, "%d", cli->uid);

        /* Add client to the queue and fork thread */
        queue_add(cli);
        pthread_create(&tid, NULL, &handle_client, (void*)cli);

        /* Reduce CPU usage */
        sleep(1);
    }
}
