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
 * Brings up a still-image/PTP class USB interface (class 0x06, subclass 0x01,
 * protocol 0x01) directly on libnx's `usbds` service — the values and call
 * sequence below are what that service's own public API requires for a
 * bulk in/out + interrupt-in device-mode interface; there's no vendor-specific
 * behavior here, and no other project's code was consulted to write it.
 */
#include <mtp/usb_session.hpp>

namespace mtp {

    namespace {

        constexpr const u32 DefaultInterfaceNumber = 0;

        /* MTP does not require a real serial, and reading set:sys here would add
         * a service dependency to the transport. A fixed string is enough for
         * the host to enumerate the device. */
        const char *GetSerialNumber() {
            return "HaulNX00000000001";
        }

    }

    Result UsbSession::InitializeLegacy(const UsbInterfaceInfo *info) {
        struct usb_interface_descriptor interface_descriptor = {
            .bLength            = USB_DT_INTERFACE_SIZE,
            .bDescriptorType    = USB_DT_INTERFACE,
            .bInterfaceNumber   = DefaultInterfaceNumber,
            .bInterfaceClass    = info->bInterfaceClass,
            .bInterfaceSubClass = info->bInterfaceSubClass,
            .bInterfaceProtocol = info->bInterfaceProtocol,
        };

        struct usb_endpoint_descriptor endpoint_descriptor_in = {
            .bLength          = USB_DT_ENDPOINT_SIZE,
            .bDescriptorType  = USB_DT_ENDPOINT,
            .bEndpointAddress = USB_ENDPOINT_IN,
            .bmAttributes     = USB_TRANSFER_TYPE_BULK,
            .wMaxPacketSize   = PtpUsbBulkHighSpeedMaxPacketLength,
        };

        struct usb_endpoint_descriptor endpoint_descriptor_out = {
            .bLength          = USB_DT_ENDPOINT_SIZE,
            .bDescriptorType  = USB_DT_ENDPOINT,
            .bEndpointAddress = USB_ENDPOINT_OUT,
            .bmAttributes     = USB_TRANSFER_TYPE_BULK,
            .wMaxPacketSize   = PtpUsbBulkHighSpeedMaxPacketLength,
        };

        struct usb_endpoint_descriptor endpoint_descriptor_interrupt = {
            .bLength          = USB_DT_ENDPOINT_SIZE,
            .bDescriptorType  = USB_DT_ENDPOINT,
            .bEndpointAddress = USB_ENDPOINT_IN,
            .bmAttributes     = USB_TRANSFER_TYPE_INTERRUPT,
            .wMaxPacketSize   = 0x18,
            .bInterval        = 0x4,
        };

        Result rc;

        /* Set up interface. */
        rc = usbDsGetDsInterface(std::addressof(m_interface), std::addressof(interface_descriptor), "usb");
        if (R_FAILED(rc)) return rc;

        /* Set up endpoints. */
        rc = usbDsInterface_GetDsEndpoint(m_interface, std::addressof(m_endpoints[UsbSessionEndpoint_Write]), std::addressof(endpoint_descriptor_in));
        if (R_FAILED(rc)) return rc;
        rc = usbDsInterface_GetDsEndpoint(m_interface, std::addressof(m_endpoints[UsbSessionEndpoint_Read]), std::addressof(endpoint_descriptor_out));
        if (R_FAILED(rc)) return rc;
        rc = usbDsInterface_GetDsEndpoint(m_interface, std::addressof(m_endpoints[UsbSessionEndpoint_Interrupt]), std::addressof(endpoint_descriptor_interrupt));
        if (R_FAILED(rc)) return rc;

        return usbDsInterface_EnableInterface(m_interface);
    }

