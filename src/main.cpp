#include <stdio.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>


int main(int argc, char **argv){

    char buf[256];
    sockaddr_in struktura;
    struktura.sin_family = AF_INET;
    struktura.sin_addr.s_addr = inet_addr("127.0.0.1");
    struktura.sin_port = htons(1234);

    int fd = socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    if (fd < 0){
        printf("Blad funkcji Socket %s \n", strerror(errno));
        return 1;
    }
    

    int serwer = bind(fd,(struct sockaddr *)&struktura,sizeof(struktura));

    if (serwer < 0){
        printf("Blad funkcji Bind %s \n", strerror(errno));
        return 1;
    }

    listen(fd,5);

    while(1){

        sockaddr_in client_data;
        socklen_t client_data_len = sizeof(client_data);

        int client = accept(fd,(sockaddr *)&client_data,&client_data_len);
        int client_read = read(client,buf,sizeof(buf));

        printf("Adres klienta: %s, Port klienta %d \n", inet_ntoa(client_data.sin_addr), ntohs(client_data.sin_port));

        write(client,buf,client_read);

        shutdown(client,SHUT_RDWR);
        close(client);

    }

    close(fd);

    return 0;

}
