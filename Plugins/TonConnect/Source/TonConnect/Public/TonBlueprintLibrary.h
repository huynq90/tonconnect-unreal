#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "TonTypes.h"
#include "TonBlueprintLibrary.generated.h"

class UTonConnectSubsystem;

UCLASS()
class TONCONNECT_API UTonBlueprintLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    // ---------------------------------------------------------------
    // Display formatting
    // ---------------------------------------------------------------

    // "1500000000" (nanoTON) → "1.50 TON"
    UFUNCTION(BlueprintPure, Category="TonConnect|Format")
    static FString FormatTon(const FString& NanoTon, int32 Decimals = 2);

    // "10000000" (6-decimal jetton, e.g. USDT) → "10.00 USDT"
    UFUNCTION(BlueprintPure, Category="TonConnect|Format")
    static FString FormatJetton(const FString& BaseUnits, int32 Decimals, const FString& Symbol);

    // "1.5" → "1500000000" (nanoTON string)
    UFUNCTION(BlueprintPure, Category="TonConnect|Format")
    static FString ParseTonAmount(const FString& TonString);

    // "1.5" with 6 decimals → "1500000" (jetton base units string)
    UFUNCTION(BlueprintPure, Category="TonConnect|Format")
    static FString ParseJettonAmount(const FString& AmountString, int32 Decimals);

    // "EQABCDxxxxxxWXYZabcd" → "EQABCd…abcd"
    UFUNCTION(BlueprintPure, Category="TonConnect|Format")
    static FString TruncateAddress(const FString& Address, int32 PrefixLen = 6, int32 SuffixLen = 4);

    // ---------------------------------------------------------------
    // Fee estimation (quick / heuristic — no network call)
    // ---------------------------------------------------------------

    // Instant heuristic network-fee estimate (nanoTON) by transaction kind.
    // TON transfers are near-constant (~0.0055 TON); jetton/NFT are approximate.
    // For exact fees use UTonConnectSubsystem::EstimateFeeEmulated (on-chain emulation).
    UFUNCTION(BlueprintPure, Category="TonConnect|Fee")
    static FString EstimateFeeQuick(ETonTxKind Kind);

    // ---------------------------------------------------------------
    // Conversion helpers
    // ---------------------------------------------------------------

    // nanoTON decimal string → int64 (saturates at INT64_MAX for giant values)
    UFUNCTION(BlueprintPure, Category="TonConnect|Convert")
    static int64 NanoTonToInt64(const FString& NanoTon);

    // int64 nanoTON → decimal string (for passing back into Send functions)
    UFUNCTION(BlueprintPure, Category="TonConnect|Convert")
    static FString Int64ToNanoTon(int64 NanoTon);

    // ---------------------------------------------------------------
    // Validation
    // ---------------------------------------------------------------

    // Returns true if the string looks like a valid TON address (raw 0: or friendly base64url)
    UFUNCTION(BlueprintPure, Category="TonConnect|Validation")
    static bool IsValidTonAddress(const FString& Address);

    // ---------------------------------------------------------------
    // Subsystem shortcut
    // ---------------------------------------------------------------

    // Get TonConnectSubsystem from any Blueprint context (actor, widget, etc.)
    UFUNCTION(BlueprintPure, Category="TonConnect",
              meta=(DefaultToSelf="WorldContextObject", HidePin="WorldContextObject"))
    static UTonConnectSubsystem* GetTonConnect(const UObject* WorldContextObject);
};
