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
 * USB device-mode session for the embedded MTP responder.
 *
 * A thin wrapper over libnx's own `usbds` device-mode API (nx/include/switch/
 * services/usbds.h) — nothing here is more than what that header's public
 * functions already do; this just groups the interface/endpoint handles the
 * PTP/MTP responder needs behind one small class.
 */
#pragma once
#include <switch.h>
#include <memory> // std::addressof
#include <mtp/ptp.hpp>

namespace mtp {

    enum UsbSessionEndpoint {
        UsbSessionEndpoint_Read      = 0,
        UsbSessionEndpoint_Write     = 1,
        UsbSessionEndpoint_Interrupt = 2,
        UsbSessionEndpoint_Count     = 3,
    };

    class UsbSession {
        private:
            UsbDsInterface *m_interface;
            UsbDsEndpoint *m_endpoints[UsbSessionEndpoint_Count];
        private:
            /* Pre-5.0.0 firmware: a single high-speed-only configuration. */
            Result InitializeLegacy(const UsbInterfaceInfo *info);
            /* 5.0.0+: dual High/Super speed configuration with SuperSpeed
             * endpoint-companion descriptors. */
            Result InitializeModern(const UsbInterfaceInfo *info);
        public:
            constexpr explicit UsbSession() : m_interface(), m_endpoints() { /* ... */ }

            Result Initialize(const UsbInterfaceInfo *info, u16 id_vendor, u16 id_product);
            void Finalize();

            bool GetConfigured() const;
            Event *GetCompletionEvent(UsbSessionEndpoint ep) const;
            Result TransferAsync(UsbSessionEndpoint ep, void *buffer, u32 size, u32 *out_urb_id);
            Result GetTransferResult(UsbSessionEndpoint ep, u32 urb_id, u32 *out_transferred_size);
    };

}
