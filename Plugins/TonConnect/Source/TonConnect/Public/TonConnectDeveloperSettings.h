#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "TonConnectDeveloperSettings.generated.h"

UENUM(BlueprintType)
enum class ETonMockResult : uint8
{
    Approve,
    Reject,
    Timeout,
};

UENUM(BlueprintType)
enum class ETonNetwork : uint8
{
    // Mainnet (network ID -239). Uses tonapi.io
    Mainnet     UMETA(DisplayName="Mainnet"),
    // Public testnet (network ID -3). Uses testnet.tonapi.io
    Testnet     UMETA(DisplayName="Testnet"),
    // Custom — supply BridgeUrlOverride and TonApiUrlOverride manually
    Custom      UMETA(DisplayName="Custom"),
};

UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="TON Connect"))
class TONCONNECT_API UTonConnectDeveloperSettings : public UDeveloperSettings
{
    GENERATED_BODY()
public:
    UTonConnectDeveloperSettings();

    virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

    // ---- Network ----

    // Target TON network. Controls bridge URL and TonAPI base URL.
    UPROPERTY(Config, EditAnywhere, Category="Network")
    ETonNetwork Network = ETonNetwork::Mainnet;

    // Override bridge URL (only used when Network = Custom)
    UPROPERTY(Config, EditAnywhere, Category="Network",
              meta=(EditCondition="Network == ETonNetwork::Custom", EditConditionHides))
    FString BridgeUrlOverride = TEXT("https://bridge.tonapi.io/bridge");

    // Override TonAPI base URL (only used when Network = Custom)
    UPROPERTY(Config, EditAnywhere, Category="Network",
              meta=(EditCondition="Network == ETonNetwork::Custom", EditConditionHides))
    FString TonApiUrlOverride = TEXT("https://tonapi.io/v2");

    // ---- Manifest ----

    // TON Connect manifest URL — must be publicly accessible over HTTPS
    UPROPERTY(Config, EditAnywhere, Category="General")
    FString ManifestUrl = TEXT("https://mrcentimetre.github.io/ton-smart-contract-frontend/tonconnect-manifest.json");

    // ---- Connect ----

    // Max seconds to stay in "Connecting" before giving up. If the wallet hasn't
    // approved (or the bridge can't be reached) within this window, the subsystem
    // fires OnError and returns to Disconnected so the UI can offer a retry.
    // 0 disables the timeout (wait forever).
    UPROPERTY(Config, EditAnywhere, Category="General",
              meta=(DisplayName="Connect Timeout (seconds)", ClampMin="0.0", ClampMax="600.0"))
    float ConnectTimeoutSeconds = 180.0f;

    // ---- Mock ----

    // Use mock transport instead of the real SSE bridge.
    // Auto-enabled in Debug/Editor builds; override via -ton.mock CLI flag.
    UPROPERTY(Config, EditAnywhere, Category="Mock", meta=(DisplayName="Use Mock"))
    bool bUseMock = false;

    // How the mock resolves Connect() requests
    UPROPERTY(Config, EditAnywhere, Category="Mock")
    ETonMockResult MockConnectResult = ETonMockResult::Approve;

    // How the mock resolves SendTon() requests
    UPROPERTY(Config, EditAnywhere, Category="Mock")
    ETonMockResult MockSendResult = ETonMockResult::Approve;

    // Seconds before the mock fires its result
    UPROPERTY(Config, EditAnywhere, Category="Mock", meta=(ClampMin="0.0", ClampMax="30.0"))
    float MockDelaySeconds = 1.0f;

    // ---- Helpers ----

    // Returns the active bridge URL based on Network setting
    FString GetBridgeUrl() const;

    // Returns the active TonAPI base URL based on Network setting
    FString GetTonApiUrl() const;

    // Returns the expected network ID string ("-239" mainnet, "-3" testnet, "" custom)
    FString GetNetworkId() const;

    static bool ShouldUseMock();
};
