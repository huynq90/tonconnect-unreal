#include "TonConnectSubsystem.h"
#include "TonWalletSave.h"
#include "GameFramework/PlayerController.h"
#include "Engine/GameInstance.h"
#include "ISessionStore.h"
#include "TonUtils.h"
#include "Cells/TonCell.h"
#include "Cells/TonBoc.h"
#include "Cells/TonWalletMessage.h"
#include "Contract/TonCellBuilder.h"
#include "TonConnectDeveloperSettings.h"
#include "Transport/TonHttpBridge.h"
#include "Transport/TonMockBridge.h"
#include "Api/TonApiClient.h"
#include "Api/TonMockApiClient.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/Base64.h"
#include "Engine/Texture2D.h"
#include "Kismet/GameplayStatics.h"
#include "TonBlueprintLibrary.h"

static const FString WalletCacheSlot = TEXT("TonWalletCache");

// ---- QR texture generation using bundled qrcodegen ----

#pragma pack(push, 2)
THIRD_PARTY_INCLUDES_START
#pragma push_macro("check")
#undef check
#include "qrcodegen.hpp"
#pragma pop_macro("check")
THIRD_PARTY_INCLUDES_END

static UTexture2D* MakeQRTexture(const FString& Url)
{
    using qrcodegen::QrCode;

    std::string Utf8(TCHAR_TO_UTF8(*Url));
    QrCode Qr = QrCode::encodeText(Utf8.c_str(), QrCode::Ecc::MEDIUM);

    const int32 Size = Qr.getSize();
    const int32 Scale = 8;
    const int32 TexSize = Size * Scale;
    const int32 DataBytes = TexSize * TexSize * 4;

    TArray<uint8> Pixels;
    Pixels.SetNum(DataBytes);
    FMemory::Memset(Pixels.GetData(), 0xFF, DataBytes); // white background

    for (int32 Y = 0; Y < Size; ++Y)
    {
        for (int32 X = 0; X < Size; ++X)
        {
            if (!Qr.getModule(X, Y)) continue;
            for (int32 SY = 0; SY < Scale; ++SY)
            {
                for (int32 SX = 0; SX < Scale; ++SX)
                {
                    int32 PX = X * Scale + SX;
                    int32 PY = Y * Scale + SY;
                    int32 Idx = (PY * TexSize + PX) * 4;
                    Pixels[Idx + 0] = 0x00; // B
                    Pixels[Idx + 1] = 0x00; // G
                    Pixels[Idx + 2] = 0x00; // R
                    Pixels[Idx + 3] = 0xFF; // A
                }
            }
        }
    }

    UTexture2D* Tex = UTexture2D::CreateTransient(TexSize, TexSize);
    if (!Tex) return nullptr;

    FTexture2DMipMap& Mip = Tex->GetPlatformData()->Mips[0];
    void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
    FMemory::Memcpy(Data, Pixels.GetData(), DataBytes);
    Mip.BulkData.Unlock();
    Tex->UpdateResource();
    return Tex;
}
#pragma pack(pop)

// ---- UTonConnectSubsystem ----

void UTonConnectSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    bIsMock = UTonConnectDeveloperSettings::ShouldUseMock();
    SessionStore = ISessionStore::Create();
    CreateTransportAndApi();
    LoadSession();
    LoadWalletCache();
    UE_LOG(LogTemp, Log, TEXT("TonConnectSubsystem initialized (mock=%s)"),
           bIsMock ? TEXT("true") : TEXT("false"));
}

void UTonConnectSubsystem::Deinitialize()
{
    // Cancel TTL timers for pending send results
    UWorld* DeinitWorld = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
    for (auto& Pair : PendingTtlTimers)
    {
        if (DeinitWorld) DeinitWorld->GetTimerManager().ClearTimer(Pair.Value);
    }
    PendingTtlTimers.Empty();
    PendingResults.Empty();

    // Cancel all active transaction polls before tearing down
    TArray<FString> Keys;
    ActiveTxPolls.GetKeys(Keys);
    for (const FString& K : Keys) CancelTxPoll(K);

    if (Bridge.IsValid()) Bridge->Disconnect();
    Bridge.Reset();
    ApiClient.Reset();
    Super::Deinitialize();
}

void UTonConnectSubsystem::CreateTransportAndApi()
{
    UWorld* World = GetGameInstance()->GetWorld();

    if (bIsMock)
    {
        TSharedPtr<FTonMockBridge> Mock = MakeShared<FTonMockBridge>();
        Mock->GameWorld = World;
        Bridge = Mock;
        ApiClient = MakeShared<FTonMockApiClient>();
    }
    else
    {
        TSharedPtr<FTonHttpBridge> Http = MakeShared<FTonHttpBridge>();
        Http->GameWorld = World;
        Bridge = Http;
        ApiClient = MakeShared<FTonApiClient>();
    }

    Bridge->OnEvent.AddLambda([this](const FString& EventId, const FString& RawJson)
    {
        OnBridgeEvent(EventId, RawJson);
    });
    Bridge->OnError.AddLambda([this](const FString& Reason)
    {
        OnBridgeError(Reason);
    });
}

void UTonConnectSubsystem::SelectWallet(const FString& AppName, const FString& UniversalUrl, const FString& BridgeUrl)
{
    SelectedWalletAppName     = AppName;
    SelectedWalletUniversalUrl = UniversalUrl;
    SelectedWalletBridgeUrl    = BridgeUrl;
    UE_LOG(LogTemp, Log, TEXT("TonConnect: selected wallet '%s' (bridge=%s)"), *AppName, *BridgeUrl);
}

void UTonConnectSubsystem::FetchWalletList(TFunction<void(bool, TArray<FTonWalletListEntry>)> Callback)
{
    // Real, non-mock mode: ApiClient already hits the live registry.
    if (!bIsMock)
    {
        if (!ApiClient.IsValid()) { if (Callback) Callback(false, {}); return; }
        ApiClient->GetWalletList(MoveTemp(Callback));
        return;
    }

    // Mock mode: the wallet registry is public metadata, so fetch the REAL list (disk-cached)
    // to show every wallet + icons. Fall back to the mock fixtures only if offline with no cache.
    if (!WalletListApi.IsValid())
        WalletListApi = MakeShared<FTonApiClient>();

    TSharedPtr<ITonApiClient> MockFallback = ApiClient;
    WalletListApi->GetWalletList([Callback, MockFallback](bool bOk, TArray<FTonWalletListEntry> Wallets)
    {
        if (bOk && Wallets.Num() > 0)
        {
            if (Callback) Callback(true, Wallets);
            return;
        }
        if (MockFallback.IsValid()) { MockFallback->GetWalletList(Callback); return; }
        if (Callback) Callback(false, {});
    });
}

