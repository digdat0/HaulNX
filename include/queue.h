#ifndef QUEUE_H
#define QUEUE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define QUEUE_MAX 64

typedef enum {
    Q_FREE = 0,    /* empty slot */
    Q_QUEUED,      /* waiting to start */
    Q_DOWNLOADING, /* transferring */
    Q_VERIFYING,   /* checking md5 */
    Q_EXTRACTING,  /* unpacking archive */
    Q_DONE,        /* finished OK */
    Q_SAVED,       /* downloaded but couldn't extract; raw archive kept */
    Q_FAILED,      /* error */
    Q_CANCELLED    /* user cancelled */
} QStatus;

typedef struct {
    char url[1024];
    char name[512];
    char target[64]; /* console folder under tico/roms */
    char auth[320];  /* optional archive.org S3 auth header */
    char md5[33];    /* expected MD5 hex from metadata, "" if unknown */
    uint64_t size;   /* expected file size from metadata (0 if unknown) */
    bool is_archive;
    uint32_t seq; /* insertion order, for FIFO */
    volatile QStatus status;
    volatile uint64_t now;
    volatile uint64_t total;
    volatile uint64_t speed; /* bytes/sec, while downloading */
    volatile bool cancel;
    long http_code;
    char fail_reason[24]; /* short reason shown on a failed item, e.g. "HTTP 404" */
    int overwrote;        /* # existing files this install replaced (0 = all new) */
} QueueItem;

/* A snapshot entry: a copy of the item plus its stable slot index. */
typedef struct {
    QueueItem item;
    int slot;
} QueueView;

/* Start/stop the background worker thread. Call after net_init / before net_exit.
 * roms_root is the base ROM directory (e.g. "sdmc:/tico/roms"); the pointer must
 * remain valid for the lifetime of the queue. */
void queue_init(const char *roms_root);
void queue_exit(void);

/* Enqueue a download. Returns false if the queue is full. md5 may be "" or NULL
 * when no checksum is known. */
bool queue_add(const char *url, const char *name, const char *target,
               const char *auth, uint64_t size, bool is_archive,
               const char *md5);

/* Copy current items (sorted FIFO) into out (size max). Returns count. */
int queue_snapshot(QueueView *out, int max);

/* Cancel a queued/active item by its slot index. */
void queue_cancel(int slot);

/* Re-queue a failed/cancelled item (by slot) to run again in its current list
 * position, resuming from any .part already on disk. No-op for other states. */
void queue_retry(int slot);

/* Move an item one row up (dir=-1) or down (dir=+1) in the list. The active
 * download can't be moved and nothing can move above it. Returns true if the
 * order actually changed. */
bool queue_move(int slot, int dir);

/* If a download/verify/extract is currently in progress, fill the out params
 * with its summary and return true; otherwise return false. `index` gets the
 * 1-based position of the active item among all queued items (by FIFO order) and
 * `count` the total number of items in the queue. Any out pointer may be NULL. */
bool queue_active_info(char *name, size_t name_sz, QStatus *status,
                       uint64_t *now, uint64_t *total, uint64_t *speed,
                       int *index, int *count);

/* Remove all finished/failed/cancelled items. */
void queue_clear_finished(void);

/* Number of items still pending or in progress (for sleep-prevention). */
int queue_active_count(void);

#ifdef __cplusplus
}
#endif

#endif /* QUEUE_H */
