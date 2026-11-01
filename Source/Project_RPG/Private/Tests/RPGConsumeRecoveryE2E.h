#pragma once
#include "CoreMinimal.h"
class UWorld;
class APlayerController;
class ARPGPlayerState;
class APawn;
class UAbilitySystemComponent;
class UGameplayEffect;

/** Single-player opt-in observer; approved damage and read-only observation, never HP writes or item grants. */
struct FRPGConsumeRecoveryServerProbe
{
	~FRPGConsumeRecoveryServerProbe();
	int32 Tick(UWorld& World);
	FString Error;
private:
	bool SameTarget() const;
	bool bInitialized = false, bArmed = false, bLockedObserved = false, bHealed = false, bStopped = false;
	bool bDeathMode = false, bKilled = false, bDeathResolved = false;
	FGuid Source, Request;
	FString Owner, Session, Steam, StopFile, DeathFile;
	int64 Generation = 0;
	float HealthBefore = 0.f;
	int32 Effects = 0;
	TWeakObjectPtr<APlayerController> Controller;
	TWeakObjectPtr<ARPGPlayerState> State;
	TWeakObjectPtr<APawn> Pawn;
	TWeakObjectPtr<UAbilitySystemComponent> Abilities;
	TWeakObjectPtr<const UGameplayEffect> PotionEffect;
	FDelegateHandle EffectHandle;
};