    Result UsbSession::InitializeModern(const UsbInterfaceInfo *info) {
        struct usb_interface_descriptor interface_descriptor = {
            .bLength            = USB_DT_INTERFACE_SIZE,
            .bDescriptorType    = USB_DT_INTERFACE,
            .bInterfaceNumber   = DefaultInterfaceNumber,
            .bNumEndpoints      = 3,
            .bInterfaceClass    = info->bInterfaceClass,
            .bInterfaceSubClass = info->bInterfaceSubClass,
            .bInterfaceProtocol = info->bInterfaceProtocol,
        };

        struct usb_endpoint_descriptor endpoint_descriptor_in = {
            .bLength          = USB_DT_ENDPOINT_SIZE,
            .bDescriptorType  = USB_DT_ENDPOINT,
            .bEndpointAddress = USB_ENDPOINT_IN,
            .bmAttributes     = USB_TRANSFER_TYPE_BULK,
            .wMaxPacketSize   = PtpUsbBulkHighSpeedMaxPacketLength,
        };

        struct usb_endpoint_descriptor endpoint_descriptor_out = {
            .bLength          = USB_DT_ENDPOINT_SIZE,
            .bDescriptorType  = USB_DT_ENDPOINT,
            .bEndpointAddress = USB_ENDPOINT_OUT,
            .bmAttributes     = USB_TRANSFER_TYPE_BULK,
            .wMaxPacketSize   = PtpUsbBulkHighSpeedMaxPacketLength,
        };

        struct usb_endpoint_descriptor endpoint_descriptor_interrupt = {
            .bLength          = USB_DT_ENDPOINT_SIZE,
            .bDescriptorType  = USB_DT_ENDPOINT,
            .bEndpointAddress = USB_ENDPOINT_IN,
            .bmAttributes     = USB_TRANSFER_TYPE_INTERRUPT,
            .wMaxPacketSize   = 0x18,
            .bInterval        = 0x6,
        };

        struct usb_ss_endpoint_companion_descriptor endpoint_companion = {
            .bLength           = sizeof(struct usb_ss_endpoint_companion_descriptor),
            .bDescriptorType   = USB_DT_SS_ENDPOINT_COMPANION,
            .bMaxBurst         = 0x0f,
            .bmAttributes      = 0x00,
            .wBytesPerInterval = 0x00,
        };

        struct usb_ss_endpoint_companion_descriptor endpoint_companion_interrupt = {
            .bLength           = sizeof(struct usb_ss_endpoint_companion_descriptor),
            .bDescriptorType   = USB_DT_SS_ENDPOINT_COMPANION,
            .bMaxBurst         = 0x00,
            .bmAttributes      = 0x00,
            .wBytesPerInterval = 0x00,
        };

        Result rc;

        rc = usbDsRegisterInterface(std::addressof(m_interface));
        if (R_FAILED(rc)) return rc;

        u8 iInterface;
        rc = usbDsAddUsbStringDescriptor(std::addressof(iInterface), "MTP");
        if (R_FAILED(rc)) return rc;

        interface_descriptor.bInterfaceNumber = m_interface->interface_index;
        interface_descriptor.iInterface = iInterface;
        endpoint_descriptor_in.bEndpointAddress += interface_descriptor.bInterfaceNumber + 1;
        endpoint_descriptor_out.bEndpointAddress += interface_descriptor.bInterfaceNumber + 1;
        endpoint_descriptor_interrupt.bEndpointAddress += interface_descriptor.bInterfaceNumber + 2;

        /* High speed config. */
        rc = usbDsInterface_AppendConfigurationData(m_interface, UsbDeviceSpeed_High, std::addressof(interface_descriptor), USB_DT_INTERFACE_SIZE);
        if (R_FAILED(rc)) return rc;
        rc = usbDsInterface_AppendConfigurationData(m_interface, UsbDeviceSpeed_High, std::addressof(endpoint_descriptor_in), USB_DT_ENDPOINT_SIZE);
        if (R_FAILED(rc)) return rc;
        rc = usbDsInterface_AppendConfigurationData(m_interface, UsbDeviceSpeed_High, std::addressof(endpoint_descriptor_out), USB_DT_ENDPOINT_SIZE);
        if (R_FAILED(rc)) return rc;
        rc = usbDsInterface_AppendConfigurationData(m_interface, UsbDeviceSpeed_High, std::addressof(endpoint_descriptor_interrupt), USB_DT_ENDPOINT_SIZE);
        if (R_FAILED(rc)) return rc;

        /* Super speed config. */
        endpoint_descriptor_in.wMaxPacketSize  = PtpUsbBulkSuperSpeedMaxPacketLength;
        endpoint_descriptor_out.wMaxPacketSize = PtpUsbBulkSuperSpeedMaxPacketLength;
        rc = usbDsInterface_AppendConfigurationData(m_interface, UsbDeviceSpeed_Super, std::addressof(interface_descriptor), USB_DT_INTERFACE_SIZE);
        if (R_FAILED(rc)) return rc;
        rc = usbDsInterface_AppendConfigurationData(m_interface, UsbDeviceSpeed_Super, std::addressof(endpoint_descriptor_in), USB_DT_ENDPOINT_SIZE);
        if (R_FAILED(rc)) return rc;
        rc = usbDsInterface_AppendConfigurationData(m_interface, UsbDeviceSpeed_Super, std::addressof(endpoint_companion), USB_DT_SS_ENDPOINT_COMPANION_SIZE);
        if (R_FAILED(rc)) return rc;
        rc = usbDsInterface_AppendConfigurationData(m_interface, UsbDeviceSpeed_Super, std::addressof(endpoint_descriptor_out), USB_DT_ENDPOINT_SIZE);
        if (R_FAILED(rc)) return rc;
        rc = usbDsInterface_AppendConfigurationData(m_interface, UsbDeviceSpeed_Super, std::addressof(endpoint_companion), USB_DT_SS_ENDPOINT_COMPANION_SIZE);
        if (R_FAILED(rc)) return rc;
        rc = usbDsInterface_AppendConfigurationData(m_interface, UsbDeviceSpeed_Super, std::addressof(endpoint_descriptor_interrupt), USB_DT_ENDPOINT_SIZE);
        if (R_FAILED(rc)) return rc;
        rc = usbDsInterface_AppendConfigurationData(m_interface, UsbDeviceSpeed_Super, std::addressof(endpoint_companion_interrupt), USB_DT_SS_ENDPOINT_COMPANION_SIZE);
        if (R_FAILED(rc)) return rc;

        /* Set up endpoints. */
        rc = usbDsInterface_RegisterEndpoint(m_interface, std::addressof(m_endpoints[UsbSessionEndpoint_Write]), endpoint_descriptor_in.bEndpointAddress);
        if (R_FAILED(rc)) return rc;
        rc = usbDsInterface_RegisterEndpoint(m_interface, std::addressof(m_endpoints[UsbSessionEndpoint_Read]), endpoint_descriptor_out.bEndpointAddress);
        if (R_FAILED(rc)) return rc;
        rc = usbDsInterface_RegisterEndpoint(m_interface, std::addressof(m_endpoints[UsbSessionEndpoint_Interrupt]), endpoint_descriptor_interrupt.bEndpointAddress);
        if (R_FAILED(rc)) return rc;

        return usbDsInterface_EnableInterface(m_interface);
    }

