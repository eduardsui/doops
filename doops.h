#ifndef __DOOPS_H
#define __DOOPS_H

#include <time.h>
#include <stdlib.h>
#include <errno.h>
#include <inttypes.h>
#include <string.h>

#ifdef _WIN32
    #define WITH_SELECT
    #pragma message ( "Building with SELECT" )
#else
#ifdef __linux__
    #define WITH_EPOLL
    #pragma message ( "Building with EPOLL" )
#else
#if defined(__MACH__) || defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
    #define WITH_KQUEUE
    #pragma message ( "Building with KQUEUE" )
#else
    #pragma message ( "WARNING: Cannot determine operating system. Falling back to select." )
    #define WITH_SELECT
#endif
#endif
#endif

#ifdef WITH_EPOLL
    #include <sys/epoll.h>
#endif
#ifdef WITH_KQUEUE
    #include <sys/event.h>
#endif

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

#define DOOPS_READ      0
#define DOOPS_READWRITE 1

struct doops_loop;

typedef int (*doop_callback)(struct doops_loop *loop, void *userdata);
typedef int (*doop_idle_callback)(struct doops_loop *loop);
typedef void (*doop_io_callback)(struct doops_loop *loop, int fd);

struct doops_event {
    doop_callback event_callback;
    uint64_t when;
    uint64_t interval;
    void *user_data;
    struct doops_event *next;
};

struct doops_loop {
    int quit;
    doop_idle_callback idle;
    struct doops_event *events;
    doop_io_callback io_read;
    doop_io_callback io_write;
#if defined(WITH_EPOLL) || defined(WITH_KQUEUE)
    int poll_fd;
#else
    int max_fd;
    // fallback to select
    fd_set inlist;
    fd_set outlist;
    fd_set exceptlist;
#endif
};


static void _private_loop_init_io(struct doops_loop *loop) {
    if (!loop)
        return;

#ifdef WITH_EPOLL
    if (loop->poll_fd <= 0)
        loop->poll_fd = epoll_create1(0);
#else
#ifdef WITH_KQUEUE
    if (loop->poll_fd <= 0)
        loop->poll_fd = kqueue();
#else
    if (!loop->max_fd) {
        FD_ZERO(&loop->inlist);
        FD_ZERO(&loop->outlist);
        FD_ZERO(&loop->exceptlist);
        loop->max_fd = 1;
    }
#endif
#endif
}

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

static int loop_add_io(struct doops_loop *loop, int fd, int mode) {
    if ((fd < 0) || (!loop)) {
        errno = EINVAL;
        return -1;
    }
    _private_loop_init_io(loop);
#ifdef WITH_EPOLL
    struct epoll_event event;
    // supress valgrind warning
    event.data.u64 = 0;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLPRI | EPOLLHUP | EPOLLRDHUP | EPOLLET;
    if (mode)
        event.events |= EPOLLOUT;

    err = epoll_ctl (loop->poll_fd, EPOLL_CTL_ADD, fd, &event);
    if ((err) && (errno == EEXIST))
        err = epoll_ctl (loop->poll_fd, EPOLL_CTL_MOD, fd, &event);
    if (err)
        return -1;
#else
#ifdef WITH_KQUEUE
    struct kevent events[2];
    int nume_vents = 1;
    EV_SET(&events[0], fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, 0);
    if (mode) {
        EV_SET(&events[1], fd, EVFILT_WRITE, EV_ADD | EV_ENABLE, 0, 0, 0);
        num_events = 2;
    }
    return kevent(loop->poll_fd, events, num_events, NULL, 0, NULL);
#else
    FD_SET(fd, &loop->inlist);
    FD_SET(fd, &loop->exceptlist);
    if (mode)
        FD_SET(fd, &loop->outlist);
    if (fd >= loop->max_fd)
        loop->max_fd = fd + 1;
#endif
#endif
    return 0;
}

