#include "UI/TonStatusWidget.h"
#include "TonConnectSubsystem.h"
#include "TonBlueprintLibrary.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

void UTonStatusWidget::NativeConstruct()
{
    Super::NativeConstruct();

    UGameInstance* GI = GetGameInstance();
    if (!GI) return;

    TonConnect = GI->GetSubsystem<UTonConnectSubsystem>();
    if (!TonConnect) return;

    TonConnect->OnConnected.AddDynamic(this, &UTonStatusWidget::HandleConnected);
    TonConnect->OnDisconnected.AddDynamic(this, &UTonStatusWidget::HandleDisconnected);

    // Sync immediately with current state in case we're shown after a connect
    if (TonConnect->GetState() == ETonConnectState::Connected)
    {
        RefreshState(ETonConnectState::Connected,
                     TonConnect->GetConnectedWallet().Address);
    }
    else
    {
        RefreshState(TonConnect->GetState());
    }
}

void UTonStatusWidget::NativeDestruct()
{
    if (TonConnect)
    {
        TonConnect->OnConnected.RemoveDynamic(this, &UTonStatusWidget::HandleConnected);
        TonConnect->OnDisconnected.RemoveDynamic(this, &UTonStatusWidget::HandleDisconnected);
    }
    Super::NativeDestruct();
}

void UTonStatusWidget::HandleConnected(const FTonWalletInfo& Info)
{
    RefreshState(ETonConnectState::Connected, Info.Address);
}

void UTonStatusWidget::HandleDisconnected()
{
    RefreshState(ETonConnectState::Disconnected);
}

void UTonStatusWidget::RefreshState(ETonConnectState State, const FString& Address)
{
    CurrentState   = State;
    FullAddress    = Address;
    DisplayAddress = Address.IsEmpty()
        ? TEXT("")
        : UTonBlueprintLibrary::TruncateAddress(Address, AddressPrefixLen, AddressSuffixLen);

    OnStateChanged(State);
    OnAddressChanged(DisplayAddress, FullAddress);
}
