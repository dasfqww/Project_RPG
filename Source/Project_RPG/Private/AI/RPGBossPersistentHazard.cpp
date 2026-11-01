#include "AI/RPGBossPersistentHazard.h"

#include "Ability/RPGBossPersistentHazardAbility.h"
#include "AI/RPGBossAIComponent.h"
#include "AI/RPGBossThreatComponent.h"
#include "AI/RPGBossTelegraphPresentationComponent.h"
#include "AbilitySystemComponent.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Combat/HitQuery/RPGHitQuerySubsystem.h"
#include "Components/DecalComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "FunctionLibrary/RPGAbilityFunctionLibrary.h"
#include "GameFramework/Controller.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "GameplayEffect.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "RPGGameplayTags.h"

ARPGBossPersistentHazard::ARPGBossPersistentHazard()
{
	bReplicates = true;
	bReplicateUsingRegisteredSubObjectList = true;
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = .1f;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("HazardRoot"));
}

void ARPGBossPersistentHazard::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ARPGBossPersistentHazard, State);
}

bool ARPGBossPersistentHazard::IsTimingValid(float Duration, float Interval)
{
	return FMath::IsFinite(Duration) && Duration >= .1f && Duration <= 120.f &&
		FMath::IsFinite(Interval) && Interval >= .1f && Interval <= 30.f && Interval <= Duration &&
		FMath::CeilToInt(Duration / Interval) <= 128;
}

double ARPGBossPersistentHazard::ServerNow() const
{
	const AGameStateBase* GameState = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	return GameState ? GameState->GetServerWorldTimeSeconds() : (GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0);
}

bool ARPGBossPersistentHazard::InitializeFromCast(const URPGBossPersistentHazardAbility* Caster,
	FVector Center, int32 CastSequence, const FGameplayEffectSpecHandle& DamageSpec)
{
	FString Error;
	APawn* Source = IsValid(Caster) ? Cast<APawn>(Caster->GetAvatarActorFromActorInfo()) : nullptr;
	URPGBossAIComponent* BossStateComponent = Source ? Source->FindComponentByClass<URPGBossAIComponent>() : nullptr;
	URPGBossThreatComponent* Targets = Source ? Source->FindComponentByClass<URPGBossThreatComponent>() : nullptr;
	UAbilitySystemComponent* ASC = Source ? URPGAbilityFunctionLibrary::NativeGetWarriorASCFromActor(Source) : nullptr;
	if (!HasAuthority() || !GetIsReplicated() || bInitialized || HasActorBegunPlay() || !Source || !Source->HasAuthority() || !ASC ||
		!Caster->IsActive() || !Caster->IsBossExecutionValid() || !BossStateComponent || !Targets || CastSequence <= 0 ||
		Center.ContainsNaN() || !Caster->ActionTag.IsValid() || !FMath::IsFinite(Caster->Radius) || Caster->Radius <= 0.f || Caster->Radius > 2500.f ||
		!IsTimingValid(Caster->DurationSeconds, Caster->PulseInterval) || !Caster->SecurityProfile.bRequireAuthorityHitQuery ||
		FMath::CeilToInt(Caster->DurationSeconds / Caster->PulseInterval) > Caster->SecurityProfile.MaximumHitsPerActivation ||
		!Caster->SecurityProfile.IsValid(&Error) || !DamageSpec.IsValid() || !DamageSpec.Data->Def ||
		DamageSpec.Data->Def->DurationPolicy != EGameplayEffectDurationType::Instant || DamageSpec.Data->GetPeriod() != 0.f ||
		DamageSpec.Data->GetDuration() != UGameplayEffect::INSTANT_APPLICATION ||
		DamageSpec.Data->GetContext().GetInstigator() != Source || DamageSpec.Data->GetContext().GetInstigatorAbilitySystemComponent() != ASC)
	{
		return false;
	}
	const float Damage = DamageSpec.Data->GetSetByCallerMagnitude(RPGGameplayTags::Shared_SetByCaller_BaseDamage, false, 0.f);
	if (!FMath::IsFinite(ServerNow()) || !FMath::IsFinite(Damage) || Damage <= 0.f || Damage > Caster->SecurityProfile.MaximumDamagePerHit) { return false; }
	State.SourcePawn = Source;
	const FRPGBossState BossSnapshot = BossStateComponent->GetBossState();
	State.EncounterId = BossSnapshot.EncounterId;
	State.PhaseIndex = BossSnapshot.PhaseIndex;
	State.CastSequence = CastSequence;
	State.ActionTag = Caster->ActionTag;
	State.Center = Center;
	State.Radius = Caster->Radius;
	State.StartServerTime = ServerNow();
	State.EndServerTime = State.StartServerTime + Caster->DurationSeconds;
	State.PulseInterval = Caster->PulseInterval;
	SourceState = BossStateComponent;
	SourceThreat = Targets;
	SourceController = Source->GetController();
	SourceASC = ASC;
	FrozenSecurity = Caster->SecurityProfile;
	DamageTemplate = FGameplayEffectSpecHandle(new FGameplayEffectSpec(*DamageSpec.Data));
	// Delayed pulses carry their immutable area identity, not a subsequent reuse of the caster instance.
	FGameplayEffectContextHandle Context = DamageSpec.Data->GetContext().Duplicate();
	Context.AddSourceObject(this);
	DamageTemplate.Data->SetContext(Context);
	bInitialized = true;
	BossStateComponent->OnBossStateChanged.AddUniqueDynamic(this, &ThisClass::OnBossStateChanged);
	Source->OnDestroyed.AddUniqueDynamic(this, &ThisClass::OnSourceDestroyed);
	Source->ReceiveControllerChangedDelegate.AddUniqueDynamic(this, &ThisClass::OnSourceControllerChanged);
	return true;
}

