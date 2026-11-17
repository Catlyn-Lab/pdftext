//go:build cgo && !nomupdf

// MuPDF's locks, for a Go program that uses the package from several
// goroutines at once. Every pdftext context is its own (made by
// fz_new_context, never cloned) and is used by one goroutine at a time, so
// what MuPDF guards per context (its allocator, glyph cache and store) needs
// no lock. What it keeps for the whole process does: the pointer its
// HarfBuzz glue keeps while text is shaped, guarded by FZ_LOCK_FREETYPE.
// Without that, two Story layouts at once crash the process. So
// FZ_LOCK_FREETYPE is one mutex for the whole process, and the other locks
// cost nothing (sharing them all made independent documents wait on each
// other's every allocation).

#include <mupdf/fitz.h>

#include "nx.h"

#ifdef _WIN32
#include <windows.h>

static CRITICAL_SECTION nx_freetype;
static INIT_ONCE nx_once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK nx_init_mutex(PINIT_ONCE once, PVOID param, PVOID *out) {
	(void)once; (void)param; (void)out;
	InitializeCriticalSection(&nx_freetype);
	return TRUE;
}

static void nx_lock(void *user, int lock) {
	(void)user;
	if (lock == FZ_LOCK_FREETYPE) EnterCriticalSection(&nx_freetype);
}
static void nx_unlock(void *user, int lock) {
	(void)user;
	if (lock == FZ_LOCK_FREETYPE) LeaveCriticalSection(&nx_freetype);
}
static void nx_init(void) { InitOnceExecuteOnce(&nx_once, nx_init_mutex, NULL, NULL); }
#else
#include <pthread.h>

static pthread_mutex_t nx_freetype = PTHREAD_MUTEX_INITIALIZER;

static void nx_lock(void *user, int lock) {
	(void)user;
	if (lock == FZ_LOCK_FREETYPE) pthread_mutex_lock(&nx_freetype);
}
static void nx_unlock(void *user, int lock) {
	(void)user;
	if (lock == FZ_LOCK_FREETYPE) pthread_mutex_unlock(&nx_freetype);
}
static void nx_init(void) {}
#endif

static fz_locks_context nx_locks_ctx = {NULL, nx_lock, nx_unlock};

// nx_locks is the locks every context is made with.
fz_locks_context *nx_locks(void) {
	nx_init();
	return &nx_locks_ctx;
}
