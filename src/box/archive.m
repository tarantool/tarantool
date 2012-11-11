#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <sys/stat.h> 
#include <fcntl.h>
#include <time.h>
#include <dirent.h>
#include <unistd.h>
#include "archive.h"
#include "fio.h"
#include "log_io.h"
#include "fiber.h"
#include "txn.h"
#include "box.h"
#include "tuple.h"
#include "request.h"
#include "space.h"
#include "tarantool_pthread.h"

#ifndef ARC_NAME_MAX_LEN
#define ARC_NAME_MAX_LEN 64
#endif
#ifndef ARC_BATCH_MAX_ROWS
#define ARC_BATCH_MAX_ROWS 1024
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
    double fsync_deplay;
    ev_tstamp last_fsync;
    int not_synced_rows;

    double xlog_tm;
    bool is_save_recovery;
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

bool is_any_archive_exists() {
    struct log_dir *dir = archive_state->arc_dir;
    DIR *dh = opendir(dir->dirname);
    if(dh == NULL) {
        panic("Can't open archive dir `%s`",dir->dirname);
    }
    bool result = false;
    struct dirent *dent;
    while ((dent = readdir(dh)) != NULL) {
        char *ext = strrchr(dent->d_name, '.');
        if(ext!=NULL && strcmp(ext, dir->filename_ext) == 0 ) {
            result = true;
            break;
        }
    }
    if (dh != NULL) {
        closedir(dh);
    }
    return result;
}


void arc_init(const char *arc_dirname,const char *arc_filename_patter, double fsync_delay) {
    assert(archive_state == NULL);
    if(arc_dirname != NULL) {
        archive_state = p0alloc(eter_pool, sizeof(struct arc_state));
        archive_state->arc_dir = &arc_dir;
        archive_state->filename_format = strdup(arc_filename_patter);
        archive_state->fsync_deplay = fsync_delay;
        archive_state->arc_dir->dirname = strdup(arc_dirname);
        archive_state->batch = fio_batch_alloc(sysconf(_SC_IOV_MAX));
        fio_batch_start(archive_state->batch,ARC_BATCH_MAX_ROWS);
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
        if(!is_any_archive_exists()) {
            archive_state -> is_save_recovery = true;
        }
    }
}

char* arc_get_filename_for_time(double action_time) {
    static __thread char filename[PATH_MAX + 1];
    static __thread char name[ARC_NAME_MAX_LEN + 1];
    memset(name, 0, PATH_MAX+1);
    memset(filename, 0, PATH_MAX+1);
    time_t time=(time_t)action_time;
    struct tm* tm = localtime (&time);
    strftime(name,ARC_NAME_MAX_LEN,archive_state->filename_format,tm);

    snprintf(filename, PATH_MAX, "%s/%s-latest%s",
             archive_state->arc_dir->dirname, name,archive_state->arc_dir->filename_ext);
    return filename;
}

struct log_io *arc_create_io_from_file(struct log_dir *dir,const char *filename, FILE *file) {
    struct log_io *l = NULL;
    l = calloc(1, sizeof(*l));
    l->f = file;
    strncpy(l->filename, filename, PATH_MAX);
    l->dir = dir;
    l->mode = LOG_WRITE;
    return l;
}

void arc_new_io(char* filename) {
    char newfilename[PATH_MAX + 1];
    char newsuffix[PATH_MAX + 1];
    memset(newfilename, 0, PATH_MAX+1);
    memset(newsuffix, 0, PATH_MAX+1);
    archive_state->not_synced_rows = 0;
    archive_state->last_fsync = ev_now();
    say_info("creating new archive file `%s'", filename);
    if(archive_state ->current_io!=NULL) {
        log_io_close(&archive_state->current_io);
    }
    bool file_exists = (access(filename,W_OK)==0);
    if(file_exists) {
        say_info("archive with name `%s` found, trying to rename",filename);
        for(int i=0;i<1000;i++) {
            size_t n=strrchr(filename,'-') - filename;
            strncpy(newfilename,filename,n);
            int len=snprintf(newsuffix,PATH_MAX,"-%04d%s",i,archive_state->arc_dir->filename_ext);
            strncpy(newfilename+n,newsuffix, len);
           if(access(newfilename,W_OK)==-1) {
               rename(filename,newfilename);
               say_info("renaming old one to `%s`",newfilename);
               break;
           }
        }
    }
    int fd = open(filename, O_CREAT |  O_APPEND | O_WRONLY | archive_state -> arc_dir->open_wflags, 0664);
    if (fd > 0) {
        FILE *f = fdopen(fd, "a");
        archive_state -> current_io = arc_create_io_from_file(archive_state -> arc_dir, filename, f);
        log_io_write_header(archive_state->current_io);
        fflush(archive_state->current_io->f);
    } else {
        say_syserror("%s: failed to open `%s'", __func__, filename);
    }
}





