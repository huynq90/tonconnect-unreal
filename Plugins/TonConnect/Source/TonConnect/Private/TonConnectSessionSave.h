#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "TonConnectSessionSave.generated.h"

// Persists session x25519 keypair between game runs so RestoreSession() works.
UCLASS()
class UTonConnectSessionSave : public USaveGame
{
    GENERATED_BODY()
public:
    UPROPERTY() TArray<uint8> PublicKey;
    UPROPERTY() TArray<uint8> PrivateKey;
};
