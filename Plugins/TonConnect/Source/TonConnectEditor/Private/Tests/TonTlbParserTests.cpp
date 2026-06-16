#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "TonTlbParser.h"
#include "Contract/TonMessageSpec.h"

#if WITH_DEV_AUTOMATION_TESTS

// ── ParseLine — TEP-74 transfer ───────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTonTlb_ParseTransfer,
    "TonConnect.TlbParser.ParseTransfer",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTonTlb_ParseTransfer::RunTest(const FString&)
{
    const FString Line =
        TEXT("transfer#0f8a7ea5 query_id:uint64 amount:(VarUInteger 16) destination:MsgAddress = InternalMsgBody;");

    FTonTlbMessage Msg;
    TestTrue(TEXT("parse ok"), FTonTlbParser::ParseLine(Line, Msg));

    TestEqual(TEXT("name"),   Msg.Name,   TEXT("transfer"));
    TestEqual(TEXT("opcode"), Msg.Opcode, (int64)0x0f8a7ea5);
    TestTrue (TEXT("3+ fields"), Msg.Fields.Num() >= 3);

    // query_id: uint64 → UInt64
    TestEqual(TEXT("query_id name"), Msg.Fields[0].Name, TEXT("query_id"));
    TestEqual(TEXT("query_id type"), Msg.Fields[0].Type, ETonFieldType::UInt64);
    TestEqual(TEXT("query_id bits"), Msg.Fields[0].BitWidth, 64);

    // amount: (VarUInteger 16) → Coins
    TestEqual(TEXT("amount name"), Msg.Fields[1].Name,   TEXT("amount"));
    TestEqual(TEXT("amount type"), Msg.Fields[1].Type,   ETonFieldType::Coins);

    // destination: MsgAddress → Address
    TestEqual(TEXT("dest name"), Msg.Fields[2].Name,   TEXT("destination"));
    TestEqual(TEXT("dest type"), Msg.Fields[2].Type,   ETonFieldType::Address);
    return true;
}

// ── ParseLine — no opcode tag ─────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTonTlb_ParseNoTag,
    "TonConnect.TlbParser.ParseNoTag",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTonTlb_ParseNoTag::RunTest(const FString&)
{
    const FString Line = TEXT("some_message value:uint32 = SomeType;");
    FTonTlbMessage Msg;
    TestTrue(TEXT("parse ok"), FTonTlbParser::ParseLine(Line, Msg));
    TestEqual(TEXT("name"),   Msg.Name,   TEXT("some_message"));
    TestEqual(TEXT("opcode"), Msg.Opcode, (int64)0);
    TestEqual(TEXT("fields"), Msg.Fields.Num(), 1);
    TestEqual(TEXT("field type"), Msg.Fields[0].Type, ETonFieldType::UInt32);
    return true;
}

// ── ParseLine — invalid input ─────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTonTlb_ParseInvalid,
    "TonConnect.TlbParser.ParseInvalid",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTonTlb_ParseInvalid::RunTest(const FString&)
{
    FTonTlbMessage Msg;
    TestFalse(TEXT("empty"),     FTonTlbParser::ParseLine(TEXT(""), Msg));
    TestFalse(TEXT("no equals"), FTonTlbParser::ParseLine(TEXT("just_a_name field:uint8;"), Msg));
    return true;
}

// ── ParseFile — multi-line TEP-74 ─────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTonTlb_ParseFile,
    "TonConnect.TlbParser.ParseFile",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTonTlb_ParseFile::RunTest(const FString&)
{
    const FString TlbText =
        TEXT("// TEP-74 jetton wallet messages\n")
        TEXT("transfer#0f8a7ea5 query_id:uint64 amount:(VarUInteger 16) destination:MsgAddress = InternalMsgBody;\n")
        TEXT("burn#595f07bc query_id:uint64 amount:(VarUInteger 16) = InternalMsgBody;\n");

    TArray<FTonTlbMessage> Messages = FTonTlbParser::ParseFile(TlbText);
    TestEqual(TEXT("2 messages"), Messages.Num(), 2);

    TestEqual(TEXT("msg0 name"),   Messages[0].Name,   TEXT("transfer"));
    TestEqual(TEXT("msg0 opcode"), Messages[0].Opcode, (int64)0x0f8a7ea5);

    TestEqual(TEXT("msg1 name"),   Messages[1].Name,   TEXT("burn"));
    TestEqual(TEXT("msg1 opcode"), Messages[1].Opcode, (int64)0x595f07bc);

    return true;
}

// ── MapType coverage ──────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTonTlb_TypeMapping,
    "TonConnect.TlbParser.TypeMapping",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTonTlb_TypeMapping::RunTest(const FString&)
{
    // uint8
    {
        const FString L = TEXT("msg a:uint8 = T;");
        FTonTlbMessage M;
        FTonTlbParser::ParseLine(L, M);
        TestTrue(TEXT("uint8 parsed"), M.Fields.Num() > 0);
        TestEqual(TEXT("uint8 type"), M.Fields[0].Type, ETonFieldType::UInt8);
    }
    // int32
    {
        const FString L = TEXT("msg a:int32 = T;");
        FTonTlbMessage M;
        FTonTlbParser::ParseLine(L, M);
        TestTrue(TEXT("int32 parsed"), M.Fields.Num() > 0);
        TestEqual(TEXT("int32 type"), M.Fields[0].Type, ETonFieldType::Int32);
    }
    // Bool
    {
        const FString L = TEXT("msg a:Bool = T;");
        FTonTlbMessage M;
        FTonTlbParser::ParseLine(L, M);
        TestTrue(TEXT("Bool parsed"), M.Fields.Num() > 0);
        TestEqual(TEXT("Bool type"), M.Fields[0].Type, ETonFieldType::Bool);
    }
    // addr_std → Address
    {
        const FString L = TEXT("msg a:addr_std = T;");
        FTonTlbMessage M;
        FTonTlbParser::ParseLine(L, M);
        TestTrue(TEXT("addr_std parsed"), M.Fields.Num() > 0);
        TestEqual(TEXT("addr_std type"), M.Fields[0].Type, ETonFieldType::Address);
    }
    // Coins
    {
        const FString L = TEXT("msg a:Coins = T;");
        FTonTlbMessage M;
        FTonTlbParser::ParseLine(L, M);
        TestTrue(TEXT("Coins parsed"), M.Fields.Num() > 0);
        TestEqual(TEXT("Coins type"), M.Fields[0].Type, ETonFieldType::Coins);
    }
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
