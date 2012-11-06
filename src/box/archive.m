#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <sys/stat.h> 
#include <fcntl.h>
#include <time.h>
#include "archive.h"
#include "fio.h"
#include "log_io.h"
#include "fiber.h"
#include "request.h"
#include "space.h"
#include "tarantool_pthread.h"

#ifndef ARC_NAME_MAX_LEN
#define ARC_NAME_MAX_LEN 64
#endif

struct arc_write_request {
    STAILQ_ENTRY(arc_write_request) fifo_entry;
    struct fiber *callback_fiber;
    int res;
    struct row_v11 row;
};

STAILQ_HEAD(arc_fifo, arc_write_request);

struct arc_state {
    struct log_io *current_io;
    struct log_dir *arc_dir;

    char *filename_format;

    pthread_t worker_thread;
    pthread_cond_t queue_not_empty;

    pthread_mutex_t mutex;

    struct arc_fifo input_queue;

    ev_async wakeup_fibers;
    struct arc_fifo processed_queue;




    bool is_shutdown;
    bool is_started;

    struct fio_batch *batch;
};



struct archive_metadata {
    u32 space;
};

struct log_dir arc_dir = {
    .filetype = "ARCH\n",
    .filename_ext = ".arch"
};


struct arc_state *archive_state;








void arc_wakeup_fibers(ev_watcher *watcher, int event __attribute__((unused))) {
    struct arc_state *arc_state = watcher->data;
    struct arc_fifo queue = STAILQ_HEAD_INITIALIZER(queue);
    tt_pthread_mutex_lock(&arc_state->mutex);
    STAILQ_CONCAT(&queue,&arc_state->processed_queue);
    tt_pthread_mutex_unlock(&arc_state->mutex);
    struct arc_write_request *request = STAILQ_FIRST(&queue);
    do {
        fiber_call(request->callback_fiber);
        request = STAILQ_NEXT(request,fifo_entry);
    } while(request!=NULL);


}


void arc_init(const char *arc_dirname,const char *arc_filename_patter) {
    assert(archive_state == NULL);

    archive_state = p0alloc(eter_pool, sizeof(struct arc_state));
    archive_state->arc_dir = &arc_dir;
    archive_state->filename_format = strdup(arc_filename_patter);
    archive_state->arc_dir->dirname = strdup(arc_dirname);
    archive_state->batch = fio_batch_alloc(sysconf(_SC_IOV_MAX));
    if(archive_state->batch == NULL) {
        panic_syserror("fio_batch_alloc");
    }

    pthread_mutexattr_t errorcheck;

    (void) tt_pthread_mutexattr_init(&errorcheck);

    (void) tt_pthread_mutex_init(&archive_state->mutex, &errorcheck);
    (void) tt_pthread_cond_init(&archive_state->queue_not_empty,NULL);

    (void) tt_pthread_mutexattr_destroy(&errorcheck);

    ev_async_init(&archive_state->wakeup_fibers, (void *)arc_wakeup_fibers);
    archive_state->wakeup_fibers.data = archive_state;

    STAILQ_INIT(&archive_state->input_queue);
    STAILQ_INIT(&archive_state->processed_queue);
}

char* get_filename_for_time(double action_time) {
    static __thread char filename[PATH_MAX + 1];
    static __thread char name[ARC_NAME_MAX_LEN + 1];
    time_t time=(time_t)action_time;
    struct tm* tm = localtime (&time);
    strftime(name,ARC_NAME_MAX_LEN,archive_state->filename_format,tm);

    snprintf(filename, PATH_MAX, "%s/%s%s",
             archive_state->arc_dir->dirname, name,archive_state->arc_dir->filename_ext);
    return filename;
}

void create_new_io(char* filename) {
    say_info("creating new archive file `%s'", filename);
    if(archive_state ->current_io!=NULL) {
        log_io_close(&archive_state->current_io);
    }

    int fd = open(filename,
                  O_WRONLY | O_CREAT | archive_state -> arc_dir->open_wflags, 0664);
    if (fd > 0) {
        FILE *f = fdopen(fd, "w");
        archive_state -> current_io = log_io_open(archive_state -> arc_dir, LOG_WRITE, filename, NONE, f);
    } else {
        say_syserror("%s: failed to open `%s'", __func__, filename);
    }


}

int arc_start() {
    ev_async_start(&archive_state->wakeup_fibers);
    if (tt_pthread_create(&archive_state->worker_thread, NULL, &arc_writer_thread,archive_state)) {
        return -1;
    }
    archive_state -> is_started = true;
    return 0;
}

