#include "Tests/TonConnectGauntletController.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "TonConnectSubsystem.h"

UTonConnectSubsystem* UTonConnectGauntletController::FindSubsystem() const
{
    if (!GEngine) return nullptr;
    for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
    {
        UWorld* W = Ctx.World();
        if (!W) continue;
        UGameInstance* GI = W->GetGameInstance();
        if (!GI) continue;
        UTonConnectSubsystem* Sub = GI->GetSubsystem<UTonConnectSubsystem>();
        if (Sub) return Sub;
    }
    return nullptr;
}

void UTonConnectGauntletController::Fail(const FString& Reason)
{
    UE_LOG(LogTemp, Error, TEXT("[TonGauntlet] FAIL: %s"), *Reason);
    EndTest(1);
}

void UTonConnectGauntletController::OnInit()
{
    UE_LOG(LogTemp, Display, TEXT("[TonGauntlet] Initialized — waiting for world"));
}

void UTonConnectGauntletController::HandleConnected(const FTonWalletInfo& Info)
{
    bConnectFired = true;
    UE_LOG(LogTemp, Display, TEXT("[TonGauntlet] OnConnected: %s"), *Info.Address);
}

void UTonConnectGauntletController::HandleSendResult(const FTonSendResult& Result)
{
    LastSendResult = Result;
    bSendFired     = true;
}

void UTonConnectGauntletController::OnTick(float DeltaTime)
{
    StepTimer += DeltaTime;

    switch (Step)
    {
    case EStep::WaitForWorld:
    {
        UTonConnectSubsystem* Sub = FindSubsystem();
        if (!Sub) return;
        Subsystem = Sub;
        Sub->OnConnected.AddDynamic(this, &UTonConnectGauntletController::HandleConnected);
        UE_LOG(LogTemp, Display, TEXT("[TonGauntlet] World ready — calling Connect()"));
        Sub->Connect();
        Step = EStep::Connecting;
        StepTimer = 0.f;
        break;
    }

    case EStep::Connecting:
    {
        if (!Subsystem.IsValid()) { Fail(TEXT("Subsystem lost")); return; }
        if (Subsystem->GetState() == ETonConnectState::Connecting)
        {
            Step = EStep::WaitConnect;
            StepTimer = 0.f;
        }
        else if (StepTimer >= 2.f)
        {
            Fail(TEXT("Connect() did not transition to Connecting"));
        }
        break;
    }

    case EStep::WaitConnect:
    {
        if (!Subsystem.IsValid()) { Fail(TEXT("Subsystem lost")); return; }
        if (bConnectFired && Subsystem->GetState() == ETonConnectState::Connected)
        {
            const FTonWalletInfo& Info = Subsystem->GetConnectedWallet();
            if (Info.Address.IsEmpty())
            {
                Fail(TEXT("Connected but wallet address is empty"));
                return;
            }
            UE_LOG(LogTemp, Display, TEXT("[TonGauntlet] Connected OK — sending TON"));
            Step = EStep::SendingTon;
            StepTimer = 0.f;
            return;
        }
        if (StepTimer >= StepTimeout) Fail(TEXT("Timeout waiting for Connected"));
        break;
    }

    case EStep::SendingTon:
    {
        if (!Subsystem.IsValid()) { Fail(TEXT("Subsystem lost")); return; }
        bSendFired = false;

        // Listen on the global multicast for result (simpler than per-call binding in Gauntlet)
        Subsystem->OnSendResult.AddDynamic(this, &UTonConnectGauntletController::HandleSendResult);

        FOnTonSendResultDelegate Cb; // unbound — result captured via OnSendResult multicast
        Subsystem->SendTon(
            TEXT("0:3333333333333333333333333333333333333333333333333333333333333333"),
            TEXT("50000000"), TEXT("gauntlet"), Cb);

        Step = EStep::WaitSend;
        StepTimer = 0.f;
        break;
    }

    case EStep::WaitSend:
    {
        if (bSendFired)
        {
            if (LastSendResult.Result != ETonSendResult::Approved)
            {
                Fail(FString::Printf(TEXT("Send not approved: %s"), *LastSendResult.ErrorMessage));
                return;
            }
            UE_LOG(LogTemp, Display, TEXT("[TonGauntlet] Send approved — all steps PASSED"));
            if (Subsystem.IsValid())
            {
                Subsystem->OnSendResult.RemoveDynamic(this, &UTonConnectGauntletController::HandleSendResult);
                Subsystem->Disconnect();
            }
            Step = EStep::Done;
            EndTest(0);
            return;
        }
        if (StepTimer >= StepTimeout) Fail(TEXT("Timeout waiting for send result"));
        break;
    }

    case EStep::Done:
        break;
    }
}
