#include "Tests/RPGBossMapE2EProbeComponent.h"

#include "AI/RPGBossAIComponent.h"
#include "AI/RPGBossThreatComponent.h"
#include "AI/RPGBossTelegraphPresentationComponent.h"
#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbility.h"
#include "GameplayEffect.h"
#include "Component/Combat/PlayerCombatComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Item/Weapon/RPGWeaponBase.h"
#include "Attribute/RPGAttributeSet.h"
#include "BrainComponent.h"
#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BTNode.h"
#include "Blueprint/UserWidget.h"
#include "Character/RPGBossCharacter.h"
#include "Character/RPGPlayer.h"
#include "Controller/RPGPlayerController.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "NavigationSystem.h"
#include "NavigationData.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameState/RPGBossBattleGameState.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "RPGGameplayTags.h"
#include "UI/RPGBossBattleRetryBinding.h"
#include "Tests/RPGBossAttackScenario.h"
#include "UObject/UnrealType.h"

namespace
{
	constexpr const TCHAR* BaseSwordManClassPath = TEXT("/Game/Blueprints/Character/NPC/SwordMan/BP_RaidSwordMan.BP_RaidSwordMan_C");
	constexpr const TCHAR* SwordManVariantClassPath = TEXT("/Game/Blueprints/Character/NPC/Raid/SwordManVariant/BP_RaidSwordManVariant.BP_RaidSwordManVariant_C");
	constexpr const TCHAR* SummonMechanicClassPath = TEXT("/Game/Blueprints/Character/NPC/Raid/SwordManSummonMechanic/BP_RaidSwordManSummonMechanic.BP_RaidSwordManSummonMechanic_C");
	bool Enabled()
	{
#if UE_BUILD_SHIPPING
		return false;
#else
		return FParse::Param(FCommandLine::Get(), TEXT("RPGBossMapE2E"));
#endif
	}
	bool Alive(APawn* Pawn)
	{
		ARPGPlayer* Player = Cast<ARPGPlayer>(Pawn);
		UAbilitySystemComponent* ASC = IsValid(Player) ? Player->GetAbilitySystemComponent() : nullptr;
		if (!ASC || ASC->GetAvatarActor() != Player) { return false; }
		const float HP = ASC->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute());
		return FMath::IsFinite(HP) && HP > 0.f && !ASC->HasMatchingGameplayTag(RPGGameplayTags::Shared_Status_Death);
	}
}

URPGBossMapE2EProbeComponent::URPGBossMapE2EProbeComponent()
{
	SetIsReplicatedByDefault(true);
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickInterval = .1f;
}

