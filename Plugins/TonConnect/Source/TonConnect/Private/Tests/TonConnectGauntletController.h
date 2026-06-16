#pragma once

#include "CoreMinimal.h"
#include "GauntletTestController.h"
#include "TonTypes.h"
#include "TonConnectGauntletController.generated.h"

class UTonConnectSubsystem;

// Gauntlet session controller for end-to-end TonConnect mock flow tests.
//
// Launch with:
//   <Game>.exe <Map> -gauntlet -TonGauntlet -ton.mock -ton.mock.approve_delay=0.5 -nullrhi
//
// The controller steps through: Connect → wait → SendTon → wait → Disconnect → pass.
UCLASS()
class TONCONNECT_API UTonConnectGauntletController : public UGauntletTestController
{
    GENERATED_BODY()

public:
    virtual void OnInit() override;
    virtual void OnTick(float DeltaTime) override;

private:
    enum class EStep : uint8
    {
        WaitForWorld,
        Connecting,
        WaitConnect,
        SendingTon,
        WaitSend,
        Done,
    };

    EStep Step      = EStep::WaitForWorld;
    float StepTimer = 0.f;

    static constexpr float StepTimeout = 10.f;

    TWeakObjectPtr<UTonConnectSubsystem> Subsystem;

    // Captured send result — set from per-call lambda
    bool bSendFired = false;
    FTonSendResult LastSendResult;

    // UFUNCTION event handlers — required for AddDynamic
    UFUNCTION()
    void HandleConnected(const FTonWalletInfo& Info);

    UFUNCTION()
    void HandleSendResult(const FTonSendResult& Result);

    bool bConnectFired = false;

    UTonConnectSubsystem* FindSubsystem() const;
    void Fail(const FString& Reason);
};