static int loop_remove_io(struct doops_loop *loop, int fd) {
    if ((fd < 0) || (!loop)) {
        errno = EINVAL;
        return -1;
    }
    _private_loop_init_io(loop);
#ifdef WITH_EPOLL
    struct epoll_event event;
    // supress valgrind warning
    event.data.u64 = 0;
    event.data.fd = fd;
    event.events = 0;
    return epoll_ctl (loop->poll_fd, EPOLL_CTL_DEL, fd, &event);
#else
#ifdef WITH_KQUEUE
    struct kevent event;
    EV_SET(&event, fd, EVFILT_READ, EV_DELETE, 0, 0, 0);
    kevent(loop->poll_fd, &event, 1, NULL, 0, NULL);
    EV_SET(&event, fd, EVFILT_WRITE, EV_DELETE, 0, 0, 0);
    kevent(loop->poll_fd, &event, 1, NULL, 0, NULL);
#else
    FD_CLR(fd, &loop->inlist);
    FD_CLR(fd, &loop->exceptlist);
    FD_CLR(fd, &loop->outlist);
#endif
#endif
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

static int loop_io(struct doops_loop *loop, doop_io_callback read_callback, doop_io_callback write_callback) {
    if (!loop) {
        errno = EINVAL;
        return -1;
    }
    loop->io_read = read_callback;
    loop->io_write = write_callback;
    return 0;
}

static void _private_loop_remove_events(struct doops_loop *loop) {
    struct doops_event *next_ev;
    while (loop->events) {
        next_ev = loop->events->next;
        DOOPS_FREE(loop->events);
        loop->events = next_ev;
    }
}

static void _private_sleep(struct doops_loop *loop, int sleep_val) {
    if (!loop)
        return;

#ifdef WITH_EPOLL
    // to do
#else
#ifdef WITH_KQUEUE
    // to do
#else
    if ((loop->max_fd) && ((loop->io_read) || (loop->io_write))) {
        struct timeval tout;
        tout.tv_sec = 0;
        tout.tv_usec = 0;
        if (sleep_val > 0) {
            tout.tv_sec = sleep_val / 1000;
            tout.tv_usec = (sleep_val % 1000) * 1000;
        }
        fd_set inlist;
        fd_set outlist;
        fd_set exceptlist;

        // fd_set is a struct
        inlist = loop->inlist;
        outlist = loop->outlist;
        exceptlist = loop->exceptlist;
        int err = select(loop->max_fd, &inlist, &outlist, &exceptlist, &tout);
        if (err >= 0) {
            if (!err)
                return;
            int i;
            for (i = 0; i < loop->max_fd; i ++) {
                if (loop->io_read) {
                    if ((FD_ISSET(i, &inlist)) || (FD_ISSET(i, &exceptlist)))
                        loop->io_read(loop, i);
                }
                if (loop->io_write) {
                    if (FD_ISSET(i, &outlist))
                        loop->io_write(loop, i);
                }
            }
        }
    }
#endif
#endif

#ifdef _WIN32
    Sleep(sleep_val);
#else
    usleep(sleep_val * 1000);
#endif
}

static void loop_run(struct doops_loop *loop) {
    if (!loop)
        return;

    int sleep_val;
    while ((loop->events) && (!loop->quit)) {
        int loops = _private_loop_iterate(loop, &sleep_val);
        if ((sleep_val > 0) && (!loops) && (loop->idle) && (loop->idle(loop)))
            break;
        _private_sleep(loop, sleep_val);
    }
    _private_loop_remove_events(loop);
    loop->quit = 1;
}

static void loop_free(struct doops_loop *loop) {
    struct doops_event *next_ev;
    if (loop) {
#if defined(WITH_EPOLL) || defined(WITH_KQUEUE)
        if (loop->poll_fd > 0) {
            close(loop->poll_fd);
            loop->poll_fd = -1;
        }
#else
        FD_ZERO(&loop->inlist);
        FD_ZERO(&loop->outlist);
        FD_ZERO(&loop->exceptlist);
#endif
        _private_loop_remove_events(loop);
        DOOPS_FREE(loop);
    }
}

#ifdef __cplusplus
}
#endif

#endif
