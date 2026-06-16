#include "TonConnectUIDemoActor.h"
#include "TonConnectSubsystem.h"
#include "TonBlueprintLibrary.h"
#include "ITonApiClient.h"
#include "UI/STonConnectPanel.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "GameFramework/PlayerController.h"
#include "Components/InputComponent.h"

#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"
#include "TextureResource.h"
#include "RenderUtils.h"

#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/CoreStyle.h"
#include "Brushes/SlateColorBrush.h"

#define LOCTEXT_NAMESPACE "TonConnectUIDemo"

// One shared dark brush for toast backgrounds (lives for the module's lifetime)
static const FSlateColorBrush GToastBrush(FLinearColor(0.05f, 0.06f, 0.09f, 0.95f));

ATonConnectUIDemoActor::ATonConnectUIDemoActor()
{
    PrimaryActorTick.bCanEverTick = true;
    AutoReceiveInput = EAutoReceiveInput::Player0;
}

void ATonConnectUIDemoActor::BeginPlay()
{
    Super::BeginPlay();

    UGameInstance* GI = GetGameInstance();
    if (!GI) return;

    TonConnect = GI->GetSubsystem<UTonConnectSubsystem>();
    if (!TonConnect)
    {
        UE_LOG(LogTemp, Error, TEXT("TonConnectUIDemoActor: subsystem not found — is the plugin enabled?"));
        return;
    }

    // Bind subsystem events
    TonConnect->OnQRReady.AddDynamic(this,            &ATonConnectUIDemoActor::HandleQRReady);
    TonConnect->OnConnected.AddDynamic(this,          &ATonConnectUIDemoActor::HandleConnected);
    TonConnect->OnDisconnected.AddDynamic(this,       &ATonConnectUIDemoActor::HandleDisconnected);
    TonConnect->OnError.AddDynamic(this,              &ATonConnectUIDemoActor::HandleError);
    TonConnect->OnSendResult.AddDynamic(this,         &ATonConnectUIDemoActor::HandleSendResult);
    TonConnect->OnAccountInfoUpdated.AddDynamic(this, &ATonConnectUIDemoActor::HandleAccountInfoUpdated);

    // Build the popup (hidden until ShowPanel)
    TWeakObjectPtr<UTonConnectSubsystem> WeakSub = TonConnect;
    SAssignNew(Panel, STonConnectPanel)
        .Subsystem(WeakSub)
        .OnConnectClicked(FSimpleDelegate::CreateUObject(this, &ATonConnectUIDemoActor::OnConnectClicked))
        .OnDisconnectClicked(FSimpleDelegate::CreateUObject(this, &ATonConnectUIDemoActor::OnDisconnectClicked))
        .OnCloseClicked(FSimpleDelegate::CreateUObject(this, &ATonConnectUIDemoActor::HidePanel))
        .OnRefreshClicked(FSimpleDelegate::CreateUObject(this, &ATonConnectUIDemoActor::OnRefreshClicked))
        .OnSendRequested(STonConnectPanel::FOnSendRequested::CreateUObject(
            this, &ATonConnectUIDemoActor::OnSendRequested))
        .OnSendInputChanged(FSimpleDelegate::CreateUObject(
            this, &ATonConnectUIDemoActor::RequestFeeEstimate));

    // Toast container — always on, top-center of the viewport
    SAssignNew(ToastBox, SVerticalBox);

    if (UGameViewportClient* VP = GetWorld() ? GetWorld()->GetGameViewport() : nullptr)
    {
        ToastLayer =
            SNew(SBox)
            .HAlign(HAlign_Center).VAlign(VAlign_Top)
            .Padding(FMargin(0.f, 24.f, 0.f, 0.f))
            [ ToastBox.ToSharedRef() ];
        VP->AddViewportWidgetContent(ToastLayer.ToSharedRef(), /*ZOrder*/ 100);
    }

    // Input — T toggles the popup
    if (InputComponent)
        InputComponent->BindKey(EKeys::T, IE_Pressed, this, &ATonConnectUIDemoActor::TogglePanel);

    // Pre-load the wallet picker list (cached on disk after first run)
    RefreshWalletList();

    // Already connected (restored session) → no toast spam, panel will show connected page
    if (bShowOnStart)
        ShowPanel();
}

void ATonConnectUIDemoActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (TonConnect)
    {
        TonConnect->OnQRReady.RemoveDynamic(this,            &ATonConnectUIDemoActor::HandleQRReady);
        TonConnect->OnConnected.RemoveDynamic(this,          &ATonConnectUIDemoActor::HandleConnected);
        TonConnect->OnDisconnected.RemoveDynamic(this,       &ATonConnectUIDemoActor::HandleDisconnected);
        TonConnect->OnError.RemoveDynamic(this,              &ATonConnectUIDemoActor::HandleError);
        TonConnect->OnSendResult.RemoveDynamic(this,         &ATonConnectUIDemoActor::HandleSendResult);
        TonConnect->OnAccountInfoUpdated.RemoveDynamic(this, &ATonConnectUIDemoActor::HandleAccountInfoUpdated);
    }

    if (UGameViewportClient* VP = GetWorld() ? GetWorld()->GetGameViewport() : nullptr)
    {
        if (bPanelVisible && Panel.IsValid()) VP->RemoveViewportWidgetContent(Panel.ToSharedRef());
        if (ToastLayer.IsValid())             VP->RemoveViewportWidgetContent(ToastLayer.ToSharedRef());
    }
    Panel.Reset();
    ToastBox.Reset();
    ToastLayer.Reset();
    bPanelVisible = false;

    Super::EndPlay(EndPlayReason);
}

void ATonConnectUIDemoActor::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    // Persistent hint when the popup is closed
    if (!bPanelVisible && GEngine)
    {
        GEngine->AddOnScreenDebugMessage(
            /*key*/ 7, 0.f, FColor(0, 168, 255),
            TEXT("Press [T] to open the TON wallet"));
    }
}

// ── Show / hide ───────────────────────────────────────────────────────────────

void ATonConnectUIDemoActor::TogglePanel()
{
    bPanelVisible ? HidePanel() : ShowPanel();
}

void ATonConnectUIDemoActor::ShowPanel()
{
    if (bPanelVisible || !Panel.IsValid()) return;

    if (UGameViewportClient* VP = GetWorld() ? GetWorld()->GetGameViewport() : nullptr)
    {
        VP->AddViewportWidgetContent(Panel.ToSharedRef(), /*ZOrder*/ 50);
        bPanelVisible = true;
        ApplyInputMode(true);
    }
}

void ATonConnectUIDemoActor::HidePanel()
{
    if (!bPanelVisible || !Panel.IsValid()) return;

    if (UGameViewportClient* VP = GetWorld() ? GetWorld()->GetGameViewport() : nullptr)
    {
        VP->RemoveViewportWidgetContent(Panel.ToSharedRef());
        bPanelVisible = false;
        ApplyInputMode(false);
    }
}

void ATonConnectUIDemoActor::ApplyInputMode(bool bUIOpen)
{
    APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
    if (!PC) return;

    if (bUIOpen)
    {
        FInputModeGameAndUI Mode;
        Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
        PC->SetInputMode(Mode);
        PC->bShowMouseCursor = true;
    }
    else
    {
        PC->SetInputMode(FInputModeGameOnly());
        PC->bShowMouseCursor = false;
    }
}

// ── Panel actions ─────────────────────────────────────────────────────────────

void ATonConnectUIDemoActor::OnConnectClicked()
{
    if (!TonConnect) return;

    // Use the wallet picked in the popup; fall back to subsystem default (Tonkeeper).
    if (Panel.IsValid())
    {
        TSharedPtr<FTonWalletUi> W = Panel->GetSelectedWallet();
        if (W.IsValid())
        {
            TonConnect->SelectWallet(W->AppName, W->UniversalUrl, W->BridgeUrl);
            PushToast(FString::Printf(TEXT("Connecting via %s…"), *W->DisplayName), FLinearColor(0.f, 0.66f, 1.f));
        }
    }
    TonConnect->Connect();
}

