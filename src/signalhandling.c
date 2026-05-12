#include "../include/signalhandling.h"
#include <stddef.h>
#include <sys/signal.h>

volatile sig_atomic_t keep_running = 1;

void handle_signal(int sig) {
  keep_running = 0;
  return;
}

void setup_signal_handlers(void) {
  struct sigaction sa;
  sa.sa_handler = handle_signal;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;

  sigaction(SIGINT, &sa, NULL);
  sigaction(SIGTERM, &sa, NULL);
}