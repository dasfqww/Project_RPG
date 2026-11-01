#include "Tests/RPGSkillDamageNetworkProbe.h"
#include "Component/Combat/RPGCombatContributionComponent.h"

#include "AIController.h"
#include "Attribute/RPGAttributeSet.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Component/RPGSecurityValidationComponent.h"
#include "Component/UI/PawnUIComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameplayPrediction.h"
#include "Net/UnrealNetwork.h"
#include "RPGGameplayTags.h"
#include "Skill/RPGGameplayAbility_SkillContainer.h"

#if !UE_BUILD_SHIPPING
#include "Engine/NetDriver.h"
#include "EngineUtils.h"
#include "Iris/ReplicationSystem/ObjectReplicationBridge.h"
#include "Iris/ReplicationSystem/ReplicationSystem.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#endif

#include UE_INLINE_GENERATED_CPP_BY_NAME(RPGSkillDamageNetworkProbe)

#if !UE_BUILD_SHIPPING
namespace
{
bool Enabled()
{
	return FParse::Param(FCommandLine::Get(), TEXT("RPGSkillNetE2E")) &&
		FParse::Param(FCommandLine::Get(), TEXT("RPGSkillDamageE2E"));
}
bool AbuseEnabled() { return Enabled() && FParse::Param(FCommandLine::Get(), TEXT("RPGSkillTargetDataAbuseE2E")); }
bool MultiEnabled() { return AbuseEnabled() && FParse::Param(FCommandLine::Get(), TEXT("RPGSkillMultiPeerE2E")); }
constexpr int32 AbuseCaseCount = 7;
const TCHAR* AbuseNames[AbuseCaseCount] = { TEXT("ReplayInitial"), TEXT("AlteredReplay"), TEXT("WrongActivation"),
	TEXT("OversizedActors"), TEXT("ActorForgery"), TEXT("OutOfRange"), TEXT("UnexpectedTag") };
uint64 NetId(const UObject* Object)
{
	const UNetDriver* Driver = Object->GetWorld()->GetNetDriver();
	const UReplicationSystem* System = Driver ? Driver->GetReplicationSystem() : nullptr;
	const UObjectReplicationBridge* Bridge = System ? System->GetReplicationBridge() : nullptr;
	return Bridge ? Bridge->GetReplicatedRefHandle(Object).GetId() : 0;
}
const FVector Offsets[4] = { FVector(300,0,0), FVector(-300,0,0), FVector(300,120,0), FVector(1500,0,0) };
}
#define RPG_DAMAGE_LOG(Format, ...) UE_LOG(LogTemp, Display, TEXT("%s%s"), \
	*LogContext(SourceController.Get()), *FString::Printf(Format, ##__VA_ARGS__))
#endif

FString FRPGSkillDamageNetworkProbe::LogContext(const AActor* Actor)
{
#if !UE_BUILD_SHIPPING
	if (MultiEnabled())
	{
		const auto* PC = Cast<APlayerController>(Actor);
		if (const auto* Pawn = Cast<APawn>(Actor)) PC = Cast<APlayerController>(Pawn->GetController());
		return FString::Printf(TEXT("RPG_SKILL_PEER Peer=%d "),
			PC && PC->PlayerState ? PC->PlayerState->GetPlayerId() : INDEX_NONE);
	}
#endif
	return FString();
}

ARPGSkillDamageNetworkTarget::ARPGSkillDamageNetworkTarget()
{
	auto* Capsule = CreateDefaultSubobject<UCapsuleComponent>(TEXT("DamageProbeCapsule"));
	SetRootComponent(Capsule);
	Capsule->InitCapsuleSize(20.0f, 60.0f);
	Capsule->SetCollisionObjectType(ECC_Pawn);
	Capsule->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Capsule->SetCollisionResponseToAllChannels(ECR_Overlap);
	TargetASC = CreateDefaultSubobject<URPGAbilitySystemComponent>(TEXT("DamageProbeASC"));
	TargetContributions = CreateDefaultSubobject<URPGCombatContributionComponent>(TEXT("DamageProbeContributions"));
	TargetASC->SetIsReplicated(true);
	TargetASC->SetReplicationMode(EGameplayEffectReplicationMode::Minimal);
	TargetAttributes = CreateDefaultSubobject<URPGAttributeSet>(TEXT("DamageProbeAttributes"));
	TargetUI = CreateDefaultSubobject<UPawnUIComponent>(TEXT("DamageProbeUI"));
	bReplicates = true;
	bAlwaysRelevant = true;
	bReplicateUsingRegisteredSubObjectList = true;
}

void ARPGSkillDamageNetworkTarget::BeginPlay()
{
	Super::BeginPlay();
#if !UE_BUILD_SHIPPING
	if (!Enabled() && !(FParse::Param(FCommandLine::Get(), TEXT("RPGProgressionRestoreE2E"))
		&& FParse::Param(FCommandLine::Get(), TEXT("RPGProgressionCombatE2E"))
		&& FParse::Param(FCommandLine::Get(), TEXT("nosteam")))) return;
	TargetASC->InitAbilityActorInfo(this, this);
	TargetASC->AddAttributeSetSubobject(TargetAttributes.Get());
	if (HasAuthority())
	{
		TargetAttributes->SetMaxHealth(BaselineHealth);
		TargetAttributes->SetCurrentHealth(BaselineHealth);
		TargetAttributes->SetDefense(1.0f);
	}
#endif
}
void ARPGSkillDamageNetworkTarget::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ARPGSkillDamageNetworkTarget, TestIndex);
	DOREPLIFETIME(ARPGSkillDamageNetworkTarget, ExpectedHealth);
	DOREPLIFETIME(ARPGSkillDamageNetworkTarget, bSuiteFinished);
	DOREPLIFETIME(ARPGSkillDamageNetworkTarget, bAbuseRequestsVerified);
	DOREPLIFETIME(ARPGSkillDamageNetworkTarget, SourcePeer);
	DOREPLIFETIME(ARPGSkillDamageNetworkTarget, SourceASCNetId);
	DOREPLIFETIME(ARPGSkillDamageNetworkTarget, FixtureSourceLocation);
	DOREPLIFETIME(ARPGSkillDamageNetworkTarget, bInputReady);
	DOREPLIFETIME(ARPGSkillDamageNetworkTarget, bPeerPrimed);
	DOREPLIFETIME(ARPGSkillDamageNetworkTarget, AppliedEffects);
	DOREPLIFETIME(ARPGSkillDamageNetworkTarget, ObservedEvents);
}

