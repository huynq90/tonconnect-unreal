#include "TonUtils.h"
#include "Misc/Base64.h"

// ---- FTonAmountUtils ----

bool FTonAmountUtils::ParseNanoTon(const FString& TonString, uint64& OutNano)
{
    if (TonString.IsEmpty()) return false;

    FString IntPart, FracPart;
    if (!TonString.Split(TEXT("."), &IntPart, &FracPart))
    {
        IntPart = TonString;
        FracPart = TEXT("");
    }

    // Pad or truncate fractional part to exactly 9 digits (nanoTON precision)
    while (FracPart.Len() < 9) FracPart += TEXT("0");
    if (FracPart.Len() > 9) FracPart = FracPart.Left(9);

    if (IntPart.IsEmpty()) IntPart = TEXT("0");

    uint64 IntVal = FCString::Strtoui64(*IntPart, nullptr, 10);
    uint64 FracVal = FCString::Strtoui64(*FracPart, nullptr, 10);

    OutNano = IntVal * 1000000000ULL + FracVal;
    return true;
}

FString FTonAmountUtils::FormatTon(uint64 NanoTon)
{
    uint64 IntPart = NanoTon / 1000000000ULL;
    uint64 FracPart = NanoTon % 1000000000ULL;

    if (FracPart == 0)
    {
        return FString::Printf(TEXT("%llu"), IntPart);
    }

    FString FracStr = FString::Printf(TEXT("%09llu"), FracPart);
    while (FracStr.EndsWith(TEXT("0"))) FracStr = FracStr.LeftChop(1);

    return FString::Printf(TEXT("%llu.%s"), IntPart, *FracStr);
}

uint64 FTonAmountUtils::NanoStringToUint64(const FString& NanoString)
{
    if (NanoString.IsEmpty()) return 0;
    return FCString::Strtoui64(*NanoString, nullptr, 10);
}

// ---- FTonByteUtils ----

FString FTonByteUtils::BytesToHex(const TArray<uint8>& Bytes)
{
    FString Result;
    Result.Reserve(Bytes.Num() * 2);
    for (uint8 B : Bytes)
    {
        Result += FString::Printf(TEXT("%02x"), B);
    }
    return Result;
}

TArray<uint8> FTonByteUtils::HexToBytes(const FString& Hex)
{
    TArray<uint8> Result;
    FString Clean = Hex.Replace(TEXT(" "), TEXT(""));
    if (Clean.Len() % 2 != 0) return Result;

    Result.Reserve(Clean.Len() / 2);
    for (int32 i = 0; i < Clean.Len(); i += 2)
    {
        FString ByteStr = Clean.Mid(i, 2);
        Result.Add((uint8)FCString::Strtoi(*ByteStr, nullptr, 16));
    }
    return Result;
}

FString FTonByteUtils::BytesToBase64(const TArray<uint8>& Bytes)
{
    return FBase64::Encode(Bytes);
}

TArray<uint8> FTonByteUtils::Base64ToBytes(const FString& Base64)
{
    TArray<uint8> Result;
    FBase64::Decode(Base64, Result);
    return Result;
}

void FTonByteUtils::SplitAt(const TArray<uint8>& In, int32 Index,
                              TArray<uint8>& OutA, TArray<uint8>& OutB)
{
    const int32 ClampedIndex = FMath::Clamp(Index, 0, In.Num());
    OutA = TArray<uint8>(In.GetData(), ClampedIndex);
    if (ClampedIndex < In.Num())
    {
        OutB = TArray<uint8>(In.GetData() + ClampedIndex, In.Num() - ClampedIndex);
    }
    else
    {
        OutB.Reset();
    }
}

// ---- FTonAddressUtils ----

