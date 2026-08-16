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
 * PTP/MTP protocol constants.
 *
 * Numeric operation/response/format/property codes and container-header shape
 * are fixed by the published protocol: ISO 15740 (Picture Transfer Protocol)
 * plus Microsoft's MTP vendor extension. These values aren't anyone's
 * expression to own — cross-checked here against the equivalent tables in
 * cmtp-responder (github.com/cmtp-responder/cmtp-responder, Apache-2.0,
 * Samsung/Collabora) for completeness, trimmed to what this responder's
 * operation set (see mtp/responder.hpp) actually uses.
 */
#pragma once
#include <switch.h>

namespace mtp {

    /* Bulk endpoint packet sizes, by USB speed. */
    constexpr inline u32 PtpUsbBulkHighSpeedMaxPacketLength  = 0x200;
    constexpr inline u32 PtpUsbBulkSuperSpeedMaxPacketLength = 0x400;

    constexpr inline u32 PtpUsbBulkHeaderLength = 2 * sizeof(u32) + 2 * sizeof(u16);

    /* A VID:PID recognized by libmtp (Nintendo Switch), so desktop MTP stacks
     * pick the device up without a custom driver. */
    constexpr inline u16 SwitchMtpIdVendor  = 0x057e;
    constexpr inline u16 SwitchMtpIdProduct = 0x201d;

    /* Still-image (PTP) interface: class 0x06, subclass 0x01, protocol 0x01. */
    struct UsbInterfaceInfo {
        u8 bInterfaceClass;
        u8 bInterfaceSubClass;
        u8 bInterfaceProtocol;
    };

    constexpr inline UsbInterfaceInfo MtpInterfaceInfo = {
        0x06, 0x01, 0x01,
    };

    /* --- PTP/MTP wire protocol (PIMA 15740 + MTP extensions) ---------------
     * All multi-byte fields are little-endian, matching the Switch's ABI, so
     * the container and dataset structs below can be written/read as-is. */

    /* USB bulk container header (length, type, code, transaction id). */
    struct PtpUsbBulkContainer {
        u32 length;
        u16 type;
        u16 code;
        u32 trans_id;
    };

    enum PtpContainerType : u16 {
        PtpContainerType_Command  = 1,
        PtpContainerType_Data     = 2,
        PtpContainerType_Response = 3,
        PtpContainerType_Event    = 4,
    };

    /* Operations this responder implements. */
    enum PtpOperationCode : u16 {
        PtpOperationCode_GetDeviceInfo          = 0x1001,
        PtpOperationCode_OpenSession            = 0x1002,
        PtpOperationCode_CloseSession           = 0x1003,
        PtpOperationCode_GetStorageIds          = 0x1004,
        PtpOperationCode_GetStorageInfo         = 0x1005,
        PtpOperationCode_GetNumObjects          = 0x1006,
        PtpOperationCode_GetObjectHandles       = 0x1007,
        PtpOperationCode_GetObjectInfo          = 0x1008,
        PtpOperationCode_GetObject              = 0x1009,
        PtpOperationCode_DeleteObject           = 0x100B,
        PtpOperationCode_SendObjectInfo         = 0x100C,
        PtpOperationCode_SendObject             = 0x100D,
        PtpOperationCode_MoveObject             = 0x1019,
        PtpOperationCode_SetObjectPropValue     = 0x9804,
        PtpOperationCode_GetObjectPropsSupported = 0x9801,
        PtpOperationCode_GetObjectPropDesc      = 0x9802,
        PtpOperationCode_GetObjectPropValue     = 0x9803,
        PtpOperationCode_GetObjectPropList      = 0x9805,
    };