void arc_free() {
    if(archive_state!=NULL) {
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
}

struct arc_write_request* arc_create_write_request(u32 space, u64 cookie, struct tuple *tuple,double tm) {
    struct box_snap_row metadata;
    metadata.space = space;
    metadata.tuple_size = tuple->field_count;
    metadata.data_size = tuple->bsize;
    int row_size = sizeof ( struct row_v11 ) + sizeof ( struct box_snap_row ) + tuple->bsize;
    struct arc_write_request *request =
            palloc(fiber->gc_pool, sizeof(struct arc_write_request) +
                   row_size);
    struct row_v11 *row = &request->row;
    request->callback_fiber = fiber;
    request->res = -1;
    row_v11_fill ( row, 0, ARCH, cookie, &metadata, sizeof ( struct box_snap_row ), tuple->data, tuple->bsize );
    if(tm > 0) {
        row->header.tm = tm;
    }
    header_v11_sign ( &row->header );
    return request;
}

int arc_schedule_write( u32 space, u64 cookie, struct tuple *tuple) {
    struct arc_write_request* request=arc_create_write_request(space,cookie,tuple,0.0);
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
void arc_flush_batch() {
    struct fio_batch *batch = archive_state->batch;
    if(batch->rows > 0) {
        fio_batch_write(batch,fileno(archive_state->current_io->f));
        fio_batch_start(batch,ARC_BATCH_MAX_ROWS);
    }

}

void arc_batch_write(struct arc_write_request *request,bool is_flush_after) {
    static __thread char current_filename[PATH_MAX + 1];
    struct fio_batch *batch = archive_state->batch;
    char* required_filename = arc_get_filename_for_time(request->row.header.tm);
    if(strcmp(current_filename,required_filename) != 0) {
        arc_flush_batch();
        arc_new_io(required_filename);
        strncpy(current_filename,required_filename,PATH_MAX+1);
    }
    fio_batch_add(batch,&request->row,row_v11_size(&request->row));
    request->res = 0;
    if(fio_batch_is_full(batch) || is_flush_after) {
        archive_state->not_synced_rows+=batch->rows;
        arc_flush_batch();
        if(archive_state->fsync_deplay == 0 ) {
            log_io_sync(archive_state -> current_io);
            archive_state->not_synced_rows = 0;
        }
    }
}


void arc_write(u32 space, u64 cookie, struct tuple *tuple,double tm) {
    struct arc_write_request* request=arc_create_write_request(space,cookie,tuple,tm);
    arc_batch_write(request,false);
}

void arc_do_txn(struct txn *txn) {
    if(archive_state != NULL) {
        if(txn->op == DELETE && txn->old_tuple) {
            if(archive_state -> is_started) {
                arc_schedule_write(txn->space->no,0,txn->old_tuple);
            } else if(archive_state->is_save_recovery) {
                arc_write(txn->space->no,0,txn->old_tuple,archive_state->xlog_tm);
                archive_state->xlog_tm = 0;
            }
        }
    }
}

void arc_save_real_tm(double tm) {
    if(archive_state!=NULL) {
        archive_state->xlog_tm = tm;
    }
}

bool arc_read_input(struct arc_state *arc_state,struct arc_fifo *queue) {
    static __thread struct timespec timeout;
    bool result = false;
    tt_pthread_mutex_lock(&arc_state->mutex);
    while(!arc_state -> is_shutdown) {
        if(arc_state->not_synced_rows > 0 && arc_state->fsync_deplay > 0 && arc_state->current_io!=NULL && (ev_time() - arc_state -> last_fsync >= arc_state->fsync_deplay - 0.01)) {
            log_io_sync(arc_state->current_io);
            arc_state->last_fsync = ev_now();
            arc_state->not_synced_rows = 0;
        }
        if(!STAILQ_EMPTY(&arc_state->input_queue)) {
            STAILQ_CONCAT(queue,&arc_state->input_queue);
            result = true;
            break;
        } else {
            if(arc_state->not_synced_rows > 0 && arc_state -> fsync_deplay > 0 && arc_state->current_io != NULL) {
                double next_sync = arc_state->last_fsync + archive_state->fsync_deplay;
                timeout.tv_sec = (time_t)next_sync;
                timeout.tv_nsec = (u64)((next_sync - (double)timeout.tv_sec)*1000000000.0);
                tt_pthread_cond_timedwait(&arc_state->queue_not_empty, &arc_state->mutex,&timeout);
            } else {
                tt_pthread_cond_wait(&arc_state->queue_not_empty, &arc_state->mutex);
            }
        }
    }
    (void) tt_pthread_mutex_unlock(&arc_state->mutex);
    return result;
}



void *arc_writer_thread(void *args) {
    struct arc_state *arc_state = args;
    struct arc_fifo queue = STAILQ_HEAD_INITIALIZER(queue);

    while (arc_read_input(arc_state,&queue)) {
        struct arc_write_request *request = STAILQ_FIRST(&queue);
        do {
            bool is_last_item = STAILQ_NEXT(request,fifo_entry) == NULL;
            arc_batch_write(request,is_last_item);
            request=STAILQ_NEXT(request,fifo_entry);
        } while(request != NULL);
        tt_pthread_mutex_lock(&arc_state->mutex);
        STAILQ_CONCAT(&arc_state->processed_queue, &queue);
        tt_pthread_mutex_unlock(&arc_state->mutex);
        ev_async_send(&arc_state->wakeup_fibers);
    }
    if (arc_state->current_io != NULL)
        if(arc_state->batch->rows > 0) {
            arc_flush_batch();
        }
    log_io_close(&arc_state->current_io);

    return NULL;
}

int arc_start() {
    if(archive_state != NULL) {
        if(!archive_state -> is_started) {
            ev_async_start(&archive_state->wakeup_fibers);
            if (tt_pthread_create(&archive_state->worker_thread, NULL, &arc_writer_thread,archive_state)) {
                return -1;
            }
            archive_state -> is_started = true;
        }
        if(archive_state->batch->rows > 0) {
            arc_flush_batch(archive_state->batch);
        }
        archive_state->is_save_recovery = false;
    }
    return 0;
}

