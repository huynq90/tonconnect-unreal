#include "TonConnectDemoActor.h"
#include "TonConnectSubsystem.h"
#include "TonBlueprintLibrary.h"
#include "TonQRWidget.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Components/InputComponent.h"
#include "Blueprint/UserWidget.h"

ATonConnectDemoActor::ATonConnectDemoActor()
{
    PrimaryActorTick.bCanEverTick = true;
    // Receive input without needing to call EnableInput explicitly
    AutoReceiveInput = EAutoReceiveInput::Player0;
}

// ─── BeginPlay ────────────────────────────────────────────────────────────────

void ATonConnectDemoActor::BeginPlay()
{
    Super::BeginPlay();

    UGameInstance* GI = GetGameInstance();
    if (!GI) { Log(TEXT("Demo: no GameInstance — actor must be in a playable level"), FColor::Red); return; }

    TonConnect = GI->GetSubsystem<UTonConnectSubsystem>();
    if (!TonConnect) { Log(TEXT("Demo: TonConnectSubsystem not found — is the plugin enabled?"), FColor::Red); return; }

    TonConnect->OnQRReady.AddDynamic(this,           &ATonConnectDemoActor::HandleQRReady);
    TonConnect->OnConnected.AddDynamic(this,         &ATonConnectDemoActor::HandleConnected);
    TonConnect->OnDisconnected.AddDynamic(this,      &ATonConnectDemoActor::HandleDisconnected);
    TonConnect->OnError.AddDynamic(this,             &ATonConnectDemoActor::HandleError);
    TonConnect->OnSendResult.AddDynamic(this,        &ATonConnectDemoActor::HandleSendResult);
    TonConnect->OnAccountInfoUpdated.AddDynamic(this, &ATonConnectDemoActor::HandleAccountInfoUpdated);
    TonConnect->OnAssetsUpdated.AddDynamic(this,      &ATonConnectDemoActor::HandleAssetsUpdated);

    Log(TEXT("─────────────────────────────"), FColor::White, 60.f);
    Log(TEXT("TonConnect Demo Actor ready"),   FColor::Cyan,  60.f);
    Log(TEXT("Keys:  1=Connect  2=Send  3=Disconnect  4=SendToSelf  5=ReadOnChain"), FColor::White, 60.f);
    Log(TEXT("─────────────────────────────"), FColor::White, 60.f);

    // AutoReceiveInput=Player0 (set in constructor) calls EnableInput in Super::BeginPlay,
    // so InputComponent is valid here — bind keys without taking Pawn possession.
    if (InputComponent)
    {
        InputComponent->BindKey(EKeys::One,   IE_Pressed, this, &ATonConnectDemoActor::DemoConnect);
        InputComponent->BindKey(EKeys::Two,   IE_Pressed, this, &ATonConnectDemoActor::DemoSendTon);
        InputComponent->BindKey(EKeys::Three, IE_Pressed, this, &ATonConnectDemoActor::DemoDisconnect);
        InputComponent->BindKey(EKeys::Four,  IE_Pressed, this, &ATonConnectDemoActor::DemoSendToSelf);
        InputComponent->BindKey(EKeys::Five,  IE_Pressed, this, &ATonConnectDemoActor::DemoReadOnChain);
    }

    // Already connected from a restored session — show info without re-pairing
    if (TonConnect->GetState() == ETonConnectState::Connected)
    {
        HandleConnected(TonConnect->GetConnectedWallet());
        return;
    }

    if (bAutoConnect)
        DemoConnect();
}

void ATonConnectDemoActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (TonConnect)
    {
        TonConnect->OnQRReady.RemoveDynamic(this,           &ATonConnectDemoActor::HandleQRReady);
        TonConnect->OnConnected.RemoveDynamic(this,         &ATonConnectDemoActor::HandleConnected);
        TonConnect->OnDisconnected.RemoveDynamic(this,      &ATonConnectDemoActor::HandleDisconnected);
        TonConnect->OnError.RemoveDynamic(this,             &ATonConnectDemoActor::HandleError);
        TonConnect->OnSendResult.RemoveDynamic(this,        &ATonConnectDemoActor::HandleSendResult);
        TonConnect->OnAccountInfoUpdated.RemoveDynamic(this, &ATonConnectDemoActor::HandleAccountInfoUpdated);
        TonConnect->OnAssetsUpdated.RemoveDynamic(this,      &ATonConnectDemoActor::HandleAssetsUpdated);
    }
    HideQR();
    GetWorldTimerManager().ClearTimer(AutoSendTimer);
    Super::EndPlay(EndPlayReason);
}

// ─── Tick — on-screen state display ──────────────────────────────────────────

