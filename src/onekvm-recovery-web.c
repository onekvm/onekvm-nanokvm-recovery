#include <arpa/inet.h>
#include <stdint.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "recovery-display.h"

#ifdef HAVE_OPENSSL
#include <openssl/err.h>
#include <openssl/ssl.h>
#endif

#define HTTP_PORT 80
#define HTTPS_PORT 443
#define HEADER_LIMIT 8192
#define MAX_STATIC (4LL * 1024 * 1024)
#define DEFAULT_WWW "/usr/share/onekvm-recovery"
#define DEFAULT_CERT "/etc/onekvm-recovery/cert.pem"
#define DEFAULT_KEY "/etc/onekvm-recovery/key.pem"
#define DEFAULT_CONFIG "/run/onekvm-recovery-data/upper/etc/onekvm/config.json"
#define DEFAULT_FIRMWARE "/run/onekvm-firmware/recovery-update.fwup"
#define INSTALL_FWUP "/usr/sbin/onekvm-recovery-install-fwup"
#define PREPARE_DATA "/usr/sbin/onekvm-recovery-prepare-data"
#define DEFAULT_PROGRESS "/run/onekvm-recovery-progress"
#define PROGRESS_STEP (512 * 1024)

static const char *www_root = DEFAULT_WWW;
#ifdef HAVE_OPENSSL
static const char *cert_path = DEFAULT_CERT;
static const char *key_path = DEFAULT_KEY;
#endif
static const char *config_path = DEFAULT_CONFIG;
static const char *firmware_path = DEFAULT_FIRMWARE;
static const char *progress_path = DEFAULT_PROGRESS;

struct conn {
	int fd;
#ifdef HAVE_OPENSSL
	SSL *ssl;
#endif
};

static char *json_skip_ws(char *cursor, char *end)
{
	while (cursor < end && isspace((unsigned char)*cursor))
		cursor++;
	return cursor;
}

static char *json_skip_string(char *cursor, char *end)
{
	if (cursor >= end || *cursor != '"')
		return NULL;
	cursor++;
	while (cursor < end) {
		if (*cursor == '\\') {
			cursor += (cursor + 1 < end) ? 2 : 1;
			continue;
		}
		if (*cursor == '"')
			return cursor + 1;
		cursor++;
	}
	return NULL;
}

static char *json_skip_value(char *cursor, char *end)
{
	int depth;

	cursor = json_skip_ws(cursor, end);
	if (cursor >= end)
		return NULL;
	if (*cursor == '"')
		return json_skip_string(cursor, end);
	if (*cursor == '{' || *cursor == '[') {
		depth = 1;
		cursor++;
		while (cursor < end && depth > 0) {
			if (*cursor == '"') {
				cursor = json_skip_string(cursor, end);
				if (!cursor)
					return NULL;
				continue;
			}
			if (*cursor == '{' || *cursor == '[')
				depth++;
			else if (*cursor == '}' || *cursor == ']')
				depth--;
			cursor++;
		}
		return depth == 0 ? cursor : NULL;
	}
	while (cursor < end && !strchr(",}] \t\r\n", *cursor))
		cursor++;
	return cursor;
}

static char *json_find_key(char *object, char *end, const char *key)
{
	size_t key_len = strlen(key);
	char *cursor = json_skip_ws(object, end);

	if (cursor >= end || *cursor != '{')
		return NULL;
	cursor++;
	while (cursor < end) {
		cursor = json_skip_ws(cursor, end);
		if (cursor >= end || *cursor == '}')
			return NULL;
		if (*cursor != '"')
			return NULL;
		if ((size_t)(end - cursor) > key_len + 2 && cursor[1 + key_len] == '"' &&
		    memcmp(cursor + 1, key, key_len) == 0) {
			cursor = json_skip_string(cursor, end);
			if (!cursor)
				return NULL;
			cursor = json_skip_ws(cursor, end);
			if (cursor >= end || *cursor != ':')
				return NULL;
			return json_skip_ws(cursor + 1, end);
		}
		cursor = json_skip_string(cursor, end);
		if (!cursor)
			return NULL;
		cursor = json_skip_ws(cursor, end);
		if (cursor >= end || *cursor != ':')
			return NULL;
		cursor = json_skip_value(cursor + 1, end);
		if (!cursor)
			return NULL;
		cursor = json_skip_ws(cursor, end);
		if (cursor < end && *cursor == ',')
			cursor++;
	}
	return NULL;
}

