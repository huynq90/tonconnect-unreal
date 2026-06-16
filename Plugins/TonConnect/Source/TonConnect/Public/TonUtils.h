#pragma once

#include "CoreMinimal.h"

// Amount helpers — all amounts stay as integers (nanoTON = uint64).
// Never use float/double for TON amounts.
struct TONCONNECT_API FTonAmountUtils
{
    // "1.5" -> 1500000000 nanoTON. Returns false if parse fails.
    static bool ParseNanoTon(const FString& TonString, uint64& OutNano);

    // 1500000000 nanoTON -> "1.5"
    static FString FormatTon(uint64 NanoTon);

    // "1500000000" string -> uint64. Returns 0 on failure.
    static uint64 NanoStringToUint64(const FString& NanoString);
};

// Byte / hex utilities
struct TONCONNECT_API FTonByteUtils
{
    static FString BytesToHex(const TArray<uint8>& Bytes);
    static TArray<uint8> HexToBytes(const FString& Hex);
    static FString BytesToBase64(const TArray<uint8>& Bytes);
    static TArray<uint8> Base64ToBytes(const FString& Base64);

    // Split array at Index: [0, Index) -> OutA, [Index, end) -> OutB
    static void SplitAt(const TArray<uint8>& In, int32 Index,
                        TArray<uint8>& OutA, TArray<uint8>& OutB);
};

// TON address utilities.
//
// Tag byte variants (from TON C++ SDK block.h):
//   0x11 = bounceable mainnet  (contract addresses)
//   0x51 = non-bounceable mainnet  (wallet receive — default)
//   0x91 = bounceable testnet
//   0xD1 = non-bounceable testnet
//
// Workchain: 0 = base chain (wallets/contracts), -1 = master chain (validators).
// Address utils are flag-agnostic: callers pass booleans, not network ID strings.
struct TONCONNECT_API FTonAddressUtils
{
    // Convert raw "workchain:hexhash" → user-friendly base64url.
    //   bBounceable = false → non-bounceable tag (0x51/0xD1) — use for wallet receive addresses
    //   bBounceable = true  → bounceable tag  (0x11/0x91) — use for smart contract addresses
    //   bTestnet    = true  → adds 0x80 testnet bit
    static FString ToHumanReadable(const FString& RawAddress,
                                    bool bBounceable = false,
                                    bool bTestnet    = false);

    // Parse any address format → workchain + 32-byte hash.
    // Accepts: friendly base64url (all 4 tag variants) or raw "W:hexhash".
    // Optional output flags are populated only for friendly format; false for raw input.
    // Returns false if format is unrecognised or CRC check fails.
    static bool ParseHumanAddress(const FString& Addr,
                                   int32& OutWorkchain,
                                   TArray<uint8>& OutHash,
                                   bool* bOutBounceable = nullptr,
                                   bool* bOutTestnet    = nullptr);
};
