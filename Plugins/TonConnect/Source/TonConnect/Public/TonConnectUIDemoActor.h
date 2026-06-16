#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TonTypes.h"
#include "TonConnectUIDemoActor.generated.h"

class UTonConnectSubsystem;
class STonConnectPanel;
class SVerticalBox;
class SWidget;

// Drop-in actor that spawns a full TON Connect wallet UI — no Blueprint required.
//
// Press [T] to toggle a pause-menu-style popup that walks through the whole flow:
//   Connect → scan QR → (auto-closes to) connected view → Send TON (with loading) → Disconnect.
// Connection/send results surface as on-screen toasts.
//
// Enable mock mode (Project Settings → TonConnect → Use Mock, or -ton.mock) to test
// the entire flow without a real wallet.
UCLASS(Blueprintable, BlueprintType)
class TONCONNECT_API ATonConnectUIDemoActor : public AActor
{
    GENERATED_BODY()

public:
    ATonConnectUIDemoActor();

    // Show the wallet popup on BeginPlay (otherwise press T)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="TonConnect|UI")
    bool bShowOnStart = false;

    // Toggle the popup open/closed
    UFUNCTION(BlueprintCallable, Category="TonConnect|UI")
    void TogglePanel();

    UFUNCTION(BlueprintCallable, Category="TonConnect|UI")
    void ShowPanel();

    UFUNCTION(BlueprintCallable, Category="TonConnect|UI")
    void HidePanel();

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void Tick(float DeltaSeconds) override;

private:
    // ── Subsystem event handlers (dynamic delegate targets) ─────────────────────
    UFUNCTION() void HandleQRReady(UTexture2D* Tex, const FString& DeepLink);
    UFUNCTION() void HandleConnected(const FTonWalletInfo& Info);
    UFUNCTION() void HandleDisconnected();
    UFUNCTION() void HandleError(const FString& Msg);
    UFUNCTION() void HandleSendResult(const FTonSendResult& Result);
    UFUNCTION() void HandleAccountInfoUpdated(const FString& Version, const FString& BalanceNano);
    UFUNCTION() void HandleFeeEstimate(const FTonFeeEstimate& Estimate);

    // Debounced fee re-estimate when the Send form changes
    void RequestFeeEstimate();

    // ── Panel action callbacks ─────────────────────────────────────────────────
    void OnConnectClicked();
    void OnRefreshClicked();   // regenerate QR: disconnect then reconnect
    void OnDisconnectClicked();
    void OnSendRequested(FString ToAddress, FString AmountTon, FString Comment);

    // ── Wallet picker ─────────────────────────────────────────────────────────
    void RefreshWalletList();
    void LoadWalletIcon(const FString& Url, TSharedPtr<FSlateBrush> Brush);

    // ── Toasts ──────────────────────────────────────────────────────────────────
    void PushToast(const FString& Message, const FLinearColor& Color, float Duration = 3.5f);

    void ApplyInputMode(bool bUIOpen);

    UPROPERTY() UTonConnectSubsystem* TonConnect = nullptr;
    UPROPERTY() UTexture2D*           QRTexture  = nullptr; // GC guard
    UPROPERTY() TArray<UTexture2D*>   IconTextures;         // wallet icons, GC keep-alive
    bool bWalletsRequested = false;

    TSharedPtr<STonConnectPanel> Panel;
    TSharedPtr<SVerticalBox>     ToastBox;
    TSharedPtr<SWidget>          ToastLayer; // viewport wrapper around ToastBox
    bool bPanelVisible = false;
};
