/*
#	IPv4 Tunneling Over IPv6
#
#	Author: KIMOTO Mizuki "teddy@sfc.wide.ad.jp"
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <net/if.h>
#include <linux/if_tun.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/time.h>
#include <errno.h>
#include <stdarg.h>
#include <pthread.h>

#define BUFSIZE 2000
#define PORT 55550

int debug;
char *progname;

struct hoge6tun {
	int tap_fd;
	int remote_net_fd;
	int local_net_fd;
	struct sockaddr_in6 remote;
	struct sockaddr_in6 local;
};

int tun_alloc(char *dev, int flags)
{

	struct ifreq ifr;
	int fd, err;
	char *clonedev = "/dev/net/tun";

	if((fd = open(clonedev, O_RDWR)) < 0) {
		perror("open");
		return fd;
	}

	memset(&ifr, 0, sizeof(ifr));

	ifr.ifr_flags = flags;

	if(*dev) {
		strncpy(ifr.ifr_name, dev, IFNAMSIZ);
	}

	if((err = ioctl(fd, TUNSETIFF, (void *)&ifr)) < 0) {
		perror("ioctl");
		close (fd);
		return err;
	}

	strcpy(dev, ifr.ifr_name);

	return fd;
}

int cread(int fd, char *buf, int n)
{

	int nread;

	if((nread = read(fd, buf, n)) < 0){
		perror("Reading data");
		exit(1);
	}

	return nread;
}

int cwrite(int fd, char *buf, int n)
{

	int nwrite;

	if((nwrite = write(fd, buf, n)) < 0) {
		perror("Writing data");
		exit(1);
	}

	return nwrite;
}

void do_debug(char *msg, ...)
{

	va_list argp;

	if(debug) {
		va_start(argp, msg);
		vfprintf(stderr, msg, argp);
		va_end(argp);
	}
}

void my_err(char *msg, ...)
{

	va_list argp;

	va_start(argp, msg);
	vfprintf(stderr, msg, argp);
	va_end(argp);

}

void* mon_tap2net (void* p)
{

	uint16_t nread, nwrite;
	char buffer[BUFSIZE];
	unsigned long int tap2net = 0;
	struct hoge6tun *h6t = p;

	while(1) {

		/* data from tun/tap: just read it and write it to the network */
		nread = cread(h6t->tap_fd, buffer, BUFSIZE);

		tap2net++;
		do_debug("TAP2NET %lu: Read %d bytes from the tap interface\n", tap2net, nread);

		/* write length + packet */
		nwrite = sendto(h6t->remote_net_fd, buffer, nread, 0,
				(struct sockaddr *)&h6t->remote, sizeof(h6t->remote));

		do_debug("TAP2NET %lu: Written %d bytes to the network\n", tap2net, nwrite);
	}
}

void* mon_net2tap (void* p)
{

	uint16_t nread, nwrite;
	char buffer[BUFSIZE];
	unsigned long int net2tap = 0;
	struct hoge6tun *h6t = p;
	socklen_t sin_size;

	while (1) {
		/* data from the network: read it, and write it to the tun/tap interface.
       	 	* We need to read the length first, and then the packet */

		/* Read length */
		nread = recvfrom(h6t->local_net_fd, buffer, sizeof(buffer), 0,
						(struct sockaddr*)&h6t->local, &sin_size);

		if(nread == 0) {
			/* ctrl-c at the other end */
			break;
		}

		net2tap++;
		/* read packet */
		do_debug("NET2TAP %lu: Read %d bytes from the network\n", net2tap, nread);

		/* now buffer[] contains a full packet or frame, write it into the tun/tap interface */
		nwrite = cwrite(h6t->tap_fd, buffer, nread);
		do_debug("NET2TAP %lu: Written %d bytes to the tap interface\n", net2tap, nwrite);
	}

	exit(1);
}