void ATonConnectUIDemoActor::OnRefreshClicked()
{
    if (!TonConnect) return;
    // Tear down the current (stale) connect attempt, then start a fresh one —
    // new keypair → new QR + deep-link.
    TonConnect->Disconnect();
    PushToast(TEXT("Refreshing QR…"), FLinearColor(0.f, 0.66f, 1.f), 2.f);
    OnConnectClicked(); // re-selects the chosen wallet and calls Connect()
}

// ── Wallet picker ─────────────────────────────────────────────────────────────

void ATonConnectUIDemoActor::RefreshWalletList()
{
    if (!TonConnect || !Panel.IsValid() || bWalletsRequested) return;
    bWalletsRequested = true;

    TWeakObjectPtr<ATonConnectUIDemoActor> WeakThis = this;
    TonConnect->FetchWalletList([WeakThis](bool bOk, TArray<FTonWalletListEntry> Wallets)
    {
        ATonConnectUIDemoActor* Self = WeakThis.Get();
        if (!Self || !Self->Panel.IsValid()) return;

        if (!bOk)
        {
            Self->bWalletsRequested = false; // allow a retry next time the panel opens
            Self->PushToast(TEXT("Could not load wallet list"), FLinearColor(1.f, 0.6f, 0.3f));
            return;
        }

        TArray<TSharedPtr<FTonWalletUi>> Items;
        TArray<FString> IconUrls;
        for (const FTonWalletListEntry& E : Wallets)
        {
            if (E.BridgeUrl.IsEmpty()) continue; // SSE-capable wallets only

            TSharedPtr<FTonWalletUi> Ui = MakeShared<FTonWalletUi>();
            Ui->AppName      = E.AppName;
            Ui->DisplayName  = E.Name.IsEmpty() ? E.AppName : E.Name;
            Ui->BridgeUrl    = E.BridgeUrl;
            Ui->UniversalUrl = E.UniversalUrls.Num() > 0 ? E.UniversalUrls[0] : FString();
            Ui->Icon         = MakeShared<FSlateBrush>();
            Ui->Icon->ImageSize = FVector2D(28.f, 28.f);
            Ui->Icon->DrawAs = ESlateBrushDrawType::Image;
            Ui->Icon->TintColor = FSlateColor(FLinearColor(1, 1, 1, 0)); // transparent until the icon loads
            Items.Add(Ui);
            IconUrls.Add(E.ImageUrl);
        }

        Self->Panel->SetWallets(Items);

        // Download + decode icons asynchronously (brush ptr is stable, repaints on arrival)
        Self->IconTextures.Reset();
        for (int32 i = 0; i < Items.Num(); ++i)
            Self->LoadWalletIcon(IconUrls[i], Items[i]->Icon);

        UE_LOG(LogTemp, Log, TEXT("TonConnect UI: %d SSE wallets in picker"), Items.Num());
    });
}

