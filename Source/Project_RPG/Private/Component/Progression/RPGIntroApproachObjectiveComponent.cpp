#include "Component/Progression/RPGIntroApproachObjectiveComponent.h"

#include "Character/RPGPlayer.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Component/Skill/RPGPlayerSkillComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/TriggerBox.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "GameMode/RPGGameModeBase.h"
#include "Manager/HttpWebManager.h"
#include "Net/UnrealNetwork.h"
#include "Player/RPGPlayerState.h"
#include "PlayerClass/RPGPlayerClassComponent.h"
#include "PlayerClass/RPGPlayerClassDefinition.h"
#include "RPGGameplayTags.h"
#include "Security/RPGDungeonGameplayAuthority.h"
#include "Skill/RPGGameplayAbility_SkillContainer.h"
#include "Skill/RPGSkillDefinition.h"

namespace
{
	const FName ZoneTags[] = {TEXT("Progression.Intro1.Zone.Start"),
		TEXT("Progression.Intro1.Zone.Approach"), TEXT("Progression.Intro1.Zone.Attack")};
}

URPGIntroApproachObjectiveComponent::URPGIntroApproachObjectiveComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickInterval = .1f;
	SetIsReplicatedByDefault(true);
}

void URPGIntroApproachObjectiveComponent::BeginPlay()
{
	Super::BeginPlay();
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		SetComponentTickEnabled(true);
		RefreshObservation();
	}
}

void URPGIntroApproachObjectiveComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	bEnding = true;
	ClearObservation();
	Super::EndPlay(Reason);
}

void URPGIntroApproachObjectiveComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* TickFunction)
{
	Super::TickComponent(DeltaTime, TickType, TickFunction);
	RefreshObservation();
}

void URPGIntroApproachObjectiveComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION_NOTIFY(URPGIntroApproachObjectiveComponent, Progress, COND_OwnerOnly, REPNOTIFY_Always);
}

URPGAbilitySystemComponent* URPGIntroApproachObjectiveComponent::ResolveEligibleASC() const
{
	auto* Player = Cast<ARPGPlayer>(GetOwner());
	const auto* World = GetWorld();
	const auto* Mode = World ? World->GetAuthGameMode<ARPGGameModeBase>() : nullptr;
	const auto* State = Player ? Player->GetPlayerState<ARPGPlayerState>() : nullptr;
	const auto* Controller = Player ? Player->GetController() : nullptr;
	const auto* Growth = Player ? Player->FindComponentByClass<URPGPlayerSkillComponent>() : nullptr;
	const auto* Class = Growth ? Growth->GetPlayerClassDefinition() : nullptr;
	const auto* Web = World && World->GetGameInstance() ? World->GetGameInstance()->GetSubsystem<UHttpWebManager>() : nullptr;
	auto* ASC = Player ? Cast<URPGAbilitySystemComponent>(Player->GetAbilitySystemComponent()) : nullptr;
	if (bEnding || !IsValid(Player) || Player->IsActorBeingDestroyed() || !Player->HasAuthority()
		|| !IsValid(World) || World->bIsTearingDown || World->GetNetMode() != NM_DedicatedServer
		|| !IsValid(Mode) || !Mode->IsBackendDungeonDifficultyReady()
		|| Mode->GetConfirmedDungeonContentId() != TEXT("Progression.Intro1")
		|| Mode->GetCurrentGameDifficulty() != ERPGGameDifficulty::Normal
		|| !IsValid(State) || State->IsInactive() || !State->HasAuthenticatedCharacter() || State->GetBackendConnectionGeneration() <= 0
		|| !IsValid(Controller) || Controller->GetPawn() != Player || Controller->PlayerState != State || State->GetOwner() != Controller
		|| IsDungeonGameplayRevoked(Controller) || !Web || !Web->IsConfiguredForDungeonServer()
		|| State->GetBackendDungeonSessionId() != Web->GetConfiguredDungeonSessionId()
		|| !Growth || Growth->GetSkillLoadState() != ERPGPlayerSkillLoadState::Ready || Growth->IsConfigurationMutable()
		|| !Class || Class->ClassId != FName(TEXT("GreatSwordWarrior"))
		|| Player->FindComponentByClass<URPGIntroApproachObjectiveComponent>() != this
		|| !ASC || ASC->GetAvatarActor() != Player
		|| Mode->IsDungeonPartyMemberRevoked(State->GetBackendDungeonSessionId(),
			State->GetBackendCharacterId(), State->GetAuthenticatedSteamId())) return nullptr;
	return FRPGApprovedHitContext(Player, Player).IsCurrent(Player, Player) ? ASC : nullptr;
}

