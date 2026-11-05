#include "AI/RPGBossAIComponent.h"
#include "Ability/RPGBossEnrageEffect.h"
#include "Ability/RPGBossPhaseInvulnerabilityEffect.h"
#include "Ability/RPGBossGameplayAbility.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "AI/RPGBossDefinition.h"
#include "AI/RPGBossEncounterProfiles.h"
#include "AI/RPGBossPatternExecutor.h"
#include "Attribute/RPGAttributeSet.h"
#include "Character/RPGNonPlayerCharacter.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Controller.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "RPGGameplayTags.h"

DEFINE_LOG_CATEGORY_STATIC(LogRPGBossAI, Log, All);

URPGBossAIComponent::URPGBossAIComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
	PatternExecutor = CreateDefaultSubobject<URPGBossPatternExecutor>(TEXT("PatternExecutor"));
}

void URPGBossAIComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(URPGBossAIComponent, State);
	DOREPLIFETIME(URPGBossAIComponent, Telegraph);
}

int32 URPGBossAIComponent::BeginTelegraph(FGameplayTag ActionTag, FVector AimLocation, float WindupSeconds)
{
	FRPGBossPatternExecution Execution;
	Execution.bShowTelegraph = ShouldShowTelegraphsByDefault();
	return BeginExecutionTelegraph(ActionTag, AimLocation, WindupSeconds, Execution, nullptr, nullptr);
}

int32 URPGBossAIComponent::BeginTelegraphWithShape(FGameplayTag ActionTag, const FTransform& QueryTransform,
	const FRPGHitQueryShape& Shape, float WindupSeconds)
{
	FRPGBossPatternExecution Execution;
	Execution.bShowTelegraph = ShouldShowTelegraphsByDefault();
	return BeginExecutionTelegraph(ActionTag, QueryTransform.GetLocation(), WindupSeconds, Execution, &QueryTransform, &Shape);
}

int32 URPGBossAIComponent::BeginExecutionTelegraph(FGameplayTag ActionTag, FVector AimLocation, float WindupSeconds,
	const FRPGBossPatternExecution& Execution, const FTransform* QueryTransform, const FRPGHitQueryShape* Shape)
{
	FRPGBossTelegraphGeometry Geometry;
	if (QueryTransform && Shape)
	{
		Geometry.bEnabled = true;
		Geometry.QueryTransform = *QueryTransform;
		Geometry.Shape = *Shape;
		FRPGBossTelegraphFootprint Footprint;
		if (!FRPGBossTelegraphFootprint::Build(Geometry, Footprint)) return 0;
		AimLocation = Footprint.Center;
	}
	return BeginTelegraphInternal(ActionTag, AimLocation, WindupSeconds, Geometry, Execution);
}

int32 URPGBossAIComponent::BeginTelegraphInternal(FGameplayTag ActionTag, FVector AimLocation, float WindupSeconds,
	const FRPGBossTelegraphGeometry& Geometry, const FRPGBossPatternExecution& Execution)
{
	if (!CanMutate() || bPublishingTelegraph || State.EncounterState != ERPGBossEncounterState::InCombat ||
		Telegraph.bActive || Telegraph.Sequence == MAX_int32 || !ActionTag.IsValid() ||
		AimLocation.ContainsNaN() || !FMath::IsFinite(WindupSeconds) || WindupSeconds <= 0.f ||
		WindupSeconds > 120.f || !GetWorld())
	{
		return 0;
	}
	const FRPGBossTelegraphState Previous = Telegraph;
	const AGameStateBase* GameState = GetWorld()->GetGameState();
	Telegraph.bActive = true;
	Telegraph.bShowIndicator = Execution.bShowTelegraph;
	Telegraph.PatternId = Execution.PatternId;
	Telegraph.PhaseIndex = State.PhaseIndex;
	Telegraph.EncounterId = State.EncounterId;
	++Telegraph.Sequence;
	Telegraph.ActionTag = ActionTag;
	Telegraph.AimLocation = AimLocation;
	Telegraph.Geometry = Geometry;
	Telegraph.StartServerTime = GameState ? GameState->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
	Telegraph.EndServerTime = Telegraph.StartServerTime + WindupSeconds;
	const int32 StartedEncounter = Telegraph.EncounterId;
	const int32 StartedSequence = Telegraph.Sequence;
	GetWorld()->GetTimerManager().SetTimer(TelegraphTimeout,
		FTimerDelegate::CreateWeakLambda(this, [this, StartedEncounter, StartedSequence]()
		{
			EndTelegraph(StartedEncounter, StartedSequence);
		}), WindupSeconds + 0.1f, false);
	PublishTelegraph(Previous);
	return StartedSequence;
}