void UTonConnectSubsystem::EstimateFeeEmulated(const FString& ToAddress, const FString& AmountNanoTon,
                                               ETonTxKind Kind, const FOnTonFeeEstimateDelegate& OnResult)
{
    UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
    if (!World)
    {
        // No world → just hand back the quick estimate immediately.
        FTonFeeEstimate E;
        E.TotalFeeNano = UTonBlueprintLibrary::EstimateFeeQuick(Kind);
        OnResult.ExecuteIfBound(E);
        return;
    }

    const int32 ReqId = ++FeeReqSerial;
    World->GetTimerManager().ClearTimer(FeeDebounceTimer);

    TWeakObjectPtr<UTonConnectSubsystem> WeakThis = this;
    FString To = ToAddress, Amt = AmountNanoTon;
    ETonTxKind K = Kind;
    FOnTonFeeEstimateDelegate Cb = OnResult;
    World->GetTimerManager().SetTimer(FeeDebounceTimer,
        [WeakThis, ReqId, To, Amt, K, Cb]()
        {
            if (UTonConnectSubsystem* S = WeakThis.Get())
                S->RunEmulatedEstimate(ReqId, To, Amt, K, Cb);
        }, 0.6f, false);
}

void UTonConnectSubsystem::RunEmulatedEstimate(int32 ReqId, const FString& ToAddress,
                                               const FString& AmountNanoTon, ETonTxKind Kind,
                                               FOnTonFeeEstimateDelegate Cb)
{
    if (FeeReqSerial != ReqId) return; // superseded

    int32 DestWc = 0;
    TArray<uint8> DestHash;
    if (!FTonAddressUtils::ParseHumanAddress(ToAddress, DestWc, DestHash))
    {
        FTonFeeEstimate E;
        E.TotalFeeNano = UTonBlueprintLibrary::EstimateFeeQuick(Kind);
        E.ErrorMessage = TEXT("bad address");
        Cb.ExecuteIfBound(E);
        return;
    }

    const uint64 Amount = (uint64)UTonBlueprintLibrary::NanoTonToInt64(AmountNanoTon);
    TSharedPtr<FTonCell> Int = FTonWalletMessage::BuildInternalMessage(
        DestWc, DestHash, Amount, /*bBounce=*/true, /*Body=*/nullptr);
    EmulateInternalMessage(ReqId, Kind, Int, Cb);
}

void UTonConnectSubsystem::EmulateInternalMessage(int32 ReqId, ETonTxKind Kind,
                                                  TSharedPtr<FTonCell> InternalMsg,
                                                  FOnTonFeeEstimateDelegate Cb)
{
    // Quick estimate, flagged as not emulated.
    auto FallBack = [Cb, Kind](const FString& Why)
    {
        FTonFeeEstimate E;
        E.bSuccess     = false;
        E.bEmulated    = false;
        E.ErrorMessage = Why;
        E.TotalFeeNano = UTonBlueprintLibrary::EstimateFeeQuick(Kind);
        Cb.ExecuteIfBound(E);
    };

    if (FeeReqSerial != ReqId) return; // superseded
    if (State != ETonConnectState::Connected || !ApiClient.IsValid()) { FallBack(TEXT("not connected")); return; }
    if (!InternalMsg.IsValid()) { FallBack(TEXT("message build failed")); return; }

    const FString Ver = ConnectedWallet.WalletVersion;
    const bool bSupported = Ver.StartsWith(TEXT("V4")) || Ver.StartsWith(TEXT("W5")) || Ver.StartsWith(TEXT("V5"));
    if (!bSupported) { FallBack(FString::Printf(TEXT("emulation unsupported for %s"), *Ver)); return; }

    int32 WalletWc = 0;
    TArray<uint8> WalletHash;
    if (!FTonAddressUtils::ParseHumanAddress(ConnectedWallet.Address, WalletWc, WalletHash))
    { FallBack(TEXT("bad wallet address")); return; }

    const bool bTestnet = ConnectedWallet.Network == TEXT("testnet");

    TWeakObjectPtr<UTonConnectSubsystem> WeakThis = this;
    ApiClient->GetSeqno(ConnectedWallet.Address,
        [WeakThis, ReqId, Kind, InternalMsg, WalletWc, WalletHash, bTestnet, Ver, Cb, FallBack]
        (bool bOk, int32 Seqno)
        {
            UTonConnectSubsystem* S = WeakThis.Get();
            if (!S || S->FeeReqSerial != ReqId) return;
            if (!bOk) { FallBack(TEXT("seqno fetch failed")); return; }

            const int64 ValidUntil = FDateTime::UtcNow().ToUnixTimestamp() + 60;
            const FString Boc = FTonWalletMessage::WrapExternalForEmulate(
                Ver, bTestnet, WalletWc, WalletHash, Seqno, ValidUntil, InternalMsg);
            if (Boc.IsEmpty()) { FallBack(TEXT("message wrap failed")); return; }

            S->ApiClient->EmulateMessage(Boc,
                [WeakThis, ReqId, Cb, FallBack](bool bOk2, int64 FeeNano)
                {
                    UTonConnectSubsystem* S2 = WeakThis.Get();
                    if (!S2 || S2->FeeReqSerial != ReqId) return;
                    if (!bOk2) { FallBack(TEXT("emulate failed")); return; }

                    FTonFeeEstimate E;
                    E.bSuccess     = true;
                    E.bEmulated    = true;
                    E.TotalFeeNano = FString::Printf(TEXT("%lld"), FeeNano);
                    Cb.ExecuteIfBound(E);
                });
        });
}

void UTonConnectSubsystem::EstimateFeeEmulatedJetton(const FString& JettonWalletAddr, const FString& DestAddr,
                                                     const FString& AmountBaseUnits, const FOnTonFeeEstimateDelegate& OnResult)
{
    UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
    int32 JwWc = 0, DestWc = 0, RespWc = 0;
    TArray<uint8> JwHash, DestHash, RespHash;
    if (!World ||
        !FTonAddressUtils::ParseHumanAddress(JettonWalletAddr, JwWc, JwHash) ||
        !FTonAddressUtils::ParseHumanAddress(DestAddr, DestWc, DestHash) ||
        !FTonAddressUtils::ParseHumanAddress(ConnectedWallet.Address, RespWc, RespHash))
    {
        FTonFeeEstimate E;
        E.TotalFeeNano = UTonBlueprintLibrary::EstimateFeeQuick(ETonTxKind::JettonTransfer);
        OnResult.ExecuteIfBound(E);
        return;
    }

    const uint64 Amount = (uint64)UTonBlueprintLibrary::NanoTonToInt64(AmountBaseUnits);
    TSharedPtr<FTonCell> Body = FTonCell::JettonTransfer(DestWc, DestHash, RespWc, RespHash, Amount, 1);
    // Jetton transfers attach ~0.05 TON for gas; the internal msg goes to the jetton wallet.
    TSharedPtr<FTonCell> Int = FTonWalletMessage::BuildInternalMessage(JwWc, JwHash, 50000000ull, true, Body);

    const int32 ReqId = ++FeeReqSerial;
    World->GetTimerManager().ClearTimer(FeeDebounceTimer);
    TWeakObjectPtr<UTonConnectSubsystem> WeakThis = this;
    FOnTonFeeEstimateDelegate Cb = OnResult;
    World->GetTimerManager().SetTimer(FeeDebounceTimer,
        [WeakThis, ReqId, Int, Cb]()
        {
            if (UTonConnectSubsystem* S = WeakThis.Get())
                S->EmulateInternalMessage(ReqId, ETonTxKind::JettonTransfer, Int, Cb);
        }, 0.6f, false);
}

