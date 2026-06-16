#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "TonTypes.h"
#include "TonConnectWidget.generated.h"

class UTonConnectSubsystem;

// Base class for "Connect Wallet" UMG widgets.
// Subclass in Blueprint, override the BlueprintImplementableEvents to drive your UI.
//
// Usage:
//   1. Create a WBP_ConnectModal subclass of UTonConnectWidget
//   2. Override BP events: OnWalletConnected, OnQRCodeReady, OnWalletError
//   3. Add an Image widget and set its brush from the QR texture in OnQRCodeReady
//   4. Show/hide the widget in response to connection state changes
UCLASS(Abstract, Blueprintable)
class TONCONNECT_API UTonConnectWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    // Cached subsystem — set during NativeConstruct, valid for widget lifetime
    UPROPERTY(BlueprintReadOnly, Category="TonConnect")
    UTonConnectSubsystem* TonConnect = nullptr;

    // Call this from Blueprint to start a new connection attempt
    UFUNCTION(BlueprintCallable, Category="TonConnect")
    void StartConnect();

    // Call this from Blueprint to disconnect and close
    UFUNCTION(BlueprintCallable, Category="TonConnect")
    void StartDisconnect();

protected:
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;

    // Override in Blueprint to react to connection events
    // DeepLink = tc:// universal URL (tap-to-open on mobile); may be empty.
    UFUNCTION(BlueprintImplementableEvent, Category="TonConnect")
    void OnQRCodeReady(UTexture2D* QRTexture, const FString& DeepLink);

    UFUNCTION(BlueprintImplementableEvent, Category="TonConnect")
    void OnWalletConnected(const FTonWalletInfo& WalletInfo);

    UFUNCTION(BlueprintImplementableEvent, Category="TonConnect")
    void OnWalletDisconnected();

    UFUNCTION(BlueprintImplementableEvent, Category="TonConnect")
    void OnWalletError(const FString& ErrorMessage);

private:
    UFUNCTION()
    void HandleQRReady(UTexture2D* Tex, const FString& DeepLink);
    UFUNCTION()
    void HandleConnected(const FTonWalletInfo& Info);
    UFUNCTION()
    void HandleDisconnected();
    UFUNCTION()
    void HandleError(const FString& Msg);
};
