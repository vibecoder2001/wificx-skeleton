#pragma once

#include <wlan/2.0/TlvGeneratorParser.hpp>

//
// Normalized view of a WDI cipher key. The WDI TLV stores the key
// material in one of N Optional fields keyed by cipher (CCMPKey,
// GCMPKey, TKIPInfo, BIPKey, WEPKey, ...); we flatten that into a
// single byte buffer + length so backends don't have to switch on
// cipher to find the bytes.
//
// HasPeer: TRUE for pairwise (peer MAC supplied); FALSE for group
//          / IGTK / BIGTK keys (PeerAddress is zero in that case).
//
typedef struct _WDI_KEY_DESCRIPTOR {
    UCHAR                 PeerAddress[6];
    UCHAR                 KeyIndex;          // 0-3 pairwise/group, 4-5 IGTK, 6-7 BIGTK
    BOOLEAN               HasPeer;
    WDI_CIPHER_KEY_TYPE   Type;              // WDI_CIPHER_KEY_TYPE_{PAIRWISE,GROUP,IGTK,BIGTK}
    WDI_CIPHER_ALGORITHM  Cipher;            // WDI_CIPHER_ALGO_*
    UCHAR                 Rsc[8];            // receive sequence counter (PN/IV start)
    BOOLEAN               HasRsc;
    UCHAR                 KeyLength;         // 0..32
    UCHAR                 Key[32];           // GCMP-256 / CCMP-256 max
} WDI_KEY_DESCRIPTOR;

//
// Extract a normalized WDI_KEY_DESCRIPTOR from one container in a
// WDI_SET_ADD_CIPHER_KEYS_PARAMETERS. Returns FALSE when the
// container carries a cipher we don't surface here (e.g. an IHV-
// vendor key); caller should skip rather than fail.
//
BOOLEAN
WdiKeyFromContainer(
    _In_ const WDI_SET_ADD_CIPHER_KEYS_CONTAINER* Container,
    _Out_ WDI_KEY_DESCRIPTOR* OutKey);