bool URPGBossAIComponent::EndTelegraph(int32 ExpectedEncounterId, int32 ExpectedSequence)
{
	if (!CanMutate() || bPublishingTelegraph || !Telegraph.bActive ||
		Telegraph.EncounterId != ExpectedEncounterId || Telegraph.Sequence != ExpectedSequence)
	{
		return false;
	}
	ClearTelegraph();
	return true;
}

bool URPGBossAIComponent::CanMutate() const
{
	return IsValid(GetOwner()) && GetOwner()->HasAuthority() && !bPublishingState && !bPublishingTelegraph &&
		!bUpdatingEnrage && !bUpdatingPhaseProtection;
}

bool URPGBossAIComponent::BindAbilitySystem()
{
	UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(GetOwner());
	if (!IsValid(ASC) || ASC->GetAvatarActor() != GetOwner() ||
		!ASC->HasAttributeSetForAttribute(URPGAttributeSet::GetCurrentHealthAttribute()) ||
		!ASC->HasAttributeSetForAttribute(URPGAttributeSet::GetMaxHealthAttribute()))
	{
		return false;
	}
	if (BoundASC.Get() != ASC)
	{
		UnbindAbilitySystem();
		BoundASC = ASC;
		HealthChangedHandle = ASC->GetGameplayAttributeValueChangeDelegate(
			URPGAttributeSet::GetCurrentHealthAttribute()).AddUObject(this, &ThisClass::OnHealthChanged);
		MaxHealthChangedHandle = ASC->GetGameplayAttributeValueChangeDelegate(
			URPGAttributeSet::GetMaxHealthAttribute()).AddUObject(this, &ThisClass::OnHealthChanged);
	}
	if (APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		Pawn->ReceiveControllerChangedDelegate.AddUniqueDynamic(this, &ThisClass::OnBossControllerChanged);
	}
	return true;
}

void URPGBossAIComponent::UnbindAbilitySystem()
{
	ClearPhaseEntryAbility();
	if (APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		Pawn->ReceiveControllerChangedDelegate.RemoveDynamic(this, &ThisClass::OnBossControllerChanged);
	}
	ClearPhaseTimeout();
	ClearEnrage();
	ClearPhaseInvulnerability();
	if (UAbilitySystemComponent* ASC = BoundASC.Get())
	{
		ASC->GetGameplayAttributeValueChangeDelegate(URPGAttributeSet::GetCurrentHealthAttribute()).Remove(HealthChangedHandle);
		ASC->GetGameplayAttributeValueChangeDelegate(URPGAttributeSet::GetMaxHealthAttribute()).Remove(MaxHealthChangedHandle);
	}
	BoundASC.Reset();
	HealthChangedHandle.Reset();
	MaxHealthChangedHandle.Reset();
}

bool URPGBossAIComponent::ReadHealth(float& OutHealth, float& OutMaxHealth) const
{
	const UAbilitySystemComponent* ASC = BoundASC.Get();
	if (!ASC || ASC->GetAvatarActor() != GetOwner())
	{
		return false;
	}
	OutHealth = ASC->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute());
	OutMaxHealth = ASC->GetNumericAttribute(URPGAttributeSet::GetMaxHealthAttribute());
	return FMath::IsFinite(OutHealth) && FMath::IsFinite(OutMaxHealth) && OutMaxHealth > 0.0f;
}

const FRPGBossPhaseDefinition* URPGBossAIComponent::GetActivePhaseDefinition() const
{
	return GetOwner() && GetOwner()->HasAuthority() && State.EncounterState == ERPGBossEncounterState::InCombat &&
		ActiveDefinition && ActiveDefinition->Phases.IsValidIndex(State.PhaseIndex)
		? &ActiveDefinition->Phases[State.PhaseIndex] : nullptr;
}

bool URPGBossAIComponent::ShouldShowTelegraphsByDefault() const
{
	return !ActiveDefinition || ActiveDefinition->bShowTelegraphsByDefault;
}

bool URPGBossAIComponent::CanStartPattern(int32 ExpectedRevision) const
{
	return CanMutate() && State.Revision == ExpectedRevision && GetActivePhaseDefinition();
}

bool URPGBossAIComponent::TryActivatePattern(FGameplayTag PatternId, int32 ExpectedEncounterId, FGameplayTag ExpectedPhase)
{
	return CanMutate() && State.EncounterState == ERPGBossEncounterState::InCombat &&
		State.EncounterId == ExpectedEncounterId && State.PhaseTag == ExpectedPhase &&
		PatternExecutor->TryActivate(PatternId, State.Revision);
}