void ATonConnectUIDemoActor::LoadWalletIcon(const FString& Url, TSharedPtr<FSlateBrush> Brush)
{
    if (Url.IsEmpty() || !Brush.IsValid()) return;

    TWeakObjectPtr<ATonConnectUIDemoActor> WeakThis = this;
    TWeakPtr<FSlateBrush> WeakBrush = Brush;

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
    Req->SetURL(Url);
    Req->SetVerb(TEXT("GET"));
    Req->OnProcessRequestComplete().BindLambda(
        [WeakThis, WeakBrush](FHttpRequestPtr, FHttpResponsePtr Resp, bool bOk)
        {
            ATonConnectUIDemoActor* Self = WeakThis.Get();
            TSharedPtr<FSlateBrush> B = WeakBrush.Pin();
            if (!Self || !B.IsValid() || !bOk || !Resp.IsValid() || Resp->GetResponseCode() != 200)
                return;

            const TArray<uint8>& Data = Resp->GetContent();
            IImageWrapperModule& Mod = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
            const EImageFormat Fmt = Mod.DetectImageFormat(Data.GetData(), Data.Num());
            if (Fmt == EImageFormat::Invalid) return; // e.g. SVG — leave placeholder

            TSharedPtr<IImageWrapper> Wrapper = Mod.CreateImageWrapper(Fmt);
            if (!Wrapper.IsValid() || !Wrapper->SetCompressed(Data.GetData(), Data.Num())) return;

            TArray<uint8> Raw;
            if (!Wrapper->GetRaw(ERGBFormat::BGRA, 8, Raw)) return;
            const int32 W = Wrapper->GetWidth();
            const int32 H = Wrapper->GetHeight();
            if (W <= 0 || H <= 0 || Raw.Num() < W * H * 4) return;

            UTexture2D* Tex = UTexture2D::CreateTransient(W, H, PF_B8G8R8A8);
            if (!Tex) return;
            FTexture2DMipMap& Mip = Tex->GetPlatformData()->Mips[0];
            void* Dst = Mip.BulkData.Lock(LOCK_READ_WRITE);
            FMemory::Memcpy(Dst, Raw.GetData(), Raw.Num());
            Mip.BulkData.Unlock();
            Tex->UpdateResource();

            Self->IconTextures.Add(Tex); // keep alive from GC
            B->SetResourceObject(Tex);
            B->ImageSize = FVector2D(W, H);
            B->TintColor = FSlateColor(FLinearColor::White); // reveal now that we have pixels
        });
    Req->ProcessRequest();
}

void ATonConnectUIDemoActor::OnDisconnectClicked()
{
    if (TonConnect) TonConnect->Disconnect();
}

void ATonConnectUIDemoActor::OnSendRequested(FString ToAddress, FString AmountTon, FString Comment)
{
    if (!TonConnect || !Panel.IsValid()) return;

    if (TonConnect->GetState() != ETonConnectState::Connected)
    {
        PushToast(TEXT("Connect a wallet first"), FLinearColor(1.f, 0.7f, 0.2f));
        return;
    }

    const ETonTxKind Kind  = Panel->GetKind();
    const FString    Addr1 = Panel->GetAddressInput();
    const FString    Addr2 = Panel->GetAddress2Input();
    const FLinearColor Blue(0.f, 0.66f, 1.f), Warn(1.f, 0.7f, 0.2f), Bad(1.f, 0.4f, 0.4f);
    FOnTonSendResultDelegate Cb; // result captured via OnSendResult multicast

    switch (Kind)
    {
    case ETonTxKind::JettonTransfer:
    {
        if (!UTonBlueprintLibrary::IsValidTonAddress(Addr1))
        { PushToast(TEXT("Enter your jetton wallet address"), Bad); return; }
        if (!UTonBlueprintLibrary::IsValidTonAddress(Addr2))
        { PushToast(TEXT("Enter a recipient address"), Bad); return; }
        if (AmountTon.IsEmpty() || AmountTon == TEXT("0"))
        { PushToast(TEXT("Enter a jetton amount (base units)"), Warn); return; }

        Panel->SetSending(true);
        PushToast(FString::Printf(TEXT("Sending %s jetton units…"), *AmountTon), Blue);
        TonConnect->SendJettonTransfer(Addr1, Addr2, AmountTon, Comment, Cb);
        break;
    }
    case ETonTxKind::NftTransfer:
    {
        if (!UTonBlueprintLibrary::IsValidTonAddress(Addr1))
        { PushToast(TEXT("Enter the NFT item address"), Bad); return; }
        if (!UTonBlueprintLibrary::IsValidTonAddress(Addr2))
        { PushToast(TEXT("Enter the new owner address"), Bad); return; }

        Panel->SetSending(true);
        const FString Short = UTonBlueprintLibrary::TruncateAddress(Addr2, 6, 4);
        PushToast(FString::Printf(TEXT("Transferring NFT to %s…"), *Short), Blue);
        TonConnect->SendNftTransfer(Addr1, Addr2, Cb);
        break;
    }
    case ETonTxKind::NativeTransfer:
    default:
    {
        // Empty recipient → send to self
        const FString Dest = Addr1.IsEmpty() ? TonConnect->GetConnectedWallet().Address : Addr1;
        if (!UTonBlueprintLibrary::IsValidTonAddress(Dest))
        { PushToast(TEXT("Invalid recipient address"), Bad); return; }

        const FString Nano = UTonBlueprintLibrary::ParseTonAmount(AmountTon);
        if (Nano.IsEmpty() || Nano == TEXT("0"))
        { PushToast(TEXT("Enter an amount greater than 0"), Warn); return; }

        Panel->SetSending(true);
        const FString Short = UTonBlueprintLibrary::TruncateAddress(Dest, 6, 4);
        PushToast(FString::Printf(TEXT("Sending %s TON to %s…"), *AmountTon, *Short), Blue);
        TonConnect->SendTon(Dest, Nano, Comment, Cb);
        break;
    }
    }
}