bool ARPGBossPersistentHazard::IsServerContextValid() const
{
	if (!HasAuthority() || !bInitialized || IsActorBeingDestroyed() || !IsValid(State.SourcePawn) ||
		!State.SourcePawn->HasAuthority() || !SourceController.IsValid() || State.SourcePawn->GetController() != SourceController.Get() ||
		!SourceASC.IsValid() || SourceASC->GetAvatarActor() != State.SourcePawn ||
		URPGAbilityFunctionLibrary::NativeGetWarriorASCFromActor(State.SourcePawn) != SourceASC.Get() ||
		!SourceState.IsValid() || !SourceThreat.IsValid() || !DamageTemplate.IsValid() || !FMath::IsFinite(ServerNow()) || ServerNow() >= State.EndServerTime) { return false; }
	const FRPGBossState BossSnapshot = SourceState->GetBossState();
	return BossSnapshot.EncounterState == ERPGBossEncounterState::InCombat && BossSnapshot.EncounterId == State.EncounterId &&
		BossSnapshot.PhaseIndex == State.PhaseIndex;
}

void ARPGBossPersistentHazard::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		if (!IsServerContextValid()) { Destroy(); return; }
		GetWorldTimerManager().SetTimer(ExpiryTimer, FTimerDelegate::CreateWeakLambda(this, [this]() { Destroy(); }),
			static_cast<float>(State.EndServerTime - ServerNow()), false);
		Pulse();
	}
	RefreshPresentation();
}

