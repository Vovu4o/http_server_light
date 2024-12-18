#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <arpa/inet.h>

#define PORT 8080
#define MAX_BUFFER_SIZE 1024

typedef struct {
    const char *path;
    const char *mime_type;
} FileData;

FileData files[] = {
        { "python.html", "text/html" },
        { "go.html", "text/html" },
        { "kotlin.html", "text/html" },
        { "python.jpg", "image/jpeg" },
        { "1.zip", "application/zip" },
};

const char *get_mime_type(const char *file_name) {
    for (int i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
        if (strstr(file_name, files[i].path)) {
            return files[i].mime_type;
        }
    }
    return "application/octet-stream";
}

void serve_file(int client_socket, const char *file_name) {
    int fd = open(file_name, O_RDONLY);
    if (fd == -1) {
        perror("Ошибка при открытии файла");
        return;
    }

    struct stat file_stat;
    if (fstat(fd, &file_stat) == -1) {
        perror("Ошибка при получении информации о файле");
        close(fd);
        return;
    }

    // Отображаем файл в память
    void *file_data = mmap(NULL, file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (file_data == MAP_FAILED) {
        perror("Ошибка при отображении файла в память");
        close(fd);
        return;
    }

    char response_header[MAX_BUFFER_SIZE];
    snprintf(response_header, sizeof(response_header),
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: %s\r\n"
             "Content-Length: %ld\r\n"
             "Connection: close\r\n"
             "\r\n",
             get_mime_type(file_name), file_stat.st_size);
    printf("HTTP/1.1 200 OK\n");
    // Отправляем заголовки
    send(client_socket, response_header, strlen(response_header), 0);

    // Отправляем данные файла
    send(client_socket, file_data, file_stat.st_size, 0);

    // Освобождаем ресурсы
    munmap(file_data, file_stat.st_size);
    close(fd);
}

void handle_request(int client_socket) {
    char buffer[MAX_BUFFER_SIZE];
    int bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
    if (bytes_received <= 0) {
        return;
    }

    buffer[bytes_received] = '\0'; // Завершаем строку

    // Логируем запрос
    printf("Запрос получен:\n%s\n", buffer);

    // Получаем первый путь запроса
    char method[16], path[256];
    sscanf(buffer, "%s %s", method, path);

    if (strncmp(method, "GET", 3) == 0) {
        // Убираем начальный слэш
        if (path[0] == '/') {
            memmove(path, path + 1, strlen(path));
        }

        // Ищем запрашиваемый файл
        int found = 0;
        for (int i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
            if (strcmp(path, files[i].path) == 0) {
                serve_file(client_socket, files[i].path);
                found = 1;
                break;
            }
        }

        if (!found) {
            // Если файл не найден, отправляем 404
            char response_header[] = "HTTP/1.1 404 Not Found\r\n\r\n";
            printf("\n%s\n", response_header);
            send(client_socket, response_header, strlen(response_header), 0);
        }
    }
}

int main() {
    // Создаем сокет
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        perror("Ошибка при создании сокета");
        return 1;
    }

    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    // Привязываем сокет к адресу
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("Ошибка при привязке сокета");
        close(server_socket);
        return 1;
    }

    // Начинаем прослушивание порта
    if (listen(server_socket, 5) == -1) {
        perror("Ошибка при прослушивании порта");
        close(server_socket);
        return 1;
    }

    printf("Сервер запущен на порту %d\n", PORT);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_len);

        if (client_socket == -1) {
            perror("Ошибка при принятии соединения");
            continue;
        }

        // Логируем информацию о клиенте
        printf("Соединение с клиентом: %s\n", inet_ntoa(client_addr.sin_addr));

        // Создаем дочерний процесс
        pid_t pid = fork();
        if (pid == 0) { // Дочерний процесс
            // Обрабатываем запрос в дочернем процессе
            close(server_socket); // Закрываем серверный сокет в дочернем процессе
            handle_request(client_socket);
            close(client_socket);
            exit(0); // Завершаем дочерний процесс
        } else if (pid > 0) { // Родительский процесс
            close(client_socket); // Родительский процесс продолжает слушать, закрывает клиентский сокет
        } else {
            perror("Ошибка при вызове fork");
            close(client_socket);
        }
    }

    // Закрываем серверный сокет (этот код не будет выполнен, так как сервер работает в бесконечном цикле)
    close(server_socket);

    return 0;
}
    /*// Создаем сокет
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        perror("Ошибка при создании сокета");
        return 1;
    }

    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    // Привязываем сокет к адресу
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("Ошибка при привязке сокета");
        close(server_socket);
        return 1;
    }

    // Начинаем прослушивание порта
    if (listen(server_socket, 5) == -1) {
        perror("Ошибка при прослушивании порта");
        close(server_socket);
        return 1;
    }

    printf("Сервер запущен на порту %d\n", PORT);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_socket = accept(server_socket, (struct sockaddr *) &client_addr, &client_len);

        if (client_socket == -1) {
            perror("Ошибка при принятии соединения");
            continue;
        }

        // Логируем информацию о клиенте
        printf("Соединение с клиентом: %s\n", inet_ntoa(client_addr.sin_addr));

        pid_t pid = fork();
        if (pid == 0) { // Дочерний процесс
            // Обрабатываем запрос в дочернем процессе
            close(server_socket); // Закрываем серверный сокет в дочернем процессе
            handle_request(client_socket);
            close(client_socket);
            exit(0); // Завершаем дочерний процесс
        } else if (pid > 0) {
            close(client_socket); // Родительский процесс продолжает слушать, закрывает клиентский сокет
        } else {
            perror("Ошибка при вызове fork");
            close(client_socket);
        }
    }

        // Обрабатываем запрос
        /*handle_request(client_socket);

        // Закрываем соединение с клиентом
        close(client_socket);
    //}

    // Закрываем серверный сокет
    close(server_socket);

    return 0;
} */