ARPGSkillMultiPeerCoordinator::ARPGSkillMultiPeerCoordinator()
{
	bReplicates = true;
	bAlwaysRelevant = true;
}

void ARPGSkillMultiPeerCoordinator::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ARPGSkillMultiPeerCoordinator, SourcePeer);
	DOREPLIFETIME(ARPGSkillMultiPeerCoordinator, SourceASCNetId);
}

void ARPGSkillMultiPeerCoordinator::ServerFixtureReady_Implementation()
{
#if !UE_BUILD_SHIPPING
	const auto* PC = Cast<APlayerController>(GetOwner());
	if (!MultiEnabled() || !HasAuthority() || bFixtureReadyReceived ||
		!PC || !PC->PlayerState || PC->PlayerState->GetPlayerId() != SourcePeer || SourceASCNetId == 0) return;
	TArray<ARPGSkillDamageNetworkTarget*> Roots;
	int32 Count = 0;
	for (TActorIterator<ARPGSkillDamageNetworkTarget> It(GetWorld()); It; ++It)
	{
		++Count;
		if (It->Attributes()->GetCurrentHealth() != ARPGSkillDamageNetworkTarget::BaselineHealth || It->AppliedEffects != 0) return;
		if (It->TestIndex == 0) Roots.Add(*It);
	}
	if (Count != 8 || Roots.Num() != 2 || Roots[0]->SourcePeer == Roots[1]->SourcePeer ||
		!Roots.ContainsByPredicate([this](const auto* Root) { return Root->SourcePeer == SourcePeer && Root->SourceASCNetId == SourceASCNetId; })) return;
	bFixtureReadyReceived = true;
	UE_LOG(LogTemp, Display, TEXT("RPG_SKILL_MULTI READY Peer=%d ASCNetId=%llu Targets=8"), SourcePeer, SourceASCNetId);
	TArray<ARPGSkillMultiPeerCoordinator*> Coordinators;
	for (TActorIterator<ARPGSkillMultiPeerCoordinator> It(GetWorld()); It; ++It) Coordinators.Add(*It);
	if (Coordinators.Num() != 2 || Coordinators[0]->SourcePeer == Coordinators[1]->SourcePeer ||
		!Coordinators[0]->bFixtureReadyReceived || !Coordinators[1]->bFixtureReadyReceived) return;
	for (auto* Root : Roots)
	{
		Root->bInputReady = true;
		Root->ForceNetUpdate();
		UE_LOG(LogTemp, Display, TEXT("RPG_SKILL_MULTI INPUT_READY Peer=%d ASCNetId=%llu Targets=8"), Root->SourcePeer, Root->SourceASCNetId);
	}
#endif
}
UAbilitySystemComponent* ARPGSkillDamageNetworkTarget::GetAbilitySystemComponent() const { return TargetASC; }
UPawnUIComponent* ARPGSkillDamageNetworkTarget::GetPawnUIComponent() const { return TargetUI; }

FRPGSkillDamageNetworkProbe::~FRPGSkillDamageNetworkProbe()
{
#if !UE_BUILD_SHIPPING
	if (auto* ASC = SourceASC.Get()) ASC->RemoveGameplayEventTagContainerDelegate(
		FGameplayTagContainer(RPGGameplayTags::Player_Event_AOE), EventHandle);
	for (int32 I = 0; I < 4; ++I)
	{
		if (auto* Target = Targets[I].Get())
		{
			Target->ASC()->OnGameplayEffectAppliedDelegateToSelf.Remove(EffectHandles[I]);
			Target->ASC()->GetGameplayAttributeValueChangeDelegate(URPGAttributeSet::GetCurrentHealthAttribute()).Remove(HealthHandles[I]);
		}
		if (auto* Target = ForeignTargets[I].Get())
			Target->ASC()->GetGameplayAttributeValueChangeDelegate(URPGAttributeSet::GetCurrentHealthAttribute()).Remove(ForeignHealthHandles[I]);
	}
#endif
}
void FRPGSkillDamageNetworkProbe::Fail(const TCHAR* Reason)
{
#if !UE_BUILD_SHIPPING
	if (!FailureReason.IsEmpty()) return;
	FailureReason = Reason;
	UE_LOG(LogTemp, Error, TEXT("RPG_SKILL_DAMAGE FAILED Reason=%s"), Reason);
#endif
}
void FRPGSkillDamageNetworkProbe::Tick(APlayerController& Controller,
	URPGAbilitySystemComponent& ASC, FGameplayAbilitySpecHandle Handle)
{
#if !UE_BUILD_SHIPPING
	if (!Enabled() || bPassed || !FailureReason.IsEmpty()) return;
	SourceController = &Controller;
	SourceASC = &ASC;
	bMultiPeer = MultiEnabled();
	if (bMultiPeer && (!Controller.PlayerState || Controller.PlayerState->GetPlayerId() <= 0)) return;
	if (Controller.HasAuthority()) TickServer(Controller, ASC, Handle);
	else if (Controller.IsLocalController()) TickClient(Controller);
#endif
}