bool URPGIntroApproachObjectiveComponent::FindZones(TArray<FZone>& OutZones) const
{
	OutZones.Reset();
	const auto* World = GetWorld();
	if (!World || World->bIsTearingDown) return false;
	TArray<FZone> Found;
	Found.SetNum(UE_ARRAY_COUNT(ZoneTags));
	for (TActorIterator<ATriggerBox> It(GetWorld()); It; ++It)
	{
		auto* Actor = *It;
		for (int32 Index = 0; Index < Found.Num(); ++Index)
		{
			if (!Actor->ActorHasTag(ZoneTags[Index])) continue;
			if (Actor->IsActorBeingDestroyed() || Found[Index].Actor.IsValid()) return false;
			const auto* Box = Cast<UBoxComponent>(Actor->GetCollisionComponent());
			if (!Box || Box->GetCollisionProfileName() != FName(TEXT("Trigger"))) return false;
			FZone Zone;
			Zone.Actor = Actor; Zone.Transform = Box->GetComponentTransform(); Zone.Extent = Box->GetUnscaledBoxExtent();
			const FVector Scale = Zone.Transform.GetScale3D();
			if (Zone.Transform.ContainsNaN() || Zone.Extent.ContainsNaN() || Scale.GetMin() <= SMALL_NUMBER
				|| Zone.Extent.GetMin() <= 0.f || Zone.Extent.GetMax() > 5000.f) return false;
			for (int32 Other = 0; Other < Found.Num(); ++Other)
				if (Found[Other].Actor == Actor) return false;
			Found[Index] = Zone;
		}
	}
	for (const auto& Zone : Found) if (!Zone.Actor.IsValid()) return false;
	OutZones = MoveTemp(Found);
	return true;
}

bool URPGIntroApproachObjectiveComponent::IsLayoutCurrent() const
{
	TArray<FZone> Current;
	if (Zones.Num() != UE_ARRAY_COUNT(ZoneTags) || !FindZones(Current)) return false;
	for (int32 Index = 0; Index < Zones.Num(); ++Index)
		if (Current[Index].Actor != Zones[Index].Actor || !Current[Index].Transform.Equals(Zones[Index].Transform)
			|| !Current[Index].Extent.Equals(Zones[Index].Extent)) return false;
	return true;
}

bool URPGIntroApproachObjectiveComponent::Contains(const FZone& Zone, const FVector& Location)
{
	if (!Zone.Actor.IsValid() || Location.ContainsNaN() || Zone.Transform.ContainsNaN()
		|| Zone.Extent.ContainsNaN() || Zone.Extent.GetMin() <= 0.f) return false;
	const FVector Local = Zone.Transform.InverseTransformPosition(Location).GetAbs();
	return !Local.ContainsNaN() && Local.X <= Zone.Extent.X && Local.Y <= Zone.Extent.Y && Local.Z <= Zone.Extent.Z;
}

bool URPGIntroApproachObjectiveComponent::IsObservationCurrent() const
{
	auto* ASC = ResolveEligibleASC();
	const auto* Player = Cast<ARPGPlayer>(GetOwner());
	const auto* State = Player ? Player->GetPlayerState<ARPGPlayerState>() : nullptr;
	const auto* Growth = Player ? Player->FindComponentByClass<URPGPlayerSkillComponent>() : nullptr;
	return Progress.bActive && ASC && ASC == ObservedASC.Get() && State && Growth
		&& State == ObservedPlayerState.Get() && Player->GetController() == ObservedController.Get()
		&& State->GetBackendDungeonSessionId() == ObservedSession && State->GetBackendCharacterId() == ObservedCharacter
		&& State->GetAuthenticatedSteamId() == ObservedSteamId && State->GetBackendConnectionGeneration() == ObservedConnectionGeneration
		&& Growth->GetSkillRestoreGeneration() == ObservedRestoreGeneration && IsLayoutCurrent()
		&& PawnLifetime.IsCurrent(GetOwner(), GetOwner());
}

