#include "UI/STonConnectPanel.h"
#include "TonConnectSubsystem.h"
#include "TonBlueprintLibrary.h"
#include "Engine/Texture2D.h"

#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Engine/Texture2D.h"
#include "Styling/StyleDefaults.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Images/SThrobber.h"
#include "Styling/CoreStyle.h"
#include "HAL/PlatformProcess.h" // FPlatformProcess::LaunchURL (tap-to-open deep link)

#define LOCTEXT_NAMESPACE "TonConnectPanel"

namespace
{
    // Small helpers for consistent typography
    TSharedRef<STextBlock> Label(const FText& Text, int32 Size, const FLinearColor& Color, bool bBold = false)
    {
        return SNew(STextBlock)
            .Text(Text)
            .ColorAndOpacity(FSlateColor(Color))
            .Font(FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", Size));
    }
}

void STonConnectPanel::Construct(const FArguments& InArgs)
{
    Subsystem           = InArgs._Subsystem;
    OnConnectClicked    = InArgs._OnConnectClicked;
    OnDisconnectClicked = InArgs._OnDisconnectClicked;
    OnCloseClicked      = InArgs._OnCloseClicked;
    OnRefreshClicked    = InArgs._OnRefreshClicked;
    OnSendRequested     = InArgs._OnSendRequested;
    OnSendInputChanged  = InArgs._OnSendInputChanged;

    const FLinearColor White  (0.92f, 0.94f, 0.98f);
    const FLinearColor Muted  (0.55f, 0.60f, 0.70f);
    const FLinearColor Accent (0.00f, 0.66f, 1.00f);

    ChildSlot
    .HAlign(HAlign_Center)
    .VAlign(VAlign_Center)
    [
        // Fixed-width popup card
        SNew(SBox).WidthOverride(420.f)
        [
            SNew(SBorder)
            .BorderImage(&PanelBgBrush)
            .Padding(0.f)
            [
                SNew(SVerticalBox)

                // ── Header bar ───────────────────────────────────────────────
                + SVerticalBox::Slot().AutoHeight()
                [
                    SNew(SBorder)
                    .BorderImage(&HeaderBgBrush)
                    .Padding(FMargin(16.f, 10.f))
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
                        [
                            Label(LOCTEXT("Title", "TON Wallet"), 15, FLinearColor::White, true)
                        ]
                        // Network badge — wallet's authoritative network, shown once connected
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 12, 0)
                        [
                            SNew(STextBlock)
                            .Visibility(this, &STonConnectPanel::GetNetworkBadgeVisibility)
                            .Text(this, &STonConnectPanel::GetNetworkBadgeText)
                            .ColorAndOpacity(this, &STonConnectPanel::GetNetworkBadgeColor)
                            .Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
                        ]
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                        [
                            SNew(SButton)
                            .ButtonStyle(FCoreStyle::Get(), "NoBorder")
                            .OnClicked(this, &STonConnectPanel::HandleClose)
                            .ToolTipText(LOCTEXT("Close", "Close (T)"))
                            [
                                Label(LOCTEXT("X", "✕"), 16, FLinearColor::White, true)
                            ]
                        ]
                    ]
                ]

                // ── Error banner (connect timeout / bridge unreachable / etc.) ─
                + SVerticalBox::Slot().AutoHeight().Padding(20.f, 12.f, 20.f, 0.f)
                [
                    SNew(SBorder)
                    .Visibility(this, &STonConnectPanel::GetErrorVisibility)
                    .BorderImage(&CardBgBrush)
                    .Padding(FMargin(12.f, 8.f))
                    [
                        SNew(STextBlock)
                        .AutoWrapText(true)
                        .Text(this, &STonConnectPanel::GetErrorText)
                        .ColorAndOpacity(FSlateColor(FLinearColor(1.f, 0.45f, 0.40f)))
                        .Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
                    ]
                ]

                // ── Body: page switcher driven by connection state ───────────
                + SVerticalBox::Slot().AutoHeight().Padding(20.f)
                [
                    SNew(SWidgetSwitcher)
                    .WidgetIndex(this, &STonConnectPanel::GetActivePage)

                    // Page 0 — Disconnected (wallet picker)
                    + SWidgetSwitcher::Slot()
                    [
                        SNew(SVerticalBox)
                        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8).HAlign(HAlign_Center)
                        [
                            Label(LOCTEXT("ChooseWallet", "Choose your wallet"), 11, Muted, true)
                        ]

                        // "Loading…" until the list arrives
                        + SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 6).HAlign(HAlign_Center)
                        [
                            SNew(SHorizontalBox)
                            .Visibility(this, &STonConnectPanel::GetWalletLoadingVisibility)
                            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
                            [
                                SNew(SCircularThrobber).Radius(8.f)
                            ]
                            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                            [
                                Label(LOCTEXT("LoadingWallets", "Loading wallets…"), 10, Muted)
                            ]
                        ]

                        // Scrollable wallet list
                        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 14)
                        [
                            SNew(SBox).MaxDesiredHeight(208.f)
                            [
                                SNew(SScrollBox)
                                + SScrollBox::Slot()
                                [
                                    SAssignNew(WalletListBox, SVerticalBox)
                                ]
                            ]
                        ]

                        + SVerticalBox::Slot().AutoHeight()
                        [
                            SNew(SButton)
                            .IsEnabled(this, &STonConnectPanel::IsConnectEnabled)
                            .HAlign(HAlign_Center).VAlign(VAlign_Center)
                            .ContentPadding(FMargin(0, 12))
                            .OnClicked(this, &STonConnectPanel::HandleConnect)
                            [
                                Label(LOCTEXT("ConnectBtn", "Connect Wallet"), 13, FLinearColor::White, true)
                            ]
                        ]
                    ]

