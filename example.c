#include "doops.h"
#include <stdio.h>
#ifndef _WIN32
    #include <sys/socket.h>
    #include <netinet/in.h>
#endif

#define SOCKET_DATA     "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-type: text/html\r\nContent-length: 11\r\n\r\nhello world"

int create_socket(int port) {
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
    int sockfd, connfd, len; 
    struct sockaddr_in servaddr, cli; 
  
    sockfd = socket(AF_INET, SOCK_STREAM, 0); 
    memset(&servaddr, 0, sizeof(servaddr)); 
  
    servaddr.sin_family = AF_INET; 
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY); 
    servaddr.sin_port = htons(port); 

    bind(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr));

    listen(sockfd, 1024);

    return sockfd;
}

int main() {
    struct doops_loop loop;
    loop_init(&loop);

    loop_code(&loop, {
        fprintf(stdout, "HELLO WORLD!\n");
    }, 1000);

    loop_code(&loop, {
        fprintf(stdout, "HELLO WORLD 2!\n");
    }, 750);

    loop_on_read(&loop, {
        int socket = accept(loop_event_socket(loop), NULL, NULL);

        send(socket, SOCKET_DATA, sizeof(SOCKET_DATA) - 1, 0);
#ifdef _WIN32
        closesocket(socket);
#else
        close(socket);
#endif
    });
    loop_add_io(&loop, create_socket(8080), DOOPS_READ);
    loop_run(&loop);
    loop_deinit(&loop);

    return 0;
}
