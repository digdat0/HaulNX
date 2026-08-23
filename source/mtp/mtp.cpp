/*
 * HaulNX — Copyright (c) 2026 digdat0
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 3 (or, at your option,
 * any later version), as published by the Free Software Foundation, and comes
 * WITHOUT ANY WARRANTY; see the GNU General Public License
 * (licenses/GPL-3.0.txt) for details.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * Embedded MTP responder — controller.
 *
 * Owns the UsbSession, a worker thread that runs the command responder, and the
 * link-state reported to the UI. Start() brings USB device mode up and spawns
 * the worker; the worker waits for a host to configure the interface, then runs
 * RunResponder until the host disconnects (looping to wait for the next one) or
 * Stop() tears the whole thing down. See mtp/usb_session.hpp for the transport
 * and mtp/responder.hpp for the command loop.
 */
#include <mtp/mtp.h>
#include <mtp/usb_session.hpp>
#include <mtp/responder.hpp>
#include <mtp/ptp.hpp>
#include <extract.h>
#include <fsutil.h> /* fs_rm_rf_cancelable: the background delete worker */
#include <rar3.h>

#include <cstdio>
#include <cstring>
#include <strings.h> /* strcasecmp */
#include <string>
#include <vector>

namespace mtp {

    namespace {

        UsbSession g_session;
        Thread     g_thread;
        UEvent     g_stop;
        bool       g_up       = false;
        bool       g_stopping = false;
        char       g_root[512];
        char       g_inbox[512];
        char       g_sdroot[16]; /* "sdmc:/" when full SD access is on, else "" */
        std::vector<Folder> g_folders; /* set in Start, read by the worker */

        /* Per-file transfer progress, published by the worker and read by the
         * UI thread. A small ring of the session's files; when it fills, the
         * oldest drops out. g_xcur is the file currently receiving (-1 = idle).
         * Transfers are serial (one host, one SendObject at a time), so the
         * critical sections are tiny. */
        constexpr int XferMax = 16;
        Mutex g_xlock;                 /* zero-init = unlocked */
        Xfer  g_xfers[XferMax];
        int   g_xcount  = 0;
        int   g_xcur    = -1;
        u32   g_xseq    = 0;           /* next transfer id (session-unique) */
        bool  g_xcancel = false;       /* UI asked to abort g_xcur, in place */

        /* Background extract worker. Unpacking a received archive on the
         * responder thread froze the MTP command loop for the whole unzip, and
         * WPD's Commit blocks on a follow-up command -> the PC stalled at ~99%
         * for the entire extract (see responder.cpp SendObject). So SendObject
         * hands the archive here via EnqueueExtract and returns at once; this
         * worker unpacks serially and drives the transfer's row by id (never via
         * g_xcur, which the responder has already moved on to the next file). */
        struct ExtractJob {
            char path[1040];
            char dir[1040];
            u32  id;
            u64  size;
        };
        Thread g_exthread;
        UEvent g_exwake;               /* signalled when a job is queued */
        Mutex  g_exlock;               /* guards g_exq */
        std::vector<ExtractJob> g_exq;
        bool   g_exup = false;         /* worker thread created */
        u32    g_ex_cur_id = 0;        /* job the worker is unpacking (worker-only) */

        void Worker(void *) {
            /* Serve one host at a time; when it disconnects RunResponder returns
             * and we wait for the next, until Stop() sets g_stopping. */
            while (!g_stopping) {
                if (g_session.GetConfigured()) {
                    RunResponder(std::addressof(g_session), std::addressof(g_stop), g_root,
                                 g_folders.data(), static_cast<int>(g_folders.size()), g_inbox,
                                 g_sdroot);
                }
                if (g_stopping) break;
                svcSleepThread(100000000ull); /* 100ms */
            }
        }

        /* --- background extract worker ------------------------------------ */

        /* Locate a ring entry by its stable id (may be gone if 16 newer
         * transfers evicted it). Caller holds g_xlock. */
        int FindXferIdx(u32 id) {
            for (int i = 0; i < g_xcount; i++)
                if (g_xfers[i].id == id) return i;
            return -1;
        }

        void XferExtractById(u32 id, u64 total) {
            mutexLock(std::addressof(g_xlock));
            int i = FindXferIdx(id);
            if (i >= 0) { g_xfers[i].state = Xfer_Extracting; g_xfers[i].done = 0; g_xfers[i].total = total; }
            mutexUnlock(std::addressof(g_xlock));
        }

