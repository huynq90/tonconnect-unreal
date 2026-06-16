#pragma once

#include "CoreMinimal.h"
#include "ITonBridgeTransport.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"

// Real SSE bridge transport — connects to bridge.tonapi.io via UE HTTP streaming.
// Handles reconnection with exponential backoff, last_event_id resume,
// and a heartbeat watchdog that reconnects if no data arrives for HeartbeatTimeoutSec.
class FTonHttpBridge : public ITonBridgeTransport
{
public:
    FTonHttpBridge();
    virtual ~FTonHttpBridge() override;

    virtual void Connect(const FString& BridgeUrl, const FString& ClientId) override;
    virtual void Disconnect() override;
    virtual void SendMessage(const FString& To, const FString& Topic, const FString& BodyBase64,
                             TFunction<void(bool)> Callback) override;

    // Set before calling Connect(). Used for timers.
    TWeakObjectPtr<UWorld> GameWorld;

    // Seconds of silence before the watchdog triggers a reconnect (default 30s).
    float HeartbeatTimeoutSec = 30.0f;

private:
    FHttpModule* Http = nullptr;
    TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> SSERequest;
    FTimerHandle ReconnectTimer;
    FTimerHandle HeartbeatTimer;

    FString StoredBridgeUrl;
    FString StoredClientId;
    FString LastEventId;
    FString SSEBuffer;
    float ReconnectDelay = 1.0f;
    bool bActive = false;

    static constexpr float MaxReconnectDelay = 30.0f;

    void StartSSERequest();
    void OnStreamData(void* InDataPtr, int64& InOutLength);
    void OnRequestComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSuccess);
    void ScheduleReconnect();
    void ResetHeartbeatWatchdog();
    void OnHeartbeatTimeout();
    void ParseBuffer();
};