void FRPGSkillDamageNetworkProbe::TickServer(APlayerController& Controller,
	URPGAbilitySystemComponent& ASC, FGameplayAbilitySpecHandle Handle)
{
#if !UE_BUILD_SHIPPING
	if (!bPrepared)
	{
		const auto* Team = Cast<IGenericTeamAgentInterface>(&Controller);
		const auto* Attributes = ASC.GetSet<URPGAttributeSet>();
		if (!Team || !Attributes || !Controller.GetPawn()) return;
		if (bMultiPeer)
		{
			if (NetId(&ASC) == 0) return;
			auto* Character = Cast<ACharacter>(Controller.GetPawn());
			auto* Security = Character ? Character->FindComponentByClass<URPGSecurityValidationComponent>() : nullptr;
			if (!Character || !Security) { Fail(TEXT("MultiMissingFixtureCharacterOrSecurity")); return; }
			int32 Arena = 0;
			for (TActorIterator<ARPGSkillDamageNetworkTarget> It(Controller.GetWorld()); It; ++It)
				if (It->TestIndex == 0) ++Arena;
			if (Arena >= 2) { Fail(TEXT("MultiTooManySources")); return; }
			// Opt-in stationary fixture, not movement/teleport coverage. No policy thresholds are lowered.
			Character->GetCharacterMovement()->DisableMovement();
			Character->SetActorLocation(Character->GetActorLocation() + FVector(0, Arena * 12000.0, 0), false, nullptr, ETeleportType::TeleportPhysics);
			Security->ResetMovementBaseline();
			Character->ForceNetUpdate();
		}
		ASC.SetNumericAttributeBase(URPGAttributeSet::GetCriticalChanceAttribute(), 0.0f);
		if (Attributes->GetCriticalChance() != 0.0f) { Fail(TEXT("FixtureCriticalChanceNotZero")); return; }
		const uint8 SourceTeam = Team->GetGenericTeamId().GetId();
		if (SourceTeam == 255) { Fail(TEXT("SourceHasNoTeam")); return; }
		SourceTeamId = SourceTeam;
		SourceASC = &ASC;
		EventHandle = ASC.AddGameplayEventTagContainerDelegate(FGameplayTagContainer(RPGGameplayTags::Player_Event_AOE),
			FGameplayEventTagMulticastDelegate::FDelegate::CreateLambda([this](FGameplayTag Tag, const FGameplayEventData*)
			{
				++EventCount;
				if (bMultiPeer && Targets[0].IsValid()) Targets[0]->ObservedEvents = EventCount;
				RPG_DAMAGE_LOG(TEXT("RPG_SKILL_DAMAGE EVENT Tag=%s Count=%d"), *Tag.ToString(), EventCount);
			}));
		for (int32 I = 0; I < 4; ++I)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			auto* Target = Controller.GetWorld()->SpawnActor<ARPGSkillDamageNetworkTarget>(
				Controller.GetPawn()->GetActorTransform().TransformPosition(Offsets[I]), FRotator::ZeroRotator, Params);
			auto* AI = Controller.GetWorld()->SpawnActor<AAIController>();
			if (!Target || !AI) { Fail(TEXT("TargetSpawnFailed")); return; }
			AI->SetGenericTeamId(FGenericTeamId(I == 2 ? SourceTeam : (SourceTeam == 0 ? 1 : 0)));
			AI->Possess(Target);
			Target->TestIndex = I;
			if (bMultiPeer)
			{
				Target->SourcePeer = Controller.PlayerState->GetPlayerId();
				Target->SourceASCNetId = NetId(&ASC);
				Target->FixtureSourceLocation = Controller.GetPawn()->GetActorLocation();
			}
			Targets[I] = Target;
			EffectHandles[I] = Target->ASC()->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
				[this, I](UAbilitySystemComponent* ApplyingASC, const FGameplayEffectSpec& Effect, FActiveGameplayEffectHandle)
				{
					++EffectCounts[I];
					if (bMultiPeer && Targets[I].IsValid()) Targets[I]->AppliedEffects = EffectCounts[I];
					const float Damage = Effect.GetSetByCallerMagnitude(RPGGameplayTags::Shared_SetByCaller_BaseDamage, false, -1.0f);
					if (I != 0 || ApplyingASC != SourceASC.Get() || !Effect.Def ||
						Effect.Def->GetClass()->GetName() != TEXT("GE_Shared_ApplyDamage_C") ||
						!FMath::IsNearlyEqual(Damage, ExpectedSetByCaller))
					{
						Fail(TEXT("UnexpectedEffectSourceTargetOrMagnitude")); return;
					}
					const auto* ActiveSkill = SourceSkill.Get();
					if (!ActiveSkill || !ActiveSkill->GetActiveSkillTargetResult().bIsValid)
					{ Fail(TEXT("MissingDamageQuerySnapshot")); return; }
					const auto& QueryTransform = ActiveSkill->GetActiveSkillTargetResult().HitQueryTransform;
					for (int32 J = 0; J < 4; ++J)
					{
						const auto* T = Targets[J].Get();
						const auto* TTeam = T ? Cast<IGenericTeamAgentInterface>(T->GetController()) : nullptr;
						if (!T || !TTeam) { Fail(TEXT("DamageTargetDestroyedOrUnpossessed")); return; }
						const FVector Local = QueryTransform.InverseTransformPosition(T->GetActorLocation());
						const bool bFriendly = TTeam->GetGenericTeamId().GetId() == SourceTeamId;
						if (!Local.Equals(Offsets[J], 1.0f) || bFriendly != (J == 2))
						{ Fail(TEXT("DamageTargetGeometryOrTeamChanged")); return; }
						RPG_DAMAGE_LOG(TEXT("RPG_SKILL_DAMAGE POSE Index=%d NetId=%llu X=%.6f Y=%.6f Z=%.6f Friendly=%d"),
							J, NetId(T), Local.X, Local.Y, Local.Z, bFriendly ? 1 : 0);
					}
					RPG_DAMAGE_LOG(TEXT("RPG_SKILL_DAMAGE EFFECT Index=%d NetId=%llu Count=%d SetByCaller=%.6f Class=%s"),
						I, NetId(Targets[I].Get()), EffectCounts[I], Damage, *Effect.Def->GetClass()->GetName());
				});
		}
		if (bMultiPeer)
		{
			FActorSpawnParameters Params;
			Params.Owner = &Controller;
			auto* Coordinator = Controller.GetWorld()->SpawnActor<ARPGSkillMultiPeerCoordinator>(Params);
			if (!Coordinator) { Fail(TEXT("MultiCoordinatorSpawnFailed")); return; }
			Coordinator->SourcePeer = Controller.PlayerState->GetPlayerId();
			Coordinator->SourceASCNetId = NetId(&ASC);
			Coordinator->ForceNetUpdate();
		}
		bPrepared = true;
	}
	const auto* Spec = ASC.FindAbilitySpecFromHandle(Handle);
	auto* Skill = Spec ? Cast<URPGGameplayAbility_SkillContainer>(Spec->GetPrimaryInstance()) : nullptr;
	if (!Skill) return;
	if (Spec->IsActive())
	{
		if (!Skill->GetActiveSkillTargetResult().bIsValid) return;
		const auto& Runtime = Skill->GetActiveSkillSpec();
		if (!bObservedActive)
		{
			const auto* Attributes = ASC.GetSet<URPGAttributeSet>();
			if (Skill->GetClass()->GetName() != TEXT("GA_Player_ChargeSlash_C") ||
				Runtime.TargetingProfile.Shape.Type != ERPGHitQueryShape::Sector ||
				Runtime.TargetingProfile.Shape.Radius != 650.0f || Runtime.TargetingProfile.Shape.AngleDegrees != 120.0f ||
				!Attributes || Attributes->GetCriticalChance() != 0.0f) { Fail(TEXT("AuthoredRuntimeMismatch")); return; }
			ExpectedSetByCaller = Runtime.BaseDamage * Runtime.GetStatScalar(FGameplayTag::RequestGameplayTag(TEXT("Shared.Stat.Damage")));
			const double Damage = static_cast<double>(ExpectedSetByCaller) * Attributes->GetAttack();
			if (!FMath::IsFinite(Damage) || Damage < 1.0 || Damage >= ARPGSkillDamageNetworkTarget::BaselineHealth)
			{ Fail(TEXT("FixtureDamageOutOfRange")); return; }
			ExpectedDamage = static_cast<int32>(Damage);
			SourceSkill = Skill;
			Targets[0]->ExpectedHealth = ARPGSkillDamageNetworkTarget::BaselineHealth - ExpectedDamage;
			bObservedActive = true;
			RPG_DAMAGE_LOG(TEXT("RPG_SKILL_DAMAGE CONFIG Ability=%s Radius=650 Angle=120 Base=%.6f Scalar=%.6f Attack=%.6f Defense=1 Crit=0 ExpectedDamage=%.6f"),
				*Skill->GetClass()->GetName(), Runtime.BaseDamage,
				Runtime.GetStatScalar(FGameplayTag::RequestGameplayTag(TEXT("Shared.Stat.Damage"))), Attributes->GetAttack(), ExpectedDamage);
		}
		const FTransform Transform = Skill->GetActiveSkillTargetResult().HitQueryTransform;
		for (int32 I = 0; I < 4; ++I)
		{
			if (!Targets[I].IsValid()) { Fail(TEXT("ActiveTargetDestroyed")); return; }
			Targets[I]->SetActorLocation(Transform.TransformPosition(Offsets[I]));
		}
		return;
	}
	if (!bObservedActive) return;
	if (EndedAt < 0.0) EndedAt = Controller.GetWorld()->GetTimeSeconds();
	if (Controller.GetWorld()->GetTimeSeconds() - EndedAt < 3.0) return;
	if (EventCount != 1) { Fail(TEXT("AuthoredDamageNotifyCount")); return; }
	if (AbuseEnabled() && (AbuseReceipts != AbuseCaseCount || !bAbuseRecoveryObserved))
	{ Fail(TEXT("MissingAbuseReceiptsOrAuthoredRecovery")); return; }
	for (int32 I = 0; I < 4; ++I)
	{
		auto* Target = Targets[I].Get();
		if (!Target || EffectCounts[I] != (I == 0 ? 1 : 0) ||
			Target->Attributes()->GetCurrentHealth() != Target->ExpectedHealth || NetId(Target) == 0)
		{ Fail(TEXT("ServerHealthOrEffectMismatch")); return; }
		Target->bSuiteFinished = true;
		Target->ForceNetUpdate();
		RPG_DAMAGE_LOG(TEXT("RPG_SKILL_DAMAGE RESULT Role=Server Index=%d NetId=%llu Before=%.6f After=%.6f Effects=%d"),
			I, NetId(Target), ARPGSkillDamageNetworkTarget::BaselineHealth, Target->Attributes()->GetCurrentHealth(), EffectCounts[I]);
	}
	RPG_DAMAGE_LOG(TEXT("RPG_SKILL_DAMAGE SERVER_COMPLETE Targets=4 Events=1"));
	if (AbuseEnabled()) RPG_DAMAGE_LOG(TEXT("RPG_SKILL_ABUSE SERVER_COMPLETE Cases=7 Recovery=1 Targets=4"));
	bPassed = true;