void URPGIntroApproachObjectiveComponent::ClearObservation()
{
	if (auto* ASC = ObservedASC.Get())
	{
		ASC->AbilityActivatedCallbacks.Remove(ActivatedHandle);
		ASC->AbilityCommittedCallbacks.Remove(CommittedHandle);
		ASC->OnAbilityEnded.Remove(EndedHandle);
	}
	ActivatedHandle.Reset(); CommittedHandle.Reset(); EndedHandle.Reset();
	ObservedASC.Reset(); ObservedPlayerState.Reset(); ObservedController.Reset();
	ObservedSession.Reset(); ObservedCharacter.Reset(); ObservedSteamId.Reset();
	ObservedConnectionGeneration = 0; ObservedRestoreGeneration = 0;
	Casts.Reset(); Zones.Reset(); Progress = {};
}

void URPGIntroApproachObjectiveComponent::RefreshObservation()
{
	if (bEnding || bRefreshing || !GetOwner() || !GetOwner()->HasAuthority()) return;
	TGuardValue<bool> Guard(bRefreshing, true);
	if (!IsObservationCurrent())
	{
		const bool bWasActive = Progress.bActive;
		ClearObservation();
		auto* ASC = ResolveEligibleASC();
		if (ASC && FindZones(Zones))
		{
			auto* Player = CastChecked<ARPGPlayer>(GetOwner());
			auto* State = Player->GetPlayerState<ARPGPlayerState>();
			ObservedASC = ASC; ObservedPlayerState = State; ObservedController = Player->GetController();
			ObservedSession = State->GetBackendDungeonSessionId(); ObservedCharacter = State->GetBackendCharacterId();
			ObservedSteamId = State->GetAuthenticatedSteamId(); ObservedConnectionGeneration = State->GetBackendConnectionGeneration();
			ObservedRestoreGeneration = Player->FindComponentByClass<URPGPlayerSkillComponent>()->GetSkillRestoreGeneration();
			PawnLifetime = FRPGApprovedHitContext(Player, Player); Progress.bActive = true;
			ActivatedHandle = ASC->AbilityActivatedCallbacks.AddUObject(this, &ThisClass::ObserveActivated);
			CommittedHandle = ASC->AbilityCommittedCallbacks.AddUObject(this, &ThisClass::ObserveCommitted);
			EndedHandle = ASC->OnAbilityEnded.AddUObject(this, &ThisClass::ObserveEnded);
		}
		if (bWasActive || Progress.bActive) Publish();
	}
	if (!Progress.bActive || !IsObservationCurrent()) return;
	for (auto It = Casts.CreateIterator(); It; ++It)
		if (!It.Key().IsValid() || !It.Key()->IsActive()) It.RemoveCurrent();
	if (!Progress.bStartReached && Contains(Zones[0], GetOwner()->GetActorLocation()))
	{
		Progress.bStartReached = true;
		Publish();
	}
}

URPGIntroApproachObjectiveComponent::EGoal URPGIntroApproachObjectiveComponent::Classify(const UGameplayAbility& Ability) const
{
	const auto* Skill = Cast<URPGGameplayAbility_SkillContainer>(&Ability);
	const auto* Definition = Skill ? Skill->GetSkillDefinition() : nullptr;
	if (!Definition) return EGoal::None;
	if (Definition->SkillTag == RPGGameplayTags::Player_Ability_Skill_AssultBlade) return EGoal::Approach;
	return Definition->SkillTag == RPGGameplayTags::Player_Ability_Skill_WhirlWind ? EGoal::Attack : EGoal::None;
}

bool URPGIntroApproachObjectiveComponent::IsCurrentGrant(const UGameplayAbility& Ability, FGameplayAbilitySpecHandle Handle) const
{
	const auto* ASC = ObservedASC.Get();
	const auto* Info = Ability.GetCurrentActorInfo();
	const auto* Spec = ASC ? ASC->FindAbilitySpecFromHandle(Handle) : nullptr;
	const auto* ClassOwner = GetOwner() ? GetOwner()->FindComponentByClass<URPGPlayerClassComponent>() : nullptr;
	return Info && Info->AvatarActor.Get() == GetOwner() && Info->AbilitySystemComponent.Get() == ASC
		&& Spec && Spec->Ability && Spec->Ability->GetClass() == Ability.GetClass()
		&& ClassOwner && ClassOwner->CanUseAbilitySpec(Handle, Ability.GetClass());
}

