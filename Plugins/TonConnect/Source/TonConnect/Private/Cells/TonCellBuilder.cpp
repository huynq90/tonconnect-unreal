#include "Contract/TonCellBuilder.h"
#include "Contract/TonMessageSpec.h"
#include "TonUtils.h"

TSharedPtr<FTonCell> FTonCellBuilder::Build(const UTonMessageSpec* Spec,
                                             const TMap<FString, FString>& Values)
{
    if (!Spec) return nullptr;

    auto Cell = MakeShared<FTonCell>();
    Cell->WriteUint((uint64)Spec->Opcode, 32);

    for (const FTonFieldSpec& Field : Spec->Fields)
    {
        const FString Val = Values.Contains(Field.Name) ? Values[Field.Name] : TEXT("");

        switch (Field.Type)
        {
        case ETonFieldType::UInt8:
            Cell->WriteUint(FCString::Strtoui64(*Val, nullptr, 10),
                            Field.BitWidth > 0 ? Field.BitWidth : 8);
            break;

        case ETonFieldType::UInt16:
            Cell->WriteUint(FCString::Strtoui64(*Val, nullptr, 10),
                            Field.BitWidth > 0 ? Field.BitWidth : 16);
            break;

        case ETonFieldType::UInt32:
            Cell->WriteUint(FCString::Strtoui64(*Val, nullptr, 10),
                            Field.BitWidth > 0 ? Field.BitWidth : 32);
            break;

        case ETonFieldType::UInt64:
            Cell->WriteUint(FCString::Strtoui64(*Val, nullptr, 10),
                            Field.BitWidth > 0 ? Field.BitWidth : 64);
            break;

        case ETonFieldType::Int8:
            Cell->WriteInt(FCString::Atoi64(*Val),
                           Field.BitWidth > 0 ? Field.BitWidth : 8);
            break;

        case ETonFieldType::Int16:
            Cell->WriteInt(FCString::Atoi64(*Val),
                           Field.BitWidth > 0 ? Field.BitWidth : 16);
            break;

        case ETonFieldType::Int32:
            Cell->WriteInt(FCString::Atoi64(*Val),
                           Field.BitWidth > 0 ? Field.BitWidth : 32);
            break;

        case ETonFieldType::Int64:
            Cell->WriteInt(FCString::Atoi64(*Val),
                           Field.BitWidth > 0 ? Field.BitWidth : 64);
            break;

        case ETonFieldType::Bool:
            Cell->WriteBit(Val.Equals(TEXT("true"), ESearchCase::IgnoreCase));
            break;

        case ETonFieldType::Address:
        {
            int32 Workchain = 0;
            TArray<uint8> Hash;
            if (FTonAddressUtils::ParseHumanAddress(Val, Workchain, Hash))
                Cell->WriteAddress(Workchain, Hash);
            break;
        }

        case ETonFieldType::Coins:
            Cell->WriteVarUint(FCString::Strtoui64(*Val, nullptr, 10), 4);
            break;

        case ETonFieldType::Bytes:
        {
            TArray<uint8> Bytes = FTonByteUtils::HexToBytes(Val);
            if (Field.BitWidth > 0) Bytes.SetNum(FMath::Min(Bytes.Num(), Field.BitWidth));
            Cell->WriteBytes(Bytes);
            break;
        }

        case ETonFieldType::Text:
        {
            FTCHARToUTF8 Conv(*Val);
            TArray<uint8> Bytes((const uint8*)Conv.Get(), Conv.Length());
            if (Field.BitWidth > 0) Bytes.SetNum(FMath::Min(Bytes.Num(), Field.BitWidth));
            Cell->WriteBytes(Bytes);
            break;
        }
        }
    }

    return Cell;
}
