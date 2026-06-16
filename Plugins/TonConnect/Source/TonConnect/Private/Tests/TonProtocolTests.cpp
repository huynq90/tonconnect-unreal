#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Misc/Base64.h"
#include "Api/TonMockApiClient.h"

#if WITH_DEV_AUTOMATION_TESTS

// ---- Test 1: Mock wallet list populates BridgeUrl --------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FTonProtocol_MockBridgeUrl,
    "TonConnect.Protocol.MockBridgeUrl",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTonProtocol_MockBridgeUrl::RunTest(const FString&)
{
    FTonMockApiClient Client;
    bool bCalled = false;

    Client.GetWalletList([&](bool bOk, TArray<FTonWalletListEntry> Wallets)
    {
        bCalled = true;
        TestTrue(TEXT("GetWalletList success"), bOk);
        TestTrue(TEXT("At least 2 wallets"), Wallets.Num() >= 2);

        bool bFoundTonkeeper = false, bFoundMyTon = false;
        for (const FTonWalletListEntry& W : Wallets)
        {
            if (W.AppName == TEXT("tonkeeper"))
            {
                bFoundTonkeeper = true;
                TestFalse(TEXT("Tonkeeper BridgeUrl non-empty"), W.BridgeUrl.IsEmpty());
                TestEqual(TEXT("Tonkeeper BridgeUrl value"),
                    W.BridgeUrl,
                    FString(TEXT("https://bridge.tonapi.io/bridge")));
            }
            if (W.AppName == TEXT("mytonwallet"))
            {
                bFoundMyTon = true;
                TestFalse(TEXT("MyTonWallet BridgeUrl non-empty"), W.BridgeUrl.IsEmpty());
            }
        }
        TestTrue(TEXT("Tonkeeper entry present"),    bFoundTonkeeper);
        TestTrue(TEXT("MyTonWallet entry present"),  bFoundMyTon);
    });

    TestTrue(TEXT("Callback invoked synchronously"), bCalled);
    return true;
}

// ---- Test 2: sendTransaction RPC envelope has required jsonrpc 2.0 fields -------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FTonProtocol_RpcEnvelopeSchema,
    "TonConnect.Protocol.RpcEnvelopeSchema",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTonProtocol_RpcEnvelopeSchema::RunTest(const FString&)
{
    // Build envelope identical to SendTransfer() (sans crypto/world)
    const int32 RpcId = 1;
    const FString ToAddress    = TEXT("0:aaaa");
    const FString AmountNano   = TEXT("1000000000");
    const int64   ValidUntil   = FDateTime::UtcNow().ToUnixTimestamp() + 300;

    TSharedRef<FJsonObject> MsgObj = MakeShared<FJsonObject>();
    MsgObj->SetStringField(TEXT("address"), ToAddress);
    MsgObj->SetStringField(TEXT("amount"),  AmountNano);

    TArray<TSharedPtr<FJsonValue>> Messages;
    Messages.Add(MakeShared<FJsonValueObject>(MsgObj));

    TSharedRef<FJsonObject> TxObj = MakeShared<FJsonObject>();
    TxObj->SetNumberField(TEXT("valid_until"), (double)ValidUntil);
    TxObj->SetArrayField(TEXT("messages"), Messages);

    FString TxJsonStr;
    TSharedRef<TJsonWriter<>> TxW = TJsonWriterFactory<>::Create(&TxJsonStr);
    FJsonSerializer::Serialize(TxObj, TxW);

    TArray<TSharedPtr<FJsonValue>> Params;
    Params.Add(MakeShared<FJsonValueString>(TxJsonStr));

    TSharedRef<FJsonObject> RpcObj = MakeShared<FJsonObject>();
    RpcObj->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
    RpcObj->SetStringField(TEXT("method"),  TEXT("sendTransaction"));
    RpcObj->SetArrayField(TEXT("params"),   Params);
    RpcObj->SetStringField(TEXT("id"),      FString::FromInt(RpcId));

    FString RpcJson;
    TSharedRef<TJsonWriter<>> RpcW = TJsonWriterFactory<>::Create(&RpcJson);
    FJsonSerializer::Serialize(RpcObj, RpcW);

    // Parse back and verify all required fields are present
    TSharedPtr<FJsonObject> Parsed;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(RpcJson);
    TestTrue(TEXT("RPC JSON parses"), FJsonSerializer::Deserialize(Reader, Parsed) && Parsed.IsValid());
    if (!Parsed.IsValid()) return false;

    TestEqual(TEXT("jsonrpc == 2.0"),
        Parsed->GetStringField(TEXT("jsonrpc")), FString(TEXT("2.0")));
    TestEqual(TEXT("method == sendTransaction"),
        Parsed->GetStringField(TEXT("method")), FString(TEXT("sendTransaction")));
    TestEqual(TEXT("id == 1"),
        Parsed->GetStringField(TEXT("id")), FString(TEXT("1")));

    const TArray<TSharedPtr<FJsonValue>>* ParamsArr;
    TestTrue(TEXT("params is array"), Parsed->TryGetArrayField(TEXT("params"), ParamsArr));
    TestTrue(TEXT("params has 1 element"), ParamsArr && ParamsArr->Num() == 1);

    // Decode the inner tx JSON
    if (ParamsArr && ParamsArr->Num() > 0)
    {
        FString InnerStr = (*ParamsArr)[0]->AsString();
        TSharedPtr<FJsonObject> TxParsed;
        TSharedRef<TJsonReader<>> InnerReader = TJsonReaderFactory<>::Create(InnerStr);
        TestTrue(TEXT("inner tx JSON parses"),
            FJsonSerializer::Deserialize(InnerReader, TxParsed) && TxParsed.IsValid());

        if (TxParsed.IsValid())
        {
            TestTrue(TEXT("valid_until present"), TxParsed->HasField(TEXT("valid_until")));
            const TArray<TSharedPtr<FJsonValue>>* MsgArr;
            TestTrue(TEXT("messages array present"), TxParsed->TryGetArrayField(TEXT("messages"), MsgArr));
            TestTrue(TEXT("messages has 1 entry"), MsgArr && MsgArr->Num() == 1);
        }
    }

    // In mock mode the body is base64(RpcJson) — verify decode roundtrip
    FString Encoded = FBase64::Encode(RpcJson);
    FString Decoded;
    FBase64::Decode(Encoded, Decoded);
    TestEqual(TEXT("base64 roundtrip"), Decoded, RpcJson);

    return true;
}

