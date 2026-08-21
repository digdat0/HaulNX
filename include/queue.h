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
    char dest[600];  /* resolved absolute install directory. Empty = the default
                        <roms_root>/<target>; set when the console has a custom
                        install folder. Snapshotted at enqueue so a later config
                        change doesn't redirect an item already in flight. */
    char auth[320];  /* optional archive.org S3 auth header */
    char md5[33];    /* expected MD5 hex from metadata, "" if unknown */
    uint64_t size;   /* expected file size from metadata (0 if unknown) */
    bool is_archive;
    uint32_t seq; /* insertion order, for FIFO */
    volatile QStatus status;
    volatile uint64_t now;
    volatile uint64_t total;
    volatile uint64_t speed;   /* bytes/sec, while downloading */
    /* True while Q_DOWNLOADING but no bytes are currently moving: a failed
     * attempt is about to be retried (backoff sleep or an immediate
     * credentialed re-attempt). Lets the UI show "reconnecting" instead of a
     * blank speed field during a stall the worker is actively recovering
     * from. Cleared the moment a fresh byte actually arrives. In-memory only. */
    volatile bool stalled;
    volatile int ex_files;     /* files extracted so far, while extracting */
    volatile bool cancel;
    volatile bool pause; /* ask the worker to preempt this download (keep .part) */
    /* Non-zero asks the owning worker to hand a stuck/in-progress item back to
     * the queue instead of cancelling it: 1 = retry (keep the .part and re-run
     * verify/extract), 2 = redownload (drop the .part and pull it again). Paired
     * with `cancel` for actively-owned items; see queue_requeue. Transient. */
    volatile int requeue;
    /* Passed over this round because the card can't hold what's left of it.
     * In-memory only (never persisted): a smaller item behind it still runs,
     * and the flag is cleared whenever free space could have changed. */
    volatile bool no_space;
    long http_code;
    char fail_reason[24]; /* short reason shown on a failed item, e.g. "HTTP 404" */
    int overwrote;        /* # existing files this install replaced (0 = all new) */
    /* An "external" item is one the Queue tab displays and tracks but the
     * download/extract workers never drive: a self-update download, a DAT sync,
     * or a PC->Switch receive. Its progress is pushed in by whoever owns the
     * transfer (see queue_ext_*). Never persisted (can't resume across launch).
     * xkind picks the status verb shown: 0 = download, 1 = receive/incoming,
     * 2 = install/update. */
    bool external;
    uint8_t xkind;
    /* External items only: last progress sample, so queue_ext_progress can
     * derive bytes/sec itself (like dl_progress does for real downloads) and
     * every receive shows a rate without its owner tracking one. In-memory,
     * never persisted. */
    uint64_t x_last_now;
    uint64_t x_last_tick;
} QueueItem;

/* A snapshot entry: a copy of the item plus its stable slot index. */
typedef struct {
    QueueItem item;
    int slot;
} QueueView;

/* Optional post-import hook. If set, a worker calls it right after an item
 * lands successfully, before the item is marked done: for a plain file with
 * path = the installed file and is_dir = false; for an extracted archive with
 * path = the destination directory and is_dir = true. It may reprocess what
 * landed (updating it->status/now/total for the UI) and should clear it->cancel
 * if it consumed one. NULL by default; the core carries no dependency on it. */
extern void (*queue_post_import)(QueueItem *it, const char *path, bool is_dir);

/* Enable/disable the post-import hook at runtime (user preference). When off,
 * queue_post_import is not invoked even if a module registered it. Default on. */
void queue_set_post_import_enabled(bool on);

/* Lightweight "an item just landed" notification -- separate from
 * queue_post_import above on purpose. That slot is reserved for a single
 * heavy transform add-on module (at most one can be registered at a time);
 * this one is a plain, always-available notification so an unrelated
 * subsystem (box art auto-fetch) can react to new arrivals without any risk
 * of the two stepping on each other. Called from the same two
 * successful-landing points, after queue_post_import if that ran, with
 * `name` = it->name (the display/query title) and the same path/is_dir shape.
 * Must return quickly and must not block: it runs on the download or extract
 * worker thread, immediately before the item is marked Q_DONE, so any real
 * work here should just hand off to the caller's own background thread.
 * NULL by default; the core carries no dependency on it. */
extern void (*queue_on_landed)(const char *name, const char *path,
                               bool is_dir);

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

/* When on, a downloaded archive is saved compressed (as-is) instead of being
 * extracted. Off by default. Affects items queued after the call. */
void queue_set_keep_archives(bool on);

