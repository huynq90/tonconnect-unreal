#include "Cells/TonBoc.h"
#include "TonSession.h"
#include "Misc/Base64.h"

namespace
{
    // One parsed cell: raw descriptors + data bytes + child indices.
    struct FBocCell
    {
        uint8 D1 = 0;
        uint8 D2 = 0;
        TArray<uint8> Data;
        TArray<int32> Refs;
    };

    // Read a big-endian unsigned integer of `Size` bytes from Bytes at Pos (advances Pos).
    bool ReadUIntBE(const TArray<uint8>& Bytes, int32& Pos, int32 Size, uint32& Out)
    {
        if (Size <= 0 || Pos + Size > Bytes.Num()) return false;
        uint32 V = 0;
        for (int32 i = 0; i < Size; ++i) V = (V << 8) | Bytes[Pos++];
        Out = V;
        return true;
    }

    // Parse a standard BOC (magic b5ee9c72). Fills cells + root index.
    bool ParseBoc(const TArray<uint8>& B, TArray<FBocCell>& OutCells, int32& OutRoot)
    {
        int32 Pos = 0;
        uint32 Magic = 0;
        if (!ReadUIntBE(B, Pos, 4, Magic) || Magic != 0xB5EE9C72u) return false;

        if (Pos >= B.Num()) return false;
        const uint8 Flags = B[Pos++];
        const bool  bHasIdx = (Flags & 0x80) != 0;
        const bool  bHasCrc = (Flags & 0x40) != 0;
        const int32 RefSize = Flags & 0x07;           // bytes per cell index
        if (RefSize == 0 || RefSize > 4) return false;

        if (Pos >= B.Num()) return false;
        const int32 OffSize = B[Pos++];               // bytes per offset
        if (OffSize == 0 || OffSize > 8) return false;

        uint32 CellsCount = 0, RootsCount = 0, Absent = 0, TotSize = 0;
        if (!ReadUIntBE(B, Pos, RefSize, CellsCount)) return false;
        if (!ReadUIntBE(B, Pos, RefSize, RootsCount)) return false;
        if (!ReadUIntBE(B, Pos, RefSize, Absent))     return false;
        if (!ReadUIntBE(B, Pos, OffSize, TotSize))    return false;
        if (CellsCount == 0 || CellsCount > 100000)   return false;

        // Root indices (use the first)
        OutRoot = 0;
        for (uint32 i = 0; i < RootsCount; ++i)
        {
            uint32 R = 0;
            if (!ReadUIntBE(B, Pos, RefSize, R)) return false;
            if (i == 0) OutRoot = (int32)R;
        }

        // Optional index section — skip
        if (bHasIdx) Pos += (int32)CellsCount * OffSize;
        if (Pos > B.Num()) return false;

        OutCells.SetNum((int32)CellsCount);
        for (uint32 i = 0; i < CellsCount; ++i)
        {
            if (Pos + 2 > B.Num()) return false;
            FBocCell& C = OutCells[(int32)i];
            C.D1 = B[Pos++];
            C.D2 = B[Pos++];

            const int32 RefsCount = C.D1 & 0x07;
            const int32 DataLen   = (C.D2 >> 1) + (C.D2 & 1); // bytes of cell data (augmented if odd)

            if (Pos + DataLen > B.Num()) return false;
            C.Data.Append(B.GetData() + Pos, DataLen);
            Pos += DataLen;

            for (int32 r = 0; r < RefsCount; ++r)
            {
                uint32 Idx = 0;
                if (!ReadUIntBE(B, Pos, RefSize, Idx)) return false;
                if ((int32)Idx >= (int32)CellsCount) return false;
                C.Refs.Add((int32)Idx);
            }
        }

        // (CRC32C trailer ignored)
        return true;
    }

    // Representation hash of one cell given its children's hashes + depths already computed.
    // repr = D1 | D2 | data | (depth_i, 2B BE)* | (hash_i, 32B)*  ; hash = SHA256(repr).
    TArray<uint8> CellHash(const FBocCell& C,
                           const TArray<TArray<uint8>>& Hashes,
                           const TArray<int32>& Depths)
    {
        TArray<uint8> Repr;
        Repr.Add(C.D1);
        Repr.Add(C.D2);
        Repr.Append(C.Data);
        for (int32 Ref : C.Refs)
        {
            const int32 D = Depths[Ref];
            Repr.Add((uint8)((D >> 8) & 0xFF));
            Repr.Add((uint8)(D & 0xFF));
        }
        for (int32 Ref : C.Refs)
            Repr.Append(Hashes[Ref]);
        return FTonSession::SHA256(Repr);
    }

