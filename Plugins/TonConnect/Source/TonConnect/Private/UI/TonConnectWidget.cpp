#include "UI/TonConnectWidget.h"
#include "TonConnectSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

void UTonConnectWidget::NativeConstruct()
{
    Super::NativeConstruct();

    UGameInstance* GI = GetGameInstance();
    if (!GI) return;

    TonConnect = GI->GetSubsystem<UTonConnectSubsystem>();
    if (!TonConnect) return;

    TonConnect->OnQRReady.AddDynamic(this, &UTonConnectWidget::HandleQRReady);
    TonConnect->OnConnected.AddDynamic(this, &UTonConnectWidget::HandleConnected);
    TonConnect->OnDisconnected.AddDynamic(this, &UTonConnectWidget::HandleDisconnected);
    TonConnect->OnError.AddDynamic(this, &UTonConnectWidget::HandleError);
}

void UTonConnectWidget::NativeDestruct()
{
    if (TonConnect)
    {
        TonConnect->OnQRReady.RemoveDynamic(this, &UTonConnectWidget::HandleQRReady);
        TonConnect->OnConnected.RemoveDynamic(this, &UTonConnectWidget::HandleConnected);
        TonConnect->OnDisconnected.RemoveDynamic(this, &UTonConnectWidget::HandleDisconnected);
        TonConnect->OnError.RemoveDynamic(this, &UTonConnectWidget::HandleError);
    }
    Super::NativeDestruct();
}

void UTonConnectWidget::StartConnect()
{
    if (TonConnect) TonConnect->Connect();
}

void UTonConnectWidget::StartDisconnect()
{
    if (TonConnect) TonConnect->Disconnect();
}

void UTonConnectWidget::HandleQRReady(UTexture2D* Tex, const FString& DeepLink)    { OnQRCodeReady(Tex, DeepLink); }
void UTonConnectWidget::HandleConnected(const FTonWalletInfo& Info) { OnWalletConnected(Info); }
void UTonConnectWidget::HandleDisconnected()              { OnWalletDisconnected(); }
void UTonConnectWidget::HandleError(const FString& Msg)  { OnWalletError(Msg); }