#endif
}

void FRPGSkillDamageNetworkProbe::TickClient(APlayerController& Controller)
{
#if !UE_BUILD_SHIPPING
	const int32 Peer = Controller.PlayerState ? Controller.PlayerState->GetPlayerId() : INDEX_NONE;
	for (TActorIterator<ARPGSkillDamageNetworkTarget> It(Controller.GetWorld()); It; ++It)
	{
		const int32 I = It->TestIndex;
		if (I < 0 || I >= 4) continue;
		if (bMultiPeer)
		{
			if (It->SourcePeer <= 0 || It->SourceASCNetId == 0) continue;
			if (It->SourcePeer != Peer)
			{
				if (ForeignTargets[I].IsValid() && ForeignTargets[I].Get() != *It)
				{ Fail(TEXT("MultiDuplicateForeignTarget")); return; }
				if (!ForeignTargets[I].IsValid())
				{
					ForeignTargets[I] = *It;
					ForeignHealthHandles[I] = It->ASC()->GetGameplayAttributeValueChangeDelegate(URPGAttributeSet::GetCurrentHealthAttribute()).AddLambda(
						[this, I, Peer](const FOnAttributeChangeData& Change)
						{
							if (Change.NewValue != Change.OldValue && Change.NewValue < ARPGSkillDamageNetworkTarget::BaselineHealth)
							{
								bForeignHealthNotified[I] = true;
								UE_LOG(LogTemp, Display, TEXT("RPG_SKILL_MULTI CROSS_NOTIFY Peer=%d OtherPeer=%d Index=%d NetId=%llu Old=%.6f New=%.6f"),
									Peer, ForeignTargets[I]->SourcePeer, I, NetId(ForeignTargets[I].Get()), Change.OldValue, Change.NewValue);
							}
						});
				}
				continue;
			}
		}
		if (Targets[I].IsValid() && Targets[I].Get() != *It) { Fail(TEXT("DuplicateTargetIdentity")); return; }
		if (!Targets[I].IsValid())
		{
			Targets[I] = *It;
			HealthHandles[I] = It->ASC()->GetGameplayAttributeValueChangeDelegate(URPGAttributeSet::GetCurrentHealthAttribute()).AddLambda(
				[this,I](const FOnAttributeChangeData& Change)
				{
					if (Change.NewValue != Change.OldValue && Change.NewValue < ARPGSkillDamageNetworkTarget::BaselineHealth)
					{
						bHealthNotified[I] = true;
						RPG_DAMAGE_LOG(TEXT("RPG_SKILL_DAMAGE HP_NOTIFY Index=%d NetId=%llu Old=%.6f New=%.6f"),
							I, NetId(Targets[I].Get()), Change.OldValue, Change.NewValue);
					}
				});
		}
	}
	for (int32 I = 0; I < 4; ++I) if (!Targets[I].IsValid() || NetId(Targets[I].Get()) == 0) return;
	if (bMultiPeer)
	{
		for (int32 I = 0; I < 4; ++I)
			if (!ForeignTargets[I].IsValid() || NetId(ForeignTargets[I].Get()) == 0) return;
		if (Targets[0]->SourceASCNetId != NetId(SourceASC.Get()) ||
			Targets[0]->SourceASCNetId == ForeignTargets[0]->SourceASCNetId)
		{ Fail(TEXT("MultiPlayerASCIdentityMismatch")); return; }
	}
	if (!bClientReady)
	{
		for (int32 I = 0; I < 4; ++I)
			if (Targets[I]->Attributes()->GetCurrentHealth() != ARPGSkillDamageNetworkTarget::BaselineHealth) return;
		if (bMultiPeer)
		{
			for (int32 I = 0; I < 4; ++I)
				if (ForeignTargets[I]->Attributes()->GetCurrentHealth() != ARPGSkillDamageNetworkTarget::BaselineHealth) return;
			auto* Character = Cast<ACharacter>(Controller.GetPawn());
			if (!Character || !Character->GetActorLocation().Equals(Targets[0]->FixtureSourceLocation, 2.0)) return;
			Character->GetCharacterMovement()->DisableMovement();
			for (int32 I = 0; I < 4; ++I)
				UE_LOG(LogTemp, Display, TEXT("RPG_SKILL_MULTI CROSS_BASELINE Peer=%d OtherPeer=%d Index=%d NetId=%llu Health=1000000.000000"),
					Peer, ForeignTargets[I]->SourcePeer, I, NetId(ForeignTargets[I].Get()));
		}
		for (int32 I = 0; I < 4; ++I)
			RPG_DAMAGE_LOG(TEXT("RPG_SKILL_DAMAGE BASELINE Index=%d NetId=%llu Health=%.6f"),
				I, NetId(Targets[I].Get()), Targets[I]->Attributes()->GetCurrentHealth());
		bClientReady = true;
	}
	if (bMultiPeer && !bMultiReadySent)
	{
		ARPGSkillMultiPeerCoordinator* Coordinator = nullptr;
		for (TActorIterator<ARPGSkillMultiPeerCoordinator> It(Controller.GetWorld()); It; ++It)
			if (It->SourcePeer == Peer && It->SourceASCNetId == NetId(SourceASC.Get()) && It->GetOwner() == &Controller)
			{
				if (Coordinator) { Fail(TEXT("MultiDuplicateClientCoordinator")); return; }
				Coordinator = *It;
			}
		if (!Coordinator) return;
		bMultiReadySent = true;
		Coordinator->ServerFixtureReady();
		UE_LOG(LogTemp, Display, TEXT("RPG_SKILL_MULTI FIXTURE_READY Peer=%d ASCNetId=%llu Targets=8"), Peer, NetId(SourceASC.Get()));
	}
	if (bMultiPeer) bMultiInputReady = Targets[0]->bInputReady && ForeignTargets[0]->bInputReady;
	for (int32 I = 1; I < 4; ++I)
		if (Targets[I]->Attributes()->GetCurrentHealth() != ARPGSkillDamageNetworkTarget::BaselineHealth || bHealthNotified[I])
		{ Fail(TEXT("RejectedTargetReplicatedDamage")); return; }
	for (int32 I = 0; I < 4; ++I)
		if (!Targets[I]->bSuiteFinished || Targets[I]->Attributes()->GetCurrentHealth() != Targets[I]->ExpectedHealth) return;
	if (!bHealthNotified[0] || Targets[0]->ExpectedHealth >= ARPGSkillDamageNetworkTarget::BaselineHealth) return;
	if (bMultiPeer)
	{
		for (int32 I = 0; I < 4; ++I)
		{
			if (I != 0 && (ForeignTargets[I]->Attributes()->GetCurrentHealth() != ARPGSkillDamageNetworkTarget::BaselineHealth || bForeignHealthNotified[I]))
			{ Fail(TEXT("MultiForeignRejectedTargetDamaged")); return; }
			if (!ForeignTargets[I]->bSuiteFinished || ForeignTargets[I]->Attributes()->GetCurrentHealth() != ForeignTargets[I]->ExpectedHealth) return;
		}
		if (!bForeignHealthNotified[0] || !bMultiAllVerified) return;
		for (int32 I = 0; I < 4; ++I)
			UE_LOG(LogTemp, Display, TEXT("RPG_SKILL_MULTI CROSS_RESULT Peer=%d OtherPeer=%d Index=%d NetId=%llu Before=1000000.000000 After=%.6f Notify=%d"),
				Peer, ForeignTargets[I]->SourcePeer, I, NetId(ForeignTargets[I].Get()), ForeignTargets[I]->Attributes()->GetCurrentHealth(), bForeignHealthNotified[I] ? 1 : 0);
		UE_LOG(LogTemp, Display, TEXT("RPG_SKILL_MULTI CLIENT_COMPLETE Peer=%d OtherPeer=%d Targets=8"), Peer, ForeignTargets[0]->SourcePeer);
	}
	for (int32 I = 0; I < 4; ++I)
		RPG_DAMAGE_LOG(TEXT("RPG_SKILL_DAMAGE RESULT Role=Client Index=%d NetId=%llu Before=%.6f After=%.6f Notify=%d"),
			I, NetId(Targets[I].Get()), ARPGSkillDamageNetworkTarget::BaselineHealth,
			Targets[I]->Attributes()->GetCurrentHealth(), bHealthNotified[I] ? 1 : 0);
	RPG_DAMAGE_LOG(TEXT("RPG_SKILL_DAMAGE CLIENT_COMPLETE Targets=4 ReplicatedHealth=1"));
	if (AbuseEnabled())
	{
		if (!bAbuseQueued || !bAbuseReleaseReady) { Fail(TEXT("ClientAbuseNotPerformed")); return; }
		RPG_DAMAGE_LOG(TEXT("RPG_SKILL_ABUSE CLIENT_COMPLETE Cases=7 Recovery=1 Targets=4"));
	}
	bPassed = true;
#endif
}