bool URPGBossAIComponent::StartEncounter()
{
	if (!CanMutate() || State.EncounterState != ERPGBossEncounterState::Idle || (!Definition && !EncounterProfiles))
	{
		return false;
	}
	if (const ARPGNonPlayerCharacter* NPC = Cast<ARPGNonPlayerCharacter>(GetOwner()))
	{
		if (!NPC->IsNPCStartUpDataReady() || !IsValid(NPC->GetController()) || !NPC->HasActorBegunPlay())
		{
			return false;
		}
	}
	FText Error;
	const URPGBossDefinition* SelectedDefinition = EncounterProfiles ? EncounterProfiles->ResolveForWorld(GetWorld(), Error) : Definition.Get();
	float Health = 0.0f;
	float MaxHealth = 0.0f;
	if (!SelectedDefinition || !SelectedDefinition->ValidateDefinition(Error) || !BindAbilitySystem() || !ReadHealth(Health, MaxHealth) || Health <= 0.0f)
	{
		UE_LOG(LogRPGBossAI, Warning, TEXT("Cannot start %s: valid phase data and an initialized, living ASC are required. %s"),
			*GetNameSafe(GetOwner()), *Error.ToString());
		return false;
	}
	ActiveDefinition = DuplicateObject<URPGBossDefinition>(SelectedDefinition, this);
	const int32 InitialPhase = ActiveDefinition->ResolvePhase(FMath::Clamp(Health, 0.0f, MaxHealth) / MaxHealth, 0);
	if (!EnsurePhaseInvulnerability(ActiveDefinition->Phases[InitialPhase].bInvulnerable))
	{
		ActiveDefinition = nullptr;
		UE_LOG(LogRPGBossAI, Warning, TEXT("Cannot start %s: phase protection could not be applied."), *GetNameSafe(GetOwner()));
		return false;
	}
	const FRPGBossState Previous = State;
	State.EncounterState = ERPGBossEncounterState::InCombat;
	++State.EncounterId;
	State.PhaseIndex = InitialPhase;
	State.PhaseTag = ActiveDefinition->Phases[State.PhaseIndex].PhaseTag;
	State.bEnraged = false;
	State.EnrageServerTime = 0.0;
	StartPhaseTimeout();
	if (ActiveDefinition->EnrageSeconds > 0.f)
	{
		const AGameStateBase* GameState = GetWorld()->GetGameState();
		State.EnrageServerTime = (GameState ? GameState->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds()) +
			static_cast<double>(ActiveDefinition->EnrageSeconds);
		const int32 StartedEncounter = State.EncounterId;
		GetWorld()->GetTimerManager().SetTimer(EnrageTimeout,
			FTimerDelegate::CreateWeakLambda(this, [this, StartedEncounter]() { TryAutoEnrage(StartedEncounter); }),
			ActiveDefinition->EnrageSeconds, false);
	}
	PublishState(Previous);
	EvaluateHealth();
	return true;
}

bool URPGBossAIComponent::AdvanceToPhase(int32 ExpectedEncounterId, FGameplayTag ExpectedCurrentPhase, FGameplayTag NextPhase)
{
	if (!CanMutate() || State.EncounterState != ERPGBossEncounterState::InCombat ||
		State.EncounterId != ExpectedEncounterId || !ExpectedCurrentPhase.IsValid() || !NextPhase.IsValid() ||
		State.PhaseTag != ExpectedCurrentPhase || !GetActivePhaseDefinition()) { return false; }
	const int32 NextIndex = State.PhaseIndex + 1;
	if (!ActiveDefinition->Phases.IsValidIndex(NextIndex) ||
		!ActiveDefinition->Phases[NextIndex].bRequiresExplicitTransition ||
		ActiveDefinition->Phases[NextIndex].PhaseTag != NextPhase) { return false; }
	if (const ARPGNonPlayerCharacter* NPC = Cast<ARPGNonPlayerCharacter>(GetOwner()))
	{
		if (!NPC->IsNPCStartUpDataReady() || !IsValid(NPC->GetController()) || !NPC->HasActorBegunPlay()) { return false; }
	}
	float Health = 0.f;
	float MaxHealth = 0.f;
	if (!ReadHealth(Health, MaxHealth) || Health <= 0.f) { return false; }
	if (HasPhaseTimeExpired())
	{
		TryPhaseTimeout(State.EncounterId, State.PhaseIndex);
		return false;
	}
	if (!EnsurePhaseInvulnerability(ActiveDefinition->Phases[NextIndex].bInvulnerable)) { return false; }
	const FRPGBossState Previous = State;
	State.PhaseIndex = NextIndex;
	State.PhaseTag = ActiveDefinition->Phases[NextIndex].PhaseTag;
	StartPhaseTimeout();
	// Reuse the HP transition path's boundary: clear the old windup before
	// listeners cancel abilities, retire summons/hazards and read the new phase.
	ClearTelegraph();
	PublishState(Previous);
	// Observers may change health, and HP-only successors can already be due.
	EvaluateHealth();
	return true;
}