    Result UsbSession::Initialize(const UsbInterfaceInfo *info, u16 id_vendor, u16 id_product) {
        Result rc = usbDsInitialize();
        if (R_FAILED(rc)) return rc;

        if (hosversionAtLeast(5, 0, 0)) {
            /* Report language as US English. */
            static const u16 supported_langs[1] = { 0x0409 };
            rc = usbDsAddUsbLanguageStringDescriptor(nullptr, supported_langs, 1);
            if (R_FAILED(rc)) return rc;

            /* Report strings. */
            u8 iManufacturer, iProduct, iSerialNumber;
            rc = usbDsAddUsbStringDescriptor(std::addressof(iManufacturer), "Nintendo");
            if (R_FAILED(rc)) return rc;
            rc = usbDsAddUsbStringDescriptor(std::addressof(iProduct), "Nintendo Switch");
            if (R_FAILED(rc)) return rc;
            rc = usbDsAddUsbStringDescriptor(std::addressof(iSerialNumber), GetSerialNumber());
            if (R_FAILED(rc)) return rc;

            /* Send device descriptors */
            struct usb_device_descriptor device_descriptor = {
                .bLength            = USB_DT_DEVICE_SIZE,
                .bDescriptorType    = USB_DT_DEVICE,
                .bcdUSB             = 0x0200,
                .bDeviceClass       = 0x00,
                .bDeviceSubClass    = 0x00,
                .bDeviceProtocol    = 0x00,
                .bMaxPacketSize0    = 0x40,
                .idVendor           = id_vendor,
                .idProduct          = id_product,
                .bcdDevice          = 0x0100,
                .iManufacturer      = iManufacturer,
                .iProduct           = iProduct,
                .iSerialNumber      = iSerialNumber,
                .bNumConfigurations = 0x01
            };
            rc = usbDsSetUsbDeviceDescriptor(UsbDeviceSpeed_High, std::addressof(device_descriptor));
            if (R_FAILED(rc)) return rc;

            device_descriptor.bcdUSB = 0x0300;
            device_descriptor.bMaxPacketSize0 = 0x09;
            rc = usbDsSetUsbDeviceDescriptor(UsbDeviceSpeed_Super, std::addressof(device_descriptor));
            if (R_FAILED(rc)) return rc;

            /* Binary Object Store: advertise both a USB 2.0 and a USB 3.0
             * device capability descriptor, per the BOS layout usbds expects. */
            u8 bos[0x16] = {
                0x05,       /* .bLength */
                USB_DT_BOS, /* .bDescriptorType */
                0x16, 0x00, /* .wTotalLength */
                0x02,       /* .bNumDeviceCaps */

                /* USB 2.0 */
                0x07,                     /* .bLength */
                USB_DT_DEVICE_CAPABILITY, /* .bDescriptorType */
                0x02,                     /* .bDevCapabilityType */
                0x02, 0x00, 0x00, 0x00,   /* .bmAttributes */

                /* USB 3.0 */
                0x0a,                     /* .bLength */
                USB_DT_DEVICE_CAPABILITY, /* .bDescriptorType */
                0x03,                     /* .bDevCapabilityType */
                0x00,                     /* .bmAttributes */
                0x0c, 0x00,               /* .wSpeedSupported */
                0x03,                     /* .bFunctionalitySupport */
                0x00,                     /* .bU1DevExitLat */
                0x00, 0x00                /* .bU2DevExitLat */
            };
            rc = usbDsSetBinaryObjectStore(bos, sizeof(bos));
            if (R_FAILED(rc)) return rc;

            rc = this->InitializeModern(info);
            if (R_FAILED(rc)) return rc;
            rc = usbDsEnable();
            if (R_FAILED(rc)) return rc;
        } else {
            rc = this->InitializeLegacy(info);
            if (R_FAILED(rc)) return rc;
        }

        return 0;
    }

