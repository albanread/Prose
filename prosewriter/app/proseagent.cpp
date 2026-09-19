// proseagent — the guest side of the ProseWriter dev harness.
//
// Started by the guest's UserBootscript, listens on 0.0.0.0:9000 (the host
// reaches it through QEMU's hostfwd as 127.0.0.1:9000) and answers one
// request per connection:
//
//   ping
//   run <shell command>          -> <output lines> then "EXIT <code>"
//   get <path>                   -> "OK <len>" + <len> raw bytes | "ERR ..."
//   put <path> <len> + <bytes>   -> "OK" | "ERR ..."     (mode 0755: binaries)
//   launch <prog> [args ...]     -> "OK <pid>" | "ERR ..."
//
// Deliberately tiny: plain BSD sockets, no threads, one request at a time.

#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <fcntl.h>
#include <OS.h>

static const int kPort = 9000;

static bool
recvLine(int fd, std::string& line)
{
	line.clear();
	char c;
	while (true) {
		int n = recv(fd, &c, 1, 0);
		if (n <= 0)
			return !line.empty();
		if (c == '\n')
			return true;
		if (c != '\r')
			line += c;
	}
}

static bool
recvExact(int fd, char* buf, size_t len)
{
	size_t got = 0;
	while (got < len) {
		int n = recv(fd, buf + got, len - got, 0);
		if (n <= 0)
			return false;
		got += n;
	}
	return true;
}

static void
sendAll(int fd, const void* data, size_t len)
{
	const char* p = (const char*)data;
	while (len > 0) {
		int n = send(fd, p, len, 0);
		if (n <= 0)
			return;
		p += n;
		len -= n;
	}
}

static void
handleRun(int fd, const std::string& cmd)
{
	std::string full = cmd + " 2>&1";
	FILE* f = popen(full.c_str(), "r");
	if (!f) {
		sendAll(fd, "ERR popen\n", 10);
		return;
	}
	char buf[4096];
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
		sendAll(fd, buf, n);
	int rc = pclose(f);
	char tail[32];
	snprintf(tail, sizeof(tail), "\nEXIT %d\n",
		WIFEXITED(rc) ? WEXITSTATUS(rc) : -WTERMSIG(rc));
	sendAll(fd, tail, strlen(tail));
}

static void
handleGet(int fd, const std::string& path)
{
	FILE* f = fopen(path.c_str(), "rb");
	if (!f) {
		sendAll(fd, "ERR open\n", 9);
		return;
	}
	fseek(f, 0, SEEK_END);
	long len = ftell(f);
	fseek(f, 0, SEEK_SET);
	char head[40];
	snprintf(head, sizeof(head), "OK %ld\n", len);
	sendAll(fd, head, strlen(head));
	char buf[65536];
	while (len > 0) {
		size_t chunk = fread(buf, 1, sizeof(buf), f);
		if (chunk == 0)
			break;
		sendAll(fd, buf, chunk);
		len -= chunk;
	}
	fclose(f);
}

static void
handlePut(int fd, const std::string& path, long len)
{
	FILE* f = fopen(path.c_str(), "wb");
	if (!f) {
		// eat the body anyway so the protocol stays in step
		char sink[65536];
		while (len > 0) {
			size_t chunk = len > (long)sizeof(sink) ? sizeof(sink) : len;
			if (!recvExact(fd, sink, chunk))
				return;
			len -= chunk;
		}
		sendAll(fd, "ERR create\n", 11);
		return;
	}
	char buf[65536];
	while (len > 0) {
		size_t chunk = len > (long)sizeof(buf) ? sizeof(buf) : len;
		if (!recvExact(fd, buf, chunk)) {
			fclose(f);
			return;
		}
		fwrite(buf, 1, chunk, f);
		len -= chunk;
	}
	fclose(f);
	chmod(path.c_str(), 0755);   // hosts push executables more often than data
	sendAll(fd, "OK\n", 3);
}

static void
handleLaunch(int fd, char** argv)
{
	pid_t pid = fork();
	if (pid < 0) {
		sendAll(fd, "ERR fork\n", 9);
		return;
	}
	if (pid == 0) {
		setsid();
		int devnull = open("/dev/null", O_WRONLY);
		if (devnull >= 0) { dup2(devnull, 0); }
		execv(argv[0], argv);
		_exit(127);
	}
	char head[40];
	snprintf(head, sizeof(head), "OK %d\n", pid);
	sendAll(fd, head, strlen(head));
}

int
main()
{
	int s = socket(AF_INET, SOCK_STREAM, 0);
	if (s < 0) {
		fprintf(stderr, "proseagent: socket: %s\n", strerror(errno));
		return 1;
	}
	int one = 1;
	setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(kPort);
	if (bind(s, (sockaddr*)&addr, sizeof(addr)) < 0) {
		fprintf(stderr, "proseagent: bind: %s\n", strerror(errno));
		return 1;
	}
	listen(s, 4);
	printf("proseagent: listening on %d (build " __DATE__ ")\n", kPort);

	while (true) {
		int fd = accept(s, NULL, NULL);
		if (fd < 0)
			continue;
		// launched children must not inherit the request socket, or the
		// client waits for EOF from a GUI app that never closes it
		fcntl(fd, F_SETFD, FD_CLOEXEC);
		std::string line;
		if (!recvLine(fd, line)) {
			close(fd);
			continue;
		}
		if (line == "ping") {
			system_info info;
			get_system_info(&info);
			char msg[256];
			snprintf(msg, sizeof(msg),
				"PONG haiku %ld-bit up %llds team proseagent\n",
				(long)sizeof(void*) * 8,
				system_time() / 1000000LL);
			sendAll(fd, msg, strlen(msg));
		} else if (line.rfind("run ", 0) == 0) {
			handleRun(fd, line.substr(4));
		} else if (line.rfind("get ", 0) == 0) {
			handleGet(fd, line.substr(4));
		} else if (line.rfind("put ", 0) == 0) {
			size_t sp = line.find(' ', 5);
			if (sp == std::string::npos) {
				sendAll(fd, "ERR usage\n", 10);
			} else {
				long len = atol(line.c_str() + sp + 1);
				handlePut(fd, line.substr(4, sp - 4), len);
			}
		} else if (line.rfind("launch ", 0) == 0) {
			// split the rest on spaces into argv
			std::string rest = line.substr(7);
			char* argv[64] = {NULL};
			int argc = 0;
			char* save = NULL;
			for (char* tok = strtok_r(&rest[0], " ", &save); tok && argc < 63;
					tok = strtok_r(NULL, " ", &save)) {
				argv[argc++] = tok;
			}
			argv[argc] = NULL;
			if (argc == 0)
				sendAll(fd, "ERR usage\n", 10);
			else
				handleLaunch(fd, argv);
		} else {
			sendAll(fd, "ERR unknown command\n", 21);
		}
		close(fd);
	}
	return 0;
}
