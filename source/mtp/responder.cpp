/*
 * HaulNX — Copyright (c) 2026 digdat0
 * Portions of the container/dispatch pattern follow cmtp-responder
 * (github.com/cmtp-responder/cmtp-responder), Copyright (c) 2012, 2013
 * Samsung Electronics Co., Ltd. and Copyright (c) 2019 Collabora Ltd.
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
 * Embedded MTP command responder.
 *
 * Implements the PTP/MTP command loop over a configured UsbSession. The ROM
 * library is exposed as one MTP storage rooted at the ROM root; its top level
 * lists the console folders that live there plus a synthetic "Inbox" folder
 * (the drop point for files pushed from the PC). Object handles are assigned
 * lazily as the host enumerates and kept stable for the life of the session.
 *
 * Built directly on libnx usbds + newlib directory calls (see
 * mtp/usb_session.hpp for the transport). The container framing follows the
 * same init/send/receive shape as cmtp-responder's ptp_container.c
 * (Apache-2.0) — see mtp/responder.hpp for the attribution and why that
 * project is a clean layout reference. The object model and folder mapping
 * below are HaulNX's own, built around this responder's actual feature set
 * (a curated console-folder tree + Inbox, not a general filesystem).
 */
#include <mtp/responder.hpp>
#include <mtp/mtp.h>
#include <mtp/ptp.hpp>
#include <fsutil.h>
#include <extract.h>
#include <config.h>
#include <updman.h>
#include <boxart.h> /* boxart_lookup: root "art_<target>.png" objects below */
#include <queue.h> /* queue_write_status_json: root "queue_status.json" object below */

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <malloc.h>
#include <vector>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <strings.h> /* strcasecmp */

namespace mtp {

    namespace {

        constexpr u32 StorageId  = 0x00010001;
        constexpr size_t IoCap   = 0x200000; /* 2 MiB transfer buffer */

        /* Append a timestamped line to the shared debug log -- same file/format
         * as extract.c's ex_log. The HH:MM:SS prefix is what lets a stall be
         * read off the log: a big gap between "recv done" and the next command
         * pins the delay to extraction rather than the wire. Pure file I/O, no
         * USB waits -- safe to call from anywhere on the responder thread. */
        void mtp_log(const char *fmt, ...) {
            fs_mkdir_p(LOGS_DIR);
            FILE *f = fopen(LOG_PATH, "a");
            if (!f) return;
            time_t now = time(nullptr);
            struct tm tmv;
            localtime_r(std::addressof(now), std::addressof(tmv));
            char ts[16];
            strftime(ts, sizeof(ts), "%H:%M:%S", std::addressof(tmv));
            fprintf(f, "%s  ", ts);
            va_list ap;
            va_start(ap, fmt);
            vfprintf(f, fmt, ap);
            va_end(ap);
            fputc('\n', f);
            fclose(f);
        }

        struct Obj {
            u32    handle;
            u32    parent;   /* PtpRootParentObject when in the storage root */
            bool   is_dir;
            u64    size;
            time_t mtime;
            char   path[768];
            char   name[256];
        };

        /* An object announced by SendObjectInfo, awaiting its SendObject data. */
        struct Pending {
            bool active;
            bool extract;  /* the file is an archive to unpack in place once received */
            bool apply_updsrc; /* root update_sources.json push: validate + apply on finish */
            u32  handle;
            u64  size;
            char path[1040];
        };

        struct Ctx {
            UsbSession   *session;
            UEvent       *stop;
            const char   *root;
            const Folder *folders;
            int           nfolders;
            const char   *inbox;
            /* Optional extra top-level folder over the WHOLE card (see
             * mtp::Start's sd_root). NULL/"" when the feature is off — the
             * ordinary console-folders-only tree, unchanged. Deliberately
             * kept OUT of `folders`/`nfolders`: UnderKnownFolder below (the
             * archive-auto-extract gate) walks only the real console paths,
             * so browsing the same physical console folder through this
             * generic SD tree instead of its own named folder can never
             * trigger an unpack somewhere that isn't actually a managed
             * install folder (e.g. dropping a random .zip at sdmc:/ or into
             * an unrelated sdmc:/ subfolder must never auto-extract). */
            const char   *sd_root;
            bool        stopping;
            bool        session_open;
            u32         session_id;
            std::vector<Obj> objs;
            Pending     pending;
            u8         *io;
            u8         *io2;   /* second receive buffer for double-buffered SendObject */
        };

        /* --- USB transfer plumbing ---------------------------------------- */

        bool WaitTransfer(Ctx *c, UsbSessionEndpoint ep) {
            Event *ce = c->session->GetCompletionEvent(ep);
            /* Also watch the USB link: when the cable is yanked mid-transfer the
             * posted URB never completes, so without this the worker would block
             * here forever, holding usb:ds (the port stays in device mode and a
             * controller can't be hosted). A state change that leaves us no
             * longer configured is a link drop -> bail so we can free USB.
             *
             * NOTE: this used to carry a 3s diagnostic timeout that logged and
             * looped back into the same wait when a transfer stalled. Reverted --
             * it correlated with a Switch-side USB wedge (device unrecognizable
             * on replug until console reboot) reported right after it shipped.
             * A single unbounded wait per completion is the known-good shape;
             * don't reintroduce a timeout/re-wait loop here without hardware
             * evidence it's safe. */
            Event *se = usbDsGetStateChangeEvent();
            for (;;) {
                int idx = 0;
                Result rc = waitMulti(std::addressof(idx), UINT64_MAX,
                                      waiterForEvent(ce), waiterForUEvent(c->stop),
                                      waiterForEvent(se));
                if (R_FAILED(rc) || idx == 1) { /* stop requested */
                    c->stopping = true;
                    return false;
                }
                if (idx == 2) {                 /* USB state changed */
                    eventClear(se);
                    if (!c->session->GetConfigured()) { /* host went away */
                        c->stopping = true;
                        return false;
                    }
                    continue;                   /* still configured; keep waiting */
                }
                return true;                    /* transfer completed */
            }
        }

        /* Post an async read (returns immediately) and reap it separately, so a
         * caller can overlap the SD write of one chunk with the USB read of the
         * next (see SendObject). ReadData keeps the post+reap-in-one contract. */
        bool ReadPost(Ctx *c, void *buf, u32 size, u32 *urb) {
            if (R_FAILED(c->session->TransferAsync(UsbSessionEndpoint_Read, buf, size, urb))) {
                c->stopping = true;
                return false;
            }
            return true;
        }

        bool ReadReap(Ctx *c, u32 urb, u32 *out) {
            if (!WaitTransfer(c, UsbSessionEndpoint_Read)) {
                return false;
            }
            if (R_FAILED(c->session->GetTransferResult(UsbSessionEndpoint_Read, urb, out))) {
                c->stopping = true;
                return false;
            }
            return true;
        }

        bool ReadData(Ctx *c, void *buf, u32 size, u32 *out) {
            u32 urb;
            if (!ReadPost(c, buf, size, std::addressof(urb))) return false;
            return ReadReap(c, urb, out);
        }

        /* Post an async write (returns immediately) and reap it separately, so a
         * caller can overlap the next SD read with this USB write in flight (see
         * OpGetObject). WriteData keeps the post+reap-in-one contract. */
        bool WritePost(Ctx *c, void *buf, u32 size, u32 *urb) {
            if (R_FAILED(c->session->TransferAsync(UsbSessionEndpoint_Write, buf, size, urb))) {
                c->stopping = true;
                return false;
            }
            return true;
        }

        bool WriteReap(Ctx *c, u32 urb) {
            if (!WaitTransfer(c, UsbSessionEndpoint_Write)) {
                return false;
            }
            u32 transferred;
            if (R_FAILED(c->session->GetTransferResult(UsbSessionEndpoint_Write, urb, std::addressof(transferred)))) {
                c->stopping = true;
                return false;
            }
            return true;
        }

        bool WriteData(Ctx *c, void *buf, u32 size) {
            u32 urb;
            return WritePost(c, buf, size, std::addressof(urb)) && WriteReap(c, urb);
        }

        /* --- little-endian dataset writer --------------------------------- */

