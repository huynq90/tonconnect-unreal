#include "Transport/TonHttpBridge.h"
#include "Async/TaskGraphInterfaces.h"

FTonHttpBridge::FTonHttpBridge()
    : Http(&FHttpModule::Get())
{
}

FTonHttpBridge::~FTonHttpBridge()
{
    Disconnect();
}

void FTonHttpBridge::Connect(const FString& BridgeUrl, const FString& ClientId)
{
    StoredBridgeUrl = BridgeUrl;
    StoredClientId = ClientId;
    ReconnectDelay = 1.0f;
    bActive = true;
    StartSSERequest();
}

void FTonHttpBridge::Disconnect()
{
    bActive = false;
    if (UWorld* World = GameWorld.Get())
    {
        World->GetTimerManager().ClearTimer(ReconnectTimer);
        World->GetTimerManager().ClearTimer(HeartbeatTimer);
    }
    if (SSERequest.IsValid())
    {
        SSERequest->CancelRequest();
        SSERequest.Reset();
    }
}

void FTonHttpBridge::StartSSERequest()
{
    if (!bActive) return;

    FString Url = StoredBridgeUrl + TEXT("/events?client_id=") + StoredClientId;
    if (!LastEventId.IsEmpty())
    {
        Url += TEXT("&last_event_id=") + LastEventId;
    }

    SSEBuffer.Empty();
    SSERequest = Http->CreateRequest();
    SSERequest->SetURL(Url);
    SSERequest->SetVerb(TEXT("GET"));
    SSERequest->SetHeader(TEXT("Accept"), TEXT("text/event-stream"));

    FHttpRequestStreamDelegateV2 StreamDelegate;
    StreamDelegate.BindRaw(this, &FTonHttpBridge::OnStreamData);
    SSERequest->SetResponseBodyReceiveStreamDelegateV2(StreamDelegate);

    SSERequest->OnProcessRequestComplete().BindRaw(this, &FTonHttpBridge::OnRequestComplete);
    SSERequest->ProcessRequest();

    ResetHeartbeatWatchdog();
}

void FTonHttpBridge::OnStreamData(void* InDataPtr, int64& InOutLength)
{
    if (!bActive) return;

    // Convert bytes to FString on the HTTP thread while the buffer is still valid.
    FUTF8ToTCHAR Converter(static_cast<const ANSICHAR*>(InDataPtr), (int32)InOutLength);
    FString Chunk(Converter.Get());

    // All timer and state mutations must happen on the game thread.
    AsyncTask(ENamedThreads::GameThread, [this, Chunk = MoveTemp(Chunk)]()
    {
        if (!bActive) return;
        ResetHeartbeatWatchdog();
        SSEBuffer += Chunk;
        ParseBuffer();
    });
}

void FTonHttpBridge::OnRequestComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSuccess)
{
    if (!bActive) return;

    if (UWorld* World = GameWorld.Get())
    {
        World->GetTimerManager().ClearTimer(HeartbeatTimer);
    }
    ScheduleReconnect();
}

void FTonHttpBridge::ResetHeartbeatWatchdog()
{
    UWorld* World = GameWorld.Get();
    if (!World || HeartbeatTimeoutSec <= 0.0f) return;

    FTimerDelegate Delegate;
    Delegate.BindRaw(this, &FTonHttpBridge::OnHeartbeatTimeout);
    // SetTimer with bLoop=false: clears and restarts the timer each call
    World->GetTimerManager().SetTimer(HeartbeatTimer, Delegate, HeartbeatTimeoutSec, false);
}

void FTonHttpBridge::OnHeartbeatTimeout()
{
    if (!bActive) return;

    UE_LOG(LogTemp, Warning,
           TEXT("TonConnect: no SSE data for %.0fs — reconnecting"), HeartbeatTimeoutSec);

    if (SSERequest.IsValid())
    {
        SSERequest->CancelRequest();
        SSERequest.Reset();
    }

    // Reconnect immediately (keep existing backoff delay)
    StartSSERequest();
}

void FTonHttpBridge::ScheduleReconnect()
{
    UE_LOG(LogTemp, Log, TEXT("TonConnect: SSE disconnected, retry in %.1fs"), ReconnectDelay);

    UWorld* World = GameWorld.Get();
    if (!World) return;

    FTimerDelegate Delegate;
    Delegate.BindLambda([this]() { StartSSERequest(); });
    World->GetTimerManager().SetTimer(ReconnectTimer, Delegate, ReconnectDelay, false);

    ReconnectDelay = FMath::Min(ReconnectDelay * 2.0f, MaxReconnectDelay);
}

void FTonHttpBridge::ParseBuffer()
{
    TArray<FString> Events;
    SSEBuffer.ParseIntoArray(Events, TEXT("\n\n"), true);

    for (const FString& Event : Events)
    {
        if (Event.IsEmpty() || Event.Contains(TEXT("event: heartbeat"))) continue;

        TArray<FString> Lines;
        Event.ParseIntoArray(Lines, TEXT("\n"), false);

        FString EventId, EventData;
        for (const FString& Line : Lines)
        {
            if (Line.StartsWith(TEXT("id: ")))        EventId   = Line.RightChop(4);
            else if (Line.StartsWith(TEXT("data: "))) EventData = Line.RightChop(6);
        }

        if (!EventId.IsEmpty())   LastEventId = EventId;
        if (!EventData.IsEmpty())
        {
            ReconnectDelay = 1.0f; // successful data — reset backoff
            OnEvent.Broadcast(EventId, EventData);
        }
    }

    // Keep only the partial event after the last complete \n\n boundary
    int32 LastEnd = SSEBuffer.Find(TEXT("\n\n"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
    if (LastEnd != INDEX_NONE)
    {
        SSEBuffer = SSEBuffer.Mid(LastEnd + 2);
    }
}

void FTonHttpBridge::SendMessage(const FString& To, const FString& Topic, const FString& BodyBase64,
                                  TFunction<void(bool)> Callback)
{
    FString Url = StoredBridgeUrl
        + TEXT("/message?client_id=") + StoredClientId
        + TEXT("&to=") + To
        + TEXT("&ttl=300&topic=") + Topic;

    UE_LOG(LogTemp, Log, TEXT("TonConnect: POST %s"), *Url);

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = Http->CreateRequest();
    Request->SetURL(Url);
    Request->SetVerb(TEXT("POST"));
    Request->SetHeader(TEXT("Content-Type"), TEXT("text/plain"));
    Request->SetContentAsString(BodyBase64);

    Request->OnProcessRequestComplete().BindLambda(
        [Callback, Url](FHttpRequestPtr Req, FHttpResponsePtr Resp, bool bSuccess)
        {
            int32 Code = Resp.IsValid() ? Resp->GetResponseCode() : 0;
            FString Body = Resp.IsValid() ? Resp->GetContentAsString() : TEXT("(no response)");
            bool bOk = bSuccess && Code == 200;
            UE_LOG(LogTemp, Log, TEXT("TonConnect: bridge POST → %d  body: %s"), Code, *Body);
            if (Callback) Callback(bOk);
        });

    Request->ProcessRequest();
}
