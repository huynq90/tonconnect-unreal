#pragma once

#include "CoreMinimal.h"
#include "Contract/TonMessageSpec.h"

// Represents one parsed TL-B constructor.
struct TONCONNECTEDITOR_API FTonTlbMessage
{
    FString Name;           // constructor name (e.g. "transfer")
    int64   Opcode = 0;     // 32-bit tag from #xxxxxxxx (0 if absent)
    TArray<FTonFieldSpec> Fields;
};

// Lightweight TL-B parser.
// Handles the subset of TL-B used by TEP standard contracts and Tact-generated schemes:
//   uint<N>, int<N>, bits<N>, MsgAddress (addr_std), (VarUInteger <N>),
//   Bool, (Maybe <T>) [skipped], (Either <T> ^<T>) [first branch only], ^Cell [skipped].
// Does NOT support nested type definitions, combined constructors, or type parameters.
struct TONCONNECTEDITOR_API FTonTlbParser
{
    // Parse a full .tlb file content. Returns all recognised constructors.
    static TArray<FTonTlbMessage> ParseFile(const FString& TlbText);

    // Parse a single constructor line:  "name#tag field:type ... = Type;"
    // Returns false if the line doesn't match the expected pattern.
    static bool ParseLine(const FString& Line, FTonTlbMessage& OutMsg);

private:
    // Map a TL-B type string to ETonFieldType + BitWidth.
    // Returns false for types we skip (Maybe, ^Cell, etc.).
    static bool MapType(const FString& TlbType, ETonFieldType& OutType, int32& OutBits);
};
