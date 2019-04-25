#include "doops.h"
#include <stdio.h>

int some_event(struct doops_loop *loop, void *userdata) {
    fprintf(stdout, "*");
    // loop_quit(loop);
    return 1;
}

int some_event2(struct doops_loop *loop, void *userdata) {
    fprintf(stdout, "10 seconds");
    return 0;
}

int main() {
    struct doops_loop loop;
    loop_init(&loop);

    loop_add(&loop, some_event, 1000, NULL);
    loop_add(&loop, some_event2, 10000, NULL);

    loop_run(&loop);

    return 0;
}