bool URPGBossAIComponent::MarkWiped()
{
	return MarkWipedInternal(false);
}

bool URPGBossAIComponent::MarkWipedInternal(bool bDueToPhaseTimeout)
{
	if (!CanMutate() || State.EncounterState != ERPGBossEncounterState::InCombat)
	{
		return false;
	}
	const FRPGBossState Previous = State;
	State.EncounterState = ERPGBossEncounterState::Wiped;
	State.bPhaseTimedOut = bDueToPhaseTimeout;
	ClearPhaseTimeout();
	ClearPhaseInvulnerability();
	ClearEnrage();
	ClearTelegraph();
	PublishState(Previous);
	return true;
}

bool URPGBossAIComponent::BeginReset()
{
	if (!CanMutate() || (State.EncounterState != ERPGBossEncounterState::InCombat &&
		State.EncounterState != ERPGBossEncounterState::Wiped))
	{
		return false;
	}
	const FRPGBossState Previous = State;
	State.EncounterState = ERPGBossEncounterState::Resetting;
	ClearPhaseTimeout();
	State.bPhaseTimedOut = false;
	ClearPhaseInvulnerability();
	State.PhaseIndex = INDEX_NONE;
	State.PhaseTag = FGameplayTag();
	State.bEnraged = false;
	ClearEnrage();
	ClearTelegraph();
	PublishState(Previous);
	return true;
}

bool URPGBossAIComponent::CompleteReset()
{
	float Health = 0.0f;
	float MaxHealth = 0.0f;
	if (!CanMutate() || State.EncounterState != ERPGBossEncounterState::Resetting ||
		!BindAbilitySystem() || !ReadHealth(Health, MaxHealth) || Health <= 0.0f)
	{
		return false;
	}
	const FRPGBossState Previous = State;
	State.EncounterState = ERPGBossEncounterState::Idle;
	ActiveDefinition = nullptr;
	PublishState(Previous);
	return true;
}

bool URPGBossAIComponent::SetEnraged(bool bNewEnraged)
{
	float Health = 0.f;
	float MaxHealth = 0.f;
	if (!CanMutate() || State.EncounterState != ERPGBossEncounterState::InCombat || State.bEnraged == bNewEnraged ||
		!ActiveDefinition || !ReadHealth(Health, MaxHealth) || Health <= 0.f)
	{
		return false;
	}
	const FRPGBossState Previous = State;
	{
		// Attribute/GE observers must not start a nested lifecycle transaction while the owned handle is being assigned.
		TGuardValue<bool> Guard(bUpdatingEnrage, true);
		if (bNewEnraged && ActiveDefinition->EnragedAttackMultiplier > 1.f)
		{
			UAbilitySystemComponent* ASC = BoundASC.Get();
			const float Attack = ASC->GetNumericAttribute(URPGAttributeSet::GetAttackAttribute());
			if (!FMath::IsFinite(Attack) || Attack < 0.f || !FMath::IsFinite(Attack * ActiveDefinition->EnragedAttackMultiplier)) { return false; }
			const FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(URPGBossEnrageEffect::StaticClass(), 1.f, ASC->MakeEffectContext());
			if (!Spec.IsValid()) { return false; }
			Spec.Data->SetSetByCallerMagnitude(URPGBossEnrageEffect::GetMultiplierTag(), ActiveDefinition->EnragedAttackMultiplier);
			const FActiveGameplayEffectHandle Applied = ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data);
			if (!Applied.IsValid() || !IsValid(ASC) || !ASC->GetActiveGameplayEffect(Applied) || !IsValid(GetOwner()) || BoundASC.Get() != ASC ||
				State.EncounterState != ERPGBossEncounterState::InCombat || State.EncounterId != Previous.EncounterId)
			{
				if (Applied.IsValid() && IsValid(ASC)) { ASC->RemoveActiveGameplayEffect(Applied); }
				return false;
			}
			EnrageEffectHandle = Applied;
		}
		else if (!bNewEnraged)
		{
			ClearEnrage();
		}
		if (!IsValid(GetOwner()) || !BoundASC.IsValid() || !GetWorld() ||
			State.EncounterState != ERPGBossEncounterState::InCombat || State.EncounterId != Previous.EncounterId) { return false; }
		State.bEnraged = bNewEnraged;
		State.EnrageServerTime = 0.0;
		GetWorld()->GetTimerManager().ClearTimer(EnrageTimeout);
	}
	PublishState(Previous);
	EvaluateHealth();
	return true;
}

