/*
 * trap_check: what a user thread on Prose arm64 gets for the synchronous
 * exceptions the kernel turns into signals (do_sync_handler, patch 0029).
 *
 * Each case runs in a forked child. With handlers installed, the child
 * reports the signal, si_code and si_addr it got; without handlers, the
 * child must end (debug_server names the exception in the syslog first).
 * Last line: PASS or FAIL. Built and run by private_workspace/trap-test.sh.
 */
#include <setjmp.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define PSTATE_IL	(1UL << 20)

static sigjmp_buf sJump;
static void* volatile sExpected;
	// the address each case expects in si_addr, stored by its own asm
static volatile int sCount;
static struct {
	int signo;
	int code;
	void* address;
} sGot[2];
static uint64_t sWords[4] __attribute__((aligned(16)));


static void
record(int signo, siginfo_t* info)
{
	int i = sCount < 2 ? sCount : 1;
	sGot[i].signo = signo;
	sGot[i].code = info->si_code;
	sGot[i].address = info->si_addr;
	sCount++;
}


static void
handler(int signo, siginfo_t* info, void* context)
{
	record(signo, info);
	siglongjmp(sJump, 1);
}


/* The first SIGILL (a UDF) returns to the next instruction with PSTATE.IL
 * set through the signal frame's SPSR; that instruction then raises an
 * illegal execution state exception, the second signal. */
static void
illstate_handler(int signo, siginfo_t* info, void* context)
{
	ucontext_t* uc = (ucontext_t*)context;
	record(signo, info);
	if (sCount == 1) {
		uc->uc_mcontext.spsr |= PSTATE_IL;
		uc->uc_mcontext.elr += 4;
		return;
	}
	siglongjmp(sJump, 1);
}


/* -- the cases: each stores the address it expects, then traps ------- */

static void
t_udf(void)
{
	__asm__ volatile("adr x9, 1f\n\tstr x9, %0\n"
		"1:\t.inst 0x00000000\n"			/* udf #0 */
		: "=m"(sExpected) :: "x9", "memory");
}

static void
t_sve(void)
{
	__asm__ volatile("adr x9, 1f\n\tstr x9, %0\n"
		"1:\t.inst 0x2518e3e0\n"			/* ptrue p0.b */
		: "=m"(sExpected) :: "x9", "memory");
}

static void
t_sme(void)
{
	__asm__ volatile("adr x9, 1f\n\tstr x9, %0\n"
		"1:\t.inst 0xd503477f\n"			/* smstart */
		: "=m"(sExpected) :: "x9", "memory");
}

static void
t_mrs_id(void)
{
	uint64_t value;
	__asm__ volatile("adr x9, 1f\n\tstr x9, %1\n"
		"1:\tmrs %0, ID_AA64ISAR0_EL1\n"
		: "=r"(value), "=m"(sExpected) :: "x9", "memory");
	(void)value;
}

static void
t_daif(void)
{
	/* SCTLR_EL1.UMA is clear: EL0 may not touch the interrupt masks */
	__asm__ volatile("adr x9, 1f\n\tstr x9, %0\n"
		"1:\tmsr daifset, #2\n"
		: "=m"(sExpected) :: "x9", "memory");
}

static void
t_brk(void)
{
	__asm__ volatile("adr x9, 1f\n\tstr x9, %0\n"
		"1:\tbrk #1000\n"					/* __builtin_trap() */
		: "=m"(sExpected) :: "x9", "memory");
}

static void
t_pc_align(void)
{
	__asm__ volatile("adr x9, 1f\n\tadd x9, x9, #2\n\tstr x9, %0\n"
		"\tbr x9\n"
		"1:\tnop\n\tnop\n"
		: "=m"(sExpected) :: "x9", "memory");
}

