#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

int main() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Ошибка с сокетами");
        return 1;
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(5050);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Ошибка подключения");
        return 1;
    }

    char *msg = "Hello(от .c)";
    send(sock, msg, strlen(msg), 0);

    char buffer[1024] = {0};
    int bytes = recv(sock, buffer, sizeof(buffer), 0);
    if (bytes > 0) {
        printf("Сервер ответил: %s\n", buffer);
    }

    close(sock);
    return 0;
}