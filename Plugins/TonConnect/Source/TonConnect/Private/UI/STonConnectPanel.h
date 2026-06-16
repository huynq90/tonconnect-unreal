#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Styling/SlateBrush.h"
#include "Brushes/SlateColorBrush.h"
#include "TonTypes.h" // ETonTxKind

class UTonConnectSubsystem;
class UTexture2D;
class SEditableTextBox;
class SVerticalBox;

// View-model for one selectable wallet in the picker. The owning actor fills the
// icon brush asynchronously (downloaded + decoded); the brush pointer stays stable.
struct FTonWalletUi
{
    FString AppName;       // slug, e.g. "tonkeeper"
    FString DisplayName;   // e.g. "Tonkeeper"
    FString BridgeUrl;     // SSE bridge URL
    FString UniversalUrl;  // universal deep link
    TSharedPtr<FSlateBrush> Icon; // stable; texture set when the download finishes
};

// Self-contained "TON Wallet" popup — built entirely in C++ Slate, no Blueprint asset required.
//
// Pages are driven by the live subsystem state via a SWidgetSwitcher:
//   Disconnected → "Connect Wallet" button
//   Connecting   → QR image + spinner + Cancel
//   Connected    → wallet info + Send form + Disconnect
//
// All actions are routed back to the owner (ATonConnectUIDemoActor) through delegates
// so the owner can handle input mode, toasts and the loading flag.
class STonConnectPanel : public SCompoundWidget
{
public:
    DECLARE_DELEGATE_ThreeParams(FOnSendRequested,
        FString /*ToAddress*/, FString /*AmountTon*/, FString /*Comment*/);

    SLATE_BEGIN_ARGS(STonConnectPanel) {}
        SLATE_ARGUMENT(TWeakObjectPtr<UTonConnectSubsystem>, Subsystem)
        SLATE_EVENT(FSimpleDelegate, OnConnectClicked)
        SLATE_EVENT(FSimpleDelegate, OnDisconnectClicked)
        SLATE_EVENT(FSimpleDelegate, OnCloseClicked)
        SLATE_EVENT(FSimpleDelegate, OnRefreshClicked) // regenerate QR (disconnect + reconnect)
        SLATE_EVENT(FOnSendRequested, OnSendRequested)
        SLATE_EVENT(FSimpleDelegate, OnSendInputChanged) // amount/address edited → re-estimate fee
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

    // Called by the owner when OnQRReady fires — swaps the QR brush image.
    void SetQRTexture(UTexture2D* Texture);

    // Deep-link for the current connect (tc:// universal URL). Enables the
    // "Open in wallet" button on the QR page — tap-to-open on mobile.
    void SetDeepLink(const FString& InLink);

    // Toggles the Send button between "Send" and a throbber/disabled "Sending…".
    void SetSending(bool bInSending) { bSending = bInSending; }

    // Populate the wallet picker (disconnected page). Auto-selects "tonkeeper" if present.
    void SetWallets(const TArray<TSharedPtr<FTonWalletUi>>& InWallets);

    // The wallet the user picked, or null if none selected.
    TSharedPtr<FTonWalletUi> GetSelectedWallet() const;

    // Current Send-form inputs (for the owner to drive fee emulation).
    FString GetAmountInput() const;
    FString GetAddressInput() const;
    // Second address field — meaning depends on kind (jetton dest / NFT new owner).
    FString GetAddress2Input() const;
    // Which transaction kind the Send form is currently configured for.
    ETonTxKind GetKind() const { return SelectedKind; }

    // Owner pushes the emulated fee result here (nanoTON string, empty = pending/failed).
    void SetEmulatedFee(const FString& FeeNano, bool bOk);

    // Owner pushes a connection error here to show a persistent banner (e.g. connect
    // timeout / bridge unreachable). Pass empty to clear it.
    void SetError(const FString& Message);

private:
    // Page index for the SWidgetSwitcher, derived from subsystem state.
    int32 GetActivePage() const;

    // Live-text attribute helpers (read from the subsystem each paint).
    FText GetAddressText() const;
    FText GetBalanceText() const;
    FText GetVersionText() const;     // "Tonkeeper 5.0.1 · W5 R1"
    FText GetWalletMetaText() const;  // "android · up to 4 msgs/tx · SignData ✓"
    FText GetNetworkText() const;