static void
t_sp_align(void)
{
	/* SCTLR_EL1.SA0 is set: a load through an SP that is not 16-byte
	 * aligned faults. siglongjmp() restores SP. */
	__asm__ volatile("mov x9, sp\n\tsub x10, x9, #8\n\tstr x10, %0\n"
		"\tmov sp, x10\n"
		"\tldr x11, [sp]\n"
		"\tmov sp, x9\n"
		: "=m"(sExpected) :: "x9", "x10", "x11", "memory");
}

static void
t_ldxr_align(void)
{
	/* Exclusives need natural alignment, whatever SCTLR_EL1.A says. The
	 * M4 lets one through inside a 16-byte granule (FEAT_LSE2), so this one
	 * crosses the boundary. */
	__asm__ volatile("add x9, %1, #12\n\tstr x9, %0\n"
		"\tldxr x10, [x9]\n"
		"\tclrex\n"
		: "=m"(sExpected) : "r"(sWords) : "x9", "x10", "memory");
}

static void
t_segv(void)
{
	volatile int* bad = (volatile int*)(uintptr_t)8;
	sExpected = (void*)bad;
	*bad = 1;
}

static void
t_illstate(void)
{
	__asm__ volatile("adr x9, 1f\n\tstr x9, %0\n"
		"\t.inst 0x00000000\n"
		"1:\tnop\n\tnop\n"
		: "=m"(sExpected) :: "x9", "memory");
}


/* -- running them ------------------------------------------------------- */

static const char*
signal_name(int signo)
{
	switch (signo) {
		case SIGILL: return "SIGILL";
		case SIGTRAP: return "SIGTRAP";
		case SIGBUS: return "SIGBUS";
		case SIGSEGV: return "SIGSEGV";
		case SIGFPE: return "SIGFPE";
		case SIGKILL: return "SIGKILL";
		case SIGKILLTHR: return "SIGKILLTHR";
		case 0: return "no signal";
	}
	return "other signal";
}

static const char*
code_name(int code)
{
	switch (code) {
		case ILL_ILLOPC: return "ILL_ILLOPC";
		case ILL_ILLTRP: return "ILL_ILLTRP";
		case ILL_PRVOPC: return "ILL_PRVOPC";
		case TRAP_BRKPT: return "TRAP_BRKPT";
		case BUS_ADRALN: return "BUS_ADRALN";
		case BUS_ADRERR: return "BUS_ADRERR";
		case SEGV_MAPERR: return "SEGV_MAPERR";
		case SEGV_ACCERR: return "SEGV_ACCERR";
	}
	return "other code";
}

enum { HANDLED, ILLSTATE, NO_HANDLER };

struct test {
	const char*	name;
	void		(*run)(void);
	int			mode;
	int			signo;
	int			code;
	int			altCode;	/* also accepted, e.g. ILL_ILLOPC without FEAT_IDST */
};

static const struct test kTests[] = {
	{ "udf", t_udf, HANDLED, SIGILL, ILL_ILLOPC, 0 },
	{ "sve (ptrue)", t_sve, HANDLED, SIGILL, ILL_ILLOPC, 0 },
	{ "sme (smstart)", t_sme, HANDLED, SIGILL, ILL_ILLOPC, 0 },
	{ "mrs ID_AA64ISAR0_EL1", t_mrs_id, HANDLED, SIGILL, ILL_PRVOPC, ILL_ILLOPC },
	{ "msr daifset", t_daif, HANDLED, SIGILL, ILL_PRVOPC, 0 },
	{ "brk #1000", t_brk, HANDLED, SIGTRAP, TRAP_BRKPT, 0 },
	{ "misaligned pc", t_pc_align, HANDLED, SIGBUS, BUS_ADRALN, 0 },
	{ "misaligned sp", t_sp_align, HANDLED, SIGBUS, BUS_ADRALN, 0 },
	{ "misaligned ldxr", t_ldxr_align, HANDLED, SIGBUS, BUS_ADRALN, 0 },
	{ "store to 0x8", t_segv, HANDLED, SIGSEGV, SEGV_MAPERR, 0 },
	{ "illegal state", t_illstate, ILLSTATE, SIGILL, ILL_ILLOPC, 0 },
	{ "udf, no handler", t_udf, NO_HANDLER, 0, 0, 0 },
	{ "brk, no handler", t_brk, NO_HANDLER, 0, 0, 0 },
};