double URPGBossAIComponent::GetServerTime() const
{
	const AGameStateBase* Clock = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	return Clock ? Clock->GetServerWorldTimeSeconds() : (GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0);
}

bool URPGBossAIComponent::HasPhaseTimeExpired() const
{
	return State.EncounterState == ERPGBossEncounterState::InCombat && State.PhaseEndServerTime > 0.0 &&
		GetServerTime() >= State.PhaseEndServerTime;
}

void URPGBossAIComponent::ClearPhaseTimeout()
{
	if (GetWorld()) { GetWorld()->GetTimerManager().ClearTimer(PhaseTimeout); }
	State.PhaseEndServerTime = 0.0;
}

void URPGBossAIComponent::StartPhaseTimeout()
{
	ClearPhaseTimeout();
	State.bPhaseTimedOut = false;
	const FRPGBossPhaseDefinition* Phase = GetActivePhaseDefinition();
	if (!Phase || Phase->TimeLimitSeconds <= 0.f || !GetWorld()) { return; }
	State.PhaseEndServerTime = GetServerTime() + static_cast<double>(Phase->TimeLimitSeconds);
	const int32 Encounter = State.EncounterId;
	const int32 PhaseIndex = State.PhaseIndex;
	GetWorld()->GetTimerManager().SetTimer(PhaseTimeout,
		FTimerDelegate::CreateWeakLambda(this, [this, Encounter, PhaseIndex]() { TryPhaseTimeout(Encounter, PhaseIndex); }),
		Phase->TimeLimitSeconds, false);
}

void URPGBossAIComponent::TryPhaseTimeout(int32 ExpectedEncounterId, int32 ExpectedPhaseIndex)
{
	if (!IsValid(GetOwner()) || GetOwner()->IsActorBeingDestroyed() || !GetOwner()->HasAuthority() || !GetWorld() ||
		State.EncounterState != ERPGBossEncounterState::InCombat || State.EncounterId != ExpectedEncounterId ||
		State.PhaseIndex != ExpectedPhaseIndex || State.PhaseEndServerTime <= 0.0) { return; }
	if (!CanMutate())
	{
		GetWorld()->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this,
			[this, ExpectedEncounterId, ExpectedPhaseIndex]() { TryPhaseTimeout(ExpectedEncounterId, ExpectedPhaseIndex); }));
		return;
	}
	if (!HasPhaseTimeExpired())
	{
		const float Remaining = static_cast<float>(FMath::Max(.01, State.PhaseEndServerTime - GetServerTime()));
		GetWorld()->GetTimerManager().SetTimer(PhaseTimeout, FTimerDelegate::CreateWeakLambda(this,
			[this, ExpectedEncounterId, ExpectedPhaseIndex]() { TryPhaseTimeout(ExpectedEncounterId, ExpectedPhaseIndex); }), Remaining, false);
		return;
	}
	float Health = 0.f;
	float MaxHealth = 0.f;
	// Do not convert an already lethal hit into a recoverable wipe.
	if (ReadHealth(Health, MaxHealth) && Health <= 0.f) { EvaluateHealth(); return; }
	MarkWipedInternal(true);
}