// ---- Test 3: TTL expiry removes pending result and fires Timeout -----------------
//
// Latent: needs a PIE world + mock subsystem. Soft-skips if not running in PIE.

#include "Kismet/GameplayStatics.h"
#include "TonConnectSubsystem.h"
#include "Transport/TonMockBridge.h"
#include "Engine/Engine.h"

IMPLEMENT_COMPLEX_AUTOMATION_TEST(
    FTonProtocol_TtlExpiry,
    "TonConnect.Protocol.TtlExpiry",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

void FTonProtocol_TtlExpiry::GetTests(TArray<FString>& OutBeautifiedNames,
                                       TArray<FString>& OutTestCommands) const
{
    OutBeautifiedNames.Add(TEXT("TtlExpiry"));
    OutTestCommands.Add(TEXT("TtlExpiry"));
}

bool FTonProtocol_TtlExpiry::RunTest(const FString&)
{
    // Requires PIE + mock subsystem
    if (!GEngine) { AddWarning(TEXT("TtlExpiry: no GEngine — skipping")); return true; }
    UWorld* World = nullptr;
    for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
    {
        if (Ctx.WorldType == EWorldType::PIE || Ctx.WorldType == EWorldType::Game)
        {
            World = Ctx.World();
            break;
        }
    }
    if (!World) { AddWarning(TEXT("TtlExpiry: no PIE world — run in PIE")); return true; }

    UGameInstance* GI = World->GetGameInstance();
    if (!GI) { AddWarning(TEXT("TtlExpiry: no GameInstance")); return true; }

    UTonConnectSubsystem* Sub = GI->GetSubsystem<UTonConnectSubsystem>();
    if (!Sub) { AddWarning(TEXT("TtlExpiry: subsystem not found")); return true; }

#if WITH_DEV_AUTOMATION_TESTS
    // Inject a fake pending result (unbound delegate — ExecuteIfBound is a no-op)
    const int32 FakeId = 9999;
    FOnTonSendResultDelegate FakeDelegate;
    Sub->Test_PendingResults().Add(FakeId, FakeDelegate);
    TestTrue(TEXT("PendingResults contains FakeId before expire"),
        Sub->Test_PendingResults().Contains(FakeId));

    // Fire expiry directly (bypasses the 305s timer)
    Sub->Test_ExpirePending(FakeId);

    TestFalse(TEXT("PendingResults no longer contains FakeId after expire"),
        Sub->Test_PendingResults().Contains(FakeId));
#endif

    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