// CRC-16/CCITT — same algorithm used in the reference TonCommon implementation
static TArray<uint8> TonCRC16(const TArray<uint8>& Data)
{
    const uint16 Poly = 0x1021;
    int Reg = 0;
    TArray<uint8> Msg = Data;
    Msg.Add(0);
    Msg.Add(0);
    for (uint8 Byte : Msg)
    {
        int Mask = 0x80;
        while (Mask > 0)
        {
            Reg <<= 1;
            if (Byte & Mask) Reg += 1;
            Mask >>= 1;
            if (Reg > 0xFFFF) { Reg &= 0xFFFF; Reg ^= Poly; }
        }
    }
    TArray<uint8> Result;
    Result.Add(Reg >> 8);
    Result.Add(Reg & 0xFF);
    return Result;
}

bool FTonAddressUtils::ParseHumanAddress(const FString& Addr,
                                          int32& OutWorkchain,
                                          TArray<uint8>& OutHash,
                                          bool* bOutBounceable,
                                          bool* bOutTestnet)
{
    // Raw format "workchain:hexhash" — no flag information available
    int32 ColonIdx = INDEX_NONE;
    if (Addr.FindChar(TEXT(':'), ColonIdx))
    {
        OutWorkchain = FCString::Atoi(*Addr.Left(ColonIdx));
        OutHash = FTonByteUtils::HexToBytes(Addr.Mid(ColonIdx + 1));
        if (bOutBounceable) *bOutBounceable = false;
        if (bOutTestnet)    *bOutTestnet    = false;
        return OutHash.Num() == 32;
    }

    // Friendly base64url: replace URL-safe chars then decode
    FString B64 = Addr;
    B64 = B64.Replace(TEXT("-"), TEXT("+")).Replace(TEXT("_"), TEXT("/"));
    while (B64.Len() % 4 != 0) B64 += TEXT("=");

    TArray<uint8> Decoded;
    if (!FBase64::Decode(B64, Decoded) || Decoded.Num() != 36) return false;

    // CRC-16 check over first 34 bytes
    TArray<uint8> Header(Decoded.GetData(), 34);
    TArray<uint8> Expected = TonCRC16(Header);
    if (Decoded[34] != Expected[0] || Decoded[35] != Expected[1]) return false;

    // Decode flags from tag byte
    // Bit 7 (0x80): testnet; bit 6 (0x40): non-bounceable (0 = bounceable)
    const uint8 Tag = Decoded[0];
    if (bOutBounceable) *bOutBounceable = !(Tag & 0x40);
    if (bOutTestnet)    *bOutTestnet    =  (Tag & 0x80) != 0;

    OutWorkchain = static_cast<int8>(Decoded[1]);
    OutHash = TArray<uint8>(Decoded.GetData() + 2, 32);
    return true;
}

FString FTonAddressUtils::ToHumanReadable(const FString& RawAddress, bool bBounceable, bool bTestnet)
{
    TArray<FString> Parts;
    RawAddress.ParseIntoArray(Parts, TEXT(":"), true);
    if (Parts.Num() != 2) return RawAddress;  // pass-through if already friendly / unrecognised

    int32 WorkChain = FCString::Atoi(*Parts[0]);
    TArray<uint8> Hash = FTonByteUtils::HexToBytes(Parts[1]);
    if (Hash.Num() != 32) return RawAddress;

    // Build tag: 0x11 base + 0x40 if non-bounceable + 0x80 if testnet
    uint8 Tag = 0x11;
    if (!bBounceable) Tag |= 0x40;
    if (bTestnet)     Tag |= 0x80;

    TArray<uint8> AddrBuf;
    AddrBuf.SetNumZeroed(34);
    AddrBuf[0] = Tag;
    AddrBuf[1] = static_cast<uint8>(WorkChain);
    FMemory::Memcpy(AddrBuf.GetData() + 2, Hash.GetData(), 32);

    TArray<uint8> Checksum = TonCRC16(AddrBuf);
    AddrBuf.Append(Checksum);

    FString B64 = FBase64::Encode(AddrBuf);
    B64 = B64.Replace(TEXT("+"), TEXT("-")).Replace(TEXT("/"), TEXT("_"));
    // Remove base64 padding — TON friendly addresses are always 48 chars unpadded
    B64 = B64.Replace(TEXT("="), TEXT(""));
    return B64;
}