                    // Page 1 — Connecting (QR)
                    + SWidgetSwitcher::Slot()
                    [
                        SNew(SVerticalBox)
                        + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 10)
                        [
                            Label(LOCTEXT("ScanQR", "Scan with Tonkeeper"), 13, White, true)
                        ]
                        + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 12)
                        [
                            SNew(SBorder)
                            .BorderImage(&CardBgBrush)
                            .Padding(10.f)
                            [
                                SNew(SBox).WidthOverride(220.f).HeightOverride(220.f)
                                [
                                    SNew(SImage).Image(&QRBrush)
                                ]
                            ]
                        ]
                        + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 14)
                        [
                            SNew(SHorizontalBox)
                            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
                            [
                                SNew(SCircularThrobber).Radius(10.f)
                            ]
                            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                            [
                                Label(LOCTEXT("Waiting", "Waiting for wallet…"), 10, Muted)
                            ]
                        ]
                        // Tap-to-open the wallet app on the same device (mobile)
                        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
                        [
                            SNew(SButton)
                            .Visibility(this, &STonConnectPanel::GetOpenWalletVisibility)
                            .HAlign(HAlign_Center).VAlign(VAlign_Center)
                            .ContentPadding(FMargin(0, 10))
                            .OnClicked(this, &STonConnectPanel::HandleOpenWallet)
                            [
                                Label(LOCTEXT("OpenWalletBtn", "Open in wallet  ↗"), 12, FLinearColor::White, true)
                            ]
                        ]
                        // Refresh (regenerate) the QR + Cancel, side by side
                        + SVerticalBox::Slot().AutoHeight()
                        [
                            SNew(SHorizontalBox)
                            + SHorizontalBox::Slot().FillWidth(1.f).Padding(0, 0, 4, 0)
                            [
                                SNew(SButton)
                                .HAlign(HAlign_Center).ContentPadding(FMargin(0, 8))
                                .ToolTipText(LOCTEXT("RefreshTip", "Generate a fresh QR / link"))
                                .OnClicked(this, &STonConnectPanel::HandleRefresh)
                                [
                                    Label(LOCTEXT("RefreshBtn", "↻ Refresh QR"), 12, FLinearColor(0.70f, 0.75f, 0.85f))
                                ]
                            ]
                            + SHorizontalBox::Slot().FillWidth(1.f).Padding(4, 0, 0, 0)
                            [
                                SNew(SButton)
                                .HAlign(HAlign_Center).ContentPadding(FMargin(0, 8))
                                .OnClicked(this, &STonConnectPanel::HandleDisconnect)
                                [
                                    Label(LOCTEXT("CancelBtn", "Cancel"), 12, Muted)
                                ]
                            ]
                        ]
                    ]

                    // Page 2 — Connected
                    + SWidgetSwitcher::Slot()
                    [
                        SNew(SVerticalBox)

                        // Wallet info card
                        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 16)
                        [
                            SNew(SBorder)
                            .BorderImage(&CardBgBrush)
                            .Padding(FMargin(14.f, 12.f))
                            [
                                SNew(SVerticalBox)
                                + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
                                [
                                    SNew(SHorizontalBox)
                                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
                                    [
                                        Label(LOCTEXT("Dot", "●"), 11, FLinearColor(0.1f, 0.85f, 0.4f), true)
                                    ]
                                    + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
                                    [
                                        SNew(STextBlock)
                                        .Text(this, &STonConnectPanel::GetAddressText)
                                        .ColorAndOpacity(FSlateColor(White))
                                        .Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
                                    ]
                                ]
                                + SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 0)
                                [
                                    SNew(STextBlock)
                                    .Text(this, &STonConnectPanel::GetBalanceText)
                                    .ColorAndOpacity(FSlateColor(Accent))
                                    .Font(FCoreStyle::GetDefaultFontStyle("Bold", 16))
                                ]
                                + SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
                                [
                                    SNew(STextBlock)
                                    .Text(this, &STonConnectPanel::GetVersionText)
                                    .ColorAndOpacity(FSlateColor(Muted))
                                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
                                ]
                                + SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 0)
                                [
                                    SNew(STextBlock)
                                    .Text(this, &STonConnectPanel::GetWalletMetaText)
                                    .ColorAndOpacity(FSlateColor(FLinearColor(0.40f, 0.45f, 0.55f)))
                                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
                                ]
                            ]
                        ]

                        // Send form
                        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
                        [
                            SNew(STextBlock)
                            .Text(this, &STonConnectPanel::GetSendFormTitle)
                            .ColorAndOpacity(FSlateColor(Muted))
                            .Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
                        ]

                        // Kind tabs: TON / Jetton / NFT
                        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
                        [
                            SNew(SHorizontalBox)
                            + SHorizontalBox::Slot().FillWidth(1.f).Padding(0, 0, 4, 0)
                            [
                                SNew(SButton)
                                .IsEnabled(this, &STonConnectPanel::IsInteractable)
                                .HAlign(HAlign_Center).ContentPadding(FMargin(0, 6))
                                .ButtonColorAndOpacity(TAttribute<FSlateColor>::CreateSP(this, &STonConnectPanel::KindTabColor, 0))
                                .OnClicked(FOnClicked::CreateSP(this, &STonConnectPanel::HandleKindClicked, 0))
                                [ Label(LOCTEXT("KindTon", "TON"), 11, FLinearColor::White, true) ]
                            ]
                            + SHorizontalBox::Slot().FillWidth(1.f).Padding(2, 0, 2, 0)
                            [
                                SNew(SButton)
                                .IsEnabled(this, &STonConnectPanel::IsInteractable)
                                .HAlign(HAlign_Center).ContentPadding(FMargin(0, 6))
                                .ButtonColorAndOpacity(TAttribute<FSlateColor>::CreateSP(this, &STonConnectPanel::KindTabColor, 1))
                                .OnClicked(FOnClicked::CreateSP(this, &STonConnectPanel::HandleKindClicked, 1))
                                [ Label(LOCTEXT("KindJetton", "Jetton"), 11, FLinearColor::White, true) ]
                            ]
                            + SHorizontalBox::Slot().FillWidth(1.f).Padding(4, 0, 0, 0)
                            [
                                SNew(SButton)
                                .IsEnabled(this, &STonConnectPanel::IsInteractable)
                                .HAlign(HAlign_Center).ContentPadding(FMargin(0, 6))
                                .ButtonColorAndOpacity(TAttribute<FSlateColor>::CreateSP(this, &STonConnectPanel::KindTabColor, 2))
                                .OnClicked(FOnClicked::CreateSP(this, &STonConnectPanel::HandleKindClicked, 2))
                                [ Label(LOCTEXT("KindNft", "NFT"), 11, FLinearColor::White, true) ]
                            ]
                        ]

                        // Primary address (recipient / jetton wallet / NFT item)
                        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
                        [
                            SAssignNew(AddressBox, SEditableTextBox)
                            .IsEnabled(this, &STonConnectPanel::IsInteractable)
                            .OnTextChanged(this, &STonConnectPanel::HandleSendInputChanged)
                            .HintText(this, &STonConnectPanel::GetAddress1Hint)
                        ]

                        // Secondary address (jetton dest / NFT new owner) — hidden for native
                        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
                        [
                            SAssignNew(Address2Box, SEditableTextBox)
                            .Visibility(this, &STonConnectPanel::GetAddress2Visibility)
                            .IsEnabled(this, &STonConnectPanel::IsInteractable)
                            .OnTextChanged(this, &STonConnectPanel::HandleSendInputChanged)
                            .HintText(this, &STonConnectPanel::GetAddress2Hint)
                        ]

                        // Amount (TON / jetton base units) — hidden for NFT
                        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
                        [
                            SNew(SHorizontalBox)
                            .Visibility(this, &STonConnectPanel::GetAmountRowVisibility)
                            + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
                            [
                                SAssignNew(AmountBox, SEditableTextBox)
                                .IsEnabled(this, &STonConnectPanel::IsInteractable)
                                .Text(LOCTEXT("DefaultAmount", "0.01"))
                                .OnTextChanged(this, &STonConnectPanel::HandleSendInputChanged)
                                .HintText(LOCTEXT("AmountHint", "Amount"))
                            ]
                            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 0, 0)
                            [
                                SNew(STextBlock)
                                .Text(this, &STonConnectPanel::GetAmountUnitText)
                                .ColorAndOpacity(FSlateColor(Muted))
                                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 12))
                            ]
                        ]

                        // Fee estimates — instant heuristic + on-chain emulation
                        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
                        [
                            SNew(SHorizontalBox)
                            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                            [
                                Label(LOCTEXT("FeeQuickLabel", "Est. fee (rough)"), 9, Muted)
                            ]
                            + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(8, 0, 0, 0)
                            [
                                SNew(STextBlock)
                                .Text(this, &STonConnectPanel::GetQuickFeeText)
                                .ColorAndOpacity(FSlateColor(FLinearColor(0.70f, 0.75f, 0.85f)))
                                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
                            ]
                        ]
                        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
                        [
                            SNew(SHorizontalBox)
                            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                            [
                                Label(LOCTEXT("FeeEmuLabel", "Emulated"), 9, Muted)
                            ]
                            + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(8, 0, 0, 0)
                            [
                                SNew(STextBlock)
                                .Text(this, &STonConnectPanel::GetEmulatedFeeText)
                                .ColorAndOpacity(FSlateColor(FLinearColor(0.30f, 0.80f, 0.95f)))
                                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
                            ]
                        ]

                        // Send button / sending throbber
                        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 16)
                        [
                            SNew(SBox).HeightOverride(40.f)
                            [
                                SNew(SOverlay)
                                + SOverlay::Slot()
                                [
                                    SNew(SButton)
                                    .Visibility(this, &STonConnectPanel::GetSendButtonVisibility)
                                    .HAlign(HAlign_Center).VAlign(VAlign_Center)
                                    .OnClicked(this, &STonConnectPanel::HandleSend)
                                    [
                                        Label(LOCTEXT("SendBtn", "Send"), 13, FLinearColor::White, true)
                                    ]
                                ]
                                + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
                                [
                                    SNew(SHorizontalBox)
                                    .Visibility(this, &STonConnectPanel::GetSendingVisibility)
                                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
                                    [
                                        SNew(SCircularThrobber).Radius(9.f)
                                    ]
                                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                                    [
                                        Label(LOCTEXT("Sending", "Sending… confirm on your wallet"), 10, Muted)
                                    ]
                                ]
                            ]
                        ]

                        // Disconnect
                        + SVerticalBox::Slot().AutoHeight()
                        [
                            SNew(SButton)
                            .IsEnabled(this, &STonConnectPanel::IsInteractable)
                            .HAlign(HAlign_Center).ContentPadding(FMargin(0, 8))
                            .OnClicked(this, &STonConnectPanel::HandleDisconnect)
                            [
                                Label(LOCTEXT("DisconnectBtn", "Disconnect"), 12, FLinearColor(1.f, 0.45f, 0.4f))
                            ]
                        ]
                    ]
                ]
            ]
        ]
    ];

    // Placeholder QR brush size until a texture arrives
    QRBrush.ImageSize = FVector2D(220.f, 220.f);
}