void ATonConnectDemoActor::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    if (!TonConnect) return;

    const ETonConnectState State = TonConnect->GetState();

    FString StateLine;
    FColor  StateColor = FColor::White;

    switch (State)
    {
    case ETonConnectState::Disconnected:
        StateLine  = TEXT("State: Disconnected");
        StateColor = FColor::Silver;
        break;
    case ETonConnectState::Connecting:
        StateLine  = TEXT("State: Connecting…");
        StateColor = FColor::Yellow;
        break;
    case ETonConnectState::Connected:
        {
            const FTonWalletInfo& W = TonConnect->GetConnectedWallet();
            const FString Balance = UTonBlueprintLibrary::FormatTon(TonConnect->GetCachedBalance(), 4);
            const FString Version = W.WalletVersion.IsEmpty() ? TEXT("?") : W.WalletVersion;
            StateLine = FString::Printf(TEXT("Connected  %s  [%s/%s]  %s"),
                *UTonBlueprintLibrary::TruncateAddress(W.Address, 8, 6),
                *Version, *W.Network, *Balance);
            StateColor = FColor::Green;
        }
        break;
    case ETonConnectState::Disconnecting:
        StateLine  = TEXT("State: Disconnecting…");
        StateColor = FColor::Orange;
        break;
    }

    // Key 0 = always-on display line (clears each frame)
    if (GEngine)
        GEngine->AddOnScreenDebugMessage(0, 0.f, StateColor, StateLine);
}

// ─── Public actions ───────────────────────────────────────────────────────────

void ATonConnectDemoActor::DemoConnect()
{
    if (!TonConnect) return;
    if (TonConnect->GetState() != ETonConnectState::Disconnected)
    {
        Log(TEXT("Demo: already connecting or connected"), FColor::Yellow);
        return;
    }
    Log(TEXT("Demo: calling Connect()…"), FColor::Cyan);
    TonConnect->Connect();
}

void ATonConnectDemoActor::DemoSendTon()
{
    if (!TonConnect) return;
    if (TonConnect->GetState() != ETonConnectState::Connected)
    {
        Log(TEXT("Demo: not connected — connect first (key 1)"), FColor::Yellow);
        return;
    }

    // Fall back to self-send when no address is configured
    const FString Dest = AutoSendAddress.IsEmpty()
        ? TonConnect->GetConnectedWallet().Address
        : AutoSendAddress;

    const FString DisplayAmt = UTonBlueprintLibrary::FormatTon(AutoSendAmountNano, 4);
    Log(FString::Printf(TEXT("Demo: sending %s to %s…"), *DisplayAmt,
        *UTonBlueprintLibrary::TruncateAddress(Dest, 6, 4)), FColor::Cyan);

    FOnTonSendResultDelegate Cb; // result captured via OnSendResult multicast
    TonConnect->SendTon(Dest, AutoSendAmountNano, AutoSendComment, Cb);
}

void ATonConnectDemoActor::DemoSendToSelf()
{
    if (!TonConnect) return;
    if (TonConnect->GetState() != ETonConnectState::Connected)
    {
        Log(TEXT("Demo: not connected — connect first (key 1)"), FColor::Yellow);
        return;
    }

    const FString SelfAddress = TonConnect->GetConnectedWallet().Address;
    const FString AmountNano  = TEXT("10000000"); // 0.01 TON

    Log(FString::Printf(TEXT("Demo: sending 0.01 TON to self (%s)…"),
        *UTonBlueprintLibrary::TruncateAddress(SelfAddress, 6, 4)), FColor::Cyan);

    FOnTonSendResultDelegate Cb;
    TonConnect->SendTon(SelfAddress, AmountNano, TEXT("self-send test"), Cb);
}

void ATonConnectDemoActor::DemoDisconnect()
{
    if (!TonConnect) return;
    if (TonConnect->GetState() == ETonConnectState::Disconnected)
    {
        Log(TEXT("Demo: already disconnected"), FColor::Yellow);
        return;
    }
    Log(TEXT("Demo: calling Disconnect()…"), FColor::Cyan);
    TonConnect->Disconnect();
}

void ATonConnectDemoActor::DemoReadOnChain()
{
    if (!TonConnect) return;

    FString Addr   = GetMethodAddress;
    FString Method = GetMethodName;

    // No contract configured → read the connected wallet's seqno (a simple read-only call).
    if (Addr.IsEmpty())
    {
        if (TonConnect->GetState() != ETonConnectState::Connected)
        {
            Log(TEXT("Demo: set GetMethodAddress, or connect first (key 1) to read wallet seqno"), FColor::Yellow);
            return;
        }
        Addr = TonConnect->GetConnectedWallet().Address;
        if (Method.IsEmpty()) Method = TEXT("seqno");
    }

    Log(FString::Printf(TEXT("Demo: get-method '%s' on %s…"), *Method,
        *UTonBlueprintLibrary::TruncateAddress(Addr, 6, 4)), FColor::Cyan);

    FOnTonGetMethodDelegate Cb;
    Cb.BindDynamic(this, &ATonConnectDemoActor::HandleGetMethodResult);
    TonConnect->CallGetMethod(Addr, Method, GetMethodArgs, Cb);
}

