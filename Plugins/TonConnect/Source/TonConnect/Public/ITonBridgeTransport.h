#pragma once

#include "CoreMinimal.h"

DECLARE_MULTICAST_DELEGATE_TwoParams(FOnBridgeEvent, const FString& /*EventId*/, const FString& /*RawJson*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnBridgeError, const FString& /*Reason*/);

// Transport interface — separates SSE bridge from business logic and enables mock injection.
class TONCONNECT_API ITonBridgeTransport
{
public:
    virtual ~ITonBridgeTransport() = default;

    // Start SSE connection to the bridge. ClientId is the hex client identifier.
    virtual void Connect(const FString& BridgeUrl, const FString& ClientId) = 0;

    // Disconnect and clean up.
    virtual void Disconnect() = 0;

    // Send an encrypted message to the wallet via bridge HTTP POST.
    // To: wallet's bridge client id. Body: base64-encoded NaCl box ciphertext.
    virtual void SendMessage(
        const FString& To,
        const FString& Topic,
        const FString& BodyBase64,
        TFunction<void(bool /*bSuccess*/)> Callback) = 0;

    // Fired for every SSE event received.
    FOnBridgeEvent OnEvent;

    // Fired on connection error or unexpected close.
    FOnBridgeError OnError;
};
