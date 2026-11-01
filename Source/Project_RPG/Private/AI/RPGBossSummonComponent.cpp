#include "AI/RPGBossSummonComponent.h"
#include "Ability/RPGBossGameplayAbility.h"
#include "Character/RPGNonPlayerCharacter.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Attribute/RPGAttributeSet.h"
#include "AIController.h"
#include "BrainComponent.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"

namespace
{
	void DestroySummon(TWeakObjectPtr<ARPGNonPlayerCharacter> Member, TWeakObjectPtr<AController> OwnedAI)
	{
		ARPGNonPlayerCharacter* Pawn = Member.Get();
		if (IsValid(Pawn))
		{
			if (AController* Current = Pawn->GetController()) { Current->UnPossess(); }
			if (!Pawn->IsActorBeingDestroyed()) { Pawn->Destroy(); }
		}
		if (AController* Owned = OwnedAI.Get())
		{
			if (!Owned->GetPawn() || Owned->GetPawn() == Pawn) { Owned->Destroy(); }
		}
	}
}

URPGBossSummonComponent::URPGBossSummonComponent()
{
	SetIsReplicatedByDefault(true);
	bReplicateUsingRegisteredSubObjectList = true;
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = .1f;
}

void URPGBossSummonComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(URPGBossSummonComponent, Summons);
}

double URPGBossSummonComponent::ServerNow() const
{
	const AGameStateBase* Clock = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	return Clock ? Clock->GetServerWorldTimeSeconds() : (GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0);
}

bool URPGBossSummonComponent::HasCapacity(int32 Count, int32 MaximumOwned) const
{
	return GetOwner() && GetOwner()->HasAuthority() && !bSpawning && !bCleaning && !bNotifyingDefeat && GetIsReplicated() &&
		Count >= 1 && Count <= 8 && MaximumOwned >= 1 && MaximumOwned <= 32 && Summons.Num() + Count <= MaximumOwned;
}

bool URPGBossSummonComponent::IsPlanValid(const FRPGBossSummonPlan& Plan)
{
	const ARPGNonPlayerCharacter* CDO = Plan.PawnClass ? Plan.PawnClass->GetDefaultObject<ARPGNonPlayerCharacter>() : nullptr;
	if (!CDO || Plan.PawnClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists) || !CDO->GetIsReplicated() ||
		!CDO->AIControllerClass || !CDO->AIControllerClass->IsChildOf(AAIController::StaticClass()) ||
		CDO->AIControllerClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists) ||
		Plan.Transforms.Num() < 1 || Plan.Transforms.Num() > 8 || Plan.MaximumOwned < Plan.Transforms.Num() || Plan.MaximumOwned > 32 ||
		!FMath::IsFinite(Plan.Lifetime) || Plan.Lifetime < .1f || Plan.Lifetime > 300.f ||
		!FMath::IsFinite(Plan.StartupTimeout) || Plan.StartupTimeout < .1f || Plan.StartupTimeout > 30.f || Plan.StartupTimeout > Plan.Lifetime) { return false; }
	for (const FTransform& Transform : Plan.Transforms)
	{
		if (Transform.ContainsNaN() || !Transform.GetScale3D().Equals(FVector::OneVector) || !Transform.IsRotationNormalized()) { return false; }
	}
	return true;
}

void URPGBossSummonComponent::BeginPlay()
{
	Super::BeginPlay();
	if (!GetOwner()->HasAuthority()) { SetComponentTickEnabled(false); return; }
	BoundBoss = GetOwner()->FindComponentByClass<URPGBossAIComponent>();
	if (BoundBoss.IsValid()) { BoundBoss->OnBossStateChanged.AddUniqueDynamic(this, &ThisClass::OnBossStateChanged); }
	if (APawn* OwnerPawn = Cast<APawn>(GetOwner())) { OwnerPawn->ReceiveControllerChangedDelegate.AddUniqueDynamic(this, &ThisClass::OnSourceControllerChanged); }
}