        void XferUpdateById(u32 id, u64 done) {
            mutexLock(std::addressof(g_xlock));
            int i = FindXferIdx(id);
            if (i >= 0) g_xfers[i].done = done;
            mutexUnlock(std::addressof(g_xlock));
        }

        void XferEndById(u32 id, bool ok) {
            mutexLock(std::addressof(g_xlock));
            int i = FindXferIdx(id);
            if (i >= 0) {
                g_xfers[i].state = ok ? Xfer_Done : Xfer_Failed;
                if (ok && g_xfers[i].done < g_xfers[i].total) g_xfers[i].done = g_xfers[i].total;
            }
            mutexUnlock(std::addressof(g_xlock));
        }

        /* Extraction progress -> the connect screen's live list, keyed to the
         * worker's current job. Also the cancel hook: bail if Stop() signalled
         * so a disconnect mid-extract doesn't hold the archive for the whole
         * unpack (mirrors the old inline ExtractProgress, minus the per-session
         * Ctx which the worker doesn't have). */
        bool ExWorkerProgress(void *ud, const char *entry, int done, uint64_t bytes_read) {
            (void)ud; (void)entry; (void)done;
            XferUpdateById(g_ex_cur_id, bytes_read);
            if (g_stopping) return false;
            if (R_SUCCEEDED(waitSingle(waiterForUEvent(std::addressof(g_stop)), 0))) return false;
            return true;
        }

        void RunOneExtract(const ExtractJob &job) {
            g_ex_cur_id = job.id;
            XferExtractById(job.id, job.size);
            int ow = 0;
            int nfiles = extract_archive(job.path, job.dir, ExWorkerProgress, nullptr, std::addressof(ow));
            /* libarchive failed and this is a .rar: same fallback the
             * download-install path uses (see queue.c install_item) -- RAR3's
             * "programmable filter" entries libarchive has never implemented. */
            if (nfiles <= 0) {
                size_t plen = strlen(job.path);
                if (plen > 4 && strcasecmp(job.path + plen - 4, ".rar") == 0) {
                    ow = 0;
                    nfiles = rar3_extract(job.path, job.dir, ExWorkerProgress, nullptr, std::addressof(ow));
                }
            }
            if (nfiles > 0) remove(job.path);
            XferEndById(job.id, true); /* the game is on disk either way */
        }

        void ExtractWorker(void *) {
            while (!g_stopping) {
                int idx = 0;
                waitMulti(std::addressof(idx), UINT64_MAX,
                          waiterForUEvent(std::addressof(g_exwake)),
                          waiterForUEvent(std::addressof(g_stop)));
                if (g_stopping) break;
                for (;;) {
                    ExtractJob job;
                    mutexLock(std::addressof(g_exlock));
                    if (g_exq.empty()) { mutexUnlock(std::addressof(g_exlock)); break; }
                    job = g_exq.front();
                    g_exq.erase(g_exq.begin());
                    mutexUnlock(std::addressof(g_exlock));
                    RunOneExtract(job);
                    if (g_stopping) break;
                }
            }
        }

        /* --- background delete worker --------------------------------------
         * DeleteObject can target a huge folder (a whole console's ROMs, or
         * anywhere on the SD Card tree) -- see responder.cpp's DeleteObject,
         * which orphans the object and acks the command immediately, then
         * hands the path here via EnqueueDelete rather than recursing inline
         * and freezing the command loop for the whole delete, the same shape
         * the extract worker above exists to avoid. Simpler than that worker:
         * no per-file progress to publish (the object is already gone from
         * the host's point of view once acked), just the removal itself,
         * checked against g_stopping between entries (fs_rm_rf_cancelable) so
         * Stop() doesn't have to ride out a huge tree to join this thread. */
        Thread g_delthread;
        UEvent g_delwake;
        Mutex  g_dellock;              /* guards g_delq */
        std::vector<std::string> g_delq;
        bool   g_delup = false;        /* worker thread created */

        void DeleteWorker(void *) {
            while (!g_stopping) {
                int idx = 0;
                waitMulti(std::addressof(idx), UINT64_MAX,
                          waiterForUEvent(std::addressof(g_delwake)),
                          waiterForUEvent(std::addressof(g_stop)));
                if (g_stopping) break;
                for (;;) {
                    std::string path;
                    mutexLock(std::addressof(g_dellock));
                    if (g_delq.empty()) { mutexUnlock(std::addressof(g_dellock)); break; }
                    path = g_delq.front();
                    g_delq.erase(g_delq.begin());
                    mutexUnlock(std::addressof(g_dellock));
                    fs_rm_rf_cancelable(path.c_str(), std::addressof(g_stopping));
                    if (g_stopping) break;
                }
            }
        }

    }

