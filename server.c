#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>

static const size_t max_clients = 5; // Максимально возможное количество клиентов
size_t currently_connected = 0; // Подключённые на данный момент клиенты
FILE* write_file = NULL; // Файловый дескриптор файла, куда всё писать
int socket_serv_fd = -1; // Файловый дескриптор серверного сокета
sig_atomic_t ended = 0;

typedef struct {
	size_t cursor;
	char buffer[1000000]; // Будем действовать в предположении, что клиент не пишет строки длиннее 1e6 символов без энтера.
	size_t buffer_number;
	int client_fd;
	size_t array_pos;
} buffer_struct;

int* taken_buffer_slots; // Занятые слоты под буферы
buffer_struct** buffers; // Указатели на буферы

void finalSigHandler() {
	ended = 1;
	shutdown(socket_serv_fd, SHUT_RDWR);
	close(socket_serv_fd);

	fflush(write_file);
	fclose(write_file);
} // Обработчик сигналов завершения

void initializeBufferPointers() {
	taken_buffer_slots = (int*)malloc(max_clients * sizeof(int));
	memset(taken_buffer_slots, 0, max_clients * sizeof(int));
	buffers = (buffer_struct**)malloc(max_clients * sizeof(buffer_struct*));
} // Инициализатор глобальных переменных переменного размера

void freeDisconnectEverything() {
	for (size_t i = 0; i < max_clients; ++i) {
		if (taken_buffer_slots[i] == 1) {
			shutdown(buffers[i]->client_fd, SHUT_RDWR);
			close(buffers[i]->client_fd);
			free(buffers[i]);
		}
	}
	free(buffers);
	free(taken_buffer_slots);

	shutdown(socket_serv_fd, SHUT_RDWR);
	close(socket_serv_fd);
} // Очиститель динамически выделенной памяти и закрыватель подключённых сокетов

void shutdownClient(int client_fd, buffer_struct* buff_addr, int epoll_fd) {
	shutdown(client_fd, SHUT_RDWR);
	close(client_fd);

	--currently_connected;
	taken_buffer_slots[buff_addr->array_pos] = 0;
	free(buff_addr);
	epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
} // Завершить клиента

void setupFile(const char* path_to_log) {
	int fd;
	if ((fd = open(path_to_log, O_CREAT | O_TRUNC | O_RDWR, 0600)) < 0) {
		perror("Error occurred while creating log file.\n");
		exit(9);
	}
	close(fd);
	if ((write_file = fopen(path_to_log, "a")) == NULL) {
		perror("Error occurred while creating log file.\n");
		exit(9);
	}
} // Подготавливаем файл лога для работы с ним

void setupServer(const char* ip_addr, const char* rec_port, int queue) {
	int port = htons(strtol(rec_port, NULL, 10)); //порт
	socket_serv_fd = socket(AF_INET, SOCK_STREAM, 0);

	int flags = fcntl(socket_serv_fd, F_GETFL); // Делаем сокет неблокирующим и добавляем в epoll
	flags |= O_NONBLOCK;
	int res1 = fcntl(socket_serv_fd, F_SETFL, flags);
	struct epoll_event ev_s = {.events = EPOLLIN | EPOLLERR | EPOLLHUP, .data.fd = socket_serv_fd};
	int res2 = epoll_ctl(queue, EPOLL_CTL_ADD, socket_serv_fd, &ev_s);
	if (res1 < 0 || res2 < 0 || flags < 0 || socket_serv_fd < 0) {
		perror("Error creating server.\n");
		exit(10);
	}

	struct sockaddr_in address; // Завершаем сетевую настройку сокета
	address.sin_family = AF_INET;
	address.sin_port = port;
	address.sin_addr.s_addr = inet_addr(ip_addr);
	if (bind(socket_serv_fd, (struct sockaddr*)&address, sizeof(address))) {
		perror("Bind failed.\n");
		exit(2);
	}

	if (listen(socket_serv_fd, SOMAXCONN)) {
		perror("Error listening to the socket.\n");
		exit(3);
	}
} // Основные действия по настройке сервера

