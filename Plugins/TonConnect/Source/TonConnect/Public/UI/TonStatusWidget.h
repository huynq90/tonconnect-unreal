#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "TonTypes.h"
#include "TonStatusWidget.generated.h"

class UTonConnectSubsystem;

// Lightweight status bar widget — shows current wallet state and truncated address.
// Subclass in Blueprint: bind OnStateChanged / OnAddressChanged to text blocks.
//
// Usage:
//   1. Create WBP_TonStatus as a subclass of UTonStatusWidget
//   2. Add two Text widgets: one for state label, one for address
//   3. In NativeTick (BP) or via OnStateChanged event: set the text from CurrentState/CurrentAddress
UCLASS(Abstract, Blueprintable)
class TONCONNECT_API UTonStatusWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    UPROPERTY(BlueprintReadOnly, Category="TonConnect")
    UTonConnectSubsystem* TonConnect = nullptr;

    // Polled each tick and updated on events — safe to read in BP NativeTick
    UPROPERTY(BlueprintReadOnly, Category="TonConnect")
    ETonConnectState CurrentState = ETonConnectState::Disconnected;

    // Truncated wallet address for display (e.g. "EQABCd…abcd"), empty when disconnected
    UPROPERTY(BlueprintReadOnly, Category="TonConnect")
    FString DisplayAddress;

    // Full wallet address — use for copy-to-clipboard
    UPROPERTY(BlueprintReadOnly, Category="TonConnect")
    FString FullAddress;

    // How many prefix/suffix chars to keep in DisplayAddress
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="TonConnect")
    int32 AddressPrefixLen = 6;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="TonConnect")
    int32 AddressSuffixLen = 4;

protected:
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;

    // Override in Blueprint to react to state or address changes
    UFUNCTION(BlueprintImplementableEvent, Category="TonConnect")
    void OnStateChanged(ETonConnectState NewState);

    UFUNCTION(BlueprintImplementableEvent, Category="TonConnect")
    void OnAddressChanged(const FString& NewDisplayAddress, const FString& NewFullAddress);

private:
    UFUNCTION() void HandleConnected(const FTonWalletInfo& Info);
    UFUNCTION() void HandleDisconnected();

    void RefreshState(ETonConnectState State, const FString& Address = TEXT(""));
};