bool URPGBossSummonComponent::IsSourceValid() const
{
	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	const AAIController* AI = OwnerPawn ? Cast<AAIController>(OwnerPawn->GetController()) : nullptr;
	if (!OwnerPawn || !OwnerPawn->HasAuthority() || OwnerPawn->IsActorBeingDestroyed() || !AI || AI != SourceController.Get() ||
		AI->GetGenericTeamId().GetId() != SourceTeam || !SourceASC.IsValid() || SourceASC->GetAvatarActor() != OwnerPawn ||
		UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(OwnerPawn) != SourceASC.Get() ||
		!BoundBoss.IsValid() || GetOwner()->FindComponentByClass<URPGBossAIComponent>() != BoundBoss.Get() || !FMath::IsFinite(ServerNow())) { return false; }
	const FRPGBossState Context = BoundBoss->GetBossState();
	if (Context.EncounterState != ERPGBossEncounterState::InCombat) { return false; }
	for (const FRPGBossSummonRecord& Record : Summons)
	{
		if (Record.EncounterId != Context.EncounterId || Record.PhaseIndex != Context.PhaseIndex) { return false; }
	}
	return true;
}

bool URPGBossSummonComponent::SpawnWave(const URPGBossGameplayAbility* Caster, const FRPGBossSummonPlan& Plan, int32 Sequence)
{
	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	AAIController* AI = OwnerPawn ? Cast<AAIController>(OwnerPawn->GetController()) : nullptr;
	if (!IsValid(Caster) || !Caster->IsActive() || !Caster->IsBossExecutionValid() || Caster->GetAvatarActorFromActorInfo() != OwnerPawn ||
		!AI || AI->GetGenericTeamId() == FGenericTeamId::NoTeam || Sequence <= 0 || !BoundBoss.IsValid() ||
		!IsPlanValid(Plan) || !HasCapacity(Plan.Transforms.Num(), Plan.MaximumOwned)) { return false; }
	for (const FTransform& Transform : Plan.Transforms)
	{
		if (FVector::DistSquared(Transform.GetLocation(), OwnerPawn->GetActorLocation()) > FMath::Square(2500.f)) { return false; }
	}
	if (!Summons.IsEmpty() && !IsSourceValid()) { ClearSummons(); return false; }
	SourceController = AI;
	SourceTeam = AI->GetGenericTeamId().GetId();
	SourceASC = Caster->GetAbilitySystemComponentFromActorInfo();
	if (!IsSourceValid()) { return false; }
	const uint64 Serial = Caster->GetBossExecutionSerial();
	const uint64 Epoch = MutationEpoch;
	const FRPGBossState Context = BoundBoss->GetBossState();
	if (Summons.ContainsByPredicate([&](const FRPGBossSummonRecord& Record)
		{ return Record.EncounterId == Context.EncounterId && Record.Sequence == Sequence; })) { return false; }
	const auto IsCurrent = [&]()
	{
		return MutationEpoch == Epoch && !bCleaning && IsValid(Caster) && Caster->IsActive() &&
			Caster->GetBossExecutionSerial() == Serial && Caster->IsBossExecutionValid() && IsSourceValid();
	};
	TGuardValue<bool> SpawnGuard(bSpawning, true);
	for (int32 Slot = 0; Slot < Plan.Transforms.Num(); ++Slot)
	{
		if (!IsCurrent()) { RemoveWave(Context.EncounterId, Sequence); return false; }
		ARPGNonPlayerCharacter* Pawn = GetWorld()->SpawnActorDeferred<ARPGNonPlayerCharacter>(Plan.PawnClass,
			Plan.Transforms[Slot], OwnerPawn, OwnerPawn, ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding);
		if (!IsValid(Pawn) || !IsCurrent())
		{
			if (IsValid(Pawn)) { Pawn->Destroy(); }
			RemoveWave(Context.EncounterId, Sequence); return false;
		}
		Pawn->ConfigureEncounterSummon(SourceTeam);
		FRPGBossSummonRecord Record;
		Record.Pawn = Pawn; Record.EncounterId = Context.EncounterId; Record.PhaseIndex = Context.PhaseIndex;
		Record.Sequence = Sequence; Record.Slot = Slot;
		Record.StartServerTime = ServerNow(); Record.EndServerTime = Record.StartServerTime + Plan.Lifetime;
		Record.StartupDeadline = Record.StartServerTime + Plan.StartupTimeout;
		Summons.Add(Record); // Reserve ownership before construction/BeginPlay callbacks.
		Pawn->OnDestroyed.AddUniqueDynamic(this, &ThisClass::OnMemberDestroyed);
		Pawn->OnNPCStartUpDataReady.AddUniqueDynamic(this, &ThisClass::OnMemberReady);
		Pawn->FinishSpawning(Plan.Transforms[Slot]);
		if (!IsCurrent() || !IsValid(Pawn) || Pawn->IsActorBeingDestroyed() || !Pawn->GetIsReplicated() ||
			FVector::DistSquared(Pawn->GetActorLocation(), Plan.Transforms[Slot].GetLocation()) > FMath::Square(250.f))
		{
			RemoveWave(Context.EncounterId, Sequence); return false;
		}
		// Respect authored auto-possession; support NPCs that explicitly disable it as well.
		if (!Pawn->GetController()) { Pawn->SpawnDefaultController(); }
		AAIController* Controller = Cast<AAIController>(Pawn->GetEncounterSummonController());
		if (!IsCurrent() || !IsValid(Pawn) || !IsValid(Controller) || Pawn->GetController() != Controller ||
			Controller->GetGenericTeamId().GetId() != SourceTeam)
		{
			RemoveWave(Context.EncounterId, Sequence); return false;
		}
		const int32 Index = Summons.IndexOfByPredicate([Pawn](const FRPGBossSummonRecord& Item) { return Item.Pawn == Pawn; });
		if (Index == INDEX_NONE) { RemoveWave(Context.EncounterId, Sequence); return false; }
		Summons[Index].Controller = Controller;
		Pawn->SetOwner(OwnerPawn); // PossessedBy sets Owner to the AI; retain encounter relevancy ownership.
		Pawn->ReceiveControllerChangedDelegate.AddUniqueDynamic(this, &ThisClass::OnMemberControllerChanged);
	}
	Publish();
	OnWaveSpawned.Broadcast(Context.EncounterId, Context.PhaseIndex, Sequence, Plan.Transforms.Num());
	return IsCurrent();
}

