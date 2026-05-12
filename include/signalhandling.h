#ifndef SIGNALHANDLING_H
#define SIGNALHANDLING_H
#include <signal.h>

extern volatile sig_atomic_t keep_running;

void setup_signal_handlers(void);

#endif