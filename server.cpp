#include <sys/socket.h>
#include <netinet/ip.h>
#include <unistd.h>
#include <iostream>


void handle_request(int client_fd){
    std::cout << "Handled request from client: " << client_fd << "\n";
}


int main(){
    // allocates a TCP socket and returns the fd
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    std::cout << "Created socket\n";
    
    // set options for socket, most important is SO_REUSEADDR, without setting socket cannot bind to same IP:port after restart
    int val = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));
    std::cout << "Set socket options\n";
    
    // set socket addresses
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(1234);        // port
    addr.sin_addr.s_addr = htonl(0);    // wildcard IP 0.0.0.0
    
    // bind socket to the port
    if(bind(server_fd, (const struct sockaddr *)&addr, sizeof(addr)) != 0){
        std::cerr << "Failed to bind to port 6379\n";
        return 1;
    }
    std::cout << "Bound socket to port\n";
    
    // set port to be listening (server), rather then connecting (client)
    if(listen(server_fd, SOMAXCONN) != 0){
        std::cerr << "Failed to listen\n";
        return 1;
    }
    std::cout << "Set socket to listening\n";
    
    while(1){
        // accept incoming connections and get connection fd
        struct sockaddr_in client_addr = {};
        socklen_t addrlen = sizeof(client_addr);

        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &addrlen);
        if(client_fd < 0){
            continue;
        }
        
        handle_request(client_fd);
        close(client_fd); // close connection to client
    }

    // close the listening server socket
    close(server_fd);

    return 0;
}