void URPGBossSummonComponent::Publish() { if (GetOwner()) { GetOwner()->ForceNetUpdate(); } }

void URPGBossSummonComponent::DestroyRecords(const TArray<FRPGBossSummonRecord>& Records)
{
	for (const FRPGBossSummonRecord& Record : Records)
	{
		if (UAbilitySystemComponent* Observed = Record.ObservedASC.Get())
		{
			Observed->GetGameplayAttributeValueChangeDelegate(URPGAttributeSet::GetCurrentHealthAttribute()).Remove(Record.HealthChangedHandle);
		}
		ARPGNonPlayerCharacter* Pawn = Record.Pawn;
		const TWeakObjectPtr<AController> OwnedAI = Record.Controller.IsValid() ? Record.Controller :
			(IsValid(Pawn) ? TWeakObjectPtr<AController>(Pawn->GetEncounterSummonController()) : TWeakObjectPtr<AController>());
		if (IsValid(Pawn))
		{
			Pawn->OnDestroyed.RemoveDynamic(this, &ThisClass::OnMemberDestroyed);
			Pawn->OnNPCStartUpDataReady.RemoveDynamic(this, &ThisClass::OnMemberReady);
			Pawn->ReceiveControllerChangedDelegate.RemoveDynamic(this, &ThisClass::OnMemberControllerChanged);
			Pawn->RetireEncounterSummon();
			if (UAbilitySystemComponent* ASC = Pawn->GetAbilitySystemComponent()) { ASC->CancelAllAbilities(); }
			Pawn->GetCharacterMovement()->DisableMovement();
		}
		if (AController* Owned = OwnedAI.Get())
		{
			// The old AI may now possess somebody else's Pawn. Do not destroy that new ownership.
			if (!Owned->GetPawn() || Owned->GetPawn() == Pawn)
			{
				if (AAIController* AI = Cast<AAIController>(Owned)) { AI->StopMovement(); if (AI->BrainComponent) { AI->BrainComponent->StopLogic(TEXT("Boss summon removed")); } }
			}
		}
		const TWeakObjectPtr<ARPGNonPlayerCharacter> Member = Pawn;
		if (bSpawning || bNotifyingDefeat || Record.bRetiring)
		{
			// Construction/possession/death callbacks can reset the encounter. Finish their stack first.
			GetWorld()->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateLambda([Member, OwnedAI]() { DestroySummon(Member, OwnedAI); }));
		}
		else { DestroySummon(Member, OwnedAI); }
	}
}

