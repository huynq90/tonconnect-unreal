#include "TonBlueprintLibrary.h"
#include "TonConnectSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

// -----------------------------------------------------------------------
// Internal helpers
// -----------------------------------------------------------------------

static const uint64 TON_NANO = 1000000000ULL; // 1 TON in nanoTON

FString UTonBlueprintLibrary::EstimateFeeQuick(ETonTxKind Kind)
{
    // Rough per-kind heuristic (nanoTON). This is a typical floor — it canNOT model
    // contract weight, how many contracts the message hops through, or cell count.
    // A plain TON→wallet transfer is near-constant; jetton/NFT bounce through several
    // contracts (sender jetton wallet → receiver jetton wallet → notify), so the
    // numbers below are conservative ballparks. Use EstimateFeeEmulated for accuracy.
    switch (Kind)
    {
    case ETonTxKind::JettonTransfer: return TEXT("60000000"); // ~0.06 TON (multi-hop)
    case ETonTxKind::NftTransfer:    return TEXT("60000000"); // ~0.06 TON
    case ETonTxKind::NativeTransfer:
    default:                         return TEXT("5500000");  // ~0.0055 TON (wallet→wallet)
    }
}

// Parse a decimal integer string into uint64. Returns 0 on failure.
static uint64 SafeParseUint64(const FString& S)
{
    if (S.IsEmpty()) return 0ULL;
    return FCString::Strtoui64(*S, nullptr, 10);
}

// Format N units with the given decimal precision into a display string.
// Example: N=1500000000, NanoPerUnit=1000000000, Decimals=2 → "1.50"
static FString FormatWithDecimals(uint64 Raw, uint64 NanoPerUnit, int32 Decimals)
{
    if (Decimals <= 0)
        return FString::Printf(TEXT("%llu"), Raw / NanoPerUnit);

    // Clamp decimals so pow fits in uint64
    Decimals = FMath::Clamp(Decimals, 1, 9);

    uint64 Scale = NanoPerUnit;
    for (int32 i = 0; i < Decimals; ++i) Scale /= 10;
    if (Scale == 0) Scale = 1;

    uint64 Major = Raw / NanoPerUnit;
    uint64 FracRaw = (Raw % NanoPerUnit) / Scale;

    // Left-pad fractional part to 'Decimals' digits
    FString FracStr = FString::Printf(TEXT("%llu"), FracRaw);
    while (FracStr.Len() < Decimals) FracStr = TEXT("0") + FracStr;

    return FString::Printf(TEXT("%llu.%s"), Major, *FracStr);
}

// Parse "1.500" into raw units given a multiplier (e.g. 1e9 for TON, 1e6 for USDT).
static FString ParseDecimalAmount(const FString& Input, int32 Decimals)
{
    if (Input.IsEmpty()) return TEXT("0");
    Decimals = FMath::Clamp(Decimals, 0, 18);

    int32 DotIdx = INDEX_NONE;
    Input.FindChar(TEXT('.'), DotIdx);

    FString IntPart  = (DotIdx != INDEX_NONE) ? Input.Left(DotIdx) : Input;
    FString FracPart = (DotIdx != INDEX_NONE) ? Input.Mid(DotIdx + 1) : TEXT("");

    // Pad or truncate fractional part to exactly Decimals digits
    while (FracPart.Len() < Decimals) FracPart += TEXT("0");
    if (FracPart.Len() > Decimals) FracPart = FracPart.Left(Decimals);

    // Remove leading zeros from integer part but keep at least one digit
    IntPart.TrimStartInline();
    if (IntPart.IsEmpty()) IntPart = TEXT("0");

    // Combine: if Decimals=0 just return integer
    FString Combined = IntPart + FracPart;

    // Strip leading zeros, preserve at least one
    int32 First = 0;
    while (First < Combined.Len() - 1 && Combined[First] == TEXT('0')) ++First;

    return Combined.Mid(First);
}

// -----------------------------------------------------------------------
// UTonBlueprintLibrary
// -----------------------------------------------------------------------

FString UTonBlueprintLibrary::FormatTon(const FString& NanoTon, int32 Decimals)
{
    uint64 N = SafeParseUint64(NanoTon);
    return FormatWithDecimals(N, TON_NANO, Decimals) + TEXT(" TON");
}

FString UTonBlueprintLibrary::FormatJetton(const FString& BaseUnits, int32 Decimals, const FString& Symbol)
{
    uint64 N = SafeParseUint64(BaseUnits);
    Decimals  = FMath::Clamp(Decimals, 0, 18);

    // Compute NanoPerUnit = 10^Decimals (capped at uint64)
    uint64 NanoPerUnit = 1;
    for (int32 i = 0; i < Decimals && NanoPerUnit <= UINT64_MAX / 10; ++i) NanoPerUnit *= 10;

    FString Num = FormatWithDecimals(N, NanoPerUnit, FMath::Min(Decimals, 6));
    return Symbol.IsEmpty() ? Num : Num + TEXT(" ") + Symbol;
}

FString UTonBlueprintLibrary::ParseTonAmount(const FString& TonString)
{
    return ParseDecimalAmount(TonString, 9);
}

FString UTonBlueprintLibrary::ParseJettonAmount(const FString& AmountString, int32 Decimals)
{
    return ParseDecimalAmount(AmountString, Decimals);
}

FString UTonBlueprintLibrary::TruncateAddress(const FString& Address, int32 PrefixLen, int32 SuffixLen)
{
    PrefixLen = FMath::Max(PrefixLen, 1);
    SuffixLen = FMath::Max(SuffixLen, 1);

    if (Address.Len() <= PrefixLen + SuffixLen + 2)
        return Address; // short enough to show fully

    FString Prefix = Address.Left(PrefixLen);
    FString Suffix = Address.Right(SuffixLen);
    return Prefix + TEXT("…") + Suffix; // "EQABCd…abcd"
}

int64 UTonBlueprintLibrary::NanoTonToInt64(const FString& NanoTon)
{
    // FCString::Atoi64 handles negative; for nanoTON we expect positive
    return FCString::Atoi64(*NanoTon);
}

FString UTonBlueprintLibrary::Int64ToNanoTon(int64 NanoTon)
{
    return FString::Printf(TEXT("%lld"), NanoTon);
}

bool UTonBlueprintLibrary::IsValidTonAddress(const FString& Address)
{
    if (Address.IsEmpty()) return false;

    // Raw format: "<workchain>:<64 hex chars>"
    int32 ColonIdx = INDEX_NONE;
    if (Address.FindChar(TEXT(':'), ColonIdx))
    {
        FString HexPart = Address.Mid(ColonIdx + 1);
        if (HexPart.Len() != 64) return false;
        for (TCHAR C : HexPart)
            if (!FChar::IsHexDigit(C)) return false;
        return true;
    }

    // Friendly format: 48 base64url characters (no padding or with '=')
    FString Stripped = Address.Replace(TEXT("="), TEXT(""));
    if (Stripped.Len() != 48) return false;
    for (TCHAR C : Stripped)
    {
        if (!FChar::IsAlnum(C) && C != TEXT('+') && C != TEXT('/') &&
            C != TEXT('-') && C != TEXT('_')) return false;
    }
    return true;
}

UTonConnectSubsystem* UTonBlueprintLibrary::GetTonConnect(const UObject* WorldContextObject)
{
    if (!WorldContextObject) return nullptr;
    UWorld* World = WorldContextObject->GetWorld();
    if (!World) return nullptr;
    UGameInstance* GI = World->GetGameInstance();
    if (!GI) return nullptr;
    return GI->GetSubsystem<UTonConnectSubsystem>();
}
