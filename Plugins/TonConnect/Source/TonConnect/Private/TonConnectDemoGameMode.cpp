#include "TonConnectDemoGameMode.h"
#include "GameFramework/SpectatorPawn.h"
#include "GameFramework/PlayerController.h"

ATonConnectDemoGameMode::ATonConnectDemoGameMode()
{
    // Free-floating camera — no ground collision, WASD + right-click mouse look
    DefaultPawnClass     = ASpectatorPawn::StaticClass();
    PlayerControllerClass = APlayerController::StaticClass();
    HUDClass              = nullptr;
}
