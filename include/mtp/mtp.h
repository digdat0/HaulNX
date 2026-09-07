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
 * Embedded MTP responder — public interface.
 *
 * HaulNX exposes its ROM library to a PC over USB by acting as an MTP device
 * itself, entirely in-process — see mtp/usb_session.hpp and mtp/responder.hpp
 * for the transport and protocol layers this sits on top of.
 *
 * Device mode comes up as an MTP device and a worker thread serves the command
 * responder, exposing the ROM library as one storage: a folder per console
 * (each backed by that console's resolved install path — custom or default)
 * plus a synthetic "Inbox" folder (the PC drop point). Files the PC writes land
 * under those folders; the responder publishes per-file progress (see
 * Xfer/GetTransfers) so the connect screen can show a live copy list.
 */
#pragma once

#include <switch.h>
#include <boxart.h> /* BOXART_MAX_CANDIDATES, for BoxartStatus below */

#ifdef __cplusplus
namespace mtp {

    enum Status {
        Status_Down      = 0, // device mode not brought up
        Status_Waiting   = 1, // up, waiting for a host to enumerate
        Status_Connected = 2, // host has configured the interface
    };

    // One top-level folder in the MTP storage. `name` is what the PC sees (the
    // console); `path` is the console's resolved install directory on disk —
    // the per-console custom folder when set, else <roms_root>/<target>. The PC
    // only ever sees the name, but files dropped in land at `path`.
    struct Folder {
        char name[64];
        char path[768];
    };

    // Bring up USB device mode as an MTP device and start the responder worker.
    // root    = resolved ROM library root (used for the storage's free/total).
    // folders = the console folders to surface at the storage root (copied).
    // inbox   = staging folder shown as a top-level "Inbox" object.
    // sd_root = when non-NULL/non-empty (Prefs.sd_full_access on), an extra
    //           top-level "SD Card" object browsable/writable/deletable over
    //           the WHOLE card (normally "sdmc:/") -- the USB counterpart of
    //           the inventory server's fs_* routes. NULL/"" (the default)
    //           omits it entirely, same as if the feature didn't exist.
    // Returns false if usb:ds could not be acquired (e.g. docked, or the host
    // owns USB). Safe to call twice.
    bool Start(const char *root, const Folder *folders, int nfolders,
              const char *inbox, const char *sd_root = nullptr);

    // Current link state; call once per frame while the screen is open.
    Status Poll();

    // Actual negotiated USB speed of the live session (Status_Connected),
    // straight from usbDs -- the ground truth for "is this a USB 3.0 link
    // right now", independent of the console's "Enable USB 3.0" system
    // setting (that setting only controls whether a SuperSpeed configuration
    // is *offered*; whether the host actually negotiated it is this). Returns
    // UsbDeviceSpeed_None when not connected.
    UsbDeviceSpeed GetLinkSpeed();

    // Tear down USB device mode. Safe to call when already stopped.
    void Stop();

    // --- per-file transfer progress (PC->Switch), for the connect screen ----

    enum XferState { Xfer_Active = 0, Xfer_Done = 1, Xfer_Failed = 2, Xfer_Extracting = 3 };

    struct Xfer {
        u32  id;     // session-unique, strictly increasing — stable across the
                     // ring dropping its oldest entry, so the UI can map a
                     // transfer to its Queue item by id, not by array position
        char name[256];
        char console[64]; // console key of the destination folder ("" = Inbox/root)
        u64  done;   // bytes written so far
        u64  total;  // announced size (0 if the host didn't give one)
        int  state;  // XferState
    };

    // Snapshot the session's transfers (oldest first) into `out`, up to `max`
    // entries; returns the count. Thread-safe against the responder worker.
    int GetTransfers(Xfer *out, int max);

    // Progress hooks the responder worker calls as it receives a file.
    // `console` is the destination folder's console key (badges the UI row);
    // pass "" or nullptr for Inbox / storage-root drops.
    void XferBegin(const char *name, u64 total, const char *console);
    void XferUpdate(u64 done);
    void XferEnd(bool ok);

    // Ask the in-flight transfer (if any) to abort, without tearing the USB
    // session down. The responder keeps reading the announced byte count off
    // the wire (so the bulk pipe stays in sync for the next command) but stops
    // writing to disk and drops the partial file, the same cleanup path as any
    // other failed SendObject. No-op if nothing is active. UI cancel from the
    // Queue tab should call this instead of Stop() — Stop() is a full
    // disconnect/reconnect, which a single-file cancel shouldn't cost.
    void CancelCurrentTransfer();

    // Polled by the responder's receive loop; true once CancelCurrentTransfer
    // has been called for the transfer currently in flight. Cleared by the
    // next XferBegin/XferEnd.
    bool XferCancelRequested();

    // Hand the just-received archive at `path` (its file size = `size`) to the
    // background extract worker and return at once. Marks the transfer currently
    // being received (g_xcur) as extracting, captures its id, then unpacks it
    // off the responder thread so the MTP command loop stays responsive during
    // the (possibly long) unzip -- see the call site in responder.cpp SendObject
    // for why inline extraction stalled the PC. The archive is removed on a
    // successful unpack, same as the download-install path.
    void EnqueueExtract(const char *path, u64 size);

    // Hand a DeleteObject target at `path` to the background delete worker and
    // return at once. A folder can be huge (a whole console's ROMs, or
    // anywhere on the SD Card tree) -- recursing rm -rf inline on the
    // responder thread would freeze the command loop and the USB link for the
    // whole delete, the same shape EnqueueExtract exists to avoid for a
    // received archive's unpack. The caller (responder.cpp) has already
    // orphaned the object and acked the command by the time this is called,
    // so the deletion itself is fire-and-forget from the protocol's point of
    // view; Stop() unwinds an in-flight one promptly rather than riding out
    // the whole tree (see fs_rm_rf_cancelable).
    void EnqueueDelete(const char *path);

    // A console cover-art image received over USB (the MTP counterpart of
    // Wi-Fi's X-Art-Target push), staged to a temp file. Filing it needs
    // config_find_console/config_save and a Toast/screen refresh -- none of
    // which the responder thread can safely touch (see MainApplication's own
    // main-thread-only UI/config conventions) -- so responder.cpp stages the
    // file and calls this instead of applying it inline, the same handoff
    // shape EnqueueExtract/EnqueueDelete use for their own thread-unsafe
    // work. `target` is the console key; ownership of the staged file at
    // `path` passes to the consumer of PollArtPush below (it deletes it).
    void EnqueueArtPush(const char *target, const char *path);

    // One pending art push, staged by EnqueueArtPush above.
    struct ArtPush {
        char target[64];
        char path[768];
    };

    // Main thread, once per frame alongside PushExtractTick: pop one pending
    // art push if any is waiting. Returns false (out untouched) when empty.
    bool PollArtPush(ArtPush *out);

    // Box-art search/pick over USB -- the MTP counterpart of Wi-Fi's
    // boxartsearch/boxartpick routes and their _status polling. A search/pick
    // is genuine SteamGridDB network I/O, so it's triggered and answered the
    // same way the Wi-Fi routes already are (see the long comment on
    // HttpSrv's boxsearch_*/boxpick_* fields in httpsrv.h): a request is
    // stashed here (by the responder thread, off a buffered "boxart_request.
    // json" push -- see mtp/responder.cpp), MainApplication's InvBoxartTick
    // (which now runs every frame regardless of transport) notices it and
    // does the real work on its own background thread, then publishes the
    // result back via SetBoxartStatus for the responder thread's
    // boxart_status.json root object to report. Guarded by an internal mutex
    // since both threads touch this.
    struct BoxartReq {
        bool is_pick;    // false = search, true = pick
        char target[64];
        char query[256]; // search only
        int  index;      // pick only
    };
    // Responder thread: stash a decoded request (see mtp/responder.cpp's
    // "boxart_request.json" handling) for the main thread to pick up.
    void EnqueueBoxartReq(const BoxartReq &req);
    // Main thread, once per frame: pop a pending request, if any. Returns
    // false (out untouched) when none is waiting.
    bool PollBoxartReq(BoxartReq *out);

    struct BoxartStatus {
        bool search_running = false, search_done = false;
        int  search_count = 0;
        int  search_w[BOXART_MAX_CANDIDATES] = {};
        int  search_h[BOXART_MAX_CANDIDATES] = {};
        bool pick_running = false, pick_done = false;
        bool pick_ok = false;
    };
    // Main thread: publish the current search/pick snapshot after
    // InvBoxartTick updates it (mirrors what it already writes into
    // HttpSrv's own fields for the Wi-Fi side).
    void SetBoxartStatus(const BoxartStatus &st);
    // Responder thread: read the last-published snapshot, for
    // boxart_status.json.
    BoxartStatus GetBoxartStatus();

}
#endif
