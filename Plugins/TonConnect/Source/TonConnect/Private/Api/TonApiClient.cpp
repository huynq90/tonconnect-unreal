#include "Api/TonApiClient.h"
#include "TonConnectDeveloperSettings.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/DateTime.h"
#include "Misc/Parse.h"
#include "HAL/FileManager.h"
#include "GenericPlatform/GenericPlatformHttp.h"

const FString FTonApiClient::WalletListUrl = TEXT("https://raw.githubusercontent.com/ton-connect/wallets-list/refs/heads/main/wallets-v2.json");

// On-disk cache for the wallet list (it changes rarely). Refreshed after TTL.
static FString WalletListCachePath()
{
    return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("TonConnect"), TEXT("wallets-v2.json"));
}
static const double WalletListCacheTtlHours = 24.0;

// Parse the wallets-v2.json text into entries. Returns false on malformed JSON.
static bool ParseWalletListJson(const FString& Json, TArray<FTonWalletListEntry>& OutWallets)
{
    TArray<TSharedPtr<FJsonValue>> JsonArray;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
    if (!FJsonSerializer::Deserialize(Reader, JsonArray)) return false;

    for (const TSharedPtr<FJsonValue>& Val : JsonArray)
    {
        const TSharedPtr<FJsonObject>* ObjPtr;
        if (!Val->TryGetObject(ObjPtr) || !ObjPtr) continue;
        const TSharedPtr<FJsonObject>& Obj = *ObjPtr;

        FTonWalletListEntry Entry;
        Obj->TryGetStringField(TEXT("app_name"), Entry.AppName);
        Obj->TryGetStringField(TEXT("name"), Entry.Name);
        Obj->TryGetStringField(TEXT("image"), Entry.ImageUrl);
        Obj->TryGetStringField(TEXT("about_url"), Entry.AboutUrl);

        FString SingleUrl;
        if (Obj->TryGetStringField(TEXT("universal_url"), SingleUrl))
        {
            Entry.UniversalUrls.Add(SingleUrl);
        }
        else
        {
            const TArray<TSharedPtr<FJsonValue>>* UrlArray;
            if (Obj->TryGetArrayField(TEXT("universal_url"), UrlArray))
                for (const auto& U : *UrlArray) Entry.UniversalUrls.Add(U->AsString());
        }

        const TArray<TSharedPtr<FJsonValue>>* Links;
        if (Obj->TryGetArrayField(TEXT("deepLinks"), Links))
            for (const auto& D : *Links) Entry.DeepLinks.Add(D->AsString());

        // bridge: [{type:"sse", url:"https://..."}, ...] — only SSE wallets get a BridgeUrl
        const TArray<TSharedPtr<FJsonValue>>* BridgeArr;
        if (Obj->TryGetArrayField(TEXT("bridge"), BridgeArr))
        {
            for (const auto& B : *BridgeArr)
            {
                const TSharedPtr<FJsonObject>* BObj;
                if (!B->TryGetObject(BObj)) continue;
                FString BType;
                (*BObj)->TryGetStringField(TEXT("type"), BType);
                if (BType == TEXT("sse"))
                {
                    (*BObj)->TryGetStringField(TEXT("url"), Entry.BridgeUrl);
                    break; // first SSE bridge wins
                }
            }
        }

        OutWallets.Add(Entry);
    }
    return true;
}

static FString GetConfiguredTonApiUrl()
{
    const UTonConnectDeveloperSettings* S = GetDefault<UTonConnectDeveloperSettings>();
    return S ? S->GetTonApiUrl() : TEXT("https://tonapi.io/v2");
}

FTonApiClient::FTonApiClient()
    : Http(&FHttpModule::Get())
{
}

