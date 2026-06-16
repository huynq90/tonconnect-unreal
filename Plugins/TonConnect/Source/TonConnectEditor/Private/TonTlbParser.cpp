#include "TonTlbParser.h"

// -------------------------------------------------------------------
// ParseFile
// -------------------------------------------------------------------
TArray<FTonTlbMessage> FTonTlbParser::ParseFile(const FString& TlbText)
{
    TArray<FTonTlbMessage> Result;

    // Join continuation lines (TL-B allows line breaks mid-definition)
    FString Joined = TlbText.Replace(TEXT("\r\n"), TEXT("\n"))
                             .Replace(TEXT("\r"),   TEXT("\n"));

    // Collapse lines that don't end in ';' with the next one
    TArray<FString> RawLines;
    Joined.ParseIntoArray(RawLines, TEXT("\n"), false);

    TArray<FString> Lines;
    FString Buf;
    for (const FString& Raw : RawLines)
    {
        FString Trimmed = Raw.TrimStartAndEnd();
        if (Trimmed.IsEmpty() || Trimmed.StartsWith(TEXT("//"))) continue;
        if (!Buf.IsEmpty()) Buf += TEXT(" ");
        Buf += Trimmed;
        if (Trimmed.EndsWith(TEXT(";")))
        {
            Lines.Add(Buf);
            Buf.Empty();
        }
    }
    if (!Buf.IsEmpty()) Lines.Add(Buf);

    for (const FString& Line : Lines)
    {
        FTonTlbMessage Msg;
        if (ParseLine(Line, Msg))
            Result.Add(MoveTemp(Msg));
    }
    return Result;
}

// -------------------------------------------------------------------
// ParseLine
// Parse:  constructor_name[#hexOpcode] field:type ... = TypeName;
// -------------------------------------------------------------------
bool FTonTlbParser::ParseLine(const FString& Line, FTonTlbMessage& OutMsg)
{
    // Must contain '=' and end with ';'
    int32 EqIdx = INDEX_NONE;
    if (!Line.FindChar(TEXT('='), EqIdx)) return false;
    FString LHS = Line.Left(EqIdx).TrimStartAndEnd();
    // Strip trailing ';' from RHS (we don't need the result type)

    // First token = constructor name (may include #tag)
    TArray<FString> Tokens;
    LHS.ParseIntoArrayWS(Tokens);
    if (Tokens.Num() == 0) return false;

    FString NamePart = Tokens[0];
    int32 HashIdx = INDEX_NONE;
    if (NamePart.FindChar(TEXT('#'), HashIdx))
    {
        OutMsg.Name   = NamePart.Left(HashIdx);
        FString TagHex = NamePart.Mid(HashIdx + 1);
        if (!TagHex.IsEmpty())
            OutMsg.Opcode = (int64)FCString::Strtoui64(*TagHex, nullptr, 16);
    }
    else
    {
        OutMsg.Name = NamePart;
    }

    if (OutMsg.Name.IsEmpty()) return false;

    // Remaining tokens: field:type pairs (some types have spaces, e.g. "(VarUInteger 16)")
    // Strategy: re-join then scan for "word:" patterns
    FString FieldStr;
    for (int32 i = 1; i < Tokens.Num(); ++i)
    {
        if (!FieldStr.IsEmpty()) FieldStr += TEXT(" ");
        FieldStr += Tokens[i];
    }

    // Split on "(\w+):" boundaries — each segment is one field
    // We'll do a simple character scan
    int32 Len = FieldStr.Len();
    int32 Pos = 0;

    auto SkipWhitespace = [&]()
    {
        while (Pos < Len && (FieldStr[Pos] == TEXT(' ') || FieldStr[Pos] == TEXT('\t'))) ++Pos;
    };

    auto ReadIdentifier = [&]() -> FString
    {
        int32 Start = Pos;
        while (Pos < Len && (FChar::IsAlnum(FieldStr[Pos]) || FieldStr[Pos] == TEXT('_') ||
               FieldStr[Pos] == TEXT('-'))) ++Pos;
        return FieldStr.Mid(Start, Pos - Start);
    };

    auto ReadTypeToken = [&]() -> FString
    {
        // Read until the next "identifier:" pattern or end
        int32 Start = Pos;
        while (Pos < Len)
        {
            // Peek: if current char starts an identifier and is followed by ':', stop
            if (FChar::IsAlpha(FieldStr[Pos]) || FieldStr[Pos] == TEXT('_'))
            {
                // Look ahead for ':'
                int32 LookAhead = Pos;
                while (LookAhead < Len &&
                       (FChar::IsAlnum(FieldStr[LookAhead]) || FieldStr[LookAhead] == TEXT('_')))
                    ++LookAhead;
                if (LookAhead < Len && FieldStr[LookAhead] == TEXT(':'))
                    break; // next field starts here
            }
            ++Pos;
        }
        return FieldStr.Mid(Start, Pos - Start).TrimStartAndEnd();
    };

    while (Pos < Len)
    {
        SkipWhitespace();
        if (Pos >= Len) break;

        FString FieldName = ReadIdentifier();
        SkipWhitespace();
        if (Pos >= Len || FieldStr[Pos] != TEXT(':') || FieldName.IsEmpty()) break;
        ++Pos; // consume ':'

        FString TypeStr = ReadTypeToken();
        if (TypeStr.IsEmpty()) continue;

        ETonFieldType FieldType;
        int32 BitWidth = 0;
        if (!MapType(TypeStr, FieldType, BitWidth)) continue;

        FTonFieldSpec Spec;
        Spec.Name     = FieldName;
        Spec.Type     = FieldType;
        Spec.BitWidth = BitWidth;
        OutMsg.Fields.Add(Spec);
    }

    return true;
}