    void EnqueueDelete(const char *path) {
        if (!path || !path[0]) return;
        mutexLock(std::addressof(g_dellock));
        g_delq.push_back(path);
        mutexUnlock(std::addressof(g_dellock));
        ueventSignal(std::addressof(g_delwake));
    }

    bool Start(const char *root, const Folder *folders, int nfolders,
              const char *inbox, const char *sd_root) {
        if (g_up) {
            return true;
        }

        snprintf(g_root, sizeof(g_root), "%s", root ? root : "");
        snprintf(g_inbox, sizeof(g_inbox), "%s", inbox ? inbox : "");
        snprintf(g_sdroot, sizeof(g_sdroot), "%s", (sd_root && sd_root[0]) ? sd_root : "");
        g_folders.assign(folders, folders + (nfolders > 0 ? nfolders : 0));

        /* Fresh progress list for this connection. */
        mutexLock(std::addressof(g_xlock));
        g_xcount  = 0;
        g_xcur    = -1;
        g_xseq    = 0;
        g_xcancel = false;
        mutexUnlock(std::addressof(g_xlock));

        if (R_FAILED(g_session.Initialize(std::addressof(MtpInterfaceInfo), SwitchMtpIdVendor, SwitchMtpIdProduct))) {
            g_session.Finalize();
            return false;
        }

        g_stopping = false;
        ueventCreate(std::addressof(g_stop), false);

        /* Fresh extract queue + its worker for this session. */
        mutexLock(std::addressof(g_exlock));
        g_exq.clear();
        mutexUnlock(std::addressof(g_exlock));
        ueventCreate(std::addressof(g_exwake), true /* autoclear */);

        if (R_FAILED(threadCreate(std::addressof(g_thread), Worker, nullptr, nullptr, 0x20000, 0x2C, -2)) ||
            R_FAILED(threadStart(std::addressof(g_thread)))) {
            g_session.Finalize();
            return false;
        }

        /* Extract worker: unpacks received archives off the responder thread so
         * the MTP command loop stays responsive. Its own stack (0x40000) since
         * libarchive/rar3 recurse; if it can't start, tear the responder back
         * down rather than run with inline-blocking extraction. */
        if (R_FAILED(threadCreate(std::addressof(g_exthread), ExtractWorker, nullptr, nullptr, 0x40000, 0x2C, -2)) ||
            R_FAILED(threadStart(std::addressof(g_exthread)))) {
            g_stopping = true;
            ueventSignal(std::addressof(g_stop));
            threadWaitForExit(std::addressof(g_thread));
            threadClose(std::addressof(g_thread));
            g_session.Finalize();
            return false;
        }
        g_exup = true;

        /* Delete worker: unpacks DeleteObject targets off the responder thread
         * so a big folder doesn't freeze the command loop -- see EnqueueDelete.
         * Its own fresh queue for this session; if it can't start, tear the
         * extract worker and responder back down too rather than run with
         * inline-blocking deletes. */
        mutexLock(std::addressof(g_dellock));
        g_delq.clear();
        mutexUnlock(std::addressof(g_dellock));
        ueventCreate(std::addressof(g_delwake), true /* autoclear */);

        if (R_FAILED(threadCreate(std::addressof(g_delthread), DeleteWorker, nullptr, nullptr, 0x10000, 0x2C, -2)) ||
            R_FAILED(threadStart(std::addressof(g_delthread)))) {
            g_stopping = true;
            ueventSignal(std::addressof(g_stop));
            threadWaitForExit(std::addressof(g_exthread));
            threadClose(std::addressof(g_exthread));
            threadWaitForExit(std::addressof(g_thread));
            threadClose(std::addressof(g_thread));
            g_session.Finalize();
            return false;
        }
        g_delup = true;

        g_up = true;
        return true;
    }

    Status Poll() {
        if (!g_up) {
            return Status_Down;
        }

        return g_session.GetConfigured() ? Status_Connected : Status_Waiting;
    }

