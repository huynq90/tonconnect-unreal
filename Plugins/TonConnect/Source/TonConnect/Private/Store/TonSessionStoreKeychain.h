#pragma once

#include "CoreMinimal.h"
#include "ISessionStore.h"

// iOS Keychain session store.
// Uses SecItemAdd / SecItemCopyMatching from Security.framework.
// Session keys are stored in the kSecClassGenericPassword class under a fixed service name.
class FTonSessionStoreKeychain : public ISessionStore
{
public:
    virtual bool Save(const TArray<uint8>& PublicKey, const TArray<uint8>& PrivateKey) override;
    virtual bool Load(TArray<uint8>& OutPublicKey, TArray<uint8>& OutPrivateKey) override;
    virtual void Clear() override;

private:
    static const char* ServiceName;  // "com.tonconnect.session"
    static const char* AccountName;  // "keypair"
};