void FTonApiClient::GetWalletList(TFunction<void(bool, TArray<FTonWalletListEntry>)> Callback)
{
    const FString CachePath = WalletListCachePath();

    // 1) Serve from disk cache if it's still fresh.
    if (IFileManager::Get().FileExists(*CachePath))
    {
        const FDateTime Modified = IFileManager::Get().GetTimeStamp(*CachePath);
        const double AgeHours = (FDateTime::UtcNow() - Modified).GetTotalHours();
        if (AgeHours >= 0.0 && AgeHours < WalletListCacheTtlHours)
        {
            FString Cached;
            TArray<FTonWalletListEntry> Wallets;
            if (FFileHelper::LoadFileToString(Cached, *CachePath)
                && ParseWalletListJson(Cached, Wallets) && Wallets.Num() > 0)
            {
                UE_LOG(LogTemp, Log, TEXT("TonConnect: wallet list from cache (%d entries, %.1fh old)"),
                       Wallets.Num(), AgeHours);
                if (Callback) Callback(true, Wallets);
                return;
            }
        }
    }

    // 2) Download fresh, write cache, parse. Fall back to stale cache on network failure.
    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = Http->CreateRequest();
    Request->SetURL(WalletListUrl);
    Request->SetVerb(TEXT("GET"));
    Request->SetHeader(TEXT("Accept"), TEXT("application/json"));

    Request->OnProcessRequestComplete().BindLambda(
        [Callback, CachePath](FHttpRequestPtr Req, FHttpResponsePtr Resp, bool bSuccess)
        {
            TArray<FTonWalletListEntry> Wallets;

            if (bSuccess && Resp.IsValid() && Resp->GetResponseCode() == 200)
            {
                const FString Body = Resp->GetContentAsString();
                if (ParseWalletListJson(Body, Wallets) && Wallets.Num() > 0)
                {
                    // Ensure the cache directory exists, then refresh the cache file.
                    IFileManager::Get().MakeDirectory(*FPaths::GetPath(CachePath), /*Tree=*/true);
                    FFileHelper::SaveStringToFile(Body, *CachePath);
                    if (Callback) Callback(true, Wallets);
                    return;
                }
            }

            // Network/parse failed — try a stale cache as a last resort.
            FString Cached;
            if (FFileHelper::LoadFileToString(Cached, *CachePath)
                && ParseWalletListJson(Cached, Wallets) && Wallets.Num() > 0)
            {
                UE_LOG(LogTemp, Warning, TEXT("TonConnect: wallet list fetch failed — using stale cache"));
                if (Callback) Callback(true, Wallets);
                return;
            }

            if (Callback) Callback(false, Wallets);
        });

    Request->ProcessRequest();
}

static FString ParseWalletVersion(const FString& Interface)
{
    if (Interface == TEXT("wallet_v5_r1")) return TEXT("W5 R1");
    if (Interface == TEXT("wallet_v4r2"))  return TEXT("V4R2");
    if (Interface == TEXT("wallet_v3r2"))  return TEXT("V3R2");
    if (Interface == TEXT("wallet_v3r1"))  return TEXT("V3R1");
    if (Interface == TEXT("wallet_v2r2"))  return TEXT("V2R2");
    if (Interface == TEXT("wallet_v1r3"))  return TEXT("V1R3");
    return Interface;
}

void FTonApiClient::GetAccountInfo(const FString& Address, TFunction<void(bool, FTonAccountInfo)> Callback)
{
    FString Url = GetConfiguredTonApiUrl() + TEXT("/accounts/") + Address;
    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = Http->CreateRequest();
    Request->SetURL(Url);
    Request->SetVerb(TEXT("GET"));
    Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
    Request->OnProcessRequestComplete().BindLambda(
        [Callback](FHttpRequestPtr, FHttpResponsePtr Resp, bool bSuccess)
        {
            FTonAccountInfo Info;
            if (!bSuccess || !Resp.IsValid() || Resp->GetResponseCode() != 200)
            {
                if (Callback) Callback(false, Info);
                return;
            }
            TSharedPtr<FJsonObject> Obj;
            TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Resp->GetContentAsString());
            if (!FJsonSerializer::Deserialize(Reader, Obj) || !Obj.IsValid())
            {
                if (Callback) Callback(false, Info);
                return;
            }
            Obj->TryGetStringField(TEXT("balance"), Info.BalanceNano);
            const TArray<TSharedPtr<FJsonValue>>* Interfaces;
            if (Obj->TryGetArrayField(TEXT("interfaces"), Interfaces) && Interfaces->Num() > 0)
                Info.WalletVersion = ParseWalletVersion((*Interfaces)[0]->AsString());
            if (Callback) Callback(true, Info);
        });
    Request->ProcessRequest();
}