void STonConnectPanel::SetQRTexture(UTexture2D* Texture)
{
    if (!Texture) return;
    QRBrush.SetResourceObject(Texture);
    QRBrush.ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
}

void STonConnectPanel::SetDeepLink(const FString& InLink)
{
    DeepLink = InLink;
}

FReply STonConnectPanel::HandleRefresh()
{
    LastError.Reset();
    OnRefreshClicked.ExecuteIfBound();
    return FReply::Handled();
}

FReply STonConnectPanel::HandleOpenWallet()
{
    if (!DeepLink.IsEmpty())
        FPlatformProcess::LaunchURL(*DeepLink, nullptr, nullptr);
    return FReply::Handled();
}

EVisibility STonConnectPanel::GetOpenWalletVisibility() const
{
    return DeepLink.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible;
}

// ── Wallet picker ─────────────────────────────────────────────────────────────

void STonConnectPanel::SetWallets(const TArray<TSharedPtr<FTonWalletUi>>& InWallets)
{
    Wallets = InWallets;

    // Default-select Tonkeeper if present, else the first wallet.
    SelectedWalletIndex = Wallets.Num() > 0 ? 0 : INDEX_NONE;
    for (int32 i = 0; i < Wallets.Num(); ++i)
    {
        if (Wallets[i].IsValid() && Wallets[i]->AppName == TEXT("tonkeeper")) { SelectedWalletIndex = i; break; }
    }

    RebuildWalletList();
}