void UTonConnectSubsystem::EstimateFeeEmulatedNft(const FString& NftAddress, const FString& NewOwnerAddr,
                                                  const FOnTonFeeEstimateDelegate& OnResult)
{
    UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
    int32 NftWc = 0, OwnerWc = 0, RespWc = 0;
    TArray<uint8> NftHash, OwnerHash, RespHash;
    if (!World ||
        !FTonAddressUtils::ParseHumanAddress(NftAddress, NftWc, NftHash) ||
        !FTonAddressUtils::ParseHumanAddress(NewOwnerAddr, OwnerWc, OwnerHash) ||
        !FTonAddressUtils::ParseHumanAddress(ConnectedWallet.Address, RespWc, RespHash))
    {
        FTonFeeEstimate E;
        E.TotalFeeNano = UTonBlueprintLibrary::EstimateFeeQuick(ETonTxKind::NftTransfer);
        OnResult.ExecuteIfBound(E);
        return;
    }

    TSharedPtr<FTonCell> Body = FTonCell::NftTransfer(OwnerWc, OwnerHash, RespWc, RespHash, 0);
    TSharedPtr<FTonCell> Int = FTonWalletMessage::BuildInternalMessage(NftWc, NftHash, 50000000ull, true, Body);

    const int32 ReqId = ++FeeReqSerial;
    World->GetTimerManager().ClearTimer(FeeDebounceTimer);
    TWeakObjectPtr<UTonConnectSubsystem> WeakThis = this;
    FOnTonFeeEstimateDelegate Cb = OnResult;
    World->GetTimerManager().SetTimer(FeeDebounceTimer,
        [WeakThis, ReqId, Int, Cb]()
        {
            if (UTonConnectSubsystem* S = WeakThis.Get())
                S->EmulateInternalMessage(ReqId, ETonTxKind::NftTransfer, Int, Cb);
        }, 0.6f, false);
}

void UTonConnectSubsystem::Connect()
{
    if (State != ETonConnectState::Disconnected)
    {
        UE_LOG(LogTemp, Warning, TEXT("TonConnect::Connect called while state=%d"), (int32)State);
        return;
    }

    SetState(ETonConnectState::Connecting);
    Session.GenerateKeyPair();
    WalletBridgePublicKey.Reset();

    // Fast path: a wallet was picked via SelectWallet() — connect straight to its bridge.
    if (!SelectedWalletBridgeUrl.IsEmpty())
    {
        ActiveWalletAppName = SelectedWalletAppName;
        BeginConnect(SelectedWalletUniversalUrl, SelectedWalletBridgeUrl);
        return;
    }

    // Default path: no selection — resolve Tonkeeper from the wallet list.
    TWeakObjectPtr<UTonConnectSubsystem> WeakThis = this;
    ApiClient->GetWalletList([WeakThis](bool bSuccess, TArray<FTonWalletListEntry> Wallets)
    {
        UTonConnectSubsystem* Self = WeakThis.Get();
        if (!Self || Self->State != ETonConnectState::Connecting) return;

        FString WalletUrl = TEXT("https://app.tonkeeper.com/ton-connect");
        FString WalletBridgeUrl;
        for (const auto& W : Wallets)
        {
            if (W.AppName == TEXT("tonkeeper"))
            {
                if (W.UniversalUrls.Num() > 0) WalletUrl = W.UniversalUrls[0];
                WalletBridgeUrl = W.BridgeUrl;
                break;
            }
        }
        Self->ActiveWalletAppName = TEXT("tonkeeper");
        Self->BeginConnect(WalletUrl, WalletBridgeUrl);
    });
}

void UTonConnectSubsystem::BeginConnect(const FString& WalletUrl, const FString& BridgeUrl)
{
    if (State != ETonConnectState::Connecting) return;

    FString WalletBridgeUrl = BridgeUrl;
    if (WalletBridgeUrl.IsEmpty())
    {
        const UTonConnectDeveloperSettings* Cfg = GetDefault<UTonConnectDeveloperSettings>();
        WalletBridgeUrl = Cfg ? Cfg->GetBridgeUrl() : TEXT("https://bridge.tonapi.io/bridge");
    }
    ActiveBridgeUrl = WalletBridgeUrl;

    const FString ClientId = FTonByteUtils::BytesToHex(Session.PublicKey);
    const FString DeepLink = BuildConnectLink(WalletUrl, ClientId);

    UE_LOG(LogTemp, Log, TEXT("TonConnect deep link: %s"), *DeepLink);
    UE_LOG(LogTemp, Log, TEXT("TonConnect bridge: %s (wallet=%s)"), *WalletBridgeUrl, *ActiveWalletAppName);

    ConnectDeepLink = DeepLink;
    QRTexture = GenerateQRTexture(DeepLink);
    // QR + deep-link are delivered via OnQRReady — the app renders them itself.
    OnQRReady.Broadcast(QRTexture, DeepLink);

    Bridge->Connect(WalletBridgeUrl, ClientId);

    StartConnectTimeout();
}

void UTonConnectSubsystem::StartConnectTimeout()
{
    ClearConnectTimeout();
    UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
    if (!World) return;

    const UTonConnectDeveloperSettings* Cfg = GetDefault<UTonConnectDeveloperSettings>();
    const float Secs = Cfg ? Cfg->ConnectTimeoutSeconds : 180.0f;
    if (Secs <= 0.0f) return; // disabled — wait forever

    FTimerDelegate D;
    D.BindUObject(this, &UTonConnectSubsystem::OnConnectTimeout);
    World->GetTimerManager().SetTimer(ConnectTimeoutTimer, D, Secs, false);
}

void UTonConnectSubsystem::ClearConnectTimeout()
{
    if (UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr)
        World->GetTimerManager().ClearTimer(ConnectTimeoutTimer);
}

void UTonConnectSubsystem::OnConnectTimeout()
{
    if (State != ETonConnectState::Connecting) return;

    UE_LOG(LogTemp, Warning, TEXT("TonConnect: connect timed out — wallet/bridge did not respond"));
    if (Bridge.IsValid()) Bridge->Disconnect();
    ConnectDeepLink.Reset();
    SetState(ETonConnectState::Disconnected);
    OnError.Broadcast(TEXT("Connect timed out — couldn't reach the wallet or bridge. Please try again."));
}