void FTonApiClient::GetJettonBalances(const FString& Address,
                                       TFunction<void(bool, TArray<FTonJettonBalance>)> Callback)
{
    FString Url = GetConfiguredTonApiUrl() + TEXT("/accounts/") + Address + TEXT("/jettons");
    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = Http->CreateRequest();
    Request->SetURL(Url);
    Request->SetVerb(TEXT("GET"));
    Request->SetHeader(TEXT("Accept"), TEXT("application/json"));

    Request->OnProcessRequestComplete().BindLambda(
        [Callback](FHttpRequestPtr, FHttpResponsePtr Resp, bool bSuccess)
        {
            TArray<FTonJettonBalance> Result;
            if (!bSuccess || !Resp.IsValid() || Resp->GetResponseCode() != 200)
            {
                if (Callback) Callback(false, Result);
                return;
            }
            TSharedPtr<FJsonObject> Root;
            TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Resp->GetContentAsString());
            if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
            {
                if (Callback) Callback(false, Result);
                return;
            }
            const TArray<TSharedPtr<FJsonValue>>* Items;
            if (Root->TryGetArrayField(TEXT("balances"), Items))
            {
                for (const auto& Val : *Items)
                {
                    const TSharedPtr<FJsonObject>* ObjPtr;
                    if (!Val->TryGetObject(ObjPtr)) continue;
                    FTonJettonBalance B;
                    (*ObjPtr)->TryGetStringField(TEXT("balance"), B.Balance);
                    const TSharedPtr<FJsonObject>* WalletObj;
                    if ((*ObjPtr)->TryGetObjectField(TEXT("wallet_address"), WalletObj))
                        (*WalletObj)->TryGetStringField(TEXT("address"), B.WalletAddress);
                    const TSharedPtr<FJsonObject>* JettonObj;
                    if ((*ObjPtr)->TryGetObjectField(TEXT("jetton"), JettonObj))
                    {
                        (*JettonObj)->TryGetStringField(TEXT("address"), B.JettonAddress);
                        const TSharedPtr<FJsonObject>* Meta;
                        if ((*JettonObj)->TryGetObjectField(TEXT("metadata"), Meta))
                        {
                            (*Meta)->TryGetStringField(TEXT("symbol"), B.Symbol);
                            double Dec = 9;
                            (*Meta)->TryGetNumberField(TEXT("decimals"), Dec);
                            B.Decimals = (int32)Dec;
                        }
                    }
                    Result.Add(B);
                }
            }
            if (Callback) Callback(true, Result);
        });
    Request->ProcessRequest();
}

void FTonApiClient::GetNfts(const FString& Address,
                             TFunction<void(bool, TArray<FTonNftItem>)> Callback)
{
    FString Url = GetConfiguredTonApiUrl() + TEXT("/accounts/") + Address + TEXT("/nfts?limit=100");
    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = Http->CreateRequest();
    Request->SetURL(Url);
    Request->SetVerb(TEXT("GET"));
    Request->SetHeader(TEXT("Accept"), TEXT("application/json"));

    Request->OnProcessRequestComplete().BindLambda(
        [Callback](FHttpRequestPtr, FHttpResponsePtr Resp, bool bSuccess)
        {
            TArray<FTonNftItem> Result;
            if (!bSuccess || !Resp.IsValid() || Resp->GetResponseCode() != 200)
            {
                if (Callback) Callback(false, Result);
                return;
            }
            TSharedPtr<FJsonObject> Root;
            TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Resp->GetContentAsString());
            if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
            {
                if (Callback) Callback(false, Result);
                return;
            }
            const TArray<TSharedPtr<FJsonValue>>* Items;
            if (Root->TryGetArrayField(TEXT("nft_items"), Items))
            {
                for (const auto& Val : *Items)
                {
                    const TSharedPtr<FJsonObject>* ObjPtr;
                    if (!Val->TryGetObject(ObjPtr)) continue;
                    FTonNftItem N;
                    (*ObjPtr)->TryGetStringField(TEXT("address"), N.Address);
                    double Idx = 0;
                    (*ObjPtr)->TryGetNumberField(TEXT("index"), Idx);
                    N.Index = (int64)Idx;
                    const TSharedPtr<FJsonObject>* ColObj;
                    if ((*ObjPtr)->TryGetObjectField(TEXT("collection"), ColObj))
                        (*ColObj)->TryGetStringField(TEXT("address"), N.CollectionAddress);
                    const TSharedPtr<FJsonObject>* Meta;
                    if ((*ObjPtr)->TryGetObjectField(TEXT("metadata"), Meta))
                        (*Meta)->TryGetStringField(TEXT("name"), N.Name);
                    Result.Add(N);
                }
            }
            if (Callback) Callback(true, Result);
        });
    Request->ProcessRequest();
}

