#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "TonTypes.h"
#include "TonSession.h"
#include "ISessionStore.h"
#include "ITonApiClient.h"
#include "Contract/TonMessageSpec.h"
#include "TonConnectSubsystem.generated.h"

class ITonBridgeTransport;
class UUserWidget;
struct FTonCell;

UCLASS()
class TONCONNECT_API UTonConnectSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    // USubsystem
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    // --- Connect flow ---

    // Start wallet connection. Fires OnQRReady when QR is ready, then OnConnected on success.
    // Uses the wallet chosen via SelectWallet(); if none was chosen, defaults to Tonkeeper.
    UFUNCTION(BlueprintCallable, Category="TonConnect")
    void Connect();

    // Choose which wallet app to pair with (its SSE bridge + universal link).
    // Call before Connect(). Pass values from an FTonWalletListEntry (see FetchWalletList).
    void SelectWallet(const FString& AppName, const FString& UniversalUrl, const FString& BridgeUrl);

    // Fetch the TON Connect wallet list (cached on disk, SSE-capable wallets have a BridgeUrl).
    // Used by UI to present a wallet picker before connecting.
    void FetchWalletList(TFunction<void(bool /*bSuccess*/, TArray<FTonWalletListEntry>)> Callback);

    // Restore a previous session from ISessionStore.
    // Fires OnConnected if session is still valid, OnError otherwise.
    UFUNCTION(BlueprintCallable, Category="TonConnect")
    void RestoreSession();

    // Disconnect wallet and clear session.
    UFUNCTION(BlueprintCallable, Category="TonConnect")
    void Disconnect();

    // --- Transactions ---

    // Send native TON. AmountNanoTon is a decimal string (e.g. "1000000000" = 1 TON).
    UFUNCTION(BlueprintCallable, Category="TonConnect")
    void SendTon(const FString& ToAddress, const FString& AmountNanoTon,
                 const FString& Comment, const FOnTonSendResultDelegate& OnResult);

    // TEP-74: send jettons.
    // JettonWalletAddr = sender's jetton wallet contract (from GetJettonBalances).
    // DestAddr         = recipient address (human-readable or raw).
    // AmountBaseUnits  = jetton amount in the token's smallest unit (decimal string).
    UFUNCTION(BlueprintCallable, Category="TonConnect")
    void SendJettonTransfer(const FString& JettonWalletAddr, const FString& DestAddr,
                            const FString& AmountBaseUnits, const FString& Comment,
                            const FOnTonSendResultDelegate& OnResult);

    // TEP-62: transfer an NFT item to a new owner.
    UFUNCTION(BlueprintCallable, Category="TonConnect")
    void SendNftTransfer(const FString& NftAddress, const FString& ToAddress,
                         const FOnTonSendResultDelegate& OnResult);

    // Estimate the network fee by emulating the transaction on-chain (TonAPI).
    // DEBOUNCED ~0.6s: rapid calls (e.g. typing an amount) coalesce into one API hit.
    // Falls back to the quick heuristic estimate on any failure (unsupported wallet,
    // network error). Currently emulates V4 wallets doing a native TON transfer.
    UFUNCTION(BlueprintCallable, Category="TonConnect|Fee")
    void EstimateFeeEmulated(const FString& ToAddress, const FString& AmountNanoTon,
                             ETonTxKind Kind, const FOnTonFeeEstimateDelegate& OnResult);

    // Emulated fee for a TEP-74 jetton transfer (debounced). JettonWalletAddr is the
    // sender's jetton wallet (from GetJettonBalances); AmountBaseUnits is the token's
    // smallest unit. Falls back to the quick jetton estimate on failure.
    UFUNCTION(BlueprintCallable, Category="TonConnect|Fee")
    void EstimateFeeEmulatedJetton(const FString& JettonWalletAddr, const FString& DestAddr,
                                   const FString& AmountBaseUnits, const FOnTonFeeEstimateDelegate& OnResult);

    // Emulated fee for a TEP-62 NFT transfer (debounced).
    UFUNCTION(BlueprintCallable, Category="TonConnect|Fee")
    void EstimateFeeEmulatedNft(const FString& NftAddress, const FString& NewOwnerAddr,
                                const FOnTonFeeEstimateDelegate& OnResult);

    // Poll the chain until TxHash appears or TimeoutSec elapses.
    // Polls every 2s with the current ApiClient. Not BlueprintCallable — use from C++.
    // TxHash is the 64-char hex hash returned in FTonSendResult::TxHash.
    void WaitForTransaction(const FString& TxHash, float TimeoutSec,
                            TFunction<void(bool bFound, FTonTxEntry)> OnResult);

    // Call a smart-contract get-method (read-only — no gas, no signing, no wallet needed).
    // Method is the get-method NAME (e.g. "get_jetton_data"). Args are plain strings TonAPI
    // parses into stack values (decimal/0x-hex int, address → slice, BOC → cell); empty = no args.
    // Stack values returned indexed "0","1",… as decimal strings or BOC base64.
    UFUNCTION(BlueprintCallable, Category="TonConnect")
    void CallGetMethod(const FString& Address, const FString& Method,
                       const TArray<FString>& Args, const FOnTonGetMethodDelegate& OnResult);

    // Build and send an arbitrary contract message from a UTonMessageSpec + value map.
    // The built cell is used as the payload BOC for a native TON transfer to ToAddress.
    // AmountNanoTon covers the gas (e.g. "50000000" = 0.05 TON).
    UFUNCTION(BlueprintCallable, Category="TonConnect")
    void SendContractMessage(UTonMessageSpec* Spec, const TMap<FString,FString>& Values,
                             const FString& ToAddress, const FString& AmountNanoTon,
                             const FOnTonSendResultDelegate& OnResult);

    // --- State ---

    UFUNCTION(BlueprintPure, Category="TonConnect")
    ETonConnectState GetState() const { return State; }

    UFUNCTION(BlueprintPure, Category="TonConnect")
    FTonWalletInfo GetConnectedWallet() const { return ConnectedWallet; }

    // Last known TON balance of the connected wallet (nanoTON string). Updated after connect and on demand.
    UFUNCTION(BlueprintPure, Category="TonConnect")
    FString GetCachedBalance() const { return CachedBalanceNano; }

    // The current connect deep-link (tc:// universal URL), also delivered by OnQRReady.
    // Use for a "tap to open wallet" button on mobile, or a "copy link" action.
    // Empty when not connecting or the chosen wallet has no universal link.
    UFUNCTION(BlueprintPure, Category="TonConnect")
    FString GetConnectDeepLink() const { return ConnectDeepLink; }

    // --- Events ---
    UPROPERTY(BlueprintAssignable, Category="TonConnect")
    FOnTonConnected OnConnected;

    UPROPERTY(BlueprintAssignable, Category="TonConnect")
    FOnTonDisconnected OnDisconnected;

    UPROPERTY(BlueprintAssignable, Category="TonConnect")
    FOnTonError OnError;

    UPROPERTY(BlueprintAssignable, Category="TonConnect")
    FOnTonQRReady OnQRReady;

    // Fired for every SendTon() result regardless of which BP initiated it.
    // Useful for global toast/notification systems that listen without knowing the caller.
    UPROPERTY(BlueprintAssignable, Category="TonConnect")
    FOnTonSendResult OnSendResult;

    // Fired after GetAccountInfo completes (after connect or session restore).
    // WalletVersion: e.g. "W5 R1". BalanceNano: nanoTON decimal string.
    UPROPERTY(BlueprintAssignable, Category="TonConnect")
    FOnTonAccountInfoUpdated OnAccountInfoUpdated;

    // Fired after jetton + NFT balances load. JettonInfo: "10.00 USDT, 5.00 DOGS".
    UPROPERTY(BlueprintAssignable, Category="TonConnect")
    FOnTonAssetsUpdated OnAssetsUpdated;