void URPGBossSummonComponent::Retire(ARPGNonPlayerCharacter* Pawn)
{
	if (!GetOwner()->HasAuthority() || bCleaning) { return; }
	TArray<FRPGBossSummonRecord> Removed;
	for (const FRPGBossSummonRecord& Record : Summons) { if (Record.Pawn == Pawn) { Removed.Add(Record); } }
	if (Removed.IsEmpty()) { return; }
	TGuardValue<bool> CleaningGuard(bCleaning, true);
	++MutationEpoch;
	Summons.RemoveAll([Pawn](const FRPGBossSummonRecord& Record) { return Record.Pawn == Pawn; });
	Publish();
	DestroyRecords(Removed);
}

void URPGBossSummonComponent::RemoveWave(int32 EncounterId, int32 Sequence)
{
	if (!GetOwner()->HasAuthority()) { return; }
	const TArray<FRPGBossSummonRecord> Snapshot = Summons;
	for (const FRPGBossSummonRecord& Record : Snapshot) { if (Record.EncounterId == EncounterId && Record.Sequence == Sequence) { Retire(Record.Pawn); } }
}

void URPGBossSummonComponent::ClearSummons()
{
	if (!GetOwner()->HasAuthority() || bCleaning) { return; }
	TGuardValue<bool> CleaningGuard(bCleaning, true);
	++MutationEpoch;
	const TArray<FRPGBossSummonRecord> Removed = MoveTemp(Summons);
	Summons.Reset();
	Publish();
	DestroyRecords(Removed);
}

void URPGBossSummonComponent::ObserveDefeat(ARPGNonPlayerCharacter* Pawn)
{
	if (!IsValid(GetOwner()) || !GetOwner()->HasAuthority() || bCleaning || !IsSourceValid() || !IsValid(Pawn)) { return; }
	const int32 Index = Summons.IndexOfByPredicate([Pawn](const FRPGBossSummonRecord& Item) { return Item.Pawn == Pawn; });
	if (Index == INDEX_NONE) { return; }
	const FRPGBossSummonRecord Record = Summons[Index];
	UAbilitySystemComponent* ASC = Record.ObservedASC.Get();
	const AAIController* AI = Cast<AAIController>(Record.Controller.Get());
	if (!Record.bInitialized || Record.bRetiring || Record.bDefeated || ServerNow() >= Record.EndServerTime ||
		!Pawn->IsNPCStartUpDataReady() || !ASC || Pawn->GetAbilitySystemComponent() != ASC || ASC->GetAvatarActor() != Pawn ||
		!AI || Pawn->GetController() != AI || AI->GetGenericTeamId().GetId() != SourceTeam ||
		Pawn->GetOwner() != GetOwner() || Pawn->GetInstigator() != GetOwner()) { return; }
	const float Health = ASC->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute());
	if (!FMath::IsFinite(Health) || Health > 0.f) { return; }
	Summons[Index].bDefeated = true;
	// A mechanic may advance phase/reset here. Defer destruction of NPCs until
	// after the GAS health notification stack, just as during construction.
	TGuardValue<bool> CallbackGuard(bNotifyingDefeat, true);
	OnSummonDefeated.Broadcast(Record.EncounterId, Record.PhaseIndex, Record.Sequence, Record.Slot);
}

