#pragma once

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"

namespace RPGTestWorld
{
/** End actors while their physics scene still exists, then release the test world.
 * DestroyWorld alone bypasses EndPlay. In UE 5.8 this leaves Water's async
 * buoyancy outputs referring to the deleted physics solver until a later GC.
 */
inline void Destroy(UWorld* World)
{
	if (!World) return;
	if (!World->EndPlay(EEndPlayReason::Quit))
	{
		// A fixture may route actor BeginPlay without fully starting its world.
		for (TActorIterator<AActor> It(World); It; ++It) It->RouteEndPlay(EEndPlayReason::Quit);
	}
	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
}
}
