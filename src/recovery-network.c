/* Recovery IPv4 snapshot parser. Input is data, never shell source. */
#include "recovery-network.h"
#include "recovery-sha256.h"
#include <arpa/inet.h>
#include <ctype.h>
#include <fcntl.h>
#include <json-c/json.h>
#include <net/if.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define LIMIT 4096
#define FLAGS (JSON_C_TO_STRING_PLAIN | JSON_C_TO_STRING_NOSLASHESCAPE)
static const char *fields[] = {"source",
			       "interface",
			       "mode",
			       "address",
			       "gateway",
			       "fallback_enabled",
			       "fallback_address",
			       "mac_address",
			       "use_custom_dns",
			       "dns",
			       "dhcp_client_id"};
static int string_field(struct json_object *o, const char *key,
			const char **out)
{
	struct json_object *v;
	if (!json_object_object_get_ex(o, key, &v) ||
	    !json_object_is_type(v, json_type_string))
		return -1;
	*out = json_object_get_string(v);
	return strlen(*out) == (size_t)json_object_get_string_len(v) ? 0 : -1;
}
static int bool_field(struct json_object *o, const char *key, int *out)
{
	struct json_object *v;
	if (!json_object_object_get_ex(o, key, &v) ||
	    !json_object_is_type(v, json_type_boolean))
		return -1;
	*out = json_object_get_boolean(v);
	return 0;
}
static int valid_ip(const char *s, int dns)
{
	struct in_addr v4;
	struct in6_addr v6;
	if (inet_pton(AF_INET, s, &v4) == 1) {
		unsigned n = ntohl(v4.s_addr);
		return n && n != 0xffffffff && (n >> 24) != 127 &&
		       (n >> 28) != 14;
	}
	return dns && inet_pton(AF_INET6, s, &v6) == 1 &&
	       !IN6_IS_ADDR_UNSPECIFIED(&v6) && !IN6_IS_ADDR_LOOPBACK(&v6) &&
	       !IN6_IS_ADDR_MULTICAST(&v6);
}
static int valid_cidr(const char *s)
{
	char buf[64], *slash, *end;
	long prefix;
	if (strlen(s) >= sizeof(buf))
		return 0;
	strcpy(buf, s);
	slash = strchr(buf, '/');
	if (!slash)
		return 0;
	*slash++ = 0;
	if (!*slash)
		return 0;
	prefix = strtol(slash, &end, 10);
	return !*end && prefix > 0 && prefix <= 32 && valid_ip(buf, 0);
}
static int valid_mac(const char *s)
{
	unsigned bytes[6];
	int used = 0, nonzero = 0;
	if (strlen(s) != 17)
		return 0;
	for (int i = 0; i < 17; i++)
		if (i % 3 == 2 ? s[i] != ':' : !isxdigit((unsigned char)s[i]))
			return 0;
	if (sscanf(s, "%2x:%2x:%2x:%2x:%2x:%2x%n", bytes, bytes + 1, bytes + 2,
		   bytes + 3, bytes + 4, bytes + 5, &used) != 6 ||
	    used != 17)
		return 0;
	for (int i = 0; i < 6; i++)
		nonzero |= bytes[i];
	return !(bytes[0] & 1) && nonzero;
}
static int valid_name(const char *s)
{
	size_t n = strlen(s);
	if (!n || n >= IFNAMSIZ || !strcmp(s, "lo"))
		return 0;
	for (size_t i = 0; i < n; i++)
		if (!isalnum((unsigned char)s[i]) && s[i] != '_' && s[i] != '-')
			return 0;
	return 1;
}
static struct json_object *parse(const char *text, size_t length)
{
	struct json_tokener *tok;
	struct json_object *o;
	size_t end;
	if (!length || length > LIMIT)
		return NULL;
	tok = json_tokener_new_ex(8);
	if (!tok)
		return NULL;
	json_tokener_set_flags(tok, JSON_TOKENER_STRICT |
					JSON_TOKENER_VALIDATE_UTF8);
	o = json_tokener_parse_ex(tok, text, (int)length);
	end = json_tokener_get_parse_end(tok);
	if (json_tokener_get_error(tok) != json_tokener_success) {
		if (o)
			json_object_put(o);
		o = NULL;
	}
	while (end < length && isspace((unsigned char)text[end]))
		end++;
	if (end != length && o) {
		json_object_put(o);
		o = NULL;
	}
	json_tokener_free(tok);
	return o;
}
static struct json_object *canonical(struct json_object *o)
{
	const char *source, *iface, *mode, *address, *gateway, *fallback, *mac;
	int fall, custom;
	struct json_object *dns, *result;
	if (!o || !json_object_is_type(o, json_type_object) ||
	    json_object_object_length(o) != 11)
		return NULL;
	if (string_field(o, "source", &source) ||
	    string_field(o, "interface", &iface) ||
	    string_field(o, "mode", &mode) ||
	    string_field(o, "address", &address) ||
	    string_field(o, "gateway", &gateway) ||
	    string_field(o, "fallback_address", &fallback) ||
	    string_field(o, "mac_address", &mac) ||
	    bool_field(o, "fallback_enabled", &fall) ||
	    bool_field(o, "use_custom_dns", &custom))
		return NULL;
	if (strcmp(source, "system") && strcmp(source, "custom") &&
	    strcmp(source, "session") && strcmp(source, "fallback"))
		return NULL;
	if (!valid_name(iface) || !valid_mac(mac) ||
	    (strcmp(mode, "dhcp") && strcmp(mode, "static")))
		return NULL;
	if ((!strcmp(mode, "static") && !valid_cidr(address)) ||
	    (*gateway && !valid_ip(gateway, 0)) ||
	    (fall && !valid_cidr(fallback)))
		return NULL;
	if (strlen(address) > 18 || strlen(gateway) > 15 ||
	    strlen(fallback) > 18)
		return NULL;
	if (!json_object_object_get_ex(o, "dns", &dns) ||
	    !json_object_is_type(dns, json_type_array) ||
	    json_object_array_length(dns) > 16 ||
	    (custom && !json_object_array_length(dns)))
		return NULL;
	for (size_t i = 0; i < json_object_array_length(dns); i++) {
		struct json_object *item = json_object_array_get_idx(dns, i);
		if (!json_object_is_type(item, json_type_string) ||
		    strlen(json_object_get_string(item)) !=
			(size_t)json_object_get_string_len(item) ||
		    !valid_ip(json_object_get_string(item), 1))
			return NULL;
	}
	const char *client_id;
	if (string_field(o, "dhcp_client_id", &client_id) ||
	    strlen(client_id) < 2 || strlen(client_id) > 510 || strlen(client_id) % 2)
		return NULL;
	for (const char *p = client_id; *p; p++)
		if (!isxdigit((unsigned char)*p))
			return NULL;
	result = json_object_new_object();
	for (size_t i = 0; i < 11; i++) {
		struct json_object *v;
		json_object_object_get_ex(o, fields[i], &v);
		json_object_object_add(result, fields[i], json_object_get(v));
	}
	return result;
}
static void checksum(struct json_object *network, char hex[65])
{
	struct recovery_sha256 hash;
	unsigned char digest[32];
	const char *body = json_object_to_json_string_ext(network, FLAGS);
	recovery_sha256_init(&hash);
	recovery_sha256_update(&hash, body, strlen(body));
	recovery_sha256_final(&hash, digest);
	recovery_sha256_hex(digest, hex);
}
static struct json_object *load(const char *path)
{
	char body[LIMIT + 1], actual[65];
	const char *expected;
	struct stat st;
	struct json_object *envelope, *network, *version, *value;
	int fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
	ssize_t got;
	if (fd < 0)
		return NULL;
	if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size <= 0 ||
	    st.st_size > LIMIT) {
		close(fd);
		return NULL;
	}
	got = read(fd, body, LIMIT);
	close(fd);
	if (got != st.st_size)
		return NULL;
	body[got] = 0;
	envelope = parse(body, (size_t)got);
	if (!envelope)
		return NULL;
	network = NULL;
	if (!json_object_is_type(envelope, json_type_object) ||
	    json_object_object_length(envelope) != 3 ||
	    !json_object_object_get_ex(envelope, "version", &version) ||
	    !json_object_is_type(version, json_type_int) ||
	    json_object_get_int64(version) != 1 ||
	    string_field(envelope, "sha256", &expected) ||
	    strlen(expected) != 64 ||
	    !json_object_object_get_ex(envelope, "network", &value))
		goto done;
	network = canonical(value);
	if (!network)
		goto done;
	checksum(network, actual);
	if (strcmp(actual, expected)) {
		json_object_put(network);
		network = NULL;
	}