void FTonApiClient::GetHistory(const FString& Address, int32 Limit,
                                TFunction<void(bool, TArray<FTonTxEntry>)> Callback)
{
    FString Url = GetConfiguredTonApiUrl() + TEXT("/accounts/") + Address
        + FString::Printf(TEXT("/events?limit=%d"), FMath::Clamp(Limit, 1, 100));
    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = Http->CreateRequest();
    Request->SetURL(Url);
    Request->SetVerb(TEXT("GET"));
    Request->SetHeader(TEXT("Accept"), TEXT("application/json"));

    Request->OnProcessRequestComplete().BindLambda(
        [Callback](FHttpRequestPtr, FHttpResponsePtr Resp, bool bSuccess)
        {
            TArray<FTonTxEntry> Result;
            if (!bSuccess || !Resp.IsValid() || Resp->GetResponseCode() != 200)
            {
                if (Callback) Callback(false, Result);
                return;
            }
            TSharedPtr<FJsonObject> Root;
            TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Resp->GetContentAsString());
            if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
            {
                if (Callback) Callback(false, Result);
                return;
            }
            const TArray<TSharedPtr<FJsonValue>>* Events;
            if (Root->TryGetArrayField(TEXT("events"), Events))
            {
                for (const auto& Val : *Events)
                {
                    const TSharedPtr<FJsonObject>* ObjPtr;
                    if (!Val->TryGetObject(ObjPtr)) continue;
                    FTonTxEntry Tx;
                    (*ObjPtr)->TryGetStringField(TEXT("event_id"), Tx.Hash);
                    double Ts = 0;
                    (*ObjPtr)->TryGetNumberField(TEXT("timestamp"), Ts);
                    Tx.Timestamp = (int64)Ts;
                    // Parse first TonTransfer action for amount+comment if present
                    const TArray<TSharedPtr<FJsonValue>>* Actions;
                    if ((*ObjPtr)->TryGetArrayField(TEXT("actions"), Actions) && Actions->Num() > 0)
                    {
                        const TSharedPtr<FJsonObject>* ActPtr;
                        if ((*Actions)[0]->TryGetObject(ActPtr))
                        {
                            const TSharedPtr<FJsonObject>* TT;
                            if ((*ActPtr)->TryGetObjectField(TEXT("TonTransfer"), TT))
                            {
                                double Amt = 0;
                                (*TT)->TryGetNumberField(TEXT("amount"), Amt);
                                Tx.InAmountNano = FString::Printf(TEXT("%.0f"), Amt);
                                (*TT)->TryGetStringField(TEXT("comment"), Tx.Comment);
                            }
                        }
                    }
                    Result.Add(Tx);
                }
            }
            if (Callback) Callback(true, Result);
        });
    Request->ProcessRequest();
}

void FTonApiClient::GetTransaction(const FString& TxHash, TFunction<void(bool, FTonTxEntry)> Callback)
{
    FString Url = GetConfiguredTonApiUrl() + TEXT("/blockchain/transactions/") + TxHash;
    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = Http->CreateRequest();
    Request->SetURL(Url);
    Request->SetVerb(TEXT("GET"));
    Request->SetHeader(TEXT("Accept"), TEXT("application/json"));

    Request->OnProcessRequestComplete().BindLambda(
        [Callback](FHttpRequestPtr, FHttpResponsePtr Resp, bool bSuccess)
        {
            // 404 = not yet on chain — treat as "not found yet", not as error
            if (!bSuccess || !Resp.IsValid() || Resp->GetResponseCode() != 200)
            {
                if (Callback) Callback(false, FTonTxEntry{});
                return;
            }
            TSharedPtr<FJsonObject> Root;
            TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Resp->GetContentAsString());
            if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
            {
                if (Callback) Callback(false, FTonTxEntry{});
                return;
            }
            FTonTxEntry Tx;
            Root->TryGetStringField(TEXT("hash"), Tx.Hash);
            double Ts = 0;
            Root->TryGetNumberField(TEXT("utime"), Ts);
            Tx.Timestamp = (int64)Ts;
            // Extract in_msg amount if present
            const TSharedPtr<FJsonObject>* InMsg;
            if (Root->TryGetObjectField(TEXT("in_msg"), InMsg))
            {
                double Val = 0;
                (*InMsg)->TryGetNumberField(TEXT("value"), Val);
                Tx.InAmountNano = FString::Printf(TEXT("%.0f"), Val);
                (*InMsg)->TryGetStringField(TEXT("decoded_body"), Tx.Comment);
            }
            if (Callback) Callback(true, Tx);
        });
    Request->ProcessRequest();
}