void FRPGSkillDamageNetworkProbe::ObserveSentTargetData(URPGAbilitySystemComponent& ASC,
	const FGameplayAbilitySpecHandle Handle, const FPredictionKey Original,
	const FGameplayAbilityTargetDataHandle& Data, const FGameplayTag Tag, const FPredictionKey Message)
{
#if !UE_BUILD_SHIPPING
	if (!AbuseEnabled() || bAbuseQueued || !bClientReady) return;
	const auto* Spec = ASC.FindAbilitySpecFromHandle(Handle);
	const auto* Skill = Spec ? Cast<URPGGameplayAbility_SkillContainer>(Spec->GetPrimaryInstance()) : nullptr;
	FRPGSkillTargetResult Decoded;
	if (!Skill || Skill->GetClass()->GetName() != TEXT("GA_Player_ChargeSlash_C") ||
		!Original.IsValidKey() || !Message.IsValidKey() || Tag.IsValid() ||
		!FRPGSkillTargetDataCodec::Decode(Data, Decoded) || NetId(&ASC) == 0)
	{ Fail(TEXT("AbuseInitialClientTargetData")); return; }
	if (AbuseOriginal.IsValidKey())
	{
		if (bAbusePrimed || Handle != AbuseHandle || Original != AbuseOriginal || Message == AbuseInitialMessage)
		{ Fail(TEXT("AbuseAuthoredInputPressTargetData")); return; }
		bAbusePrimed = true;
		AbuseLastFreshMessage = Message;
		RPG_DAMAGE_LOG(TEXT("RPG_SKILL_ABUSE PRIMED Role=Client ASCNetId=%llu Handle=%s Original=%d Message=%d Entries=%d Valid=1"),
			NetId(&ASC), *Handle.ToString(), Original.Current, Message.Current, Data.Num());
		return;
	}
	AbuseHandle = Handle;
	AbuseOriginal = Original;
	AbuseInitialMessage = Message;
	AbuseInitialData = Data; // Immutable payload captured from the authored ability's actual send.
	RPG_DAMAGE_LOG(TEXT("RPG_SKILL_ABUSE INITIAL Role=Client ASCNetId=%llu Handle=%s Original=%d Message=%d Entries=%d Valid=1"),
		NetId(&ASC), *Handle.ToString(), Original.Current, Message.Current, Data.Num());
#endif
}

