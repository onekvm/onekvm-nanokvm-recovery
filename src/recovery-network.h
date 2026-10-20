#ifndef RECOVERY_NETWORK_H
#define RECOVERY_NETWORK_H
#include <stddef.h>
#define RECOVERY_NETWORK_STATE "/run/onekvm-recovery-network.json"
#define RECOVERY_NETWORK_PENDING "/run/onekvm-recovery-network.pending"
#define RECOVERY_NETWORK_LOCK "/run/onekvm-recovery-network.lock"
int recovery_network_request(const char *body, size_t size, const char *path);
int recovery_network_status(char *out, size_t size);
#endif
