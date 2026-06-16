#include "ISessionStore.h"
#include "Store/TonSessionStoreDpapi.h"
#include "Store/TonSessionStoreFallback.h"
#if PLATFORM_IOS
#include "Store/TonSessionStoreKeychain.h"
#endif

TSharedPtr<ISessionStore> ISessionStore::Create()
{
#if PLATFORM_WINDOWS
    return MakeShared<FTonSessionStoreDpapi>();
#elif PLATFORM_IOS
    return MakeShared<FTonSessionStoreKeychain>();
#else
    // Android and other platforms: encrypted file store.
    // TODO P4+: Replace with Android Keystore (JNI) for hardware-backed key protection.
    return MakeShared<FTonSessionStoreFallback>();
#endif
}
