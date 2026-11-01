#include "Components/BoxComponent.h"
#include "Components/InterpToMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "Tests/RPGCombatControlTestFixture.h"
#include "Tests/RPGSkillDamageSecurityTestFixture.h"
#include "Ability/RPGNPCGameplayAbility.h"
#include "Attribute/RPGAttributeSet.h"
#include "Combat/RPGApprovedHitContext.h"
#include "Combat/RPGKnockbackTypes.h"
#include "Component/Combat/RPGCombatControlComponent.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Component/RPGCharacterMovementComponent.h"
#include "Component/RPGSecurityValidationComponent.h"
#include "DataAsset/StartUpData/DataAsset_NPCStartUpData.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Tests/RPGTestWorldCleanup.h"
#include "GameFramework/RootMotionSource.h"
#include "GameFramework/WorldSettings.h"
#include "GameplayEffect/RPGKnockbackEffect.h"
#include "Misc/AutomationTest.h"
#include "RPGInputTags.h"
#include "Security/RPGDamageExecutionGuard.h"
#include "Skill/RPGSkillDefinition.h"
#include <limits>

struct FRPGKnockbackTestAccess
{
	static void DisableVisual(URPGCombatControlComponent& C) { C.StunVisual.Reset(); }
	static FActiveGameplayEffectHandle Effect(const URPGCombatControlComponent& C) { return C.OwnedKnockback; }
	static FName Motion(const URPGCombatControlComponent& C) { return C.KnockbackMotionName; }
};

