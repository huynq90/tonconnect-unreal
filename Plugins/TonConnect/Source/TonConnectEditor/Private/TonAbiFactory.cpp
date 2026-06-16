#include "TonAbiFactory.h"
#include "TonTlbParser.h"
#include "Contract/TonMessageSpec.h"
#include "Misc/FileHelper.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Subsystems/ImportSubsystem.h"
#include "Editor.h"

// -----------------------------------------------------------------------
//  Helpers
// -----------------------------------------------------------------------

static ETonFieldType TactTypeToUE(const FString& TactType, int32& OutBits)
{
    OutBits = 0;
    FString T = TactType.TrimStartAndEnd().ToLower();

    if (T == TEXT("bool"))                        return ETonFieldType::Bool;
    if (T == TEXT("address"))                     return ETonFieldType::Address;
    if (T == TEXT("cell") || T == TEXT("slice"))  return ETonFieldType::Bytes;
    if (T == TEXT("string") || T == TEXT("stringbuilder")) return ETonFieldType::Text;
    if (T == TEXT("coins") || T == TEXT("nanoton")) return ETonFieldType::Coins;

    if (T.StartsWith(TEXT("uint")))
    {
        int32 N = FCString::Atoi(*T.Mid(4));
        OutBits = (N == 8 || N == 16 || N == 32 || N == 64) ? 0 : N;
        if (N <= 8)  return ETonFieldType::UInt8;
        if (N <= 16) return ETonFieldType::UInt16;
        if (N <= 32) return ETonFieldType::UInt32;
        return ETonFieldType::UInt64;
    }
    if (T.StartsWith(TEXT("int")))
    {
        int32 N = FCString::Atoi(*T.Mid(3));
        OutBits = (N == 8 || N == 16 || N == 32 || N == 64) ? 0 : N;
        if (N <= 8)  return ETonFieldType::Int8;
        if (N <= 16) return ETonFieldType::Int16;
        if (N <= 32) return ETonFieldType::Int32;
        return ETonFieldType::Int64;
    }
    return ETonFieldType::UInt32; // fallback
}

static FString SanitizeAssetName(const FString& Raw)
{
    FString Out = Raw;
    for (TCHAR& C : Out)
        if (!FChar::IsAlnum(C) && C != TEXT('_')) C = TEXT('_');
    if (Out.IsEmpty()) Out = TEXT("TonMessage");
    if (FChar::IsDigit(Out[0])) Out = TEXT("Msg_") + Out;
    return Out;
}

static UTonMessageSpec* CreateSpec(UObject* InParent, const FString& Name, EObjectFlags Flags)
{
    FName AssetName = FName(*SanitizeAssetName(Name));
    return NewObject<UTonMessageSpec>(InParent, UTonMessageSpec::StaticClass(), AssetName, Flags);
}

// -----------------------------------------------------------------------
//  Parse a Tact ABI "messages" array entry
// -----------------------------------------------------------------------
// Tact ABI example:
// { "name": "Transfer", "opcode": "0xf8a7ea5", "fields": [
//     {"name":"amount","type":{"kind":"simple","type":"Int","format":"coins"}},
//     {"name":"recipient","type":{"kind":"simple","type":"Address"}}
// ]}
static bool ParseTactMessage(const TSharedPtr<FJsonObject>& Obj, FString& OutName, int64& OutOpcode,
                              TArray<FTonFieldSpec>& OutFields)
{
    Obj->TryGetStringField(TEXT("name"), OutName);
    if (OutName.IsEmpty()) return false;

    FString OpcodeStr;
    if (Obj->TryGetStringField(TEXT("opcode"), OpcodeStr) && !OpcodeStr.IsEmpty())
        OutOpcode = (int64)FCString::Strtoui64(*OpcodeStr, nullptr, 0);
    else
    {
        double N = 0;
        Obj->TryGetNumberField(TEXT("opcode"), N);
        OutOpcode = (int64)N;
    }

    const TArray<TSharedPtr<FJsonValue>>* Fields;
    if (!Obj->TryGetArrayField(TEXT("fields"), Fields)) return true;

    for (const TSharedPtr<FJsonValue>& FVal : *Fields)
    {
        const TSharedPtr<FJsonObject>* FObj;
        if (!FVal->TryGetObject(FObj)) continue;

        FTonFieldSpec Spec;
        (*FObj)->TryGetStringField(TEXT("name"), Spec.Name);
        if (Spec.Name.IsEmpty()) continue;

        // Field type can be nested: { "kind": "simple", "type": "Int", "format": "coins" }
        const TSharedPtr<FJsonObject>* TypeObj;
        FString TypeStr;
        FString FormatStr;
        if ((*FObj)->TryGetObjectField(TEXT("type"), TypeObj))
        {
            (*TypeObj)->TryGetStringField(TEXT("type"), TypeStr);
            (*TypeObj)->TryGetStringField(TEXT("format"), FormatStr);
        }
        else
        {
            (*FObj)->TryGetStringField(TEXT("type"), TypeStr);
        }

        if (!FormatStr.IsEmpty()) TypeStr = FormatStr; // "coins" overrides "Int"

        int32 Bits = 0;
        Spec.Type     = TactTypeToUE(TypeStr, Bits);
        Spec.BitWidth = Bits;
        OutFields.Add(Spec);
    }
    return true;
}