bool URPGIntroApproachObjectiveComponent::CanStartGoal(EGoal Goal, const FVector& Location) const
{
	if (!Progress.bActive || !Progress.bStartReached || Progress.IsComplete() || Zones.Num() != 3 || Location.ContainsNaN()) return false;
	if (Goal == EGoal::Approach) return !Progress.bApproachCompleted && !Contains(Zones[1], Location);
	return Goal == EGoal::Attack && Progress.bApproachCompleted && Contains(Zones[2], Location);
}

bool URPGIntroApproachObjectiveComponent::CompleteSpatialGoal(const FObservedCast& Cast, const FVector& End)
{
	if (!Cast.bCommitted || !CanStartGoal(Cast.Goal, Cast.Start) || End.ContainsNaN()) return false;
	if (Cast.Goal == EGoal::Approach)
	{
		if (!Contains(Zones[1], End) || FVector::DistSquared(Cast.Start, End) < FMath::Square(100.f)) return false;
		Progress.bApproachCompleted = true;
	}
	else
	{
		if (!Contains(Zones[2], End)) return false;
		Progress.bAttackCompleted = true;
	}
	return true;
}

void URPGIntroApproachObjectiveComponent::ObserveActivated(UGameplayAbility* Ability)
{
	if (bRefreshing || !IsObservationCurrent() || !IsValid(Ability) || !Ability->IsInstantiated() || !Ability->IsActive()
		|| !IsCurrentGrant(*Ability, Ability->GetCurrentAbilitySpecHandle())) return;
	FObservedCast Cast;
	Cast.Goal = Classify(*Ability); Cast.Start = GetOwner()->GetActorLocation();
	if (!CanStartGoal(Cast.Goal, Cast.Start)) return;
	Cast.Handle = Ability->GetCurrentAbilitySpecHandle();
	Casts.Add(Ability, Cast);
}

void URPGIntroApproachObjectiveComponent::ObserveCommitted(UGameplayAbility* Ability)
{
	if (bRefreshing || !IsObservationCurrent() || !IsValid(Ability)) return;
	auto* Record = Casts.Find(Ability);
	const auto* Skill = Cast<URPGGameplayAbility_SkillContainer>(Ability);
	if (!Record || Record->bCommitted || !Skill || Record->Handle != Ability->GetCurrentAbilitySpecHandle()
		|| !IsCurrentGrant(*Ability, Record->Handle)) return;
	const auto& Spec = Skill->GetActiveSkillSpec();
	if ((Record->Goal == EGoal::Approach && Spec.SkillTag != RPGGameplayTags::Player_Ability_Skill_AssultBlade)
		|| (Record->Goal == EGoal::Attack && Spec.SkillTag != RPGGameplayTags::Player_Ability_Skill_WhirlWind)) return;
	Record->Activation = Skill->GetSkillActivationSerial(); Record->bCommitted = true;
}

void URPGIntroApproachObjectiveComponent::ObserveEnded(const FAbilityEndedData& Ended)
{
	if (bRefreshing || !IsObservationCurrent()) return;
	auto* Skill = Cast<URPGGameplayAbility_SkillContainer>(Ended.AbilityThatEnded.Get());
	if (!Skill || Skill->IsActive() || Skill->GetCombatExecutionId().IsValid()) return;
	FObservedCast Cast;
	if (!Casts.RemoveAndCopyValue(Skill, Cast) || Ended.bWasCancelled || Cast.Handle != Ended.AbilitySpecHandle
		|| !IsCurrentGrant(*Skill, Cast.Handle) || !Cast.bCommitted || Cast.Activation != Skill->GetSkillActivationSerial()) return;
	if (CompleteSpatialGoal(Cast, GetOwner()->GetActorLocation())) Publish();
}

void URPGIntroApproachObjectiveComponent::Publish()
{
	GetOwner()->ForceNetUpdate();
	OnApproachProgressChanged.Broadcast(Progress);
}

void URPGIntroApproachObjectiveComponent::OnRep_Progress()
{
	OnApproachProgressChanged.Broadcast(Progress);
}