        struct Buf {
            u8  *base;
            u8  *p;
            u8  *end;
            bool ovf;
        };

        Buf DataBuf(Ctx *c) {
            Buf b;
            b.base = c->io + sizeof(PtpUsbBulkContainer);
            b.p    = b.base;
            b.end  = c->io + IoCap;
            b.ovf  = false;
            return b;
        }

        void bU8 (Buf *b, u8 v)  { if (b->p + 1 > b->end) { b->ovf = true; return; } *b->p++ = v; }
        void bU16(Buf *b, u16 v) { if (b->p + 2 > b->end) { b->ovf = true; return; } memcpy(b->p, &v, 2); b->p += 2; }
        void bU32(Buf *b, u32 v) { if (b->p + 4 > b->end) { b->ovf = true; return; } memcpy(b->p, &v, 4); b->p += 4; }
        void bU64(Buf *b, u64 v) { if (b->p + 8 > b->end) { b->ovf = true; return; } memcpy(b->p, &v, 8); b->p += 8; }
        void bU128(Buf *b, u64 lo, u64 hi) { bU64(b, lo); bU64(b, hi); }

        /* UTF-8 -> UTF-16 (BMP + surrogates); returns code units written. */
        int Utf8ToUtf16(const char *s, u16 *out, int cap) {
            int n = 0;
            const unsigned char *p = reinterpret_cast<const unsigned char *>(s);
            while (*p && n < cap) {
                u32 cp;
                unsigned char c = *p;
                if (c < 0x80) {
                    cp = c; p += 1;
                } else if ((c >> 5) == 0x6 && p[1]) {
                    cp = ((c & 0x1F) << 6) | (p[1] & 0x3F); p += 2;
                } else if ((c >> 4) == 0xE && p[1] && p[2]) {
                    cp = ((c & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F); p += 3;
                } else if ((c >> 3) == 0x1E && p[1] && p[2] && p[3]) {
                    cp = ((c & 0x07) << 18) | ((p[1] & 0x3F) << 12) | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F); p += 4;
                } else {
                    cp = '?'; p += 1;
                }
                if (cp <= 0xFFFF) {
                    out[n++] = static_cast<u16>(cp);
                } else {
                    cp -= 0x10000;
                    if (n + 2 > cap) break;
                    out[n++] = static_cast<u16>(0xD800 + (cp >> 10));
                    out[n++] = static_cast<u16>(0xDC00 + (cp & 0x3FF));
                }
            }
            return n;
        }

        /* MTP string: u8 length (chars incl. terminator), then UTF-16LE + NUL. */
        void bStr(Buf *b, const char *s) {
            if (!s || !*s) { bU8(b, 0); return; }
            u16 tmp[256];
            int n = Utf8ToUtf16(s, tmp, 254);
            bU8(b, static_cast<u8>(n + 1));
            for (int i = 0; i < n; i++) bU16(b, tmp[i]);
            bU16(b, 0);
        }