void ARPGBossPersistentHazard::Pulse()
{
	if (!IsServerContextValid()) { if (HasAuthority()) { Destroy(); } return; }
	if (bApplyingPulse || LastPulseFrame == GFrameCounter || State.PulseCount >= FMath::Min(128, FrozenSecurity.MaximumHitsPerActivation)) { return; }
	TGuardValue<bool> ApplyingGuard(bApplyingPulse, true);
	LastPulseFrame = GFrameCounter;
	++State.PulseCount; // A miss consumes this pulse. Never catch up missed pulses in a burst.
	ForceNetUpdate();
	URPGHitQuerySubsystem* Queries = GetWorld()->GetSubsystem<URPGHitQuerySubsystem>();
	FRPGHitQueryContext Query;
	Query.SourceActor = State.SourcePawn;
	Query.QueryTransform = FTransform(State.Center);
	Query.Profile.Shape.Type = ERPGHitQueryShape::Sphere;
	Query.Profile.Shape.Radius = State.Radius;
	Query.Profile.Filter.TeamRule = ERPGHitQueryTeamRule::HostileOnly;
	Query.Profile.Filter.MaxResults = FrozenSecurity.MaximumTargetsPerQuery;
	TArray<FRPGHitQueryResult> Hits;
	if (Queries) { Queries->ExecuteHitQuery(Query, Hits); }
	if (!IsServerContextValid()) { return; }
	TArray<TWeakObjectPtr<AController>> Controllers;
	for (const FRPGHitQueryResult& Hit : Hits)
	{
		APawn* Participant = Cast<APawn>(Hit.TargetActor);
		Controllers.Add(Participant && SourceThreat->GetThreatForTarget(Participant) > 0.0 ? Participant->GetController() : nullptr);
	}
	const FGameplayEffectSpecHandle Template = DamageTemplate;
	TSet<TWeakObjectPtr<APawn>> Visited;
	for (int32 Index = 0; Index < Hits.Num(); ++Index)
	{
		if (!IsServerContextValid()) { break; }
		const FRPGHitQueryResult& Hit = Hits[Index];
		APawn* Participant = Cast<APawn>(Hit.TargetActor);
		if (!IsValid(Participant) || Hit.HitResult.GetActor() != Participant || !Controllers[Index].IsValid() ||
			Participant->GetController() != Controllers[Index].Get() || Visited.Contains(Participant) ||
			SourceThreat->GetThreatForTarget(Participant) <= 0.0) { continue; }
		Visited.Add(Participant);
		const FGameplayEffectSpecHandle Spec(new FGameplayEffectSpec(*Template.Data));
		URPGAbilityFunctionLibrary::ApplyGameplayEffectSpecHandleToServerHit(State.SourcePawn, Hit.HitResult, Spec, FrozenSecurity);
	}
	if (IsServerContextValid() && State.PulseCount < FMath::Min(128, FrozenSecurity.MaximumHitsPerActivation))
	{
		GetWorldTimerManager().SetTimer(PulseTimer, FTimerDelegate::CreateWeakLambda(this, [this]() { Pulse(); }), State.PulseInterval, false);
	}
}

bool ARPGBossPersistentHazard::IsPresentationActive() const
{
	if (!GetWorld() || (GetNetMode() == NM_Client && !GetWorld()->GetGameState()) || !IsValid(State.SourcePawn) ||
		!State.ActionTag.IsValid() || State.Center.ContainsNaN() || State.CastSequence <= 0 ||
		!FMath::IsFinite(State.Radius) || State.Radius <= 0.f || State.Radius > 2500.f ||
		!FMath::IsFinite(State.StartServerTime) || !FMath::IsFinite(State.EndServerTime) ||
		State.EndServerTime <= State.StartServerTime || State.EndServerTime - State.StartServerTime > 120.0 ||
		!FMath::IsFinite(ServerNow()) || ServerNow() >= State.EndServerTime || IsActorBeingDestroyed()) { return false; }
	const URPGBossAIComponent* BossStateComponent = State.SourcePawn->FindComponentByClass<URPGBossAIComponent>();
	const FRPGBossState BossSnapshot = BossStateComponent ? BossStateComponent->GetBossState() : FRPGBossState();
	return BossSnapshot.EncounterState == ERPGBossEncounterState::InCombat && BossSnapshot.EncounterId == State.EncounterId && BossSnapshot.PhaseIndex == State.PhaseIndex;
}