static const int kCatch[] = { SIGILL, SIGTRAP, SIGBUS, SIGSEGV, SIGFPE };


static int
check(const struct test* test, int i, void* expected)
{
	int ok = sGot[i].signo == test->signo
		&& (sGot[i].code == test->code
			|| (test->altCode != 0 && sGot[i].code == test->altCode))
		&& sGot[i].address == expected;
	printf("%-22s %s %s (%d) at %p, expected %s %s at %p: %s\n",
		i == 0 ? test->name : "  then",
		signal_name(sGot[i].signo), code_name(sGot[i].code), sGot[i].code,
		sGot[i].address, signal_name(test->signo), code_name(test->code),
		expected, ok ? "ok" : "WRONG");
	return ok;
}


/* runs in the child; the exit status is the verdict */
static int
child(const struct test* test)
{
	struct sigaction action;
	memset(&action, 0, sizeof(action));
	action.sa_flags = SA_SIGINFO;
	action.sa_sigaction = test->mode == ILLSTATE ? illstate_handler : handler;
	if (test->mode != NO_HANDLER) {
		for (size_t i = 0; i < sizeof(kCatch) / sizeof(kCatch[0]); i++)
			sigaction(kCatch[i], &action, NULL);
	}

	if (sigsetjmp(sJump, 1) == 0) {
		test->run();
		if (test->mode == NO_HANDLER) {
			printf("%-22s returned: no exception\n", test->name);
			return 1;
		}
	}

	if (test->mode == HANDLED)
		return !check(test, 0, sExpected);

	/* ILLSTATE: the UDF, then the instruction after it with PSTATE.IL */
	int ok = check(test, 0, (char*)sExpected - 4);
	if (sCount < 2) {
		printf("%-22s no second signal: sigreturn did not pass PSTATE.IL "
			"(SPSR sanitized?), not tested\n", "  then");
		return !ok;
	}
	return !(check(test, 1, sExpected) && ok);
}


int
main(void)
{
	int failed = 0;
	setvbuf(stdout, NULL, _IOLBF, 0);

	for (size_t i = 0; i < sizeof(kTests) / sizeof(kTests[0]); i++) {
		const struct test* test = &kTests[i];
		fflush(stdout);
		pid_t pid = fork();
		if (pid == 0) {
			int status = child(test);
			fflush(stdout);
			_exit(status);
		}
		if (pid < 0) {
			printf("%-22s fork failed\n", test->name);
			failed++;
			continue;
		}

		int status = 0;
		int done = 0;
		for (int tries = 0; tries < 1000 && !done; tries++) {
			if (waitpid(pid, &status, WNOHANG) == pid)
				done = 1;
			else
				usleep(10000);
		}
		if (!done) {
			printf("%-22s still running after 10 s (in the debugger?), "
				"killed\n", test->name);
			kill(pid, SIGKILL);
			waitpid(pid, &status, 0);
			failed++;
			continue;
		}

		if (test->mode == NO_HANDLER) {
			/* debug_server kills the team (default_action kill); without
			 * a debugger, the signal's default action ends it */
			if (WIFSIGNALED(status)) {
				printf("%-22s ended by %s (%d): ok\n", test->name,
					signal_name(WTERMSIG(status)), WTERMSIG(status));
			} else {
				printf("%-22s exited with %d: WRONG\n", test->name,
					WEXITSTATUS(status));
				failed++;
			}
		} else if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
			if (WIFSIGNALED(status)) {
				printf("%-22s child ended by %s (%d): WRONG\n", test->name,
					signal_name(WTERMSIG(status)), WTERMSIG(status));
			}
			failed++;
		}
	}

	printf("%s\n", failed == 0 ? "PASS" : "FAIL");
	return failed;
}
