#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "TonUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

// ── ParseHumanAddress ─────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTonAddr_ParseRaw,
    "TonConnect.Address.ParseRaw",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTonAddr_ParseRaw::RunTest(const FString&)
{
    int32 WC = -99;
    TArray<uint8> Hash;
    const bool bOk = FTonAddressUtils::ParseHumanAddress(
        TEXT("0:3333333333333333333333333333333333333333333333333333333333333333"),
        WC, Hash);

    TestTrue (TEXT("parse ok"),      bOk);
    TestEqual(TEXT("workchain"),     WC,           0);
    TestEqual(TEXT("hash len"),      Hash.Num(),   32);
    TestEqual(TEXT("hash[0]"),       Hash[0],      (uint8)0x33);
    TestEqual(TEXT("hash[31]"),      Hash[31],     (uint8)0x33);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTonAddr_ParseBadHex,
    "TonConnect.Address.ParseBadHex",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTonAddr_ParseBadHex::RunTest(const FString&)
{
    int32 WC = 0;
    TArray<uint8> Hash;
    // Hex part is only 4 chars — should fail
    TestFalse(TEXT("bad hex"), FTonAddressUtils::ParseHumanAddress(TEXT("0:1234"), WC, Hash));
    TestFalse(TEXT("empty"),   FTonAddressUtils::ParseHumanAddress(TEXT(""),       WC, Hash));
    return true;
}

// ── ToHumanReadable / round-trip ──────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTonAddr_RoundTrip,
    "TonConnect.Address.RoundTrip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTonAddr_RoundTrip::RunTest(const FString&)
{
    const FString Raw = TEXT("0:3333333333333333333333333333333333333333333333333333333333333333");

    // Encode all 4 tag variants, then parse back — should recover same workchain + hash
    const bool Bounce[2]  = { false, true  };
    const bool Testnet[2] = { false, true  };

    for (bool bB : Bounce)
    {
        for (bool bT : Testnet)
        {
            const FString Friendly = FTonAddressUtils::ToHumanReadable(Raw, bB, bT);
            TestEqual(TEXT("friendly len"), Friendly.Len(), 48);

            int32 WC = -99;
            TArray<uint8> Hash;
            bool bOutB = !bB, bOutT = !bT;
            const bool bOk = FTonAddressUtils::ParseHumanAddress(Friendly, WC, Hash, &bOutB, &bOutT);

            TestTrue (TEXT("parse ok"),    bOk);
            TestEqual(TEXT("workchain"),   WC,     0);
            TestEqual(TEXT("hash[0]"),     Hash[0], (uint8)0x33);
            TestEqual(TEXT("bounce flag"), bOutB,  bB);
            TestEqual(TEXT("testnet flag"),bOutT,  bT);
        }
    }
    return true;
}

// ── Masterchain workchain -1 ──────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTonAddr_Masterchain,
    "TonConnect.Address.Masterchain",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTonAddr_Masterchain::RunTest(const FString&)
{
    const FString Raw = TEXT("-1:3333333333333333333333333333333333333333333333333333333333333333");
    const FString Friendly = FTonAddressUtils::ToHumanReadable(Raw, true, false);
    TestEqual(TEXT("friendly len"), Friendly.Len(), 48);

    int32 WC = 0;
    TArray<uint8> Hash;
    const bool bOk = FTonAddressUtils::ParseHumanAddress(Friendly, WC, Hash);
    TestTrue (TEXT("parse ok"),   bOk);
    TestEqual(TEXT("workchain"), WC, -1);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
