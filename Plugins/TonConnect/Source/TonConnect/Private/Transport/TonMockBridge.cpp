#include "Transport/TonMockBridge.h"
#include "TonConnectDeveloperSettings.h"
#include "Misc/Base64.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"

void FTonMockBridge::Connect(const FString& BridgeUrl, const FString& ClientId)
{
    bActive = true;

    const UTonConnectDeveloperSettings* S = GetDefault<UTonConnectDeveloperSettings>();
    float Delay = S ? S->MockDelaySeconds : 1.0f;
    ETonMockResult Result = S ? S->MockConnectResult : ETonMockResult::Approve;

    if (Result == ETonMockResult::Timeout) return;

    UWorld* World = GameWorld.Get();
    if (!World) return;

    FTimerDelegate Delegate;
    Delegate.BindLambda([this, Result]()
    {
        if (!bActive) return;

        if (Result == ETonMockResult::Reject)
        {
            OnError.Broadcast(TEXT("Mock: wallet rejected connection"));
            return;
        }

        // Emit a synthetic connect event — mirror the configured network so the
        // subsystem's network-mismatch check always passes in mock mode.
        FString NetworkId = TEXT("-239"); // mainnet default
        if (const UTonConnectDeveloperSettings* Cfg = GetDefault<UTonConnectDeveloperSettings>())
        {
            FString Id = Cfg->GetNetworkId();
            if (!Id.IsEmpty()) NetworkId = Id;
        }

        const FString ConnectJson = FString::Printf(
            TEXT("{\"event\":\"connect\",\"id\":\"1\",\"payload\":{")
            TEXT("\"items\":[{")
                TEXT("\"name\":\"ton_addr\",")
                TEXT("\"address\":\"0:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",")
                TEXT("\"network\":\"%s\",")
                TEXT("\"publicKey\":\"aabbccddeeff00112233445566778899aabbccddeeff00112233445566778899\",")
                TEXT("\"walletStateInit\":\"te6cckEBAQEAAgAAAEysuc0=\"")
            TEXT("}],")
            TEXT("\"device\":{")
                TEXT("\"platform\":\"android\",")
                TEXT("\"appName\":\"MockWallet\",")
                TEXT("\"appVersion\":\"1.0\",")
                TEXT("\"features\":[")
                    TEXT("\"SendTransaction\",")
                    TEXT("{\"name\":\"SendTransaction\",\"maxMessages\":4},")
                    TEXT("{\"name\":\"SignData\",\"types\":[\"text\",\"binary\",\"cell\"]}")
                TEXT("]")
            TEXT("}")
            TEXT("}}"),
            *NetworkId);

        OnEvent.Broadcast(TEXT("mock-1"), ConnectJson);
    });
    World->GetTimerManager().SetTimer(ConnectTimer, Delegate, Delay, false);
}

void FTonMockBridge::Disconnect()
{
    bActive = false;
    if (UWorld* World = GameWorld.Get())
    {
        World->GetTimerManager().ClearTimer(ConnectTimer);
        World->GetTimerManager().ClearTimer(SendTimer);
    }
}

void FTonMockBridge::SendMessage(const FString& To, const FString& Topic, const FString& BodyBase64,
                                  TFunction<void(bool)> Callback)
{
    LastSentBodyBase64 = BodyBase64;
    if (Callback) Callback(true);

    const UTonConnectDeveloperSettings* S = GetDefault<UTonConnectDeveloperSettings>();
    float Delay = S ? S->MockDelaySeconds : 1.0f;
    ETonMockResult Result = S ? S->MockSendResult : ETonMockResult::Approve;

    // Echo back the request's RPC id so each send resolves its own pending result.
    // In mock mode the body is base64(plaintext RPC JSON) — decode and read "id".
    FString RpcId = TEXT("1");
    FString Decoded;
    if (FBase64::Decode(BodyBase64, Decoded))
    {
        TSharedPtr<FJsonObject> Obj;
        TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Decoded);
        if (FJsonSerializer::Deserialize(Reader, Obj) && Obj.IsValid())
        {
            FString ParsedId;
            if (Obj->TryGetStringField(TEXT("id"), ParsedId) && !ParsedId.IsEmpty())
                RpcId = ParsedId;
        }
    }

    UWorld* World = GameWorld.Get();
    if (!World) return;

    FTimerDelegate Delegate;
    Delegate.BindLambda([this, Result, RpcId]()
    {
        if (!bActive) return;

        FString ResponseJson;
        if (Result == ETonMockResult::Reject)
        {
            ResponseJson = FString::Printf(
                TEXT("{\"id\":\"%s\",\"error\":{\"code\":300,\"message\":\"Rejected by user\"}}"), *RpcId);
        }
        else if (Result == ETonMockResult::Timeout)
        {
            ResponseJson = FString::Printf(
                TEXT("{\"id\":\"%s\",\"error\":{\"code\":408,\"message\":\"Request timeout\"}}"), *RpcId);
        }
        else
        {
            ResponseJson = FString::Printf(
                TEXT("{\"id\":\"%s\",\"result\":\"mockboc_base64\"}"), *RpcId);
        }

        OnEvent.Broadcast(TEXT("mock-send"), ResponseJson);
    });
    World->GetTimerManager().SetTimer(SendTimer, Delegate, Delay, false);
}
