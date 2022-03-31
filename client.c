#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/fcntl.h>

int socket_fd;
char* string = NULL;
typedef struct {
	size_t cursor;
	char buffer[1000001];
} buffer_struct;

buffer_struct main_buffer;

void finalSigHandler() {
	shutdown(socket_fd, SHUT_RDWR);
	close(socket_fd);
	if (string != NULL) {
		free(string);
	}
	exit(0);
} // Обработчик нормальных сигналов завершения

void emergency(const char* message) {
	perror(message);
	shutdown(socket_fd, SHUT_RDWR);
	close(socket_fd);
	if (string != NULL) {
		free(string);
	}
	exit(1);
} // Обработчик экстренных сценариев завершения

void setupConnection(const char* address_params[]) {
	socket_fd = socket(AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in address;
	address.sin_family = AF_INET;
	address.sin_port = htons(strtol(address_params[1], NULL, 10));
	address.sin_addr.s_addr = inet_addr(address_params[0]);
	int connection_status = connect(
		socket_fd, (struct sockaddr*)&address, sizeof(struct sockaddr_in));
	if (connection_status == -1) {
		emergency("Could not connect.\n");
	}
} // Настройка соединения с сервером

void read_write() {
	char buffer[4096];
	ssize_t code;
	while ((code = read(STDIN_FILENO, buffer, sizeof(buffer))) > 0) { // Читаем пока можем
		for (ssize_t i = 0; i < code; ++i) {
			main_buffer.buffer[main_buffer.cursor] = buffer[i];
			++main_buffer.cursor;
			if (buffer[i] == '\n') { // Если нашли enter, кидаем текст в сокет
				if (write(socket_fd, main_buffer.buffer, main_buffer.cursor) < 0) {
					emergency("Error while writing to socket.\n");
				}
				main_buffer.cursor = 0;
			}
		}
	}
	if ((code < 0) && (errno != EAGAIN)) {
		emergency("Error reading line.\n");
	}
} // Читаем со стандартного потока ввода и пишем в сокет

int setupEpoll() {
	int queue = epoll_create1(0);
	struct epoll_event socket_ev = {.events = EPOLLIN | EPOLLERR | EPOLLHUP, .data.fd = socket_fd}; // добавляем сокет в epoll
	if (epoll_ctl(queue, EPOLL_CTL_ADD, socket_fd, &socket_ev) < 0) {
		emergency("Error during epoll setup occurred.\n");
	}

	int flags = fcntl(STDIN_FILENO, F_GETFL); // Делаем ввод неблокирующим
	flags |= O_NONBLOCK;
	fcntl(STDIN_FILENO, F_SETFL, flags);

	struct epoll_event stdin_ev = {.events = EPOLLIN | EPOLLHUP, .data.fd = STDIN_FILENO}; // Добавляем стандартный поток в epoll
	if (epoll_ctl(queue, EPOLL_CTL_ADD, STDIN_FILENO, &stdin_ev) < 0) {
		emergency("Error during epoll setup occurred 2.\n");
	}

	return queue;
} // Настраиваем epoll

int main(int argc, char* argv[]) { // Аргументы - ip и port
	struct sigaction action_final = {.sa_handler = finalSigHandler, .sa_flags = SA_RESTART}; // Настраиваем обработчик сигналов
	sigaction(SIGTERM, &action_final, NULL);
	sigaction(SIGINT, &action_final, NULL);

	if (argc != 3) {
		fprintf(stderr, "Wrong number of arguments.\n");
		exit(1);
	}

	main_buffer.cursor = 0;
	memset(main_buffer.buffer, 0, 1000001);

	const char* connection_params[2] = {argv[1], argv[2]};
	setupConnection(connection_params);

	int epoll_fd = setupEpoll();
	struct epoll_event events[SOMAXCONN];

	int ended = 0;
	while (ended == 0) {
		int n = epoll_wait(epoll_fd, events, (int)SOMAXCONN, -1);
		if (n < 0) {
			finalSigHandler();
		}
		for (int i = 0; (i < n) && (ended == 0); ++i) {
			if (events[i].data.fd == STDIN_FILENO) {
				read_write();
			} else {
				ended = 1;
			}
		}
	}

	shutdown(socket_fd, SHUT_RDWR);
	close(socket_fd);
	if (string != NULL) {
		free(string);
	}
	return 0;
}
