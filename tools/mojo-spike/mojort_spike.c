// Spike only: the eight Mojo runtime entry points a hello-world object needs,
// just enough to prove compile -> link -> run on Haiku. Not the real runtime.
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

struct global {
	struct global* next;
	char* name;
	size_t length;
	void* value;
	void (*destroy)(void*);
};

static struct global* sGlobals;
static pthread_mutex_t sLock = PTHREAD_MUTEX_INITIALIZER;
static int sArgc;
static char** sArgv;
static int sDevice;

void KGEN_CompilerRT_SetArgV(int argc, char** argv) { sArgc = argc; sArgv = argv; }
void KGEN_CompilerRT_PrintStackTraceOnFault(void) {}
void KGEN_CompilerRT_AlignedFree(void* pointer) { free(pointer); }
void* KGEN_CompilerRT_AsyncRT_GetOrCreateCPUDevice(void) { return &sDevice; }
void* KGEN_CompilerRT_AsyncRT_GetCurrentCPUDevice(void) { return NULL; }
void KGEN_CompilerRT_AsyncRT_ReleaseCPUDevice(void* device) { (void)device; }

// llvm::StringRef by value arrives as (data, length) in x0, x1 under AAPCS64.
void*
KGEN_CompilerRT_GetOrCreateGlobal(const char* name, size_t length,
	void* (*init)(void), void (*destroy)(void*))
{
	pthread_mutex_lock(&sLock);
	for (struct global* g = sGlobals; g != NULL; g = g->next) {
		if (g->length == length && memcmp(g->name, name, length) == 0) {
			pthread_mutex_unlock(&sLock);
			return g->value;
		}
	}
	void* value = NULL;
	if (init != NULL) {
		struct global* g = malloc(sizeof(*g));
		g->name = malloc(length);
		memcpy(g->name, name, length);
		g->length = length;
		g->value = value = init();
		g->destroy = destroy;
		g->next = sGlobals;
		sGlobals = g;
	}
	pthread_mutex_unlock(&sLock);
	return value;
}

void
KGEN_CompilerRT_DestroyGlobals(void)
{
	while (sGlobals != NULL) {
		struct global* g = sGlobals;
		sGlobals = g->next;
		if (g->destroy != NULL)
			g->destroy(g->value);
		free(g->name);
		free(g);
	}
}
