using UnrealBuildTool;
using System.IO;

public class TonConnect : ModuleRules
{
    public TonConnect(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "HTTP",
            "Json",
            "JsonUtilities",
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "Slate",
            "SlateCore",
            "UMG",
            "DeveloperSettings",
            "InputCore",
            "ImageWrapper",   // decode wallet icons (PNG/JPEG) → UTexture2D
        });

        // tweetnacl — compiled as C source via TonCryptoAdapter.cpp (no prebuilt lib needed)
        string TweetNaClDir = Path.Combine(ModuleDirectory, "ThirdParty/tweetnacl");
        PublicSystemIncludePaths.Add(TweetNaClDir);

        // qrcodegen — header-only C++ lib
        string QrDir = Path.Combine(ModuleDirectory, "ThirdParty/qrcodegen");
        PublicSystemIncludePaths.Add(QrDir);

        // Windows system libs: BCrypt (CSPRNG) + Crypt32 (DPAPI session store)
        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            PublicSystemLibraries.Add("bcrypt.lib");
            PublicSystemLibraries.Add("Crypt32.lib");
        }

        // iOS: Security.framework for Keychain session store
        if (Target.Platform == UnrealTargetPlatform.IOS)
        {
            PublicFrameworks.Add("Security");
            PublicFrameworks.Add("Foundation");
        }

        // Gauntlet: always linked so UHT can process TonConnectGauntletController.h
        // The controller is only instantiated when launched with -gauntlet flag.
        PrivateDependencyModuleNames.Add("Gauntlet");

        // Suppress warnings from third-party C code
        UndefinedIdentifierWarningLevel = WarningLevel.Off;
    }
}