void UTonConnectSubsystem::RestoreSession()
{
    if (State != ETonConnectState::Disconnected) return;

    if (!Session.IsValid())
    {
        OnError.Broadcast(TEXT("No saved session to restore"));
        return;
    }

    SetState(ETonConnectState::Connecting);
    FString ClientId = FTonByteUtils::BytesToHex(Session.PublicKey);

    // Use the bridge URL from the last successful Connect(), fall back to configured default
    FString BridgeUrl = ActiveBridgeUrl;
    if (BridgeUrl.IsEmpty())
    {
        const UTonConnectDeveloperSettings* Cfg = GetDefault<UTonConnectDeveloperSettings>();
        BridgeUrl = Cfg ? Cfg->GetBridgeUrl() : TEXT("https://bridge.tonapi.io/bridge");
    }
    Bridge->Connect(BridgeUrl, ClientId);
}

void UTonConnectSubsystem::Disconnect()
{
    if (State == ETonConnectState::Disconnected) return;

    SetState(ETonConnectState::Disconnecting);
    Bridge->Disconnect();
    ConnectedWallet = FTonWalletInfo();
    WalletBridgePublicKey.Reset();

    // Cancel all TTL timers and fail pending sends
    UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
    TArray<int32> PendingIds;
    PendingResults.GetKeys(PendingIds);
    for (int32 Id : PendingIds)
    {
        if (W && PendingTtlTimers.Contains(Id))
            W->GetTimerManager().ClearTimer(PendingTtlTimers[Id]);
    }
    PendingTtlTimers.Empty();
    PendingResults.Empty();

    if (SessionStore) SessionStore->Clear();
    UGameplayStatics::DeleteGameInSlot(WalletCacheSlot, 0);
    CachedBalanceNano.Empty();
    CachedJettons.Empty();
    CachedNftCount = 0;
    ConnectDeepLink.Reset();
    Session = FTonSession();
    SetState(ETonConnectState::Disconnected);
    OnDisconnected.Broadcast();
}

void UTonConnectSubsystem::SendTon(const FString& ToAddress, const FString& AmountNanoTon,
                                    const FString& Comment, const FOnTonSendResultDelegate& OnResult)
{
    FString PayloadBoc;
    if (!Comment.IsEmpty())
        PayloadBoc = FTonCell::TextComment(Comment)->ToBocBase64();

    SendTransfer(ToAddress, AmountNanoTon, PayloadBoc, OnResult);
}

void UTonConnectSubsystem::SendJettonTransfer(const FString& JettonWalletAddr,
                                               const FString& DestAddr,
                                               const FString& AmountBaseUnits,
                                               const FString& Comment,
                                               const FOnTonSendResultDelegate& OnResult)
{
    FTonSendResult FailResult;
    FailResult.Result = ETonSendResult::Error;

    int32 DestWorkchain = 0, RespWorkchain = 0;
    TArray<uint8> DestHash, RespHash;

    if (!FTonAddressUtils::ParseHumanAddress(DestAddr, DestWorkchain, DestHash) ||
        !FTonAddressUtils::ParseHumanAddress(ConnectedWallet.Address, RespWorkchain, RespHash))
    {
        FailResult.ErrorMessage = TEXT("Invalid address for jetton transfer");
        OnResult.ExecuteIfBound(FailResult);
        return;
    }

    uint64 Amount = FTonAmountUtils::NanoStringToUint64(AmountBaseUnits);

    // Build TEP-74 transfer body manually to support optional forward_payload comment
    auto Cell = MakeShared<FTonCell>();
    Cell->WriteUint(0xf8a7ea5ULL, 32);
    Cell->WriteUint(0, 64);
    Cell->WriteVarUint(Amount, 4);
    Cell->WriteAddress(DestWorkchain, DestHash);
    Cell->WriteAddress(RespWorkchain, RespHash);
    Cell->WriteBit(false);   // no custom_payload

    const uint64 FwdTon = Comment.IsEmpty() ? 1ULL : 100000000ULL;  // 0.1 TON if comment
    Cell->WriteVarUint(FwdTon, 4);

    if (Comment.IsEmpty())
    {
        Cell->WriteBit(false);  // no forward_payload
    }
    else
    {
        Cell->WriteBit(true);   // forward_payload is a ref
        Cell->AddRef(FTonCell::TextComment(Comment));
    }

    // 0.05 TON gas for jetton wallet execution; bump to 0.15 TON when forwarding a comment
    const FString GasNano = Comment.IsEmpty() ? TEXT("50000000") : TEXT("150000000");
    SendTransfer(JettonWalletAddr, GasNano, Cell->ToBocBase64(), OnResult);
}

void UTonConnectSubsystem::SendNftTransfer(const FString& NftAddress, const FString& ToAddress,
                                            const FOnTonSendResultDelegate& OnResult)
{
    FTonSendResult FailResult;
    FailResult.Result = ETonSendResult::Error;

    int32 DestWorkchain = 0, RespWorkchain = 0;
    TArray<uint8> DestHash, RespHash;

    if (!FTonAddressUtils::ParseHumanAddress(ToAddress, DestWorkchain, DestHash) ||
        !FTonAddressUtils::ParseHumanAddress(ConnectedWallet.Address, RespWorkchain, RespHash))
    {
        FailResult.ErrorMessage = TEXT("Invalid address for NFT transfer");
        OnResult.ExecuteIfBound(FailResult);
        return;
    }

    auto Cell = FTonCell::NftTransfer(DestWorkchain, DestHash, RespWorkchain, RespHash);
    // 0.05 TON gas
    SendTransfer(NftAddress, TEXT("50000000"), Cell->ToBocBase64(), OnResult);
}

