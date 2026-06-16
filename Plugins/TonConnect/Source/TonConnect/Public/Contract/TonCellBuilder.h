#pragma once

#include "CoreMinimal.h"
#include "Cells/TonCell.h"

class UTonMessageSpec;

// Builds a TonCell from a UTonMessageSpec + a flat string-value map.
// Value encoding by field type:
//   UInt/Int    → decimal string  (e.g. "1000000000")
//   Bool        → "true" / "false" (case-insensitive)
//   Address     → human-readable or raw TON address
//   Coins       → nanoTON as decimal string
//   Bytes       → hex string without prefix (e.g. "deadbeef")
//   Text        → plain UTF-8 string written as inline bytes
struct TONCONNECT_API FTonCellBuilder
{
    // Returns nullptr if Spec is null.
    static TSharedPtr<FTonCell> Build(const UTonMessageSpec* Spec,
                                      const TMap<FString, FString>& Values);
};