TSharedPtr<FTonWalletUi> STonConnectPanel::GetSelectedWallet() const
{
    return Wallets.IsValidIndex(SelectedWalletIndex) ? Wallets[SelectedWalletIndex] : nullptr;
}

// ── Send inputs + fee display ─────────────────────────────────────────────────

FString STonConnectPanel::GetAmountInput() const
{
    return AmountBox.IsValid() ? AmountBox->GetText().ToString().TrimStartAndEnd() : FString();
}

FString STonConnectPanel::GetAddressInput() const
{
    return AddressBox.IsValid() ? AddressBox->GetText().ToString().TrimStartAndEnd() : FString();
}

void STonConnectPanel::SetEmulatedFee(const FString& FeeNano, bool bOk)
{
    EmulatedFeeNano = FeeNano;
    bEmulatedFeeOk  = bOk;
}

void STonConnectPanel::SetError(const FString& Message)
{
    LastError = Message;
}

void STonConnectPanel::HandleSendInputChanged(const FText&)
{
    EmulatedFeeNano.Reset();         // show "estimating…" until the new result lands
    bEmulatedFeeOk = false;
    OnSendInputChanged.ExecuteIfBound();
}

FText STonConnectPanel::GetQuickFeeText() const
{
    const FString Nano = UTonBlueprintLibrary::EstimateFeeQuick(SelectedKind);
    return FText::FromString(TEXT("~ ") + UTonBlueprintLibrary::FormatTon(Nano, 4));
}

