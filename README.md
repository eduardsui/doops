# doops
Single C header file event loop.

Doops is an event loop designed to provide basic message dispatch and async I/O. For now it includes a timer and socket/file descriptor polling via select/poll/epoll/kqueue.

Compiling
----------

gcc:\
  `gcc example.c`

gcc(windows):\
  `gcc example.c -lws2_32`

clang:\
  `clang -fblocks example.c -lBlocksRuntime`

clang(windows):\
  `clang -fblocks example.c -lBlocksRuntime -lws2_32`

clang(OS X):\
  `clang -fblocks example.c`

**Important note**: for any other compiler than gcc and clang, blocks will not be available. Function pointers should be used instead.

Hello world
----------
Minimal code:
```
#include "doops.h"

int main() {
    // create loop
    struct doops_loop *loop = loop_new();

    int times = 0;

    // schedule event every second (1000ms)
    loop_schedule(loop, {
        times ++;
        printf("Hello world! (%i)\n", times);
        // remove event
        if (times == 10)
            return 1;
    }, 1000);

    // run the loop
    loop_run(loop);

    loop_free(loop);
    return 0;
}
```