void URPGBossMapE2EProbeComponent::BeginPlay()
{
	Super::BeginPlay();
	bEnabled = Enabled();
	if (!bEnabled) { return; }
	bDeathAbilityScenario = FParse::Param(FCommandLine::Get(), TEXT("RPGBossMapE2EDeathAbilities"));
	bLightHitScenario = FParse::Param(FCommandLine::Get(), TEXT("RPGBossMapE2ELightHit"));
	bStunScenario = FParse::Param(FCommandLine::Get(), TEXT("RPGBossMapE2EStun"));
	bKnockbackScenario = FParse::Param(FCommandLine::Get(), TEXT("RPGBossMapE2EKnockback"));
	bKnockbackScenario |= FParse::Param(FCommandLine::Get(), TEXT("RPGBossMapE2EAirborne"));
	bKnockbackScenario |= FParse::Param(FCommandLine::Get(), TEXT("RPGBossMapE2ESlow"));
	bKnockbackScenario |= FParse::Param(FCommandLine::Get(), TEXT("RPGBossMapE2EBurn"));
	bKnockbackScenario |= FParse::Param(FCommandLine::Get(), TEXT("RPGBossMapE2EStagger"));
	bKnockbackScenario |= FParse::Param(FCommandLine::Get(), TEXT("RPGBossMapE2EMontageImmunity"));
	bKnockbackScenario |= FParse::Param(FCommandLine::Get(), TEXT("RPGBossMapE2EImmunity"));
	bLightHitScenario |= bStunScenario || bKnockbackScenario;
	bLightHitInputScenario = bLightHitScenario && FParse::Param(FCommandLine::Get(), TEXT("RPGBossMapE2ELightHitInput"));
	if (bLightHitInputScenario || bStunScenario || bKnockbackScenario) PrimaryComponentTick.TickInterval = 0.f;
	bNaturalCombatScenario = FParse::Param(FCommandLine::Get(), TEXT("RPGBossMapE2ENaturalCombat"));
	bCombatPhaseScenario = FParse::Param(FCommandLine::Get(), TEXT("RPGBossMapE2ECombatPhases"));
	bBossAreaStrikeScenario = FParse::Param(FCommandLine::Get(), TEXT("RPGBossMapE2EAreaStrike"));
	bCombatPhaseScenario |= bBossAreaStrikeScenario;
	bDodgeScenario = FParse::Param(FCommandLine::Get(), TEXT("RPGBossMapE2EDodge"));
	bPlayerAttackScenario = FParse::Param(FCommandLine::Get(), TEXT("RPGBossMapE2EPlayerAttack"));
	bBossDeathScenario = FParse::Param(FCommandLine::Get(), TEXT("RPGBossMapE2EBossDeath"));
	bSummonMechanicScenario = FParse::Param(FCommandLine::Get(), TEXT("RPGBossMapE2ESummonMechanic"));
	bSummonPlayerKillScenario = FParse::Param(FCommandLine::Get(), TEXT("RPGBossMapE2ESummonPlayerKill"));
	if (bSummonMechanicScenario && FParse::Param(FCommandLine::Get(), TEXT("RPGBossMapE2ESwordManVariant"))) { Fail(TEXT("ConflictingBossPresets")); return; }
	const int32 SelectedSkills = RPGBossAttackScenarios::CountSelectedSkills(FCommandLine::Get());
	if (bSummonPlayerKillScenario && (!bSummonMechanicScenario || SelectedSkills != 1 ||
		!FParse::Param(FCommandLine::Get(), TEXT("RPGBossMapE2EAssultBlade")))) { Fail(TEXT("SummonPlayerKillRequiresAssultBlade")); return; }
	bPlayerAttackScenario |= SelectedSkills > 0 && !bSummonPlayerKillScenario;
	if (SelectedSkills > 1) { Fail(TEXT("ConflictingPlayerSkills")); return; }
	bNaturalCombatScenario |= bCombatPhaseScenario || bDodgeScenario || bPlayerAttackScenario || bBossDeathScenario || bSummonMechanicScenario;
	if (int32(bCombatPhaseScenario) + int32(bDodgeScenario) + int32(bPlayerAttackScenario) + int32(bBossDeathScenario) + int32(bSummonMechanicScenario) > 1) { Fail(TEXT("ConflictingCombatScenarios")); return; }
	if (bDeathAbilityScenario && bNaturalCombatScenario) { Fail(TEXT("ConflictingScenarios")); return; }
	if (bLightHitScenario && (bDeathAbilityScenario || bNaturalCombatScenario)) { Fail(TEXT("ConflictingLightHitScenario")); return; }
	Deadline = FPlatformTime::Seconds() + 900.0;
	if (ARPGBossBattleGameMode* Mode = Cast<ARPGBossBattleGameMode>(GetOwner()))
	{
		if (!GetWorld()->GetMapName().Contains(TEXT("bossmap"))) { Fail(TEXT("WrongMap")); return; }
		const bool bVariant = FParse::Param(FCommandLine::Get(), TEXT("RPGBossMapE2ESwordManVariant"));
		if (bVariant && !bCombatPhaseScenario) { Fail(TEXT("VariantSelectionInvalid")); return; }
		{
			// Component BeginPlay precedes the owning mode's native SpawnBoss call.
			// Pin each opted-in scenario to its fixture, independently of the playable default.
			FClassProperty* BossProperty = FindFProperty<FClassProperty>(Mode->GetClass(), TEXT("BossClass"));
			const TCHAR* ClassPath = bSummonMechanicScenario ? SummonMechanicClassPath :
				(bVariant ? SwordManVariantClassPath : BaseSwordManClassPath);
			UClass* SelectedBoss = LoadClass<ARPGBossCharacter>(nullptr, ClassPath);
			if (!BossProperty || !SelectedBoss || !SelectedBoss->IsChildOf(BossProperty->MetaClass))
			{
				Fail(TEXT("VariantSelectionInvalid")); return;
			}
			BossProperty->SetObjectPropertyValue_InContainer(Mode, SelectedBoss);
			if (bSummonMechanicScenario || bVariant)
			{
				UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_VARIANT_SELECTED Class=%s RuntimeOnly=1"), *SelectedBoss->GetPathName());
			}
			else
			{
				UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_BASE_SELECTED RuntimeOnly=1"));
			}
		}
		// Do not let cold client loading consume the authored short battle timer.
		// This changes only the opted-in runtime instance, never the BP asset.
		FFloatProperty* Time = FindFProperty<FFloatProperty>(Mode->GetClass(), TEXT("BattleTimeLimit"));
		if (!Time) { Fail(TEXT("MissingBattleTime")); return; }
		Time->SetPropertyValue_InContainer(Mode, 600.f);
		UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_READY Mode=%s Map=%s"),
			*Mode->GetClass()->GetPathName(), *GetWorld()->GetMapName());
		SetComponentTickEnabled(true);
	}
	else if (ARPGPlayerController* PC = Cast<ARPGPlayerController>(GetOwner()))
	{
		// LocalPlayer assignment can follow component BeginPlay during net join.
		SetComponentTickEnabled(PC->GetNetMode() == NM_Client
			|| ((bDodgeScenario || bPlayerAttackScenario || bSummonPlayerKillScenario) && PC->IsLocalController()));
	}
}

void URPGBossMapE2EProbeComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CleanupMontageImmunityFixture();
	CleanupLightHitInput();
	CleanupBossDeathProbe();
	CleanupCombatPhaseProbe();
	CleanupDodgeProbe();
	CleanupPlayerAttackProbe();
	Super::EndPlay(EndPlayReason);
}