void arc_free() {
    say_info("stopping archive module");
    tt_pthread_mutex_lock(&archive_state->mutex);
    archive_state -> is_shutdown = true;
    tt_pthread_cond_signal(&archive_state->queue_not_empty);
    tt_pthread_mutex_unlock(&archive_state->mutex);
    if (tt_pthread_join(archive_state->worker_thread, NULL) != 0) {
        panic_syserror("Archive: thread join failed");
    }
    tt_pthread_mutex_destroy(&archive_state->mutex);
    tt_pthread_cond_destroy(&archive_state->queue_not_empty);
    free(archive_state->batch);
    ev_async_stop(&archive_state->wakeup_fibers);
}

int arc_write ( u32 space, u64 cookie, struct tuple *tuple) {

    struct archive_metadata metadata;
    metadata.space = space;
    int row_size = sizeof ( struct row_v11 ) + sizeof ( struct archive_metadata ) + tuple->bsize;
    struct arc_write_request *request =
            palloc(fiber->gc_pool, sizeof(struct arc_write_request) +
                   row_size);
    struct row_v11 *row = &request->row;
    request->callback_fiber = fiber;
    request->res = -1;

    row_v11_fill ( row, 0, ARCH, cookie, &metadata, sizeof ( struct archive_metadata ), tuple->data, tuple->bsize );
    header_v11_sign ( &row->header );

    tt_pthread_mutex_lock(&archive_state->mutex);
    bool input_was_empty = STAILQ_EMPTY(&archive_state->input_queue);
    //create request here
    STAILQ_INSERT_TAIL(&archive_state->input_queue, request, fifo_entry);
    if (input_was_empty) {
        tt_pthread_cond_signal(&archive_state->queue_not_empty);
    }
    tt_pthread_mutex_unlock(&archive_state->mutex);

    fiber_yield();
    return request->res;
}

void arc_do_txn(struct txn *txn) {
    if(archive_state -> is_started && txn->op == DELETE && txn->old_tuple) {
        arc_write(txn->space->no,0,txn->old_tuple);
    }
}

bool arc_read_input(struct arc_state *arc_state,struct arc_fifo *queue) {
    bool result = false;
    tt_pthread_mutex_lock(&arc_state->mutex);
    while(!archive_state -> is_shutdown) {
        if(!STAILQ_EMPTY(&arc_state->input_queue)) {
            STAILQ_CONCAT(queue,&arc_state->input_queue);
            result = true;
            break;
        } else {
            tt_pthread_cond_wait(&arc_state->queue_not_empty, &arc_state->mutex);
        }
    }
    (void) tt_pthread_mutex_unlock(&arc_state->mutex);
    return result;
}



void *arc_writer_thread(void *args) {
    static __thread char current_filename[PATH_MAX + 1];
    struct arc_state *arc_state = args;
    struct arc_fifo queue = STAILQ_HEAD_INITIALIZER(queue);
    struct fio_batch *batch = arc_state->batch;
    long max_rows = 1024;
    fio_batch_start(batch, max_rows);

    while (arc_read_input(arc_state,&queue)) {
        struct arc_write_request *request = STAILQ_FIRST(&queue);
        do {
            char* required_filename = get_filename_for_time(request->row.header.tm);
            if(strcmp(current_filename,required_filename) != 0) {
                if(batch->rows > 0) {
                    fio_batch_write(batch,fileno(arc_state->current_io->f)),
                    fio_batch_start(batch,max_rows);
                }
                create_new_io(required_filename);
                strcpy(current_filename,required_filename);
            }
            fio_batch_add(batch,&request->row,row_v11_size(&request->row));
            request->res = 0;
            if(fio_batch_is_full(batch) || STAILQ_NEXT(request,fifo_entry)==NULL) {
                fio_batch_write(batch,fileno(arc_state->current_io->f));
                fio_batch_start(batch,max_rows);
            }
            request=STAILQ_NEXT(request,fifo_entry);
        } while(request != NULL);

        tt_pthread_mutex_lock(&arc_state->mutex);
        STAILQ_CONCAT(&arc_state->processed_queue, &queue);
        tt_pthread_mutex_unlock(&arc_state->mutex);
        ev_async_send(&arc_state->wakeup_fibers);

    }


    if (arc_state->current_io != NULL)
        log_io_close(&arc_state->current_io);

    return NULL;
}