void URPGBossSummonComponent::UpdateSummons()
{
	if (!GetOwner()->HasAuthority() || bCleaning || bSpawning || bNotifyingDefeat || Summons.IsEmpty()) { return; }
	if (!IsSourceValid()) { ClearSummons(); return; }
	const double Now = ServerNow();
	const TArray<FRPGBossSummonRecord> Snapshot = Summons;
	for (const FRPGBossSummonRecord& Record : Snapshot)
	{
		ARPGNonPlayerCharacter* Pawn = Record.Pawn;
		const AAIController* AI = Record.Controller.IsValid() ? Cast<AAIController>(Record.Controller.Get()) : nullptr;
		if (!IsValid(Pawn) || Pawn->IsActorBeingDestroyed() || Record.bRetiring || Now >= Record.EndServerTime ||
			(!Record.bInitialized && Now >= Record.StartupDeadline) || !AI || Pawn->GetController() != AI ||
			AI->GetGenericTeamId().GetId() != SourceTeam || Pawn->GetOwner() != GetOwner() || Pawn->GetInstigator() != GetOwner()) { Retire(Pawn); continue; }
		if (!Record.bInitialized && Pawn->IsNPCStartUpDataReady())
		{
			UAbilitySystemComponent* ASC = Pawn->GetAbilitySystemComponent();
			const float Health = ASC ? ASC->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute()) : 0.f;
			if (!ASC || ASC->GetAvatarActor() != Pawn || !FMath::IsFinite(Health) || Health <= 0.f) { Retire(Pawn); continue; }
			const int32 Index = Summons.IndexOfByPredicate([Pawn](const FRPGBossSummonRecord& Item) { return Item.Pawn == Pawn; });
			if (Index != INDEX_NONE)
			{
				Summons[Index].bInitialized = true;
				Summons[Index].ObservedASC = ASC;
				const TWeakObjectPtr<ARPGNonPlayerCharacter> Member = Pawn;
				Summons[Index].HealthChangedHandle = ASC->GetGameplayAttributeValueChangeDelegate(
					URPGAttributeSet::GetCurrentHealthAttribute()).AddWeakLambda(this,
					[this, Member](const FOnAttributeChangeData&) { ObserveDefeat(Member.Get()); });
				Publish();
			}
		}
		ObserveDefeat(Pawn);
		if (!IsSourceValid() || Summons.IsEmpty()) { return; }
	}
}

void URPGBossSummonComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
	Super::TickComponent(DeltaTime, TickType, TickFunction);
	UpdateSummons();
}

void URPGBossSummonComponent::OnBossStateChanged(const FRPGBossState& Previous, const FRPGBossState& Current)
{
	if (!Summons.IsEmpty() && !IsSourceValid()) { ClearSummons(); }
}

void URPGBossSummonComponent::OnSourceControllerChanged(APawn* Pawn, AController* Previous, AController* Current)
{
	if (Previous != Current) { ClearSummons(); }
}

void URPGBossSummonComponent::OnMemberControllerChanged(APawn* Pawn, AController* Previous, AController* Current)
{
	if (Previous == Current || bCleaning) { return; }
	const int32 Index = Summons.IndexOfByPredicate([Pawn](const FRPGBossSummonRecord& Item) { return Item.Pawn == Pawn; });
	if (Index == INDEX_NONE || Summons[Index].Controller.Get() == Current) { return; }
	Summons[Index].bRetiring = true;
	Summons[Index].bInitialized = false;
	Publish();
	if (ARPGNonPlayerCharacter* NPC = Cast<ARPGNonPlayerCharacter>(Pawn))
	{
		if (UAbilitySystemComponent* ASC = NPC->GetAbilitySystemComponent()) { ASC->CancelAllAbilities(); }
		NPC->GetCharacterMovement()->StopMovementImmediately();
	}
	// Never destroy a Pawn from inside another Controller's Possess/UnPossess stack.
	const TWeakObjectPtr<ARPGNonPlayerCharacter> Member = Cast<ARPGNonPlayerCharacter>(Pawn);
	GetWorld()->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this, Member]() { if (Member.IsValid()) { Retire(Member.Get()); } }));
}

void URPGBossSummonComponent::OnMemberDestroyed(AActor* Actor)
{
	ARPGNonPlayerCharacter* Pawn = Cast<ARPGNonPlayerCharacter>(Actor);
	ObserveDefeat(Pawn);
	Retire(Pawn);
}
void URPGBossSummonComponent::OnMemberReady() { UpdateSummons(); }

void URPGBossSummonComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	ClearSummons();
	if (BoundBoss.IsValid()) { BoundBoss->OnBossStateChanged.RemoveDynamic(this, &ThisClass::OnBossStateChanged); }
	if (APawn* Pawn = Cast<APawn>(GetOwner())) { Pawn->ReceiveControllerChangedDelegate.RemoveDynamic(this, &ThisClass::OnSourceControllerChanged); }
	Super::EndPlay(Reason);
}