        /* UTF-16 -> UTF-8 (handles surrogate pairs); NUL-terminates out. */
        void Utf16ToUtf8(const u16 *in, int n, char *out, size_t outsz) {
            size_t o = 0;
            for (int i = 0; i < n && o + 4 < outsz; i++) {
                u32 cp = in[i];
                if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < n) {
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (in[++i] - 0xDC00);
                }
                if (cp < 0x80) {
                    out[o++] = static_cast<char>(cp);
                } else if (cp < 0x800) {
                    out[o++] = static_cast<char>(0xC0 | (cp >> 6));
                    out[o++] = static_cast<char>(0x80 | (cp & 0x3F));
                } else if (cp < 0x10000) {
                    out[o++] = static_cast<char>(0xE0 | (cp >> 12));
                    out[o++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                    out[o++] = static_cast<char>(0x80 | (cp & 0x3F));
                } else {
                    out[o++] = static_cast<char>(0xF0 | (cp >> 18));
                    out[o++] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                    out[o++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                    out[o++] = static_cast<char>(0x80 | (cp & 0x3F));
                }
            }
            out[o] = 0;
        }

        /* Parse an MTP string at *pp (u8 length incl. terminator, then UTF-16LE),
         * decode to UTF-8 in out, and advance *pp past it. */
        void ParseStr(const u8 **pp, const u8 *end, char *out, size_t outsz) {
            const u8 *p = *pp;
            out[0] = 0;
            if (p >= end) return;
            u8 nchars = *p++;
            if (nchars == 0) { *pp = p; return; }
            u16 tmp[256];
            int n = 0;
            for (u8 i = 0; i < nchars; i++) {
                if (p + 2 > end) break;
                u16 v;
                memcpy(&v, p, 2);
                p += 2;
                if (i + 1 < nchars && n < 255) tmp[n++] = v; /* drop terminator unit */
            }
            Utf16ToUtf8(tmp, n, out, outsz);
            *pp = p;
        }

        void FmtDate(time_t t, char *out, size_t n) {
            struct tm tmv;
            gmtime_r(std::addressof(t), std::addressof(tmv));
            strftime(out, n, "%Y%m%dT%H%M%S", std::addressof(tmv));
        }

        /* --- container send ----------------------------------------------- */

        bool SendData(Ctx *c, u16 code, u32 trans, Buf *b) {
            if (b->ovf) { c->stopping = true; return false; }
            u32 payload = static_cast<u32>(b->p - (c->io + sizeof(PtpUsbBulkContainer)));
            PtpUsbBulkContainer *h = reinterpret_cast<PtpUsbBulkContainer *>(c->io);
            h->length   = sizeof(PtpUsbBulkContainer) + payload;
            h->type     = PtpContainerType_Data;
            h->code     = code;
            h->trans_id = trans;
            return WriteData(c, c->io, sizeof(PtpUsbBulkContainer) + payload);
        }

        bool SendResponse(Ctx *c, u16 code, u32 trans, const u32 *params, u32 nparams) {
            PtpUsbBulkContainer *h = reinterpret_cast<PtpUsbBulkContainer *>(c->io);
            h->length   = sizeof(PtpUsbBulkContainer) + 4 * nparams;
            h->type     = PtpContainerType_Response;
            h->code     = code;
            h->trans_id = trans;
            for (u32 i = 0; i < nparams; i++) memcpy(c->io + sizeof(PtpUsbBulkContainer) + 4 * i, &params[i], 4);
            return WriteData(c, c->io, sizeof(PtpUsbBulkContainer) + 4 * nparams);
        }

        void RC(Ctx *c, u16 code, u32 trans) { SendResponse(c, code, trans, nullptr, 0); }

        /* --- object database ---------------------------------------------- */

        Obj *FindHandle(Ctx *c, u32 h) {
            for (auto &o : c->objs) if (o.handle == h) return std::addressof(o);
            return nullptr;
        }

        u32 AddOrFind(Ctx *c, u32 parent, const char *path, const char *name,
                      bool is_dir, u64 size, time_t mtime) {
            for (auto &o : c->objs) {
                if (o.parent == parent && strcmp(o.name, name) == 0) {
                    o.is_dir = is_dir;
                    o.size   = size;
                    o.mtime  = mtime;
                    return o.handle;
                }
            }
            Obj o{};
            o.handle = static_cast<u32>(c->objs.size()) + 1;
            o.parent = parent;
            o.is_dir = is_dir;
            o.size   = size;
            o.mtime  = mtime;
            snprintf(o.path, sizeof(o.path), "%s", path);
            snprintf(o.name, sizeof(o.name), "%s", name);
            c->objs.push_back(o);
            return o.handle;
        }

        /* Repoint an object at its new on-disk path after a rename/move. Kept
         * out-of-line so the source length is unknown at the copy site — inlining
         * would propagate the caller's larger buffer size and trip
         * -Wformat-truncation. o->path is generously sized; real paths fit. */
        __attribute__((noinline))
        void SetObjPath(Obj *o, const char *path) {
            snprintf(o->path, sizeof(o->path), "%s", path);
        }

        /* The console key of the top-level folder `full` lives under, or "" for
         * the Inbox / storage root. Only used to badge the connect screen's
         * progress row with the right console icon and short name. */
        const char *ConsoleForPath(Ctx *c, const char *full) {
            for (int i = 0; i < c->nfolders; i++) {
                const char *fp = c->folders[i].path;
                size_t l = strlen(fp);
                if (l == 0) continue;
                if (strncmp(full, fp, l) == 0 && (full[l] == '/' || full[l] == '\0'))
                    return c->folders[i].name;
            }
            /* Inbox drops get a sentinel key: the UI badges them with the generic
             * icon (console_icon falls back to "default") and the label "INBOX". */
            if (c->inbox && c->inbox[0]) {
                size_t l = strlen(c->inbox);
                if (strncmp(full, c->inbox, l) == 0 && (full[l] == '/' || full[l] == '\0'))
                    return "inbox";
            }
            return "";
        }

        /* True if `full` is (a descendant of) one of the real console
         * folders in c->folders — never the synthetic Inbox or the SD Card
         * root, even though both can reach the same physical file. Used to
         * gate archive auto-extraction on SendObject: extraction should only
         * ever fire for an actual managed install folder, regardless of
         * which top-level object the host walked through to get there. */
        bool UnderKnownFolder(Ctx *c, const char *full) {
            for (int i = 0; i < c->nfolders; i++) {
                const char *fp = c->folders[i].path;
                size_t l = strlen(fp);
                if (l == 0) continue;
                if (strncmp(full, fp, l) == 0 && (full[l] == '/' || full[l] == '\0'))
                    return true;
            }
            return false;
        }

        /* True if `path` names the SD Card object's own root (c->sd_root,
         * e.g. "sdmc:/") rather than something under it. DeleteObject,
         * SetObjectPropValue (rename) and MoveObject must all refuse this:
         * unlike a console folder or Inbox, this object stands for the
         * ENTIRE physical card -- deleting or renaming it would touch every
         * other homebrew's data and saves, not one managed folder. Nothing
         * about "browse/add/rename/delete anywhere on the card" was ever
         * meant to include the card itself. Mirrors sd_path_is_root on the
         * Wi-Fi side (httpsrv.c) for the same guard over fs_rm/fs_mv. */
        bool IsSdRoot(Ctx *c, const char *path) {
            return c->sd_root && c->sd_root[0] && strcmp(path, c->sd_root) == 0;
        }

        /* Enumerate the children of `parent` into the DB and return their
         * handles. */
        u16 CollectChildren(Ctx *c, u32 parent, std::vector<u32> &out) {
            u32 pnorm = (parent == 0 || parent == PtpRootParentObject) ? PtpRootParentObject : parent;

            /* Root: the console folders (shown by name, backed by each console's
             * resolved install path) plus the synthetic Inbox. The name/path
             * split lets a custom-folder console like 3ds -> sdmc:/3ds appear as
             * plain "3ds" while writes land at the real path. */
            if (pnorm == PtpRootParentObject) {
                for (int i = 0; i < c->nfolders; i++) {
                    const Folder &f = c->folders[i];
                    struct stat st;
                    time_t mt = (stat(f.path, &st) == 0) ? st.st_mtime : 0;
                    out.push_back(AddOrFind(c, PtpRootParentObject, f.path, f.name, true, 0, mt));
                }
                struct stat st;
                time_t mt = (stat(c->inbox, &st) == 0) ? st.st_mtime : 0;
                out.push_back(AddOrFind(c, PtpRootParentObject, c->inbox, "Inbox", true, 0, mt));

                /* Full SD card access (Prefs.sd_full_access): one more
                 * top-level folder over the whole card, generic like any
                 * other -- CollectChildren's recursive branch below already
                 * just opendir()s whatever real path it's handed, so nothing
                 * else here needs to change for browsing, writing, renaming
                 * or (recursive) deleting anywhere under it. Only the
                 * archive-auto-extract gate above treats it differently. */
                if (c->sd_root && c->sd_root[0]) {
                    struct stat sdst;
                    time_t sdmt = (stat(c->sd_root, &sdst) == 0) ? sdst.st_mtime : 0;
                    out.push_back(AddOrFind(c, PtpRootParentObject, c->sd_root,
                                            "SD Card", true, 0, sdmt));
                }

                /* A read-only snapshot of what's installed, so a USB-connected PC
                 * companion can pull the same inventory the Wi-Fi server serves.
                 * Only appears once the UI thread has written it (see UsbMtpTick),
                 * and GetObject streams it straight from INVENTORY_PATH. */
                struct stat ist;
                if (stat(INVENTORY_PATH, &ist) == 0)
                    out.push_back(AddOrFind(c, PtpRootParentObject, INVENTORY_PATH,
                                            "inventory.json", false,
                                            static_cast<u64>(ist.st_size), ist.st_mtime));

                /* The saved collection (archive.org sources), so a USB companion
                 * can pull it into its editor the same way the Wi-Fi server serves
                 * it. Present once a collection has been saved at least once. */
                struct stat sst;
                if (stat(SOURCES_PATH, &sst) == 0)
                    out.push_back(AddOrFind(c, PtpRootParentObject, SOURCES_PATH,
                                            "dl_sources.json", false,
                                            static_cast<u64>(sst.st_size), sst.st_mtime));

                /* The saved archive.org S3 keys, so a USB companion can adopt
                 * them on connect the same way the Wi-Fi server hands them over.
                 * Present once the user has saved credentials at least once. */
                struct stat cst;
                if (stat(CREDS_PATH, &cst) == 0)
                    out.push_back(AddOrFind(c, PtpRootParentObject, CREDS_PATH,
                                            "credentials.json", false,
                                            static_cast<u64>(cst.st_size), cst.st_mtime));

                /* The shared emulator/app update manifest, so a USB companion can
                 * pull and edit the same list the Wi-Fi server serves. Writing it
                 * back is handled specially in SendObject (validated + applied to
                 * UPDSRC_PATH). Present once the update manager has seeded it. */
                struct stat ust;
                if (stat(UPDSRC_PATH, &ust) == 0)
                    out.push_back(AddOrFind(c, PtpRootParentObject, UPDSRC_PATH,
                                            "update_sources.json", false,
                                            static_cast<u64>(ust.st_size), ust.st_mtime));

                /* The rolling debug-log bundle (Settings > Diagnostics), so a
                 * USB companion can pull the same file GET /debug_bundle.txt
                 * serves over Wi-Fi -- see diag_bundle_write's own comment.
                 * Unlike the Wi-Fi route (one regen per deliberate GET),
                 * reaching this branch at all doesn't mean the host actually
                 * wants the bundle -- top()/file_in() re-enumerate the whole
                 * root on EVERY USB read (console-folder browsing, downloads,
                 * every micro-op), so calling the Wi-Fi side's "cheap, do it
                 * every time" logic here would mean re-concatenating every log
                 * file on the SD card many times a second during something as
                 * ordinary as browsing a console folder. Throttled instead: at
                 * most once every few seconds, still fresh enough for a
                 * deliberate pull. The object itself is still (re-)listed
                 * every time off whatever's already on disk, so a lookup never
                 * intermittently 404s just because this call landed inside a
                 * throttle window. */
                static time_t s_diag_last = 0;
                time_t diag_now = time(nullptr);
                if (diag_now - s_diag_last >= 3) {
                    diag_bundle_write();
                    s_diag_last = diag_now;
                }
                struct stat dst;
                if (stat(DIAG_BUNDLE_PATH, &dst) == 0)
                    out.push_back(AddOrFind(c, PtpRootParentObject, DIAG_BUNDLE_PATH,
                                            "debug_bundle.txt", false,
                                            static_cast<u64>(dst.st_size), dst.st_mtime));

                /* The live Queue tab snapshot (active/recent transfers), so a
                 * USB companion's Downloads tab can show the Switch's own
                 * queue alongside its PC-side one -- same file GET
                 * /queue_status.json serves over Wi-Fi. queue_write_status_json
                 * is just an in-memory snapshot plus a small JSON write (much
                 * cheaper than the debug bundle's log concatenation), but this
                 * branch still runs on every USB micro-op, so it gets the same
                 * throttle rather than writing the SD card that often -- a
                 * shorter one since a live-feeling queue view is the whole
                 * point here. */
                static time_t s_qstat_last = 0;
                time_t qstat_now = time(nullptr);
                if (qstat_now - s_qstat_last >= 1) {
                    queue_write_status_json(QUEUE_STATUS_PATH);
                    s_qstat_last = qstat_now;
                }
                struct stat qst;
                if (stat(QUEUE_STATUS_PATH, &qst) == 0)
                    out.push_back(AddOrFind(c, PtpRootParentObject, QUEUE_STATUS_PATH,
                                            "queue_status.json", false,
                                            static_cast<u64>(qst.st_size), qst.st_mtime));

                /* Each console's current cover art (Console Art / companion
                 * push), so a USB companion can preview and cache it the same
                 * way GET /consoleart serves it over Wi-Fi -- boxart_lookup
                 * resolves the same "console:<target>" cache key either way.
                 * Named "art_<target>.png" so the companion can ask for one
                 * console's art directly instead of listing the whole root to
                 * find it; the reported mtime is what lets it skip re-pulling
                 * art that hasn't changed since the last sync.
                 *
                 * Unlike the two objects above, this loop's cost scales with
                 * the library's console count (a boxart_lookup + stat() per
                 * console) -- on a 50+ console library that's 50+ blocking
                 * syscalls on this thread, on every USB micro-op, since this
                 * whole branch runs that often (see the header comment on
                 * this function). Cache the resolved list the same way the
                 * objects above cache their own expensive part, so ordinary
                 * browsing only pays this cost once every couple of seconds
                 * instead of on every listing. */
                struct ArtEnt { char path[768]; char name[96]; u64 size; time_t mtime; };
                static std::vector<ArtEnt> s_art_cache;
                static time_t s_art_last = 0;
                time_t art_now = time(nullptr);
                if (art_now - s_art_last >= 2 || s_art_cache.empty()) {
                    s_art_cache.clear();
                    for (int i = 0; i < c->nfolders; i++) {
                        char key[80], path[768];
                        snprintf(key, sizeof(key), "console:%s", c->folders[i].name);
                        if (!boxart_lookup(key, path, sizeof(path))) continue;
                        struct stat ast;
                        if (stat(path, &ast) != 0) continue;
                        ArtEnt e{};
                        snprintf(e.path, sizeof(e.path), "%s", path);
                        snprintf(e.name, sizeof(e.name), "art_%s.png", c->folders[i].name);
                        e.size = static_cast<u64>(ast.st_size);
                        e.mtime = ast.st_mtime;
                        s_art_cache.push_back(e);
                    }
                    s_art_last = art_now;
                }
                for (const auto &e : s_art_cache) {
                    out.push_back(AddOrFind(c, PtpRootParentObject, e.path, e.name,
                                            false, e.size, e.mtime));
                }
                return PtpResponseCode_Ok;
            }

            Obj *po = FindHandle(c, pnorm);
            if (!po)          return PtpResponseCode_InvalidObjectHandle;
            if (!po->is_dir)  return PtpResponseCode_InvalidParentObject;

            /* Build the listing from the live directory so objects deleted
             * (by the host or elsewhere) drop out and new ones appear. */
            DIR *d = opendir(po->path);
            if (d) {
                struct dirent *e;
                while ((e = readdir(d)) != nullptr) {
                    if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
                    char fp[1040];
                    snprintf(fp, sizeof(fp), "%s/%s", po->path, e->d_name);
                    struct stat st;
                    if (stat(fp, &st) != 0) continue;
                    out.push_back(AddOrFind(c, pnorm, fp, e->d_name, S_ISDIR(st.st_mode),
                                            static_cast<u64>(st.st_size), st.st_mtime));
                }
                closedir(d);
            }

            return PtpResponseCode_Ok;
        }

        /* --- dataset builders --------------------------------------------- */

        void BuildDeviceInfo(Buf *b) {
            static const u16 ops[] = {
                PtpOperationCode_GetDeviceInfo, PtpOperationCode_OpenSession,
                PtpOperationCode_CloseSession, PtpOperationCode_GetStorageIds,
                PtpOperationCode_GetStorageInfo, PtpOperationCode_GetNumObjects,
                PtpOperationCode_GetObjectHandles, PtpOperationCode_GetObjectInfo,
                PtpOperationCode_GetObject, PtpOperationCode_GetObjectPropsSupported,
                PtpOperationCode_GetObjectPropDesc, PtpOperationCode_GetObjectPropValue,
                PtpOperationCode_SendObjectInfo, PtpOperationCode_SendObject,
                PtpOperationCode_DeleteObject, PtpOperationCode_MoveObject,
                PtpOperationCode_SetObjectPropValue,
            };

            bU16(b, 100);                          /* StandardVersion */
            bU32(b, PtpMtpVendorExtensionId);      /* MTP */
            bU16(b, 100);                          /* VendorExtensionVersion */
            bStr(b, "microsoft.com: 1.0;");        /* VendorExtensionDesc */
            bU16(b, 0);                            /* FunctionalMode */

            bU32(b, sizeof(ops) / sizeof(ops[0])); /* OperationsSupported */
            for (u16 op : ops) bU16(b, op);
            bU32(b, 0);                            /* EventsSupported */
            bU32(b, 0);                            /* DevicePropertiesSupported */
            bU32(b, 0);                            /* CaptureFormats */
            bU32(b, 2);                            /* PlaybackFormats */
            bU16(b, PtpObjectFormatCode_Association);
            bU16(b, PtpObjectFormatCode_Undefined);

            bStr(b, "Nintendo");                   /* Manufacturer */
            bStr(b, "HaulNX");                     /* Model */
            bStr(b, "1.0");                        /* DeviceVersion */
            bStr(b, "HaulNX00000000001");          /* SerialNumber */
        }

        void BuildObjectInfo(Obj *o, Buf *b) {
            char date[32];
            FmtDate(o->mtime, date, sizeof(date));

            bU32(b, StorageId);
            bU16(b, o->is_dir ? PtpObjectFormatCode_Association : PtpObjectFormatCode_Undefined);
            bU16(b, PtpProtectionStatus_NoProtection);
            bU32(b, o->size > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<u32>(o->size)); /* compressed size */
            bU16(b, 0);                            /* ThumbFormat */
            bU32(b, 0);                            /* ThumbCompressedSize */
            bU32(b, 0);                            /* ThumbPixWidth */
            bU32(b, 0);                            /* ThumbPixHeight */
            bU32(b, 0);                            /* ImagePixWidth */
            bU32(b, 0);                            /* ImagePixHeight */
            bU32(b, 0);                            /* ImageBitDepth */
            bU32(b, o->parent == PtpRootParentObject ? 0 : o->parent);
            bU16(b, o->is_dir ? PtpAssociationType_GenericFolder : 0);
            bU32(b, 0);                            /* AssociationDesc */
            bU32(b, 0);                            /* SequenceNumber */
            bStr(b, o->name);                      /* Filename */
            bStr(b, date);                         /* DateCreated */
            bStr(b, date);                         /* DateModified */
            bStr(b, "");                           /* Keywords */
        }

        u16 PropDataType(u16 pc) {
            switch (pc) {
                case PtpObjectPropertyCode_StorageId:        return PtpDataTypeCode_U32;
                case PtpObjectPropertyCode_ObjectFormat:     return PtpDataTypeCode_U16;
                case PtpObjectPropertyCode_ProtectionStatus: return PtpDataTypeCode_U16;
                case PtpObjectPropertyCode_ObjectSize:       return PtpDataTypeCode_U64;
                case PtpObjectPropertyCode_ObjectFileName:   return PtpDataTypeCode_String;
                case PtpObjectPropertyCode_ParentObject:     return PtpDataTypeCode_U32;
                case PtpObjectPropertyCode_PersistentUniqueObjectIdentifier: return PtpDataTypeCode_U128;
                case PtpObjectPropertyCode_Name:             return PtpDataTypeCode_String;
                case PtpObjectPropertyCode_DateModified:     return PtpDataTypeCode_String;
                default:                                     return 0;
            }
        }

        /* Write a single object property value (no container header). */
        void WritePropValue(Obj *o, u16 pc, Buf *b) {
            char date[32];
            switch (pc) {
                case PtpObjectPropertyCode_StorageId:        bU32(b, StorageId); break;
                case PtpObjectPropertyCode_ObjectFormat:     bU16(b, o->is_dir ? PtpObjectFormatCode_Association : PtpObjectFormatCode_Undefined); break;
                case PtpObjectPropertyCode_ProtectionStatus: bU16(b, PtpProtectionStatus_NoProtection); break;
                case PtpObjectPropertyCode_ObjectSize:       bU64(b, o->size); break;
                case PtpObjectPropertyCode_ObjectFileName:   bStr(b, o->name); break;
                case PtpObjectPropertyCode_ParentObject:     bU32(b, o->parent == PtpRootParentObject ? 0 : o->parent); break;
                case PtpObjectPropertyCode_PersistentUniqueObjectIdentifier: bU128(b, o->handle, 0); break;
                case PtpObjectPropertyCode_Name:             bStr(b, o->name); break;
                case PtpObjectPropertyCode_DateModified:     FmtDate(o->mtime, date, sizeof(date)); bStr(b, date); break;
                default: break;
            }
        }

        /* --- GetObject (stream a file to the host) ------------------------ */

        u16 OpGetObject(Ctx *c, PtpUsbBulkContainer *cmd, u32 *p, u32 np) {
            if (np < 1) return PtpResponseCode_InvalidParameter;
            Obj *o = FindHandle(c, p[0]);
            if (!o)         return PtpResponseCode_InvalidObjectHandle;
            if (o->is_dir)  return PtpResponseCode_InvalidObjectHandle;

            FILE *f = fopen(o->path, "rb");
            if (!f) return PtpResponseCode_AccessDenied;

            u64 total = sizeof(PtpUsbBulkContainer) + o->size;
            /* Double-buffered, the pull-side mirror of SendObject's push-side
             * overlap: post the USB write for the chunk just read, then read the
             * next chunk off SD while that write is still in flight, and only
             * then reap it. Serializing read-then-write-then-read is what halves
             * throughput on the push side; the same fix applies here. */
            u8  *bufs[2] = { c->io, c->io2 };
            int  cur     = 0;

            PtpUsbBulkContainer *h = reinterpret_cast<PtpUsbBulkContainer *>(bufs[cur]);
            h->length   = (total > 0xFFFFFFFFull) ? 0xFFFFFFFFu : static_cast<u32>(total);
            h->type     = PtpContainerType_Data;
            h->code     = cmd->code;
            h->trans_id = cmd->trans_id;

            size_t first = fread(bufs[cur] + sizeof(PtpUsbBulkContainer), 1,
                                 IoCap - sizeof(PtpUsbBulkContainer), f);
            u32 curlen = static_cast<u32>(sizeof(PtpUsbBulkContainer) + first);
            u64 sent   = first;

            u32  urb    = 0;
            bool ok     = true;
            bool posted = WritePost(c, bufs[cur], curlen, std::addressof(urb));
            if (!posted) ok = false;

            // Reap-driven, same shape as SendObject's read loop: each iteration
            // reaps exactly the one write it posted last time, so a failure or a
            // short read can never leave (or double-drain) an in-flight urb.
            while (posted) {
                bool   more  = sent < o->size;
                int    nxt   = cur ^ 1;
                size_t chunk = more ? fread(bufs[nxt], 1, IoCap, f) : 0;

                if (!WriteReap(c, urb)) { ok = false; break; }
                if (!more || chunk == 0) break; /* done, or file shrank/hit EOF early */
                sent += chunk;

                posted = WritePost(c, bufs[nxt], static_cast<u32>(chunk), std::addressof(urb));
                if (!posted) ok = false;
                cur = nxt;
            }

            fclose(f);
            return ok ? PtpResponseCode_Ok : PtpResponseCode_IncompleteTransfer;
        }

        /* --- dispatch ----------------------------------------------------- */

        void Dispatch(Ctx *c, PtpUsbBulkContainer *cmd, u32 *p, u32 np) {
            u32 trans = cmd->trans_id;

            /* Operations allowed before a session is open. */
            switch (cmd->code) {
                case PtpOperationCode_GetDeviceInfo: {
                    Buf b = DataBuf(c);
                    BuildDeviceInfo(&b);
                    if (SendData(c, cmd->code, trans, &b)) RC(c, PtpResponseCode_Ok, trans);
                    return;
                }
                case PtpOperationCode_OpenSession: {
                    if (np < 1)          { RC(c, PtpResponseCode_InvalidParameter, trans); return; }
                    if (c->session_open) { RC(c, PtpResponseCode_SessionAlreadyOpen, trans); return; }
                    c->session_open = true;
                    c->session_id   = p[0];
                    c->objs.clear();
                    RC(c, PtpResponseCode_Ok, trans);
                    return;
                }
                default: break;
            }

            if (!c->session_open) { RC(c, PtpResponseCode_SessionNotOpen, trans); return; }

            switch (cmd->code) {
                case PtpOperationCode_CloseSession: {
                    c->session_open = false;
                    c->objs.clear();
                    RC(c, PtpResponseCode_Ok, trans);
                    return;
                }
                case PtpOperationCode_GetStorageIds: {
                    Buf b = DataBuf(c);
                    bU32(&b, 1);
                    bU32(&b, StorageId);
                    if (SendData(c, cmd->code, trans, &b)) RC(c, PtpResponseCode_Ok, trans);
                    return;
                }
                case PtpOperationCode_GetStorageInfo: {
                    u64 total = fs_total_bytes(c->root);
                    u64 freeb = fs_free_bytes(c->root);
                    if (total == UINT64_MAX) total = 0;
                    if (freeb == UINT64_MAX) freeb = 0;
                    Buf b = DataBuf(c);
                    bU16(&b, PtpStorageType_FixedRam);
                    bU16(&b, PtpFilesystemType_GenericHierarchical);
                    bU16(&b, PtpAccessCapability_ReadWrite);
                    bU64(&b, total);
                    bU64(&b, freeb);
                    bU32(&b, 0xFFFFFFFF);          /* FreeSpaceInObjects: unknown */
                    bStr(&b, "HaulNX");            /* StorageDescription */
                    bStr(&b, "");                  /* VolumeIdentifier */
                    if (SendData(c, cmd->code, trans, &b)) RC(c, PtpResponseCode_Ok, trans);
                    return;
                }
                case PtpOperationCode_GetNumObjects: {
                    std::vector<u32> h;
                    u16 rc = CollectChildren(c, np >= 3 ? p[2] : PtpRootParentObject, h);
                    if (rc != PtpResponseCode_Ok) { RC(c, rc, trans); return; }
                    u32 cnt = static_cast<u32>(h.size());
                    SendResponse(c, PtpResponseCode_Ok, trans, &cnt, 1);
                    return;
                }
                case PtpOperationCode_GetObjectHandles: {
                    std::vector<u32> h;
                    u16 rc = CollectChildren(c, np >= 3 ? p[2] : PtpRootParentObject, h);
                    if (rc != PtpResponseCode_Ok) { RC(c, rc, trans); return; }
                    Buf b = DataBuf(c);
                    bU32(&b, static_cast<u32>(h.size()));
                    for (u32 x : h) bU32(&b, x);
                    if (SendData(c, cmd->code, trans, &b)) RC(c, PtpResponseCode_Ok, trans);
                    return;
                }
                case PtpOperationCode_GetObjectInfo: {
                    if (np < 1) { RC(c, PtpResponseCode_InvalidParameter, trans); return; }
                    Obj *o = FindHandle(c, p[0]);
                    if (!o) { RC(c, PtpResponseCode_InvalidObjectHandle, trans); return; }
                    Buf b = DataBuf(c);
                    BuildObjectInfo(o, &b);
                    if (SendData(c, cmd->code, trans, &b)) RC(c, PtpResponseCode_Ok, trans);
                    return;
                }
                case PtpOperationCode_GetObject: {
                    u16 rc = OpGetObject(c, cmd, p, np);
                    if (!c->stopping) RC(c, rc, trans);
                    return;
                }
                case PtpOperationCode_GetObjectPropsSupported: {
                    static const u16 props[] = {
                        PtpObjectPropertyCode_StorageId, PtpObjectPropertyCode_ObjectFormat,
                        PtpObjectPropertyCode_ProtectionStatus, PtpObjectPropertyCode_ObjectSize,
                        PtpObjectPropertyCode_ObjectFileName, PtpObjectPropertyCode_ParentObject,
                        PtpObjectPropertyCode_PersistentUniqueObjectIdentifier,
                        PtpObjectPropertyCode_Name, PtpObjectPropertyCode_DateModified,
                    };
                    Buf b = DataBuf(c);
                    bU32(&b, sizeof(props) / sizeof(props[0]));
                    for (u16 pc : props) bU16(&b, pc);
                    if (SendData(c, cmd->code, trans, &b)) RC(c, PtpResponseCode_Ok, trans);
                    return;
                }
                case PtpOperationCode_GetObjectPropDesc: {
                    if (np < 1) { RC(c, PtpResponseCode_InvalidParameter, trans); return; }
                    u16 pc = static_cast<u16>(p[0]);
                    u16 dt = PropDataType(pc);
                    if (dt == 0) { RC(c, PtpResponseCode_ObjectPropNotSupported, trans); return; }
                    Buf b = DataBuf(c);
                    bU16(&b, pc);
                    bU16(&b, dt);
                    /* Filename/Name are writable so a host can rename in place
                     * (MTP SetObjectPropValue); everything else is read-only. */
                    bU8(&b, (pc == PtpObjectPropertyCode_ObjectFileName ||
                             pc == PtpObjectPropertyCode_Name)
                                ? PtpGetSetFlag_GetSet : PtpGetSetFlag_Get);
                    switch (dt) {                  /* default value */
                        case PtpDataTypeCode_String: bStr(&b, ""); break;
                        case PtpDataTypeCode_U16:    bU16(&b, 0); break;
                        case PtpDataTypeCode_U32:    bU32(&b, 0); break;
                        case PtpDataTypeCode_U64:    bU64(&b, 0); break;
                        case PtpDataTypeCode_U128:   bU128(&b, 0, 0); break;
                    }
                    bU32(&b, 0);                   /* GroupCode */
                    bU8(&b, 0);                    /* FormFlag: none */
                    if (SendData(c, cmd->code, trans, &b)) RC(c, PtpResponseCode_Ok, trans);
                    return;
                }
                case PtpOperationCode_GetObjectPropValue: {
                    if (np < 2) { RC(c, PtpResponseCode_InvalidParameter, trans); return; }
                    Obj *o = FindHandle(c, p[0]);
                    if (!o) { RC(c, PtpResponseCode_InvalidObjectHandle, trans); return; }
                    u16 pc = static_cast<u16>(p[1]);
                    if (PropDataType(pc) == 0) { RC(c, PtpResponseCode_ObjectPropNotSupported, trans); return; }
                    Buf b = DataBuf(c);
                    WritePropValue(o, pc, &b);
                    if (SendData(c, cmd->code, trans, &b)) RC(c, PtpResponseCode_Ok, trans);
                    return;
                }
                case PtpOperationCode_SendObjectInfo: {
                    if (np < 2) { RC(c, PtpResponseCode_InvalidParameter, trans); return; }

                    /* Data-out phase: the host sends the ObjectInfo dataset. */
                    u32 n = 0;
                    if (!ReadData(c, c->io, IoCap, &n)) return; /* stop / drop */
                    if (n < sizeof(PtpUsbBulkContainer) + 52) { RC(c, PtpResponseCode_NoValidObjectInfo, trans); return; }
                    PtpUsbBulkContainer dh;
                    memcpy(&dh, c->io, sizeof(dh));
                    if (dh.type != PtpContainerType_Data) { RC(c, PtpResponseCode_GeneralError, trans); return; }

                    const u8 *d   = c->io + sizeof(PtpUsbBulkContainer);
                    const u8 *end = c->io + n;
                    u16 format;
                    u32 csize;
                    memcpy(&format, d + 4, 2);
                    memcpy(&csize,  d + 8, 4);
                    const u8 *sp = d + 52;         /* Filename offset in the dataset */
                    char fname[256];               /* Obj.name caps at this anyway */
                    ParseStr(&sp, end, fname, sizeof(fname));
                    /* One path segment only -- fname is concatenated straight into
                     * the destination path below ("%s/%s", pdir, fname). Without
                     * this, a host sending "../../../whatever" walks straight out
                     * of pdir (a console folder, Inbox, or root) to write or mkdir
                     * anywhere reachable by traversal, regardless of the SD Card
                     * toggle. Mirrors the same guard SetObjectPropValue's rename
                     * already applies to newname. */
                    if (!fname[0] || strchr(fname, '/') || strchr(fname, '\\')) {
                        RC(c, PtpResponseCode_NoValidObjectInfo, trans); return;
                    }

                    /* Resolve the parent directory. */
                    u32 parentReq = p[1];
                    u32 pnorm;
                    const char *pdir;
                    if (parentReq == 0 || parentReq == PtpRootParentObject) {
                        pnorm = PtpRootParentObject;
                        pdir  = c->root;
                    } else {
                        Obj *po = FindHandle(c, parentReq);
                        if (!po || !po->is_dir) { RC(c, PtpResponseCode_InvalidParentObject, trans); return; }
                        pnorm = parentReq;
                        pdir  = po->path;
                    }

                    char full[1040];
                    snprintf(full, sizeof(full), "%s/%s", pdir, fname);

                    if (format == PtpObjectFormatCode_Association) {
                        fs_mkdir_p(full);
                        u32 h = AddOrFind(c, pnorm, full, fname, true, 0, time(nullptr));
                        c->pending.active = false;
                        u32 rp[3] = { StorageId, parentReq, h };
                        SendResponse(c, PtpResponseCode_Ok, trans, rp, 3);
                        return;
                    }

                    /* A companion pushing the shared update manifest at the root:
                     * capture it to a staging file so a malformed push can't clobber
                     * the live manifest, then validate + apply it on completion (see
                     * SendObject). This is the USB mirror of the Wi-Fi InvApplyPush. */
                    bool is_updsrc = (pnorm == PtpRootParentObject) &&
                                     strcmp(fname, "update_sources.json") == 0;
                    if (is_updsrc)
                        snprintf(full, sizeof(full), "%s.mtp", UPDSRC_PATH);

                    u32 h = AddOrFind(c, pnorm, full, fname, false, csize, time(nullptr));
                    c->pending.active = true;
                    c->pending.apply_updsrc = is_updsrc;
                    /* Unpack archives dropped straight into a console folder,
                     * mirroring the download install step. Inbox drops are left
                     * whole so the sorter can identify them first; root drops
                     * have no console context. UnderKnownFolder additionally
                     * requires the destination to actually be inside one of
                     * the real console folders (not just "some folder that
                     * isn't Inbox or root") -- otherwise a drop anywhere else
                     * reachable generically (the SD Card tab's whole-card
                     * tree, or a nested Inbox subfolder) would wrongly
                     * auto-extract too. */
                    c->pending.extract = is_archive_name(fname) &&
                                         pnorm != PtpRootParentObject &&
                                         strcmp(pdir, c->inbox) != 0 &&
                                         UnderKnownFolder(c, full);
                    c->pending.handle = h;
                    c->pending.size   = csize;
                    snprintf(c->pending.path, sizeof(c->pending.path), "%s", full);
                    /* List the file now, at announce time, so it shows on the
                     * connect screen before its data phase starts, badged with
                     * the destination console (empty for Inbox / root). */
                    XferBegin(fname, csize, ConsoleForPath(c, full));
                    u32 rp[3] = { StorageId, parentReq, h };
                    SendResponse(c, PtpResponseCode_Ok, trans, rp, 3);
                    return;
                }
                case PtpOperationCode_SendObject: {
                    if (!c->pending.active) { RC(c, PtpResponseCode_NoValidObjectInfo, trans); return; }
                    // Bracket every push with a start/end line (see the matching
                    // one below): cheap enough per-file, and it's what lets a
                    // reported stall -- desktop stuck mid-write, device shows no
                    // error -- be told apart from "the responder thread never
                    // got back here at all" vs "it finished fine and something
                    // downstream of the ack is what actually hung".
                    mtp_log("usb        recv start %s (%llu bytes declared)",
                             c->pending.path, (unsigned long long)c->pending.size);

                    FILE *f = fopen(c->pending.path, "wb");
                    if (!f) { c->pending.active = false; XferEnd(false); RC(c, PtpResponseCode_AccessDenied, trans); return; }

                    u64  expected  = c->pending.size;
                    u64  written   = 0;
                    bool ok        = true;
                    /* Double-buffered: while one chunk is written to the SD card,
                     * the next is already being pulled over USB. Serializing the
                     * two (read, then write, then read) roughly halves throughput;
                     * overlapping them is what closes the gap to DBI's speed. */
                    u8  *bufs[2] = { c->io, c->io2 };
                    int  cur     = 0;

                    /* Prime with the header-bearing first chunk, skipping any stray
                     * short/zero packets the host emits before the data phase. */
                    u32 n = 0;
                    for (;;) {
                        if (!ReadData(c, bufs[cur], IoCap, &n)) { ok = false; break; }
                        if (n >= sizeof(PtpUsbBulkContainer)) break;
                    }

                    const u8 *data = nullptr;
                    u32       len  = 0;
                    /* Size of the most recent USB read. When the final read comes
                     * back exactly full (== IoCap), the host's data phase ended on
                     * a packet boundary and a terminating zero-length packet is
                     * still queued on the OUT pipe -- see the drain below. */
                    u32       last_read = n;
                    if (ok) {
                        PtpUsbBulkContainer dh;
                        memcpy(&dh, bufs[cur], sizeof(dh));
                        if (dh.type != PtpContainerType_Data) {
                            ok = false;
                        } else {
                            if (dh.length != 0xFFFFFFFFu && dh.length >= sizeof(PtpUsbBulkContainer))
                                expected = dh.length - sizeof(PtpUsbBulkContainer);
                            data = bufs[cur] + sizeof(PtpUsbBulkContainer);
                            len  = n - sizeof(PtpUsbBulkContainer);
                        }
                    }

                    /* Preallocate the file to its final size so exFAT lays down
                     * the whole cluster chain in one metadata pass instead of
                     * extending it on every write. Growing cluster-by-cluster is
                     * what pins throughput near ~25 MB/s regardless of buffering;
                     * this is the write-side counterpart to the double buffer and
                     * the bigger win on SD. Harmless if it fails. */
                    if (ok && expected > 0) ftruncate(fileno(f), static_cast<off_t>(expected));

                    bool cancelled = false;
                    while (ok) {
                        /* Kick off the next read (into the other buffer) before
                         * spending time on the SD write, so they overlap. Only
                         * post when more data is genuinely coming, else we'd block
                         * on an endpoint with nothing queued. */
                        bool more   = (written + len < expected) && (len != 0);
                        int  nxt    = cur ^ 1;
                        u32  nurb   = 0;
                        bool posted = more && ReadPost(c, bufs[nxt], IoCap, &nurb);
                        if (more && !posted) ok = false;

                        /* A cancel from the Queue tab: keep reading (so the bulk
                         * pipe stays in sync for the next command -- abandoning the
                         * host's remaining announced bytes mid-stream would desync
                         * every transfer after this one) but stop writing them to
                         * disk; the drained file is dropped by the existing !ok
                         * cleanup below. Cheaper than what a real disconnect costs
                         * (full USB re-enumeration), and matches how cancelling a
                         * copy in Windows Explorer behaves -- the link stays up. */
                        if (!cancelled && XferCancelRequested()) cancelled = true;

                        if (ok && !cancelled && len && fwrite(data, 1, len, f) != len) ok = false;
                        written += len;
                        if (!cancelled) XferUpdate(written);

                        if (!posted) break;

                        u32 got = 0;
                        if (!ReadReap(c, nurb, &got)) { ok = false; break; }
                        if (!ok) break;   /* SD write failed while this read flew */
                        cur  = nxt;
                        data = bufs[cur];
                        len  = got;
                        last_read = got;
                    }
                    if (cancelled) ok = false; /* drained clean; still fails the transfer */
                    /* Trim the preallocated tail if the transfer ended short. */
                    if (ok && written != expected) { fflush(f); ftruncate(fileno(f), static_cast<off_t>(written)); }
                    fclose(f);

                    /* An incomplete write (cancel, disconnect, or short read) is
                     * garbage — MTP has no resume, so the partial can only linger
                     * in the Inbox as a phantom entry after the user cancels.
                     * Drop it and orphan the DB entry so re-enumeration is clean. */
                    if (!ok) {
                        remove(c->pending.path);
                        Obj *o = FindHandle(c, c->pending.handle);
                        if (o) o->parent = 0xFFFFFFFEu;
                        if (!c->stopping) RC(c, PtpResponseCode_IncompleteTransfer, trans);
                        XferEnd(false);
                        c->pending.active = false;
                        return;
                    }

                    /* Refresh the DB entry with what actually landed on disk. */
                    struct stat st;
                    bool have_stat = (stat(c->pending.path, &st) == 0);
                    if (have_stat) {
                        Obj *o = FindHandle(c, c->pending.handle);
                        if (o) { o->size = static_cast<u64>(st.st_size); o->mtime = st.st_mtime; }
                    }

                    /* A pushed update manifest: validate the staged file and only
                     * promote it to the live path if it parses as a sources list,
                     * so a bad push can't wipe the working manifest. Reuses the
                     * same parser the Wi-Fi push path (InvApplyPush) uses. */
                    if (c->pending.apply_updsrc) {
                        bool applied = false;
                        FILE *sf = fopen(c->pending.path, "rb");
                        if (sf) {
                            fseek(sf, 0, SEEK_END);
                            long sz = ftell(sf);
                            fseek(sf, 0, SEEK_SET);
                            if (sz > 0 && sz < 256 * 1024) {
                                char *buf = static_cast<char *>(malloc(static_cast<size_t>(sz) + 1));
                                if (buf && fread(buf, 1, static_cast<size_t>(sz), sf) == static_cast<size_t>(sz)) {
                                    buf[sz] = '\0';
                                    // static: ~24KB, kept off the worker thread's
                                    // stack; only this single responder thread uses it.
                                    static UpdSource tmp[UPD_MAX];
                                    int nrows = updman_parse_buf(buf, static_cast<size_t>(sz), tmp, UPD_MAX);
                                    if (nrows > 0) applied = updman_save(tmp, nrows);
                                }
                                free(buf);
                            }
                            fclose(sf);
                        }
                        remove(c->pending.path); /* drop the staging file either way */
                        Obj *o = FindHandle(c, c->pending.handle);
                        if (o) o->parent = 0xFFFFFFFEu; /* orphan the staging entry */
                        if (!c->stopping)
                            RC(c, applied ? PtpResponseCode_Ok : PtpResponseCode_GeneralError, trans);
                        XferEnd(applied);
                        c->pending.active = false;
                        return;
                    }

                    bool do_extract = ok && !c->stopping && c->pending.extract;

                    mtp_log("usb        recv %s %s: %llu/%llu bytes, last_read=%u, acking now%s",
                             ok ? "done" : "failed", c->pending.path,
                             (unsigned long long)written, (unsigned long long)expected,
                             last_read, do_extract ? " (will extract)" : "");

                    /* Ack the transfer, then hand any unpack off to the background
                     * extract worker and return to the command loop immediately.
                     * Extracting inline here would freeze this single responder
                     * thread for the whole unpack (tens of seconds), and WPD's
                     * Commit fires a follow-up command it blocks on -- so the
                     * desktop sat at ~99% for the entire unzip, unable to send
                     * anything else, until the loop finally came back. Off-thread
                     * extraction keeps the MTP command loop responsive; the worker
                     * drives this transfer's row to Extracting/Done by id. Any
                     * end-of-data-phase terminator the host left is now drained by
                     * the very next command-loop read (instant), not stuck behind
                     * the unpack. */
                    if (!c->stopping) RC(c, ok ? PtpResponseCode_Ok : PtpResponseCode_IncompleteTransfer, trans);

                    if (do_extract) {
                        EnqueueExtract(c->pending.path,
                                       have_stat ? static_cast<u64>(st.st_size)
                                                 : c->pending.size);
                    } else {
                        XferEnd(ok);
                    }
                    c->pending.active = false;
                    return;
                }
                case PtpOperationCode_DeleteObject: {
                    if (np < 1) { RC(c, PtpResponseCode_InvalidParameter, trans); return; }
                    Obj *o = FindHandle(c, p[0]);
                    if (!o) { RC(c, PtpResponseCode_InvalidObjectHandle, trans); return; }
                    if (IsSdRoot(c, o->path)) { RC(c, PtpResponseCode_AccessDenied, trans); return; }
                    /* A folder can be huge (a whole console's ROMs, or
                     * anywhere on the SD Card tree) -- recursing fs_rm_rf
                     * inline here would freeze this command loop for the
                     * whole delete, the same shape EnqueueExtract exists to
                     * avoid for a received archive's unpack. Orphan and ack
                     * now; a background worker does the actual removal (see
                     * EnqueueDelete), so a big delete never blocks the next
                     * command or the USB link. */
                    o->parent = 0xFFFFFFFEu; /* orphan it; re-enumeration rebuilds from disk */
                    RC(c, PtpResponseCode_Ok, trans);
                    EnqueueDelete(o->path);
                    return;
                }
                case PtpOperationCode_SetObjectPropValue: {
                    /* Rename in place: the host sets ObjectFileName/Name with the
                     * new basename. Data-out phase carries the string. */
                    if (np < 2) { RC(c, PtpResponseCode_InvalidParameter, trans); return; }
                    Obj *o = FindHandle(c, p[0]);
                    if (!o) { RC(c, PtpResponseCode_InvalidObjectHandle, trans); return; }
                    if (IsSdRoot(c, o->path)) { RC(c, PtpResponseCode_AccessDenied, trans); return; }
                    u16 pc = static_cast<u16>(p[1]);
                    if (pc != PtpObjectPropertyCode_ObjectFileName &&
                        pc != PtpObjectPropertyCode_Name) {
                        RC(c, PtpResponseCode_ObjectPropNotSupported, trans); return;
                    }
                    u32 n = 0;
                    if (!ReadData(c, c->io, IoCap, &n)) return; /* stop / drop */
                    if (n < sizeof(PtpUsbBulkContainer)) { RC(c, PtpResponseCode_GeneralError, trans); return; }
                    const u8 *d   = c->io + sizeof(PtpUsbBulkContainer);
                    const u8 *end = c->io + n;
                    char newname[256];             /* Obj.name caps at this anyway */
                    ParseStr(&d, end, newname, sizeof(newname));
                    /* MTP 'Name' (0xDC44) is the extension-less friendly name, not
                     * the filesystem name. Windows sets it right after an Explorer
                     * copy; honouring it as a rename strips the file's extension
                     * (e.g. ".cci"). Ack it but leave the on-disk file alone — only
                     * ObjectFileName (0xDC07) renames the file. */
                    if (pc == PtpObjectPropertyCode_Name) { RC(c, PtpResponseCode_Ok, trans); return; }
                    /* One path segment only — no traversal, no folder change. */
                    if (!newname[0] || strchr(newname, '/') || strchr(newname, '\\')) {
                        RC(c, PtpResponseCode_InvalidObjectPropFormat, trans); return;
                    }
                    char dir[768];
                    snprintf(dir, sizeof(dir), "%s", o->path);
                    char *slash = strrchr(dir, '/');
                    if (!slash) { RC(c, PtpResponseCode_GeneralError, trans); return; }
                    *slash = '\0';
                    char newpath[1040];
                    snprintf(newpath, sizeof(newpath), "%s/%s", dir, newname);
                    struct stat exists_st;
                    if (stat(newpath, &exists_st) == 0) { RC(c, PtpResponseCode_AccessDenied, trans); return; }
                    if (rename(o->path, newpath) != 0) { RC(c, PtpResponseCode_GeneralError, trans); return; }
                    SetObjPath(o, newpath);
                    snprintf(o->name, sizeof(o->name), "%s", newname);
                    RC(c, PtpResponseCode_Ok, trans);
                    return;
                }
                case PtpOperationCode_MoveObject: {
                    /* Move a file between managed folders. Params: object handle,
                     * destination StorageID (single storage — ignored), new parent
                     * handle (a console folder, the Inbox, or the storage root). */
                    if (np < 3) { RC(c, PtpResponseCode_InvalidParameter, trans); return; }
                    Obj *o = FindHandle(c, p[0]);
                    if (!o) { RC(c, PtpResponseCode_InvalidObjectHandle, trans); return; }
                    if (IsSdRoot(c, o->path)) { RC(c, PtpResponseCode_AccessDenied, trans); return; }
                    u32 destParent = p[2];
                    const char *ddir;
                    u32 dnorm;
                    if (destParent == 0 || destParent == PtpRootParentObject) {
                        dnorm = PtpRootParentObject; ddir = c->root;
                    } else {
                        Obj *dp = FindHandle(c, destParent);
                        if (!dp || !dp->is_dir) { RC(c, PtpResponseCode_InvalidParentObject, trans); return; }
                        dnorm = destParent; ddir = dp->path;
                    }
                    char newpath[1040];
                    snprintf(newpath, sizeof(newpath), "%s/%s", ddir, o->name);
                    struct stat exists_st;
                    if (stat(newpath, &exists_st) == 0) { RC(c, PtpResponseCode_AccessDenied, trans); return; }
                    if (rename(o->path, newpath) != 0) { RC(c, PtpResponseCode_GeneralError, trans); return; }
                    SetObjPath(o, newpath);
                    o->parent = dnorm;
                    RC(c, PtpResponseCode_Ok, trans);
                    return;
                }
                default:
                    RC(c, PtpResponseCode_OperationNotSupported, trans);
                    return;
            }
        }

    } // namespace

