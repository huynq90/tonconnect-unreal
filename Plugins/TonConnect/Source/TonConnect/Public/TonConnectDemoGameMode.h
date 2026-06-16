#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "TonConnectDemoGameMode.generated.h"

// Minimal game mode for the TonConnect example level.
//
// - Free-floating spectator pawn (WASD + mouse look, no gravity)
// - No HUD class — TonConnectDemoActor prints state via AddOnScreenDebugMessage
// - Mock mode enabled by default via TonConnect developer settings
//
// Set this as the default game mode in your demo level's World Settings,
// or run the setup script:
//   Editor menu → File → Execute Python Script → TonConnect/Scripts/setup_example_level.py
UCLASS()
class TONCONNECT_API ATonConnectDemoGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    ATonConnectDemoGameMode();
};