namespace RPGKnockbackTests
{
	struct FWorldScope
	{
		UWorld* World = nullptr;
		~FWorldScope() { if (World) { RPGTestWorld::Destroy(World); } }
		bool Initialize()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()));
			if (!World) return false;
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL()); World->BeginPlay(); World->GetWorldSettings()->NotifyBeginPlay();
			return true;
		}
		ARPGStunTestCharacter* Spawn(FVector Location = FVector(0.f, 0.f, 1000.f))
		{
			FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			auto* C = World->SpawnActor<ARPGStunTestCharacter>(Location, FRotator::ZeroRotator, Params);
			auto* ASC = C->GetRPGAbilitySystemComponent(); ASC->InitAbilityActorInfo(C, C);
			ASC->SetNumericAttributeBase(URPGAttributeSet::GetMaxHealthAttribute(), 100.f);
			ASC->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(), 100.f);
			C->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
			// This native fixture has no controller; opt into CMC physics explicitly.
			C->GetCharacterMovement()->bRunPhysicsWithNoController = true;
			FRPGKnockbackTestAccess::DisableVisual(*C->GetCombatControlComponent());
			return C;
		}
	};
	FRPGKnockbackSettings Settings(float Distance = 300.f, float Duration = .6f)
	{
		FRPGKnockbackSettings Result; Result.Distance = Distance; Result.Duration = Duration; return Result;
	}
	bool Apply(ARPGBaseCharacter* Source, ARPGBaseCharacter* Target, float Distance = 300.f, float Duration = .6f, FGuid Receipt = FGuid::NewGuid())
	{
		FRPGApprovedHitContext Context(Source, Target);
		return Target->GetCombatControlComponent()->ApplyApprovedDamageKnockback(Source,
			FHitResult(Target, nullptr, Target->GetActorLocation(), Context.AwayDirection), Receipt, Settings(Distance, Duration), Context.AwayDirection);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGKnockbackValidationTest, "ProjectRPG.Combat.Knockback.ValidationAndImmunity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGKnockbackValidationTest::RunTest(const FString&)
{
	using namespace RPGKnockbackTests;
	FWorldScope Scope; if (!Scope.Initialize()) return false;
	auto* Source = Scope.Spawn(FVector(-500.f, 0.f, 1000.f)); auto* Target = Scope.Spawn();
	auto* Control = Target->GetCombatControlComponent(); auto* ASC = Target->GetRPGAbilitySystemComponent();
	TestFalse(TEXT("Disabled attacks do not push"), Apply(Source, Target, 0.f));
	TestFalse(TEXT("Excess distance rejected"), Apply(Source, Target, 1001.f));
	TestFalse(TEXT("Excess velocity rejected"), Apply(Source, Target, 1000.f, .1f));
	TestFalse(TEXT("Nonfinite configuration rejected"), Apply(Source, Target, std::numeric_limits<float>::quiet_NaN()));
	TestFalse(TEXT("No invented approval"), Apply(Source, Target, 300.f, .6f, FGuid()));
	TestFalse(TEXT("No arbitrary vector or vertical launch"), Control->ApplyApprovedDamageKnockback(Source,
		FHitResult(Target, nullptr, FVector::ZeroVector, FVector::UpVector), FGuid::NewGuid(), Settings(), FVector::UpVector));
	Control->SetDefaultKnockbackImmunity(true);
	TestFalse(TEXT("Target default immunity"), Apply(Source, Target)); Control->SetDefaultKnockbackImmunity(false);
	const auto Receipt = FGuid::NewGuid(); ASC->AddLooseGameplayTag(RPGKnockback::ImmunityTag());
	TestFalse(TEXT("Temporary immunity"), Apply(Source, Target, 300.f, .6f, Receipt));
	ASC->RemoveLooseGameplayTag(RPGKnockback::ImmunityTag());
	TestFalse(TEXT("Rejected hit cannot be replayed after immunity"), Apply(Source, Target, 300.f, .6f, Receipt));
	const auto Accepted = FGuid::NewGuid(); TestTrue(TEXT("Approved hit starts one execution"), Apply(Source, Target, 300.f, .6f, Accepted));
	const auto Id = Control->GetKnockbackState().ExecutionId;
	TestFalse(TEXT("Duplicate cannot replace or extend it"), Apply(Source, Target, 300.f, .6f, Accepted));
	TestEqual(TEXT("Execution preserved"), Control->GetKnockbackState().ExecutionId, Id);
	TestEqual(TEXT("No fake damage"), ASC->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute()), 100.f);
	Control->CancelKnockback(Id);
	auto Unnamed = MakeShared<FRootMotionSource_ConstantForce>(); Unnamed->Duration = 3.f;
	Target->GetCharacterMovement()->ApplyRootMotionSource(Unnamed);
	TestFalse(TEXT("An unnamed foreign RMS is still foreign"), Apply(Source, Target));
	TestFalse(TEXT("Rejected displacement cannot retire that source"), Unnamed->Status.HasFlag(ERootMotionSourceStatusFlags::MarkedForRemoval));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGKnockbackDataTest, "ProjectRPG.Combat.Knockback.DataAndFrozenHitContext",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGKnockbackDataTest::RunTest(const FString&)
{
	using namespace RPGKnockbackTests;
	FRPGPlayerAbilitySet Entry; Entry.InputTag = RPGGameplayTags::InputTag_Roll;
	Entry.AbilityToGrant = URPGStunTestCombatAbility::StaticClass(); Entry.StunDuration = 1.f; Entry.Knockback = Settings();
	FGameplayAbilitySpec Spec;
	TestTrue(TEXT("Existing GA grant data"), Entry.BuildAbilitySpec(2, GetTransientPackage(), Spec));
	Entry.Knockback.Distance = 0.f;
	TestEqual(TEXT("Distance frozen at grant"), RPGKnockback::GetGrantedSettings(&Spec).Distance, 300.f);
	TestEqual(TEXT("Other control metadata retained"), RPGCombatControl::GetGrantedStunDuration(&Spec), 1.f);
	TestTrue(TEXT("Input ownership retained"), Spec.GetDynamicSpecSourceTags().HasTagExact(Entry.InputTag));
	auto* NPC = NewObject<UDataAsset_NPCStartUpData>(); NPC->AttackKnockbacks.Add(URPGNPCGameplayAbility::StaticClass(), Settings(400.f));
	TestTrue(TEXT("NPC uses the same grant metadata"), NPC->BuildAttackAbilitySpec(URPGNPCGameplayAbility::StaticClass(), 1, NPC, Spec));
	TestEqual(TEXT("NPC configuration frozen"), RPGKnockback::GetGrantedSettings(&Spec).Distance, 400.f);
	auto* Definition = NewObject<URPGSkillDefinition>(); Definition->Knockback = Settings(250.f);
	FRPGSkillRuntimeSpec Runtime; Definition->BuildRuntimeSpec(nullptr, FRPGSkillSaveData(), Runtime); Definition->Knockback.Distance = 0.f;
	TestEqual(TEXT("Canonical skill activation frozen"), Runtime.Knockback.Distance, 250.f);
	FWorldScope Scope; if (!Scope.Initialize()) return false;
	auto* Source = Scope.Spawn(FVector(-500.f, 0.f, 1000.f)); auto* Target = Scope.Spawn();
	const FRPGApprovedHitContext Hit(Source, Target);
	Source->SetActorLocation(FVector(0.f, -500.f, 1000.f));
	TestEqual(TEXT("Hit direction cannot change in a damage callback"), Hit.AwayDirection, FVector::ForwardVector);
	TestTrue(TEXT("Same living avatars still eligible"), Hit.IsCurrent(Source, Target));
	Target->GetCombatControlComponent()->ResetForAvatarReuse();
	TestFalse(TEXT("Same Pawn/ASC reset retires the old hit even without a presentation revision"), Hit.IsCurrent(Source, Target));
	const FRPGApprovedHitContext BeforeDeath(Source, Target);
	Target->GetRPGAbilitySystemComponent()->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(), 0.f);
	Target->GetRPGAbilitySystemComponent()->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(), 100.f);
	TestFalse(TEXT("Death/revival in a damage callback retires its old outcome"), BeforeDeath.IsCurrent(Source, Target));
	Target->AdvanceLife();
	TestFalse(TEXT("Same Pawn's next life is not the approved victim"), Hit.IsCurrent(Source, Target));
	const FRPGApprovedHitContext Fresh(Source, Target);
	Target->GetRPGAbilitySystemComponent()->ClearActorInfo();
	TestFalse(TEXT("ActorInfo invalidation retires the outcome"), Fresh.IsCurrent(Source, Target));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGKnockbackOwnershipTest, "ProjectRPG.Combat.Knockback.ReplacementOwnership",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGKnockbackOwnershipTest::RunTest(const FString&)
{
	using namespace RPGKnockbackTests;
	FWorldScope Scope; if (!Scope.Initialize()) return false;
	auto* Source = Scope.Spawn(FVector(-500.f, 0.f, 1000.f)); auto* Target = Scope.Spawn();
	auto* Control = Target->GetCombatControlComponent(); auto* Move = Target->GetCharacterMovement();
	TestTrue(TEXT("First execution"), Apply(Source, Target));
	const auto Old = Control->GetKnockbackState().ExecutionId; const auto OldName = FRPGKnockbackTestAccess::Motion(*Control);
	const auto OldEffect = FRPGKnockbackTestAccess::Effect(*Control);
	TestTrue(TEXT("New approved hit replaces previous motion"), Apply(Source, Target, 200.f, .5f));
	TestTrue(TEXT("Execution id changes"), Control->GetKnockbackState().ExecutionId != Old);
	TestNull(TEXT("Old GE removed"), Target->GetRPGAbilitySystemComponent()->GetActiveGameplayEffect(OldEffect));
	const auto OldMotion = Move->GetRootMotionSource(OldName);
	TestTrue(TEXT("Old motion retired"), !OldMotion.IsValid() || OldMotion->Status.HasFlag(ERootMotionSourceStatusFlags::MarkedForRemoval));
	TestFalse(TEXT("Stale cancellation cannot cancel new execution"), Control->CancelKnockback(Old));
	auto* ASC = Target->GetRPGAbilitySystemComponent();
	ASC->SetActiveGameplayEffectInhibit(FActiveGameplayEffectHandle(FRPGKnockbackTestAccess::Effect(*Control)), true, false);
	TestFalse(TEXT("Inhibition retires active motion immediately"), Control->HasKnockbackMotion());
	TestFalse(TEXT("Inhibition clears this execution"), Control->GetKnockbackState().bActive);
	const auto InhibitOnApply = ASC->OnActiveGameplayEffectAddedDelegateToSelf.AddLambda(
		[ASC](UAbilitySystemComponent*, const FGameplayEffectSpec& Spec, FActiveGameplayEffectHandle Handle)
		{
			if (Spec.Def->GetClass() == URPGKnockbackEffect::StaticClass())
				ASC->SetActiveGameplayEffectInhibit(MoveTemp(Handle), true, false);
		});
	TestFalse(TEXT("Already inhibited GE cannot start displacement"), Apply(Source, Target));
	ASC->OnActiveGameplayEffectAddedDelegateToSelf.Remove(InhibitOnApply);
	TestFalse(TEXT("No active snapshot after reentrant inhibition"), Control->GetKnockbackState().bActive);
	TestTrue(TEXT("No inhibited GE leaked"), ASC->GetActiveEffects(FGameplayEffectQuery()).IsEmpty());
	TestTrue(TEXT("Fresh hit after inhibition can start"), Apply(Source, Target));
	auto Foreign = MakeShared<FRootMotionSource_ConstantForce>(); Foreign->InstanceName = TEXT("Foreign_Test"); Foreign->Duration = 3.f;
	Move->ApplyRootMotionSource(Foreign);
	Move->SetMovementMode(MOVE_None); Move->Velocity = FVector(77.f, 0.f, -50.f);
	TestTrue(TEXT("Only the current execution can be cleansed"), Control->CancelKnockback(Control->GetKnockbackState().ExecutionId));
	TestFalse(TEXT("Foreign RMS preserved"), Foreign->Status.HasFlag(ERootMotionSourceStatusFlags::MarkedForRemoval));
	TestEqual(TEXT("Foreign velocity is not reset"), Move->Velocity.X, 77.0);
	TestEqual(TEXT("No walking-mode restoration"), EMovementMode(Move->MovementMode), MOVE_None);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGKnockbackCompositionTest, "ProjectRPG.Combat.Knockback.StunAndGravity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGKnockbackCompositionTest::RunTest(const FString&)
{
	using namespace RPGKnockbackTests;
	FWorldScope Scope; if (!Scope.Initialize()) return false;
	auto* Source = Scope.Spawn(FVector(-500.f, 0.f, 1000.f)); auto* Target = Scope.Spawn();
	auto* Control = Target->GetCombatControlComponent(); auto* Move = Cast<URPGCharacterMovementComponent>(Target->GetCharacterMovement());
	const auto Stun = Control->ApplyApprovedDamageStun(Source, FHitResult(Target, nullptr, Target->GetActorLocation(), FVector::ForwardVector), FGuid::NewGuid(), 2.f);
	TestTrue(TEXT("Independent stun"), Stun.IsValid());
	TestTrue(TEXT("Approved knockback still moves a stunned target"), Apply(Source, Target));
	TestEqual(TEXT("Input remains blocked"), Move->ConstrainInputAcceleration(FVector(100.f, 0.f, 0.f)), FVector::ZeroVector);
	for (int32 Index=0; Index<4; ++Index) Scope.World->Tick(LEVELTICK_All, .025f);
	TestTrue(TEXT("Actual shared CMC displacement despite stun"), Target->GetActorLocation().X > 10.f);
	TestTrue(TEXT("Falling gravity continues"), Move->Velocity.Z < 0.f);
	TestEqual(TEXT("Falling mode retained"), EMovementMode(Move->MovementMode), MOVE_Falling);
	auto Context = Target->GetRPGAbilitySystemComponent()->MakeEffectContext(); Context.AddInstigator(Target, Target);
	FGameplayEffectSpec Damage(GetDefault<URPGSkillDamageSecurityTestEffect>(), Context, 1.f); int32 Out=0;
	{ FRPGDamageExecutionGuard Guard(Damage, *Target->GetRPGAbilitySystemComponent(), *Source->GetRPGAbilitySystemComponent(), 100.f);
		TestFalse(TEXT("Delayed attack cannot deal damage during displacement"), FRPGDamageExecutionGuard::Approve(Damage, Target->GetRPGAbilitySystemComponent(), Source->GetRPGAbilitySystemComponent(), 10.0, Out)); }
	Control->CancelKnockback(Control->GetKnockbackState().ExecutionId);
	TestTrue(TEXT("Knockback end does not cleanse another stun"), RPGCombatControl::IsStunned(Target));
	TestEqual(TEXT("Other restriction stays effective"), Move->GetMaxSpeed(), 0.f);
	Control->RemoveOwnedStun(Stun);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGKnockbackLifecycleTest, "ProjectRPG.Combat.Knockback.UncancellableDeathAndRecovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGKnockbackLifecycleTest::RunTest(const FString&)
{
	using namespace RPGKnockbackTests;
	FWorldScope Scope; if (!Scope.Initialize()) return false;
	auto* Source = Scope.Spawn(FVector(-500.f, 0.f, 1000.f)); auto* Target = Scope.Spawn();
	auto* Control = Target->GetCombatControlComponent(); auto* ASC = Target->GetRPGAbilitySystemComponent();
	const auto Ability = ASC->GiveAbility(FGameplayAbilitySpec(URPGStunTestCombatAbility::StaticClass(), 1)); ASC->TryActivateAbility(Ability);
	auto* Instance = Cast<URPGStunTestCombatAbility>(ASC->FindAbilitySpecFromHandle(Ability)->GetPrimaryInstance());
	Instance->SetInterruptible(false); TestFalse(TEXT("Protected combat is not partially interrupted"), Apply(Source, Target));
	Instance->SetInterruptible(true);
	const auto Callback = ASC->OnAbilityEnded.AddLambda([Target](const FAbilityEndedData&) { Target->AdvanceLife(); Target->GetCombatControlComponent()->ResetForAvatarReuse(); });
	TestFalse(TEXT("Reentrant recovery discards pending GE and motion"), Apply(Source, Target)); ASC->OnAbilityEnded.Remove(Callback);
	TestFalse(TEXT("No leaked tag into next life"), ASC->HasMatchingGameplayTag(RPGKnockback::StatusTag()));
	TestTrue(TEXT("Next life accepts new hit"), Apply(Source, Target));
	Target->GetCharacterMovement()->SetMovementMode(MOVE_None);
	ASC->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(), 0.f);
	TestFalse(TEXT("Death clears execution"), Control->GetKnockbackState().bActive);
	TestFalse(TEXT("Death clears owned tag"), ASC->HasMatchingGameplayTag(RPGKnockback::StatusTag()));
	TestEqual(TEXT("Death's mode retained"), EMovementMode(Target->GetCharacterMovement()->MovementMode), MOVE_None);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGKnockbackSecurityTest, "ProjectRPG.Combat.Knockback.SecurityWindowIsolation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGKnockbackSecurityTest::RunTest(const FString&)
{
	using namespace RPGKnockbackTests;
	FWorldScope Scope; if (!Scope.Initialize()) return false;
	auto* Source = Scope.Spawn(FVector(-500.f, 0.f, 1000.f)); auto* Target = Scope.Spawn();
	auto* Security = NewObject<URPGSecurityValidationComponent>(Target, TEXT("KnockbackSecurity")); Security->RegisterComponent();
	const auto Foreign = Security->BeginMovementAuthorization(2.f, 600.f, TEXT("OtherSystem"));
	TestTrue(TEXT("Foreign authorization exists"), Foreign.IsValid());
	TestFalse(TEXT("Cannot overwrite another system's movement window"), Apply(Source, Target));
	TestEqual(TEXT("Foreign window retained"), Security->GetAuthorizedMovementReason(), FName(TEXT("OtherSystem")));
	Security->EndMovementAuthorization(Foreign);
	TestTrue(TEXT("Can authorize one bounded knockback"), Apply(Source, Target));
	TestEqual(TEXT("Own reason"), Security->GetAuthorizedMovementReason(), FName(TEXT("RPG.Knockback")));
	TestTrue(TEXT("Bounded distance"), Security->GetRemainingAuthorizedMovementDistance() <= 325.f);
	Target->GetCombatControlComponent()->ResetForAvatarReuse();
	TestFalse(TEXT("Reset revokes only its own window"), Security->IsMovementAuthorizationActive());
	return true;
}


namespace RPGKnockbackPlatformTests
{
 using namespace RPGKnockbackTests;
 void Tick(FWorldScope& W, float Delta)
 {
  static uint64 NextFrame=GFrameCounter;
  NextFrame=FMath::Max(NextFrame,GFrameCounter)+1;
  TGuardValue<uint64> Frame(GFrameCounter, NextFrame);
  W.World->Tick(LEVELTICK_All, Delta);
 }
 UInterpToMovementComponent* Platform(FWorldScope& W)
 {
  auto* Actor=W.World->SpawnActor<AActor>(); auto* Box=NewObject<UBoxComponent>(Actor);
  Actor->SetRootComponent(Box); Actor->AddInstanceComponent(Box);
  Box->SetBoxExtent(FVector(5000,1000,50)); Box->SetMobility(EComponentMobility::Movable);
  Box->SetCollisionProfileName(TEXT("BlockAllDynamic")); Box->RegisterComponent(); Actor->SetActorLocation(FVector(0,0,-50));
  auto* Mover=NewObject<UInterpToMovementComponent>(Actor); Actor->AddInstanceComponent(Mover);
  Mover->Duration=20.f; Mover->BehaviourType=EInterpToBehaviourType::OneShot; Mover->bSweep=false;
  Mover->SetUpdatedComponent(Box); Mover->AddControlPointPosition(FVector::ZeroVector);
  Mover->AddControlPointPosition(FVector(12000,0,0)); Mover->RegisterComponent(); Mover->FinaliseControlPoints();
  return Mover;
 }
 URPGSecurityValidationComponent* Possess(FWorldScope& W, ARPGStunTestCharacter* Target)
 {
  auto* Security=NewObject<URPGSecurityValidationComponent>(Target); Target->AddInstanceComponent(Security); Security->RegisterComponent();
  auto* PC=W.World->SpawnActor<APlayerController>(); PC->SetAsLocalPlayerController();
  PC->SetPlayerState(W.World->SpawnActor<APlayerState>()); PC->Possess(Target); return Security;
 }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGKnockbackPlatformCarryTest, "ProjectRPG.Combat.KnockbackPlatform.CarryThroughPush", EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRPGKnockbackPlatformCarryTest::RunTest(const FString&)
{
 using namespace RPGKnockbackPlatformTests;
 for (bool bPlayer : {false,true}) for (float Delta : {1.f/60.f,1.f/15.f})
 {
  FWorldScope W; if (!W.Initialize()) return false;
  auto* Mover=Platform(W); auto* Source=W.Spawn(FVector(-1000,0,300)); auto* Target=W.Spawn(FVector(0,0,300));
  Target->GetCharacterMovement()->AddTickPrerequisiteComponent(Mover);
  auto* Security=bPlayer?Possess(W,Target):nullptr;
  for(int32 I=0;I<FMath::CeilToInt(3.f/Delta);++I) Tick(W,Delta);
  TestTrue(TEXT("Actual grounded dynamic base"), Target->GetCharacterMovement()->IsMovingOnGround() && Target->GetMovementBaseObject()==Mover->UpdatedComponent);
  const FVector Start=Target->GetActorLocation()-Mover->UpdatedComponent->GetComponentLocation();
  if(!TestTrue(TEXT("Approve the normal horizontal push"),Apply(Source,Target))) return false;
  auto* Control=Target->GetCombatControlComponent();
  for(int32 I=0;I<FMath::CeilToInt(1.f/Delta) && Control->GetKnockbackState().bActive;++I)
  { Target->AddMovementInput(FVector::RightVector,1.f,true); Tick(W,Delta); }
  const FVector End=Target->GetActorLocation()-Mover->UpdatedComponent->GetComponentLocation();
  AddInfo(FString::Printf(TEXT("Player=%d Hz=%.0f reason=%d relativeTravel=%.3f"),bPlayer,1.f/Delta,int32(Control->GetKnockbackState().EndReason),float(End.X-Start.X)));
  TestEqual(TEXT("Passive carry cannot end a normal push"),Control->GetKnockbackState().EndReason,ERPGKnockbackEndReason::Completed);
  TestTrue(TEXT("Authored displacement actually takes place relative to the base"), End.X-Start.X>200.f && End.X-Start.X<400.f);
  TestTrue(TEXT("Voluntary lateral input remains blocked"), FMath::Abs(End.Y-Start.Y)<1.f);
  if(Security){TestFalse(TEXT("Own authorization is released"),Security->IsMovementAuthorizationActive());TestEqual(TEXT("No carry security violation"),Security->GetTotalViolationCount(),0);}
 }
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGKnockbackPlatformBetweenMovesTest, "ProjectRPG.Combat.KnockbackPlatform.BetweenMovesAndForeignTeleport", EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRPGKnockbackPlatformBetweenMovesTest::RunTest(const FString&)
{
 using namespace RPGKnockbackPlatformTests;
 FWorldScope W; if(!W.Initialize())return false;
 auto* Mover=Platform(W); auto* Source=W.Spawn(FVector(-1000,0,300)); auto* Target=W.Spawn(FVector(0,0,300));
 Target->GetCharacterMovement()->AddTickPrerequisiteComponent(Mover); auto* Security=Possess(W,Target);
 for(int32 I=0;I<180;++I)Tick(W,1.f/60.f);
 if(!Apply(Source,Target))return false;
 Mover->SetComponentTickEnabled(false); auto* Move=Target->GetCharacterMovement(); const FVector Before=Target->GetActorLocation();
 for(int32 I=0;I<2;++I){Mover->TickComponent(.05f,LEVELTICK_All,nullptr);Move->MaybeUpdateBasedMovement(.05f);Move->MaybeSaveBaseLocation();}
 TestTrue(TEXT("Engine carry between movement delegates exceeds the original discontinuity limit"),FVector::Dist2D(Before,Target->GetActorLocation())>50.f);
 Tick(W,1.f/60.f); auto* Control=Target->GetCombatControlComponent();
 TestTrue(TEXT("Intermediate based updates preserve knockback"),Control->GetKnockbackState().bActive);
 Target->SetActorLocation(Target->GetActorLocation()+FVector(35,0,0),false,nullptr,ETeleportType::TeleportPhysics);
 Tick(W,1.f/60.f);
 TestEqual(TEXT("35cm external teleport still ends the execution"),Control->GetKnockbackState().EndReason,ERPGKnockbackEndReason::ForeignMovement);
 TestFalse(TEXT("Foreign-motion cancellation revokes the owned window"),Security->IsMovementAuthorizationActive());
 return true;
}

#endif
