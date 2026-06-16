#include "Modules/ModuleManager.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "TonAbiFactory.h"
#include "TonConnectDemoActor.h"
#include "TonConnectUIDemoActor.h"
#include "TonConnectDemoGameMode.h"
#include "ToolMenus.h"
#include "LevelEditorSubsystem.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "GameFramework/PlayerStart.h"
#include "Misc/MessageDialog.h"
#include "Misc/PackageName.h"
#include "Editor.h"

// ─── Generic level builder ────────────────────────────────────────────────────

// Builds a small playable level: sky/sun template + a demo actor + PlayerStart,
// with TonConnectDemoGameMode as the world game mode. Saves and reports the path.
static void BuildExampleLevel(const FString& LevelPath, UClass* DemoActorClass,
                              const FString& ActorLabel, const FString& Instructions)
{
    if (!GEditor) return;

    ULevelEditorSubsystem* LevelSub = GEditor->GetEditorSubsystem<ULevelEditorSubsystem>();
    UEditorActorSubsystem* ActorSub = GEditor->GetEditorSubsystem<UEditorActorSubsystem>();
    if (!LevelSub || !ActorSub)
    {
        FMessageDialog::Open(EAppMsgType::Ok,
            FText::FromString(TEXT("Cannot create level: editor subsystems not available.")));
        return;
    }

    // Already exists → offer to open
    if (FPackageName::DoesPackageExist(LevelPath))
    {
        const EAppReturnType::Type Ans = FMessageDialog::Open(EAppMsgType::YesNo,
            FText::FromString(TEXT("Level already exists:\n") + LevelPath + TEXT("\n\nOpen it now?")));
        if (Ans == EAppReturnType::Yes)
            LevelSub->LoadLevel(LevelPath);
        return;
    }

    bool bCreated = LevelSub->NewLevelFromTemplate(
        LevelPath, TEXT("/Engine/Maps/Templates/Template_Default"));
    if (!bCreated)
        bCreated = LevelSub->NewLevel(LevelPath);

    if (!bCreated)
    {
        FMessageDialog::Open(EAppMsgType::Ok,
            FText::FromString(TEXT("Failed to create level. Check the Output Log.")));
        return;
    }

    if (DemoActorClass)
    {
        if (AActor* Demo = ActorSub->SpawnActorFromClass(
                DemoActorClass, FVector::ZeroVector, FRotator::ZeroRotator))
            Demo->SetActorLabel(ActorLabel);
    }

    if (AActor* PS = ActorSub->SpawnActorFromClass(
            APlayerStart::StaticClass(), FVector(0.f, -800.f, 200.f), FRotator(-10.f, 90.f, 0.f)))
        PS->SetActorLabel(TEXT("PlayerStart"));

    if (UWorld* World = GEditor->GetEditorWorldContext().World())
    {
        if (AWorldSettings* WS = World->GetWorldSettings())
            WS->DefaultGameMode = ATonConnectDemoGameMode::StaticClass();
    }

    LevelSub->SaveCurrentLevel();

    FMessageDialog::Open(EAppMsgType::Ok,
        FText::FromString(TEXT("Level created!\n\n") + LevelPath + TEXT("\n\n") + Instructions));
}

static void CreateKeyboardExampleLevel()
{
    BuildExampleLevel(
        TEXT("/TonConnect/Maps/TonConnectExample"),
        ATonConnectDemoActor::StaticClass(),
        TEXT("TonConnect_Demo"),
        TEXT("Press Play to test.\n"
             "Keys:  1 = Connect   2 = Send   3 = Disconnect   4 = Send to self\n\n"
             "Enable mock mode: Project Settings -> TonConnect -> Use Mock = true (or -ton.mock)"));
}

static void CreateUIExampleLevel()
{
    BuildExampleLevel(
        TEXT("/TonConnect/Maps/TonConnectUIExample"),
        ATonConnectUIDemoActor::StaticClass(),
        TEXT("TonConnect_UI"),
        TEXT("Press Play, then press [T] to open the wallet popup.\n"
             "Connect -> scan QR -> Send TON (with loading) -> Disconnect.\n"
             "Results show up as on-screen toasts.\n\n"
             "Enable mock mode: Project Settings -> TonConnect -> Use Mock = true (or -ton.mock)"));
}

// ─── Editor module ────────────────────────────────────────────────────────────

class FTonConnectEditorModule : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        UE_LOG(LogTemp, Log, TEXT("TonConnectEditor: module loaded. "
               "Drag .abi.json or .tlb files into the Content Browser to import UTonMessageSpec assets."));

        UToolMenus::RegisterStartupCallback(
            FSimpleMulticastDelegate::FDelegate::CreateRaw(
                this, &FTonConnectEditorModule::RegisterMenus));
    }

    virtual void ShutdownModule() override
    {
        UToolMenus::UnRegisterStartupCallback(this);
    }

private:
    void RegisterMenus()
    {
        UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");
        FToolMenuSection& Section = Menu->FindOrAddSection(
            "TonConnect", FText::FromString(TEXT("TON Connect")));

        Section.AddMenuEntry(
            "TonConnect_CreateExampleLevel",
            FText::FromString(TEXT("Create Example Level (keyboard)")),
            FText::FromString(TEXT("Creates /TonConnect/Maps/TonConnectExample — on-screen log + number-key controls")),
            FSlateIcon(),
            FUIAction(FExecuteAction::CreateStatic(&CreateKeyboardExampleLevel)));

        Section.AddMenuEntry(
            "TonConnect_CreateUIExampleLevel",
            FText::FromString(TEXT("Create Example Level (UI popup)")),
            FText::FromString(TEXT("Creates /TonConnect/Maps/TonConnectUIExample — full wallet popup (press T), QR, Send, toasts")),
            FSlateIcon(),
            FUIAction(FExecuteAction::CreateStatic(&CreateUIExampleLevel)));
    }
};

IMPLEMENT_MODULE(FTonConnectEditorModule, TonConnectEditor)