void ARPGBossPersistentHazard::RefreshPresentation()
{
	if (GetNetMode() == NM_DedicatedServer) { return; }
	if (Indicator) { Indicator->SetVisibility(false); }
	if (!IsPresentationActive()) { return; }
	const URPGBossTelegraphPresentationComponent* Styles = State.SourcePawn->FindComponentByClass<URPGBossTelegraphPresentationComponent>();
	const FRPGBossTelegraphStyle* Selected = nullptr;
	if (!Styles) { return; }
	for (const FRPGBossTelegraphStyle& Style : Styles->Styles)
	{
		if (Style.ActionTag == State.ActionTag) { if (Selected) { return; } Selected = &Style; }
	}
	UMaterialInterface* Material = Selected ? Selected->Material.Get() : nullptr;
	if (!Material || Material->GetMaterial()->MaterialDomain != MD_DeferredDecal ||
		!FMath::IsFinite(Selected->Color.R) || !FMath::IsFinite(Selected->Color.G) ||
		!FMath::IsFinite(Selected->Color.B) || !FMath::IsFinite(Selected->Color.A)) { return; }
	if (!Indicator)
	{
		Indicator = NewObject<UDecalComponent>(this, TEXT("BossHazardIndicator"));
		AddInstanceComponent(Indicator);
		Indicator->FadeScreenSize = 0.f;
		Indicator->RegisterComponent();
		IndicatorMaterial = UMaterialInstanceDynamic::Create(Material, this);
		Indicator->SetDecalMaterial(IndicatorMaterial);
		Indicator->DecalSize = FVector(100.f, State.Radius, State.Radius);
		FVector GroundLocation = State.Center;
		FHitResult Ground;
		FCollisionQueryParams Trace(SCENE_QUERY_STAT(BossHazardGround), false, State.SourcePawn);
		if (GetWorld()->LineTraceSingleByObjectType(Ground, State.Center + FVector(0.f, 0.f, 100.f),
			State.Center - FVector(0.f, 0.f, 500.f), FCollisionObjectQueryParams(ECC_WorldStatic), Trace)) { GroundLocation = Ground.ImpactPoint; }
		Indicator->SetWorldLocationAndRotation(GroundLocation, FRotator(-90.f, 0.f, 0.f));
		IndicatorMaterial->SetVectorParameterValue(TEXT("TelegraphColor"), Selected->Color);
	}
	IndicatorMaterial->SetScalarParameterValue(TEXT("TelegraphProgress"),
		static_cast<float>(FMath::Clamp((ServerNow() - State.StartServerTime) / (State.EndServerTime - State.StartServerTime), 0.0, 1.0)));
	Indicator->SetVisibility(true);
}

bool ARPGBossPersistentHazard::IsIndicatorVisible() const { return IsValid(Indicator) && Indicator->IsVisible(); }
void ARPGBossPersistentHazard::OnRep_State() { RefreshPresentation(); }

void ARPGBossPersistentHazard::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (HasAuthority() && !IsServerContextValid()) { Destroy(); return; }
	RefreshPresentation();
}

void ARPGBossPersistentHazard::OnBossStateChanged(const FRPGBossState& Previous, const FRPGBossState& Current)
{
	if (!IsServerContextValid()) { Destroy(); }
}

void ARPGBossPersistentHazard::OnSourceDestroyed(AActor* Source) { if (HasAuthority()) { Destroy(); } }

void ARPGBossPersistentHazard::OnSourceControllerChanged(APawn* Source, AController* Previous, AController* Current)
{
	if (HasAuthority() && Current != SourceController.Get()) { Destroy(); }
}

void ARPGBossPersistentHazard::EndPlay(const EEndPlayReason::Type Reason)
{
	GetWorldTimerManager().ClearTimer(PulseTimer);
	GetWorldTimerManager().ClearTimer(ExpiryTimer);
	if (SourceState.IsValid()) { SourceState->OnBossStateChanged.RemoveDynamic(this, &ThisClass::OnBossStateChanged); }
	if (IsValid(State.SourcePawn))
	{
		State.SourcePawn->OnDestroyed.RemoveDynamic(this, &ThisClass::OnSourceDestroyed);
		State.SourcePawn->ReceiveControllerChangedDelegate.RemoveDynamic(this, &ThisClass::OnSourceControllerChanged);
	}
	if (Indicator) { Indicator->DestroyComponent(); Indicator = nullptr; }
	IndicatorMaterial = nullptr;
	DamageTemplate = FGameplayEffectSpecHandle();
	bInitialized = false;
	Super::EndPlay(Reason);
}
