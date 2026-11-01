#include "Tests/RPGDungeonFlowE2EProbe.h"

#include "AbilitySystemComponent.h"
#include "Attribute/RPGAttributeSet.h"
#include "Character/RPGBossCharacter.h"
#include "Character/RPGPlayer.h"
#include "Engine/NetConnection.h"
#include "Engine/World.h"
#include "FunctionLibrary/RPGSecurityBlueprintLibrary.h"
#include "GameFramework/PlayerController.h"
#include "GameState/RPGBossBattleGameState.h"
#include "HAL/FileManager.h"
#include "Player/RPGPlayerState.h"
#include "RPGGameplayTags.h"

void URPGDungeonFlowE2EServer::ObservePartyCombatVitals()
{
#if !UE_BUILD_SHIPPING
	// An opt-in, two-player reconnect fixture only. The observer must capture
	// each rendered state before releasing the next file gate. No UI snapshot,
	// identity, HP attribute or death tag is written by this fixture.
	if (PartyVitalsPhase == 4 || InitialParty.IsEmpty()) return;
	if (InitialParty.Num() != 2 || !bReconnectWithinGrace || GetWorld()->GetNetMode() != NM_DedicatedServer)
	{ Fail(TEXT("PartyVitalsUnsafeScope")); return; }
	const auto* Battle = GetWorld()->GetGameState<ARPGBossBattleGameState>();
	if (!Battle || Battle->GetBattleSnapshot().BattleState != EBossBattleState::InProgress
		|| Battle->GetBattleSnapshot().Boss != InitialBoss.Get())
	{ Fail(TEXT("PartyVitalsBattleChanged")); return; }
	ARPGPlayer* Players[2] = {};
	UAbilitySystemComponent* Abilities[2] = {};
	for (int32 Index = 0; Index < 2; ++Index)
	{
		const auto& Peer = InitialParty[Index];
		auto* PC = Peer.Controller.Get();
		auto* State = Peer.State.Get();
		Players[Index] = Cast<ARPGPlayer>(Peer.Pawn.Get());
		if (!PC || !State || !Players[Index] || !Peer.Connection.IsValid()
			|| Peer.Connection->GetConnectionState() != USOCK_Open || PC->GetNetConnection() != Peer.Connection.Get()
			|| PC->GetPawn() != Players[Index] || PC->GetPlayerState<ARPGPlayerState>() != State
			|| !State->HasAuthenticatedCharacter() || State->GetBackendCharacterId() != Peer.CharacterId
			|| State->GetAuthenticatedSteamId() != Peer.SteamId
			|| State->GetBackendConnectionGeneration() != Peer.ConnectionGeneration
			|| State->GetBackendDungeonSessionId() != InitialParty[0].State->GetBackendDungeonSessionId())
		{ Fail(TEXT("PartyVitalsPeerChanged")); return; }
		Abilities[Index] = Players[Index]->GetAbilitySystemComponent();
		if (!Abilities[Index] || !Abilities[Index]->GetSet<URPGAttributeSet>()) return;
	}
	const float TargetHealth = Abilities[0]->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute());
	const float SurvivorHealth = Abilities[1]->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute());
	if (!FMath::IsFinite(TargetHealth) || !FMath::IsFinite(SurvivorHealth) || SurvivorHealth <= 0.f
		|| Abilities[1]->HasMatchingGameplayTag(RPGGameplayTags::Shared_Status_Death))
	{ Fail(TEXT("PartyVitalsInvalidHealth")); return; }
	if (PartyVitalsPhase == 0)
	{
		if (TargetHealth <= 10.f || Abilities[0]->HasMatchingGameplayTag(RPGGameplayTags::Shared_Status_Death))
		{ Fail(TEXT("PartyVitalsTargetNotAlive")); return; }
		PartyVitalsTargetBefore = TargetHealth;
		PartyVitalsSurvivorBefore = SurvivorHealth;
		PartyVitalsPhase = 1;
		UE_LOG(LogTemp, Display, TEXT("PARTY_HUD_VITALS READY TargetHP=%g SurvivorHP=%g AuthenticatedPeers=2"),
			TargetHealth, SurvivorHealth);
	}
	if (!FMath::IsNearlyEqual(SurvivorHealth, PartyVitalsSurvivorBefore))
	{ Fail(TEXT("PartyVitalsSurvivorHealthChanged")); return; }
	const bool bLethal = PartyVitalsPhase == 2;
	const FString Gate = GatePath + (bLethal ? TEXT(".party-hud-defeat") : TEXT(".party-hud-damage"));
	if (PartyVitalsPhase == 1 || PartyVitalsPhase == 2)
	{
		if (!IFileManager::Get().FileExists(*Gate)) return;
		auto* Source = InitialBoss.Get();
		auto* SourceASC = Source ? Source->GetAbilitySystemComponent() : nullptr;
		if (!SourceASC) { Fail(TEXT("PartyVitalsDamageSourceMissing")); return; }
		const float Attack = SourceASC->GetNumericAttribute(URPGAttributeSet::GetAttackAttribute());
		const float Defense = Abilities[0]->GetNumericAttribute(URPGAttributeSet::GetDefenseAttribute());
		const float CritDamage = SourceASC->GetNumericAttribute(URPGAttributeSet::GetCriticalDamageAttribute());
		if (!FMath::IsFinite(Attack) || Attack <= 0.f || !FMath::IsFinite(Defense) || Defense <= 0.f
			|| !FMath::IsFinite(CritDamage) || CritDamage <= 0.f || TargetHealth <= 0.f)
		{ Fail(TEXT("PartyVitalsDamageAttributesInvalid")); return; }
		// Bound the first hit below a quarter of HP even on a critical hit;
		// bound the second above HP even with a sub-1 critical multiplier.
		const float Desired = bLethal ? TargetHealth + 2.f : TargetHealth * .2f;
		const float CritBound = bLethal ? FMath::Min(1.f, CritDamage) : FMath::Max(1.f, CritDamage);
		const float BaseDamage = Desired * Defense / (Attack * CritBound);
		FRPGSkillSecurityProfile Profile;
		Profile.MaximumFinalDamagePerHit = FMath::Min(FRPGSkillSecurityProfile::MaximumSupportedFinalDamage,
			FMath::Max(100.f, Desired * FMath::Max(1.f, CritDamage) / FMath::Min(1.f, CritDamage) + 2.f));
		FActiveGameplayEffectHandle Handle;
		FText Error;
		if (!URPGSecurityBlueprintLibrary::ApplyAuthorizedServerDamage(Source,
			FHitResult(Players[0], nullptr, Players[0]->GetActorLocation(), FVector::ForwardVector),
			URPGDungeonFlowE2EDamageEffect::StaticClass(), BaseDamage, FGameplayTag(), Profile, Handle, Error))
		{ Fail(TEXT("PartyVitalsAuthorizedDamageRejected")); return; }
		const float After = Abilities[0]->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute());
		if (!FMath::IsFinite(After) || (bLethal ? After != 0.f : (After <= 0.f || After >= TargetHealth)))
		{ Fail(TEXT("PartyVitalsDamageNotObserved")); return; }
		PartyVitalsPhase = bLethal ? 3 : 2;
		UE_LOG(LogTemp, Display, TEXT("PARTY_HUD_VITALS %s Before=%g After=%g SurvivorHP=%g AuthorizedServerDamage=1 DirectHPWrite=0"),
			bLethal ? TEXT("LETHAL_HIT") : TEXT("DAMAGED"), TargetHealth, After, SurvivorHealth);
		return;
	}
	if (PartyVitalsPhase == 3 && TargetHealth == 0.f
		&& Abilities[0]->HasMatchingGameplayTag(RPGGameplayTags::Shared_Status_Death))
	{
		PartyVitalsPhase = 4;
		UE_LOG(LogTemp, Display, TEXT("PARTY_HUD_VITALS INCAPACITATED TargetHP=0 DeathTag=1 SurvivorUnchanged=1 InProgress=1 NoMembershipChange=1"));
	}
#endif
}