/* Enqueue a download. Returns false if the queue is full. md5 may be "" or NULL
 * when no checksum is known. dest may be "" or NULL to install into the default
 * <roms_root>/<target>; pass a resolved absolute directory to install a console
 * with a custom folder elsewhere. */
bool queue_add(const char *url, const char *name, const char *target,
               const char *auth, uint64_t size, bool is_archive,
               const char *md5, const char *dest);

/* ---- external transfers ---------------------------------------------------
 * Entries the Queue tab shows alongside real downloads but the workers ignore:
 * app self-update, DAT sync, PC->Switch receives. The owning code creates one,
 * pushes progress into it each frame, and marks it done/failed. They are never
 * persisted and can't be reordered/retried (they're driven from outside). */

/* Create an external transfer entry. xkind: 0 download, 1 receive, 2 install,
 * 3 sync, 4 extracting (see queue_ext_set_kind). Returns its slot index (pass
 * to the calls below), or -1 if the queue is full. */
int queue_ext_add(const char *name, const char *target, uint8_t xkind);

/* Switch an in-progress external item's shown verb (e.g. 1 "receive" -> 4
 * "extracting") without touching its progress or status. For a transfer whose
 * owner unpacks the file after it lands (a USB/MTP drop straight into a
 * console folder) and wants the Queue-tab card to say so instead of still
 * reading "receive" while the byte counter resets to 0 and climbs again for
 * the unpacked size — which otherwise looks exactly like the transfer
 * restarting. No-op if slot isn't a live external item. */
void queue_ext_set_kind(int slot, uint8_t xkind);

/* Update an external item's live counters (bytes so far / total / bytes-per-sec;
 * pass 0 for any unknown). No-op if slot isn't a live external item. */
void queue_ext_progress(int slot, uint64_t now, uint64_t total, uint64_t speed);

/* Mark an external item finished: ok -> Q_DONE, else Q_FAILED with an optional
 * short reason (may be NULL). It then lingers like any finished item until
 * queue_clear_finished. */
void queue_ext_finish(int slot, bool ok, const char *fail_reason);

/* Ask an external transfer to stop (Queue-tab cancel). Sets the item's cancel
 * flag; the owner polls queue_ext_cancelled and tears its transfer down. */
void queue_ext_cancel(int slot);

/* True once queue_ext_cancel was called for this item — the owner's abort poll. */
bool queue_ext_cancelled(int slot);

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

/* Hand an in-progress item (downloading / verifying / awaiting-extract /
 * extracting) back to the queue so it runs again from its current list
 * position — the escape hatch for an item that appears stuck in verify or
 * unzip. wipe=false keeps the downloaded .part and re-runs verify/extract
 * (Retry); wipe=true drops the .part and pulls the file again (Redownload).
 * Also works on queued/paused/failed/cancelled items. The owning worker
 * releases the item cooperatively, so the change may land a moment later. */
void queue_requeue(int slot, bool wipe);

/* Re-queue every FAILED item at once (resuming from any .part on disk).
 * Returns how many were re-queued. */
int queue_retry_all(void);

/* Pause the whole queue: park any in-flight download (keeping its .part) and
 * latch the scheduler off so nothing new starts until a resume. */
void queue_pause_all(void);

/* Cancel every non-finished item at once (queued, paused, or in flight). */
void queue_cancel_all(void);

/* Bulk retry/resume by current status: pass Q_PAUSED to resume all paused
 * items in place, or Q_FAILED / Q_CANCELLED to restart those from zero.
 * Returns how many items were re-queued. */
int queue_retry_status(QStatus want);

/* Move an item one row up (dir=-1) or down (dir=+1) in the list. The active
 * download can't be moved and nothing can move above it. Returns true if the
 * order actually changed. */
bool queue_move(int slot, int dir);

/* Jump an item to the top of the waiting section (just below any active
 * downloads) when to_bottom is false, or to the very bottom when true. The
 * active download itself can't be moved. Returns true if the order changed. */
bool queue_move_end(int slot, bool to_bottom);

/* If a download/verify/extract is currently in progress -- including an
 * external item (MTP/Wi-Fi receive, update install) -- fill the out params
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

/* True while any item is actively moving bytes (downloading, verifying, or
 * extracting) — i.e. contending for the SD card / network right now. Unlike
 * queue_active_count this excludes queued/paused items, so callers can gate
 * expensive background work (e.g. the inventory rescan) on real I/O only. */
bool queue_io_active(void);

/* Count how many queued/active items have a .part file matching `partname`
 * (the bare filename, e.g. "foo.zip.part"). If `do_cancel` is true, cancel
 * all matching items. Returns the number of matches. */
int queue_cancel_by_part(const char *partname, bool do_cancel);

#ifdef __cplusplus
}
#endif

#endif /* QUEUE_H */
