#pragma once

#include "CoreMinimal.h"

// Minimal BOC (Bag of Cells) reader — just enough to extract a wallet's code-cell
// representation hash from a `walletStateInit`, so the wallet version can be detected
// at connect time WITHOUT a network round-trip (works even for non-deployed wallets).
//
// Only the read path needed for version detection is implemented:
//   base64 BOC → cells → root → root.refs[0] (code) → repr hash → known-version table.
struct TONCONNECT_API FTonBoc
{
    // base64 walletStateInit → code cell representation hash (64-char lowercase hex).
    // Returns empty string on any parse failure.
    static FString CodeHashFromStateInit(const FString& StateInitBase64);

    // Known wallet code hash (hex) → friendly version ("W5 R1", "V4R2", …).
    // Returns empty if the hash isn't recognised.
    static FString WalletVersionFromCodeHash(const FString& CodeHashHex);

    // Convenience: StateInit base64 → version string. Empty if unknown or unparsable.
    // Always logs the computed code hash so unrecognised wallets are easy to add.
    static FString WalletVersionFromStateInit(const FString& StateInitBase64);
};