void URPGBossMapE2EProbeComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!bEnabled || bFinished) { return; }
	if (bDodgeScenario && Cast<APlayerController>(GetOwner())) { TickDodgeLocalInput(); }
	if ((bPlayerAttackScenario || bSummonPlayerKillScenario) && Cast<APlayerController>(GetOwner())) { TickPlayerAttackLocalInput(); }
	if (bFinished) { return; }
	if (FPlatformTime::Seconds() > Deadline) { Fail(TEXT("Timeout")); return; }
	if (FPlatformTime::Seconds() >= NextDiagnosticAt)
	{
		NextDiagnosticAt = FPlatformTime::Seconds() + 10.0;
		const ARPGBossBattleGameState* State = GetWorld()->GetGameState<ARPGBossBattleGameState>();
		const APlayerController* PC = Cast<APlayerController>(GetOwner());
		if (!PC) { PC = GetWorld()->GetFirstPlayerController(); }
		const ARPGPlayer* Player = PC ? Cast<ARPGPlayer>(PC->GetPawn()) : nullptr;
		const UAbilitySystemComponent* ASC = Player ? Player->GetAbilitySystemComponent() : nullptr;
		UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_DIAG NetMode=%d Stage=%d Phase=%d Battle=%d Pawn=%s Avatar=%s Health=%g ASCRep=%d Local=%d"),
			int32(GetWorld()->GetNetMode()), ServerStage, ClientPhase,
			State ? int32(State->GetBattleSnapshot().BattleState) : -1, *GetNameSafe(Player),
			ASC ? *GetNameSafe(ASC->GetAvatarActor()) : TEXT("None"),
			ASC ? ASC->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute()) : -1.f,
			ASC ? int32(ASC->GetIsReplicated()) : -1, PC ? int32(PC->IsLocalController()) : -1);
		if (const ARPGBossBattleGameMode* Mode = Cast<ARPGBossBattleGameMode>(GetOwner()); Mode && State)
		{
			const ARPGBossCharacter* Boss = State->GetBattleSnapshot().Boss;
			const FObjectPropertyBase* SpawnProperty = FindFProperty<FObjectPropertyBase>(Mode->GetClass(), TEXT("SpawnPoint"));
			const AActor* Spawn = SpawnProperty ? Cast<AActor>(SpawnProperty->GetObjectPropertyValue_InContainer(Mode)) : nullptr;
			const FFloatProperty* RadiusProperty = FindFProperty<FFloatProperty>(Mode->GetClass(), TEXT("ParticipationRadius"));
			UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_START_DIAG ModeTick=%d ModeState=%d Boss=%s NPCReady=%d BossHP=%g BossState=%d Distance=%g Radius=%g PlayerDeath=%d"),
				int32(Mode->IsActorTickEnabled()), int32(Mode->GetCurrentBossBattleState()), *GetNameSafe(Boss), Boss ? int32(Boss->IsNPCStartUpDataReady()) : -1,
				Boss ? Boss->GetAbilitySystemComponent()->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute()) : -1.f,
				Boss ? int32(Boss->BossAIComponent->GetBossState().EncounterState) : -1,
				Spawn && Player ? FVector::Dist(Spawn->GetActorLocation(), Player->GetActorLocation()) : -1.0,
				RadiusProperty ? RadiusProperty->GetPropertyValue_InContainer(Mode) : -1.f,
				ASC ? int32(ASC->HasMatchingGameplayTag(RPGGameplayTags::Shared_Status_Death)) : -1);
			if (bNaturalCombatScenario && IsValid(Boss))
			{
				const AAIController* AI = Cast<AAIController>(Boss->GetController());
				const UBrainComponent* Brain = AI ? AI->GetBrainComponent() : nullptr;
				const UBlackboardComponent* BB = AI ? AI->GetBlackboardComponent() : nullptr;
				const UBehaviorTreeComponent* BT = Cast<UBehaviorTreeComponent>(Brain);
				const UBTNode* ActiveNode = BT ? BT->GetActiveNode() : nullptr;
				UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_NATURAL_DIAG BrainRunning=%d MoveStatus=%d ThreatTarget=%s BBTarget=%s Travel=%g TelegraphSeen=%d DamageSeen=%d ClientAcks=%d ActiveNode=%s"),
					Brain ? int32(Brain->IsRunning()) : -1, AI ? int32(AI->GetMoveStatus()) : -1,
					*GetNameSafe(Boss->BossThreatComponent->GetCurrentTarget()), *GetNameSafe(BB ? BB->GetValueAsObject(TEXT("TargetActor")) : nullptr),
					ServerStage >= 2 ? FVector::Dist(CombatBossStart, Boss->GetActorLocation()) : -1.0,
					int32(bSawCombatTelegraph), int32(bSawCombatDamage), CombatClients.Num(), ActiveNode ? *ActiveNode->GetNodeName() : TEXT("None"));
			}
		}
	}
	if (GetOwner()->HasAuthority()) { TickServer(); }
	else { TickClient(); }
}

void URPGBossMapE2EProbeComponent::Fail(const TCHAR* Reason)
{
	UE_LOG(LogTemp, Error, TEXT("RAIDMAP_E2E_FAIL Reason=%s Stage=%d Phase=%d CombatStep=%d ClientCombatStep=%d"),
		Reason, ServerStage, ClientPhase, CombatPhaseStep, ClientCombatPhaseStep);
	bFinished = true;
	CleanupBossDeathProbe();
	CleanupCombatPhaseProbe();
	CleanupDodgeProbe();
	CleanupPlayerAttackProbe();
	SetComponentTickEnabled(false);
}

bool URPGBossMapE2EProbeComponent::PreparePlayerAttackBossFixture(ARPGBossCharacter* Boss)
{
	if (!Enabled() || !FParse::Param(FCommandLine::Get(), TEXT("RPGBossMapE2EChargeSlash")) || bPlayerAttackDurabilityFixturePrepared) return true;
	UAbilitySystemComponent* ASC = IsValid(Boss) ? Boss->GetAbilitySystemComponent() : nullptr;
	if (!ASC || !Boss->IsNPCStartUpDataReady()) return false;
	// Prepare while Idle so MaxHealth notifications cannot cause a phase transition.
	if (!Boss->HasAuthority() || Boss->BossAIComponent->GetBossState().EncounterState != ERPGBossEncounterState::Idle)
	{ Fail(TEXT("ChargeSlashDurabilityFixtureRequiresIdleBoss")); return false; }
	ASC->SetNumericAttributeBase(URPGAttributeSet::GetMaxHealthAttribute(), 2500.f);
	ASC->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(), 2500.f);
	if (Boss->BossAIComponent->GetBossState().EncounterState != ERPGBossEncounterState::Idle ||
		ASC->GetNumericAttribute(URPGAttributeSet::GetMaxHealthAttribute()) != 2500.f ||
		ASC->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute()) != 2500.f)
	{ Fail(TEXT("ChargeSlashDurabilityFixtureRejected")); return false; }
	bPlayerAttackDurabilityFixturePrepared = true;
	UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_CHARGE_DURABILITY_FIXTURE Authority=1 MaxHP=2500 EncounterIdle=1"));
	return true;
}