bool URPGBossAIComponent::EnsurePhaseInvulnerability(bool bEnabled)
{
	UAbilitySystemComponent* ASC = BoundASC.Get();
	if (!IsValid(GetOwner()) || GetOwner()->IsActorBeingDestroyed() || !GetOwner()->HasAuthority() ||
		!ASC || ASC->GetAvatarActor() != GetOwner()) { return false; }
	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	const TWeakObjectPtr<AController> Controller = OwnerPawn ? OwnerPawn->GetController() : nullptr;
	const auto IsContextCurrent = [&]()
	{
		return IsValid(GetOwner()) && !GetOwner()->IsActorBeingDestroyed() && GetOwner()->HasAuthority() &&
			IsValid(ASC) && BoundASC.Get() == ASC && ASC->GetAvatarActor() == GetOwner() &&
			(!OwnerPawn || OwnerPawn->GetController() == Controller.Get());
	};
	if (!bEnabled) { ClearPhaseInvulnerability(); return IsContextCurrent(); }
	if (PhaseInvulnerabilityHandle.IsValid() && ASC->GetActiveGameplayEffect(PhaseInvulnerabilityHandle)) { return true; }
	PhaseInvulnerabilityHandle.Invalidate();
	TGuardValue<bool> Guard(bUpdatingPhaseProtection, true);
	const FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(URPGBossPhaseInvulnerabilityEffect::StaticClass(), 1.f, ASC->MakeEffectContext());
	if (!Spec.IsValid()) { return false; }
	Spec.Data->DynamicGrantedTags.AddTag(RPGGameplayTags::Shared_Status_Invincible);
	const FActiveGameplayEffectHandle Applied = ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data);
	if (!Applied.IsValid() || !IsContextCurrent() || !ASC->GetActiveGameplayEffect(Applied))
	{
		if (Applied.IsValid() && IsValid(ASC)) { ASC->RemoveActiveGameplayEffect(Applied); }
		return false;
	}
	PhaseInvulnerabilityHandle = Applied;
	return true;
}

void URPGBossAIComponent::ClearPhaseInvulnerability()
{
	TGuardValue<bool> Guard(bUpdatingPhaseProtection, true);
	const FActiveGameplayEffectHandle Previous = PhaseInvulnerabilityHandle;
	PhaseInvulnerabilityHandle.Invalidate();
	if (UAbilitySystemComponent* ASC = BoundASC.Get(); ASC && GetOwner() && GetOwner()->HasAuthority() && Previous.IsValid())
	{
		// Remove only this component's effect, never another ability's invincibility tag.
		ASC->RemoveActiveGameplayEffect(Previous);
	}
}

void URPGBossAIComponent::TryAutoEnrage(int32 ExpectedEncounterId)
{
	if (!IsValid(GetOwner()) || !GetOwner()->HasAuthority() || State.EncounterId != ExpectedEncounterId ||
		State.EncounterState != ERPGBossEncounterState::InCombat || State.bEnraged || State.EnrageServerTime <= 0.0) { return; }
	if (!CanMutate())
	{
		GetWorld()->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this,
			[this, ExpectedEncounterId]() { TryAutoEnrage(ExpectedEncounterId); }));
		return;
	}
	SetEnraged(true);
}

void URPGBossAIComponent::ClearEnrage()
{
	TGuardValue<bool> Guard(bUpdatingEnrage, true);
	if (GetWorld()) { GetWorld()->GetTimerManager().ClearTimer(EnrageTimeout); }
	State.bEnraged = false;
	State.EnrageServerTime = 0.0;
	const FActiveGameplayEffectHandle PreviousHandle = EnrageEffectHandle;
	EnrageEffectHandle.Invalidate();
	if (UAbilitySystemComponent* ASC = BoundASC.Get(); ASC && GetOwner() && GetOwner()->HasAuthority() && PreviousHandle.IsValid())
	{
		ASC->RemoveActiveGameplayEffect(PreviousHandle);
	}
}

void URPGBossAIComponent::OnHealthChanged(const FOnAttributeChangeData& Change)
{
	if ((bPublishingState || bUpdatingEnrage || bUpdatingPhaseProtection) && GetWorld())
	{
		GetWorld()->GetTimerManager().SetTimerForNextTick(
			FTimerDelegate::CreateWeakLambda(this, [this]() { EvaluateHealth(); }));
		return;
	}
	EvaluateHealth();
}

void URPGBossAIComponent::EvaluateHealth()
{
	float Health = 0.0f;
	float MaxHealth = 0.0f;
	if (!CanMutate() || State.EncounterState != ERPGBossEncounterState::InCombat ||
		!ActiveDefinition || !ReadHealth(Health, MaxHealth))
	{
		return;
	}
	const FRPGBossState Previous = State;
	if (Health <= 0.0f)
	{
		State.EncounterState = ERPGBossEncounterState::Dead;
		ClearPhaseTimeout();
		State.bPhaseTimedOut = false;
		ClearPhaseInvulnerability();
		State.bEnraged = false;
		ClearEnrage();
		ClearTelegraph();
	}
	else
	{
		// Reject late HP-threshold progress even before the timer manager runs.
		if (HasPhaseTimeExpired()) { MarkWipedInternal(true); return; }
		const int32 NextPhase = ActiveDefinition->ResolvePhase(FMath::Clamp(Health, 0.0f, MaxHealth) / MaxHealth, State.PhaseIndex);
		if (NextPhase == Previous.PhaseIndex)
		{
			return;
		}
		if (!EnsurePhaseInvulnerability(ActiveDefinition->Phases[NextPhase].bInvulnerable))
		{
			UE_LOG(LogRPGBossAI, Warning, TEXT("Cannot change phase for %s: phase protection could not be applied."), *GetNameSafe(GetOwner()));
			return;
		}
		State.PhaseIndex = NextPhase;
		State.PhaseTag = ActiveDefinition->Phases[State.PhaseIndex].PhaseTag;
		StartPhaseTimeout();
		// Ability cancellation runs inside the state broadcast, where EndTelegraph
		// is intentionally blocked against reentrancy. Clear the old phase here.
		ClearTelegraph();
	}
	PublishState(Previous);
}

