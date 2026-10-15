/* C-ABI lifecycle probe (ctest capi.shutdown_cycle). Plain C on purpose: it links only against the public C ABI,
 * exactly like the Android JNI shell. R30 found that ~Runtime destroyed members in an order that crashed only with
 * libc++ (SIGSEGV in ActionRunner::~ActionRunner); this loop (init, subscribe, shutdown, init again) pins it.
 * usage: shutdown_cabi_probe <fresh-data-dir>. Exit 0 only when every cycle completed. */
#include <stdio.h>
#include "loom/loom.h"

static void ev(const char *a, const char *b, void *u) { (void)a; (void)b; (void)u; }

int main(int argc, char **argv) {
  if (argc < 2) { fputs("usage: shutdown_cabi_probe <data-dir>\n", stderr); return 2; }
  char opts[1024];
  snprintf(opts, sizeof opts, "{\"data_dir\":\"%s\"}", argv[1]);
  loom_set_log_stderr(0);
  for (int i = 0; i < 3; i++) {
    const char *err = 0;
    LoomContext *c = loom_init_ex(opts, &err);
    if (!c) { fprintf(stderr, "cycle %d: init failed\n", i); return 3; }
    loom_subscribe(c, "*", ev, 0);
    loom_shutdown(c);
    printf("shutdown ok %d\n", i);
  }
  return 0;
}