// ── Transaction-kind tabs ─────────────────────────────────────────────────────

FString STonConnectPanel::GetAddress2Input() const
{
    return Address2Box.IsValid() ? Address2Box->GetText().ToString().TrimStartAndEnd() : FString();
}

FReply STonConnectPanel::HandleKindClicked(int32 KindIndex)
{
    const ETonTxKind NewKind =
        KindIndex == 1 ? ETonTxKind::JettonTransfer :
        KindIndex == 2 ? ETonTxKind::NftTransfer    :
                         ETonTxKind::NativeTransfer;

    if (NewKind == SelectedKind) return FReply::Handled();
    SelectedKind = NewKind;

    // Re-estimate for the new kind, showing "estimating…" until the result lands.
    EmulatedFeeNano.Reset();
    bEmulatedFeeOk = false;
    OnSendInputChanged.ExecuteIfBound();
    return FReply::Handled();
}

FSlateColor STonConnectPanel::KindTabColor(int32 KindIndex) const
{
    const ETonTxKind ThisKind =
        KindIndex == 1 ? ETonTxKind::JettonTransfer :
        KindIndex == 2 ? ETonTxKind::NftTransfer    :
                         ETonTxKind::NativeTransfer;

    return (ThisKind == SelectedKind)
        ? FSlateColor(FLinearColor(0.00f, 0.52f, 1.00f))   // active — accent blue
        : FSlateColor(FLinearColor(0.16f, 0.18f, 0.24f));  // inactive — dim
}

