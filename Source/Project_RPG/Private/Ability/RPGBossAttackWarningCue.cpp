#include "Ability/RPGBossAttackWarningCue.h"

#include "Character/RPGBossCharacter.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "NativeGameplayTags.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_RaidAttackWarning, "GameplayCue.Boss.AttackWarning");

URPGBossAttackWarningCue::URPGBossAttackWarningCue()
{
	GameplayCueTag = TAG_RaidAttackWarning;
}

bool URPGBossAttackWarningCue::OnExecute_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters) const
{
	const ARPGBossCharacter* Boss = Cast<ARPGBossCharacter>(MyTarget);
	UWorld* World = IsValid(Boss) ? Boss->GetWorld() : nullptr;
	if (!World) { return false; }
#if !UE_BUILD_SHIPPING
	if (FParse::Param(FCommandLine::Get(), TEXT("RPGBossMapE2EAttackPresentation")))
		UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_BOSS_ATTACK_CUE Observed=1 Target=%s NetMode=%d CosmeticOnly=1"), *Boss->GetName(), World->GetNetMode());
#endif
	if (World->GetNetMode() != NM_DedicatedServer && WarningSystem)
	{
		// Fire-and-forget Niagara lifetime; no gameplay tags, collision or damage changes.
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, WarningSystem,
			Boss->GetActorLocation() + FVector(0.f, 0.f, 120.f), Boss->GetActorRotation());
	}
	return true;
}