private:
    ETonConnectState State = ETonConnectState::Disconnected;
    FTonSession Session;
    FTonWalletInfo ConnectedWallet;
    FString CachedBalanceNano;
    FString ConnectDeepLink; // current tc:// universal link (set in BeginConnect, cleared on disconnect)
    TArray<FTonJettonBalance> CachedJettons;
    int32 CachedNftCount = 0;

    TSharedPtr<ITonBridgeTransport> Bridge;
    TSharedPtr<ITonApiClient> ApiClient;
    TSharedPtr<ISessionStore> SessionStore;

    // Real API client used ONLY for the public wallet registry (wallets-v2.json),
    // so the picker shows all wallets + icons even in mock mode. Lazily created.
    TSharedPtr<ITonApiClient> WalletListApi;

    // Wallet's bridge x25519 public key (from the `from` field of the first SSE event)
    TArray<uint8> WalletBridgePublicKey;

    // SSE bridge URL resolved from wallet list (e.g. "https://bridge.tonapi.io/bridge").
    // Persisted here so RestoreSession() can reconnect to the same bridge.
    FString ActiveBridgeUrl;

    // Wallet chosen via SelectWallet() (UI picker). Empty = default to Tonkeeper.
    FString SelectedWalletAppName;
    FString SelectedWalletUniversalUrl;
    FString SelectedWalletBridgeUrl;
    // App we actually paired with (saved for next-session trace).
    FString ActiveWalletAppName;

    // Auto-incremented RPC ID for sendTransaction requests
    int32 NextRpcId = 1;

    // Pending send result callbacks keyed by RPC ID
    TMap<int32, FOnTonSendResultDelegate> PendingResults;

    // TTL timers per RPC ID — fires after 5 min to clean up unanswered requests
    TMap<int32, FTimerHandle> PendingTtlTimers;
    void ExpirePendingResult(int32 RpcId);

    // Whether mock transport is active (cached at startup)
    bool bIsMock = false;

    // Holds QR texture reference to prevent premature GC
    UPROPERTY() UTexture2D*  QRTexture       = nullptr;

    void OnBridgeEvent(const FString& EventId, const FString& RawJson);
    void OnBridgeError(const FString& Reason);
    void DispatchDecryptedEvent(const FString& JsonStr);
    void HandleConnectEvent(const TSharedPtr<class FJsonObject>& Payload);

    // Common send path: build RPC envelope, encrypt (if real), dispatch via bridge.
    void SendTransfer(const FString& ToAddress, const FString& AmountNanoTon,
                      const FString& PayloadBocBase64, const FOnTonSendResultDelegate& OnResult);

    // WaitForTransaction internals
    struct FTxPoll
    {
        FTimerHandle Timer;
        float Elapsed    = 0.f;
        float TimeoutSec = 30.f;
        TFunction<void(bool, FTonTxEntry)> Callback;
    };
    TMap<FString, FTxPoll> ActiveTxPolls;
    void TickTxPoll(const FString& TxHash);
    void CancelTxPoll(const FString& TxHash);

