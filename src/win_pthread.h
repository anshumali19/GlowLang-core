#ifndef WIN_PTHREAD_H
#define WIN_PTHREAD_H

#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>

typedef SRWLOCK pthread_mutex_t;
#define PTHREAD_MUTEX_INITIALIZER SRWLOCK_INIT

extern pthread_mutex_t glow_gil;

static inline int pthread_mutex_lock(pthread_mutex_t *m) {
    if (m == &glow_gil) return 0;
    AcquireSRWLockExclusive(m);
    return 0;
}

static inline int pthread_mutex_unlock(pthread_mutex_t *m) {
    if (m == &glow_gil) return 0;
    ReleaseSRWLockExclusive(m);
    return 0;
}

typedef HANDLE pthread_t;

static inline int pthread_create(pthread_t *thread, void *attr, void *(*start_routine) (void *), void *arg) {
    *thread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)start_routine, arg, 0, NULL);
    return *thread == NULL;
}

static inline int pthread_detach(pthread_t thread) {
    CloseHandle(thread);
    return 0;
}

static inline int pthread_join(pthread_t thread, void **retval) {
    (void)retval;
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
    return 0;
}

#endif // _WIN32

#endif // WIN_PTHREAD_H
