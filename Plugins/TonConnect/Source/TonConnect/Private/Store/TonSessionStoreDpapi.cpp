#include "Store/TonSessionStoreDpapi.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Misc/DateTime.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <wincrypt.h>
#pragma comment(lib, "Crypt32.lib")
#include "Windows/HideWindowsPlatformTypes.h"

static bool DpapiEncrypt(const TArray<uint8>& In, TArray<uint8>& Out)
{
    DATA_BLOB PlainBlob{ (DWORD)In.Num(), (BYTE*)In.GetData() };
    DATA_BLOB CipherBlob{};

    if (!CryptProtectData(&PlainBlob, L"TonConnect", nullptr, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &CipherBlob))
    {
        return false;
    }

    Out.SetNum((int32)CipherBlob.cbData);
    FMemory::Memcpy(Out.GetData(), CipherBlob.pbData, CipherBlob.cbData);
    LocalFree(CipherBlob.pbData);
    return true;
}

static bool DpapiDecrypt(const TArray<uint8>& In, TArray<uint8>& Out)
{
    DATA_BLOB CipherBlob{ (DWORD)In.Num(), (BYTE*)In.GetData() };
    DATA_BLOB PlainBlob{};

    if (!CryptUnprotectData(&CipherBlob, nullptr, nullptr, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &PlainBlob))
    {
        return false;
    }

    Out.SetNum((int32)PlainBlob.cbData);
    FMemory::Memcpy(Out.GetData(), PlainBlob.pbData, PlainBlob.cbData);
    LocalFree(PlainBlob.pbData);
    return true;
}

#endif // PLATFORM_WINDOWS

FString FTonSessionStoreDpapi::GetStorePath()
{
    return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("TonConnect"), TEXT("session.bin"));
}

bool FTonSessionStoreDpapi::Save(const TArray<uint8>& PublicKey, const TArray<uint8>& PrivateKey)
{
#if PLATFORM_WINDOWS
    // Layout: [8B timestamp int64 LE][4B pubLen][pubkey][4B privLen][privkey]
    TArray<uint8> Plain;
    int64 Now = FDateTime::UtcNow().ToUnixTimestamp();
    Plain.Append(reinterpret_cast<uint8*>(&Now), 8);
    int32 PubLen = PublicKey.Num();
    int32 PrivLen = PrivateKey.Num();
    Plain.Append(reinterpret_cast<uint8*>(&PubLen), 4);
    Plain.Append(PublicKey);
    Plain.Append(reinterpret_cast<uint8*>(&PrivLen), 4);
    Plain.Append(PrivateKey);

    TArray<uint8> Encrypted;
    if (!DpapiEncrypt(Plain, Encrypted))
    {
        UE_LOG(LogTemp, Error, TEXT("TonConnect DPAPI: encrypt failed (error %u)"), GetLastError());
        return false;
    }

    FString Path = GetStorePath();
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
    return FFileHelper::SaveArrayToFile(Encrypted, *Path);
#else
    return false;
#endif
}

bool FTonSessionStoreDpapi::Load(TArray<uint8>& OutPublicKey, TArray<uint8>& OutPrivateKey)
{
#if PLATFORM_WINDOWS
    // No session yet on first run — skip quietly (LoadFileToArray would log a warning).
    if (!IFileManager::Get().FileExists(*GetStorePath())) return false;

    TArray<uint8> Encrypted;
    if (!FFileHelper::LoadFileToArray(Encrypted, *GetStorePath())) return false;

    TArray<uint8> Plain;
    if (!DpapiDecrypt(Encrypted, Plain)) return false;

    int32 Offset = 0;
    if (Plain.Num() < 8 + 4 + 4) return false;

    // Read and validate timestamp (TTL = 21 days)
    int64 Timestamp = *reinterpret_cast<int64*>(Plain.GetData() + Offset); Offset += 8;
    const int64 ThreeWeeksSec = 21 * 24 * 3600;
    if (FDateTime::UtcNow().ToUnixTimestamp() - Timestamp > ThreeWeeksSec)
    {
        Clear();
        return false;
    }

    int32 PubLen = *reinterpret_cast<int32*>(Plain.GetData() + Offset); Offset += 4;
    if (Offset + PubLen > Plain.Num()) return false;
    OutPublicKey = TArray<uint8>(Plain.GetData() + Offset, PubLen); Offset += PubLen;

    if (Offset + 4 > Plain.Num()) return false;
    int32 PrivLen = *reinterpret_cast<int32*>(Plain.GetData() + Offset); Offset += 4;
    if (Offset + PrivLen > Plain.Num()) return false;
    OutPrivateKey = TArray<uint8>(Plain.GetData() + Offset, PrivLen);

    return OutPublicKey.Num() == 32 && OutPrivateKey.Num() == 32;
#else
    return false;
#endif
}

void FTonSessionStoreDpapi::Clear()
{
    IFileManager::Get().Delete(*GetStorePath());
}