public:
#if WITH_DEV_AUTOMATION_TESTS
    TMap<int32, FOnTonSendResultDelegate>& Test_PendingResults() { return PendingResults; }
    TSharedPtr<ITonBridgeTransport>        Test_Bridge()         { return Bridge; }
    void Test_ExpirePending(int32 Id) { ExpirePendingResult(Id); }
#endif

private:
    void SetState(ETonConnectState NewState);
    // Shared connect tail: build deep-link + QR, show overlay, open the SSE bridge.
    void BeginConnect(const FString& WalletUrl, const FString& BridgeUrl);

    // Overall "Connecting" watchdog — fires OnError if the wallet/bridge never
    // responds within UTonConnectDeveloperSettings::ConnectTimeoutSeconds.
    FTimerHandle ConnectTimeoutTimer;
    void StartConnectTimeout();
    void ClearConnectTimeout();
    void OnConnectTimeout();

    // Fee-estimate debounce — only the latest request (FeeReqSerial) is honoured.
    FTimerHandle FeeDebounceTimer;
    int32 FeeReqSerial = 0;
    void RunEmulatedEstimate(int32 ReqId, const FString& ToAddress, const FString& AmountNanoTon,
                             ETonTxKind Kind, FOnTonFeeEstimateDelegate Cb);
    // Shared tail: gate version (v4/v5), fetch seqno, wrap the internal message, emulate.
    void EmulateInternalMessage(int32 ReqId, ETonTxKind Kind,
                                TSharedPtr<FTonCell> InternalMsg, FOnTonFeeEstimateDelegate Cb);
    void CreateTransportAndApi();
    UTexture2D* GenerateQRTexture(const FString& Url);
    FString BuildConnectLink(const FString& WalletUrl, const FString& ClientId) const;
    void SaveSession() const;
    void LoadSession();
    void SaveWalletCache() const;
    void LoadWalletCache();
    void FetchAccountInfo();
    void FetchAssets();
};
