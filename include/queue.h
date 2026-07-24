#ifndef QUEUE_H
#define QUEUE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Queue capacity. Every slot is a fixed-size QueueItem (~2 KB) and the UI keeps
 * a few QueueView snapshot buffers of the same length, so this costs roughly
 * 2 KB * QUEUE_MAX per buffer — ~2.5 MB in total at 256. That is affordable
 * even in applet mode, and 256 is what makes "mark a filtered set and queue it"
 * useful rather than a 64-item tease. */
#define QUEUE_MAX 256

typedef enum {
    Q_FREE = 0,       /* empty slot */
    Q_QUEUED,         /* waiting to start */
    Q_PAUSED,         /* preempted (download limit lowered) or network lost;
                         .part kept, auto-resumes in order when a slot frees
                         up / the connection returns */
    Q_DOWNLOADING,    /* transferring */
    Q_VERIFYING,      /* checking md5 */
    Q_AWAIT_EXTRACT,  /* downloaded + verified, waiting for extract thread */
    Q_EXTRACTING,     /* unpacking archive */
    Q_DONE,           /* finished OK */
    Q_SAVED,          /* downloaded but couldn't extract; raw archive kept */
    Q_FAILED,         /* error */
    Q_CANCELLED       /* user cancelled */
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
    volatile uint64_t speed;   /* bytes/sec, while downloading */
    volatile int ex_files;     /* files extracted so far, while extracting */
    volatile bool cancel;
    volatile bool pause; /* ask the worker to preempt this download (keep .part) */
    /* Passed over this round because the card can't hold what's left of it.
     * In-memory only (never persisted): a smaller item behind it still runs,
     * and the flag is cleared whenever free space could have changed. */
    volatile bool no_space;
    long http_code;
    char fail_reason[24]; /* short reason shown on a failed item, e.g. "HTTP 404" */
    int overwrote;        /* # existing files this install replaced (0 = all new) */
} QueueItem;

/* A snapshot entry: a copy of the item plus its stable slot index. */
typedef struct {
    QueueItem item;
    int slot;
} QueueView;

/* Start/stop the background worker threads. Call after net_init / before net_exit.
 * roms_root is the base ROM directory (e.g. "sdmc:/roms"); the pointer must
 * remain valid for the lifetime of the queue. max_dl is the number of concurrent
 * download threads (1–10, clamped). */
void queue_init(const char *roms_root, int max_dl);
void queue_exit(void);

/* Change the concurrent-download limit (1–10, clamped) at runtime. Takes effect
 * immediately in both directions: raising it starts more queued items; lowering
 * it pauses the newest in-flight downloads (keeping their .part), which resume
 * automatically, in order, as slots free up. */
void queue_set_max_dl(int n);

/* Set the download-rate limits (both in bytes/sec, 0 = unlimited) at runtime.
 * all_bps caps the combined rate of every active download; item_bps caps each
 * one individually. Takes effect on in-flight transfers within a fraction of a
 * second. The global budget is shared fair-share: each active download is capped
 * at min(item_bps, all_bps / active_downloads), recomputed as transfers start
 * and finish, with a small floor so a tiny budget can't stall a transfer. */
void queue_set_rate_limits(int all_bps, int item_bps);

/* Enqueue a download. Returns false if the queue is full. md5 may be "" or NULL
 * when no checksum is known. */
bool queue_add(const char *url, const char *name, const char *target,
               const char *auth, uint64_t size, bool is_archive,
               const char *md5);

/* How many more items queue_add can accept right now. Lets a bulk add tell the
 * user "only 40 of your 500 fit" before it queues anything, instead of stopping
 * halfway with a "queue full" toast. */
int queue_free_slots(void);

/* Total bytes the queue still has to pull. Reads memory only — an item that
 * hasn't started counts its full size even when a resumable .part is already on
 * disk, so the figure can overshoot. Items of unknown size (0) contribute
 * nothing, so it can also undershoot. Good enough for "will this fit?"; the
 * workers do the exact, .part-aware check per item before starting one. */
uint64_t queue_pending_bytes(void);

/* Bracket a run of queue_add calls. Each add otherwise rewrites the whole
 * queue-state file, which turns queueing N items into N growing rewrites;
 * inside a batch the writes collapse into a single save at the end. Calls
 * nest, and every begin must be matched by an end. */
void queue_batch_begin(void);
void queue_batch_end(void);

/* True while the workers are holding off because the SD card is nearly full.
 * Nothing is failed or lost — queued items simply don't start, and the hold
 * lifts on its own once space is freed. Meant for a status line in the UI. */
bool queue_space_hold(void);

/* Free bytes the queue insists on leaving on the card. A download won't start
 * unless free space covers what's left of it plus this margin. */
#define QUEUE_SPACE_RESERVE (256ull * 1024 * 1024)

/* Copy current items (sorted FIFO) into out (size max). Returns count. */
int queue_snapshot(QueueView *out, int max);

/* Cancel a queued/active item by its slot index. */
void queue_cancel(int slot);

/* Re-queue a failed/cancelled item (by slot) to run again in its current list
 * position, resuming from any .part already on disk. No-op for other states. */
void queue_retry(int slot);

/* Re-queue every FAILED item at once (resuming from any .part on disk).
 * Returns how many were re-queued. */
int queue_retry_all(void);

/* Move an item one row up (dir=-1) or down (dir=+1) in the list. The active
 * download can't be moved and nothing can move above it. Returns true if the
 * order actually changed. */
bool queue_move(int slot, int dir);

/* Jump an item to the top of the waiting section (just below any active
 * downloads) when to_bottom is false, or to the very bottom when true. The
 * active download itself can't be moved. Returns true if the order changed. */
bool queue_move_end(int slot, bool to_bottom);

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

/* Count how many queued/active items have a .part file matching `partname`
 * (the bare filename, e.g. "foo.zip.part"). If `do_cancel` is true, cancel
 * all matching items. Returns the number of matches. */
int queue_cancel_by_part(const char *partname, bool do_cancel);

#ifdef __cplusplus
}
#endif

#endif /* QUEUE_H */
