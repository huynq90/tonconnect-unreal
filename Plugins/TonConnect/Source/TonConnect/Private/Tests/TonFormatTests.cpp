#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "TonBlueprintLibrary.h"
#include "Api/TonMockApiClient.h"
#include "TonSession.h"
#include "Cells/TonBoc.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
    FString BytesToHexLower(const TArray<uint8>& B)
    {
        FString H; for (uint8 X : B) H += FString::Printf(TEXT("%02x"), X); return H;
    }
}

// ── FormatTon ────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTonFormat_FormatTon,
    "TonConnect.Format.FormatTon",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTonFormat_FormatTon::RunTest(const FString&)
{
    TestEqual(TEXT("1 TON 2d"),   UTonBlueprintLibrary::FormatTon(TEXT("1000000000"), 2), TEXT("1.00 TON"));
    TestEqual(TEXT("1.5 TON 2d"), UTonBlueprintLibrary::FormatTon(TEXT("1500000000"), 2), TEXT("1.50 TON"));
    TestEqual(TEXT("0.5 TON 2d"), UTonBlueprintLibrary::FormatTon(TEXT("500000000"),  2), TEXT("0.50 TON"));
    TestEqual(TEXT("0 TON 2d"),   UTonBlueprintLibrary::FormatTon(TEXT("0"),           2), TEXT("0.00 TON"));
    TestEqual(TEXT("1 TON 0d"),   UTonBlueprintLibrary::FormatTon(TEXT("1000000000"), 0), TEXT("1 TON"));
    TestEqual(TEXT("1 TON 9d"),   UTonBlueprintLibrary::FormatTon(TEXT("1000000001"), 9), TEXT("1.000000001 TON"));
    return true;
}

// ── ParseTonAmount ────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTonFormat_ParseTonAmount,
    "TonConnect.Format.ParseTonAmount",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTonFormat_ParseTonAmount::RunTest(const FString&)
{
    TestEqual(TEXT("1.5"),          UTonBlueprintLibrary::ParseTonAmount(TEXT("1.5")),          TEXT("1500000000"));
    TestEqual(TEXT("1"),            UTonBlueprintLibrary::ParseTonAmount(TEXT("1")),             TEXT("1000000000"));
    TestEqual(TEXT("0.000000001"),  UTonBlueprintLibrary::ParseTonAmount(TEXT("0.000000001")),  TEXT("1"));
    TestEqual(TEXT("0"),            UTonBlueprintLibrary::ParseTonAmount(TEXT("0")),             TEXT("0"));
    TestEqual(TEXT("10.5"),         UTonBlueprintLibrary::ParseTonAmount(TEXT("10.5")),          TEXT("10500000000"));
    return true;
}

// ── FormatJetton ─────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTonFormat_FormatJetton,
    "TonConnect.Format.FormatJetton",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTonFormat_FormatJetton::RunTest(const FString&)
{
    // USDT-like: 6 decimals
    TestEqual(TEXT("1 USDT"),   UTonBlueprintLibrary::FormatJetton(TEXT("1000000"), 6, TEXT("USDT")), TEXT("1.000000 USDT"));
    // No symbol
    TestEqual(TEXT("no sym"),   UTonBlueprintLibrary::FormatJetton(TEXT("1000000"), 6, TEXT("")),     TEXT("1.000000"));
    // Zero
    TestEqual(TEXT("0 USDT"),   UTonBlueprintLibrary::FormatJetton(TEXT("0"),       6, TEXT("USDT")), TEXT("0.000000 USDT"));
    return true;
}

// ── TruncateAddress ───────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTonFormat_TruncateAddress,
    "TonConnect.Format.TruncateAddress",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTonFormat_TruncateAddress::RunTest(const FString&)
{
    // Short address (len <= prefix+suffix+2) → returned unchanged
    TestEqual(TEXT("short"),    UTonBlueprintLibrary::TruncateAddress(TEXT("EQABCDabcd"), 6, 4), TEXT("EQABCDabcd"));

    // Long address → prefix + ellipsis + suffix
    const FString Long = TEXT("EQAABCDE1234abcdef");    // 18 chars > 6+4+2=12
    const FString Got  = UTonBlueprintLibrary::TruncateAddress(Long, 6, 4);
    TestEqual(TEXT("prefix"),  Got.Left(6),   TEXT("EQAABC"));
    TestEqual(TEXT("suffix"),  Got.Right(4),  TEXT("cdef"));
    TestTrue (TEXT("ellipsis"), Got.Contains(TEXT("…")));
    return true;
}