static int replace_range(char **buffer, size_t *length, char *start, char *stop, const char *replacement)
{
	size_t prefix = (size_t)(start - *buffer);
	size_t suffix = *length - (size_t)(stop - *buffer);
	size_t replace_len = strlen(replacement);
	size_t next = prefix + replace_len + suffix;
	char *updated = malloc(next + 1);

	if (!updated)
		return -1;
	memcpy(updated, *buffer, prefix);
	memcpy(updated + prefix, replacement, replace_len);
	memcpy(updated + prefix + replace_len, stop, suffix);
	updated[next] = 0;
	free(*buffer);
	*buffer = updated;
	*length = next;
	return 0;
}

static int set_json_string(char **buffer, size_t *length, size_t object_offset, const char *key, const char *value)
{
	char *object = *buffer + object_offset;
	char *end = *buffer + *length;
	char *field = json_find_key(object, end, key);
	char *stop;
	char quoted[8];

	if (!field || *field != '"')
		return 0;
	stop = json_skip_string(field, end);
	if (!stop)
		return -1;
	if (snprintf(quoted, sizeof(quoted), "\"%s\"", value) >= (int)sizeof(quoted))
		return -1;
	return replace_range(buffer, length, field, stop, quoted);
}

static int set_json_array_empty(char **buffer, size_t *length, size_t object_offset, const char *key)
{
	char *object = *buffer + object_offset;
	char *end = *buffer + *length;
	char *field = json_find_key(object, end, key);
	char *stop;

	if (!field)
		return 0;
	stop = json_skip_value(field, end);
	if (!stop)
		return -1;
	return replace_range(buffer, length, field, stop, "[]");
}

int recovery_reset_user_json(char **buffer, size_t *length)
{
	char *end;
	char *auth;
	size_t auth_offset;

	if (!buffer || !*buffer)
		return -1;
	end = *buffer + *length;
	auth = json_find_key(*buffer, end, "auth");
	if (!auth || *auth != '{')
		return -1;
	auth_offset = (size_t)(auth - *buffer);
	if (set_json_array_empty(buffer, length, auth_offset, "users") < 0)
		return -1;
	if (set_json_string(buffer, length, auth_offset, "username", "") < 0)
		return -1;
	if (set_json_string(buffer, length, auth_offset, "web_password_hash", "") < 0)
		return -1;
	return 0;
}

static ssize_t conn_read(struct conn *conn, void *data, size_t length)
{
#ifdef HAVE_OPENSSL
	if (conn->ssl)
		return SSL_read(conn->ssl, data, (int)length);
#endif
	return read(conn->fd, data, length);
}

