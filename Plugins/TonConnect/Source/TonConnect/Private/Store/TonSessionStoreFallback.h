#pragma once

#include "CoreMinimal.h"
#include "ISessionStore.h"

// Cross-platform session store using tweetnacl crypto_secretbox (XSalsa20-Poly1305).
// The encryption key is derived from the hardware device ID + a static salt via SHA-256.
// Not as strong as OS keychain/DPAPI but prevents plaintext keys on disk.
class FTonSessionStoreFallback : public ISessionStore
{
public:
    virtual bool Save(const TArray<uint8>& PublicKey, const TArray<uint8>& PrivateKey) override;
    virtual bool Load(TArray<uint8>& OutPublicKey, TArray<uint8>& OutPrivateKey) override;
    virtual void Clear() override;

private:
    static FString GetStorePath();
    static TArray<uint8> DeriveKey(); // 32-byte key from device ID
};