void FTonApiClient::CallGetMethod(const FString& Address, const FString& Method,
                                   const TArray<FString>& Args,
                                   TFunction<void(bool, TMap<FString,FString>)> Callback)
{
    FString Url = GetConfiguredTonApiUrl()
        + TEXT("/blockchain/accounts/") + Address
        + TEXT("/methods/") + Method;

    // Append stack inputs as repeated ?args= query params (TonAPI runGetMethod).
    for (int32 i = 0; i < Args.Num(); ++i)
    {
        Url += (i == 0 ? TEXT("?") : TEXT("&"));
        Url += TEXT("args=") + FGenericPlatformHttp::UrlEncode(Args[i]);
    }

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = Http->CreateRequest();
    Request->SetURL(Url);
    Request->SetVerb(TEXT("GET"));
    Request->SetHeader(TEXT("Accept"), TEXT("application/json"));

    Request->OnProcessRequestComplete().BindLambda(
        [Callback](FHttpRequestPtr, FHttpResponsePtr Resp, bool bSuccess)
        {
            TMap<FString,FString> Stack;
            if (!bSuccess || !Resp.IsValid() || Resp->GetResponseCode() != 200)
            {
                if (Callback) Callback(false, Stack);
                return;
            }
            TSharedPtr<FJsonObject> Root;
            TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Resp->GetContentAsString());
            if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
            {
                if (Callback) Callback(false, Stack);
                return;
            }
            bool bOk = false;
            Root->TryGetBoolField(TEXT("success"), bOk);
            const TArray<TSharedPtr<FJsonValue>>* Items;
            if (Root->TryGetArrayField(TEXT("stack"), Items))
            {
                for (int32 i = 0; i < Items->Num(); ++i)
                {
                    const TSharedPtr<FJsonObject>* ObjPtr;
                    if (!(*Items)[i]->TryGetObject(ObjPtr)) continue;
                    FString Type;
                    (*ObjPtr)->TryGetStringField(TEXT("type"), Type);

                    FString Val;
                    if (Type == TEXT("num"))
                    {
                        FString Hex;
                        (*ObjPtr)->TryGetStringField(TEXT("num"), Hex);
                        // tonapi returns hex like "0x1234" — convert to decimal
                        if (Hex.StartsWith(TEXT("0x")) || Hex.StartsWith(TEXT("0X")))
                        {
                            uint64 N = FCString::Strtoui64(*Hex + 2, nullptr, 16);
                            Val = FString::Printf(TEXT("%llu"), N);
                        }
                        else Val = Hex;
                    }
                    else if (Type == TEXT("cell") || Type == TEXT("slice") || Type == TEXT("builder"))
                    {
                        (*ObjPtr)->TryGetStringField(Type, Val);
                    }
                    // null → Val stays empty
                    Stack.Add(FString::FromInt(i), Val);
                }
            }
            if (Callback) Callback(bOk, Stack);
        });
    Request->ProcessRequest();
}

void FTonApiClient::GetBalance(const FString& Address, TFunction<void(bool, FString)> Callback)
{
    FString Url = GetConfiguredTonApiUrl() + TEXT("/accounts/") + Address;
    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = Http->CreateRequest();
    Request->SetURL(Url);
    Request->SetVerb(TEXT("GET"));
    Request->SetHeader(TEXT("Accept"), TEXT("application/json"));

    Request->OnProcessRequestComplete().BindLambda(
        [Callback](FHttpRequestPtr Req, FHttpResponsePtr Resp, bool bSuccess)
        {
            if (!bSuccess || !Resp.IsValid() || Resp->GetResponseCode() != 200)
            {
                if (Callback) Callback(false, TEXT("0"));
                return;
            }

            TSharedPtr<FJsonObject> Obj;
            TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Resp->GetContentAsString());
            if (!FJsonSerializer::Deserialize(Reader, Obj) || !Obj.IsValid())
            {
                if (Callback) Callback(false, TEXT("0"));
                return;
            }

            FString Balance;
            if (!Obj->TryGetStringField(TEXT("balance"), Balance)) Balance = TEXT("0");
            if (Callback) Callback(true, Balance);
        });

    Request->ProcessRequest();
}