// ── Subsystem events ──────────────────────────────────────────────────────────

void ATonConnectUIDemoActor::HandleQRReady(UTexture2D* Tex, const FString& DeepLink)
{
    QRTexture = Tex; // keep alive
    if (Panel.IsValid())
    {
        Panel->SetQRTexture(Tex);
        Panel->SetDeepLink(DeepLink); // enables the "Open in wallet" button (tap-to-open on mobile)
    }

    // Auto-open the popup when a QR is produced (e.g. Connect() called externally)
    if (!bPanelVisible) ShowPanel();
}

void ATonConnectUIDemoActor::HandleConnected(const FTonWalletInfo& Info)
{
    const FString Short = UTonBlueprintLibrary::TruncateAddress(Info.Address, 6, 4);
    PushToast(FString::Printf(TEXT("Connected  •  %s"), *Short), FLinearColor(0.1f, 0.85f, 0.4f));
    if (Panel.IsValid()) Panel->SetError(FString()); // clear any stale error banner
    // Panel auto-switches to the connected page (state-driven) — keep it open
    RequestFeeEstimate(); // initial estimate for the default amount
}

void ATonConnectUIDemoActor::RequestFeeEstimate()
{
    if (!TonConnect || !Panel.IsValid()) return;
    if (TonConnect->GetState() != ETonConnectState::Connected) return;

    FOnTonFeeEstimateDelegate Cb;
    Cb.BindDynamic(this, &ATonConnectUIDemoActor::HandleFeeEstimate);

    const ETonTxKind Kind = Panel->GetKind();
    const FString Addr1 = Panel->GetAddressInput();
    const FString Addr2 = Panel->GetAddress2Input();

    switch (Kind)
    {
    case ETonTxKind::JettonTransfer:
    {
        // Needs sender's jetton wallet + recipient; until both are filled, show the
        // rough estimate as the emulated row so it isn't stuck on "estimating…".
        if (Addr1.IsEmpty() || Addr2.IsEmpty())
        {
            Panel->SetEmulatedFee(UTonBlueprintLibrary::EstimateFeeQuick(Kind), false);
            return;
        }
        TonConnect->EstimateFeeEmulatedJetton(Addr1, Addr2, Panel->GetAmountInput(), Cb);
        break;
    }
    case ETonTxKind::NftTransfer:
    {
        if (Addr1.IsEmpty() || Addr2.IsEmpty())
        {
            Panel->SetEmulatedFee(UTonBlueprintLibrary::EstimateFeeQuick(Kind), false);
            return;
        }
        TonConnect->EstimateFeeEmulatedNft(Addr1, Addr2, Cb);
        break;
    }
    case ETonTxKind::NativeTransfer:
    default:
    {
        FString Addr = Addr1.IsEmpty() ? TonConnect->GetConnectedWallet().Address : Addr1; // self
        const FString Nano = UTonBlueprintLibrary::ParseTonAmount(Panel->GetAmountInput());
        TonConnect->EstimateFeeEmulated(Addr, Nano, ETonTxKind::NativeTransfer, Cb);
        break;
    }
    }
}

