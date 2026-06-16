#pragma once

#include "CoreMinimal.h"

// Interface for persisting session keypairs securely between runs.
// P2 implementations: DPAPI (Win64), secretbox fallback (other platforms).
class TONCONNECT_API ISessionStore
{
public:
    virtual ~ISessionStore() = default;

    // Save keypair. Returns false on failure.
    virtual bool Save(const TArray<uint8>& PublicKey, const TArray<uint8>& PrivateKey) = 0;

    // Load keypair. Returns false if no saved data or decryption fails.
    virtual bool Load(TArray<uint8>& OutPublicKey, TArray<uint8>& OutPrivateKey) = 0;

    // Erase stored keys (on logout / hard disconnect).
    virtual void Clear() = 0;

    // Factory — returns the best available implementation for the current platform.
    static TSharedPtr<ISessionStore> Create();
};
