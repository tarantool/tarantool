#include <setjmp.h>
#include <pthread.h>

struct __pthread_cleanup_frame;
extern __inline void
__pthread_cleanup_routine(struct __pthread_cleanup_frame *__frame);

int main()
{
    return 0;
}

