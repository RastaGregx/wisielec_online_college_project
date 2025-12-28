#include <stdio.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <cstdlib>
#include <poll.h> 
#include "game.h"


#define BUFFER_SIZE 256
#define INITIAL_CAPACITY 10

int main(int argc, char **argv){
    setvbuf(stdout, NULL, _IONBF, 0);
    sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = inet_addr("127.0.0.1");
    server.sin_port = htons(1234);

    int fd = socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    if (fd < 0){
        printf("Error Socket %s \n", strerror(errno));
        return 1;
    }
    
    int opt = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        printf("Error setsockopt %s \n", strerror(errno));
        return 1;
    }

    int serwer = bind(fd,(struct sockaddr *)&server,sizeof(server));

    if (serwer < 0){
        printf("Error Bind %s \n", strerror(errno));
        return 1;
    }

    listen(fd, SOMAXCONN);
    printf("Server is listening on port 1234...\n");

    Game game;
    game_init(game);

    //Dynamic poll Table init
    int capacity = INITIAL_CAPACITY;
    struct pollfd *fds = (struct pollfd *)malloc(capacity * sizeof(struct pollfd));
    if (!fds){
        printf("Memory allocation problem\n");
        close(fd);
        return 1;
    }

    int nfds = 1;
    
    // Init - first fd is the server's
    fds[0].fd = fd;
    fds[0].events = POLLIN;

    while(1){

        int ready_fd = poll(fds, nfds, -1);
        if (ready_fd == -1) {
            printf("Poll error: %s\n", strerror(errno));
            break;
        }

        if (fds[0].revents & POLLIN) {
            struct sockaddr_in client_data;
            socklen_t client_data_len = sizeof(client_data);
            int client = accept(fd, (struct sockaddr *)&client_data, &client_data_len);

            if (client < 0){
                printf("Error while accepting client: %s\n", strerror(errno));
                continue;
            }

            game_add_player(game, client);

            printf("New connection: %s:%d (fd=%d, Number of clients total: %d)\n", 
                   inet_ntoa(client_data.sin_addr), 
                   ntohs(client_data.sin_port),
                   client,
                   nfds);

            // Check if the table needs to be increased
            if (nfds >= capacity){
                capacity *= 2;
                struct pollfd *new_fds = (struct pollfd *)realloc(fds, capacity * sizeof(struct pollfd));
                if (!new_fds){
                    printf("Reallocation memory error - declining client\n");
                    close(client);
                    continue;
                }
                fds = new_fds;
                printf("Increased the capacity to: %d\n", capacity);
            }

            // Adding client
            fds[nfds].fd = client;
            fds[nfds].events = POLLIN;
            nfds++;
        }

        for (int i = 1; i < nfds; i++) {
            if (fds[i].revents & POLLIN) {
            char buf[BUFFER_SIZE];

            int bytes_read = read(fds[i].fd, buf, sizeof(buf) - 1);

            if (bytes_read <= 0) {
                if (bytes_read == 0) {
                    printf("Client fd=%d has disconnected\n", fds[i].fd);
                } else {
                    printf("Read error for fd=%d: %s\n", fds[i].fd, strerror(errno));
                }

                game_remove_player(game, fds[i].fd);
                close(fds[i].fd);

                fds[i] = fds[nfds - 1];
                nfds--;
                i--;
                continue;
            }

            buf[bytes_read] = '\0';

            printf("DEBUG RAW: [%s]\n", buf);

            std::string msg(buf);
            msg.erase(msg.find_last_not_of("\r\n") + 1);

            std::string response = game_handle_message(game, fds[i].fd, msg);

            if (!response.empty()) {
                for (int j = 1; j < nfds; j++) {
                    write(fds[j].fd, response.c_str(), response.size());
                }
    }
}
        }
    }
    for (int i = 0; i < nfds; i++){
        game_remove_player(game, fds[i].fd);
        close(fds[i].fd);
    }
    
    free(fds);
    return 0;
}