    void Stop() {
        if (!g_up) {
            return;
        }

        /* Signal both workers, join them while the endpoints are still valid,
         * then tear USB down (usbDsExit must not race the responder's transfers).
         * g_stop is broadcast (non-autoclear), so both waiters see it; the
         * extract worker's progress callback also polls it, so a Stop() mid-unzip
         * aborts the unpack instead of blocking the join for the whole archive. */
        g_stopping = true;
        ueventSignal(std::addressof(g_stop));
        threadWaitForExit(std::addressof(g_thread));
        threadClose(std::addressof(g_thread));
        if (g_exup) {
            threadWaitForExit(std::addressof(g_exthread));
            threadClose(std::addressof(g_exthread));
            g_exup = false;
        }
        if (g_delup) {
            /* g_stopping is already set, and fs_rm_rf_cancelable checks it
             * between entries, so an in-flight delete unwinds promptly here
             * rather than riding out a huge tree. */
            threadWaitForExit(std::addressof(g_delthread));
            threadClose(std::addressof(g_delthread));
            g_delup = false;
        }

        g_session.Finalize();
        g_up = false;
    }

    /* --- transfer progress (worker publishes, UI reads) ------------------- */

    void XferBegin(const char *name, u64 total, const char *console) {
        mutexLock(std::addressof(g_xlock));
        if (g_xcount == XferMax) {         /* drop the oldest to make room */
            memmove(std::addressof(g_xfers[0]), std::addressof(g_xfers[1]),
                    sizeof(Xfer) * (XferMax - 1));
            g_xcount--;
        }
        Xfer *x = std::addressof(g_xfers[g_xcount]);
        x->id    = g_xseq++;
        snprintf(x->name, sizeof(x->name), "%s", name ? name : "");
        snprintf(x->console, sizeof(x->console), "%s", console ? console : "");
        x->done  = 0;
        x->total = total;
        x->state = Xfer_Active;
        g_xcur   = g_xcount;
        g_xcount++;
        g_xcancel = false; /* a stale request from the last transfer must not follow */
        mutexUnlock(std::addressof(g_xlock));
    }

    void EnqueueExtract(const char *path, u64 size) {
        /* Grab the just-received transfer (still g_xcur -- the responder hasn't
         * begun the next file), flip its row to Extracting, capture its id, then
         * clear g_xcur: the receive is done, so a Queue-tab cancel from here on
         * has nothing on the recv path to abort. The worker drives the row by id
         * from now on. */
        ExtractJob job;
        mutexLock(std::addressof(g_xlock));
        bool have = (g_xcur >= 0 && g_xcur < g_xcount);
        if (have) {
            Xfer *x  = std::addressof(g_xfers[g_xcur]);
            x->state = Xfer_Extracting;
            x->done  = 0;
            x->total = size;
            job.id   = x->id;
        }
        g_xcur    = -1;
        g_xcancel = false;
        mutexUnlock(std::addressof(g_xlock));
        if (!have) return;

        snprintf(job.path, sizeof(job.path), "%s", path ? path : "");
        snprintf(job.dir, sizeof(job.dir), "%s", path ? path : ""); /* dir = path minus filename */
        char *slash = strrchr(job.dir, '/');
        if (slash) *slash = '\0';
        job.size = size;

        mutexLock(std::addressof(g_exlock));
        g_exq.push_back(job);
        mutexUnlock(std::addressof(g_exlock));
        ueventSignal(std::addressof(g_exwake));
    }

    void XferUpdate(u64 done) {
        mutexLock(std::addressof(g_xlock));
        if (g_xcur >= 0 && g_xcur < g_xcount) g_xfers[g_xcur].done = done;
        mutexUnlock(std::addressof(g_xlock));
    }

    void XferEnd(bool ok) {
        mutexLock(std::addressof(g_xlock));
        if (g_xcur >= 0 && g_xcur < g_xcount) {
            Xfer *x = std::addressof(g_xfers[g_xcur]);
            x->state = ok ? Xfer_Done : Xfer_Failed;
            if (ok && x->done < x->total) x->done = x->total; /* land on 100% */
        }
        g_xcur    = -1;
        g_xcancel = false; /* nothing in flight to apply it to anymore */
        mutexUnlock(std::addressof(g_xlock));
    }

    int GetTransfers(Xfer *out, int max) {
        mutexLock(std::addressof(g_xlock));
        int n = g_xcount < max ? g_xcount : max;
        for (int i = 0; i < n; i++) out[i] = g_xfers[i];
        mutexUnlock(std::addressof(g_xlock));
        return n;
    }

    void CancelCurrentTransfer() {
        mutexLock(std::addressof(g_xlock));
        if (g_xcur >= 0) g_xcancel = true; /* something is actually in flight */
        mutexUnlock(std::addressof(g_xlock));
    }

    bool XferCancelRequested() {
        mutexLock(std::addressof(g_xlock));
        bool c = g_xcancel;
        mutexUnlock(std::addressof(g_xlock));
        return c;
    }

}