void URPGBossAIComponent::ClearPhaseEntryAbility()
{
	++PhaseEntryGeneration;
	if (GetWorld()) { GetWorld()->GetTimerManager().ClearTimer(PhaseEntryTimer); }
	PhaseEntryController.Reset();
	PhaseEntryASC.Reset();
}

void URPGBossAIComponent::StartPhaseEntryAbility()
{
	ClearPhaseEntryAbility();
	const FRPGBossPhaseDefinition* Phase = GetActivePhaseDefinition();
	APawn* Pawn = Cast<APawn>(GetOwner());
	if (!Phase || !Phase->GetPhaseEntryAbility() || !Pawn || !IsValid(Pawn->GetController()) || !BoundASC.IsValid() || !GetWorld()) { return; }
	PhaseEntryController = Pawn->GetController();
	PhaseEntryASC = BoundASC;
	const int32 Encounter = State.EncounterId;
	const int32 PhaseIndex = State.PhaseIndex;
	const uint64 Generation = PhaseEntryGeneration;
	// All phase listeners must finish stopping the old BT/abilities and arming
	// objectives before the new ability can publish a windup or spawn a wave.
	PhaseEntryTimer = GetWorld()->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this,
		[this, Encounter, PhaseIndex, Generation]() { TryPhaseEntryAbility(Encounter, PhaseIndex, Generation); }));
}

void URPGBossAIComponent::TryPhaseEntryAbility(int32 ExpectedEncounterId, int32 ExpectedPhaseIndex, uint64 ExpectedGeneration)
{
	APawn* Pawn = Cast<APawn>(GetOwner());
	UAbilitySystemComponent* ASC = PhaseEntryASC.Get();
	const auto IsCurrent = [&]()
	{
		return PhaseEntryGeneration == ExpectedGeneration && IsValid(Pawn) && !Pawn->IsActorBeingDestroyed() && Pawn->HasAuthority() &&
			PhaseEntryController.IsValid() && Pawn->GetController() == PhaseEntryController.Get() &&
			IsValid(ASC) && BoundASC.Get() == ASC && ASC->GetAvatarActor() == Pawn &&
			State.EncounterState == ERPGBossEncounterState::InCombat && State.EncounterId == ExpectedEncounterId &&
			State.PhaseIndex == ExpectedPhaseIndex && GetWorld();
	};
	if (!IsCurrent()) { return; }
	const auto Retry = [&]()
	{
		GetWorld()->GetTimerManager().SetTimer(PhaseEntryTimer, FTimerDelegate::CreateWeakLambda(this,
			[this, ExpectedEncounterId, ExpectedPhaseIndex, ExpectedGeneration]()
			{ TryPhaseEntryAbility(ExpectedEncounterId, ExpectedPhaseIndex, ExpectedGeneration); }), .1f, false);
	};
	if (!CanMutate()) { Retry(); return; }
	float Health = 0.f;
	float MaxHealth = 0.f;
	if (!ReadHealth(Health, MaxHealth) || Health <= 0.f) { EvaluateHealth(); return; }
	if (HasPhaseTimeExpired()) { TryPhaseTimeout(ExpectedEncounterId, ExpectedPhaseIndex); return; }
	const FRPGBossPhaseDefinition* Phase = GetActivePhaseDefinition();
	if (!Phase || !Phase->GetPhaseEntryAbility() || !Phase->bSuspendCombatAI) { ClearPhaseEntryAbility(); return; }
	const FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromClass(Phase->GetPhaseEntryAbility());
	// Use the public GAS base interface; the boss override narrows access.
	UGameplayAbility* Ability = Spec ? Spec->GetPrimaryInstance() : nullptr;
	if (!Cast<URPGBossGameplayAbility>(Ability) || Spec->IsActive() || !ASC->AbilityActorInfo.IsValid())
	{
		UE_LOG(LogRPGBossAI, Warning, TEXT("Cannot launch phase entry for %s: %s must be granted, instanced and inactive."),
			*GetNameSafe(Pawn), *GetNameSafe(Phase->GetPhaseEntryAbility().Get()));
		ClearPhaseEntryAbility();
		return;
	}
	const FGameplayAbilitySpecHandle Handle = Spec->Handle;
	const FGameplayTag EntryId = Phase->PhaseEntryPattern.PatternId;
	const int32 ExpectedRevision = State.Revision;
	const bool bCanActivate = EntryId.IsValid() ? PatternExecutor->CanActivate(EntryId, ExpectedRevision) :
		Ability->CanActivateAbility(Handle, ASC->AbilityActorInfo.Get());
	// Authored GAS checks can synchronously reset the encounter or replace its
	// controller/ASC. A canceled reservation must not be rescheduled by this stack.
	if (!IsCurrent()) { return; }
	if (!bCanActivate) { Retry(); return; }
	const FGameplayAbilitySpec* CurrentSpec = ASC->FindAbilitySpecFromHandle(Handle);
	if (!CurrentSpec || CurrentSpec->GetPrimaryInstance() != Ability || CurrentSpec->IsActive()) { ClearPhaseEntryAbility(); return; }
	// Consume before GAS callbacks. A synchronous cancellation/failure must not
	// cause another summon, another cost payment or a cooldown refund/recast.
	ClearPhaseEntryAbility();
	if (EntryId.IsValid()) PatternExecutor->TryActivate(EntryId, ExpectedRevision);
	else ASC->TryActivateAbility(Handle, false);
}

