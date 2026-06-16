#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "TonAbiFactory.generated.h"

// Import factory that reads a Tact ABI .json file and creates a UTonMessageSpec asset
// for each message type found in the file.
// Supports both Tact ABI format and a minimal Blueprint wrapper JSON format.
UCLASS()
class TONCONNECTEDITOR_API UTonAbiFactory : public UFactory
{
    GENERATED_BODY()

public:
    UTonAbiFactory();

    virtual UObject* FactoryCreateFile(UClass* InClass, UObject* InParent, FName InName,
                                       EObjectFlags Flags, const FString& Filename,
                                       const TCHAR* Parms, FFeedbackContext* Warn,
                                       bool& bOutOperationCancelled) override;

    virtual bool FactoryCanImport(const FString& Filename) override;
    virtual FText GetToolTip() const override;
};

// Import factory for .tlb (TL-B scheme) files
UCLASS()
class TONCONNECTEDITOR_API UTonTlbFactory : public UFactory
{
    GENERATED_BODY()

public:
    UTonTlbFactory();

    virtual UObject* FactoryCreateFile(UClass* InClass, UObject* InParent, FName InName,
                                       EObjectFlags Flags, const FString& Filename,
                                       const TCHAR* Parms, FFeedbackContext* Warn,
                                       bool& bOutOperationCancelled) override;

    virtual bool FactoryCanImport(const FString& Filename) override;
    virtual FText GetToolTip() const override;
};
