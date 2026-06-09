#include "precomp.h"
#include "wdi_keys.h"

BOOLEAN
WdiKeyFromContainer(
    _In_ const WDI_SET_ADD_CIPHER_KEYS_CONTAINER* C,
    _Out_ WDI_KEY_DESCRIPTOR* K)
{
    RtlZeroMemory(K, sizeof(*K));

    K->Type   = C->CipherKeyTypeInfo.KeyType;
    K->Cipher = C->CipherKeyTypeInfo.CipherAlgorithm;
    K->KeyIndex = (UCHAR)(C->Optional.CipherKeyID_IsPresent
        ? C->CipherKeyID.CipherKeyID : 0);

    if (C->Optional.PeerMacAddress_IsPresent) {
        K->HasPeer = TRUE;
        RtlCopyMemory(K->PeerAddress, C->PeerMacAddress.Address, 6);
    }

    if (C->Optional.ReceiveSequenceCount_IsPresent) {
        K->HasRsc = TRUE;
        // WDI_RSC_CONTAINER lays the 6/8-byte PN out in field order;
        // we store the raw bytes verbatim so backends can write them
        // into hardware PN registers without endianness gymnastics.
        RtlCopyMemory(K->Rsc, &C->ReceiveSequenceCount, sizeof(K->Rsc));
    }

    // Pull the key bytes out of whichever cipher-specific Optional
    // is present. The Generated TLV layout is one-of: only one of
    // these fires per container.
    const UINT8* keyBytes = NULL;
    UINT32 keyLen = 0;

    if (C->Optional.CCMPKey_IsPresent) {
        keyBytes = C->CCMPKey.pElements;
        keyLen   = C->CCMPKey.ElementCount;
    } else if (C->Optional.GCMPKey_IsPresent) {
        keyBytes = C->GCMPKey.pElements;
        keyLen   = C->GCMPKey.ElementCount;
    } else if (C->Optional.GCMP_256Key_IsPresent) {
        keyBytes = C->GCMP_256Key.pElements;
        keyLen   = C->GCMP_256Key.ElementCount;
    } else if (C->Optional.BIPKey_IsPresent) {
        keyBytes = C->BIPKey.pElements;
        keyLen   = C->BIPKey.ElementCount;
    } else if (C->Optional.BIP_GMAC_256Key_IsPresent) {
        keyBytes = C->BIP_GMAC_256Key.pElements;
        keyLen   = C->BIP_GMAC_256Key.ElementCount;
    } else if (C->Optional.WEPKey_IsPresent) {
        keyBytes = C->WEPKey.pElements;
        keyLen   = C->WEPKey.ElementCount;
    } else if (C->Optional.TKIPInfo_IsPresent) {
        // TKIPInfo bundles the temporal key plus the MIC keys. We
        // surface only the bare 16-byte TK here; backends that need
        // the MIC keys can read TKIPInfo directly off the container.
        keyBytes = C->TKIPInfo.TKIPKey.pElements;
        keyLen   = C->TKIPInfo.TKIPKey.ElementCount;
    } else {
        // IHV vendor key or some other shape we don't translate.
        return FALSE;
    }

    if (keyLen > sizeof(K->Key)) {
        // Shouldn't happen for any standard cipher, but bail rather
        // than truncate silently.
        return FALSE;
    }

    K->KeyLength = (UCHAR)keyLen;
    if (keyLen) {
        RtlCopyMemory(K->Key, keyBytes, keyLen);
    }
    return TRUE;
}
