#pragma once

#include "CoreMinimal.h"
#include "GameplayEffectTypes.h"
#include "Engine/HitResult.h"

class APlayerController;
class ARPGNonPlayerCharacter;
class ARPGSkillDamageSecurityTestPawn;
class URPGAbilitySystemComponent;

// Opt-in observer for saved NPC death/forced replacement, independent of Charge.
// No client damage RPC, invented HP/tag, or client ability activation.
class FRPGNPCDeathNetworkProbe
{
public:
	~FRPGNPCDeathNetworkProbe();
	void Tick(APlayerController& Controller);
	bool Passed() const { return bPassed; }
	const FString& Failure() const { return FailureReason; }
private:
#if !UE_BUILD_SHIPPING
	struct FObservation { int32 Activated = 0; int32 Ended = 0; bool Cancelled = false; bool ZeroNotified = false; };
	void TickServer(APlayerController& Controller);
	void TickClient(APlayerController& Controller);
	ARPGNonPlayerCharacter* SpawnNPC(UWorld& World, FName Name);
	bool Apply(ARPGNonPlayerCharacter& Target, float Base);
	void Fail(const TCHAR* Reason);
	TWeakObjectPtr<ARPGNonPlayerCharacter> OldNPC, NewNPC;
	TWeakObjectPtr<ARPGSkillDamageSecurityTestPawn> Source;
	TWeakObjectPtr<URPGAbilitySystemComponent> ObservedASC;
	TSharedRef<FObservation> Observation = MakeShared<FObservation>();
	FDelegateHandle ActivatedHandle, EndedHandle, HealthHandle;
	FHitResult OldHit;
	FGameplayEffectSpecHandle OldSpec;
	UClass* NPCClass = nullptr;
	UClass* DamageClass = nullptr;
	uint64 OldActorNetId = 0, OldASCNetId = 0;
	float Baseline = 0.f, NewBaseline = 0.f;
	double NextStepAt = 0.;
	int32 Stage = 0;
	bool bDeathMontageObserved = false;
#endif
	bool bPassed = false;
	FString FailureReason;
};
