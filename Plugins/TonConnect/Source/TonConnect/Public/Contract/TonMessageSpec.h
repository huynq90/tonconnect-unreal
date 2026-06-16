#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "TonMessageSpec.generated.h"

UENUM(BlueprintType)
enum class ETonFieldType : uint8
{
    UInt8,
    UInt16,
    UInt32,
    UInt64,
    Int8,
    Int16,
    Int32,
    Int64,
    Bool,
    Address,  // addr_std: workchain(8) + hash(256) = 267 bits
    Coins,    // VarUInteger(4) — nanoTON / jetton amounts up to 120 bits
    Bytes,    // BitWidth specifies byte count; Value is a hex string
    Text,     // UTF-8 bytes written inline; Value is a plain string
};

USTRUCT(BlueprintType)
struct TONCONNECT_API FTonFieldSpec
{
    GENERATED_BODY()

    // Field name — used as key when looking up values in the build map
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="TonConnect")
    FString Name;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="TonConnect")
    ETonFieldType Type = ETonFieldType::UInt32;

    // For UInt/Int: explicit bit width (0 = type default: UInt8→8, UInt32→32, etc.)
    // For Bytes: number of bytes to write
    // For Text: maximum byte length (0 = unlimited)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="TonConnect")
    int32 BitWidth = 0;
};

// Blueprint-editable DataAsset describing a single contract message (op + fields).
// Use TonCellBuilder::Build() to turn one of these + a value map into a BOC payload.
UCLASS(BlueprintType)
class TONCONNECT_API UTonMessageSpec : public UDataAsset
{
    GENERATED_BODY()

public:
    // 32-bit operation code written as the first uint32 in every TON message
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="TonConnect", meta=(ClampMin="0"))
    int64 Opcode = 0;

    // Fields after the op code, in order
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="TonConnect")
    TArray<FTonFieldSpec> Fields;
};