// -----------------------------------------------------------------------
//  UTonAbiFactory
// -----------------------------------------------------------------------

UTonAbiFactory::UTonAbiFactory()
{
    bCreateNew    = false;
    bEditAfterNew = true;
    bEditorImport = true;
    SupportedClass = UTonMessageSpec::StaticClass();
    Formats.Add(TEXT("json;Tact ABI JSON"));
    Formats.Add(TEXT("abi.json;Tact ABI JSON"));
}

bool UTonAbiFactory::FactoryCanImport(const FString& Filename)
{
    return Filename.EndsWith(TEXT(".json"), ESearchCase::IgnoreCase);
}

FText UTonAbiFactory::GetToolTip() const
{
    return FText::FromString(TEXT("Import Tact ABI JSON and create UTonMessageSpec assets"));
}

UObject* UTonAbiFactory::FactoryCreateFile(UClass* InClass, UObject* InParent, FName InName,
                                            EObjectFlags Flags, const FString& Filename,
                                            const TCHAR* /*Parms*/, FFeedbackContext* Warn,
                                            bool& bOutOperationCancelled)
{
    bOutOperationCancelled = false;

    FString JsonStr;
    if (!FFileHelper::LoadFileToString(JsonStr, *Filename))
    {
        if (Warn) Warn->Logf(ELogVerbosity::Error, TEXT("TonConnect: failed to load %s"), *Filename);
        return nullptr;
    }

    TSharedPtr<FJsonObject> Root;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonStr);
    if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
    {
        if (Warn) Warn->Logf(ELogVerbosity::Error, TEXT("TonConnect: invalid JSON in %s"), *Filename);
        return nullptr;
    }

    // Collect message definitions — support both "messages" and "types" arrays
    TArray<TSharedPtr<FJsonValue>> MsgArray;
    {
        const TArray<TSharedPtr<FJsonValue>>* Arr;
        if (Root->TryGetArrayField(TEXT("messages"), Arr)) MsgArray = *Arr;
        else if (Root->TryGetArrayField(TEXT("types"), Arr))  MsgArray = *Arr;
    }

    if (MsgArray.IsEmpty())
    {
        if (Warn) Warn->Logf(ELogVerbosity::Warning,
            TEXT("TonConnect: no 'messages' or 'types' array found in %s"), *Filename);
        return nullptr;
    }

    // First created asset is returned as the primary import result
    UTonMessageSpec* FirstSpec = nullptr;

    for (const TSharedPtr<FJsonValue>& Val : MsgArray)
    {
        const TSharedPtr<FJsonObject>* ObjPtr;
        if (!Val->TryGetObject(ObjPtr)) continue;

        FString MsgName;
        int64 Opcode = 0;
        TArray<FTonFieldSpec> Fields;
        if (!ParseTactMessage(*ObjPtr, MsgName, Opcode, Fields)) continue;

        UTonMessageSpec* Spec = CreateSpec(InParent, MsgName, Flags);
        Spec->Opcode = Opcode;
        Spec->Fields = MoveTemp(Fields);

        FAssetRegistryModule::AssetCreated(Spec);
        if (!FirstSpec) FirstSpec = Spec;
    }

    return FirstSpec;
}

// -----------------------------------------------------------------------
//  UTonTlbFactory
// -----------------------------------------------------------------------

UTonTlbFactory::UTonTlbFactory()
{
    bCreateNew    = false;
    bEditAfterNew = true;
    bEditorImport = true;
    SupportedClass = UTonMessageSpec::StaticClass();
    Formats.Add(TEXT("tlb;TL-B Scheme File"));
}

bool UTonTlbFactory::FactoryCanImport(const FString& Filename)
{
    return Filename.EndsWith(TEXT(".tlb"), ESearchCase::IgnoreCase);
}

FText UTonTlbFactory::GetToolTip() const
{
    return FText::FromString(TEXT("Import TL-B scheme and create UTonMessageSpec assets"));
}

UObject* UTonTlbFactory::FactoryCreateFile(UClass* InClass, UObject* InParent, FName InName,
                                            EObjectFlags Flags, const FString& Filename,
                                            const TCHAR* /*Parms*/, FFeedbackContext* Warn,
                                            bool& bOutOperationCancelled)
{
    bOutOperationCancelled = false;

    FString TlbText;
    if (!FFileHelper::LoadFileToString(TlbText, *Filename))
    {
        if (Warn) Warn->Logf(ELogVerbosity::Error, TEXT("TonConnect: failed to load %s"), *Filename);
        return nullptr;
    }

    TArray<FTonTlbMessage> Messages = FTonTlbParser::ParseFile(TlbText);
    if (Messages.IsEmpty())
    {
        if (Warn) Warn->Logf(ELogVerbosity::Warning,
            TEXT("TonConnect: no parseable constructors in %s"), *Filename);
        return nullptr;
    }

    UTonMessageSpec* FirstSpec = nullptr;

    for (FTonTlbMessage& Msg : Messages)
    {
        if (Msg.Fields.IsEmpty()) continue; // skip bare type aliases

        UTonMessageSpec* Spec = CreateSpec(InParent, Msg.Name, Flags);
        Spec->Opcode = Msg.Opcode;
        Spec->Fields = MoveTemp(Msg.Fields);

        FAssetRegistryModule::AssetCreated(Spec);
        if (!FirstSpec) FirstSpec = Spec;
    }

    return FirstSpec;
}