FText STonConnectPanel::GetSendFormTitle() const
{
    switch (SelectedKind)
    {
    case ETonTxKind::JettonTransfer: return LOCTEXT("SendJetton", "Send Jetton");
    case ETonTxKind::NftTransfer:    return LOCTEXT("TransferNft", "Transfer NFT");
    default:                         return LOCTEXT("SendTon", "Send TON");
    }
}

FText STonConnectPanel::GetAddress1Hint() const
{
    switch (SelectedKind)
    {
    case ETonTxKind::JettonTransfer: return LOCTEXT("JwHint", "Your jetton wallet address");
    case ETonTxKind::NftTransfer:    return LOCTEXT("NftHint", "NFT item address");
    default:                         return LOCTEXT("ToHint", "Recipient address (empty = self)");
    }
}

FText STonConnectPanel::GetAddress2Hint() const
{
    switch (SelectedKind)
    {
    case ETonTxKind::JettonTransfer: return LOCTEXT("JdHint", "Recipient address");
    case ETonTxKind::NftTransfer:    return LOCTEXT("NoHint", "New owner address");
    default:                         return FText::GetEmpty();
    }
}

FText STonConnectPanel::GetAmountUnitText() const
{
    return SelectedKind == ETonTxKind::JettonTransfer
        ? LOCTEXT("UnitUnits", "units") : LOCTEXT("UnitTon", "TON");
}

EVisibility STonConnectPanel::GetAddress2Visibility() const
{
    return SelectedKind == ETonTxKind::NativeTransfer
        ? EVisibility::Collapsed : EVisibility::Visible;
}

EVisibility STonConnectPanel::GetAmountRowVisibility() const
{
    // NFT transfer carries no amount.
    return SelectedKind == ETonTxKind::NftTransfer
        ? EVisibility::Collapsed : EVisibility::Visible;
}

