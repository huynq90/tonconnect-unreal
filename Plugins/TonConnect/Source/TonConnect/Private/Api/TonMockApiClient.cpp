#include "Api/TonMockApiClient.h"
#include "Misc/DateTime.h"

void FTonMockApiClient::GetWalletList(TFunction<void(bool, TArray<FTonWalletListEntry>)> Callback)
{
    TArray<FTonWalletListEntry> Wallets;

    FTonWalletListEntry Tonkeeper;
    Tonkeeper.AppName = TEXT("tonkeeper");
    Tonkeeper.Name = TEXT("Tonkeeper");
    Tonkeeper.UniversalUrls.Add(TEXT("https://app.tonkeeper.com/ton-connect"));
    Tonkeeper.DeepLinks.Add(TEXT("tonkeeper://ton-connect"));
    Tonkeeper.BridgeUrl = TEXT("https://bridge.tonapi.io/bridge");
    Wallets.Add(Tonkeeper);

    FTonWalletListEntry MyTonWallet;
    MyTonWallet.AppName = TEXT("mytonwallet");
    MyTonWallet.Name = TEXT("MyTonWallet");
    MyTonWallet.UniversalUrls.Add(TEXT("https://connect.mytonwallet.org"));
    MyTonWallet.BridgeUrl = TEXT("https://tonconnectbridge.mytonwallet.org/bridge/");
    Wallets.Add(MyTonWallet);

    if (Callback) Callback(true, Wallets);
}

void FTonMockApiClient::GetAccountInfo(const FString& /*Address*/, TFunction<void(bool, FTonAccountInfo)> Callback)
{
    FTonAccountInfo Info;
    Info.BalanceNano   = TEXT("5000000000"); // 5 TON
    Info.WalletVersion = TEXT("W5 R1");
    if (Callback) Callback(true, Info);
}

void FTonMockApiClient::GetBalance(const FString& Address, TFunction<void(bool, FString)> Callback)
{
    if (Callback) Callback(true, TEXT("5000000000"));
}

void FTonMockApiClient::GetJettonBalances(const FString& /*Address*/,
                                           TFunction<void(bool, TArray<FTonJettonBalance>)> Callback)
{
    TArray<FTonJettonBalance> Result;
    FTonJettonBalance Usdt;
    Usdt.JettonAddress  = TEXT("EQCxE6mUtQJKFnGfaROTKOt1lZbDiiX1kCixRv7Nw2Id_sDs");
    Usdt.WalletAddress  = TEXT("EQD_mock_usdt_wallet_address_00000000000000000000");
    Usdt.Balance        = TEXT("10000000");  // 10 USDT (6 decimals)
    Usdt.Symbol         = TEXT("USDT");
    Usdt.Decimals       = 6;
    Result.Add(Usdt);
    if (Callback) Callback(true, Result);
}

void FTonMockApiClient::GetNfts(const FString& /*Address*/,
                                 TFunction<void(bool, TArray<FTonNftItem>)> Callback)
{
    TArray<FTonNftItem> Result;
    FTonNftItem Nft;
    Nft.Address           = TEXT("EQD_mock_nft_item_address_000000000000000000000000");
    Nft.CollectionAddress = TEXT("EQD_mock_collection_address_0000000000000000000000");
    Nft.Name              = TEXT("Mock NFT #1");
    Nft.Index             = 1;
    Result.Add(Nft);
    if (Callback) Callback(true, Result);
}

void FTonMockApiClient::GetTransaction(const FString& TxHash,
                                        TFunction<void(bool, FTonTxEntry)> Callback)
{
    // Mock: immediately return a confirmed fixture transaction
    FTonTxEntry Tx;
    Tx.Hash          = TxHash.IsEmpty() ? TEXT("mock_tx_hash_0000000000000000000000000000000000000000000000000000000000000000") : TxHash;
    Tx.Timestamp     = FDateTime::UtcNow().ToUnixTimestamp();
    Tx.InAmountNano  = TEXT("1000000000");
    Tx.Comment       = TEXT("confirmed");
    if (Callback) Callback(true, Tx);
}

void FTonMockApiClient::CallGetMethod(const FString& /*Address*/, const FString& /*Method*/,
                                       TFunction<void(bool, TMap<FString,FString>)> Callback)
{
    TMap<FString,FString> Stack;
    Stack.Add(TEXT("0"), TEXT("0"));
    if (Callback) Callback(true, Stack);
}

void FTonMockApiClient::GetHistory(const FString& /*Address*/, int32 /*Limit*/,
                                    TFunction<void(bool, TArray<FTonTxEntry>)> Callback)
{
    TArray<FTonTxEntry> Result;
    FTonTxEntry Tx;
    Tx.Hash          = TEXT("mock_tx_hash_000000000000000000000000000000000000000000000000000000000000");
    Tx.Timestamp     = FDateTime::UtcNow().ToUnixTimestamp() - 3600;
    Tx.InAmountNano  = TEXT("1000000000");
    Tx.Comment       = TEXT("mock transfer");
    Result.Add(Tx);
    if (Callback) Callback(true, Result);
}

void FTonMockApiClient::GetSeqno(const FString& Address, TFunction<void(bool, int32)> Callback)
{
    if (Callback) Callback(true, 1);
}

void FTonMockApiClient::EmulateMessage(const FString& BocBase64, TFunction<void(bool, int64)> Callback)
{
    // Pretend the emulator returned ~0.0061 TON in fees.
    if (Callback) Callback(true, 6100000);
}