void FTonApiClient::GetSeqno(const FString& Address, TFunction<void(bool, int32)> Callback)
{
    const FString Url = GetConfiguredTonApiUrl() + TEXT("/accounts/") + Address + TEXT("/methods/seqno");
    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = Http->CreateRequest();
    Request->SetURL(Url);
    Request->SetVerb(TEXT("GET"));
    Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
    Request->OnProcessRequestComplete().BindLambda(
        [Callback](FHttpRequestPtr, FHttpResponsePtr Resp, bool bSuccess)
        {
            if (!bSuccess || !Resp.IsValid() || Resp->GetResponseCode() != 200)
            {
                if (Callback) Callback(false, 0);
                return;
            }

            TSharedPtr<FJsonObject> Obj;
            TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Resp->GetContentAsString());
            if (!FJsonSerializer::Deserialize(Reader, Obj) || !Obj.IsValid())
            {
                if (Callback) Callback(false, 0);
                return;
            }

            // Prefer the decoded value; fall back to the raw stack[0].num (may be hex "0x..").
            int32 Seqno = 0;
            const TSharedPtr<FJsonObject>* Decoded;
            if (Obj->TryGetObjectField(TEXT("decoded"), Decoded))
                (*Decoded)->TryGetNumberField(TEXT("seqno"), Seqno);

            if (Seqno == 0)
            {
                const TArray<TSharedPtr<FJsonValue>>* Stack;
                if (Obj->TryGetArrayField(TEXT("stack"), Stack) && Stack->Num() > 0)
                {
                    const TSharedPtr<FJsonObject>* S0;
                    if ((*Stack)[0]->TryGetObject(S0))
                    {
                        FString Num;
                        if ((*S0)->TryGetStringField(TEXT("num"), Num))
                        {
                            Num.TrimStartAndEndInline();
                            Seqno = Num.StartsWith(TEXT("0x"))
                                ? (int32)FParse::HexNumber(*Num.RightChop(2))
                                : FCString::Atoi(*Num);
                        }
                    }
                }
            }

            if (Callback) Callback(true, Seqno);
        });
    Request->ProcessRequest();
}

void FTonApiClient::EmulateMessage(const FString& BocBase64, TFunction<void(bool, int64)> Callback)
{
    const FString Url = GetConfiguredTonApiUrl() + TEXT("/wallet/emulate");
    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = Http->CreateRequest();
    Request->SetURL(Url);
    Request->SetVerb(TEXT("POST"));
    Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
    Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
    // base64 contains no characters needing JSON escaping
    Request->SetContentAsString(FString::Printf(TEXT("{\"boc\":\"%s\"}"), *BocBase64));
    Request->OnProcessRequestComplete().BindLambda(
        [Callback](FHttpRequestPtr, FHttpResponsePtr Resp, bool bSuccess)
        {
            if (!bSuccess || !Resp.IsValid() || Resp->GetResponseCode() != 200)
            {
                if (Callback) Callback(false, 0);
                return;
            }

            TSharedPtr<FJsonObject> Obj;
            TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Resp->GetContentAsString());
            if (!FJsonSerializer::Deserialize(Reader, Obj) || !Obj.IsValid())
            {
                if (Callback) Callback(false, 0);
                return;
            }

            // MessageConsequences.event.extra = total fee paid by the account (negative nanoTON).
            const TSharedPtr<FJsonObject>* Event;
            if (!Obj->TryGetObjectField(TEXT("event"), Event))
            {
                if (Callback) Callback(false, 0);
                return;
            }
            int64 Extra = 0;
            (*Event)->TryGetNumberField(TEXT("extra"), Extra);
            if (Callback) Callback(true, Extra < 0 ? -Extra : Extra);
        });
    Request->ProcessRequest();
}
