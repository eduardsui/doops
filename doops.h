#ifndef __DOOPS_H
#define __DOOPS_H

#include <time.h>
#include <stdlib.h>
#include <errno.h>
#include <inttypes.h>
#include <string.h>

#ifdef _WIN32
    #define WITH_SELECT
#else
#ifdef __linux__
    #define WITH_EPOLL
#else
#if defined(__MACH__) || defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
    #define WITH_KQUEUE
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
    #include <sys/types.h>
    #include <sys/event.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define DOOPS_MAX_SLEEP     500
#define DOOPS_MAX_EVENTS    1024

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

#ifdef __clang__
    #define WITH_BLOCKS

    #include <Block.h>

    #define loop_code_data(loop_ptr, code, interval, userdata_ptr) { \
        loop_add_block(loop_ptr, ^(struct doops_loop *loop, void *userdata) { \
            code; \
            return 0; \
        }, interval, userdata_ptr); \
    }

    #define loop_on_read(loop_ptr, code) { \
        if (loop_ptr) { \
            (loop_ptr)->io_read = NULL; \
            if ((loop_ptr)->io_read_block) \
                Block_release((loop_ptr)->io_read_block); \
            (loop_ptr)->io_read_block = Block_copy(^(struct doops_loop *loop, int fd) { code; }); \
        } \
    }

    #define loop_on_write(loop_ptr, code) { \
        if (loop_ptr) { \
            (loop_ptr)->io_write = NULL; \
            if ((loop_ptr)->io_write_block) \
                Block_release((loop_ptr)->io_write_block); \
            (loop_ptr)->io_write_block = Block_copy(^(struct doops_loop *loop, int fd) { code; }); \
        } \
    }

    #define LOOP_IS_READABLE(loop) ((loop->io_read) || (loop->io_read_block))
    #define LOOP_IS_WRITABLE(loop) ((loop->io_write) || (loop->io_write_block))
#else
    #define LOOP_IS_READABLE(loop) (loop->io_read)
    #define LOOP_IS_WRITABLE(loop) (loop->io_write)

    #ifdef __GNUC__
        #define PRIVATE_LOOP_MAKE_ANON_FUNCTION(x, y) private_lambda_call_ ## x ## y
        #define PRIVATE_LOOP_MAKE_ANON_FUNCTION_NAME(x, y) PRIVATE_LOOP_MAKE_ANON_FUNCTION(x, y)

        #define loop_code_data(loop_ptr, code, interval, userdata_ptr) { \
            int PRIVATE_LOOP_MAKE_ANON_FUNCTION_NAME(__func__, __LINE__) (struct doops_loop *loop, void *userdata) { \
                code; \
                return 0; \
            }; \
            loop_add(loop_ptr, PRIVATE_LOOP_MAKE_ANON_FUNCTION_NAME(__func__, __LINE__), interval, userdata_ptr); \
        }

        #define loop_on_read(loop_ptr, code) { \
            void PRIVATE_LOOP_MAKE_ANON_FUNCTION_NAME(__func__, __LINE__) (struct doops_loop *loop, int fd) { \
                code; \
            }; \
            if (loop_ptr) \
                (loop_ptr)->io_read = PRIVATE_LOOP_MAKE_ANON_FUNCTION_NAME(__func__, __LINE__); \
        }

        #define loop_on_write(loop_ptr, code) { \
            void PRIVATE_LOOP_MAKE_ANON_FUNCTION_NAME(__func__, __LINE__) (struct doops_loop *loop, int fd) { \
                code; \
            }; \
            if (loop_ptr) \
                (loop_ptr)->io_write = PRIVATE_LOOP_MAKE_ANON_FUNCTION_NAME(__func__, __LINE__); \
        }
    #else
        #pragma message ( "Code blocks are not supported by your compiler" )
    #endif
#endif

#define loop_code(loop_ptr, code, interval) loop_code_data(loop_ptr, code, interval, NULL);
#define loop_schedule                       loop_code

