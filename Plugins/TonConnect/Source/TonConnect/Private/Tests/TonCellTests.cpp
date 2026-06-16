#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Cells/TonCell.h"
#include "TonUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

// ── WriteUint ─────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTonCell_WriteUint,
    "TonConnect.Cell.WriteUint",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTonCell_WriteUint::RunTest(const FString&)
{
    {   // 0xDEADBEEF as 32 bits
        FTonCell C;
        C.WriteUint(0xDEADBEEFULL, 32);
        TestEqual(TEXT("bitlen"),  C.BitLen,    32);
        TestEqual(TEXT("bytes"),   C.Bytes.Num(), 4);
        TestEqual(TEXT("byte[0]"), C.Bytes[0],  (uint8)0xDE);
        TestEqual(TEXT("byte[1]"), C.Bytes[1],  (uint8)0xAD);
        TestEqual(TEXT("byte[2]"), C.Bytes[2],  (uint8)0xBE);
        TestEqual(TEXT("byte[3]"), C.Bytes[3],  (uint8)0xEF);
    }
    {   // Single set bit: value 1 in 1 bit → MSB = 0x80 in byte 0
        FTonCell C;
        C.WriteUint(1, 1);
        TestEqual(TEXT("bitlen 1"),  C.BitLen,    1);
        TestEqual(TEXT("byte val"),  C.Bytes[0],  (uint8)0x80);
    }
    {   // Zero in 8 bits
        FTonCell C;
        C.WriteUint(0, 8);
        TestEqual(TEXT("bitlen 8"), C.BitLen,   8);
        TestEqual(TEXT("zero"),     C.Bytes[0], (uint8)0x00);
    }
    return true;
}

// ── WriteVarUint ──────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTonCell_WriteVarUint,
    "TonConnect.Cell.WriteVarUint",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTonCell_WriteVarUint::RunTest(const FString&)
{
    {   // Val=0 → length nibble 0, no data bytes (4 bits total)
        FTonCell C;
        C.WriteVarUint(0, 4);
        TestEqual(TEXT("zero bitlen"), C.BitLen, 4);
        TestEqual(TEXT("zero byte"),   C.Bytes[0], (uint8)0x00);
    }
    {   // Val=1 → 1 byte needed → length=1 (4 bits) + data=0x01 (8 bits) = 12 bits
        FTonCell C;
        C.WriteVarUint(1, 4);
        TestEqual(TEXT("bitlen"),    C.BitLen,    12);
        // First 4 bits: 0001 → high nibble of byte[0] = 0x10
        TestEqual(TEXT("len nibble"), C.Bytes[0], (uint8)0x10);
        // Bits 4-11: value 0x01 → lower nibble of byte[0] = 0x00, byte[1] high nibble = 0x10
        // Byte layout: [0001 | 0000] [0001 | xxxx] → 0x10, 0x10
        TestEqual(TEXT("data"),      C.Bytes[1] & 0xF0, (uint8)0x10);
    }
    {   // Val=255 → 1 byte needed → 12 bits total
        FTonCell C;
        C.WriteVarUint(255, 4);
        TestEqual(TEXT("bitlen ff"),  C.BitLen, 12);
    }
    {   // Val=256 → 2 bytes → length=2 (4 bits) + 16 bits = 20 bits
        FTonCell C;
        C.WriteVarUint(256, 4);
        TestEqual(TEXT("bitlen 256"), C.BitLen, 20);
    }
    return true;
}

// ── TextComment ───────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTonCell_TextComment,
    "TonConnect.Cell.TextComment",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTonCell_TextComment::RunTest(const FString&)
{
    auto Cell = FTonCell::TextComment(TEXT("hi"));

    // op=0x00000000 (32 bits) + UTF-8 "hi" = 0x68,0x69 (16 bits) = 48 bits total
    TestEqual(TEXT("bitlen"),  Cell->BitLen,    48);
    TestEqual(TEXT("bytes"),   Cell->Bytes.Num(), 6);
    TestEqual(TEXT("op[0]"),   Cell->Bytes[0],  (uint8)0x00);
    TestEqual(TEXT("op[1]"),   Cell->Bytes[1],  (uint8)0x00);
    TestEqual(TEXT("op[2]"),   Cell->Bytes[2],  (uint8)0x00);
    TestEqual(TEXT("op[3]"),   Cell->Bytes[3],  (uint8)0x00);
    TestEqual(TEXT("'h'"),     Cell->Bytes[4],  (uint8)0x68);
    TestEqual(TEXT("'i'"),     Cell->Bytes[5],  (uint8)0x69);
    return true;
}

// ── ToBocBase64 non-empty ─────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTonCell_BocNonEmpty,
    "TonConnect.Cell.BocBase64NonEmpty",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTonCell_BocNonEmpty::RunTest(const FString&)
{
    FTonCell C;
    C.WriteUint(0, 32);
    const FString Boc = C.ToBocBase64();
    TestFalse(TEXT("non-empty"), Boc.IsEmpty());
    // BOC magic in base64 starts with "te6c" (0xB5EE9C72 base64url encoded)
    TestTrue(TEXT("magic prefix"), Boc.StartsWith(TEXT("te6c")));
    return true;
}

// ── WriteAddress bit layout ───────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTonCell_WriteAddress,
    "TonConnect.Cell.WriteAddress",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTonCell_WriteAddress::RunTest(const FString&)
{
    // addr_std: 2 (type) + 1 (anycast) + 8 (workchain) + 256 (hash) = 267 bits
    TArray<uint8> Hash;
    Hash.SetNumZeroed(32);

    FTonCell C;
    C.WriteAddress(0, Hash);
    TestEqual(TEXT("addr bitlen"), C.BitLen, 267);

    // First 3 bits: 10 0 → byte[0] MSBs: 1000 0000 = 0x80
    TestEqual(TEXT("type+anycast"), C.Bytes[0] & 0xE0, (uint8)0x80);
    return true;
}

// ── JettonTransfer refs ───────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTonCell_JettonTransfer,
    "TonConnect.Cell.JettonTransfer",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTonCell_JettonTransfer::RunTest(const FString&)
{
    TArray<uint8> Hash32;
    Hash32.SetNumZeroed(32);
    auto Cell = FTonCell::JettonTransfer(0, Hash32, 0, Hash32, 1000000, 1);

    TestFalse(TEXT("not empty"), Cell->ToBocBase64().IsEmpty());

    // No child refs — forward_payload is inline (0-bit)
    TestEqual(TEXT("refs"), Cell->Refs.Num(), 0);

    // op = 0x0f8a7ea5 written in first 4 bytes
    TestEqual(TEXT("op[0]"), Cell->Bytes[0], (uint8)0x0F);
    TestEqual(TEXT("op[1]"), Cell->Bytes[1], (uint8)0x8A);
    TestEqual(TEXT("op[2]"), Cell->Bytes[2], (uint8)0x7E);
    TestEqual(TEXT("op[3]"), Cell->Bytes[3], (uint8)0xA5);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
