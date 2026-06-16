#include "TonConnectDeveloperSettings.h"
#include "Misc/CommandLine.h"

UTonConnectDeveloperSettings::UTonConnectDeveloperSettings()
{
#if UE_BUILD_DEBUG || WITH_EDITOR
    bUseMock = true;
#endif
}

// Reads -ton.network=mainnet|testnet|custom from command line.
// Returns ETonNetwork::Mainnet/Testnet/Custom, or TFallback if flag absent.
static ETonNetwork ParseNetworkFromCLI(ETonNetwork Fallback)
{
    FString Value;
    if (!FParse::Value(FCommandLine::Get(), TEXT("ton.network="), Value)) return Fallback;
    if (Value.Equals(TEXT("mainnet"), ESearchCase::IgnoreCase)) return ETonNetwork::Mainnet;
    if (Value.Equals(TEXT("testnet"), ESearchCase::IgnoreCase)) return ETonNetwork::Testnet;
    if (Value.Equals(TEXT("custom"),  ESearchCase::IgnoreCase)) return ETonNetwork::Custom;
    UE_LOG(LogTemp, Warning, TEXT("TonConnect: unknown -ton.network value '%s', ignoring"), *Value);
    return Fallback;
}

static ETonNetwork GetEffectiveNetwork()
{
    const UTonConnectDeveloperSettings* S = GetDefault<UTonConnectDeveloperSettings>();
    ETonNetwork Base = S ? S->Network : ETonNetwork::Testnet;
    return ParseNetworkFromCLI(Base); // CLI wins over ini
}

FString UTonConnectDeveloperSettings::GetBridgeUrl() const
{
    // bridge.tonapi.io is network-agnostic; wallet handles chain selection internally
    ETonNetwork Effective = GetEffectiveNetwork();
    if (Effective == ETonNetwork::Custom)
    {
        return BridgeUrlOverride.IsEmpty() ? TEXT("https://bridge.tonapi.io/bridge") : BridgeUrlOverride;
    }
    return TEXT("https://bridge.tonapi.io/bridge");
}

FString UTonConnectDeveloperSettings::GetTonApiUrl() const
{
    switch (GetEffectiveNetwork())
    {
    case ETonNetwork::Mainnet: return TEXT("https://tonapi.io/v2");
    case ETonNetwork::Testnet: return TEXT("https://testnet.tonapi.io/v2");
    case ETonNetwork::Custom:
        return TonApiUrlOverride.IsEmpty() ? TEXT("https://tonapi.io/v2") : TonApiUrlOverride;
    }
    return TEXT("https://tonapi.io/v2");
}

FString UTonConnectDeveloperSettings::GetNetworkId() const
{
    switch (GetEffectiveNetwork())
    {
    case ETonNetwork::Mainnet: return TEXT("-239");
    case ETonNetwork::Testnet: return TEXT("-3");
    case ETonNetwork::Custom:  return TEXT(""); // accept any network
    }
    return TEXT("");
}

bool UTonConnectDeveloperSettings::ShouldUseMock()
{
    if (FParse::Param(FCommandLine::Get(), TEXT("ton.mock"))) return true;
    const UTonConnectDeveloperSettings* S = GetDefault<UTonConnectDeveloperSettings>();
    return S && S->bUseMock;
}
