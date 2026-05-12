#include "../include/signalhandling.h"
#include <stddef.h>
#include <sys/signal.h>

// Global flag checked by the main loop to know when to shut down.
// sig_atomic_t because it gets modified inside a signal handler,
// so we need it to be safe from partial writes.
volatile sig_atomic_t keep_running = 1;

// Simple handler: just flip the flag. We avoid doing anything
// complex here because signal handlers have very strict rules
// about what functions you can safely call.
void handle_signal(int sig) {
  (void)sig;
  keep_running = 0;
  return;
}

// Register our handler for SIGINT (Ctrl+C) and SIGTERM (kill).
// Using sigaction instead of signal() for more predictable behavior.
void setup_signal_handlers(void) {
  struct sigaction sa;
  sa.sa_handler = handle_signal;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;

  sigaction(SIGINT, &sa, NULL);
  sigaction(SIGTERM, &sa, NULL);
}