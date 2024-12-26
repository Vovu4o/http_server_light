#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <signal.h>

#define PORT 8080

// Функция для отправки HTTP-ответа
void send_response(int client_sock, const char *header, const char *content_type, const char *body, size_t body_len) {
    char response[1024];
    sprintf(response, "%s", header);
    send(client_sock, response, strlen(response), 0);
    if (content_type) {
        sprintf(response, "Content-Type: %s\r\n", content_type);
        send(client_sock, response, strlen(response), 0);
    }
    send(client_sock, "Content-Length: ", 16, 0);
    sprintf(response, "%zu\r\n", body_len);
    send(client_sock, response, strlen(response), 0);
    send(client_sock, "\r\n", 2, 0);
    send(client_sock, body, body_len, 0);
}

// Функция для определения MIME-типа файла
const char* get_mime_type(const char *path) {
    if (strstr(path, ".html") || strstr(path, ".htm")) {
        return "text/html";
    } else if (strstr(path, ".jpg") || strstr(path, ".jpeg")) {
        return "image/jpeg";
    } else if (strstr(path, ".png")) {
        return "image/png";
    } else if (strstr(path, ".zip")) {
        return "application/zip";
    } else if (strstr(path, ".py") || strstr(path, ".c") ||
            strstr(path, ".h") || strstr(path, ".txt")) {
        return "text/plain";
    } else if (strstr(path, ".js")) {
        return "application/javascript";
    } else if (strstr(path, ".pdf")) {
        return "application/pdf";
    } else if (strstr(path, ".css")) {
        return "text/css";
    }
    return "application/octet-stream";  // Default MIME type
}

// Функция для обработки HTTP-запроса
void handle_request(int client_sock) {
    char buffer[2048];
    ssize_t bytes_received = recv(client_sock, buffer, sizeof(buffer) - 1, 0); // получаем сообщение из сокета, в случае успеха записываем
    // его длину
    if (bytes_received < 0) {
        perror("recv");
        close(client_sock);
        return;
    }

    buffer[bytes_received] = '\0';  // Завершаем строку запроса
    printf("Request: %s\n", buffer); // Выводим запрос в консоль

    // Извлекаем метод и путь из запроса
    char method[16], path[1024];
    if (sscanf(buffer, "%s %s", method, path) != 2) {
        perror("Invalid HTTP request format");
        close(client_sock);
        return;
    }

    // Если путь начинается с '/', убираем его для работы с файловой системой
    if (path[0] == '/') {
        memmove(path, path + 1, strlen(path));
    }

    // Если путь пустой, показываем главную страницу (index.html)
    if (strlen(path) == 0) {
        strcpy(path, "index.html");
    }

    // Преобразуем путь в абсолютный путь на файловой системе
    char file_path[1024 * 2]; // пофиксить
    size_t buffer_size = 1024;
    char *cur_dir = mmap(NULL, buffer_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    if (cur_dir == MAP_FAILED) {
        perror("Ошибка при выделении памяти через mmap");
    }

    if (getcwd(cur_dir, buffer_size) != NULL) {
        snprintf(file_path, sizeof(file_path) + sizeof(cur_dir), "%s", strcat(strcat(cur_dir, "/"), path));
    } else {
        perror("Ошибка при получении текущей директории");
    }


    if (munmap(cur_dir, buffer_size) == -1) {
        perror("Ошибка при освобождении памяти");
    }
    //snprintf(file_path, sizeof(file_path), "./%s", path);  // Считываем файл относительно текущей директории
    printf("%s\n", file_path);
    // Получаем информацию о файле
    struct stat st;
    if (stat(file_path, &st) < 0) {
        // Если файл не найден, отправляем 404
        if (errno == ENOENT) {
            const char *body = "<html><body><h1>404 Not Found</h1></body></html>";
            send_response(client_sock, "HTTP/1.1 404 Not Found\r\n", "text/html", body, strlen(body));
        } else {
            perror("stat");
            close(client_sock);
            return;
        }
    } else if (S_ISDIR(st.st_mode)  || // запрет на директории
    (strstr(file_path, "../")) || (strstr(file_path, "?")) || (strstr(file_path, "'")) ||
            (strstr(file_path, "\""))) {
        // Если это директория, показываем 403 (Forbidden)
        const char *body = "<html><body><h1>403 Forbidden</h1></body></html>";
        send_response(client_sock, "HTTP/1.1 403 Forbidden\r\n", "text/html", body, strlen(body));
    } else {
        // Если это файл, отправляем его клиенту

        // Открываем файл для чтения
        int fd = open(file_path, O_RDONLY);
        if (fd == -1) {
            perror("open");
            close(client_sock);
            return;
        }

        // Определяем MIME-тип
        const char *content_type = get_mime_type(path);

        // Читаем файл в память
        size_t len = st.st_size;
        char *file_data = mmap(NULL, len, PROT_READ, MAP_PRIVATE, fd, 0);
        if (file_data == MAP_FAILED) {
            perror("mmap");
            close(fd);
            close(client_sock);
            return;
        }

        // Отправляем файл как часть HTTP-ответа
        send_response(client_sock, "HTTP/1.1 200 OK\r\n", content_type, file_data, len);

        // Освобождаем ресурсы
        munmap(file_data, len);
        close(fd);
    }

    close(client_sock);
}

// Обработчик сигнала SIGCHLD для предотвращения зомби-процессов
void sigchld_handler(int signum) {

    while (waitpid(-1, NULL, WNOHANG) > 0); // Ожидаем завершения всех дочерних процессов
}

// Основная функция сервера
int main() {
    // Настроим обработчик сигнала SIGCHLD, чтобы избежать зомби-процессов
    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP; // Перезапуск системных вызовов
    sigemptyset(&sa.sa_mask);
    sigaction(SIGCHLD, &sa, NULL);

    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    // делаем порт вновь используемым
    if (setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) != 0) {
        perror("setsockopt failed");
        close(server_sock);
        exit(EXIT_FAILURE);
    }
    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    // привязка сокета к хосту и порту
    if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(server_sock);
        exit(EXIT_FAILURE);
    }
    // слушаем сокет
    if (listen(server_sock, 20) < 0) {
        perror("listen");
        close(server_sock);
        exit(EXIT_FAILURE);
    }

    printf("HTTP server listening on port %d...\n", PORT);

    while (1) {
        int client_sock = accept(server_sock, NULL, NULL);
        if (client_sock < 0) {
            perror("accept");
            continue;
        }

        // Важно: не создаем зомби-процессы
        pid_t pid = fork();
        if (pid == 0) {
            // Дочерний процесс
            close(server_sock);
            handle_request(client_sock);
            exit(0);
        } else if (pid > 0) {
            // Родительский процесс
            close(client_sock);

        } else {
            perror("fork");
            close(client_sock);
            exit(EXIT_FAILURE);
        }
    }

    close(server_sock);
    return 0;
}