typedef int (*doop_callback)(struct doops_loop *loop, void *userdata);
typedef int (*doop_idle_callback)(struct doops_loop *loop);
typedef void (*doop_io_callback)(struct doops_loop *loop, int fd);

#ifdef WITH_BLOCKS
    typedef int (^doop_callback_block)(struct doops_loop *loop, void *userdata);
    typedef void (^doop_io_callback_block)(struct doops_loop *loop, int fd);
#endif

struct doops_event {
    doop_callback event_callback;
#ifdef WITH_BLOCKS
    doop_callback_block event_block;
#endif
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
#ifdef WITH_BLOCKS
    doop_io_callback_block io_read_block;
    doop_io_callback_block io_write_block;
#endif
#if defined(WITH_EPOLL) || defined(WITH_KQUEUE)
    int poll_fd;
#else
    int max_fd;
    // fallback to select
    fd_set inlist;
    fd_set outlist;
    fd_set exceptlist;
#endif
    int event_fd;
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
#ifdef WITH_BLOCKS
    event_callback->event_block = NULL;
#endif
    event_callback->interval = interval;
    event_callback->when = milliseconds() + interval;
    event_callback->user_data = user_data;
    event_callback->next = loop->events;

    loop->events = event_callback;
    return 0;
}

#ifdef WITH_BLOCKS
static int loop_add_block(struct doops_loop *loop, doop_callback_block callback, uint64_t interval, void *user_data) {
    if ((!callback) || (!loop)) {
        errno = EINVAL;
        return -1;
    }

    struct doops_event *event_callback = (struct doops_event *)DOOPS_MALLOC(sizeof(struct doops_event));
    if (!event_callback) {
        errno = ENOMEM;
        return -1;
    }

    event_callback->event_callback = NULL;
    event_callback->event_block = Block_copy(callback);
    event_callback->interval = interval;
    event_callback->when = milliseconds() + interval;
    event_callback->user_data = user_data;
    event_callback->next = loop->events;

    loop->events = event_callback;
    return 0;
}
#endif
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

    int err = epoll_ctl (loop->poll_fd, EPOLL_CTL_ADD, fd, &event);
    if ((err) && (errno == EEXIST))
        err = epoll_ctl (loop->poll_fd, EPOLL_CTL_MOD, fd, &event);
    if (err)
        return -1;
#else
#ifdef WITH_KQUEUE
    struct kevent events[2];
    int num_events = 1;
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
                int remove_event = 1;
#ifdef WITH_BLOCKS
                if (ev->event_block)
                    remove_event = ev->event_block(loop, ev->user_data);
                else
#endif
                if (ev->event_callback)
                    remove_event = ev->event_callback(loop, ev->user_data);

                if (remove_event) {
#ifdef WITH_BLOCKS
                    if (ev->event_block)
                        Block_release(ev->event_block);
#endif
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
#ifdef WITH_BLOCK
    if (loop->io_read_block) {
        Block_release(loop->io_read_block);
        loop->io_read_block = NULL;
    }
    if (loop->io_write_block) {
        Block_release(loop->io_write_block);
        loop->io_write_block = NULL;
    }
#endif
    return 0;
}

static void _private_loop_remove_events(struct doops_loop *loop) {
    struct doops_event *next_ev;
    while (loop->events) {
        next_ev = loop->events->next;
#ifdef WITH_BLOCKS
        if ((loop->events) && (loop->events->event_block))
            Block_release(loop->events->event_block);
#endif
        DOOPS_FREE(loop->events);
        loop->events = next_ev;
    }
}

