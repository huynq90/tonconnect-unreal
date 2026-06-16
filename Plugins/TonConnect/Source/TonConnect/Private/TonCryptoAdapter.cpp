// TonCryptoAdapter.cpp
// Compiles tweetnacl.c as part of this translation unit and provides
// the randombytes() implementation required by tweetnacl.

#include "CoreMinimal.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#include "Windows/HideWindowsPlatformTypes.h"

extern "C" void randombytes(unsigned char* buf, unsigned long long len)
{
    // BCryptGenRandom: OS CSPRNG, Win Vista+. Status ignored here for brevity;
    // in production P2 add error handling and fallback.
    BCryptGenRandom(nullptr, buf, (ULONG)len, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
}

#else
// TODO P4: platform-specific implementations for Android/iOS
extern "C" void randombytes(unsigned char* buf, unsigned long long len)
{
    // Temporary fallback — replace with platform CSPRNG before shipping on mobile
    for (unsigned long long i = 0; i < len; ++i)
        buf[i] = (unsigned char)(rand() & 0xFF);
}
#endif

// tweetnacl.c is compiled directly by UBT as its own translation unit.
// randombytes() above satisfies the extern declared in tweetnacl.c at link time.
