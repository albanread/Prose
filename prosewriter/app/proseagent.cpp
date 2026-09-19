// proseagent — the guest side of the ProseWriter dev harness.
//
// Supervised by launch_daemon (job com.prose.proseagent, respawn). The
// host reaches it through QEMU's hostfwd as 127.0.0.1:9000; one request
// per connection:
//
//   ping
//   run <shell command>          -> output, then "EXIT <n>" / "EXIT TIMEOUT"
//   get <path>                   -> "OK <len>" + bytes | "ERR ..."
//   put <path> <len> + <bytes>   -> "OK" | "ERR ..."
//   launch <prog> [args ...]     -> "OK <pid>" | "ERR ..."
//
// Rules it lives by, each earned the hard way:
//   SIGPIPE is ignored — a client that hangs up must never kill us.
//   Every syscall is checked; failures are logged, never fatal if the
//   loop can continue.
//   run children get their own session and a deadline; a stuck command
//   is killed, not endured.
//   put writes beside the target and renames over it — a running binary
//   is never truncated.

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <OS.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static const int kPort = 9000;
static const bigtime_t kRunDeadline = 90000000LL;	// 90 s
					// (microseconds; a 1.2e9 here once meant twenty
					// minutes, and one hung hey owned the agent)

static void
log(const char* what, const char* detail = NULL)
{
	fprintf(stderr, "proseagent: %s%s%s\n", what, detail ? ": " : "",
		detail ? detail : "");
	fflush(stderr);
}

static bool
sendAll(int fd, const void* data, size_t len)
{
	const char* p = (const char*)data;
	size_t left = len;
	while (left > 0) {
		ssize_t n = send(fd, p, left, MSG_NOSIGNAL);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			return false;	// client gone; never fatal for us
		}
		p += n;
		left -= (size_t)n;
	}
	return true;
}

static bool
sendStr(int fd, const char* text)
{
	return sendAll(fd, text, strlen(text));
}

static bool
recvLine(int fd, std::string& line)
{
	line.clear();
	char c;
	while (true) {
		ssize_t n = recv(fd, &c, 1, 0);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			return false;
		}
		if (n == 0)
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
		ssize_t n = recv(fd, buf + got, len - got, 0);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			return false;
		}
		if (n == 0)
			return false;
		got += (size_t)n;
	}
	return true;
}

static void
handleRun(int fd, const std::string& cmd)
{
	log("run", cmd.c_str());
	int out[2];
	if (pipe(out) != 0) {
		log("pipe failed", strerror(errno));
		sendStr(fd, "ERR pipe\n");
		return;
	}
	pid_t pid = fork();
	if (pid < 0) {
		log("fork failed", strerror(errno));
		close(out[0]);
		close(out[1]);
		sendStr(fd, "ERR fork\n");
		return;
	}
	if (pid == 0) {
		setsid();
		if (dup2(out[1], 1) < 0)
			_exit(126);
		if (dup2(out[1], 2) < 0)
			_exit(126);
		close(out[0]);
		close(out[1]);
		int devnull = open("/dev/null", O_RDONLY);
		if (devnull >= 0)
			dup2(devnull, 0);
		execl("/bin/sh", "sh", "-c", cmd.c_str(), (char*)NULL);
		_exit(127);
	}
	close(out[1]);	// the request socket is FD_CLOEXEC; children keep none

	char buf[8192];
	bool timedOut = false;
	bool clientGone = false;
	const bigtime_t deadline = system_time() + kRunDeadline;
	while (true) {
		fd_set set;
		FD_ZERO(&set);
		FD_SET(out[0], &set);
		struct timeval tv { 1, 0 };
		int r = select(out[0] + 1, &set, NULL, NULL, &tv);
		if (r > 0) {
			ssize_t n = read(out[0], buf, sizeof(buf));
			if (n <= 0)
				break;	// child side closed: done
			if (!sendAll(fd, buf, (size_t)n))
				clientGone = true;	// keep draining so the child can exit
		} else if (r == 0) {
			if (system_time() > deadline) {
				timedOut = true;
				log("run timed out, killing child");
				kill(-pid, SIGKILL);
				ssize_t n;
				while ((n = read(out[0], buf, sizeof(buf))) > 0) {
					if (!clientGone)
						sendAll(fd, buf, (size_t)n);
				}
				break;
			}
		} else if (errno != EINTR) {
			log("select failed", strerror(errno));
			break;
		}
	}
	close(out[0]);
	int status = 0;
	while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
	}
	char tail[48];
	if (timedOut)
		snprintf(tail, sizeof(tail), "\nEXIT TIMEOUT\n");
	else if (WIFEXITED(status))
		snprintf(tail, sizeof(tail), "\nEXIT %d\n", WEXITSTATUS(status));
	else if (WIFSIGNALED(status))
		snprintf(tail, sizeof(tail), "\nEXIT %d\n", -WTERMSIG(status));
	else
		snprintf(tail, sizeof(tail), "\nEXIT ?\n");
	sendStr(fd, tail);
}

static void
handleGet(int fd, const std::string& path)
{
	log("get", path.c_str());
	struct stat st;
	if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
		sendStr(fd, "ERR open\n");
		return;
	}
	FILE* f = fopen(path.c_str(), "rb");
	if (!f) {
		sendStr(fd, "ERR open\n");
		return;
	}
	char head[48];
	snprintf(head, sizeof(head), "OK %lld\n", (long long)st.st_size);
	if (!sendStr(fd, head)) {
		fclose(f);
		return;
	}
	char buf[8192];
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
		if (!sendAll(fd, buf, n))
			break;
	}
	if (ferror(f))
		log("read failed", path.c_str());
	fclose(f);
}

