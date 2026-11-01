/* External-share test fixture: nominal HTTPS ChatGPT host is mapped to own TLS
 * loopback fixture; synthetic CA still required by unmodified PublicTransport.
 * Extra loopback port is only the recording synthetic model, never a real API.
 * Compile ONLY in the later reserved acceptance phase. */
/* Trusted test-only DNS/dial fixture, injected into this test's own process.
 * No product/config/request can enable it. Unknown destinations are refused.
 * The nominal public address is never contacted: connect maps it to loopback.
 */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <dlfcn.h>
#include <errno.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

int getaddrinfo(const char *node, const char *service, const struct addrinfo *hints,
                struct addrinfo **result) {
  typedef int (*real_fn)(const char *, const char *, const struct addrinfo *, struct addrinfo **);
  real_fn real = (real_fn)dlsym(RTLD_NEXT, "getaddrinfo");
  if (!real) return EAI_FAIL;
  if (node && (!strcmp(node, "chatgpt.com") || !strcmp(node, "chat.openai.com"))) {
    struct addrinfo numeric = {0};
    if (hints) numeric = *hints;
    numeric.ai_family = AF_INET; numeric.ai_flags |= AI_NUMERICHOST;
    return real("8.8.8.8", service, &numeric, result);
  }
  /* Own server bind/loopback control is allowed; never resolve other names. */
  if (!node || !strcmp(node, "127.0.0.1")) return real(node, service, hints, result);
  return EAI_NONAME;
}

int connect(int fd, const struct sockaddr *address, socklen_t length) {
  typedef int (*real_fn)(int, const struct sockaddr *, socklen_t);
  real_fn real = (real_fn)dlsym(RTLD_NEXT, "connect");
  if (!real) { errno = EACCES; return -1; }
  if (address && address->sa_family == AF_INET && length >= sizeof(struct sockaddr_in)) {
    const struct sockaddr_in *input = (const struct sockaddr_in *)address;
    const char *port = getenv("LOOM_TEST_URL_SOURCE_PORT");
    const char *model_port = getenv("LOOM_TEST_SHARE_MODEL_PORT");
    char *end = NULL;
    long n = port ? strtol(port, &end, 10) : 0;
    if (n < 1 || n > 65535 || !end || *end) { errno = EACCES; return -1; }
    if (input->sin_addr.s_addr == inet_addr("8.8.8.8") && ntohs(input->sin_port) == 443) {
      struct sockaddr_in target = *input;
      target.sin_addr.s_addr = htonl(INADDR_LOOPBACK); target.sin_port = htons((unsigned short)n);
      return real(fd, (const struct sockaddr *)&target, sizeof(target));
    }
    char *model_end = NULL;
    long model = model_port ? strtol(model_port, &model_end, 10) : 0;
    if (input->sin_addr.s_addr == htonl(INADDR_LOOPBACK) &&
        (ntohs(input->sin_port) == n ||
         (model >= 1 && model <= 65535 && model_end && !*model_end && ntohs(input->sin_port) == model)))
      return real(fd, address, length);
  }
  errno = EACCES; return -1;
}