    FString ToHexLower(const TArray<uint8>& Bytes)
    {
        FString Hex;
        Hex.Reserve(Bytes.Num() * 2);
        for (uint8 B : Bytes) Hex += FString::Printf(TEXT("%02x"), B);
        return Hex;
    }
}

FString FTonBoc::CodeHashFromStateInit(const FString& StateInitBase64)
{
    if (StateInitBase64.IsEmpty()) return FString();

    TArray<uint8> Boc;
    if (!FBase64::Decode(StateInitBase64, Boc)) return FString();

    TArray<FBocCell> Cells;
    int32 Root = 0;
    if (!ParseBoc(Boc, Cells, Root)) return FString();
    if (!Cells.IsValidIndex(Root)) return FString();

    // StateInit = split_depth?(Maybe) special?(Maybe) code:(Maybe ^) data:(Maybe ^) library
    // For a real wallet, ref[0] is the code cell.
    const FBocCell& RootCell = Cells[Root];
    if (RootCell.Refs.Num() < 1) return FString();
    const int32 CodeIdx = RootCell.Refs[0];
    if (!Cells.IsValidIndex(CodeIdx)) return FString();

    // Hash bottom-up. Standard BOC orders refs to higher indices, so iterate descending.
    const int32 N = Cells.Num();
    TArray<TArray<uint8>> Hashes; Hashes.SetNum(N);
    TArray<int32>         Depths; Depths.SetNum(N);
    for (int32 i = N - 1; i >= 0; --i)
    {
        const FBocCell& C = Cells[i];
        int32 MaxChildDepth = 0;
        for (int32 Ref : C.Refs)
        {
            if (!Cells.IsValidIndex(Ref) || Ref <= i) return FString(); // not topo-ordered → bail
            MaxChildDepth = FMath::Max(MaxChildDepth, Depths[Ref]);
        }
        Hashes[i] = CellHash(C, Hashes, Depths);
        Depths[i] = C.Refs.Num() == 0 ? 0 : MaxChildDepth + 1;
    }

    return ToHexLower(Hashes[CodeIdx]);
}

FString FTonBoc::WalletVersionFromCodeHash(const FString& CodeHashHex)
{
    // Known TON wallet contract code representation hashes → friendly version.
    // Extend this table using the hash logged by WalletVersionFromStateInit for any
    // wallet that prints "unknown code hash".
    static const TMap<FString, FString> Table = {
        { TEXT("84dafa449f98a6987789ba232358072bc0f76dc4524002a5d0918b9a75d2d599"), TEXT("V3R2") },
        { TEXT("64dd54805522c5be8a9db59cea0105ccf0d08786ca79beb8cb79e880a8d7322d"), TEXT("V4R1") },
        { TEXT("feb5ff6820e2ff0d9483e7e0d62c817d846789fb4ae580c878866d959dabd5c0"), TEXT("V4R2") },
        { TEXT("20834b7b72b112147e1b2fb457b84e74d1a30f04f737d4f62a668e9552d2b72f"), TEXT("W5 R1") },
    };

    const FString* Found = Table.Find(CodeHashHex.ToLower());
    return Found ? *Found : FString();
}

FString FTonBoc::WalletVersionFromStateInit(const FString& StateInitBase64)
{
    const FString CodeHash = CodeHashFromStateInit(StateInitBase64);
    if (CodeHash.IsEmpty())
    {
        UE_LOG(LogTemp, Verbose, TEXT("TonBoc: could not parse code hash from stateInit"));
        return FString();
    }

    const FString Version = WalletVersionFromCodeHash(CodeHash);
    if (Version.IsEmpty())
    {
        UE_LOG(LogTemp, Log,
            TEXT("TonBoc: unknown wallet code hash %s — add it to FTonBoc::WalletVersionFromCodeHash"),
            *CodeHash);
    }
    return Version;
}
