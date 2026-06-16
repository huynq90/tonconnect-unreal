#pragma once

#include "CoreMinimal.h"
#include "ISessionStore.h"

// Win64 DPAPI-backed session store.
// Keys are encrypted with CryptProtectData (current user scope) before writing to disk.
// The save file lives in the user's AppData via FPaths::ProjectSavedDir().
class FTonSessionStoreDpapi : public ISessionStore
{
public:
    virtual bool Save(const TArray<uint8>& PublicKey, const TArray<uint8>& PrivateKey) override;
    virtual bool Load(TArray<uint8>& OutPublicKey, TArray<uint8>& OutPrivateKey) override;
    virtual void Clear() override;

private:
    static FString GetStorePath();
};