void ATonConnectUIDemoActor::HandleFeeEstimate(const FTonFeeEstimate& Estimate)
{
    if (Panel.IsValid())
        Panel->SetEmulatedFee(Estimate.TotalFeeNano, Estimate.bSuccess && Estimate.bEmulated);
}

void ATonConnectUIDemoActor::HandleDisconnected()
{
    PushToast(TEXT("Wallet disconnected"), FLinearColor(1.f, 0.6f, 0.3f));
}

void ATonConnectUIDemoActor::HandleError(const FString& Msg)
{
    PushToast(FString::Printf(TEXT("Error: %s"), *Msg), FLinearColor(1.f, 0.4f, 0.4f), 5.f);
    if (Panel.IsValid())
    {
        Panel->SetSending(false);     // unlock the form if an error interrupted a send
        Panel->SetError(Msg);         // persistent banner so the failure stays visible
    }
}

void ATonConnectUIDemoActor::HandleSendResult(const FTonSendResult& Result)
{
    if (Panel.IsValid()) Panel->SetSending(false);

    switch (Result.Result)
    {
    case ETonSendResult::Approved:
    {
        const FString Hash = Result.TxHash.IsEmpty() ? TEXT("(mock)") : Result.TxHash.Left(12) + TEXT("…");
        PushToast(FString::Printf(TEXT("✔ Sent  •  tx %s"), *Hash), FLinearColor(0.1f, 0.85f, 0.4f), 4.5f);
        break;
    }
    case ETonSendResult::Rejected:
        PushToast(TEXT("✘ Transaction rejected"), FLinearColor(1.f, 0.6f, 0.3f));
        break;
    case ETonSendResult::Timeout:
        PushToast(TEXT("✘ Transaction timed out"), FLinearColor(1.f, 0.6f, 0.3f));
        break;
    case ETonSendResult::Error:
        PushToast(FString::Printf(TEXT("⚠ Send error: %s"), *Result.ErrorMessage),
                  FLinearColor(1.f, 0.4f, 0.4f), 5.f);
        break;
    }
}

void ATonConnectUIDemoActor::HandleAccountInfoUpdated(const FString& Version, const FString& BalanceNano)
{
    const FString Bal = UTonBlueprintLibrary::FormatTon(BalanceNano, 4);
    PushToast(FString::Printf(TEXT("Balance: %s  •  %s"), *Bal, *Version), FLinearColor(0.6f, 0.7f, 0.85f), 2.5f);
}

// ── Toasts ────────────────────────────────────────────────────────────────────

void ATonConnectUIDemoActor::PushToast(const FString& Message, const FLinearColor& Color, float Duration)
{
    if (!ToastBox.IsValid()) return;

    TSharedRef<SWidget> Row =
        SNew(SBox).Padding(FMargin(0.f, 3.f))
        [
            SNew(SBorder)
            .BorderImage(&GToastBrush)
            .Padding(FMargin(16.f, 9.f))
            [
                SNew(STextBlock)
                .Text(FText::FromString(Message))
                .ColorAndOpacity(FSlateColor(Color))
                .Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
            ]
        ];

    ToastBox->AddSlot().AutoHeight().HAlign(HAlign_Center) [ Row ];

    // Auto-remove after Duration
    TWeakPtr<SWidget>     WeakRow = Row;
    TWeakPtr<SVerticalBox> WeakBox = ToastBox;
    FTimerHandle Handle;
    GetWorldTimerManager().SetTimer(Handle, [WeakRow, WeakBox]()
    {
        TSharedPtr<SVerticalBox> Box = WeakBox.Pin();
        TSharedPtr<SWidget>      R   = WeakRow.Pin();
        if (Box.IsValid() && R.IsValid())
            Box->RemoveSlot(R.ToSharedRef());
    }, Duration, false);
}

#undef LOCTEXT_NAMESPACE