void URPGBossMapE2EProbeComponent::TickServer()
{
	ARPGBossBattleGameMode* Mode = Cast<ARPGBossBattleGameMode>(GetOwner());
	ARPGBossBattleGameState* State = GetWorld()->GetGameState<ARPGBossBattleGameState>();
	if (!Mode || !State) { return; }
	const FRPGBossBattleSnapshot Snapshot = State->GetBattleSnapshot();
	if (bSummonMechanicScenario && IsValid(Snapshot.Boss) && Snapshot.Boss->GetClass()->GetPathName() != SummonMechanicClassPath)
	{
		Fail(TEXT("UnexpectedSummonMechanicBossClass")); return;
	}
	if (IsValid(Snapshot.Boss) && FParse::Param(FCommandLine::Get(), TEXT("RPGBossMapE2ESwordManVariant")))
	{
		if (Snapshot.Boss->GetClass()->GetPathName() != SwordManVariantClassPath) { Fail(TEXT("UnexpectedVariantBossClass")); return; }
		const AAIController* AI = Cast<AAIController>(Snapshot.Boss->GetController());
		const UBehaviorTreeComponent* Tree = AI ? Cast<UBehaviorTreeComponent>(AI->GetBrainComponent()) : nullptr;
		if (Tree && Tree->IsRunning() && (!Tree->GetRootTree() || Tree->GetRootTree()->GetPathName() !=
			TEXT("/Game/Blueprints/Character/NPC/Raid/SwordManVariant/BT_RaidSwordManVariant.BT_RaidSwordManVariant")))
		{
			Fail(TEXT("UnexpectedVariantBehaviorTree")); return;
		}
	}
	if (Mode->GetCurrentBossBattleState() != Snapshot.BattleState)
	{
		Fail(TEXT("GameModeSnapshotMismatch")); return;
	}
	if (!PreparePlayerAttackBossFixture(Snapshot.Boss)) return;
	// Recovery scenarios hold the brain throughout. Natural combat releases it
	// only after both cold-loading remote clients have acknowledged readiness.
	if (IsValid(Snapshot.Boss) && (!bNaturalCombatScenario || ServerStage < 2))
	{
		if (AAIController* AI = Cast<AAIController>(Snapshot.Boss->GetController()))
		{
			if (UBrainComponent* Brain = AI->GetBrainComponent(); Brain && Brain->IsRunning())
			{
				Brain->StopLogic(TEXT("Opt-in bossmap retry scenario barrier"));
			}
		}
	}
	if (ServerStage == 0 && ReadyClients.Num() >= 1 && Snapshot.BattleState == EBossBattleState::InProgress)
	{
		OriginalBoss = Snapshot.Boss;
		ServerStage = 1;
		UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_LATE_JOIN_READY"));
	}
	if (bStunScenario && ServerStage == 1) { TickStunServer(); return; }
	if (ServerStage == 1 && (FParse::Param(FCommandLine::Get(), TEXT("RPGBossMapE2ESlow"))
		|| FParse::Param(FCommandLine::Get(), TEXT("RPGBossMapE2EImmunity"))
		|| FParse::Param(FCommandLine::Get(), TEXT("RPGBossMapE2EBurn"))
		|| FParse::Param(FCommandLine::Get(), TEXT("RPGBossMapE2EMontageImmunity"))
		|| FParse::Param(FCommandLine::Get(), TEXT("RPGBossMapE2EAirborneLateJoin"))
		|| FParse::Param(FCommandLine::Get(), TEXT("RPGBossMapE2EStagger")))) { TickKnockbackServer(); return; }
	if (ServerStage == 1 && ReadyClients.Num() == 2)
	{
		if (Snapshot.BattleState != EBossBattleState::InProgress) { Fail(TEXT("PrematureDefeat")); return; }
		if (bLightHitScenario) { TickLightHitServer(); return; }
		if (bNaturalCombatScenario)
		{
			if (bDodgeScenario && (!PrepareDodgeAbilities() || DodgeReadyClients.Num() != 2)) { return; }
			if ((bPlayerAttackScenario || bSummonPlayerKillScenario) && (!PreparePlayerAttackLoadouts() || PlayerAttackReadyClients.Num() != 2)) { return; }
			ARPGBossCharacter* Boss = Snapshot.Boss;
			AAIController* AI = IsValid(Boss) ? Cast<AAIController>(Boss->GetController()) : nullptr;
			UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
			const ANavigationData* NavData = Nav && Boss ? Nav->GetNavDataForProps(Boss->GetNavAgentPropertiesRef(), Boss->GetActorLocation()) : nullptr;
			if (!AI || !AI->GetBrainComponent() || !NavData) { Fail(TEXT("NaturalCombatMissingBrainOrNavData")); return; }
			CombatBossStart = Boss->GetActorLocation();
			for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
			{
				APawn* Pawn = It->Get()->GetPawn();
				if (!Alive(Pawn)) { Fail(TEXT("NaturalCombatParticipantNotAlive")); return; }
				FNavLocation Start, Goal;
				const FVector Extent(100.f, 100.f, 300.f);
				const bool bStart = Nav->ProjectPointToNavigation(CombatBossStart, Start, Extent, NavData);
				const bool bGoal = Nav->ProjectPointToNavigation(Pawn->GetActorLocation(), Goal, Extent, NavData);
				FPathFindingResult Path;
				if (bStart && bGoal) { Path = Nav->FindPathSync(FPathFindingQuery(AI, *NavData, Start.Location, Goal.Location)); }
				UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_NAV BossProjected=%d PlayerProjected=%d PathSuccess=%d Partial=%d Distance=%g Boss=%s Player=%s"),
					int32(bStart), int32(bGoal), int32(Path.IsSuccessful()), Path.Path.IsValid() ? int32(Path.Path->IsPartial()) : -1,
					FVector::Dist(CombatBossStart, Pawn->GetActorLocation()), *CombatBossStart.ToString(), *Pawn->GetActorLocation().ToString());
				if (!bStart || !bGoal || !Path.IsSuccessful() || !Path.Path.IsValid() || Path.Path->IsPartial())
				{
					Fail(TEXT("NaturalCombatNoCompleteNavigationPath")); return;
				}
				CombatHealth.Add(Pawn, CastChecked<ARPGPlayer>(Pawn)->GetAbilitySystemComponent()->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute()));
			}
			ServerStage = 2;
			Deadline = FPlatformTime::Seconds() + 45.0;
			AI->GetBrainComponent()->RestartLogic();
			UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_NATURAL_COMBAT_STARTED BrainRestarted=1 InjectedDamage=0 ManualAbility=0"));
		}
		else
		{
			int32 LostPawns = 0;
			for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
			{
				APlayerController* PC = It->Get();
				if (!PC || !Alive(PC->GetPawn())) { Fail(TEXT("ParticipantNotAlive")); return; }
				if (bDeathAbilityScenario)
				{
					ARPGPlayer* Player = CastChecked<ARPGPlayer>(PC->GetPawn());
					UAbilitySystemComponent* ASC = Player->GetAbilitySystemComponent();
					// Inject fatal damage through the production AttributeSet callback,
					// which adds the death tag and triggers the authored GAS ability.
					UGameplayEffect* Damage = NewObject<UGameplayEffect>(this);
					Damage->DurationPolicy = EGameplayEffectDurationType::Instant;
					FGameplayModifierInfo& Modifier = Damage->Modifiers.AddDefaulted_GetRef();
					Modifier.Attribute = URPGAttributeSet::GetTakeDamageAttribute();
					Modifier.ModifierOp = EGameplayModOp::Override;
					Modifier.ModifierMagnitude = FScalableFloat(ASC->GetNumericAttribute(URPGAttributeSet::GetMaxHealthAttribute()) + 1.f);
					ASC->ApplyGameplayEffectToSelf(Damage, 1.f, ASC->MakeEffectContext());
					bool bDeathActive = false;
					for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
					{
						bDeathActive |= Spec.IsActive() && Spec.Ability && Spec.Ability->GetAssetTags().HasTag(RPGGameplayTags::Shared_Ability_Death);
					}
					if (Alive(Player) || !bDeathActive)
					{
						UE_LOG(LogTemp, Error, TEXT("RAIDMAP_E2E_DEATH_DIAG HP=%g Active=%d Capsule=%d"),
							ASC->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute()), int32(bDeathActive), int32(Player->GetCapsuleComponent()->GetCollisionEnabled()));
						Fail(TEXT("AuthoredDeathAbilityDidNotExecuteOnServer")); return;
					}
				}
				else { PC->GetPawn()->Destroy(); }
				++LostPawns;
			}
			ServerStage = 2;
			if (bDeathAbilityScenario) { UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_DEATH_ABILITIES_EXECUTED Count=%d ServerGAS=1"), LostPawns); }
			else { UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_PAWNS_LOST Count=%d"), LostPawns); }
		}
	}
	if (bNaturalCombatScenario) { TickNaturalCombatServer(); return; }
	if (ServerStage == 2 && DefeatedClients.Num() == 2)
	{
		if (Snapshot.BattleState != EBossBattleState::Defeated || !IsValid(Snapshot.Boss) ||
			Snapshot.Boss->BossAIComponent->GetBossState().EncounterState != ERPGBossEncounterState::Wiped)
		{
			Fail(TEXT("WipeNotAuthoritative")); return;
		}
		ServerStage = 3;
		for (const TWeakObjectPtr<APlayerController>& PC : DefeatedClients)
		{
			if (PC.IsValid())
			{
				PC->FindComponentByClass<URPGBossMapE2EProbeComponent>()->ClientAllowRetry();
			}
		}
		UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_DEFEAT_BARRIER Clients=2"));
	}
	if (ServerStage == 3 && RecoveredClients.Num() == 2)
	{
		if (OriginalBoss.IsValid() || Snapshot.BattleState != EBossBattleState::InProgress ||
			!IsValid(Snapshot.Boss) || Snapshot.Boss->BossAIComponent->GetBossState().PhaseIndex != 0)
		{
			Fail(TEXT("BossNotReplaced")); return;
		}
		for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		{
			if (!Alive(It->Get()->GetPawn())) { Fail(TEXT("ServerRecoveryIncomplete")); return; }
		}
		if (bDeathAbilityScenario) { UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_SERVER_COMPLETE SamePawn=1 NewBoss=1 Clients=2")); }
		else { UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_SERVER_COMPLETE Repossessed=1 NewBoss=1 Clients=2")); }
		bFinished = true;
		SetComponentTickEnabled(false);
	}
}

UUserWidget* URPGBossMapE2EProbeComponent::FindResultWidget() const
{
	const FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(GetOwner()->GetClass(), TEXT("BossResultWidget"));
	return Property ? Cast<UUserWidget>(Property->GetObjectPropertyValue_InContainer(GetOwner())) : nullptr;
}

void URPGBossMapE2EProbeComponent::TickClient()
{
	ARPGPlayerController* PC = Cast<ARPGPlayerController>(GetOwner());
	ARPGBossBattleGameState* State = GetWorld()->GetGameState<ARPGBossBattleGameState>();
	if (!PC || !PC->IsLocalController()) { return; }
	// A joined observer can inspect the victim before its own input setup finishes.
	if (ClientPhase == 0 && KnockbackExpected > 0 && LightHitPlayer.IsValid() && PC->GetPawn() != LightHitPlayer.Get()
		&& FParse::Param(FCommandLine::Get(), TEXT("RPGBossMapE2EAirborneLateJoin"))) TickKnockbackClient();
	if (bFinished || !State) return;
	const FRPGBossBattleSnapshot Snapshot = State->GetBattleSnapshot();
	if (ClientPhase == 0 && Snapshot.BattleState == EBossBattleState::InProgress &&
		IsValid(Snapshot.Boss) && Alive(PC->GetPawn()))
	{
		ARPGPlayer* Player = Cast<ARPGPlayer>(PC->GetPawn());
		if (!Player->IsLocallyControlled() || !Player->InputComponent) { return; }
		OriginalBoss = Snapshot.Boss;
		if (bDeathAbilityScenario)
		{
			const TArray<ARPGWeaponBase*> Weapons = Player->GetPlayerCombatComponent()->GetRegisteredStartupWeapons();
			for (ARPGWeaponBase* Weapon : Weapons) { OriginalStartupWeapons.Add(Weapon); }
		}
		OriginalPawn = PC->GetPawn();
		// This localhost probe isolates session recovery from movement transport.
		// Abrupt Destroy races already-sent ServerMovePacked RPCs against Iris
		// handle removal. Quiesce only the pawn we will destroy, then let queued
		// packets drain before the existing server-owned destruction barrier.
		// This is not a production death protocol or a latency/loss guarantee.
		if (!bDeathAbilityScenario && !bNaturalCombatScenario && !bLightHitScenario) { Player->GetCharacterMovement()->SetComponentTickEnabled(false); }
		ConfirmStartAt = FPlatformTime::Seconds() + ((bDeathAbilityScenario || bNaturalCombatScenario || bLightHitScenario) ? 0.0 : 1.0);
		if (bNaturalCombatScenario) { CombatBossStart = Snapshot.Boss->GetActorLocation(); }
		ClientPhase = 1;
		UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_CLIENT_STARTED Pawn=%s Boss=%s"),
			*PC->GetPawn()->GetClass()->GetPathName(), *Snapshot.Boss->GetClass()->GetPathName());
		if (!bDeathAbilityScenario && !bNaturalCombatScenario && !bLightHitScenario) { UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_CLIENT_QUIESCED MovementTick=0")); }
	}
	if (ClientPhase == 1 && !bStartConfirmed && FPlatformTime::Seconds() >= ConfirmStartAt)
	{
		ARPGPlayer* Player = Cast<ARPGPlayer>(PC->GetPawn());
		if (Player != OriginalPawn.Get() || !Alive(Player) ||
			(!bDeathAbilityScenario && !bNaturalCombatScenario && !bLightHitScenario && Player->GetCharacterMovement()->IsComponentTickEnabled()) ||
			Snapshot.BattleState != EBossBattleState::InProgress)
		{
			Fail(TEXT("PawnLossPreparationChanged")); return;
		}
		bStartConfirmed = true;
		if (!bDeathAbilityScenario && !bNaturalCombatScenario && !bLightHitScenario) { UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_CLIENT_PAWN_LOSS_READY MovementQuietSeconds=1")); }
		ServerConfirmSnapshot(0);
	}
	if (bNaturalCombatScenario) { TickNaturalCombatClient(); return; }
	if (bLightHitScenario) { TickLightHitClient(); return; }
	if (ClientPhase == 1 && Snapshot.BattleState == EBossBattleState::Defeated &&
		(bDeathAbilityScenario ? (PC->GetPawn() == OriginalPawn.Get() && !Alive(PC->GetPawn())) : !IsValid(PC->GetPawn())))
	{
		if (bDeathAbilityScenario)
		{
			ARPGPlayer* Player = Cast<ARPGPlayer>(PC->GetPawn());
			// The authored graph disables the capsule/pauses animation in
			// OnEndAbility, not at montage start. Wait for that real client path.
			if (!Player || Player->GetCapsuleComponent()->GetCollisionEnabled() != ECollisionEnabled::NoCollision ||
				!Player->GetMesh()->bPauseAnims) { return; }
		}
		UUserWidget* Widget = FindResultWidget();
		if (!Widget || !Widget->IsInViewport()) { return; }
		UWidget* Retry = Widget->GetWidgetFromName(TEXT("RetryButton"));
		FMulticastDelegateProperty* Event = RPGBossBattleUI::FindRetryClick(Retry);
		const FMulticastScriptDelegate* Click = Event ? Event->GetMulticastDelegate(Event->ContainerPtrToValuePtr<void>(Retry)) : nullptr;
		if (!Click || !Click->Contains(PC, GET_FUNCTION_NAME_CHECKED(ARPGPlayerController, RequestBossBattleRestart)))
		{
			Fail(TEXT("LiveRetryWidgetNotBound")); return;
		}
		DefeatRevision = Snapshot.Revision;
		ClientPhase = 2;
		UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_CLIENT_DEFEATED PawnMissing=%d WidgetBound=1"), bDeathAbilityScenario ? 0 : 1);
		if (bDeathAbilityScenario) { UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_CLIENT_DEATH_PRESENTATION CapsuleDisabled=1 SamePawn=1")); }
		ServerConfirmSnapshot(1);
	}
	if (ClientPhase == 2 && bAllowRetry && !bClickedRetry &&
		FParse::Param(FCommandLine::Get(), TEXT("RPGBossMapE2ERequester")))
	{
		UUserWidget* Widget = FindResultWidget();
		UWidget* Retry = Widget ? Widget->GetWidgetFromName(TEXT("RetryButton")) : nullptr;
		FMulticastDelegateProperty* Event = RPGBossBattleUI::FindRetryClick(Retry);
		if (!Event) { Fail(TEXT("RetryEventDisappeared")); return; }
		bClickedRetry = true;
		// Exercise the live widget's dispatcher -> production local handler ->
		// existing owning-controller server RPC. No synthetic mouse/GPU assertion.
		Event->GetMulticastDelegate(Event->ContainerPtrToValuePtr<void>(Retry))->ProcessDelegate<UObject>(nullptr);
		UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_CLIENT_RETRY_CLICK ExistingRPC=1"));
		if (!PC->IsBossBattleRestartPending() || PC->CanRequestBossBattleRestart() || PC->TryRequestBossBattleRestart())
		{
			Fail(TEXT("RetryPendingGuardMissing")); return;
		}
		UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_CLIENT_RETRY_GUARD Pending=1 DuplicateBlocked=1"));
	}
	if (ClientPhase == 2 && Snapshot.Revision > DefeatRevision &&
		Snapshot.BattleState == EBossBattleState::InProgress && IsValid(Snapshot.Boss) &&
		Snapshot.Boss != OriginalBoss.Get() && Alive(PC->GetPawn()) &&
		(bDeathAbilityScenario ? PC->GetPawn() == OriginalPawn.Get() : PC->GetPawn() != OriginalPawn.Get()))
	{
		ARPGPlayer* Player = Cast<ARPGPlayer>(PC->GetPawn());
		UAbilitySystemComponent* ASC = Player->GetAbilitySystemComponent();
		const float HP = ASC->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute());
		const float MaxHP = ASC->GetNumericAttribute(URPGAttributeSet::GetMaxHealthAttribute());
		if (!FMath::IsFinite(MaxHP) || MaxHP <= 0.f || !FMath::IsNearlyEqual(HP, MaxHP) ||
			!Player->IsLocallyControlled() || !Player->InputComponent ||
			!Player->GetCharacterMovement()->IsComponentTickEnabled() ||
			Player->GetCharacterMovement()->MovementMode == MOVE_None)
		{
			return;
		}
		if (FindResultWidget()) { return; }
		if (PC->IsBossBattleRestartPending()) { Fail(TEXT("RetryPendingAfterNewBattle")); return; }
		if (bDeathAbilityScenario)
		{
			if (Player->GetCapsuleComponent()->GetCollisionEnabled() == ECollisionEnabled::NoCollision || Player->GetMesh()->bPauseAnims) { return; }
			const TArray<ARPGWeaponBase*> Weapons = Player->GetPlayerCombatComponent()->GetRegisteredStartupWeapons();
			for (const TWeakObjectPtr<AActor>& Weapon : OriginalStartupWeapons)
			{
				if (!Weapon.IsValid() || !Weapons.Contains(Cast<ARPGWeaponBase>(Weapon.Get()))) { Fail(TEXT("StartupWeaponLostDuringDeathRetry")); return; }
			}
			UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_CLIENT_DEATH_RECOVERED SamePawn=1 CapsuleEnabled=1 AnimationResumed=1 ExistingStartupWeaponsPreserved=1 Count=%d"), OriginalStartupWeapons.Num());
		}
		UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_CLIENT_RECOVERED FullHealth=1 AvatarReady=1 LocalPossess=1 InputReady=1 ResultClosed=1"));
		ServerConfirmSnapshot(2);
		bFinished = true;
		SetComponentTickEnabled(false);
	}
}

void URPGBossMapE2EProbeComponent::ServerConfirmSnapshot_Implementation(int32 Phase)
{
	if (!Enabled()) { return; }
	APlayerController* PC = Cast<APlayerController>(GetOwner());
	ARPGBossBattleGameMode* Mode = GetWorld()->GetAuthGameMode<ARPGBossBattleGameMode>();
	ARPGBossBattleGameState* State = GetWorld()->GetGameState<ARPGBossBattleGameState>();
	URPGBossMapE2EProbeComponent* Driver = Mode ? Mode->FindComponentByClass<URPGBossMapE2EProbeComponent>() : nullptr;
	if (!PC || PC->IsLocalController() || !Driver || !State || !Driver->bEnabled || Driver->bFinished) { return; }
	const EBossBattleState Battle = State->GetBattleSnapshot().BattleState;
	if (Phase == 0 && Driver->ServerStage <= 1 && Battle == EBossBattleState::InProgress && Alive(PC->GetPawn()))
	{
		Driver->ReadyClients.Add(PC);
	}
	else if (Phase == 1 && Driver->ServerStage == 2 && Battle == EBossBattleState::Defeated &&
		Driver->ReadyClients.Contains(PC) &&
		(Driver->bDeathAbilityScenario ? !Alive(PC->GetPawn()) : !IsValid(PC->GetPawn())))
	{
		Driver->DefeatedClients.Add(PC);
	}
	else if (Phase == 2 && Driver->ServerStage == 3 && Battle == EBossBattleState::InProgress &&
		Driver->DefeatedClients.Contains(PC) && Alive(PC->GetPawn()))
	{
		Driver->RecoveredClients.Add(PC);
	}
	else if (Phase == 3 && Driver->bNaturalCombatScenario && Driver->ServerStage == 2 &&
		Battle == EBossBattleState::InProgress && Driver->ReadyClients.Contains(PC) && Driver->bSawCombatDamage)
	{
		Driver->CombatClients.Add(PC);
	}
}

void URPGBossMapE2EProbeComponent::TickNaturalCombatServer()
{
	if (ServerStage != 2) { return; }
	if (OriginalBoss.IsValid()) { ObserveBossAttackPresentation(OriginalBoss->BossAIComponent->GetTelegraphState().ActionTag); }
	if (bBossDeathScenario && BossDeathStep >= 2) { TickBossDeathServer(); return; }
	const FRPGBossBattleSnapshot Snapshot = GetWorld()->GetGameState<ARPGBossBattleGameState>()->GetBattleSnapshot();
	ARPGBossCharacter* Boss = Snapshot.Boss;
	if (!IsValid(Boss) || Boss != OriginalBoss.Get() || Snapshot.BattleState != EBossBattleState::InProgress)
	{
		Fail(TEXT("NaturalCombatEncounterEnded")); return;
	}
	const FRPGBossTelegraphState Telegraph = Boss->BossAIComponent->GetTelegraphState();
	if (Telegraph.bActive && Boss->BossThreatComponent->GetCurrentTarget() && !bSawCombatTelegraph)
	{
		bSawCombatTelegraph = true;
		FirstCombatSequence = Telegraph.Sequence;
		UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_NATURAL_SERVER_TELEGRAPH Sequence=%d Action=%s Target=%s Travel=%g"),
			Telegraph.Sequence, *Telegraph.ActionTag.ToString(), *GetNameSafe(Boss->BossThreatComponent->GetCurrentTarget()),
			FVector::Dist(CombatBossStart, Boss->GetActorLocation()));
	}
	for (const auto& Entry : CombatHealth)
	{
		ARPGPlayer* Player = Cast<ARPGPlayer>(Entry.Key.Get());
		if (!IsValid(Player)) { Fail(TEXT("NaturalCombatPawnLost")); return; }
		const float HP = Player->GetAbilitySystemComponent()->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute());
		if (!bSawCombatDamage && bSawCombatTelegraph && FMath::IsFinite(HP) && HP < Entry.Value &&
			FVector::Dist(CombatBossStart, Boss->GetActorLocation()) > 100.f)
		{
			bSawCombatDamage = true;
			UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_NATURAL_SERVER_DAMAGE Victim=%s Before=%g After=%g BTMove=1 GAS=1"), *Player->GetName(), Entry.Value, HP);
		}
	}
	if (bSummonMechanicScenario) { TickSummonMechanicServer(); return; }
	if (bCombatPhaseScenario) { TickCombatPhaseServer(); return; }
	if (bDodgeScenario) { TickDodgeServer(); return; }
	if (bPlayerAttackScenario) { TickPlayerAttackServer(); return; }
	if (bBossDeathScenario) { TickBossDeathServer(); return; }
	if (bSawCombatDamage && CombatClients.Num() == 2)
	{
		UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_NATURAL_SERVER_COMPLETE Navigation=1 BTMove=1 GASDamage=1 Clients=2"));
		bFinished = true;
		SetComponentTickEnabled(false);
	}
}