    // Button handlers
    FReply HandleConnect();
    FReply HandleDisconnect();
    FReply HandleClose();
    FReply HandleSend();
    FReply HandleRefresh();                           // regenerate the QR (disconnect + reconnect)
    FReply HandleOpenWallet();                       // launch the deep-link (mobile tap-to-open)
    EVisibility GetOpenWalletVisibility() const;     // shown only when a deep-link is set

    // Fee display
    FText GetQuickFeeText() const;     // instant heuristic, always shown
    FText GetEmulatedFeeText() const;  // on-chain emulation, updated by the owner
    void  HandleSendInputChanged(const FText&);

    // ── Transaction-kind tabs (TON / Jetton / NFT) ───────────────────────────────
    // The Send form re-labels its fields and re-targets fee emulation per kind.
    FReply      HandleKindClicked(int32 KindIndex);
    FSlateColor KindTabColor(int32 KindIndex) const;       // highlight active tab
    FText       GetSendFormTitle() const;                  // "Send TON" / "Send Jetton" / "Transfer NFT"
    FText       GetAddress1Hint() const;                   // recipient / jetton-wallet / NFT item
    FText       GetAddress2Hint() const;                   // jetton dest / NFT new owner
    FText       GetAmountUnitText() const;                 // "TON" / "units"
    EVisibility GetAddress2Visibility() const;             // hidden for native
    EVisibility GetAmountRowVisibility() const;            // hidden for NFT (no amount)

    // Wallet picker
    void        RebuildWalletList();
    FReply      HandleWalletClicked(int32 Index);
    FSlateColor WalletRowColor(int32 Index) const;     // highlight selected row
    bool        IsConnectEnabled() const;              // gate Connect on a selection
    EVisibility GetWalletLoadingVisibility() const;    // "Loading wallets…" until populated

    EVisibility GetSendingVisibility() const { return bSending ? EVisibility::Visible : EVisibility::Collapsed; }
    EVisibility GetSendButtonVisibility() const { return bSending ? EVisibility::Collapsed : EVisibility::Visible; }

    // Inputs + Disconnect are locked while a send is in flight
    bool IsInteractable() const { return !bSending; }

    // ── Network badge ────────────────────────────────────────────────────────────
    // The network is only knowable once the wallet reports it in the connect event
    // (the raw "0:hex" address carries no network tag). So the badge shows nothing
    // until connected, then reflects the wallet's authoritative network.
    FText       GetNetworkBadgeText() const;
    FSlateColor GetNetworkBadgeColor() const;
    EVisibility GetNetworkBadgeVisibility() const;

    TWeakObjectPtr<UTonConnectSubsystem> Subsystem;

    FSimpleDelegate  OnConnectClicked;
    FSimpleDelegate  OnDisconnectClicked;
    FSimpleDelegate  OnCloseClicked;
    FSimpleDelegate  OnRefreshClicked;
    FOnSendRequested OnSendRequested;
    FSimpleDelegate  OnSendInputChanged;

    // Emulated fee state (owner-driven)
    FString EmulatedFeeNano;       // nanoTON; empty while pending
    bool    bEmulatedFeeOk = false;

    // Persistent error banner (owner-driven); empty = hidden
    FString     LastError;
    FText       GetErrorText() const { return FText::FromString(LastError); }
    EVisibility GetErrorVisibility() const { return LastError.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; }

    TSharedPtr<SEditableTextBox> AmountBox;
    TSharedPtr<SEditableTextBox> AddressBox;
    TSharedPtr<SEditableTextBox> Address2Box; // jetton dest / NFT new owner

    // Active transaction kind for the Send form (drives labels + fee targeting).
    ETonTxKind SelectedKind = ETonTxKind::NativeTransfer;

    // Wallet picker state
    TArray<TSharedPtr<FTonWalletUi>> Wallets;
    int32 SelectedWalletIndex = INDEX_NONE;
    TSharedPtr<SVerticalBox> WalletListBox;
    FSlateColorBrush RowSelectedBrush { FLinearColor(0.00f, 0.52f, 1.00f, 0.22f) };

    // Brushes held as members so their pointers stay valid for the SImage/SBorder.
    FSlateColorBrush PanelBgBrush   { FLinearColor(0.04f, 0.05f, 0.08f, 0.97f) };
    FSlateColorBrush HeaderBgBrush  { FLinearColor(0.00f, 0.52f, 1.00f, 1.00f) };
    FSlateColorBrush CardBgBrush    { FLinearColor(0.09f, 0.11f, 0.15f, 1.00f) };
    FSlateBrush      QRBrush;

    FString DeepLink; // tc:// universal link for the current connect (mobile tap-to-open)

    bool bSending = false;
};