void URPGBossAIComponent::OnBossControllerChanged(APawn* Pawn, AController* Previous, AController* Current)
{
	if (Previous != Current) { ClearPhaseEntryAbility(); PatternExecutor->Reset(); }
}

void URPGBossAIComponent::PublishState(const FRPGBossState& PreviousState)
{
	++State.Revision;
	const AGameStateBase* GameState = GetWorld()->GetGameState();
	State.ServerStateTime = GameState ? GameState->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
	GetOwner()->FlushNetDormancy();
	GetOwner()->ForceNetUpdate();
	UE_LOG(LogRPGBossAI, Log, TEXT("%s Encounter=%d Revision=%d State=%d Phase=%s Enraged=%d"),
		*GetNameSafe(GetOwner()), State.EncounterId, State.Revision, static_cast<int32>(State.EncounterState),
		*State.PhaseTag.ToString(), State.bEnraged);
	if (State.EncounterState != PreviousState.EncounterState || State.EncounterId != PreviousState.EncounterId ||
		State.PhaseIndex != PreviousState.PhaseIndex)
	{
		PatternExecutor->Reset();
		StartPhaseEntryAbility();
	}
	TGuardValue<bool> Guard(bPublishingState, true);
	OnBossStateChanged.Broadcast(PreviousState, State);
}

void URPGBossAIComponent::OnRep_State(FRPGBossState PreviousState)
{
	TGuardValue<bool> Guard(bPublishingState, true);
	OnBossStateChanged.Broadcast(PreviousState, State);
}

void URPGBossAIComponent::ClearTelegraph()
{
	if (!Telegraph.bActive) { return; }
	if (GetWorld()) { GetWorld()->GetTimerManager().ClearTimer(TelegraphTimeout); }
	const FRPGBossTelegraphState Previous = Telegraph;
	Telegraph.bActive = false;
	Telegraph.bShowIndicator = false;
	Telegraph.PatternId = {};
	Telegraph.ActionTag = FGameplayTag();
	Telegraph.Geometry = {};
	PublishTelegraph(Previous);
}

void URPGBossAIComponent::PublishTelegraph(const FRPGBossTelegraphState& PreviousState)
{
	GetOwner()->FlushNetDormancy();
	GetOwner()->ForceNetUpdate();
	TGuardValue<bool> Guard(bPublishingTelegraph, true);
	OnTelegraphChanged.Broadcast(PreviousState, Telegraph);
}

void URPGBossAIComponent::OnRep_Telegraph(FRPGBossTelegraphState PreviousState)
{
	TGuardValue<bool> Guard(bPublishingTelegraph, true);
	OnTelegraphChanged.Broadcast(PreviousState, Telegraph);
}

void URPGBossAIComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	PatternExecutor->Reset();
	if (GetWorld()) { GetWorld()->GetTimerManager().ClearTimer(TelegraphTimeout); }
	UnbindAbilitySystem();
	Super::EndPlay(EndPlayReason);
}
