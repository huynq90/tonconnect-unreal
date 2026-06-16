#pragma once

#include "CoreMinimal.h"

// Minimal TON TL-B cell bit writer with single-root BOC serializer.
// MSB-first bit packing. Supports up to 1023 bits and 4 refs per cell.
// All factory helpers return heap-allocated cells ready for ToBocBase64().
struct TONCONNECT_API FTonCell
{
    TArray<uint8> Bytes;
    int32 BitLen = 0;
    TArray<TSharedPtr<FTonCell>> Refs;  // max 4

    void WriteBit(bool Bit);
    // Write Val using exactly Bits bits (MSB first).
    void WriteUint(uint64 Val, int32 Bits);
    // Signed two's-complement, MSB first.
    void WriteInt(int64 Val, int32 Bits);
    // Write each byte as 8 bits. Call site must ensure byte alignment if needed.
    void WriteBytes(const TArray<uint8>& Data);
    // VarUInteger(2^LenBits): LenBits-bit length prefix n, then n*8 data bits.
    // LenBits=4 → VarUInteger 16 (Grams / Coins).
    void WriteVarUint(uint64 Val, int32 LenBits = 4);
    // addr_std: 10 (type) | 0 (anycast) | workchain (8b) | hash (256b) = 267 bits.
    // Use FTonAddressUtils::ParseHumanAddress to get Workchain + Hash.
    void WriteAddress(int32 Workchain, const TArray<uint8>& Hash);
    // Append a child cell reference (max 4 refs).
    void AddRef(TSharedPtr<FTonCell> Ref);

    // Serialize as BOC base64 (single root, no index, no CRC32C).
    FString ToBocBase64() const;

    // --- Factory helpers ---

    // Text comment body: op=0x00000000 + UTF-8 text.
    static TSharedPtr<FTonCell> TextComment(const FString& Text);

    // TEP-74 jetton transfer body (goes in the `payload` field of sendTransaction).
    // Amount is the jetton base-unit amount (uint64, not nanoTON).
    // FwdTonNano: TON attached for notification (default 1 nanoTON = minimum).
    static TSharedPtr<FTonCell> JettonTransfer(
        int32 DestWorkchain, const TArray<uint8>& DestHash,
        int32 RespWorkchain, const TArray<uint8>& RespHash,
        uint64 Amount, uint64 FwdTonNano = 1);

    // TEP-62 NFT transfer body.
    // FwdTonNano: TON attached for ownership notification (default 0).
    static TSharedPtr<FTonCell> NftTransfer(
        int32 NewOwnerWorkchain, const TArray<uint8>& NewOwnerHash,
        int32 RespWorkchain, const TArray<uint8>& RespHash,
        uint64 FwdTonNano = 0);
};
