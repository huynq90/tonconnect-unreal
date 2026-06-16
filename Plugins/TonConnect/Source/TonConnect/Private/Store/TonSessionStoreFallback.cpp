#include "Store/TonSessionStoreFallback.h"
#include "TonSession.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/DateTime.h"
#include "HAL/FileManager.h"
#include "GenericPlatform/GenericPlatformMisc.h"

THIRD_PARTY_INCLUDES_START
#pragma push_macro("check")
#undef check
extern "C" {
#include "tweetnacl.h"
}
#pragma pop_macro("check")
THIRD_PARTY_INCLUDES_END

extern "C" void randombytes(unsigned char* buf, unsigned long long len);

// Static salt mixed into the device-derived key so it's app-specific
static const uint8 AppSalt[32] = {
    0x54,0x6f,0x6e,0x43,0x6f,0x6e,0x6e,0x65, // "TonConne"
    0x63,0x74,0x53,0x65,0x73,0x73,0x69,0x6f, // "ctSessio"
    0x6e,0x4b,0x65,0x79,0x53,0x61,0x6c,0x74, // "nKeySalt"
    0x56,0x31,0x00,0x00,0x00,0x00,0x00,0x00  // "V1\0...\0"
};

FString FTonSessionStoreFallback::GetStorePath()
{
    return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("TonConnect"), TEXT("session_fb.bin"));
}

TArray<uint8> FTonSessionStoreFallback::DeriveKey()
{
    // Mix device ID + salt, then SHA-256 to get a 32-byte key
    FString DeviceId = FGenericPlatformMisc::GetDeviceId();
    if (DeviceId.IsEmpty()) DeviceId = TEXT("fallback-device");

    FTCHARToUTF8 Conv(*DeviceId);
    TArray<uint8> KeyMaterial;
    KeyMaterial.Append((const uint8*)Conv.Get(), Conv.Length());
    KeyMaterial.Append(AppSalt, 32);

    return FTonSession::SHA256(KeyMaterial);
}

bool FTonSessionStoreFallback::Save(const TArray<uint8>& PublicKey, const TArray<uint8>& PrivateKey)
{
    TArray<uint8> Key = DeriveKey();
    if (Key.Num() != 32) return false;

    // Plaintext: [8B timestamp int64 LE][pubkey 32B][privkey 32B]
    TArray<uint8> Plain;
    int64 Now = FDateTime::UtcNow().ToUnixTimestamp();
    Plain.Append(reinterpret_cast<uint8*>(&Now), 8);
    Plain.Append(PublicKey);
    Plain.Append(PrivateKey);

    // NaCl secretbox: 32-byte zero prefix + plain
    TArray<uint8> PaddedPlain;
    PaddedPlain.SetNumZeroed(crypto_secretbox_ZEROBYTES);
    PaddedPlain.Append(Plain);

    TArray<uint8> Nonce;
    Nonce.SetNum(crypto_secretbox_NONCEBYTES);
    randombytes(Nonce.GetData(), (unsigned long long)Nonce.Num());

    TArray<uint8> CipherBuf;
    CipherBuf.SetNum(PaddedPlain.Num());

    if (crypto_secretbox(CipherBuf.GetData(), PaddedPlain.GetData(),
                         (unsigned long long)PaddedPlain.Num(),
                         Nonce.GetData(), Key.GetData()) != 0)
    {
        return false;
    }

    // File layout: [nonce 24B][ciphertext skip leading 16B]
    TArray<uint8> Output;
    Output.Append(Nonce);
    Output.Append(CipherBuf.GetData() + crypto_secretbox_BOXZEROBYTES,
                  CipherBuf.Num() - crypto_secretbox_BOXZEROBYTES);

    FString Path = GetStorePath();
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
    return FFileHelper::SaveArrayToFile(Output, *Path);
}

bool FTonSessionStoreFallback::Load(TArray<uint8>& OutPublicKey, TArray<uint8>& OutPrivateKey)
{
    // No session yet on first run — skip quietly (LoadFileToArray would log a warning).
    if (!IFileManager::Get().FileExists(*GetStorePath())) return false;

    TArray<uint8> FileData;
    if (!FFileHelper::LoadFileToArray(FileData, *GetStorePath())) return false;

    const int32 NonceLen = crypto_secretbox_NONCEBYTES;
    if (FileData.Num() <= NonceLen) return false;

    TArray<uint8> Key = DeriveKey();
    if (Key.Num() != 32) return false;

    TArray<uint8> Nonce(FileData.GetData(), NonceLen);

    // Restore secretbox wire format: prepend 16 zero bytes
    TArray<uint8> PaddedCipher;
    PaddedCipher.SetNumZeroed(crypto_secretbox_BOXZEROBYTES);
    PaddedCipher.Append(FileData.GetData() + NonceLen, FileData.Num() - NonceLen);

    TArray<uint8> PlainBuf;
    PlainBuf.SetNum(PaddedCipher.Num());

    if (crypto_secretbox_open(PlainBuf.GetData(), PaddedCipher.GetData(),
                               (unsigned long long)PaddedCipher.Num(),
                               Nonce.GetData(), Key.GetData()) != 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("TonConnect: session store decryption failed — key may have changed"));
        return false;
    }

    // Strip 32 zero prefix, expect [8B timestamp][pubkey 32B][privkey 32B]
    const int32 DataOffset = crypto_secretbox_ZEROBYTES;
    if (PlainBuf.Num() - DataOffset < 72) return false;

    int64 Timestamp = *reinterpret_cast<int64*>(PlainBuf.GetData() + DataOffset);
    const int64 ThreeWeeksSec = 21 * 24 * 3600;
    if (FDateTime::UtcNow().ToUnixTimestamp() - Timestamp > ThreeWeeksSec)
    {
        Clear();
        return false;
    }

    OutPublicKey  = TArray<uint8>(PlainBuf.GetData() + DataOffset + 8,      32);
    OutPrivateKey = TArray<uint8>(PlainBuf.GetData() + DataOffset + 8 + 32, 32);
    return true;
}

void FTonSessionStoreFallback::Clear()
{
    IFileManager::Get().Delete(*GetStorePath());
}