// ─── Event handlers ───────────────────────────────────────────────────────────

void ATonConnectDemoActor::HandleQRReady(UTexture2D* Tex, const FString& DeepLink)
{
    Log(TEXT("● QR Ready — open Tonkeeper and scan the QR code"), FColor::Yellow);
    Log(TEXT("  (In mock mode the wallet connects automatically)"), FColor::Silver);
    if (!DeepLink.IsEmpty())
        Log(FString::Printf(TEXT("  Deep link: %s"), *DeepLink), FColor::Cyan);

    // Render the QR ourselves (centered overlay) and tear it down once connecting ends.
    HideQR();
    APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
    if (Tex && PC)
    {
        UTonQRWidget* W = CreateWidget<UTonQRWidget>(PC, UTonQRWidget::StaticClass());
        if (W)
        {
            W->SetQRTexture(Tex);
            W->AddToViewport(100);
            QRWidget = W;
        }
    }
}

void ATonConnectDemoActor::HideQR()
{
    if (QRWidget)
    {
        QRWidget->RemoveFromParent();
        QRWidget = nullptr;
    }
}

void ATonConnectDemoActor::HandleConnected(const FTonWalletInfo& Info)
{
    HideQR();
    const FString Addr    = UTonBlueprintLibrary::TruncateAddress(Info.Address, 8, 6);
    const FString Version = Info.WalletVersion.IsEmpty() ? TEXT("?") : Info.WalletVersion;
    Log(FString::Printf(TEXT("✔ Connected: %s  [%s/%s]"), *Addr, *Version, *Info.Network), FColor::Green);

    if (bAutoSendAfterConnect)
    {
        GetWorldTimerManager().SetTimer(AutoSendTimer, this,
            &ATonConnectDemoActor::DemoSendTon, AutoSendDelaySeconds, false);
    }
}

void ATonConnectDemoActor::HandleDisconnected()
{
    HideQR();
    Log(TEXT("✘ Disconnected"), FColor::Orange);
}

void ATonConnectDemoActor::HandleError(const FString& Msg)
{
    HideQR();
    Log(FString::Printf(TEXT("⚠ Error: %s"), *Msg), FColor::Red);
}

void ATonConnectDemoActor::HandleAccountInfoUpdated(const FString& Version, const FString& BalanceNano)
{
    const FString Balance = UTonBlueprintLibrary::FormatTon(BalanceNano, 4);
    Log(FString::Printf(TEXT("● Wallet: %s  |  %s"), *Version, *Balance), FColor::Cyan);
}

void ATonConnectDemoActor::HandleAssetsUpdated(const FString& JettonInfo, int32 NftCount)
{
    if (!JettonInfo.IsEmpty())
        Log(FString::Printf(TEXT("  Jettons: %s"), *JettonInfo), FColor::Silver);
    if (NftCount > 0)
        Log(FString::Printf(TEXT("  NFTs: %d item(s)"), NftCount), FColor::Silver);
    if (JettonInfo.IsEmpty() && NftCount == 0)
        Log(TEXT("  Assets: none"), FColor::Silver);
}

void ATonConnectDemoActor::HandleGetMethodResult(const FTonGetMethodResult& Result)
{
    if (!Result.bSuccess)
    {
        Log(FString::Printf(TEXT("⚠ get-method failed: %s"), *Result.ErrorMessage), FColor::Red);
        return;
    }
    Log(FString::Printf(TEXT("✔ get-method returned %d value(s):"), Result.Stack.Num()), FColor::Green);
    for (const TPair<FString,FString>& Kv : Result.Stack)
        Log(FString::Printf(TEXT("   [%s] = %s"), *Kv.Key, *Kv.Value), FColor::Silver);
}

void ATonConnectDemoActor::HandleSendResult(const FTonSendResult& Result)
{
    switch (Result.Result)
    {
    case ETonSendResult::Approved:
        {
            const FString Hash = Result.TxHash.IsEmpty()
                ? FString(TEXT("(mock)")) : Result.TxHash.Left(16) + TEXT("...");
            Log(FString::Printf(TEXT("Send Approved - txHash: %s"), *Hash), FColor::Green);
        }
        break;
    case ETonSendResult::Rejected:
        Log(TEXT("✘ Send Rejected by user"), FColor::Orange);
        break;
    case ETonSendResult::Timeout:
        Log(TEXT("✘ Send Timeout"), FColor::Orange);
        break;
    case ETonSendResult::Error:
        Log(FString::Printf(TEXT("⚠ Send Error: %s"), *Result.ErrorMessage), FColor::Red);
        break;
    }
}

// ─── Log helper ───────────────────────────────────────────────────────────────

void ATonConnectDemoActor::Log(const FString& Msg, FColor Color, float Duration)
{
    if (GEngine)
        GEngine->AddOnScreenDebugMessage(-1, Duration, Color, Msg);
    UE_LOG(LogTemp, Display, TEXT("%s"), *Msg);
}
