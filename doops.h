#ifndef __DOOPS_H
#define __DOOPS_H

#include <time.h>
#include <stdlib.h>
#include <errno.h>
#include <inttypes.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DOOPS_MAX_SLEEP     500

#if !defined(DOOPS_FREE) || !defined(DOOPS_MALLOC)
    #define DOOPS_MALLOC(bytes)     malloc(bytes)
    #define DOOPS_FREE(ptr)         free(ptr)
#endif

#ifdef _WIN32
    #include <windows.h>

#if defined(_MSC_VER) || defined(_MSC_EXTENSIONS)
        #define DELTA_EPOCH_IN_MICROSECS  116444736000000000Ui64
    #else
        #define DELTA_EPOCH_IN_MICROSECS  116444736000000000ULL
    #endif

    int gettimeofday(struct timeval *tv, void *not_used) {
        FILETIME         ft;
        unsigned __int64 tmpres = 0;

        if (NULL != tv) {
            GetSystemTimeAsFileTime(&ft);

            tmpres  |= ft.dwHighDateTime;
            tmpres <<= 32;
            tmpres  |= ft.dwLowDateTime;

            tmpres     -= DELTA_EPOCH_IN_MICROSECS;
            tmpres     /= 10; 

            tv->tv_sec  = (long)(tmpres / 1000000UL);
            tv->tv_usec = (long)(tmpres % 1000000UL);
        }
        return 0;
    }
#else
    #include <sys/time.h>
    #include <unistd.h>
#endif

struct doops_loop;

typedef int (*doop_callback)(struct doops_loop *loop, void *userdata);
typedef int (*doop_io_callback)(struct doops_loop *loop, int fd, void *userdata);
typedef int (*doop_idle_callback)(struct doops_loop *loop);

struct doops_event {
    doop_callback event_callback;
    uint64_t when;
    uint64_t interval;
    void *user_data;
    struct doops_event *next;
};

struct doops_io_event {
    doop_io_callback event_callback;
    int fd;
    void *user_data;
    struct doops_io_event *next;
};

struct doops_loop {
    int quit;
    doop_idle_callback idle;
    struct doops_io_event *io_events;
    struct doops_event *events;
};

static uint64_t milliseconds() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)(tv.tv_sec) * 1000 + (uint64_t)(tv.tv_usec) / 1000;
}

static void loop_init(struct doops_loop *loop) {
    if (loop)
        memset(loop, 0, sizeof(struct doops_loop));
}

static struct doops_loop *loop_new() {
    struct doops_loop *loop = (struct doops_loop *)DOOPS_MALLOC(sizeof(struct doops_loop));
    if (loop)
        loop_init(loop);
    return loop;
}

static int loop_add(struct doops_loop *loop, doop_callback callback, uint64_t interval, void *user_data) {
    if ((!callback) || (!loop)) {
        errno = EINVAL;
        return -1;
    }

    struct doops_event *event_callback = (struct doops_event *)DOOPS_MALLOC(sizeof(struct doops_event));
    if (!event_callback) {
        errno = ENOMEM;
        return -1;
    }

    event_callback->event_callback = callback;
    event_callback->interval = interval;
    event_callback->when = milliseconds() + interval;
    event_callback->user_data = user_data;
    event_callback->next = loop->events;

    loop->events = event_callback;
    return 0;
}

static int loop_add_io(struct doops_loop *loop, int fd, doop_io_callback callback, void *user_data) {
    if ((!callback) || (!loop)) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

static void loop_quit(struct doops_loop *loop) {
    if (loop)
        loop->quit = 1;
}

static int _private_loop_iterate(struct doops_loop *loop, int *sleep_val) {
    int loops = 0;
    if (sleep_val)
        *sleep_val = DOOPS_MAX_SLEEP;
    if ((loop->events) && (!loop->quit)) {
        struct doops_event *ev = loop->events;
        struct doops_event *prev_ev = NULL;
        struct doops_event *next_ev = NULL; 
        while (ev) {
            next_ev = ev->next;
            uint64_t now = milliseconds();
            if (ev->when <= now) {
                loops ++;
                if (ev->event_callback(loop, ev->user_data)) {
                    DOOPS_FREE(ev);
                    if (prev_ev)
                        prev_ev->next = next_ev;
                    else
                        loop->events = next_ev;
                    ev = next_ev;
                    continue;
                }
                while ((ev->when <= now) && (ev->interval))
                    ev->when += ev->interval;
            } else
            if (sleep_val) {
                int delta = (int)(ev->when - now);
                if (delta < *sleep_val)
                    *sleep_val = delta;
            }
            prev_ev = ev;
            ev = next_ev;
        }
    }
    return loops;
}

static int loop_iterate(struct doops_loop *loop) {
    return _private_loop_iterate(loop, NULL);
}

static int loop_idle(struct doops_loop *loop, doop_idle_callback callback) {
    if (!loop) {
        errno = EINVAL;
        return -1;
    }
    loop->idle = callback;
    return 0;
}

static void _private_loop_remove_events(struct doops_loop *loop) {
    struct doops_event *next_ev;
    while (loop->events) {
        next_ev = loop->events->next;
        DOOPS_FREE(loop->events);
        loop->events = next_ev;
    }

    struct doops_io_event *next_io_ev;
    while (loop->io_events) {
        next_io_ev = loop->io_events->next;
        DOOPS_FREE(loop->io_events);
        loop->io_events = next_io_ev;
    }
}

static void loop_run(struct doops_loop *loop) {
    if (!loop)
        return;

    int sleep_val;
    while ((loop->events) && (!loop->quit)) {
        int loops = _private_loop_iterate(loop, &sleep_val);
        if ((sleep_val > 0) && (!loops) && (loop->idle) && (loop->idle(loop)))
            break;
#ifdef _WIN32
        Sleep(sleep_val);
#else
        usleep(sleep_val * 1000);
#endif
    }
    _private_loop_remove_events(loop);
    loop->quit = 1;
}

static void loop_free(struct doops_loop *loop) {
    struct doops_event *next_ev;
    if (loop) {
        _private_loop_remove_events(loop);
        DOOPS_FREE(loop);
    }
}

#ifdef __cplusplus
}
#endif

#endif