    void UsbSession::Finalize() {
        usbDsExit();
    }

    bool UsbSession::GetConfigured() const {
        UsbState usb_state;

        if (R_FAILED(usbDsGetState(std::addressof(usb_state)))) {
            return false;
        }

        return usb_state == UsbState_Configured;
    }

    Event *UsbSession::GetCompletionEvent(UsbSessionEndpoint ep) const {
        return std::addressof(m_endpoints[ep]->CompletionEvent);
    }

    Result UsbSession::TransferAsync(UsbSessionEndpoint ep, void *buffer, u32 size, u32 *out_urb_id) {
        return usbDsEndpoint_PostBufferAsync(m_endpoints[ep], buffer, size, out_urb_id);
    }

    Result UsbSession::GetTransferResult(UsbSessionEndpoint ep, u32 urb_id, u32 *out_transferred_size) {
        UsbDsReportData report_data;

        Result rc = eventClear(std::addressof(m_endpoints[ep]->CompletionEvent));
        if (R_FAILED(rc)) return rc;
        rc = usbDsEndpoint_GetReportData(m_endpoints[ep], std::addressof(report_data));
        if (R_FAILED(rc)) return rc;
        return usbDsParseReportData(std::addressof(report_data), urb_id, nullptr, out_transferred_size);
    }

}
