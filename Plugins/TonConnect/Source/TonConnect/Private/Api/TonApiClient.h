#pragma once

#include "CoreMinimal.h"
#include "ITonApiClient.h"
#include "HttpModule.h"

class FTonApiClient : public ITonApiClient
{
public:
    FTonApiClient();

    virtual void GetWalletList(TFunction<void(bool, TArray<FTonWalletListEntry>)> Callback) override;
    virtual void GetAccountInfo(const FString& Address, TFunction<void(bool, FTonAccountInfo)> Callback) override;
    virtual void GetBalance(const FString& Address, TFunction<void(bool, FString)> Callback) override;
    virtual void GetJettonBalances(const FString& Address, TFunction<void(bool, TArray<FTonJettonBalance>)> Callback) override;
    virtual void GetNfts(const FString& Address, TFunction<void(bool, TArray<FTonNftItem>)> Callback) override;
    virtual void GetHistory(const FString& Address, int32 Limit, TFunction<void(bool, TArray<FTonTxEntry>)> Callback) override;
    virtual void GetTransaction(const FString& TxHash, TFunction<void(bool, FTonTxEntry)> Callback) override;
    virtual void CallGetMethod(const FString& Address, const FString& Method,
                               TFunction<void(bool, TMap<FString,FString>)> Callback) override;
    virtual void GetSeqno(const FString& Address, TFunction<void(bool, int32)> Callback) override;
    virtual void EmulateMessage(const FString& BocBase64, TFunction<void(bool, int64)> Callback) override;

private:
    FHttpModule* Http;
    static const FString WalletListUrl;
};
