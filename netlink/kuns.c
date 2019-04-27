// Camp on the UDEV netlink bus and report activity.  Started from
// http://experimentswithtrusth.blogspot.com/2014/06/handling-kernel-device-uevent-in.html
// Kernel Udev Netlink Socket == kuns
// gcc -Werror -o kuns kuns.c

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <linux/netlink.h>

void die(char *s) {
	perror(s);
	exit(1);
}

int main(int argc, char *argv[]) {
	struct sockaddr_nl nls;
	struct pollfd pfd;
	char buf[4096];
	int ret;

	// Create a socket for the hotplug event netlink bus, then bind it.
	// Socket type is a noop for AF_NETLINK.

	if ((pfd.fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_KOBJECT_UEVENT)) == -1)
  		die("socket()");

	memset(&nls, 0, sizeof(nls));
	nls.nl_family = AF_NETLINK;
	nls.nl_pid = getpid();		// Non-zero and recoverable
	nls.nl_groups = -1;		// Bitmask, all set

 	if (bind(pfd.fd, (void *)&nls, sizeof(struct sockaddr_nl)))
  		die("bind()");

	pfd.events = POLLIN;
 	while ((ret = poll(&pfd, 1, -1)) != -1) {
  		int len;
		char *start, *eobuf;;

		if (!ret)	// Timeout should not occur
			die("timeout");

		// Insure there is a null-terminated buffer at the end of it all.
		memset(buf, 0, sizeof(buf));
		if ((len = recv(pfd.fd, buf, sizeof(buf) - 1, MSG_DONTWAIT)) == -1)
			die("recv");

		start = buf;
		eobuf = buf + len;
		// Mostly NUL-terminated strings.  Ignore the blob in 'libudev'
		while (start < eobuf) {
			if (!strcmp(start, "libudev") || strchr(start, '='))
				printf("%s\n", start);
			start += strlen(start) + 1;
		}
		fflush(stdout);
	}
	die("poll");
	return 0;
}
