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
 * Runs the PTP/MTP command loop over a configured UsbSession, exposing the ROM
 * library as a single MTP storage: the console folders under the ROM root plus
 * a synthetic "Inbox" folder (the PC drop point). Object handles are assigned
 * lazily as the host enumerates and kept stable for the life of the session.
 *
 * The container framing (length/type/code/transaction-id header, init/send/
 * receive helpers, 5-parameter command cap) follows the same shape used by
 * cmtp-responder's ptp_container.c/mtp_cmd_handler.c (Apache-2.0) — a
 * reference implementation whose protocol layer is already cleanly separated
 * from its own (Linux FunctionFS) transport, which is what makes it usable as
 * a layout reference here without dragging in anything platform-specific. The
 * object-database model, per-console/Inbox folder mapping, and the transfer-
 * progress plumbing below are HaulNX's own.
 */
#pragma once
#include <switch.h>
#include <mtp/usb_session.hpp>
#include <mtp/mtp.h>

namespace mtp {

    /* Run the command loop until *stop* is signalled or the USB link drops.
     * root    = resolved ROM library root (used for storage free/total).
     * folders = console folders surfaced at the storage root (name shown, path
     *           backing). inbox = staging folder surfaced as "Inbox".
     * sd_root = optional extra top-level "SD Card" folder over the whole
     *           card (see mtp::Start); NULL/"" omits it. */
    void RunResponder(UsbSession *session, UEvent *stop, const char *root,
                      const Folder *folders, int nfolders, const char *inbox,
                      const char *sd_root = nullptr);

}