// -------------------------------------------------------------------
// MapType
// -------------------------------------------------------------------
bool FTonTlbParser::MapType(const FString& TlbType, ETonFieldType& OutType, int32& OutBits)
{
    FString T = TlbType.TrimStartAndEnd().ToLower();

    // Skip types we can't represent or that are refs/optionals
    if (T.StartsWith(TEXT("^"))        ||   // cell reference
        T.StartsWith(TEXT("(maybe"))   ||   // optional — too complex
        T == TEXT("cell")              ||
        T == TEXT("^cell"))
    {
        return false;
    }

    // MsgAddress / addr_std → Address
    if (T == TEXT("msgaddress") || T == TEXT("addr_std"))
    {
        OutType = ETonFieldType::Address;
        OutBits = 0;
        return true;
    }

    // Bool
    if (T == TEXT("bool") || T == TEXT("boolean"))
    {
        OutType = ETonFieldType::Bool;
        OutBits = 0;
        return true;
    }

    // (VarUInteger N) or VarUInteger — used for Coins/nanoTON
    if (T.Contains(TEXT("varuinteger")) || T.Contains(TEXT("varint")) ||
        T == TEXT("coins") || T == TEXT("grams"))
    {
        OutType = ETonFieldType::Coins;
        OutBits = 0;
        return true;
    }

    // uint<N>
    if (T.StartsWith(TEXT("uint")))
    {
        int32 N = FCString::Atoi(*T.Mid(4));
        OutBits = (N > 0 && N <= 64) ? (N < 8 || N == 8 || N == 16 || N == 32 || N == 64 ? 0 : N) : 0;
        if      (N <= 8)  OutType = ETonFieldType::UInt8;
        else if (N <= 16) OutType = ETonFieldType::UInt16;
        else if (N <= 32) OutType = ETonFieldType::UInt32;
        else              OutType = ETonFieldType::UInt64;
        if (OutBits == N && (N == 8 || N == 16 || N == 32 || N == 64)) OutBits = 0;
        else OutBits = N;
        return true;
    }

    // int<N>
    if (T.StartsWith(TEXT("int")))
    {
        int32 N = FCString::Atoi(*T.Mid(3));
        if      (N <= 8)  OutType = ETonFieldType::Int8;
        else if (N <= 16) OutType = ETonFieldType::Int16;
        else if (N <= 32) OutType = ETonFieldType::Int32;
        else              OutType = ETonFieldType::Int64;
        OutBits = (N == 8 || N == 16 || N == 32 || N == 64) ? 0 : N;
        return true;
    }

    // bits<N> → Bytes (N/8 bytes)
    if (T.StartsWith(TEXT("bits")))
    {
        int32 N = FCString::Atoi(*T.Mid(4));
        OutType = ETonFieldType::Bytes;
        OutBits = (N > 0) ? (N / 8) : 0;
        return true;
    }

    // (Either Cell ^Cell) — forward_payload pattern — skip the Either wrapper
    if (T.StartsWith(TEXT("(either")))
    {
        return false;
    }

    return false;
}
