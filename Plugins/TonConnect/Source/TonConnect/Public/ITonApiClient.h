#pragma once

#include "CoreMinimal.h"

struct FTonWalletListEntry
{
    FString AppName;
    FString Name;
    FString ImageUrl;
    FString AboutUrl;
    TArray<FString> UniversalUrls;
    TArray<FString> DeepLinks;
    // SSE bridge URL — from wallet list's bridge[0].url (e.g. "https://bridge.tonapi.io/bridge")
    FString BridgeUrl;
};

struct FTonJettonBalance
{
    FString JettonAddress;   // minter contract
    FString WalletAddress;   // sender's jetton wallet contract (use in SendJettonTransfer)
    FString Balance;         // raw base-unit count as decimal string
    FString Symbol;
    int32 Decimals = 9;
};

struct FTonNftItem
{
    FString Address;
    FString CollectionAddress;
    FString Name;
    int64 Index = 0;
};

struct FTonTxEntry
{
    FString Hash;
    int64 Timestamp = 0;
    FString InAmountNano;   // nanoTON received (empty = 0)
    FString OutAmountNano;  // nanoTON sent (empty = 0)
    FString Comment;
};

struct FTonAccountInfo
{
    FString BalanceNano;    // nanoTON as decimal string
    FString WalletVersion;  // e.g. "W5 R1", "V4R2", empty if unknown
};

// REST API interface — reads chain data and posts transactions.
// Backed by TonAPI v2 (tonapi.io) in production, or FMockApiClient in tests.
class TONCONNECT_API ITonApiClient
{
public:
    virtual ~ITonApiClient() = default;

    // Fetch wallet list from wallets.ton.org (or local fixture in mock)
    virtual void GetWalletList(
        TFunction<void(bool /*bSuccess*/, TArray<FTonWalletListEntry>)> Callback) = 0;

    // Get TON balance + wallet type (interfaces) in one call to /accounts/{address}
    virtual void GetAccountInfo(
        const FString& Address,
        TFunction<void(bool /*bSuccess*/, FTonAccountInfo)> Callback) = 0;

    // Get TON balance in nanoTON as a string (no float arithmetic)
    virtual void GetBalance(
        const FString& Address,
        TFunction<void(bool /*bSuccess*/, FString /*NanoTon*/)> Callback) = 0;

    // Get all jetton balances for an account
    virtual void GetJettonBalances(
        const FString& Address,
        TFunction<void(bool /*bSuccess*/, TArray<FTonJettonBalance>)> Callback) = 0;

    // Get NFT items owned by an account
    virtual void GetNfts(
        const FString& Address,
        TFunction<void(bool /*bSuccess*/, TArray<FTonNftItem>)> Callback) = 0;

    // Get recent transaction history (up to Limit entries)
    virtual void GetHistory(
        const FString& Address,
        int32 Limit,
        TFunction<void(bool /*bSuccess*/, TArray<FTonTxEntry>)> Callback) = 0;

    // Look up a single transaction by its hash (64-char hex).
    // Calls back with bFound=false if the tx does not exist yet (404) or on network error.
    virtual void GetTransaction(
        const FString& TxHash,
        TFunction<void(bool /*bFound*/, FTonTxEntry)> Callback) = 0;

    // Call a get-method on a smart contract (read-only, no gas).
    // Args are plain strings TonAPI parses into stack values (decimal or 0x-hex int,
    // address → slice, base64/hex BOC → cell/slice); empty for no-arg methods.
    // Stack values are returned as a flat map: "0" → first value, "1" → second, etc.
    // num type → decimal string. cell/slice → BOC base64. null → empty string.
    virtual void CallGetMethod(
        const FString& Address,
        const FString& Method,
        const TArray<FString>& Args,
        TFunction<void(bool /*bSuccess*/, TMap<FString,FString> /*Stack*/)> Callback) = 0;

    // Read the wallet's current seqno (get-method "seqno"). Returns 0 for a fresh wallet.
    virtual void GetSeqno(
        const FString& Address,
        TFunction<void(bool /*bSuccess*/, int32 /*Seqno*/)> Callback) = 0;

    // Emulate an external message BOC against current chain state to estimate fees.
    // FeeNano = total network fee the sending account pays (nanoTON, always >= 0).
    virtual void EmulateMessage(
        const FString& BocBase64,
        TFunction<void(bool /*bSuccess*/, int64 /*FeeNano*/)> Callback) = 0;
};
