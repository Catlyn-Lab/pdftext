//go:build cgo && !nomupdf

// MuPDF's locks, for a Go program that uses the package from several
// goroutines at once. Every context gets the same FZ_LOCK_MAX mutexes: each
// context's own allocator, glyph cache and font loading are guarded, and so
// is what MuPDF keeps for the whole process, such as the pointer its
// HarfBuzz glue keeps while text is shaped (guarded by FZ_LOCK_FREETYPE).
// Without locks two Story layouts at once crash the process.
//
// MuPDF takes these locks in a fixed order and never one it already holds,
// so plain (non-recursive) mutexes are what it expects.

#include <mupdf/fitz.h>

#include "nx.h"

#ifdef _WIN32
#include <windows.h>

static CRITICAL_SECTION nx_mutex[FZ_LOCK_MAX];
static INIT_ONCE nx_once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK nx_init_mutexes(PINIT_ONCE once, PVOID param, PVOID *out) {
	(void)once; (void)param; (void)out;
	for (int i = 0; i < FZ_LOCK_MAX; i++) InitializeCriticalSection(&nx_mutex[i]);
	return TRUE;
}

static void nx_lock(void *user, int lock) { (void)user; EnterCriticalSection(&nx_mutex[lock]); }
static void nx_unlock(void *user, int lock) { (void)user; LeaveCriticalSection(&nx_mutex[lock]); }
static void nx_init(void) { InitOnceExecuteOnce(&nx_once, nx_init_mutexes, NULL, NULL); }
#else
#include <pthread.h>

static pthread_mutex_t nx_mutex[FZ_LOCK_MAX];
static pthread_once_t nx_once = PTHREAD_ONCE_INIT;

static void nx_init_mutexes(void) {
	for (int i = 0; i < FZ_LOCK_MAX; i++) pthread_mutex_init(&nx_mutex[i], NULL);
}

static void nx_lock(void *user, int lock) { (void)user; pthread_mutex_lock(&nx_mutex[lock]); }
static void nx_unlock(void *user, int lock) { (void)user; pthread_mutex_unlock(&nx_mutex[lock]); }
static void nx_init(void) { pthread_once(&nx_once, nx_init_mutexes); }
#endif

static fz_locks_context nx_locks_ctx = {NULL, nx_lock, nx_unlock};

// nx_locks is the locks every context is made with.
fz_locks_context *nx_locks(void) {
	nx_init();
	return &nx_locks_ctx;
}