// ── IsValidTonAddress ─────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTonFormat_IsValidTonAddress,
    "TonConnect.Format.IsValidTonAddress",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTonFormat_IsValidTonAddress::RunTest(const FString&)
{
    // Raw format: workchain:64hexchars
    TestTrue (TEXT("raw valid"),
        UTonBlueprintLibrary::IsValidTonAddress(
            TEXT("0:3333333333333333333333333333333333333333333333333333333333333333")));

    // Friendly format: 48 base64url chars (no padding)
    // Known testnet wallet address — 48 chars of base64url
    TestTrue (TEXT("friendly valid"),
        UTonBlueprintLibrary::IsValidTonAddress(
            TEXT("EQAVDMYkEWVkRqF3YLlr5eH0TbCf9nLNpq3aE7nF9lMnWUJb")));

    TestFalse(TEXT("empty"),          UTonBlueprintLibrary::IsValidTonAddress(TEXT("")));
    TestFalse(TEXT("too short"),      UTonBlueprintLibrary::IsValidTonAddress(TEXT("EQshort")));
    TestFalse(TEXT("raw hex short"),  UTonBlueprintLibrary::IsValidTonAddress(TEXT("0:tooshort")));
    TestFalse(TEXT("raw hex nonhex"), UTonBlueprintLibrary::IsValidTonAddress(TEXT("0:ZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZ")));
    return true;
}

// ── MockApiClient GetAccountInfo ──────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTonMockApi_AccountInfo,
    "TonConnect.MockApi.AccountInfo",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTonMockApi_AccountInfo::RunTest(const FString&)
{
    FTonMockApiClient Client;
    bool bCalled = false;

    Client.GetAccountInfo(TEXT("any"), [&](bool bOk, FTonAccountInfo Info)
    {
        bCalled = true;
        TestTrue (TEXT("success"),       bOk);
        TestFalse(TEXT("balance set"),   Info.BalanceNano.IsEmpty());
        TestEqual(TEXT("version"),       Info.WalletVersion, TEXT("W5 R1"));
    });

    TestTrue(TEXT("callback fired"), bCalled);
    return true;
}

// ── MockApiClient GetJettonBalances ───────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTonMockApi_Jettons,
    "TonConnect.MockApi.Jettons",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTonMockApi_Jettons::RunTest(const FString&)
{
    FTonMockApiClient Client;
    bool bCalled = false;

    Client.GetJettonBalances(TEXT("any"), [&](bool bOk, TArray<FTonJettonBalance> Jettons)
    {
        bCalled = true;
        TestTrue(TEXT("success"),      bOk);
        TestTrue(TEXT("has jettons"),  Jettons.Num() > 0);
        TestFalse(TEXT("symbol set"),  Jettons[0].Symbol.IsEmpty());
        TestFalse(TEXT("balance set"), Jettons[0].Balance.IsEmpty());

        // FormatJetton must not crash on mock data
        const FString Fmt = UTonBlueprintLibrary::FormatJetton(
            Jettons[0].Balance, Jettons[0].Decimals, Jettons[0].Symbol);
        TestFalse(TEXT("formatted not empty"), Fmt.IsEmpty());
    });

    TestTrue(TEXT("callback fired"), bCalled);
    return true;
}

// ── BOC cell hashing primitive ────────────────────────────────────────────────
// The representation hash of the empty cell (D1=0, D2=0, no data, no refs) is
// SHA256(0x00 0x00) — a fixed, well-known TON constant. This guards the SHA256
// dependency used by FTonBoc cell hashing.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTonBoc_EmptyCellHash,
    "TonConnect.Boc.EmptyCellHash",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTonBoc_EmptyCellHash::RunTest(const FString&)
{
    const TArray<uint8> Repr = { 0x00, 0x00 };
    const FString Got = BytesToHexLower(FTonSession::SHA256(Repr));
    TestEqual(TEXT("empty cell repr hash"),
        Got, TEXT("96a296d224f285c67bee93c30f8a309157f0daa35dc5b87e410b78630a09cfc7"));
    return true;
}

// ── Wallet version table lookup ────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTonBoc_VersionTable,
    "TonConnect.Boc.VersionTable",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTonBoc_VersionTable::RunTest(const FString&)
{
    TestEqual(TEXT("v4r2"),
        FTonBoc::WalletVersionFromCodeHash(
            TEXT("feb5ff6820e2ff0d9483e7e0d62c817d846789fb4ae580c878866d959dabd5c0")),
        TEXT("V4R2"));
    TestEqual(TEXT("w5r1"),
        FTonBoc::WalletVersionFromCodeHash(
            TEXT("20834b7b72b112147e1b2fb457b84e74d1a30f04f737d4f62a668e9552d2b72f")),
        TEXT("W5 R1"));
    // Case-insensitive
    TestEqual(TEXT("uppercase ok"),
        FTonBoc::WalletVersionFromCodeHash(
            TEXT("FEB5FF6820E2FF0D9483E7E0D62C817D846789FB4AE580C878866D959DABD5C0")),
        TEXT("V4R2"));
    // Unknown → empty
    TestTrue(TEXT("unknown empty"),
        FTonBoc::WalletVersionFromCodeHash(TEXT("deadbeef")).IsEmpty());
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
