#include "precomp.h"
#include "wdi_mmpdu.h"

NTSTATUS
WdiMmpduStripHeader(
    _In_reads_bytes_(FrameLen) const UINT8* Frame,
    _In_ ULONG FrameLen,
    _Outptr_result_buffer_(*OutBodyLen) const UINT8** OutBody,
    _Out_ ULONG* OutBodyLen)
{
    *OutBody = NULL;
    *OutBodyLen = 0;
    if (!Frame || FrameLen < 24) {
        return STATUS_BUFFER_TOO_SMALL;
    }
    *OutBody = Frame + 24;
    *OutBodyLen = FrameLen - 24;
    return STATUS_SUCCESS;
}

BOOLEAN
WdiMmpduAssertIsBody(
    _In_reads_bytes_(BufLen) const UINT8* Buf,
    _In_ ULONG BufLen)
{
    if (!Buf || BufLen == 0) {
        // Empty body is legal (e.g. deauth with no IEs after reason
        // code is rare but not forbidden by this check); the caller
        // gets to decide whether that's an error.
        return TRUE;
    }
    //
    // Mgmt frame FC0 values that would suggest an unstripped header:
    //   0x00 = Association Request
    //   0x10 = Association Response
    //   0x20 = Reassociation Request
    //   0x30 = Reassociation Response
    //   0x40 = Probe Request
    //   0x50 = Probe Response
    //   0x80 = Beacon
    //   0xA0 = Disassociation
    //   0xB0 = Authentication
    //   0xC0 = Deauthentication
    //
    // Heuristic — if FC0 looks like one of those AND the buffer is
    // >= 24 bytes (room for a full mgmt header), assume the caller
    // forgot to strip.
    //
    if (BufLen < 24) return TRUE;
    UCHAR fc0 = Buf[0];
    switch (fc0) {
    case 0x00: case 0x10: case 0x20: case 0x30:
    case 0x40: case 0x50: case 0x80:
    case 0xA0: case 0xB0: case 0xC0:
        return FALSE;
    default:
        return TRUE;
    }
}