void UTonConnectSubsystem::SendTransfer(const FString& ToAddress, const FString& AmountNanoTon,
                                         const FString& PayloadBocBase64,
                                         const FOnTonSendResultDelegate& OnResult)
{
    FTonSendResult FailResult;
    FailResult.Result = ETonSendResult::Error;

    if (State != ETonConnectState::Connected)
    {
        FailResult.ErrorMessage = TEXT("Not connected");
        OnResult.ExecuteIfBound(FailResult);
        return;
    }

    if (!bIsMock && WalletBridgePublicKey.Num() != 32)
    {
        FailResult.ErrorMessage = TEXT("Wallet bridge key not available");
        OnResult.ExecuteIfBound(FailResult);
        return;
    }

    const int32 RpcId = NextRpcId++;
    const int64 ValidUntil = FDateTime::UtcNow().ToUnixTimestamp() + 300;

    TSharedRef<FJsonObject> MsgObj = MakeShared<FJsonObject>();
    MsgObj->SetStringField(TEXT("address"), ToAddress);
    MsgObj->SetStringField(TEXT("amount"), AmountNanoTon);
    if (!PayloadBocBase64.IsEmpty())
        MsgObj->SetStringField(TEXT("payload"), PayloadBocBase64);

    TArray<TSharedPtr<FJsonValue>> Messages;
    Messages.Add(MakeShared<FJsonValueObject>(MsgObj));

    const FString NetworkId = (ConnectedWallet.Network == TEXT("testnet")) ? TEXT("-3") : TEXT("-239");

    TSharedRef<FJsonObject> TxObj = MakeShared<FJsonObject>();
    TxObj->SetNumberField(TEXT("valid_until"), (double)(int64)ValidUntil);
    TxObj->SetStringField(TEXT("network"), NetworkId);
    if (!ConnectedWallet.RawAddress.IsEmpty())
        TxObj->SetStringField(TEXT("from"), ConnectedWallet.RawAddress);
    TxObj->SetArrayField(TEXT("messages"), Messages);

    FString TxJsonStr;
    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> TxWriter =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&TxJsonStr);
    FJsonSerializer::Serialize(TxObj, TxWriter);

    TArray<TSharedPtr<FJsonValue>> Params;
    Params.Add(MakeShared<FJsonValueString>(TxJsonStr));

    // TON Connect wire format: {method, params, id} — no jsonrpc field
    TSharedRef<FJsonObject> RpcObj = MakeShared<FJsonObject>();
    RpcObj->SetStringField(TEXT("method"), TEXT("sendTransaction"));
    RpcObj->SetArrayField(TEXT("params"), Params);
    RpcObj->SetStringField(TEXT("id"), FString::FromInt(RpcId));

    FString RpcJsonStr;
    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> RpcWriter =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&RpcJsonStr);
    FJsonSerializer::Serialize(RpcObj, RpcWriter);

    UE_LOG(LogTemp, Log, TEXT("TonConnect: RPC plaintext: %s"), *RpcJsonStr);

    FString BodyBase64;
    if (!bIsMock)
    {
        FTCHARToUTF8 Conv(*RpcJsonStr);
        TArray<uint8> Plain;
        Plain.Append((const uint8*)Conv.Get(), Conv.Length());

        TArray<uint8> Encrypted;
        if (!Session.BoxEncrypt(Plain, WalletBridgePublicKey, Encrypted))
        {
            FailResult.ErrorMessage = TEXT("Encryption failed");
            OnResult.ExecuteIfBound(FailResult);
            return;
        }
        BodyBase64 = FTonByteUtils::BytesToBase64(Encrypted);
    }
    else
    {
        BodyBase64 = FBase64::Encode(RpcJsonStr);
    }

    PendingResults.Add(RpcId, OnResult);

    // Auto-expire after TTL (bridge drops the message after 300s, wallet never responds)
    UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
    if (World)
    {
        TWeakObjectPtr<UTonConnectSubsystem> WeakExpire = this;
        FTimerHandle TtlHandle;
        FTimerDelegate TtlDelegate;
        TtlDelegate.BindLambda([WeakExpire, RpcId]()
        {
            if (UTonConnectSubsystem* S = WeakExpire.Get()) S->ExpirePendingResult(RpcId);
        });
        World->GetTimerManager().SetTimer(TtlHandle, TtlDelegate, 305.f, false);
        PendingTtlTimers.Add(RpcId, TtlHandle);
    }

    TWeakObjectPtr<UTonConnectSubsystem> WeakThis = this;
    FString WalletClientId = FTonByteUtils::BytesToHex(WalletBridgePublicKey);

    UE_LOG(LogTemp, Log, TEXT("TonConnect: sending RPC id=%d to wallet key=%s…"), RpcId, *WalletClientId);

    Bridge->SendMessage(WalletClientId, TEXT("sendTransaction"), BodyBase64,
        [WeakThis, RpcId](bool bSendOk)
        {
            UE_LOG(LogTemp, Log, TEXT("TonConnect: bridge delivery rpc=%d ok=%s"), RpcId, bSendOk ? TEXT("true") : TEXT("false"));
            if (bSendOk) return;
            UTonConnectSubsystem* Self = WeakThis.Get();
            if (!Self) return;
            FTonSendResult Fail;
            Fail.Result = ETonSendResult::Error;
            Fail.ErrorMessage = TEXT("Bridge delivery failed");
            if (Self->PendingResults.Contains(RpcId))
            {
                Self->PendingResults[RpcId].ExecuteIfBound(Fail);
                Self->PendingResults.Remove(RpcId);
            }
        });
}

void UTonConnectSubsystem::CallGetMethod(const FString& Address, const FString& Method,
                                          const FOnTonGetMethodDelegate& OnResult)
{
    FTonGetMethodResult Fail;
    if (!ApiClient.IsValid())
    {
        Fail.ErrorMessage = TEXT("ApiClient not ready");
        OnResult.ExecuteIfBound(Fail);
        return;
    }

    TWeakObjectPtr<UTonConnectSubsystem> WeakThis = this;
    ApiClient->CallGetMethod(Address, Method,
        [WeakThis, OnResult](bool bOk, TMap<FString,FString> Stack)
        {
            FTonGetMethodResult R;
            R.bSuccess = bOk;
            R.Stack    = MoveTemp(Stack);
            if (!bOk) R.ErrorMessage = TEXT("get-method failed or contract not found");
            OnResult.ExecuteIfBound(R);
        });
}

void UTonConnectSubsystem::SendContractMessage(UTonMessageSpec* Spec,
                                                const TMap<FString,FString>& Values,
                                                const FString& ToAddress,
                                                const FString& AmountNanoTon,
                                                const FOnTonSendResultDelegate& OnResult)
{
    FTonSendResult Fail;
    Fail.Result = ETonSendResult::Error;

    if (!Spec)
    {
        Fail.ErrorMessage = TEXT("Null MessageSpec");
        OnResult.ExecuteIfBound(Fail);
        return;
    }

    TSharedPtr<FTonCell> Cell = FTonCellBuilder::Build(Spec, Values);
    if (!Cell)
    {
        Fail.ErrorMessage = TEXT("Failed to build cell from spec");
        OnResult.ExecuteIfBound(Fail);
        return;
    }

    SendTransfer(ToAddress, AmountNanoTon, Cell->ToBocBase64(), OnResult);
}

void UTonConnectSubsystem::WaitForTransaction(const FString& TxHash, float TimeoutSec,
                                               TFunction<void(bool, FTonTxEntry)> OnResult)
{
    UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
    if (!World || !ApiClient.IsValid() || TxHash.IsEmpty())
    {
        if (OnResult) OnResult(false, FTonTxEntry{});
        return;
    }

    CancelTxPoll(TxHash);

    FTxPoll& Poll = ActiveTxPolls.Add(TxHash);
    Poll.TimeoutSec = FMath::Max(TimeoutSec, 4.f);
    Poll.Callback   = MoveTemp(OnResult);

    TWeakObjectPtr<UTonConnectSubsystem> WeakThis = this;
    FString HashCopy = TxHash;

    FTimerDelegate Delegate;
    Delegate.BindLambda([WeakThis, HashCopy]()
    {
        if (UTonConnectSubsystem* S = WeakThis.Get()) S->TickTxPoll(HashCopy);
    });
    World->GetTimerManager().SetTimer(Poll.Timer, Delegate, 2.f, /*bLoop=*/true, 2.f);
}