bool FRPGSkillDamageNetworkProbe::HoldForAbuseVerification(URPGAbilitySystemComponent& ASC)
{
#if !UE_BUILD_SHIPPING
	if (!AbuseEnabled()) return false;
	if (!FailureReason.IsEmpty()) return true;
	if (!AbuseOriginal.IsValidKey() || !bClientReady || !Targets[3].IsValid())
	{ Fail(TEXT("AbuseInitialSendNotCaptured")); return true; }
	if (!bAbusePrimed) return true;
	if (bMultiPeer && (!Targets[0]->bPeerPrimed || !ForeignTargets[0].IsValid() || !ForeignTargets[0]->bPeerPrimed)) return true;
	if (!bAbuseQueued)
	{
		bAbuseQueued = true;
		for (int32 Case = 0; Case < AbuseCaseCount; ++Case)
		{
			FGameplayAbilityTargetDataHandle Data = AbuseInitialData;
			FPredictionKey Original = AbuseOriginal;
			FGameplayTag Tag;
			if (Case == 1 || Case == 4)
			{
				Data.Clear();
				auto* Actors = new FGameplayAbilityTargetData_ActorArray();
				Actors->TargetActorArray.Add(bMultiPeer && Case == 4 ? ForeignTargets[0].Get() : Targets[3].Get());
				Data.Add(Actors);
				if (bMultiPeer && Case == 4)
					UE_LOG(LogTemp, Display, TEXT("RPG_SKILL_MULTI FORGED_TARGET Role=Client Peer=%d OtherPeer=%d NetId=%llu"),
						Targets[0]->SourcePeer, ForeignTargets[0]->SourcePeer, NetId(ForeignTargets[0].Get()));
			}
			if (Case == 2) Original.Current = Original.Current == 1 ? 32767 : Original.Current - 1;
			if (Case == 3)
			{
				Data.Clear();
				for (const int32 Count : { 31, 31, 3 })
				{
					auto* Actors = new FGameplayAbilityTargetData_ActorArray();
					Actors->TargetActorArray.SetNum(Count);
					Data.Add(Actors);
				}
			}
			if (Case == 5)
			{
				FRPGSkillTargetResult Far;
				if (!FRPGSkillTargetDataCodec::Decode(AbuseInitialData, Far))
				{ Fail(TEXT("AbuseFarPayloadEncode")); return true; }
				Far.TargetActor = nullptr;
				Far.TargetLocation = Far.SourceLocation + Far.AimDirection * 1000000.0;
				Data = FRPGSkillTargetDataCodec::Encode(Far);
			}
			if (Case == 6) Tag = RPGGameplayTags::Shared_SetByCaller_BaseDamage;
			auto Submit = [&](const FPredictionKey Message)
			{
				ASC.ServerSetReplicatedTargetData(AbuseHandle, Original, Data, Tag, Message);
				RPG_DAMAGE_LOG(TEXT("RPG_SKILL_ABUSE SENT Index=%d Name=%s ASCNetId=%llu Handle=%s Original=%d Message=%d Entries=%d"),
					Case, AbuseNames[Case], NetId(&ASC), *AbuseHandle.ToString(), Original.Current, Message.Current, Data.Num());
			};
			if (Case < 2) Submit(AbuseInitialMessage);
			else
			{
				// Use real GAS keys: a fabricated large key would block the authored release's next message.
				FScopedPredictionWindow Window(&ASC, true);
				Submit(ASC.ScopedPredictionKey);
			}
		}
	}
	if (!Targets[0]->bAbuseRequestsVerified) return true;
	if (bMultiPeer)
	{
		if (!ForeignTargets[0]->bAbuseRequestsVerified) return true;
		if (!bMultiAllVerified)
		{
			for (int32 I = 0; I < 4; ++I)
				if (ForeignTargets[I]->Attributes()->GetCurrentHealth() != ARPGSkillDamageNetworkTarget::BaselineHealth || bForeignHealthNotified[I])
				{ Fail(TEXT("MultiForeignHPChangedBeforeRelease")); return true; }
			bMultiAllVerified = true;
			UE_LOG(LogTemp, Display, TEXT("RPG_SKILL_MULTI ALL_VERIFIED Peer=%d OtherPeer=%d Cases=14 HealthUnchanged=1"),
				Targets[0]->SourcePeer, ForeignTargets[0]->SourcePeer);
		}
	}
	if (!bAbuseReleaseReady)
	{
		for (int32 I = 0; I < 4; ++I)
			if (Targets[I]->Attributes()->GetCurrentHealth() != ARPGSkillDamageNetworkTarget::BaselineHealth || bHealthNotified[I])
			{ Fail(TEXT("AbuseClientHealthChangedBeforeRelease")); return true; }
		bAbuseReleaseReady = true;
		RPG_DAMAGE_LOG(TEXT("RPG_SKILL_ABUSE RELEASE_READY ASCNetId=%llu Original=%d Cases=7 HealthUnchanged=1"), NetId(&ASC), AbuseOriginal.Current);
	}
#endif
	return false;
}

