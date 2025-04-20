#include <sys/socket.h>
#include <netinet/ip.h>
#include <unistd.h>
#include <iostream>

int main() {
    // get socket (allocate socket and get client socket fd)
    int client_fd = socket(AF_INET, SOCK_STREAM, 0);

    // set socket adresses
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(1234);        // port
    addr.sin_addr.s_addr = htonl(0);    // wildcard IP 0.0.0.0

    // set as connector socket (client) and initiate connection with server by putting in same same address (port) as server
    if(connect(client_fd, (const struct sockaddr *)&addr, sizeof(addr)) != 0){
        std::cerr << "Error connecting to server\n";
        return 1;
    }

    // close client socket 
    close(client_fd);

    return 0;
}