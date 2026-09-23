// Patch 0132's regression test: thread-local storage through TLS descriptors
// (R_AARCH64_TLSDESC), which is all clang generates for thread_local on arm64.
// Before 0132 Haiku's runtime_loader refused every such program ("Troubles
// relocating: Bad data"). Built on Prose with Prose's own clang:
//
//	clang++ -shared -fPIC -o libtlsshared.so tlsshared.cpp
//	clang++ -shared -fPIC -o libtlsdl.so tlsdl.cpp
//	clang++ -o tlstest tlstest.cpp -L. -ltlsshared -Wl,-rpath,'$ORIGIN'
//	./tlstest
#include <dlfcn.h>

#include <atomic>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>

extern thread_local int tShared;
int shared_bump();

static thread_local int tProgram = 10;
static std::atomic<int> sFailures{0};
static int sPassed = 0;
static int sTotal = 0;


static void
check(bool ok, const char* what)
{
	sTotal++;
	if (ok)
		sPassed++;
	std::printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
}


// Each thread must start from the initial values and see only its own.
static void
worker(int index, int (*lateBump)())
{
	for (int i = 0; i < 1000; i++) {
		tProgram++;
		shared_bump();
		lateBump();
	}
	if (tProgram != 10 + 1000 || tShared != 100 + 1000 || lateBump() != 1000 + 1001) {
		std::printf("  thread %d: program %d, shared %d\n", index, tProgram, tShared);
		sFailures++;
	}
}


int
main()
{
	tProgram += 5;
	check(tProgram == 15, "a thread_local in the program");

	check(shared_bump() == 101 && tShared == 101,
		"a thread_local in a shared library, from the program");

	void* library = dlopen("./libtlsdl.so", RTLD_NOW);
	if (library == NULL)
		library = dlopen("libtlsdl.so", RTLD_NOW);
	int (*lateBump)() = library != NULL
		? (int (*)())dlsym(library, "late_bump") : NULL;
	check(lateBump != NULL && lateBump() == 1001,
		"a thread_local in a library loaded with dlopen()");
	if (lateBump == NULL)
		return 1;

	static std::once_flag flag;
	int calls = 0;
	std::call_once(flag, [&calls] { calls++; });
	std::call_once(flag, [&calls] { calls++; });
	check(calls == 1, "std::call_once (libstdc++'s thread_locals)");

	std::vector<std::thread> threads;
	for (int i = 0; i < 8; i++)
		threads.emplace_back(worker, i, lateBump);
	for (std::thread& thread : threads)
		thread.join();
	check(sFailures == 0, "8 threads, each with its own copy of all three");

	check(tProgram == 15 && tShared == 101 && lateBump() == 1002,
		"the main thread's values, untouched by the others");

	std::printf("SELFTEST %s %d/%d\n", sPassed == sTotal ? "PASS" : "FAIL",
		sPassed, sTotal);
	return sPassed == sTotal ? 0 : 1;
}