void UTonConnectSubsystem::TickTxPoll(const FString& TxHash)
{
    FTxPoll* Poll = ActiveTxPolls.Find(TxHash);
    if (!Poll || !ApiClient.IsValid()) return;

    Poll->Elapsed += 2.f;

    if (Poll->Elapsed >= Poll->TimeoutSec)
    {
        auto Cb = MoveTemp(Poll->Callback);
        CancelTxPoll(TxHash);
        if (Cb) Cb(false, FTonTxEntry{});
        return;
    }

    TWeakObjectPtr<UTonConnectSubsystem> WeakThis = this;
    FString HashCopy = TxHash;

    ApiClient->GetTransaction(TxHash,
        [WeakThis, HashCopy](bool bFound, FTonTxEntry Tx)
        {
            UTonConnectSubsystem* Self = WeakThis.Get();
            if (!Self || !bFound) return;
            FTxPoll* P = Self->ActiveTxPolls.Find(HashCopy);
            if (!P) return;
            auto Cb = MoveTemp(P->Callback);
            Self->CancelTxPoll(HashCopy);
            if (Cb) Cb(true, Tx);
        });
}

void UTonConnectSubsystem::ExpirePendingResult(int32 RpcId)
{
    if (!PendingResults.Contains(RpcId)) return;

    FTonSendResult Timeout;
    Timeout.Result       = ETonSendResult::Timeout;
    Timeout.ErrorMessage = TEXT("No response from wallet within TTL (300s)");
    PendingResults[RpcId].ExecuteIfBound(Timeout);
    PendingResults.Remove(RpcId);
    PendingTtlTimers.Remove(RpcId);
    OnSendResult.Broadcast(Timeout);
}

void UTonConnectSubsystem::CancelTxPoll(const FString& TxHash)
{
    FTxPoll* Poll = ActiveTxPolls.Find(TxHash);
    if (!Poll) return;
    UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
    if (World) World->GetTimerManager().ClearTimer(Poll->Timer);
    ActiveTxPolls.Remove(TxHash);
}

void UTonConnectSubsystem::OnBridgeEvent(const FString& EventId, const FString& RawJson)
{
    UE_LOG(LogTemp, Log, TEXT("TonConnect: bridge event received id=%s len=%d"), *EventId, RawJson.Len());

    if (bIsMock)
    {
        DispatchDecryptedEvent(RawJson);
        return;
    }

    // Real bridge outer envelope: {from: hex_pubkey, message: base64_nacl_box}
    TSharedPtr<FJsonObject> Envelope;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(RawJson);
    if (!FJsonSerializer::Deserialize(Reader, Envelope) || !Envelope.IsValid()) return;

    FString FromHex = Envelope->GetStringField(TEXT("from"));
    FString MsgBase64 = Envelope->GetStringField(TEXT("message"));

    TArray<uint8> FromKey = FTonByteUtils::HexToBytes(FromHex);
    if (WalletBridgePublicKey.Num() == 0 && FromKey.Num() == 32)
    {
        WalletBridgePublicKey = FromKey;
    }

    TArray<uint8> Encrypted = FTonByteUtils::Base64ToBytes(MsgBase64);
    TArray<uint8> Decrypted;
    if (!Session.BoxDecrypt(Encrypted, FromKey, Decrypted))
    {
        UE_LOG(LogTemp, Warning, TEXT("TonConnect: bridge message decryption failed"));
        return;
    }

    FUTF8ToTCHAR Converter(reinterpret_cast<const ANSICHAR*>(Decrypted.GetData()), Decrypted.Num());
    DispatchDecryptedEvent(FString(Converter.Length(), Converter.Get()));
}

void UTonConnectSubsystem::DispatchDecryptedEvent(const FString& JsonStr)
{
    TSharedPtr<FJsonObject> Obj;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonStr);
    if (!FJsonSerializer::Deserialize(Reader, Obj) || !Obj.IsValid()) return;

    // Push events have an "event" field
    FString EventName;
    if (Obj->TryGetStringField(TEXT("event"), EventName))
    {
        if (EventName == TEXT("connect") && State == ETonConnectState::Connecting)
        {
            const TSharedPtr<FJsonObject>* PayloadPtr;
            if (Obj->TryGetObjectField(TEXT("payload"), PayloadPtr))
            {
                HandleConnectEvent(*PayloadPtr);
            }
        }
        else if (EventName == TEXT("disconnect"))
        {
            Bridge->Disconnect();
            ConnectedWallet = FTonWalletInfo();
            SetState(ETonConnectState::Disconnected);
            OnDisconnected.Broadcast();
        }
        return;
    }

    // RPC responses have an "id" field and either "result" or "error"
    FString RpcIdStr;
    if (!Obj->TryGetStringField(TEXT("id"), RpcIdStr)) return;

    const int32 RpcId = FCString::Atoi(*RpcIdStr);
    if (!PendingResults.Contains(RpcId)) return;

    FTonSendResult SendResult;
    const TSharedPtr<FJsonObject>* ErrPtr;
    FString ResultStr;

    if (Obj->TryGetObjectField(TEXT("error"), ErrPtr) && ErrPtr && (*ErrPtr).IsValid())
    {
        int32 Code = 0;
        (*ErrPtr)->TryGetNumberField(TEXT("code"), Code);
        (*ErrPtr)->TryGetStringField(TEXT("message"), SendResult.ErrorMessage);
        SendResult.Result = (Code == 408) ? ETonSendResult::Timeout : ETonSendResult::Rejected;
    }
    else if (Obj->TryGetStringField(TEXT("result"), ResultStr))
    {
        SendResult.Result = ETonSendResult::Approved;
        SendResult.TxHash = ResultStr;
    }
    else
    {
        SendResult.Result = ETonSendResult::Error;
        SendResult.ErrorMessage = TEXT("Unrecognised response");
    }

    // Clear TTL timer before executing callback (callback may Disconnect which would double-clear)
    if (UWorld* W2 = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr)
    {
        if (PendingTtlTimers.Contains(RpcId))
        {
            W2->GetTimerManager().ClearTimer(PendingTtlTimers[RpcId]);
            PendingTtlTimers.Remove(RpcId);
        }
    }
    PendingResults[RpcId].ExecuteIfBound(SendResult);
    PendingResults.Remove(RpcId);
    OnSendResult.Broadcast(SendResult); // also fire global delegate for toast/HUD listeners
}

