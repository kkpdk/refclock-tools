#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>

pid_t parent_pid;

void signalhandler(int sig)
{
        switch (sig) {
            case SIGTERM:
                exit(1);
                break;
            case SIGKILL:
                exit(1);
            break;
        }
}

void daemonize(void)
{
	pid_t pid, sid;
	static struct sigaction act;

	parent_pid=getpid();
	if (getppid()==1) return; //already daemonized
	pid = fork();
	if (pid < 0) exit(EXIT_FAILURE);
	if (pid>0) exit(EXIT_SUCCESS); //we are the parent. die.
	umask(0);
	sid = setsid(); //new sessionid
	if (sid<0) exit(EXIT_FAILURE);
	//avoid locking the current path
	if ((chdir("/"))<0) exit(EXIT_FAILURE);
	freopen( "/dev/null", "r", stdin);
	freopen( "/dev/null", "w", stdout);
	freopen( "/dev/null", "w", stderr);
	//set up handlers
	sigemptyset (&act.sa_mask);
	act.sa_flags = 0;
	act.sa_handler = SIG_IGN;
	sigaction (SIGINT, &act, NULL);
	act.sa_handler = SIG_IGN;
	sigaction (SIGHUP, &act, NULL);
	act.sa_handler =  signalhandler;
	sigaction (SIGTERM, &act, 0);
	act.sa_handler =  signalhandler;
	sigaction (SIGKILL, &act, 0);
}
