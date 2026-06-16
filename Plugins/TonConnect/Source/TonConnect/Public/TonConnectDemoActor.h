#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TonTypes.h"
#include "TonConnectDemoActor.generated.h"

class UTonConnectSubsystem;
class UUserWidget;

// Drop this actor into any level to get a fully interactive TonConnect demo.
//
// Default behaviour (mock mode, bAutoConnect = true):
//   BeginPlay → Connect() → OnConnected fires → optionally auto-sends TON
//   State and wallet address are drawn on screen every tick.
//
// Keyboard shortcuts (requires focus on the game window):
//   1  — Connect
//   2  — Send TON  (uses AutoSendAddress / AutoSendAmount)
//   3  — Disconnect
//   4  — Send 0.01 TON to self (connected wallet → itself)
//   5  — Read on-chain data via a get-method (no gas, no signing)
//
// Enable mock in Project Settings → TonConnect → bUseMock = true
// or launch with  -ton.mock
UCLASS(Blueprintable, BlueprintType)
class TONCONNECT_API ATonConnectDemoActor : public AActor
{
    GENERATED_BODY()

public:
    ATonConnectDemoActor();

    // ── Config ────────────────────────────────────────────────────────────────

    // Connect automatically on BeginPlay
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="TonConnect|Demo")
    bool bAutoConnect = true;

    // After connecting, automatically send a test transaction
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="TonConnect|Demo")
    bool bAutoSendAfterConnect = false;

    // Delay (seconds) between OnConnected and the auto-send
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="TonConnect|Demo")
    float AutoSendDelaySeconds = 2.f;

    // Destination address for key '2' / auto-send.
    // Defaults to a sample testnet address so key '2' (send to others) is
    // visibly different from key '4' (send to self). Leave empty to send to self.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="TonConnect|Demo")
    FString AutoSendAddress = TEXT("0:3333333333333333333333333333333333333333333333333333333333333333");

    // Amount in nanoTON (default = 0.01 TON)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="TonConnect|Demo")
    FString AutoSendAmountNano = TEXT("10000000");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="TonConnect|Demo")
    FString AutoSendComment = TEXT("TonConnect demo");

    // ── Read on-chain (get-method) demo — key 5 ────────────────────────────────
    // Contract to query. Leave empty to read the connected wallet's `seqno`.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="TonConnect|Demo|GetMethod")
    FString GetMethodAddress;

    // Get-method name (read-only). With an empty address this reads `seqno`.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="TonConnect|Demo|GetMethod")
    FString GetMethodName = TEXT("seqno");

    // Optional stack inputs (decimal numbers or addresses). Empty for no-arg methods.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="TonConnect|Demo|GetMethod")
    TArray<FString> GetMethodArgs;

    // ── Blueprint callable ────────────────────────────────────────────────────

    UFUNCTION(BlueprintCallable, Category="TonConnect|Demo")
    void DemoConnect();

    UFUNCTION(BlueprintCallable, Category="TonConnect|Demo")
    void DemoSendTon();

    // Send 0.01 TON back to the connected wallet itself (key 4)
    UFUNCTION(BlueprintCallable, Category="TonConnect|Demo")
    void DemoSendToSelf();

    UFUNCTION(BlueprintCallable, Category="TonConnect|Demo")
    void DemoDisconnect();

    // Read on-chain data via a get-method (no gas, no signing, no wallet needed). Key 5.
    UFUNCTION(BlueprintCallable, Category="TonConnect|Demo")
    void DemoReadOnChain();

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void Tick(float DeltaTime) override;

private:
    // ── Event handlers ────────────────────────────────────────────────────────
    UFUNCTION() void HandleQRReady(UTexture2D* Tex, const FString& DeepLink);
    UFUNCTION() void HandleConnected(const FTonWalletInfo& Info);
    UFUNCTION() void HandleDisconnected();
    UFUNCTION() void HandleError(const FString& Msg);
    UFUNCTION() void HandleSendResult(const FTonSendResult& Result);
    UFUNCTION() void HandleAccountInfoUpdated(const FString& Version, const FString& BalanceNano);
    UFUNCTION() void HandleAssetsUpdated(const FString& JettonInfo, int32 NftCount);
    UFUNCTION() void HandleGetMethodResult(const FTonGetMethodResult& Result);

    // ── Internal ──────────────────────────────────────────────────────────────
    UPROPERTY() UTonConnectSubsystem* TonConnect = nullptr;

    // This minimal demo renders its own QR (the plugin no longer ships a built-in
    // overlay) — created on OnQRReady, removed once connecting ends.
    UPROPERTY() UUserWidget* QRWidget = nullptr;
    void HideQR();

    FTimerHandle AutoSendTimer;

    void Log(const FString& Msg, FColor Color, float Duration = 5.f);
};
