#include "Cells/TonWalletMessage.h"
#include "Cells/TonCell.h"

// ── Internal message (MessageRelaxed) ─────────────────────────────────────────

TSharedPtr<FTonCell> FTonWalletMessage::BuildInternalMessage(
    int32 DestWc, const TArray<uint8>& DestHash,
    uint64 AmountNano, bool bBounce, TSharedPtr<FTonCell> Body)
{
    if (DestHash.Num() != 32) return nullptr;

    TSharedPtr<FTonCell> Int = MakeShared<FTonCell>();
    Int->WriteBit(false);             // int_msg_info$0
    Int->WriteBit(true);              // ihr_disabled = 1
    Int->WriteBit(bBounce);           // bounce
    Int->WriteBit(false);             // bounced = 0
    Int->WriteUint(0, 2);             // src = addr_none$00
    Int->WriteAddress(DestWc, DestHash); // dest (addr_std)
    Int->WriteVarUint(AmountNano, 4); // value: grams
    Int->WriteBit(false);             // empty extra-currency dict
    Int->WriteVarUint(0, 4);          // ihr_fee = 0
    Int->WriteVarUint(0, 4);          // fwd_fee = 0
    Int->WriteUint(0, 64);            // created_lt = 0
    Int->WriteUint(0, 32);            // created_at = 0
    Int->WriteBit(false);             // init: nothing

    if (Body.IsValid())
    {
        Int->WriteBit(true);          // body: Either right (^) → ref
        Int->AddRef(Body);
    }
    else
    {
        Int->WriteBit(false);         // body: Either left (inline), empty
    }
    return Int;
}

// ── v4r2 wrapper ──────────────────────────────────────────────────────────────

static const uint64 WalletV4SubwalletId = 698983191ull;

static FString WrapV4R2(int32 WalletWc, const TArray<uint8>& WalletHash,
                        int32 Seqno, int64 ValidUntil, TSharedPtr<FTonCell> Int)
{
    // Signed body: signature(512 zeros) + subwallet | valid_until | seqno | op=0 | mode | ^msg
    TSharedPtr<FTonCell> Body = MakeShared<FTonCell>();
    TArray<uint8> ZeroSig; ZeroSig.SetNumZeroed(64);
    Body->WriteBytes(ZeroSig);
    Body->WriteUint(WalletV4SubwalletId, 32);
    Body->WriteUint((uint64)ValidUntil, 32);
    Body->WriteUint((uint64)Seqno, 32);
    Body->WriteUint(0, 8);            // op = 0 (simple transfer)
    Body->WriteUint(3, 8);            // send_mode = 3
    Body->AddRef(Int);

    TSharedPtr<FTonCell> Ext = MakeShared<FTonCell>();
    Ext->WriteUint(2, 2);             // ext_in_msg_info$10
    Ext->WriteUint(0, 2);             // src addr_none
    Ext->WriteAddress(WalletWc, WalletHash);
    Ext->WriteVarUint(0, 4);          // import_fee = 0
    Ext->WriteBit(false);             // init: nothing
    Ext->WriteBit(true);              // body in ref
    Ext->AddRef(Body);
    return Ext->ToBocBase64();
}

// ── v5r1 (W5) wrapper ─────────────────────────────────────────────────────────

// V5R1 wallet_id = networkGlobalId XOR context, context(wc=0,ver=0,subwallet=0) = 0x80000000.
// mainnet(-239) → 0x7FFFFF11 ; testnet(-3) → 0x7FFFFFFD.
static uint32 WalletV5Id(bool bTestnet)
{
    const uint32 Context = 0x80000000u;
    const uint32 NetworkGlobalId = bTestnet ? 0xFFFFFFFDu /*-3*/ : 0xFFFFFF11u /*-239*/;
    return NetworkGlobalId ^ Context;
}

static FString WrapV5R1(bool bTestnet, int32 WalletWc, const TArray<uint8>& WalletHash,
                        int32 Seqno, int64 ValidUntil, TSharedPtr<FTonCell> Int)
{
    // out_list with a single action: out_list$_ prev:^(empty) action:action_send_msg
    // action_send_msg#0ec3c86d mode:uint8 out_msg:^MessageRelaxed
    TSharedPtr<FTonCell> Empty = MakeShared<FTonCell>(); // out_list_empty
    TSharedPtr<FTonCell> Actions = MakeShared<FTonCell>();
    Actions->AddRef(Empty);           // prev (empty list)
    Actions->WriteUint(0x0ec3c86d, 32); // action_send_msg
    Actions->WriteUint(3, 8);         // send_mode = 3
    Actions->AddRef(Int);             // out_msg

    // Signed body: opcode | walletId | valid_until | seqno | (Maybe ^actions) | extended=0 | signature(512)
    TSharedPtr<FTonCell> Body = MakeShared<FTonCell>();
    Body->WriteUint(0x7369676e, 32);  // external signed request ("sign")
    Body->WriteUint(WalletV5Id(bTestnet), 32);
    Body->WriteUint((uint64)ValidUntil, 32);
    Body->WriteUint((uint64)Seqno, 32);
    Body->WriteBit(true);             // actions: Maybe present
    Body->WriteBit(false);            // extended actions: none
    Body->AddRef(Actions);            // the Maybe ^OutList ref
    TArray<uint8> ZeroSig; ZeroSig.SetNumZeroed(64);
    Body->WriteBytes(ZeroSig);        // signature appended at the END

    TSharedPtr<FTonCell> Ext = MakeShared<FTonCell>();
    Ext->WriteUint(2, 2);             // ext_in_msg_info$10
    Ext->WriteUint(0, 2);             // src addr_none
    Ext->WriteAddress(WalletWc, WalletHash);
    Ext->WriteVarUint(0, 4);          // import_fee = 0
    Ext->WriteBit(false);             // init: nothing
    Ext->WriteBit(true);              // body in ref
    Ext->AddRef(Body);
    return Ext->ToBocBase64();
}

// ── Dispatch ──────────────────────────────────────────────────────────────────

FString FTonWalletMessage::WrapExternalForEmulate(
    const FString& WalletVersion, bool bTestnet,
    int32 WalletWc, const TArray<uint8>& WalletHash,
    int32 Seqno, int64 ValidUntilUnix, TSharedPtr<FTonCell> InternalMessage)
{
    if (WalletHash.Num() != 32 || !InternalMessage.IsValid()) return FString();

    if (WalletVersion.StartsWith(TEXT("V4")))
        return WrapV4R2(WalletWc, WalletHash, Seqno, ValidUntilUnix, InternalMessage);

    if (WalletVersion.StartsWith(TEXT("W5")) || WalletVersion.StartsWith(TEXT("V5")))
        return WrapV5R1(bTestnet, WalletWc, WalletHash, Seqno, ValidUntilUnix, InternalMessage);

    return FString(); // unsupported version
}
