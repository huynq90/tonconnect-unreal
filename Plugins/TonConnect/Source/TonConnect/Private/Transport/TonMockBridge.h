#pragma once

#include "CoreMinimal.h"
#include "ITonBridgeTransport.h"

// Mock SSE bridge — fires synthetic events after a configurable delay.
// Connect result and send result are controlled via UTonConnectDeveloperSettings.
class FTonMockBridge : public ITonBridgeTransport
{
public:
    virtual void Connect(const FString& BridgeUrl, const FString& ClientId) override;
    virtual void Disconnect() override;
    virtual void SendMessage(const FString& To, const FString& Topic, const FString& BodyBase64,
                             TFunction<void(bool)> Callback) override;

    TWeakObjectPtr<UWorld> GameWorld;

    // Captured by SendMessage() — readable from automation tests
    FString LastSentBodyBase64;

private:
    bool bActive = false;
    FTimerHandle ConnectTimer;
    FTimerHandle SendTimer;
};