void UTonConnectSubsystem::HandleConnectEvent(const TSharedPtr<FJsonObject>& Payload)
{
    if (!Payload.IsValid()) return;

    const TArray<TSharedPtr<FJsonValue>>* Items;
    if (!Payload->TryGetArrayField(TEXT("items"), Items)) return;

    for (const TSharedPtr<FJsonValue>& Item : *Items)
    {
        const TSharedPtr<FJsonObject>* ItemObjPtr;
        if (!Item->TryGetObject(ItemObjPtr)) continue;

        FString Name;
        (*ItemObjPtr)->TryGetStringField(TEXT("name"), Name);

        if (Name == TEXT("ton_addr"))
        {
            FString RawAddress, Network;
            (*ItemObjPtr)->TryGetStringField(TEXT("address"), RawAddress);
            (*ItemObjPtr)->TryGetStringField(TEXT("network"), Network);
            (*ItemObjPtr)->TryGetStringField(TEXT("publicKey"), ConnectedWallet.PublicKey);
            (*ItemObjPtr)->TryGetStringField(TEXT("walletStateInit"), ConnectedWallet.StateInit);

            // The wallet's network is only known here — it arrives in the `network` field
            // alongside the address (the raw "0:hex" address carries no network tag).
            // Treat the wallet's report as authoritative: don't reject on a setting mismatch,
            // just adopt whatever the wallet says. The developer-settings Network is only a
            // pre-connect hint (bridge fallback URL + which API endpoint to query *before*
            // an address exists). Auto-sync it so the balance/asset reads that fire right
            // after connect hit the matching TonAPI endpoint (mainnet vs testnet).
            const bool bTestnet = Network.Equals(TEXT("-3"));
            ConnectedWallet.RawAddress = RawAddress;
            ConnectedWallet.Address = FTonAddressUtils::ToHumanReadable(RawAddress, /*bBounceable=*/false, bTestnet);
            ConnectedWallet.Network = bTestnet ? TEXT("testnet") : TEXT("mainnet");

            if (UTonConnectDeveloperSettings* M = GetMutableDefault<UTonConnectDeveloperSettings>())
            {
                // Keep an explicit Custom config untouched; otherwise follow the wallet.
                if (M->Network != ETonNetwork::Custom)
                {
                    const ETonNetwork Detected = bTestnet ? ETonNetwork::Testnet : ETonNetwork::Mainnet;
                    if (M->Network != Detected)
                    {
                        UE_LOG(LogTemp, Log,
                            TEXT("TonConnect: wallet reported %s — switching API endpoint to match (was set to %s)"),
                            bTestnet ? TEXT("testnet") : TEXT("mainnet"),
                            M->Network == ETonNetwork::Testnet ? TEXT("testnet") : TEXT("mainnet"));
                        M->Network = Detected;
                    }
                }
            }
        }
        else if (Name == TEXT("ton_proof"))
        {
            // ton_proof is only present if the connect request asked for it.
            const TSharedPtr<FJsonObject>* ProofObj;
            if ((*ItemObjPtr)->TryGetObjectField(TEXT("proof"), ProofObj))
                (*ProofObj)->TryGetStringField(TEXT("signature"), ConnectedWallet.TonProofSignature);
        }
    }

    if (ConnectedWallet.Address.IsEmpty())
    {
        OnError.Broadcast(TEXT("Connect event missing ton_addr"));
        SetState(ETonConnectState::Disconnected);
        return;
    }

    // Device info + capabilities (the `device` object sits next to `items`)
    const TSharedPtr<FJsonObject>* DeviceObj;
    if (Payload->TryGetObjectField(TEXT("device"), DeviceObj))
    {
        (*DeviceObj)->TryGetStringField(TEXT("appName"),    ConnectedWallet.WalletName);
        (*DeviceObj)->TryGetStringField(TEXT("appVersion"), ConnectedWallet.AppVersion);
        (*DeviceObj)->TryGetStringField(TEXT("platform"),   ConnectedWallet.Platform);

        const TArray<TSharedPtr<FJsonValue>>* Features;
        if ((*DeviceObj)->TryGetArrayField(TEXT("features"), Features))
        {
            for (const TSharedPtr<FJsonValue>& F : *Features)
            {
                const TSharedPtr<FJsonObject>* FObj;
                if (!F->TryGetObject(FObj)) continue; // skip legacy string-form features

                FString FName;
                (*FObj)->TryGetStringField(TEXT("name"), FName);
                if (FName == TEXT("SendTransaction"))
                {
                    int32 MaxMsg = 0;
                    if ((*FObj)->TryGetNumberField(TEXT("maxMessages"), MaxMsg) && MaxMsg > 0)
                        ConnectedWallet.MaxMessages = MaxMsg;
                }
                else if (FName == TEXT("SignData"))
                {
                    ConnectedWallet.bSupportsSignData = true;
                }
            }
        }
    }
    if (ConnectedWallet.WalletName.IsEmpty())
        ConnectedWallet.WalletName = TEXT("Wallet");

    // Remember which wallet app we paired with (for next-session trace)
    if (!ActiveWalletAppName.IsEmpty())
        ConnectedWallet.WalletAppName = ActiveWalletAppName;

    // Detect wallet version from the stateInit code hash — works immediately and even
    // for non-deployed wallets (TonAPI's interfaces[] only exists once on-chain).
    // FetchAccountInfo may later confirm/refine this from TonAPI (only if non-empty).
    if (!ConnectedWallet.StateInit.IsEmpty())
    {
        const FString Ver = FTonBoc::WalletVersionFromStateInit(ConnectedWallet.StateInit);
        if (!Ver.IsEmpty())
            ConnectedWallet.WalletVersion = Ver;
    }

    SaveSession();
    SaveWalletCache();
    SetState(ETonConnectState::Connected);
    OnConnected.Broadcast(ConnectedWallet);

    UE_LOG(LogTemp, Log, TEXT("TonConnect: wallet connected %s (%s)"),
           *ConnectedWallet.Address, *ConnectedWallet.Network);

    // Fetch balance + wallet version + assets in background
    FetchAccountInfo();
    FetchAssets();
}

void UTonConnectSubsystem::OnBridgeError(const FString& Reason)
{
    UE_LOG(LogTemp, Warning, TEXT("TonConnect bridge: %s"), *Reason);
    if (State == ETonConnectState::Connecting)
    {
        SetState(ETonConnectState::Disconnected);
        OnError.Broadcast(Reason);
    }
}

void UTonConnectSubsystem::SetState(ETonConnectState NewState)
{
    if (State == NewState) return;
    State = NewState;

    // Stop the connect watchdog once we leave Connecting
    if (NewState != ETonConnectState::Connecting)
        ClearConnectTimeout();
}

FString UTonConnectSubsystem::BuildConnectLink(const FString& WalletUrl, const FString& ClientId) const
{
    const UTonConnectDeveloperSettings* S = GetDefault<UTonConnectDeveloperSettings>();
    FString ManifestUrl = S ? S->ManifestUrl
        : TEXT("https://mrcentimetre.github.io/ton-smart-contract-frontend/tonconnect-manifest.json");

    // Percent-encode the characters that appear in the r= JSON value
    FString RJson = FString::Printf(
        TEXT("{\"manifestUrl\":\"%s\",\"items\":[{\"name\":\"ton_addr\"}]}"),
        *ManifestUrl);

    RJson = RJson
        .Replace(TEXT("{"),  TEXT("%7B"))
        .Replace(TEXT("}"),  TEXT("%7D"))
        .Replace(TEXT("\""), TEXT("%22"))
        .Replace(TEXT(":"),  TEXT("%3A"))
        .Replace(TEXT(","),  TEXT("%2C"))
        .Replace(TEXT("["),  TEXT("%5B"))
        .Replace(TEXT("]"),  TEXT("%5D"))
        .Replace(TEXT("/"),  TEXT("%2F"));

    return WalletUrl + TEXT("?v=2&id=") + ClientId
        + TEXT("&r=") + RJson
        + TEXT("&ret=none");
}