static void _private_sleep(struct doops_loop *loop, int sleep_val) {
    if (!loop)
        return;

#ifdef WITH_EPOLL
    if ((loop->poll_fd > 0) && ((LOOP_IS_READABLE(loop)) || (LOOP_IS_WRITABLE(loop)))) {
        struct epoll_event events[DOOPS_MAX_EVENTS];
        int nfds = epoll_wait(loop->poll_fd, events, DOOPS_MAX_EVENTS, sleep_val);
        int i;
        for (i = 0; i < nfds; i ++) {
            if (LOOP_IS_WRITABLE(loop)) {
                if (events[i].events & EPOLLOUT) {
                    loop->event_fd = events[i].data.fd;
#ifdef WITH_BLOCKS
                    if (loop->io_write_block)
                        loop->io_write_block(loop, events[i].data.fd);
                    else
#endif
                    loop->io_write(loop, events[i].data.fd);
                }
            }
            if (LOOP_IS_READABLE(loop)) {
                if (events[i].events ^ EPOLLOUT) {
                    loop->event_fd = events[i].data.fd;
#ifdef WITH_BLOCKS
                    if (loop->io_read_block)
                        loop->io_read_block(loop, events[i].data.fd);
                    else
#endif
                    loop->io_read(loop, events[i].data.fd);
                }
            }
        }
    }
#else
#ifdef WITH_KQUEUE
    if ((loop->poll_fd > 0) && ((LOOP_IS_READABLE(loop)) || (LOOP_IS_WRITABLE(loop)))) {
        struct kevent events[DOOPS_MAX_EVENTS];
        struct timespec timeout_spec;
        if (sleep_val > 0) {
            timeout_spec.tv_sec = sleep_val / 1000;
            timeout_spec.tv_nsec = (sleep_val % 1000) * 1000;
        }
        int events_count = kevent(loop->poll_fd, NULL, 0, events, DOOPS_MAX_EVENTS, (sleep_val > 0) ? &timeout_spec : NULL);
        int i;
        for (i = 0; i < events_count; i ++) {
            if (LOOP_IS_WRITABLE(loop)) {
                if (events[i].filter == EVFILT_WRITE) {
                    loop->event_fd = events[i].ident;
#ifdef WITH_BLOCKS
                    if (loop->io_write_block)
                        loop->io_write_block(loop, events[i].ident);
                    else
#endif
                    loop->io_write(loop, events[i].ident);
                }
            }
            if (LOOP_IS_READABLE(loop)) {
                if (events[i].filter != EVFILT_WRITE) {
                    loop->event_fd = events[i].ident;
#ifdef WITH_BLOCKS
                    if (loop->io_read_block)
                        loop->io_read_block(loop, events[i].ident);
                    else
#endif
                    loop->io_read(loop, events[i].ident);
                }
            }
        }
    }
#else
    if ((loop->max_fd) && ((LOOP_IS_READABLE(loop)) || (LOOP_IS_WRITABLE(loop)))) {
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
                if (LOOP_IS_READABLE(loop)) {
                    if ((FD_ISSET(i, &inlist)) || (FD_ISSET(i, &exceptlist))) {
                        loop->event_fd = i;
#ifdef WITH_BLOCKS
                        if (loop->io_read_block)
                            loop->io_read_block(loop, i);
                        else
#endif
                        loop->io_read(loop, i);
                    }
                }
                if (LOOP_IS_WRITABLE(loop)) {
                    if (FD_ISSET(i, &outlist)) {
                        loop->event_fd = i;
#ifdef WITH_BLOCKS
                        if (loop->io_write_block)
                            loop->io_write_block(loop, i);
                        else
#endif
                        loop->io_write(loop, i);
                    }
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
        loop->event_fd = -1;
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
#ifdef WITH_BLOCKS
        if (loop->io_read_block) {
            Block_release(loop->io_read_block);
            loop->io_read_block = NULL;
        }
        if (loop->io_write_block) {
            Block_release(loop->io_write_block);
            loop->io_write_block = NULL;
        }
#endif
        DOOPS_FREE(loop);
    }
}

static int loop_event_socket(struct doops_loop *loop) {
    if (loop)
        return loop->event_fd;
    return -1;
}

#ifdef __cplusplus
}
#endif

#endif