done:
	json_object_put(envelope);
	return network;
}
static int save(const char *path, struct json_object *network)
{
	char hex[65];
	struct json_object *envelope = json_object_new_object();
	const char *body;
	size_t length;
	int fd, result = -1;
	checksum(network, hex);
	json_object_object_add(envelope, "version", json_object_new_int(1));
	json_object_object_add(envelope, "network", json_object_get(network));
	json_object_object_add(envelope, "sha256", json_object_new_string(hex));
	body = json_object_to_json_string_ext(envelope, FLAGS);
	length = strlen(body);
	fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC,
		  0600);
	if (fd >= 0) {
		if (write(fd, body, length) == (ssize_t)length &&
		    fsync(fd) == 0)
			result = 0;
		close(fd);
	}
	json_object_put(envelope);
	return result;
}
int recovery_network_request(const char *body, size_t size, const char *path)
{
	struct json_object *input = parse(body, size),
			   *network = canonical(input);
	int result = -1;
	if (network) {
		json_object_object_add(network, "source",
				       json_object_new_string("session"));
		result = save(path, network);
		json_object_put(network);
	}
	if (input)
		json_object_put(input);
	return result;
}
int recovery_network_status(char *out, size_t size)
{
	char reason[256] = {0};
	struct json_object *network = load(RECOVERY_NETWORK_STATE),
			   *result = json_object_new_object();
	const char *body;
	FILE *f = fopen("/run/onekvm-recovery-network.reason", "r");
	int n;
	if (f) {
		fgets(reason, sizeof(reason), f);
		fclose(f);
		reason[strcspn(reason, "\r\n")] = 0;
	}
	json_object_object_add(result, "network", network);
	json_object_object_add(result, "reason",
			       json_object_new_string(reason));
	body = json_object_to_json_string_ext(result, FLAGS);
	n = snprintf(out, size, "%s\n", body);
	json_object_put(result);
	return n < 0 || (size_t)n >= size ? -1 : 0;
}
#ifdef RECOVERY_NETWORK_MAIN
int main(int argc, char **argv)
{
	struct json_object *network, *value;
	if (argc >= 4 && !strcmp(argv[1], "prepare")) {
		struct json_object *checked;
		network = load(argv[2]);
		if (!network)
			return 1;
		json_object_object_add(network, "source",
				       json_object_new_string("session"));
		if (!strcmp(argv[3], "dhcp") && argc == 4) {
			json_object_object_add(network, "mode",
					       json_object_new_string("dhcp"));
			json_object_object_add(network, "address",
					       json_object_new_string(""));
			json_object_object_add(network, "gateway",
					       json_object_new_string(""));
			json_object_object_add(network, "fallback_enabled",
					       json_object_new_boolean(1));
			json_object_object_add(
			    network, "fallback_address",
			    json_object_new_string("192.168.233.10/24"));
		} else if (!strcmp(argv[3], "static") &&
			   (argc == 5 || argc == 6)) {
			json_object_object_add(
			    network, "mode", json_object_new_string("static"));
			json_object_object_add(network, "address",
					       json_object_new_string(argv[4]));
			json_object_object_add(
			    network, "gateway",
			    json_object_new_string(argc == 6 ? argv[5] : ""));
			json_object_object_add(network, "fallback_enabled",
					       json_object_new_boolean(0));
		} else if (!strcmp(argv[3], "fallback") && argc == 5) {
			json_object_object_add(network, "fallback_enabled",
					       json_object_new_boolean(1));
			json_object_object_add(network, "fallback_address",
					       json_object_new_string(argv[4]));
		} else {
			json_object_put(network);
			return 2;
		}
		checked = canonical(network);
		json_object_put(network);
		if (!checked)
			return 1;
		int result = save(RECOVERY_NETWORK_PENDING, checked);
		json_object_put(checked);
		return result ? 1 : 0;
	}
	if (argc == 4 && !strcmp(argv[1], "defaults")) {
		char buf[1024];
		snprintf(
		    buf, sizeof(buf),
		    "{\"source\":\"fallback\",\"interface\":\"%s\",\"mode\":"
		    "\"dhcp\",\"address\":\"\",\"gateway\":\"\",\"fallback_"
		    "enabled\":true,\"fallback_address\":\"192.168.233.10/"
		    "24\",\"mac_address\":\"%s\",\"use_custom_dns\":false,"
		    "\"dns\":[]}",
		    argv[2], argv[3]);
		struct json_object *o = parse(buf, strlen(buf));
		char client_id[15] = "01";
		size_t j = 2;
		for (const char *p = argv[3]; *p && j < 14; p++)
			if (*p != ':')
				client_id[j++] = *p;
		client_id[j] = 0;
		if (o)
			json_object_object_add(
			    o, "dhcp_client_id",
			    json_object_new_string(client_id));
		network = canonical(o);
		if (o)
			json_object_put(o);
		if (!network)
			return 1;
		int result = save(RECOVERY_NETWORK_STATE, network);
		json_object_put(network);
		return result ? 1 : 0;
	}
	if (argc < 3)
		return 2;
	network = load(argv[2]);
	if (!network) {
		fprintf(stderr, "invalid Recovery network snapshot\n");
		return 1;
	}
	if (!strcmp(argv[1], "check")) {
		json_object_put(network);
		return 0;
	}
	if (!strcmp(argv[1], "inspect")) {
		puts(json_object_to_json_string_ext(network, FLAGS));
		json_object_put(network);
		return 0;
	}
	if (argc == 4 && !strcmp(argv[1], "get") &&
	    json_object_object_get_ex(network, argv[3], &value)) {
		if (json_object_is_type(value, json_type_array)) {
			for (size_t i = 0; i < json_object_array_length(value);
			     i++)
				printf(
				    "%s%s", i ? " " : "",
				    json_object_get_string(
					json_object_array_get_idx(value, i)));
			putchar('\n');
		} else if (json_object_is_type(value, json_type_boolean))
			puts(json_object_get_boolean(value) ? "true" : "false");
		else
			puts(json_object_get_string(value));
		json_object_put(network);
		return 0;
	}
	json_object_put(network);
	return 2;
}
#endif
