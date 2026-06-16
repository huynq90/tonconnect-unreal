#pragma once

#include "CoreMinimal.h"
#include "TonTypes.generated.h"

UENUM(BlueprintType)
enum class ETonConnectState : uint8
{
    Disconnected,
    Connecting,
    Connected,
    Disconnecting,
};

UENUM(BlueprintType)
enum class ETonSendResult : uint8
{
    Approved,
    Rejected,
    Timeout,
    Error,
};

// Transaction kind — drives the quick (heuristic) fee estimate.
UENUM(BlueprintType)
enum class ETonTxKind : uint8
{
    NativeTransfer  UMETA(DisplayName="TON transfer"),
    JettonTransfer  UMETA(DisplayName="Jetton transfer"),
    NftTransfer     UMETA(DisplayName="NFT transfer"),
};

// Result of a fee estimate. TotalFeeNano is the estimated network fee in nanoTON.
USTRUCT(BlueprintType)
struct TONCONNECT_API FTonFeeEstimate
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly) bool bSuccess = false;
    UPROPERTY(BlueprintReadOnly) FString TotalFeeNano;  // estimated network fee, nanoTON decimal string
    UPROPERTY(BlueprintReadOnly) bool bEmulated = false; // true = from on-chain emulation, false = heuristic
    UPROPERTY(BlueprintReadOnly) FString ErrorMessage;
};

USTRUCT(BlueprintType)
struct TONCONNECT_API FTonWalletInfo
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly) FString Address;       // friendly (non-bounceable, e.g. 0Q...)
    UPROPERTY(BlueprintReadOnly) FString RawAddress;    // raw workchain:hexhash (e.g. 0:abc...)
    UPROPERTY(BlueprintReadOnly) FString PublicKey;
    UPROPERTY(BlueprintReadOnly) FString WalletName;    // device.appName, e.g. "Tonkeeper", "MyTonWallet"
    UPROPERTY(BlueprintReadOnly) FString WalletAppName; // wallet-list app_name slug, e.g. "tonkeeper" (which app we paired with)
    UPROPERTY(BlueprintReadOnly) FString WalletVersion; // e.g. "W5 R1", "V4R2" (from TonAPI interfaces)
    UPROPERTY(BlueprintReadOnly) FString Network;       // "mainnet" | "testnet"
    UPROPERTY(BlueprintReadOnly) FString StateInit;

    // From the connect event's `device` object
    UPROPERTY(BlueprintReadOnly) FString AppVersion;    // device.appVersion, e.g. "5.0.1"
    UPROPERTY(BlueprintReadOnly) FString Platform;      // device.platform: ios | android | windows | mac | linux

    // From device.features — what the wallet can do
    UPROPERTY(BlueprintReadOnly) int32   MaxMessages = 4;        // max messages allowed in one sendTransaction
    UPROPERTY(BlueprintReadOnly) bool    bSupportsSignData = false; // SignData feature present

    // From the ton_proof item (only present if the connect request asked for it)
    UPROPERTY(BlueprintReadOnly) FString TonProofSignature;
};

USTRUCT(BlueprintType)
struct TONCONNECT_API FTonSendResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly) ETonSendResult Result = ETonSendResult::Error;
    UPROPERTY(BlueprintReadOnly) FString TxHash;
    UPROPERTY(BlueprintReadOnly) FString ErrorMessage;
};

// All amounts are nanoTON (uint64). Max TON supply ~5e9 TON = 5e18 nanoTON < uint64 max.
// Jetton amounts use FString to handle tokens with >18 decimals if needed.
USTRUCT(BlueprintType)
struct TONCONNECT_API FTonTransferParams
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite) FString ToAddress;
    UPROPERTY(BlueprintReadWrite) FString AmountNanoTon; // decimal string, e.g. "1000000000" = 1 TON
    UPROPERTY(BlueprintReadWrite) FString Comment;
};

USTRUCT(BlueprintType)
struct TONCONNECT_API FTonGetMethodResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly) bool bSuccess = false;
    // Return stack values indexed "0", "1", … as decimal strings (num type) or BOC base64
    UPROPERTY(BlueprintReadOnly) TMap<FString, FString> Stack;
    UPROPERTY(BlueprintReadOnly) FString ErrorMessage;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTonConnected, const FTonWalletInfo&, WalletInfo);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnTonDisconnected);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTonError, const FString&, ErrorMessage);
// QRTexture = QR image to display; DeepLink = the tc:// universal URL (tap-to-open on mobile,
// same data encoded in the QR). DeepLink may be empty if the wallet has no universal link.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnTonQRReady, UTexture2D*, QRTexture, const FString&, DeepLink);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTonSendResult, const FTonSendResult&, Result);
// Fired when balance + wallet version are fetched/refreshed from TonAPI
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnTonAccountInfoUpdated, const FString&, WalletVersion, const FString&, BalanceNano);
// Fired when jetton/NFT balances are loaded. JettonInfo: "10.00 USDT, 5.00 DOGS" (empty if none).
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnTonAssetsUpdated, const FString&, JettonInfo, int32, NftCount);
// Single-cast version for use as UFUNCTION callback parameters (multicast is not allowed there)
DECLARE_DYNAMIC_DELEGATE_OneParam(FOnTonSendResultDelegate, const FTonSendResult&, Result);
DECLARE_DYNAMIC_DELEGATE_OneParam(FOnTonGetMethodDelegate, const FTonGetMethodResult&, Result);
DECLARE_DYNAMIC_DELEGATE_OneParam(FOnTonFeeEstimateDelegate, const FTonFeeEstimate&, Estimate);
