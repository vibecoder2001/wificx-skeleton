#pragma once

//
// =============================================================
// CRITICAL — WDI MMPDU-BODY CONTRACT (read before writing any
//            ASSOCIATION_RESULT / DISASSOCIATION / DEAUTHENTICATION
//            indication.)
// =============================================================
//
// The WDI indication TLVs for {Association,Disassociation,
// Deauthentication}Frame carry the MMPDU **body** only — the bytes
// **after** the 24-byte 802.11 MAC header. Not the on-air frame.
// Not the FC / Duration / Addresses / Seq prefix. Body bytes only.
//
// What goes wrong if you pass the full frame:
//   - The OS MSM (msft Media-Specific Module) parses Capability,
//     Status, AID, and IEs starting at offset 0 of the supplied
//     buffer.
//   - With the 24-byte header still present, every field is
//     mis-aligned by 24 bytes. The MSM never finds the RSN IE.
//   - For an RSNA connect, that silently means the OS skips the
//     4-way handshake. No PTK derivation. No data path.
//   - There is no error indication. wlansvc shows "Connected" but
//     no traffic flows. You will spend two weeks finding this.
//
// This was discovered the hard way against the Microsoft WDI
// sample, whose RX path uses `MMPDU_BODY = buf + 24`. We confirmed
// the contract via kd-tracing through nwifi's IE walker.
//
// USE WdiMmpduStripHeader() to slice a captured on-air frame down
// to the body before assigning into a WDI_*_Frame field. In debug
// builds, the strip helper asserts the resulting body does NOT
// start with a mgmt-frame FC0 (which would indicate that the
// caller passed an already-stripped body twice, or a half-stripped
// frame).
//

//
// Strip the 24-byte 802.11 MAC header from a captured on-air mgmt
// frame and return the MMPDU body bytes.
//
// On success: *OutBody = Frame + 24, *OutBodyLen = FrameLen - 24.
// Returns STATUS_BUFFER_TOO_SMALL if the frame is shorter than 24
// bytes (impossible for a real mgmt frame; protects callers from
// undersized input).
//
// Pass the result directly to WDI_INDICATION_*_PARAMETERS's
// {Association,Disassociation,Deauthentication,...}Frame field
// via SimpleAssign.
//
NTSTATUS
WdiMmpduStripHeader(
    _In_reads_bytes_(FrameLen) const UINT8* Frame,
    _In_ ULONG FrameLen,
    _Outptr_result_buffer_(*OutBodyLen) const UINT8** OutBody,
    _Out_ ULONG* OutBodyLen);

//
// Debug-only sanity check: assert that `Buf` looks like an MMPDU
// body (i.e. NOT an on-air mgmt frame). Called by the indication
// emitters before they hand bytes to SimpleAssign.
//
// Returns FALSE if the buffer looks like an unstripped 802.11
// mgmt frame (FC0 indicates Auth/Assoc-Req/Resp/Probe-Resp/
// Disassoc/Deauth and the frame is large enough to plausibly be
// an on-air capture). Caller can NT_ASSERT on the return.
//
BOOLEAN
WdiMmpduAssertIsBody(
    _In_reads_bytes_(BufLen) const UINT8* Buf,
    _In_ ULONG BufLen);