    enum PtpResponseCode : u16 {
        PtpResponseCode_Ok                          = 0x2001,
        PtpResponseCode_GeneralError                = 0x2002,
        PtpResponseCode_SessionNotOpen              = 0x2003,
        PtpResponseCode_InvalidTransactionId        = 0x2004,
        PtpResponseCode_OperationNotSupported       = 0x2005,
        PtpResponseCode_ParameterNotSupported       = 0x2006,
        PtpResponseCode_IncompleteTransfer          = 0x2007,
        PtpResponseCode_InvalidStorageId            = 0x2008,
        PtpResponseCode_InvalidObjectHandle         = 0x2009,
        PtpResponseCode_DevicePropNotSupported      = 0x200A,
        PtpResponseCode_StoreFull                   = 0x200C,
        PtpResponseCode_StoreReadOnly               = 0x200E,
        PtpResponseCode_AccessDenied                = 0x200F,
        PtpResponseCode_PartialDeletion             = 0x2012,
        PtpResponseCode_SpecificationByFormatUnsupported = 0x2014,
        PtpResponseCode_NoValidObjectInfo           = 0x2015,
        PtpResponseCode_InvalidParentObject         = 0x201A,
        PtpResponseCode_InvalidParameter            = 0x201D,
        PtpResponseCode_SessionAlreadyOpen          = 0x201E,
        PtpResponseCode_InvalidObjectPropCode       = 0xA801,
        PtpResponseCode_InvalidObjectPropFormat     = 0xA802,
        PtpResponseCode_ObjectPropNotSupported      = 0xA80A,
    };

    /* Object format codes. Association == a folder. */
    enum PtpObjectFormatCode : u16 {
        PtpObjectFormatCode_Undefined   = 0x3000,
        PtpObjectFormatCode_Association = 0x3001,
    };

    enum PtpAssociationType : u16 {
        PtpAssociationType_GenericFolder = 0x0001,
    };

    enum PtpStorageType : u16 {
        PtpStorageType_FixedRam = 0x0004,
    };

    enum PtpFilesystemType : u16 {
        PtpFilesystemType_GenericHierarchical = 0x0003,
    };

    enum PtpAccessCapability : u16 {
        PtpAccessCapability_ReadWrite = 0x0000,
    };

    enum PtpProtectionStatus : u16 {
        PtpProtectionStatus_NoProtection = 0x0000,
    };

    /* MTP object property codes (used by Windows to list/display objects). */
    enum PtpObjectPropertyCode : u16 {
        PtpObjectPropertyCode_StorageId            = 0xDC01,
        PtpObjectPropertyCode_ObjectFormat         = 0xDC02,
        PtpObjectPropertyCode_ProtectionStatus     = 0xDC03,
        PtpObjectPropertyCode_ObjectSize           = 0xDC04,
        PtpObjectPropertyCode_ObjectFileName       = 0xDC07,
        PtpObjectPropertyCode_DateCreated          = 0xDC08,
        PtpObjectPropertyCode_DateModified         = 0xDC09,
        PtpObjectPropertyCode_ParentObject         = 0xDC0B,
        PtpObjectPropertyCode_PersistentUniqueObjectIdentifier = 0xDC41,
        PtpObjectPropertyCode_Name                 = 0xDC44,
    };

    /* MTP datatype codes (for GetObjectPropDesc / GetObjectPropList). */
    enum PtpDataTypeCode : u16 {
        PtpDataTypeCode_U16    = 0x0004,
        PtpDataTypeCode_U32    = 0x0006,
        PtpDataTypeCode_U64    = 0x0008,
        PtpDataTypeCode_U128   = 0x000A,
        PtpDataTypeCode_String = 0xFFFF,
    };

    enum PtpGetSetFlag : u8 {
        PtpGetSetFlag_Get    = 0x00,
        PtpGetSetFlag_GetSet = 0x01,
    };

    /* Sentinel parent handle: object sits in the root of its storage. */
    constexpr inline u32 PtpRootParentObject = 0xFFFFFFFF;
    /* Vendor extension: report as an MTP device so desktops mount it. */
    constexpr inline u32 PtpMtpVendorExtensionId = 0x00000006;

}
