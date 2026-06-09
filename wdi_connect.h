#pragma once

#include "wdi_chip_ops.h"

//
// Target the OS handed us via WDI_TASK_CONNECT. The chip backend's
// Connect op uses this to know which AP to associate to. Real
// backends pull more out of the TLV (auth algos, cipher pairs);
// add fields here as the skeleton grows.
//
typedef struct _WDI_CONNECT_TARGET {
    UCHAR  Bssid[6];
    UCHAR  _pad[2];
    UCHAR  Channel;
    UCHAR  SsidLen;
    UCHAR  Ssid[32];
    BOOLEAN UseRsn;
} WDI_CONNECT_TARGET;

//
// Emit WDI_INDICATION_ASSOCIATION_RESULT for the currently
// outstanding connect task. The WDI layer parks the task's
// PortId / TxnId in MTK_DEVICE during dispatch and this emitter
// reads them — chip backends don't have to know what PortId is.
//
// CALLER CONTRACT: AssocReqBody and AssocRespBody MUST be the
// MMPDU bodies (post-MAC-header bytes). Do NOT pass on-air frame
// captures with the 24-byte 802.11 header still attached. Use
// WdiMmpduStripHeader if you only have on-air bytes.
//
// In debug builds this function NT_ASSERTs the body shape via
// WdiMmpduAssertIsBody on both buffers.
void
WdiEmitAssociationResult(
    _In_ WDFDEVICE WdfDevice,
    _In_reads_(6) const UCHAR* PeerBssid,
    _In_reads_bytes_(ReqLen) const UINT8* AssocReqBody,
    _In_ ULONG ReqLen,
    _In_reads_bytes_(RespLen) const UINT8* AssocRespBody,
    _In_ ULONG RespLen,
    _In_ UINT16 StatusCode);

// Emit WDI_INDICATION_DISASSOCIATION (IsDisassoc=TRUE) or
// WDI_INDICATION_DEAUTHENTICATION (IsDisassoc=FALSE) — the AP
// kicked us off.
//
// CALLER CONTRACT: Body is the MMPDU body of the disassoc/deauth
// frame — reason code at offset 0, optional MME / vendor IEs
// following. NOT the on-air frame.
void
WdiEmitDisassociation(
    _In_ WDFDEVICE WdfDevice,
    _In_reads_(6) const UCHAR* PeerBssid,
    _In_ BOOLEAN IsDisassoc,
    _In_ UINT16 ReasonCode,
    _In_reads_bytes_opt_(BodyLen) const UINT8* Body,
    _In_ ULONG BodyLen);
