// __OBJC__ is defined only by the Apple Objective-C++ compiler.
// On Win64 this file compiles to no-op stubs (the factory never instantiates
// FTonSessionStoreKeychain on non-iOS, but the linker still needs symbols).
#if defined(__OBJC__)

#include "Store/TonSessionStoreKeychain.h"
#include "Misc/DateTime.h"

#import <Security/Security.h>
#import <Foundation/Foundation.h>

const char* FTonSessionStoreKeychain::ServiceName = "com.tonconnect.session";
const char* FTonSessionStoreKeychain::AccountName = "keypair";

static const int64 TTL_SECONDS = 21 * 24 * 3600;

static NSDictionary* MakeQuery()
{
    return @{
        (__bridge id)kSecClass:       (__bridge id)kSecClassGenericPassword,
        (__bridge id)kSecAttrService: @(FTonSessionStoreKeychain::ServiceName),
        (__bridge id)kSecAttrAccount: @(FTonSessionStoreKeychain::AccountName),
    };
}

bool FTonSessionStoreKeychain::Save(const TArray<uint8>& PublicKey, const TArray<uint8>& PrivateKey)
{
    // Payload: [8B timestamp int64 LE][pubkey 32B][privkey 32B]
    NSMutableData* Data = [NSMutableData dataWithCapacity:72];
    int64 Now = FDateTime::UtcNow().ToUnixTimestamp();
    [Data appendBytes:&Now length:8];
    [Data appendBytes:PublicKey.GetData() length:PublicKey.Num()];
    [Data appendBytes:PrivateKey.GetData() length:PrivateKey.Num()];

    SecItemDelete((__bridge CFDictionaryRef)MakeQuery());

    NSDictionary* Attrs = @{
        (__bridge id)kSecClass:          (__bridge id)kSecClassGenericPassword,
        (__bridge id)kSecAttrService:    @(ServiceName),
        (__bridge id)kSecAttrAccount:    @(AccountName),
        (__bridge id)kSecAttrAccessible: (__bridge id)kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly,
        (__bridge id)kSecValueData:      Data,
    };

    return SecItemAdd((__bridge CFDictionaryRef)Attrs, nullptr) == errSecSuccess;
}

bool FTonSessionStoreKeychain::Load(TArray<uint8>& OutPublicKey, TArray<uint8>& OutPrivateKey)
{
    NSMutableDictionary* Query = [NSMutableDictionary dictionaryWithDictionary:MakeQuery()];
    Query[(__bridge id)kSecReturnData] = @YES;
    Query[(__bridge id)kSecMatchLimit] = (__bridge id)kSecMatchLimitOne;

    CFTypeRef Result = nullptr;
    if (SecItemCopyMatching((__bridge CFDictionaryRef)Query, &Result) != errSecSuccess || !Result)
        return false;

    NSData* Data = (__bridge_transfer NSData*)Result;
    if ((int32)Data.length < 72) return false;

    const uint8* Bytes = (const uint8*)Data.bytes;
    int64 Timestamp = *reinterpret_cast<const int64*>(Bytes);
    if (FDateTime::UtcNow().ToUnixTimestamp() - Timestamp > TTL_SECONDS)
    {
        Clear();
        return false;
    }

    OutPublicKey  = TArray<uint8>(Bytes + 8,      32);
    OutPrivateKey = TArray<uint8>(Bytes + 8 + 32, 32);
    return true;
}

void FTonSessionStoreKeychain::Clear()
{
    SecItemDelete((__bridge CFDictionaryRef)MakeQuery());
}

#else

// Non-Apple stub — FTonSessionStoreKeychain is never instantiated on Win64/Android
// (ISessionStore::Create uses PLATFORM_IOS guard), but symbols must exist.
#include "Store/TonSessionStoreKeychain.h"

const char* FTonSessionStoreKeychain::ServiceName = "com.tonconnect.session";
const char* FTonSessionStoreKeychain::AccountName = "keypair";

bool FTonSessionStoreKeychain::Save(const TArray<uint8>&, const TArray<uint8>&) { return false; }
bool FTonSessionStoreKeychain::Load(TArray<uint8>&, TArray<uint8>&) { return false; }
void FTonSessionStoreKeychain::Clear() {}

#endif
