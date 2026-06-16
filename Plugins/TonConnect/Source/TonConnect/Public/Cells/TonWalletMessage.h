#pragma once

#include "CoreMinimal.h"

struct FTonCell;

// Builds UNSIGNED external messages for fee emulation (TonAPI /v2/wallet/emulate
// ignores the signature, but the wallet contract still validates structure, so the
// wallet-version layout must be correct).
//
// Flow: build a MessageRelaxed (internal message) for the desired action, then wrap
// it in the connected wallet's external-message envelope (v4r2 or v5r1).
struct TONCONNECT_API FTonWalletMessage
{
    // MessageRelaxed: int_msg to Dest carrying AmountNano grams + optional body cell.
    static TSharedPtr<FTonCell> BuildInternalMessage(
        int32 DestWc, const TArray<uint8>& DestHash,
        uint64 AmountNano, bool bBounce, TSharedPtr<FTonCell> Body);

    // Wrap an internal message into an unsigned external message for the wallet.
    // WalletVersion: "V4R1"/"V4R2" → v4r2 layout; "W5 R1"/"V5..." → v5r1 layout.
    // Returns BOC base64, or empty if the version isn't supported / inputs are bad.
    static FString WrapExternalForEmulate(
        const FString& WalletVersion, bool bTestnet,
        int32 WalletWc, const TArray<uint8>& WalletHash,
        int32 Seqno, int64 ValidUntilUnix,
        TSharedPtr<FTonCell> InternalMessage);
};
