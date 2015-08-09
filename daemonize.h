#ifndef daemonize_h_inc
#define daemonize_h_inc

#include <unistd.h>

extern pid_t parent_pid;
void daemonize(void);

#endif