void usage(void) {
  fprintf(stderr, "Usage:\n");
  fprintf(stderr, "%s -i <ifacename> [-r <remoteIP>] [-u|-a] [-d]\n", progname);
  fprintf(stderr, "%s -h\n", progname);
  fprintf(stderr, "\n");
  fprintf(stderr, "-i <ifacename>: Name of interface to use (mandatory)\n");
  fprintf(stderr, "-r <remoteIP>: Specify remote address (mandatory)\n");
  fprintf(stderr, "-u|-a: use TUN (-u, default) or TAP (-a)\n");
  fprintf(stderr, "-d: outputs debug information while running\n");
  fprintf(stderr, "-h: prints this help text\n");
  exit(1);
}


int main(int argc, char *argv[])
{
	struct hoge6tun h6t;
	int option;
	int flags = IFF_TUN;
	char if_name[IFNAMSIZ] = "";
	char remote_ip[40] = "";            /* dotted quad IP string */
	unsigned short int port = PORT;
	int sock_fd;
	pthread_t th1, th2;

	progname = argv[0];

	while((option = getopt(argc, argv, "i:r:uahd")) > 0) {
		switch(option) {
			case 'd':
				debug = 1;
				break;
			case 'h':
				usage();
				break;
			case 'i':
				strncpy(if_name, optarg, IFNAMSIZ - 1);
				break;
			case 'r':
				strncpy(remote_ip, optarg, 40);
				break;
			case 'u':
				flags = IFF_TUN;
				break;
			case 'a':
				flags = IFF_TAP;
				break;
			default:
				my_err("Unknown option %c\n", option);
				printf("\n");
				usage();
		}
	}

	argv += optind;
	argc -= optind;

	if(argc > 0) {
		my_err("Too many options!\n");
		printf("\n");
		usage();
	}

	if(*if_name == '\0' || *remote_ip == '\0') {
		my_err("Must specify interface name and remote address\n");
		printf("\n");
		usage();
	}

	memset(&h6t, 0, sizeof(h6t));

	if((daemon(1,1)) != 0){
		perror("daaemon: ");
		exit(1);
	}

	/* initialize tun/tap interface */
	if ((h6t.tap_fd = tun_alloc(if_name, flags | IFF_NO_PI)) < 0 ) {
		my_err("Error connecting to tun/tap interface %s!\n", if_name);
		exit(1);
	}

	do_debug("Successfully connected to interface %s\n", if_name);

	/* for remote */
	if ((sock_fd = socket(AF_INET6, SOCK_DGRAM, 0)) < 0) {
		perror("socket()");
		exit(1);
	}

	h6t.remote.sin6_family = AF_INET6;
	inet_pton(AF_INET6, remote_ip, &h6t.remote.sin6_addr);
	h6t.remote.sin6_port = htons(port);

	h6t.remote_net_fd = sock_fd;
	inet_ntop(AF_INET6, &h6t.remote.sin6_addr, remote_ip, sizeof(remote_ip));
	do_debug("Connected to remote %s\n", remote_ip);

	/* for local */
	if ((sock_fd = socket(AF_INET6, SOCK_DGRAM, 0)) < 0) {
		perror("socket()");
		exit(1);
	}

	h6t.local.sin6_family = AF_INET6;
	h6t.local.sin6_addr = in6addr_any;
	h6t.local.sin6_port = htons(port);
	if (bind(sock_fd, (struct sockaddr*) &h6t.local, sizeof(h6t.local)) < 0){
		perror("bind()");
		exit(1);
	}

	h6t.local_net_fd = sock_fd;
	inet_ntop(AF_INET6, &h6t.remote.sin6_addr, remote_ip, sizeof(remote_ip));

	/* fall into pthread */
	pthread_create(&th1, NULL, mon_tap2net, (void*)&h6t);
	pthread_create(&th2, NULL, mon_net2tap, (void*)&h6t);

	pthread_join(th1, NULL);
	pthread_join(th2, NULL);

	return 0;
}