    void RunResponder(UsbSession *session, UEvent *stop, const char *root,
                      const Folder *folders, int nfolders, const char *inbox,
                      const char *sd_root) {
        Ctx c{};
        c.session  = session;
        c.stop     = stop;
        c.root     = root;
        c.folders  = folders;
        c.nfolders = nfolders;
        c.inbox    = inbox;
        c.sd_root  = (sd_root && sd_root[0]) ? sd_root : nullptr;

        c.io  = static_cast<u8 *>(memalign(0x1000, IoCap));
        c.io2 = static_cast<u8 *>(memalign(0x1000, IoCap));
        if (!c.io || !c.io2) { free(c.io); free(c.io2); return; }

        fs_mkdir_p(inbox); /* so the synthetic Inbox object resolves to a real dir */
        for (int i = 0; i < nfolders; i++) fs_mkdir_p(folders[i].path); /* incl. custom paths */

        for (;;) {
            u32 n = 0;
            if (!ReadData(&c, c.io, IoCap, &n)) break;      /* stop or link drop */
            if (n < sizeof(PtpUsbBulkContainer)) {
                /* A short/zero read here is a stray end-of-data-phase terminator
                 * the host left on the pipe. Timestamped so a big gap after a
                 * "recv done" pins the desktop's ~99% stall to it (see the drain
                 * note in SendObject): if this fires right after every push, the
                 * terminator is unconditional and the drain can be too. */
                mtp_log("usb        stray %u-byte packet drained at command loop", n);
                continue;
            }

            PtpUsbBulkContainer cmd;
            memcpy(&cmd, c.io, sizeof(cmd));
            if (cmd.type != PtpContainerType_Command) continue;

            u32 np = (n > sizeof(PtpUsbBulkContainer)) ? (n - sizeof(PtpUsbBulkContainer)) / 4 : 0;
            if (np > 5) np = 5;
            u32 params[5] = { 0 };
            memcpy(params, c.io + sizeof(PtpUsbBulkContainer), np * 4);

            Dispatch(&c, &cmd, params, np);
            if (c.stopping) break;
        }

        free(c.io);
        free(c.io2);
    }

}
