#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "TonWalletSave.generated.h"

// Persists wallet connection state between game sessions.
// Non-sensitive (no private keys — those live in ISessionStore).
UCLASS()
class UTonWalletSave : public USaveGame
{
    GENERATED_BODY()
public:
    UPROPERTY() FString Address;
    UPROPERTY() FString RawAddress;
    UPROPERTY() FString PublicKey;
    UPROPERTY() FString WalletName;
    UPROPERTY() FString WalletAppName; // which wallet app we paired with (e.g. "tonkeeper")
    UPROPERTY() FString WalletVersion;
    UPROPERTY() FString Network;
    UPROPERTY() FString StateInit;
    UPROPERTY() FString ActiveBridgeUrl;
    UPROPERTY() FString WalletBridgePublicKeyHex;
    UPROPERTY() FString BalanceNano;
};