UTexture2D* UTonConnectSubsystem::GenerateQRTexture(const FString& Url)
{
    return MakeQRTexture(Url);
}

void UTonConnectSubsystem::SaveSession() const
{
    if (!SessionStore) return;
    if (!SessionStore->Save(Session.PublicKey, Session.PrivateKey))
    {
        UE_LOG(LogTemp, Warning, TEXT("TonConnect: failed to save session keys"));
    }
}

void UTonConnectSubsystem::LoadSession()
{
    if (!SessionStore) return;
    TArray<uint8> Pub, Priv;
    if (SessionStore->Load(Pub, Priv))
    {
        Session.PublicKey  = Pub;
        Session.PrivateKey = Priv;
        UE_LOG(LogTemp, Log, TEXT("TonConnect: loaded saved session keys"));
    }
}

void UTonConnectSubsystem::FetchAccountInfo()
{
    if (!ApiClient.IsValid() || ConnectedWallet.Address.IsEmpty()) return;

    FString Addr = ConnectedWallet.Address;
    TWeakObjectPtr<UTonConnectSubsystem> WeakThis = this;
    ApiClient->GetAccountInfo(Addr, [WeakThis](bool bOk, FTonAccountInfo Info)
    {
        UTonConnectSubsystem* Self = WeakThis.Get();
        if (!Self) return;
        if (!bOk)
        {
            UE_LOG(LogTemp, Warning, TEXT("TonConnect: GetAccountInfo failed for %s"),
                   *Self->ConnectedWallet.Address);
            return;
        }
        Self->CachedBalanceNano = Info.BalanceNano;
        if (!Info.WalletVersion.IsEmpty())
            Self->ConnectedWallet.WalletVersion = Info.WalletVersion;
        Self->SaveWalletCache();
        UE_LOG(LogTemp, Log, TEXT("TonConnect: account info → version=%s balance=%s"),
               *Self->ConnectedWallet.WalletVersion, *Self->CachedBalanceNano);
        Self->OnAccountInfoUpdated.Broadcast(Self->ConnectedWallet.WalletVersion, Self->CachedBalanceNano);
    });
}

void UTonConnectSubsystem::FetchAssets()
{
    if (!ApiClient.IsValid() || ConnectedWallet.Address.IsEmpty()) return;

    const FString Addr = ConnectedWallet.Address;
    TWeakObjectPtr<UTonConnectSubsystem> WeakThis = this;
    TSharedPtr<int32> Pending = MakeShared<int32>(2);

    auto TryBroadcast = [WeakThis, Pending]()
    {
        if (--(*Pending) != 0) return;
        UTonConnectSubsystem* Self = WeakThis.Get();
        if (!Self) return;

        FString JettonInfo;
        for (const FTonJettonBalance& J : Self->CachedJettons)
        {
            if (!J.Balance.IsEmpty())
            {
                if (!JettonInfo.IsEmpty()) JettonInfo += TEXT(", ");
                JettonInfo += UTonBlueprintLibrary::FormatJetton(J.Balance, J.Decimals, J.Symbol);
            }
        }
        UE_LOG(LogTemp, Log, TEXT("TonConnect: assets → jettons=[%s] nfts=%d"), *JettonInfo, Self->CachedNftCount);
        Self->OnAssetsUpdated.Broadcast(JettonInfo, Self->CachedNftCount);
    };

    ApiClient->GetJettonBalances(Addr,
        [WeakThis, TryBroadcast](bool bOk, TArray<FTonJettonBalance> Jettons) mutable
        {
            if (UTonConnectSubsystem* S = WeakThis.Get())
                if (bOk) S->CachedJettons = MoveTemp(Jettons);
            TryBroadcast();
        });

    ApiClient->GetNfts(Addr,
        [WeakThis, TryBroadcast](bool bOk, TArray<FTonNftItem> Nfts) mutable
        {
            if (UTonConnectSubsystem* S = WeakThis.Get())
                if (bOk) S->CachedNftCount = Nfts.Num();
            TryBroadcast();
        });
}

void UTonConnectSubsystem::SaveWalletCache() const
{
    UTonWalletSave* Save = Cast<UTonWalletSave>(
        UGameplayStatics::CreateSaveGameObject(UTonWalletSave::StaticClass()));
    if (!Save) return;

    Save->Address                  = ConnectedWallet.Address;
    Save->RawAddress               = ConnectedWallet.RawAddress;
    Save->PublicKey                = ConnectedWallet.PublicKey;
    Save->WalletName               = ConnectedWallet.WalletName;
    Save->WalletAppName            = ConnectedWallet.WalletAppName;
    Save->WalletVersion            = ConnectedWallet.WalletVersion;
    Save->Network                  = ConnectedWallet.Network;
    Save->StateInit                = ConnectedWallet.StateInit;
    Save->ActiveBridgeUrl          = ActiveBridgeUrl;
    Save->WalletBridgePublicKeyHex = FTonByteUtils::BytesToHex(WalletBridgePublicKey);
    Save->BalanceNano              = CachedBalanceNano;

    UGameplayStatics::SaveGameToSlot(Save, WalletCacheSlot, 0);
}

void UTonConnectSubsystem::LoadWalletCache()
{
    if (!Session.IsValid()) return; // no session keys — nothing to reconnect with

    USaveGame* Raw = UGameplayStatics::LoadGameFromSlot(WalletCacheSlot, 0);
    UTonWalletSave* Save = Cast<UTonWalletSave>(Raw);
    if (!Save || Save->Address.IsEmpty()) return;

    ConnectedWallet.Address       = Save->Address;
    ConnectedWallet.RawAddress    = Save->RawAddress;
    ConnectedWallet.PublicKey     = Save->PublicKey;
    ConnectedWallet.WalletName    = Save->WalletName;
    ConnectedWallet.WalletAppName = Save->WalletAppName;
    ConnectedWallet.WalletVersion = Save->WalletVersion;
    ActiveWalletAppName           = Save->WalletAppName;
    ConnectedWallet.Network       = Save->Network;
    ConnectedWallet.StateInit     = Save->StateInit;
    ActiveBridgeUrl               = Save->ActiveBridgeUrl;
    WalletBridgePublicKey         = FTonByteUtils::HexToBytes(Save->WalletBridgePublicKeyHex);
    CachedBalanceNano             = Save->BalanceNano;

    // Reconnect bridge SSE to receive future events (disconnect, tx updates)
    FString ClientId = FTonByteUtils::BytesToHex(Session.PublicKey);
    Bridge->Connect(ActiveBridgeUrl, ClientId);

    SetState(ETonConnectState::Connected);
    UE_LOG(LogTemp, Log, TEXT("TonConnect: session restored for %s (%s)"),
           *ConnectedWallet.Address, *ConnectedWallet.Network);

    // Refresh balance + wallet version + assets (cache may be stale)
    FetchAccountInfo();
    FetchAssets();
}
