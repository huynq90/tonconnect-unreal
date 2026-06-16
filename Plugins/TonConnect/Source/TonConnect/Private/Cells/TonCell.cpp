#include "Cells/TonCell.h"
#include "TonUtils.h"

void FTonCell::WriteBit(bool Bit)
{
    const int32 ByteIdx = BitLen / 8;
    if (ByteIdx >= Bytes.Num()) Bytes.Add(0);
    if (Bit) Bytes[ByteIdx] |= (uint8)(1 << (7 - (BitLen % 8)));
    ++BitLen;
}

void FTonCell::WriteUint(uint64 Val, int32 Bits)
{
    for (int32 i = Bits - 1; i >= 0; --i)
        WriteBit(((Val >> i) & 1) != 0);
}

void FTonCell::WriteInt(int64 Val, int32 Bits)
{
    WriteUint(static_cast<uint64>(Val), Bits);
}

void FTonCell::WriteBytes(const TArray<uint8>& Data)
{
    for (uint8 B : Data) WriteUint(B, 8);
}

void FTonCell::WriteVarUint(uint64 Val, int32 LenBits)
{
    if (Val == 0)
    {
        WriteUint(0, LenBits);
        return;
    }
    int32 n = 0;
    uint64 Tmp = Val;
    while (Tmp > 0) { Tmp >>= 8; ++n; }
    WriteUint((uint64)n, LenBits);
    for (int32 i = n - 1; i >= 0; --i)
        WriteUint((Val >> (i * 8)) & 0xFF, 8);
}

void FTonCell::WriteAddress(int32 Workchain, const TArray<uint8>& Hash)
{
    WriteUint(0b10, 2);    // addr_std type
    WriteBit(false);        // anycast = none
    WriteInt(Workchain, 8);
    for (uint8 B : Hash) WriteUint(B, 8);
}

void FTonCell::AddRef(TSharedPtr<FTonCell> Ref)
{
    if (Ref.IsValid() && Refs.Num() < 4)
        Refs.Add(Ref);
}

FString FTonCell::ToBocBase64() const
{
    // BFS ordering: root = index 0, children follow
    TArray<const FTonCell*> Order;
    TMap<const FTonCell*, int32> IndexMap;
    Order.Add(this);
    IndexMap.Add(this, 0);

    for (int32 i = 0; i < Order.Num(); ++i)
    {
        for (const auto& Ref : Order[i]->Refs)
        {
            if (Ref.IsValid() && !IndexMap.Contains(Ref.Get()))
            {
                IndexMap.Add(Ref.Get(), Order.Num());
                Order.Add(Ref.Get());
            }
        }
    }

    const int32 NumCells = Order.Num();

    // Serialize cell data (descriptors + payload + ref indices)
    TArray<uint8> AllCells;
    for (const FTonCell* C : Order)
    {
        const int32 FullBytes  = C->BitLen / 8;
        const bool  bPartial   = (C->BitLen % 8) != 0;
        const int32 TotalBytes = FullBytes + (bPartial ? 1 : 0);

        AllCells.Add((uint8)C->Refs.Num());                               // d1
        AllCells.Add((uint8)(FullBytes * 2 + (bPartial ? 1 : 0)));        // d2

        for (int32 b = 0; b < TotalBytes; ++b)
            AllCells.Add(b < C->Bytes.Num() ? C->Bytes[b] : 0);

        // Completion tag: set the bit just after the last used bit in the last byte
        if (bPartial && TotalBytes > 0)
        {
            const int32 UsedBits = C->BitLen % 8;
            AllCells[AllCells.Num() - 1] |= (uint8)(1 << (7 - UsedBits));
        }

        // Ref indices (1 byte each — sufficient for < 256 cells)
        for (const auto& Ref : C->Refs)
        {
            if (Ref.IsValid())
                AllCells.Add((uint8)IndexMap[Ref.Get()]);
        }
    }

    // BOC header
    const int32 TotBytes = AllCells.Num();
    const uint8 OffBytes = TotBytes <= 255 ? 1 : 2;

    TArray<uint8> BOC;
    BOC.Add(0xB5); BOC.Add(0xEE); BOC.Add(0x9C); BOC.Add(0x72); // magic
    BOC.Add(0x01);              // flags: size_bytes=1
    BOC.Add(OffBytes);
    BOC.Add((uint8)NumCells);
    BOC.Add(0x01);              // roots_count = 1
    BOC.Add(0x00);              // absent_count = 0
    if (OffBytes == 2) BOC.Add((uint8)(TotBytes >> 8));
    BOC.Add((uint8)(TotBytes & 0xFF));
    BOC.Add(0x00);              // root_list[0] = cell index 0
    BOC.Append(AllCells);

    return FTonByteUtils::BytesToBase64(BOC);
}

// --- Factory helpers ---

TSharedPtr<FTonCell> FTonCell::TextComment(const FString& Text)
{
    auto Cell = MakeShared<FTonCell>();
    Cell->WriteUint(0, 32);
    FTCHARToUTF8 Conv(*Text);
    TArray<uint8> Utf8;
    Utf8.Append(reinterpret_cast<const uint8*>(Conv.Get()), Conv.Length());
    Cell->WriteBytes(Utf8);
    return Cell;
}

TSharedPtr<FTonCell> FTonCell::JettonTransfer(
    int32 DestWorkchain, const TArray<uint8>& DestHash,
    int32 RespWorkchain, const TArray<uint8>& RespHash,
    uint64 Amount, uint64 FwdTonNano)
{
    auto Cell = MakeShared<FTonCell>();
    Cell->WriteUint(0xf8a7ea5ULL, 32);       // TEP-74 op
    Cell->WriteUint(0, 64);                   // query_id
    Cell->WriteVarUint(Amount, 4);            // amount (Coins)
    Cell->WriteAddress(DestWorkchain, DestHash);   // destination
    Cell->WriteAddress(RespWorkchain, RespHash);   // response_destination
    Cell->WriteBit(false);                    // no custom_payload
    Cell->WriteVarUint(FwdTonNano, 4);       // forward_ton_amount
    Cell->WriteBit(false);                    // forward_payload inline = none
    return Cell;
}

TSharedPtr<FTonCell> FTonCell::NftTransfer(
    int32 NewOwnerWorkchain, const TArray<uint8>& NewOwnerHash,
    int32 RespWorkchain, const TArray<uint8>& RespHash,
    uint64 FwdTonNano)
{
    auto Cell = MakeShared<FTonCell>();
    Cell->WriteUint(0x5fcc3d14ULL, 32);              // TEP-62 op
    Cell->WriteUint(0, 64);                           // query_id
    Cell->WriteAddress(NewOwnerWorkchain, NewOwnerHash);  // new_owner
    Cell->WriteAddress(RespWorkchain, RespHash);           // response_destination
    Cell->WriteBit(false);                            // no custom_payload
    Cell->WriteVarUint(FwdTonNano, 4);               // forward_amount
    Cell->WriteBit(false);                            // forward_payload inline = none
    return Cell;
}