static void
handlePut(int fd, const std::string& path, long len)
{
	log("put", path.c_str());
	if (len < 0 || len > 512L * 1024 * 1024) {
		char sink[8192];
		while (len > 0) {
			size_t chunk = len > (long)sizeof(sink) ? sizeof(sink)
				: (size_t)len;
			if (!recvExact(fd, sink, chunk))
				return;
			len -= (long)chunk;
		}
		sendStr(fd, "ERR size\n");
		return;
	}
	std::string tmp = path + ".agentpart";
	FILE* f = fopen(tmp.c_str(), "wb");
	if (!f) {
		char sink[8192];
		while (len > 0) {
			size_t chunk = len > (long)sizeof(sink) ? sizeof(sink)
				: (size_t)len;
			if (!recvExact(fd, sink, chunk))
				return;
			len -= (long)chunk;
		}
		sendStr(fd, "ERR create\n");
		return;
	}
	char buf[8192];
	bool ok = true;
	while (len > 0) {
		size_t chunk = len > (long)sizeof(buf) ? sizeof(buf) : (size_t)len;
		if (!recvExact(fd, buf, chunk)) {
			ok = false;
			break;
		}
		if (fwrite(buf, 1, chunk, f) != chunk) {
			ok = false;
			break;
		}
		len -= (long)chunk;
	}
	if (fclose(f) != 0)
		ok = false;
	if (ok && rename(tmp.c_str(), path.c_str()) != 0) {
		log("rename failed", strerror(errno));
		ok = false;
	}
	if (!ok)
		unlink(tmp.c_str());
	else
		chmod(path.c_str(), 0755);
	sendStr(fd, ok ? "OK\n" : "ERR write\n");
}

static void
handleLaunch(int fd, char** argv)
{
	log("launch", argv[0]);
	pid_t pid = fork();
	if (pid < 0) {
		sendStr(fd, "ERR fork\n");
		return;
	}
	if (pid == 0) {
		setsid();
		int devnull = open("/dev/null", O_RDONLY);
		if (devnull >= 0)
			dup2(devnull, 0);
		execv(argv[0], argv);
		_exit(127);
	}
	char head[48];
	snprintf(head, sizeof(head), "OK %d\n", pid);
	sendStr(fd, head);
}

int
main()
{
	// A client that hangs up mid-reply must never kill the agent (send on
	// a closed socket raises SIGPIPE; default action is death).
	signal(SIGPIPE, SIG_IGN);

	int s = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (s < 0) {
		log("socket failed", strerror(errno));
		return 1;
	}
	int one = 1;
	setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(kPort);
	int bindTries = 0;
	while (bind(s, (sockaddr*)&addr, sizeof(addr)) < 0) {
		if (++bindTries > 10) {
			log("bind failed", strerror(errno));
			return 1;
		}
		sleep(1);	// a restart may race TIME_WAIT remains of its own port
	}
	if (listen(s, 8) < 0) {
		log("listen failed", strerror(errno));
		return 1;
	}
	log("listening on 9000");

	while (true) {
		int fd = accept(s, NULL, NULL);
		if (fd < 0) {
			if (errno == EINTR)
				continue;
			log("accept failed", strerror(errno));
			sleep(1);
			continue;
		}
		fcntl(fd, F_SETFD, FD_CLOEXEC);

		std::string line;
		if (!recvLine(fd, line)) {
			close(fd);
			continue;
		}
		if (line == "ping") {
			system_info info;
			get_system_info(&info);
			char msg[128];
			snprintf(msg, sizeof(msg),
				"PONG haiku %d-bit cpus %d up %llds\n",
				(int)(sizeof(void*) * 8), (int)info.cpu_count,
				(long long)(system_time() / 1000000LL));
			sendStr(fd, msg);
		} else if (line.rfind("run ", 0) == 0) {
			handleRun(fd, line.substr(4));
		} else if (line.rfind("get ", 0) == 0) {
			handleGet(fd, line.substr(4));
		} else if (line.rfind("put ", 0) == 0) {
			size_t sp = line.find(' ', 5);
			if (sp == std::string::npos) {
				sendStr(fd, "ERR usage\n");
			} else {
				char* end = NULL;
				long len = strtol(line.c_str() + sp + 1, &end, 10);
				if (end == line.c_str() + sp + 1 || *end != '\0')
					sendStr(fd, "ERR usage\n");
				else
					handlePut(fd, line.substr(4, sp - 4), len);
			}
		} else if (line.rfind("launch ", 0) == 0) {
			std::string rest = line.substr(7);
			char* argv[64] = { NULL };
			int argc = 0;
			char* save = NULL;
			for (char* tok = strtok_r(&rest[0], " ", &save);
					tok && argc < 63; tok = strtok_r(NULL, " ", &save))
				argv[argc++] = tok;
			argv[argc] = NULL;
			if (argc == 0)
				sendStr(fd, "ERR usage\n");
			else
				handleLaunch(fd, argv);
		} else {
			sendStr(fd, "ERR unknown command\n");
		}
		close(fd);
	}
	return 0;
}
