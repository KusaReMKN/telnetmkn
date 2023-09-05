#

#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BACKLOG	5
#define MESSAGE	"hello, world\r\n"	/* default message */
#define MSGFILE	"./msgfile.txt"	/* name of message file */
#define SERVICE	"8000"	/* port to listen */

#ifndef NI_MAXHOST
#define NI_MAXHOST	1025
#endif

#ifndef NI_MAXSERV
#define NI_MAXSERV	33
#endif

static int getContent(const char *path, char **bufptr, size_t *len);
static void mknMain(int ssock, const char *mesg, size_t len);
static int serverSocket(int family, const char *serv);
static void sigHandler(int sig);
static void usage(void);

int
main(int argc, char *argv[])
{
	int c, ssock;
	struct sigaction sa;
	char *message, *msgfile, *serv;
	size_t length;

	msgfile = MSGFILE;
	serv = SERVICE;
	while ((c = getopt(argc, argv, "f:s:")) != -1)
		switch (c) {
		case 'f':
			msgfile = optarg;
			break;
		case 's':
			serv = optarg;
			break;
		case '?':
		default:
			usage();
			/* NOTREACHED */
		}
	argc -= optind;
	argv += optind;

	(void)sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;	/* for accept() */
	sa.sa_handler = sigHandler;
	if (sigaction(SIGCHLD, &sa, NULL) == -1) {
		perror("sigaction");
		exit(EXIT_FAILURE);
	}

	if (getContent(msgfile, &message, &length) == -1) {
		(void)fprintf(stderr, "\tThe default message is used.\n");
		message = MESSAGE;
		length = strlen(message);
	}

	ssock = serverSocket(AF_UNSPEC, serv);
	if (ssock == -1)
		exit(EXIT_FAILURE);
	(void)printf("Server is listening on %s\n", serv);

	while (/* CONSTCOND */ 1)
		mknMain(ssock, message, length);

	/* NOTREACHED */
	return -1;
}

/*
 * ファイル path の内容を mmap() した領域へのポインタを bufptr に格納する。
 * 領域の長さは少なくとも len だけある。
 * 失敗した場合には stderr にエラーメッセージを出力し、-1 を返す。
 */
static int
getContent(const char *path, char **bufptr, size_t *len)
{
	int fd;
	struct stat st;
	void *buf;

	fd = open(path, O_RDONLY | O_NOCTTY);
	if (fd == -1) {
		(void)fprintf(stderr, "open: %s: %s\n", path, strerror(errno));
		*bufptr = NULL;
		return -1;
	}

	if (fstat(fd, &st) == -1) {
		(void)fprintf(stderr, "stat: %s: %s\n", path, strerror(errno));
		goto quit;
	}

	/* ファイルのサイズを表現するために size_t は十分であるか確かめる */
	if (sizeof(size_t) <= sizeof(off_t)
			&& (uintmax_t)SIZE_MAX <= (uintmax_t)st.st_size) {
		(void)fprintf(stderr, "stat: %s: %s\n", path, strerror(EFBIG));
		goto quit;
	}

	buf = mmap(NULL, (size_t)st.st_size+1, PROT_READ, MAP_PRIVATE, fd, 0);
	if (buf == MAP_FAILED) {
		(void)fprintf(stderr, "mmap: %s: %s\n", path, strerror(errno));
quit:		(void)close(fd);
		*bufptr = NULL;
		return -1;
	}

	*bufptr = buf;
	*len = (size_t)st.st_size;
	return 0;
}

/*
 * 本編。接続を受け付けてメッセージを処理する。当然 fork() する（えっ）。
 * 失敗した場合には自死する。
 */
static void
mknMain(int ssock, const char *mesg, size_t len)
{
	int err, sock;
	struct sockaddr clsa;
	socklen_t clsalen;
	char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];

	clsalen = sizeof(clsa);
	sock = accept(ssock, &clsa, &clsalen);
	if (sock == -1) {
		perror("accept");
		exit(EXIT_FAILURE);
	}

	switch (fork()) {
	case -1:
		perror("fork");
		exit(EXIT_FAILURE);
		/* NOTREACHED */
	case 0:
		/* NOTHING TO DO */
		break;
	default:
		(void)close(sock);
		return ;
	}

	err = getnameinfo(&clsa, clsalen, hbuf, sizeof(hbuf),
			sbuf, sizeof(sbuf), NI_NUMERICHOST | NI_NUMERICSERV);
	if (err != 0)
		(void)fprintf(stderr, "getnameinfo: %s\n", gai_strerror(err));
	else
		(void)printf("host: %s, serv: %s\n", hbuf, sbuf);

	(void)close(ssock);
	if (write(sock, mesg, len) == -1) {
		perror("write");
		exit(EXIT_FAILURE);
	}
	(void)close(sock);

	exit(EXIT_SUCCESS);
}

/*
 * サーバ用にソケットを用意し、適切に設定した後に listen() までして返す。
 * 失敗した場合はエラーメッセージを出力して -1 を返す。
 */
static int
serverSocket(int family, const char *serv)
{
	struct addrinfo hints, *result, *rp;
	int err, sock, yes;

	(void)memset(&hints, 0, sizeof(hints));
	hints.ai_family = family;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	err = getaddrinfo(NULL, serv, &hints, &result);
	if (err != 0) {
		(void)fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(err));
		return -1;
	}

	for (rp = result; rp != NULL; rp = rp->ai_next) {
		sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if (sock == -1)
			continue;
		yes = 1;
		err = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
				&yes, sizeof(yes));
		if (err == -1)
			goto retry;
		if (bind(sock, rp->ai_addr, rp->ai_addrlen) == -1)
			goto retry;
		if (listen(sock, BACKLOG) == 0)
			break;	/* success */
retry:		(void)close(sock);
	}
	err = errno;
	freeaddrinfo(result);

	if (rp == NULL) {
		(void)fprintf(stderr, "%s: Could not listen\n", __FUNCTION__);
		return -1;
	}

	return sock;
}

/*
 * 子プロセスが死んだなら全員成仏させる。
 */
static void
sigHandler(int sig)
{
	switch (sig) {
	case SIGCHLD:
		while (waitpid(-1, NULL, WNOHANG) > 0)
			;
		break;
	}
}

/*
 * 使用方法をエラー出力して自死する。
 */
static void
usage(void)
{
	(void)fprintf(stderr, "usage: telnetmkn [ -f msgfile ] [ -s serv ]\n");
	exit(EXIT_FAILURE);
}