static int conn_write_all(struct conn *conn, const void *data, size_t length)
{
	const char *cursor = data;

	while (length) {
#ifdef HAVE_OPENSSL
		ssize_t written = conn->ssl ? SSL_write(conn->ssl, cursor, (int)length) : write(conn->fd, cursor, length);
#else
		ssize_t written = write(conn->fd, cursor, length);
#endif
		if (written < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (written == 0)
			return -1;
		cursor += written;
		length -= (size_t)written;
	}
	return 0;
}

static int write_all_fd(int fd, const void *data, size_t length)
{
	const char *cursor = data;

	while (length) {
		ssize_t written = write(fd, cursor, length);
		if (written < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		cursor += (size_t)written;
		length -= (size_t)written;
	}
	return 0;
}

static int reset_user_file(const char *path)
{
	struct stat info;
	char *buffer;
	size_t length;
	int fd;
	int result = -1;

	fd = open(path, O_RDONLY);
	if (fd < 0)
		return errno == ENOENT ? 0 : -1;
	if (fstat(fd, &info) < 0 || info.st_size <= 0 || info.st_size > 1 << 20) {
		close(fd);
		return -1;
	}
	length = (size_t)info.st_size;
	buffer = malloc(length + 1);
	if (!buffer) {
		close(fd);
		return -1;
	}
	if (read(fd, buffer, length) != (ssize_t)length) {
		free(buffer);
		close(fd);
		return -1;
	}
	close(fd);
	buffer[length] = 0;
	if (recovery_reset_user_json(&buffer, &length) < 0) {
		free(buffer);
		return -1;
	}
	fd = open(path, O_WRONLY | O_TRUNC);
	if (fd < 0) {
		free(buffer);
		return -1;
	}
	if (write_all_fd(fd, buffer, length) == 0 && fsync(fd) == 0)
		result = 0;
	close(fd);
	free(buffer);
	return result;
}

static int read_line(struct conn *conn, char *line, size_t size)
{
	size_t used = 0;

	while (used + 1 < size) {
		char byte;
		ssize_t got = conn_read(conn, &byte, 1);

		if (got == 0)
			break;
		if (got < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		line[used++] = byte;
		if (used >= 2 && line[used - 2] == '\r' && line[used - 1] == '\n') {
			line[used] = 0;
			return (int)used;
		}
		if (used > HEADER_LIMIT)
			return -1;
	}
	return -1;
}

static void send_response(struct conn *conn, int status, const char *reason, const char *type, const void *body, size_t length)
{
	char header[320];
	int header_len = snprintf(header, sizeof(header),
		"HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n",
		status, reason, type, length);

	if (header_len > 0)
		conn_write_all(conn, header, (size_t)header_len);
	if (length)
		conn_write_all(conn, body, length);
}

static void send_text(struct conn *conn, int status, const char *reason, const char *body)
{
	send_response(conn, status, reason, "text/plain; charset=utf-8", body, strlen(body));
}

static void write_progress(const char *json)
{
	char tmp[256];
	int fd;
	size_t length;

	if (!json || snprintf(tmp, sizeof(tmp), "%s.tmp", progress_path) >= (int)sizeof(tmp))
		return;
	length = strlen(json);
	fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		return;
	if (write_all_fd(fd, json, length) == 0)
		fsync(fd);
	close(fd);
	if (rename(tmp, progress_path) < 0)
		unlink(tmp);
}

static void write_progress_upload(long received, long total)
{
	char json[128];

	if (snprintf(json, sizeof(json),
		     "{\"phase\":\"upload\",\"received\":%ld,\"total\":%ld}\n",
		     received, total) >= (int)sizeof(json))
		return;
	write_progress(json);
}

static void write_progress_error(const char *message)
{
	char json[192];

	if (!message)
		message = "firmware failed";
	if (snprintf(json, sizeof(json), "{\"phase\":\"error\",\"message\":\"%s\"}\n", message) >= (int)sizeof(json))
		return;
	write_progress(json);
}

static int spawn_command(char *const argv[])
{
	pid_t child = fork();
	int status;

	if (child == 0) {
		execv(argv[0], argv);
		_exit(127);
	}
	if (child < 0)
		return -1;
	if (waitpid(child, &status, 0) < 0)
		return -1;
	return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : -1;
}

static int prepare_storage(void)
{
	char *argv[] = { PREPARE_DATA, NULL };

	return spawn_command(argv);
}

static int path_is_safe(const char *path)
{
	size_t i;

	if (!path || path[0] != '/')
		return 0;
	for (i = 0; path[i]; i++) {
		unsigned char c = (unsigned char)path[i];
		if (c < 0x20 || c == '\\' || c == '%')
			return 0;
		if (c == '.' && path[i + 1] == '.')
			return 0;
	}
	return 1;
}

static const char *mime_type(const char *path)
{
	const char *dot = strrchr(path, '.');

	if (!dot)
		return "application/octet-stream";
	if (!strcmp(dot, ".html"))
		return "text/html; charset=utf-8";
	if (!strcmp(dot, ".js"))
		return "text/javascript; charset=utf-8";
	if (!strcmp(dot, ".css"))
		return "text/css; charset=utf-8";
	if (!strcmp(dot, ".svg"))
		return "image/svg+xml";
	if (!strcmp(dot, ".png"))
		return "image/png";
	if (!strcmp(dot, ".ico"))
		return "image/x-icon";
	if (!strcmp(dot, ".json"))
		return "application/json; charset=utf-8";
	if (!strcmp(dot, ".woff2"))
		return "font/woff2";
	return "application/octet-stream";
}

static void handle_get(struct conn *conn, const char *request_path)
{
	char file_path[512];
	char *page = NULL;
	size_t length = 0;
	const char *rel = request_path;
	FILE *file;
	long size;

	if (!strcmp(request_path, "/status")) {
		char json[512];

		if (recovery_status_json(json, sizeof(json)) < 0)
			send_text(conn, 500, "Internal Server Error", "cannot list addresses\n");
		else
			send_response(conn, 200, "OK", "application/json; charset=utf-8",
				      json, strlen(json));
		return;
	}
	if (!strcmp(request_path, "/progress")) {
		char json[256];
		FILE *progress;
		size_t n = 0;

		progress = fopen(progress_path, "r");
		if (progress) {
			n = fread(json, 1, sizeof(json) - 1, progress);
			fclose(progress);
		}
		if (n == 0) {
			const char idle[] = "{\"phase\":\"idle\"}\n";

			n = sizeof(idle) - 1;
			memcpy(json, idle, n);
		}
		json[n] = 0;
		send_response(conn, 200, "OK", "application/json; charset=utf-8", json, n);
		return;
	}
	if (!strcmp(rel, "/") || !strcmp(rel, "/index.html"))
		rel = "/index.html";
	if (!path_is_safe(rel)) {
		send_text(conn, 400, "Bad Request", "bad path\n");
		return;
	}
	if (snprintf(file_path, sizeof(file_path), "%s%s", www_root, rel) >= (int)sizeof(file_path)) {
		send_text(conn, 400, "Bad Request", "path too long\n");
		return;
	}
	file = fopen(file_path, "rb");
	if (!file) {
		if (!strcmp(rel, "/index.html"))
			send_text(conn, 500, "Internal Server Error", "recovery page missing\n");
		else
			send_text(conn, 404, "Not Found", "not found\n");
		return;
	}
	{
		struct stat info;

		if (fstat(fileno(file), &info) < 0 || !S_ISREG(info.st_mode)) {
			fclose(file);
			send_text(conn, 404, "Not Found", "not found\n");
			return;
		}
	}
	if (fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) < 0 || size > MAX_STATIC ||
	    fseek(file, 0, SEEK_SET) != 0) {
		fclose(file);
		send_text(conn, 500, "Internal Server Error", "recovery page unreadable\n");
		return;
	}
	length = (size_t)size;
	page = malloc(length);
	if (!page || fread(page, 1, length, file) != length) {
		free(page);
		fclose(file);
		send_text(conn, 500, "Internal Server Error", "recovery page unreadable\n");
		return;
	}
	fclose(file);
	send_response(conn, 200, "OK", mime_type(rel), page, length);
	free(page);
}

static void drain_body(struct conn *conn, long remaining)
{
	char buffer[4096];

	while (remaining > 0) {
		ssize_t got = conn_read(conn, buffer, remaining > (long)sizeof(buffer) ? sizeof(buffer) : (size_t)remaining);
		if (got <= 0)
			return;
		remaining -= got;
	}
}

static void handle_firmware(struct conn *conn, long length)
{
	int out;
	long remaining = length;
	long limit;
	char buffer[16384];
	char *install_argv[] = { INSTALL_FWUP, (char *)firmware_path, NULL };

	limit = recovery_firmware_limit();
	if (length <= 0 || limit <= 0 || length > limit) {
		drain_body(conn, length > 0 ? length : 0);
		write_progress_error("firmware is missing or too large for tmpfs");
		send_text(conn, 400, "Bad Request", "firmware is missing or too large for tmpfs\n");
		return;
	}
	write_progress_upload(0, length);
	if (mkdir("/run/onekvm-firmware", 0750) < 0 && errno != EEXIST) {
		drain_body(conn, remaining);
		write_progress_error("cannot create firmware tmpfs");
		send_text(conn, 500, "Internal Server Error", "cannot create firmware tmpfs\n");
		return;
	}
	out = open(firmware_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (out < 0) {
		drain_body(conn, remaining);
		write_progress_error("cannot create firmware file");
		send_text(conn, 500, "Internal Server Error", "cannot create firmware file\n");
		return;
	}
	{
		long received = 0;
		long last_report = 0;

		while (remaining > 0) {
			ssize_t got = conn_read(conn, buffer, remaining > (long)sizeof(buffer) ? sizeof(buffer) : (size_t)remaining);
			if (got <= 0) {
				close(out);
				unlink(firmware_path);
				write_progress_error("firmware upload truncated");
				send_text(conn, 400, "Bad Request", "firmware upload truncated\n");
				return;
			}
			if (write_all_fd(out, buffer, (size_t)got) < 0) {
				int err = errno;

				close(out);
				unlink(firmware_path);
				if (err == ENOSPC) {
					write_progress_error("firmware is missing or too large for tmpfs");
					send_text(conn, 400, "Bad Request", "firmware is missing or too large for tmpfs\n");
				} else {
					write_progress_error("cannot write firmware");
					send_text(conn, 500, "Internal Server Error", "cannot write firmware\n");
				}
				return;
			}
			remaining -= got;
			received += got;
			if (received - last_report >= PROGRESS_STEP || remaining == 0) {
				write_progress_upload(received, length);
				last_report = received;
			}
		}
	}
	fsync(out);
	close(out);
	write_progress("{\"phase\":\"extract\"}\n");
	if (spawn_command(install_argv) < 0) {
		unlink(firmware_path);
		write_progress_error("firmware install failed");
		send_text(conn, 500, "Internal Server Error", "firmware install failed\n");
		return;
	}
	unlink(firmware_path);
	write_progress("{\"phase\":\"done\"}\n");
	send_text(conn, 200, "OK", "firmware installed\n");
}

static void handle_request(struct conn *conn)
{
	char line[HEADER_LIMIT];
	char method[16] = { 0 };
	char path[128] = { 0 };
	char *query;
	long content_length = 0;

	if (read_line(conn, line, sizeof(line)) < 0 || sscanf(line, "%15s %127s", method, path) != 2) {
		send_text(conn, 400, "Bad Request", "bad request\n");
		return;
	}
	query = strchr(path, '?');
	if (query)
		*query = 0;
	for (;;) {
		if (read_line(conn, line, sizeof(line)) < 0) {
			send_text(conn, 400, "Bad Request", "bad headers\n");
			return;
		}
		if (strcmp(line, "\r\n") == 0)
			break;
		if (strncasecmp(line, "Content-Length:", 15) == 0)
			content_length = strtol(line + 15, NULL, 10);
	}
	if (strcmp(method, "GET") == 0) {
		handle_get(conn, path);
		return;
	}
	if (strcmp(method, "POST") != 0) {
		send_text(conn, 405, "Method Not Allowed", "method not allowed\n");
		return;
	}
	if (strcmp(path, "/reboot") == 0) {
		send_text(conn, 200, "OK", "rebooting\n");
		if (spawn_command((char *const[]){ "/sbin/reboot", "-f", NULL }) < 0)
			spawn_command((char *const[]){ "/bin/busybox", "reboot", "-f", NULL });
		return;
	}
	if (strcmp(path, "/reset-user") == 0) {
		if (prepare_storage() < 0 || reset_user_file(config_path) < 0)
			send_text(conn, 500, "Internal Server Error", "cannot reset user\n");
		else
			send_text(conn, 200, "OK", "default user reset\n");
		return;
	}
	if (strcmp(path, "/firmware") == 0) {
		handle_firmware(conn, content_length);
		return;
	}
	drain_body(conn, content_length);
	send_text(conn, 404, "Not Found", "not found\n");
}

static int listen_port(int port)
{
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	int enable = 1;
	struct sockaddr_in addr = {
		.sin_family = AF_INET,
		.sin_port = htons((uint16_t)port),
		.sin_addr.s_addr = htonl(INADDR_ANY),
	};

	if (fd < 0)
		return -1;
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
	if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 || listen(fd, 8) < 0) {
		close(fd);
		return -1;
	}
	return fd;
}

static void handle_client(int client, int tls)
{
	struct conn conn = { .fd = client };

#ifdef HAVE_OPENSSL
	SSL_CTX *ctx = NULL;
	if (tls) {
		SSL_load_error_strings();
		OpenSSL_add_ssl_algorithms();
		ctx = SSL_CTX_new(TLS_server_method());
		if (!ctx || SSL_CTX_use_certificate_file(ctx, cert_path, SSL_FILETYPE_PEM) != 1 ||
		    SSL_CTX_use_PrivateKey_file(ctx, key_path, SSL_FILETYPE_PEM) != 1) {
			if (ctx)
				SSL_CTX_free(ctx);
			close(client);
			return;
		}
		conn.ssl = SSL_new(ctx);
		if (!conn.ssl || SSL_set_fd(conn.ssl, client) != 1 || SSL_accept(conn.ssl) != 1) {
			if (conn.ssl)
				SSL_free(conn.ssl);
			SSL_CTX_free(ctx);
			close(client);
			return;
		}
	}
#else
	(void)tls;
#endif
	handle_request(&conn);
#ifdef HAVE_OPENSSL
	if (conn.ssl) {
		SSL_shutdown(conn.ssl);
		SSL_free(conn.ssl);
		SSL_CTX_free(ctx);
	}
#endif
	close(client);
}

static int run_server(void)
{
	int http_fd = listen_port(HTTP_PORT);
	int https_fd = listen_port(HTTPS_PORT);

	if (http_fd < 0) {
		perror("listen 80");
		return 1;
	}
	if (https_fd < 0)
		fprintf(stderr, "onekvm-recovery-web: port 443 unavailable\n");
	signal(SIGCHLD, SIG_IGN);
	for (;;) {
		fd_set ready;
		int max_fd = http_fd;

		FD_ZERO(&ready);
		FD_SET(http_fd, &ready);
		if (https_fd >= 0) {
			FD_SET(https_fd, &ready);
			if (https_fd > max_fd)
				max_fd = https_fd;
		}
		if (select(max_fd + 1, &ready, NULL, NULL, NULL) < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		if (FD_ISSET(http_fd, &ready)) {
			int client = accept(http_fd, NULL, NULL);
			if (client >= 0 && fork() == 0) {
				close(http_fd);
				if (https_fd >= 0)
					close(https_fd);
				handle_client(client, 0);
				_exit(0);
			}
			if (client >= 0)
				close(client);
		}
		if (https_fd >= 0 && FD_ISSET(https_fd, &ready)) {
			int client = accept(https_fd, NULL, NULL);
			if (client >= 0 && fork() == 0) {
				close(http_fd);
				close(https_fd);
				handle_client(client, 1);
				_exit(0);
			}
			if (client >= 0)
				close(client);
		}
	}
	return 1;
}

int main(int argc, char **argv)
{
	int i;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--reset-user")) {
			if (i + 1 >= argc) {
				fprintf(stderr, "usage: onekvm-recovery-web [--www DIR] [--reset-user FILE]\n");
				return 2;
			}
			return reset_user_file(argv[++i]) == 0 ? 0 : 1;
		}
		if (!strcmp(argv[i], "--www")) {
			if (i + 1 >= argc) {
				fprintf(stderr, "usage: onekvm-recovery-web [--www DIR] [--reset-user FILE]\n");
				return 2;
			}
			www_root = argv[++i];
			continue;
		}
		if (!strcmp(argv[i], "--help")) {
			fprintf(stderr, "usage: onekvm-recovery-web [--www DIR] [--reset-user FILE]\n");
			return 2;
		}
		fprintf(stderr, "unknown argument: %s\n", argv[i]);
		return 2;
	}
	return run_server();
}
