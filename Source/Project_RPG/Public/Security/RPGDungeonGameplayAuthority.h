#pragma once

#include "CoreMinimal.h"

class AActor;

/** Irreversible server revocation, not a login requirement: standalone/NPC play remains valid. */
PROJECT_RPG_API bool IsDungeonGameplayRevoked(const AActor* Actor);