void URPGBossMapE2EProbeComponent::TickNaturalCombatClient()
{
	if ((ClientPhase != 1 && ClientPhase != 3) || !bStartConfirmed) { return; }
	if (OriginalBoss.IsValid()) { ObserveBossAttackPresentation(OriginalBoss->BossAIComponent->GetTelegraphState().ActionTag); }
	if (bBossDeathScenario && ClientPhase == 3) { TickBossDeathClient(); return; }
	const FRPGBossBattleSnapshot Snapshot = GetWorld()->GetGameState<ARPGBossBattleGameState>()->GetBattleSnapshot();
	ARPGBossCharacter* Boss = Snapshot.Boss;
	if (!IsValid(Boss) || Boss != OriginalBoss.Get() || Snapshot.BattleState != EBossBattleState::InProgress)
	{
		Fail(TEXT("NaturalCombatClientEncounterChanged")); return;
	}
	if (!bSawCombatTelegraph && Boss->BossAIComponent->GetTelegraphState().bActive &&
		Boss->BossTelegraphPresentation->IsIndicatorVisible())
	{
		bSawCombatTelegraph = true;
		FirstCombatSequence = Boss->BossAIComponent->GetTelegraphState().Sequence;
		UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_NATURAL_CLIENT_TELEGRAPH Sequence=%d Indicator=1"), Boss->BossAIComponent->GetTelegraphState().Sequence);
	}
	for (TActorIterator<ARPGPlayer> It(GetWorld()); It; ++It)
	{
		ARPGPlayer* Player = *It;
		if (!Alive(Player)) { continue; }
		const float HP = Player->GetAbilitySystemComponent()->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute());
		float* Before = CombatHealth.Find(Player);
		if (!Before) { CombatHealth.Add(Player, HP); }
		else if (HP < *Before && bSawCombatTelegraph) { bSawCombatDamage = true; }
	}
	if (bSawCombatDamage && FVector::Dist(CombatBossStart, Boss->GetActorLocation()) > 100.f)
	{
		if (ClientPhase == 1)
		{
			UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_NATURAL_CLIENT_COMPLETE BossMovement=1 Telegraph=1 Indicator=1 ReplicatedDamage=1"));
			// Repeat until the runner sees the server barrier, since damage's
			// replication can precede the driver's next server-side observation.
			ClientPhase = 3;
		}
		ServerConfirmSnapshot(3);
	}
	if (bSummonMechanicScenario) { TickSummonMechanicClient(); }
	if (bCombatPhaseScenario) { TickCombatPhaseClient(); }
	if (bDodgeScenario) { TickDodgeObserver(); }
	if (bPlayerAttackScenario) { TickPlayerAttackObserver(); }
	if (bBossDeathScenario) { TickBossDeathClient(); }
}

void URPGBossMapE2EProbeComponent::ClientAllowRetry_Implementation()
{
	if (Enabled()) { bAllowRetry = true; }
}