FText STonConnectPanel::GetEmulatedFeeText() const
{
    if (EmulatedFeeNano.IsEmpty())
        return LOCTEXT("FeeEmuPending", "estimating…");
    const FString Val = UTonBlueprintLibrary::FormatTon(EmulatedFeeNano, 6);
    return bEmulatedFeeOk ? FText::FromString(Val)
                          : FText::FromString(Val + TEXT("  (approx)"));
}

void STonConnectPanel::RebuildWalletList()
{
    if (!WalletListBox.IsValid()) return;
    WalletListBox->ClearChildren();

    const FLinearColor White(0.92f, 0.94f, 0.98f);

    for (int32 i = 0; i < Wallets.Num(); ++i)
    {
        const TSharedPtr<FTonWalletUi>& W = Wallets[i];
        if (!W.IsValid()) continue;

        WalletListBox->AddSlot().AutoHeight().Padding(0, 2)
        [
            SNew(SButton)
            .ButtonStyle(FCoreStyle::Get(), "NoBorder")
            .ContentPadding(FMargin(0))
            .OnClicked(FOnClicked::CreateSP(this, &STonConnectPanel::HandleWalletClicked, i))
            [
                SNew(SBorder)
                .BorderImage(&RowSelectedBrush) // tinted via BorderBackgroundColor
                .BorderBackgroundColor(TAttribute<FSlateColor>::CreateSP(this, &STonConnectPanel::WalletRowColor, i))
                .Padding(FMargin(8, 6))
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 10, 0)
                    [
                        SNew(SBox).WidthOverride(28.f).HeightOverride(28.f)
                        [
                            SNew(SImage).Image(W->Icon.IsValid() ? W->Icon.Get() : FStyleDefaults::GetNoBrush())
                        ]
                    ]
                    + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
                    [
                        SNew(STextBlock)
                        .Text(FText::FromString(W->DisplayName))
                        .ColorAndOpacity(FSlateColor(White))
                        .Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))
                    ]
                ]
            ]
        ];
    }
}

FReply STonConnectPanel::HandleWalletClicked(int32 Index)
{
    if (Wallets.IsValidIndex(Index)) SelectedWalletIndex = Index;
    return FReply::Handled();
}

FSlateColor STonConnectPanel::WalletRowColor(int32 Index) const
{
    // BorderBackgroundColor tints RowSelectedBrush; transparent when not selected.
    return (Index == SelectedWalletIndex)
        ? FSlateColor(FLinearColor::White)               // full tint → visible highlight
        : FSlateColor(FLinearColor(1, 1, 1, 0));         // transparent
}

bool STonConnectPanel::IsConnectEnabled() const
{
    return Wallets.IsValidIndex(SelectedWalletIndex);
}

EVisibility STonConnectPanel::GetWalletLoadingVisibility() const
{
    return Wallets.Num() == 0 ? EVisibility::Visible : EVisibility::Collapsed;
}

int32 STonConnectPanel::GetActivePage() const
{
    if (!Subsystem.IsValid()) return 0;
    switch (Subsystem->GetState())
    {
    case ETonConnectState::Connected:     return 2;
    case ETonConnectState::Connecting:    return 1;
    case ETonConnectState::Disconnecting: return 2;
    default:                              return 0;
    }
}

FText STonConnectPanel::GetAddressText() const
{
    if (!Subsystem.IsValid()) return FText::GetEmpty();
    return FText::FromString(
        UTonBlueprintLibrary::TruncateAddress(Subsystem->GetConnectedWallet().Address, 8, 6));
}

FText STonConnectPanel::GetBalanceText() const
{
    if (!Subsystem.IsValid()) return FText::GetEmpty();
    return FText::FromString(UTonBlueprintLibrary::FormatTon(Subsystem->GetCachedBalance(), 4));
}