void FRPGSkillDamageNetworkProbe::ObserveReceivedTargetData(URPGAbilitySystemComponent& ASC,
	const FGameplayAbilitySpecHandle Handle, const FPredictionKey Original,
	const FGameplayAbilityTargetDataHandle& Data, const FGameplayTag Tag,
	const FPredictionKey Message, const bool bAdmitted)
{
#if !UE_BUILD_SHIPPING
	if (!AbuseEnabled() || !bPrepared || !FailureReason.IsEmpty()) return;
	const auto* Spec = ASC.FindAbilitySpecFromHandle(Handle);
	auto* Skill = Spec ? Cast<URPGGameplayAbility_SkillContainer>(Spec->GetPrimaryInstance()) : nullptr;
	if (!Skill || Skill->GetClass()->GetName() != TEXT("GA_Player_ChargeSlash_C")) return;
	if (!AbuseOriginal.IsValidKey())
	{
		if (!bAdmitted || !Spec->IsActive() || !Skill->GetActiveSkillTargetResult().bIsValid || Tag.IsValid() || NetId(&ASC) == 0)
		{ Fail(TEXT("AbuseInitialServerTargetData")); return; }
		AbuseHandle = Handle;
		AbuseOriginal = Original;
		AbuseInitialMessage = AbuseLastFreshMessage = Message;
		AbuseInitialData = Data;
		// Initialize the existing runtime/target geometry oracle even if all RPCs arrive in one server tick.
		if (auto* Pawn = Cast<APawn>(ASC.GetAvatarActor()))
			if (auto* PC = Cast<APlayerController>(Pawn->GetController())) TickServer(*PC, ASC, Handle);
		if (!bObservedActive || !FailureReason.IsEmpty()) { Fail(TEXT("AbuseMissingAuthoredRuntime")); return; }
		RPG_DAMAGE_LOG(TEXT("RPG_SKILL_ABUSE INITIAL Role=Server ASCNetId=%llu Handle=%s Original=%d Message=%d Entries=%d Valid=1"),
			NetId(&ASC), *Handle.ToString(), Original.Current, Message.Current, Data.Num());
		return;
	}
	if (!bAbusePrimed)
	{
		if (!bAdmitted || !Spec->IsActive() || Handle != AbuseHandle || Original != AbuseOriginal ||
			Message == AbuseInitialMessage || Tag.IsValid() || !Skill->GetActiveSkillTargetResult().bIsValid)
		{ Fail(TEXT("AbuseAuthoredServerInputPressTargetData")); return; }
		bAbusePrimed = true;
		AbuseLastFreshMessage = Message;
		if (bMultiPeer)
		{
			Targets[0]->bPeerPrimed = true;
			Targets[0]->ForceNetUpdate();
		}
		RPG_DAMAGE_LOG(TEXT("RPG_SKILL_ABUSE PRIMED Role=Server ASCNetId=%llu Handle=%s Original=%d Message=%d Entries=%d Valid=1"),
			NetId(&ASC), *Handle.ToString(), Original.Current, Message.Current, Data.Num());
		return;
	}
	if (AbuseReceipts == AbuseCaseCount)
	{
		if (bAbuseRecoveryObserved || !bAdmitted || Handle != AbuseHandle || Original != AbuseOriginal ||
			Message == AbuseLastFreshMessage || !Spec->IsActive() || !Skill->GetActiveSkillTargetResult().bIsValid || Tag.IsValid())
		{ Fail(TEXT("AbuseNormalReleaseTargetDidNotRecover")); return; }
		bAbuseRecoveryObserved = true;
		RPG_DAMAGE_LOG(TEXT("RPG_SKILL_ABUSE RECOVERY ASCNetId=%llu Handle=%s Original=%d Message=%d Valid=1 Events=%d Effects=%d"),
			NetId(&ASC), *Handle.ToString(), Original.Current, Message.Current, EventCount, EffectCounts[0]);
		return;
	}
	const int32 Case = AbuseReceipts;
	const bool bExpectedAdmitted = Case >= 4;
	const int32 ExpectedEntries = Case == 3 ? 3 : (Case == 1 || Case == 4 || Case == 5 ? 1 : AbuseInitialData.Num());
	FPredictionKey ExpectedOriginal = AbuseOriginal;
	if (Case == 2) ExpectedOriginal.Current = ExpectedOriginal.Current == 1 ? 32767 : ExpectedOriginal.Current - 1;
	const bool bTargetValid = Skill->GetActiveSkillTargetResult().bIsValid;
	bool bPayloadMatches = Data.Num() == ExpectedEntries;
	if (bMultiPeer)
	{
		for (TActorIterator<ARPGSkillDamageNetworkTarget> It(ASC.GetWorld()); It; ++It)
			if (It->SourcePeer != Targets[0]->SourcePeer && It->TestIndex >= 0 && It->TestIndex < 4)
			{
				if (ForeignTargets[It->TestIndex].IsValid() && ForeignTargets[It->TestIndex].Get() != *It)
				{ Fail(TEXT("MultiDuplicateServerForeignTarget")); return; }
				ForeignTargets[It->TestIndex] = *It;
			}
		for (int32 I = 0; I < 4; ++I)
			if (!ForeignTargets[I].IsValid()) { Fail(TEXT("MultiMissingServerForeignTarget")); return; }
	}
	if (Case == 1 || Case == 4)
	{
		const auto* Entry = Data.Get(0);
		const auto Actors = Entry ? Entry->GetActors() : TArray<TWeakObjectPtr<AActor>>();
		bPayloadMatches &= Actors.Num() == 1 && Actors[0].Get() == (bMultiPeer && Case == 4 ? ForeignTargets[0].Get() : Targets[3].Get()) &&
			Entry->GetScriptStruct() == FGameplayAbilityTargetData_ActorArray::StaticStruct();
		if (bMultiPeer && Case == 4 && bPayloadMatches)
			UE_LOG(LogTemp, Display, TEXT("RPG_SKILL_MULTI FORGED_TARGET Role=Server Peer=%d OtherPeer=%d NetId=%llu"),
				Targets[0]->SourcePeer, ForeignTargets[0]->SourcePeer, NetId(ForeignTargets[0].Get()));
	}
	if (Case == 3)
	{
		int32 Actors = 0;
		for (int32 I = 0; I < Data.Num(); ++I) if (const auto* Entry = Data.Get(I)) Actors += Entry->GetActors().Num();
		bPayloadMatches &= Actors == 65;
	}
	if (Case == 5)
	{
		FRPGSkillTargetResult Far;
		bPayloadMatches &= FRPGSkillTargetDataCodec::Decode(Data, Far) &&
			FVector::Distance(Far.SourceLocation, Far.TargetLocation) > Skill->GetActiveSkillSpec().SecurityProfile.MaximumServerHitDistance;
	}
	const auto* Security = ASC.GetAvatarActor()->FindComponentByClass<URPGSecurityValidationComponent>();
	if (!bPayloadMatches || Handle != AbuseHandle || Original != ExpectedOriginal || bAdmitted != bExpectedAdmitted ||
		bTargetValid != (Case < 4) || !Spec->IsActive() || EventCount != 0 ||
		Tag != (Case == 6 ? RPGGameplayTags::Shared_SetByCaller_BaseDamage : FGameplayTag()) ||
		(Case < 2 ? Message != AbuseInitialMessage : Message == AbuseLastFreshMessage) || !Security || Security->IsRestricted())
	{ Fail(TEXT("AbuseReceiverPayloadAdmissionOrSkillState")); return; }
	if (Case >= 2) AbuseLastFreshMessage = Message;
	if (bMultiPeer)
	{
		const int32 OtherPeer = ForeignTargets[0]->SourcePeer;
		URPGAbilitySystemComponent* OtherASC = nullptr;
		for (FConstPlayerControllerIterator It = ASC.GetWorld()->GetPlayerControllerIterator(); It; ++It)
			if (const auto* PC = It->Get(); PC && PC->PlayerState && PC->PlayerState->GetPlayerId() == OtherPeer)
				if (const APawn* Pawn = PC->GetPawn())
					if (const auto* Interface = Cast<IAbilitySystemInterface>(Pawn))
						OtherASC = Cast<URPGAbilitySystemComponent>(Interface->GetAbilitySystemComponent());
		const FGameplayAbilitySpec* OtherSpec = nullptr;
		if (OtherASC) for (const auto& Candidate : OtherASC->GetActivatableAbilities())
			if (Candidate.Ability && Candidate.Ability->GetClass()->GetName() == TEXT("GA_Player_ChargeSlash_C")) OtherSpec = &Candidate;
		const auto* OtherSkill = OtherSpec ? OtherSpec->GetPrimaryInstance() : nullptr;
		const auto* OtherSecurity = OtherASC && OtherASC->GetAvatarActor()
			? OtherASC->GetAvatarActor()->FindComponentByClass<URPGSecurityValidationComponent>() : nullptr;
		if (!OtherSpec || !OtherSkill || !OtherSpec->IsActive() || !OtherSecurity || OtherSecurity->IsRestricted() ||
			NetId(OtherASC) != ForeignTargets[0]->SourceASCNetId || ForeignTargets[0]->ObservedEvents != 0)
		{ Fail(TEXT("MultiForeignActivationNotActiveOrIsolated")); return; }
		UE_LOG(LogTemp, Display, TEXT("RPG_SKILL_MULTI PEER_ACTIVE Peer=%d OtherPeer=%d Index=%d ASCNetId=%llu Handle=%s Original=%d Active=1 Events=0"),
			Targets[0]->SourcePeer, OtherPeer, Case, NetId(OtherASC), *OtherSpec->Handle.ToString(), OtherSkill->GetCurrentActivationInfo().GetActivationPredictionKey().Current);
		for (int32 I = 0; I < 4; ++I)
		{
			if (ForeignTargets[I]->Attributes()->GetCurrentHealth() != ARPGSkillDamageNetworkTarget::BaselineHealth || ForeignTargets[I]->AppliedEffects != 0)
			{ Fail(TEXT("MultiForeignServerHPChangedDuringAbuse")); return; }
			UE_LOG(LogTemp, Display, TEXT("RPG_SKILL_MULTI CROSS_HP Peer=%d OtherPeer=%d Index=%d Target=%d NetId=%llu Health=1000000.000000 Effects=0"),
				Targets[0]->SourcePeer, OtherPeer, Case, I, NetId(ForeignTargets[I].Get()));
		}
	}
	for (int32 I = 0; I < 4; ++I)
	{
		if (!Targets[I].IsValid() || Targets[I]->Attributes()->GetCurrentHealth() != ARPGSkillDamageNetworkTarget::BaselineHealth || EffectCounts[I] != 0)
		{ Fail(TEXT("AbuseChangedActualServerHealth")); return; }
		RPG_DAMAGE_LOG(TEXT("RPG_SKILL_ABUSE HP Index=%d Target=%d NetId=%llu Health=%.6f Effects=%d"),
			Case, I, NetId(Targets[I].Get()), Targets[I]->Attributes()->GetCurrentHealth(), EffectCounts[I]);
	}
	RPG_DAMAGE_LOG(TEXT("RPG_SKILL_ABUSE RECEIVED Index=%d Name=%s ASCNetId=%llu Handle=%s Original=%d Message=%d Entries=%d Admitted=%d TargetValid=%d Active=1 Events=%d"),
		Case, AbuseNames[Case], NetId(&ASC), *Handle.ToString(), Original.Current, Message.Current, Data.Num(), bAdmitted ? 1 : 0, bTargetValid ? 1 : 0, EventCount);
	if (++AbuseReceipts == AbuseCaseCount)
	{
		Targets[0]->bAbuseRequestsVerified = true;
		Targets[0]->ForceNetUpdate();
		RPG_DAMAGE_LOG(TEXT("RPG_SKILL_ABUSE VERIFIED Cases=7 HealthUnchanged=1"));
	}
#endif
}
