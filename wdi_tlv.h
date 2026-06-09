#pragma once

#include <wlan/2.0/TlvGeneratorParser.hpp>

//
// WDI TLV / indication helpers. Centralizes two things that were
// previously copy-pasted across the dispatcher and the workitems:
//
//   1. Building a TLV_CONTEXT from the negotiated OS WDI version.
//   2. Firing WDI indications with the correct header — including
//      the all-important TransactionId discipline (see below).
//

// Build a TLV_CONTEXT for parse / generate calls.
TLV_CONTEXT
WdiTlvContext(_In_ ULONG OsWdiVersion);

// Return the TLV body of an inbound request (the bytes past the
// WDI_MESSAGE_HEADER). Returns NULL and zero length if there's no
// body or the input is malformed.
const UINT8*
WdiTlvBody(
    _In_ PWDI_MESSAGE_HEADER MsgHdr,
    _In_ UINT InLen,
    _Out_ ULONG* OutLen);

//
// =================== INDICATION HELPERS ===================
//
// CRITICAL: choose the right helper for the indication's role.
//
// === TransactionId rules (validated against nwifi disassembly) ===
//
//   * Unsolicited indications — surprise events the device pushes
//     to the host that are NOT completing a pending Task
//     (BSS_ENTRY_LIST, RADIO_STATUS push, ASSOCIATION_RESULT for an
//     AP-initiated deauth, etc.): TransactionId MUST be 0.
//
//     Why: Task::OnDeviceIndicationArrived consumes any indication
//     whose (PortId, TransactionId) matches an outstanding Task as
//     the Task's completion — and DISCARDS the payload. Using TxnId
//     0 routes the indication to CPort::OnDeviceIndicationArrived
//     for normal processing.
//
//   * Task-completion indications (SCAN_COMPLETE,
//     DOT11_RESET_COMPLETE, SET_RADIO_STATE_COMPLETE, ...):
//     TransactionId MUST match the Task's TxnId.
//
// The helpers below encode the rule in the name. Don't reach past
// them to construct an indication header by hand.
//

// Header-only indication. Use for task-completion indications:
// SCAN_COMPLETE, DOT11_RESET_COMPLETE, SET_RADIO_STATE_COMPLETE,
// TASK_CLOSE_COMPLETE, ...
//
// Caller passes the outstanding Task's TransactionId; the OS Task
// machinery matches it and completes the Task.
NTSTATUS
WdiIndicateTaskComplete(
    _In_ WDFDEVICE WdfDevice,
    _In_ UINT16 IndicationId,
    _In_ UINT16 PortId,
    _In_ UINT32 TransactionId);

// TLV-bodied indication. Takes ownership of a buffer returned by
// one of the Generate*FromIhv functions; rewrites the header at
// offset 0 with PortId and TransactionId; fires the indication;
// frees the generated buffer via FreeGenerated. On any failure the
// buffer is still freed and an error returned.
//
// Pass TransactionId = 0 for unsolicited (BSS_ENTRY_LIST,
// RADIO_STATUS push, mid-session ASSOCIATION_RESULT from a remote-
// initiated event, etc.).
NTSTATUS
WdiIndicateTlv(
    _In_ WDFDEVICE WdfDevice,
    _In_ UINT16 IndicationId,
    _In_ UINT16 PortId,
    _In_ UINT32 TransactionId,
    _Inout_ _At_(*GenBuf, _Pre_notnull_ _Post_null_) UINT8** GenBuf,
    _In_ ULONG BufLen);