FText STonConnectPanel::GetVersionText() const
{
    if (!Subsystem.IsValid()) return FText::GetEmpty();
    const FTonWalletInfo& W = Subsystem->GetConnectedWallet();

    // "Tonkeeper 5.0.1" — name + app version (network is shown by the header badge)
    FString Line = W.WalletName;
    if (!W.AppVersion.IsEmpty())
        Line += FString::Printf(TEXT(" %s"), *W.AppVersion);

    // wallet contract version (from TonAPI interfaces), if known
    const FString Ver = W.WalletVersion.IsEmpty() ? TEXT("?") : W.WalletVersion;
    Line += FString::Printf(TEXT("  ·  %s"), *Ver);
    return FText::FromString(Line);
}

FText STonConnectPanel::GetWalletMetaText() const
{
    if (!Subsystem.IsValid()) return FText::GetEmpty();
    const FTonWalletInfo& W = Subsystem->GetConnectedWallet();

    TArray<FString> Parts;
    if (!W.Platform.IsEmpty()) Parts.Add(W.Platform);
    Parts.Add(FString::Printf(TEXT("up to %d msg%s/tx"),
        W.MaxMessages, W.MaxMessages == 1 ? TEXT("") : TEXT("s")));
    if (W.bSupportsSignData) Parts.Add(TEXT("SignData ✓"));
    return FText::FromString(FString::Join(Parts, TEXT("  ·  ")));
}

FText STonConnectPanel::GetNetworkText() const
{
    if (!Subsystem.IsValid()) return FText::GetEmpty();
    return FText::FromString(Subsystem->GetConnectedWallet().Network);
}

FReply STonConnectPanel::HandleConnect()
{
    LastError.Reset();
    OnConnectClicked.ExecuteIfBound();
    return FReply::Handled();
}

FReply STonConnectPanel::HandleDisconnect()
{
    OnDisconnectClicked.ExecuteIfBound();
    return FReply::Handled();
}

FReply STonConnectPanel::HandleClose()
{
    OnCloseClicked.ExecuteIfBound();
    return FReply::Handled();
}

// ── Network badge (authoritative only after connect) ──────────────────────────

EVisibility STonConnectPanel::GetNetworkBadgeVisibility() const
{
    // Network is unknown until the wallet reports it — hide the badge before then.
    return (Subsystem.IsValid() && Subsystem->GetState() == ETonConnectState::Connected)
        ? EVisibility::Visible : EVisibility::Collapsed;
}

FText STonConnectPanel::GetNetworkBadgeText() const
{
    if (!Subsystem.IsValid()) return FText::GetEmpty();
    const FString Net = Subsystem->GetConnectedWallet().Network;
    return FText::FromString(FString::Printf(TEXT("● %s"),
        Net.IsEmpty() ? TEXT("?") : *Net.ToUpper()));
}

FSlateColor STonConnectPanel::GetNetworkBadgeColor() const
{
    const FString Net = Subsystem.IsValid()
        ? Subsystem->GetConnectedWallet().Network.ToLower() : FString();
    if (Net == TEXT("testnet")) return FSlateColor(FLinearColor(1.0f, 0.72f, 0.30f));
    if (Net == TEXT("mainnet")) return FSlateColor(FLinearColor(0.30f, 0.90f, 0.55f));
    return FSlateColor(FLinearColor(0.70f, 0.75f, 0.85f));
}

FReply STonConnectPanel::HandleSend()
{
    if (bSending) return FReply::Handled();

    const FString Addr   = AddressBox.IsValid() ? AddressBox->GetText().ToString().TrimStartAndEnd() : FString();
    const FString Amount = AmountBox.IsValid()  ? AmountBox->GetText().ToString().TrimStartAndEnd()  : TEXT("0.01");

    OnSendRequested.ExecuteIfBound(Addr, Amount, TEXT("TonConnect UI demo"));
    return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
