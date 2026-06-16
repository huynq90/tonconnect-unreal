// Copyright Epic Games, Inc. All Rights Reserved.

#include "UETonConnectExampleGameMode.h"
#include "UETonConnectExampleCharacter.h"
#include "UObject/ConstructorHelpers.h"

AUETonConnectExampleGameMode::AUETonConnectExampleGameMode()
{
	// set default pawn class to our Blueprinted character
	static ConstructorHelpers::FClassFinder<APawn> PlayerPawnBPClass(TEXT("/Game/ThirdPerson/Blueprints/BP_ThirdPersonCharacter"));
	if (PlayerPawnBPClass.Class != NULL)
	{
		DefaultPawnClass = PlayerPawnBPClass.Class;
	}
}