void accept_connection(int queue) {
	int client_fd;
	if ((client_fd = accept(socket_serv_fd, NULL, NULL)) < 0) { // Принимаем соединение
		if (errno == EINTR || errno == EBADF) {
			shutdown(socket_serv_fd, SHUT_RDWR);
			close(socket_serv_fd);
			exit(0);
		}
		perror("Error accepting the connection.\n");
		exit(4);
	}

	int flags = fcntl(client_fd, F_GETFL); // Делаем сокет неблокирующим

	if (flags < 0) {
		perror("Error occurred when tried to modify client FD.\n");
		shutdown(client_fd, SHUT_RDWR);
		close(client_fd);
		return;
	}

	flags |= O_NONBLOCK;
	if (fcntl(client_fd, F_SETFL, flags) < 0){
		perror("Error occurred when tried to modify client FD.\n");
		shutdown(client_fd, SHUT_RDWR);
		close(client_fd);
		return;
	}
	struct epoll_event ev = {.events = EPOLLIN | EPOLLERR | EPOLLHUP};

	static size_t received_connections = 0; // Заводим буффер, отвечающий за данный сокет
	buffer_struct* new_buffer = (buffer_struct*)malloc(sizeof(buffer_struct));
	new_buffer->cursor = 0;
	memset(new_buffer->buffer, '\0', 1000000);
	new_buffer->buffer_number = received_connections++;
	new_buffer->client_fd = client_fd;
	size_t pos = 0;
	for (; pos < max_clients; ++pos) {
		if (taken_buffer_slots[pos] == 0) {
			break;
		}
	}
	new_buffer->array_pos = pos;
	taken_buffer_slots[pos] = 1;
	buffers[pos] = new_buffer;
	ev.data.ptr = new_buffer;

	if (epoll_ctl(queue, EPOLL_CTL_ADD, client_fd, &ev) < 0) { // Добавляем сокет в epoll
		perror("Error occurred when tried to add client socket to epoll.\n");
		shutdown(client_fd, SHUT_RDWR);
		close(client_fd);
		taken_buffer_slots[pos] = 0;
		free(buffers[pos]);
	}
} // Обработчик новых подключений

void process_client(int epoll_fd, buffer_struct* buff_addr) {
	char buff[4096];
	ssize_t code;
	int client_fd = buff_addr->client_fd;
	while ((code = read(client_fd, buff, sizeof(buff))) > 0) { //Читаем, пока не закончатся символы
		for (ssize_t j = 0; j < code; ++j) {
			buff_addr->buffer[buff_addr->cursor] = buff[j];
			++buff_addr->cursor;

			if (buff_addr->cursor == 1000000) { // Если превысили лимит символов без enter, выкидываем клиента
				shutdownClient(client_fd, buff_addr, epoll_fd);
			}

			if (buff[j] == '\n') { // Если нашли enter, пишем в файл
				char output[1000000 + 100] = {0};
				if (snprintf(output, 100, "%zu - ", buff_addr->buffer_number) < 0) {
					perror("Error generating string to write.\n");
				}
				size_t prefix_len = strlen(output);
				strncpy(output + prefix_len, buff_addr->buffer, buff_addr->cursor);
				if (fprintf(write_file, "%s", output) < 0) {
					perror("Errors during writing to file occurred.\n");
				}

				memset(buff_addr->buffer, '\0', buff_addr->cursor);
				buff_addr->cursor = 0;
			}
		}
	}
	if (code == -1 && errno != EAGAIN) {
		perror("Error reading from socket.\n");
		exit(2);
	}
	if (code == 0) {
		shutdownClient(client_fd, buff_addr, epoll_fd);
	}
} // Обработчик уже действующих подключений

void deny_connection() {
	int client_fd;
	if ((client_fd = accept(socket_serv_fd, NULL, NULL)) < 0) {
		if (errno == EINTR || errno == EBADF) {
			shutdown(socket_serv_fd, SHUT_RDWR);
			close(socket_serv_fd);
			exit(0);
		}
		perror("Error accepting the overflow connection.\n");
		exit(5);
	}

	static const char exceeding_answer[] = "Sorry, you are exceeding the maximum number of connections.\n";

	write(client_fd, exceeding_answer, sizeof(exceeding_answer));
	shutdown(client_fd, SHUT_RDWR);
	close(client_fd);
} // Обработчик подключений, когда достигнут лимит

signed main(int argc, char* argv[]) // Подавать ip и порт как аргументы. Третьим аргументом подавать путь до файла с логом.
{
	struct sigaction action_final = {.sa_handler = finalSigHandler, // Настраиваем обработчик сигналов
		.sa_flags = SA_RESTART};
	sigaction(SIGTERM, &action_final, NULL);
	sigaction(SIGINT, &action_final, NULL);

	if (argc != 4) {
		fprintf(stderr, "Wrong number of arguments.\n");
		exit(1);
	}

	initializeBufferPointers();

	struct epoll_event events[SOMAXCONN];
	int queue = epoll_create1(0);

	setupServer(argv[1], argv[2], queue);
	setupFile(argv[3]);

	fprintf(stdout, "Server pid: %d\n", getpid());
	fflush(stdout);

	while (ended == 0) {
		int n = epoll_wait(queue, events, (int)SOMAXCONN, -1);
		for (int i = 0; i < n; ++i) {
			int current_fd = events[i].data.fd;
			if (current_fd == socket_serv_fd) {
				if (currently_connected < max_clients) {
					accept_connection(queue);
					++currently_connected;
				} else {
					deny_connection();
				}
			} else {
				process_client(queue, events[i].data.ptr);
			}
		}
	}

	freeDisconnectEverything();
	return 0;
}
