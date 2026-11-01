#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Tests/RPGCombatControlTestFixture.h"
#include "Character/RPGNonPlayerCharacter.h"
#include "Attribute/RPGAttributeSet.h"
#include "Combat/RPGApprovedHitContext.h"
#include "Component/Combat/RPGCombatControlComponent.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Component/RPGCharacterMovementComponent.h"
#include "Component/RPGSecurityValidationComponent.h"
#include "Components/BoxComponent.h"
#include "Components/InterpToMovementComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Tests/RPGTestWorldCleanup.h"
#include "GameFramework/RootMotionSource.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameplayEffect/RPGAirborneEffect.h"
#include "Misc/AutomationTest.h"
#include "DataAsset/StartUpData/DataAsset_NPCStartUpData.h"
#include "Ability/RPGNPCGameplayAbility.h"
#include "Skill/RPGSkillDefinition.h"
#include <limits>

struct FRPGAirborneTestAccess
{
	static void Quiet(URPGCombatControlComponent& C) { C.StunVisual.Reset(); }
	static FActiveGameplayEffectHandle Root(const URPGCombatControlComponent& C) { return C.OwnedAirborneSequence; }
	static FActiveGameplayEffectHandle Phase(const URPGCombatControlComponent& C) { return C.OwnedAirbornePhase; }
	static FName Motion(const URPGCombatControlComponent& C) { return C.AirborneMotionName; }
	static void ForgetMotionTracking(URPGCombatControlComponent& C) { C.AirborneMotionName=NAME_None; }
	static UAnimMontage* Pose(const URPGCombatControlComponent& C) { return C.AirborneMontage.Get(); }
	static int32 PoseId(const URPGCombatControlComponent& C) { return C.AirborneMontageInstanceId; }
	static UAnimInstance* PoseAnim(const URPGCombatControlComponent& C) { return C.AirborneAnimInstance.Get(); }
	static void Presentation(URPGCombatControlComponent& C, const URPGCombatControlComponent& From) { C.AirbornePresentationProfile=From.AirbornePresentationProfile; }
	static void End(URPGCombatControlComponent& C) { C.EndAirborne(ERPGAirborneEndReason::Cleansed); }
	static void Snapshot(URPGCombatControlComponent& C, const FRPGAirborneState& State)
	{
		C.StopAirborneMotion(); C.StopAirbornePose();
		if (auto* Character=Cast<ACharacter>(C.GetOwner()))
			if (auto* Anim=Character->GetMesh()->GetAnimInstance())
			{
				// Each simulated initial snapshot starts with a fresh animation lifecycle.
				// Stopped montage instances may remain in the blend-out list until mesh ticks.
				Anim->UninitializeAnimation();
				Anim->InitializeAnimation();
			}
		C.AirborneState=State; C.AirbornePoseRevision=0; C.OnRep_AirborneState();
	}
	static void Refresh(URPGCombatControlComponent& C) { C.ApplyAirborneMotion(); C.UpdateAirbornePose(); }
};
namespace RPGAirborneTests
{
	struct FScope
	{
		UWorld* World = nullptr;
		UBoxComponent* Floor = nullptr;
		uint64 Frame = GFrameCounter;
		~FScope() { if (World) { RPGTestWorld::Destroy(World); } }
		bool Init(bool bFloor = true)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()));
			if (!World) return false;
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL()); World->BeginPlay(); World->GetWorldSettings()->NotifyBeginPlay();
			if (bFloor) Floor = Box(FVector(0, 0, -50), FVector(5000, 5000, 50));
			return true;
		}
		UBoxComponent* Box(FVector Location, FVector Extent)
		{
			auto* A = World->SpawnActor<AActor>(); auto* B = NewObject<UBoxComponent>(A);
			A->SetRootComponent(B); A->AddInstanceComponent(B); B->SetBoxExtent(Extent);
			B->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics); B->SetCollisionObjectType(ECC_WorldStatic);
			B->SetCollisionResponseToAllChannels(ECR_Block); B->RegisterComponent(); A->SetActorLocation(Location);
			return B;
		}
		ARPGStunTestCharacter* Spawn(FVector Location)
		{
			FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			auto* C = World->SpawnActor<ARPGStunTestCharacter>(Location, FRotator::ZeroRotator, P);
			auto* ASC = C->GetRPGAbilitySystemComponent(); ASC->InitAbilityActorInfo(C, C);
			ASC->SetNumericAttributeBase(URPGAttributeSet::GetMaxHealthAttribute(), 100);
			ASC->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(), 100);
			C->GetCharacterMovement()->bRunPhysicsWithNoController = true;
			C->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
			FRPGAirborneTestAccess::Quiet(*C->GetCombatControlComponent()); return C;
		}
		void Tick(float Seconds)
		{
			for (int i = 0; i < FMath::CeilToInt(Seconds * 60); ++i)
			{
				Frame = FMath::Max(Frame, GFrameCounter) + 1;
				TGuardValue<uint64> FrameGuard(GFrameCounter, Frame);
				World->Tick(LEVELTICK_All, 1.f / 60);
			}
		}
	};
	FRPGKnockbackSettings Settings()
	{
		FRPGKnockbackSettings S; S.Distance = 240; S.Height = 180; S.DownDuration = .5f; S.GetUpDuration = .6f; return S;
	}
	bool Apply(ARPGBaseCharacter* S, ARPGBaseCharacter* T, FRPGKnockbackSettings Config = Settings(), FGuid Id = FGuid::NewGuid())
	{
		const FRPGApprovedHitContext Hit(S, T);
		return T->GetCombatControlComponent()->ApplyApprovedDamageKnockback(S,
			FHitResult(T, nullptr, T->GetActorLocation(), Hit.AwayDirection), Id, Config, Hit.AwayDirection);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGAirborneDataTest, "ProjectRPG.Combat.Airborne.DataAndValidation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGAirborneDataTest::RunTest(const FString&)
{
	using namespace RPGAirborneTests;
	auto S = Settings(); FGameplayAbilitySpec Spec;
	TestTrue(TEXT("Freeze shared displacement"), RPGKnockback::SetGrantedSettings(Spec, S));
	S.Height = 0; TestEqual(TEXT("Frozen height"), RPGKnockback::GetGrantedSettings(&Spec).Height, 180.f);
	Spec.SetByCallerTagMagnitudes.Remove(FGameplayTag::RequestGameplayTag(TEXT("Ability.Data.Control.DownSeconds")));
	TestFalse(TEXT("Partial airborne metadata is disabled"), RPGKnockback::GetGrantedSettings(&Spec).IsEnabled());
	S = Settings(); S.Distance = 0; TestTrue(TEXT("Vertical only is supported"), S.IsEnabled());
	S.Height = 49; TestFalse(TEXT("Reject tiny unusable arc"), S.IsValid());
	S.Height = std::numeric_limits<float>::quiet_NaN(); TestFalse(TEXT("Reject nonfinite height"), S.IsValid());
	S = Settings(); S.GetUpDuration = 4; TestFalse(TEXT("Recovery is bounded"), S.IsValid());
	auto* NPC = NewObject<UDataAsset_NPCStartUpData>(); NPC->AttackKnockbacks.Add(URPGNPCGameplayAbility::StaticClass(), Settings());
	TestTrue(TEXT("NPC reuses existing GA grant"), NPC->BuildAttackAbilitySpec(URPGNPCGameplayAbility::StaticClass(), 1, NPC, Spec));
	TestEqual(TEXT("NPC frozen airborne height"), RPGKnockback::GetGrantedSettings(&Spec).Height, 180.f);
	auto* Def = NewObject<URPGSkillDefinition>(); Def->Knockback = Settings(); FRPGSkillRuntimeSpec Runtime;
	Def->BuildRuntimeSpec(nullptr, FRPGSkillSaveData(), Runtime); Def->Knockback.Height = 0;
	TestEqual(TEXT("Skill freezes same configuration"), Runtime.Knockback.Height, 180.f); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGAirborneLandingTest, "ProjectRPG.Combat.Airborne.PhysicalLandingAndRecovery", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGAirborneLandingTest::RunTest(const FString&)
{
	using namespace RPGAirborneTests; FScope W; if (!W.Init()) return false;
	auto* S = W.Spawn(FVector(-1000, 0, 300)); auto* T = W.Spawn(FVector(0, 0, 300)); W.Tick(1.f);
	auto* C = T->GetCombatControlComponent(); auto* M = T->GetCharacterMovement(); const float Ground = T->GetActorLocation().Z;
	TestTrue(TEXT("Fixture on actual floor"), M->IsMovingOnGround());
	TestTrue(TEXT("Start arc"), Apply(S, T));
	const auto Root = FRPGAirborneTestAccess::Root(*C); bool bRose = false, bDown = false, bUp = false;
	for (int i = 0; i < 240; ++i)
	{
		W.Tick(1.f / 60); const auto State = C->GetAirborneState();
		bRose |= T->GetActorLocation().Z > Ground + 100;
		if (State.Phase == ERPGAirbornePhase::Downed) { bDown = true; TestTrue(TEXT("Down only on physical ground"), M->IsMovingOnGround()); }
		if (State.Phase == ERPGAirbornePhase::GettingUp) bUp = true;
		if (State.IsActive()) { TestTrue(TEXT("No input gap across phases"), RPGCombatControl::IsActionBlocked(T)); TestNotNull(TEXT("One continuous root GE"), T->GetRPGAbilitySystemComponent()->GetActiveGameplayEffect(Root)); }
	}
	TestTrue(TEXT("Character actually rose"), bRose); TestTrue(TEXT("Observed real landing/down"), bDown); TestTrue(TEXT("Observed automatic get-up"), bUp);
	TestEqual(TEXT("Natural completion"), C->GetAirborneState().EndReason, ERPGAirborneEndReason::Completed);
	TestFalse(TEXT("Input released"), RPGCombatControl::IsActionBlocked(T)); TestTrue(TEXT("CMC owns final walking mode"), M->IsMovingOnGround()); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGAirborneOwnershipTest, "ProjectRPG.Combat.Airborne.ImmunityReplacementAndCleanse", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGAirborneOwnershipTest::RunTest(const FString&)
{
	using namespace RPGAirborneTests; FScope W; if (!W.Init()) return false;
	auto* S = W.Spawn(FVector(-1000, 0, 300)); auto* T = W.Spawn(FVector(0, 0, 300)); W.Tick(1);
	auto* C = T->GetCombatControlComponent(); auto* ASC = T->GetRPGAbilitySystemComponent(); const auto Rejected = FGuid::NewGuid();
	C->SetDefaultAirborneImmunity(true); TestFalse(TEXT("Default immunity"), Apply(S, T)); C->SetDefaultAirborneImmunity(false);
	ASC->AddLooseGameplayTag(RPGAirborne::ImmunityTag()); TestFalse(TEXT("Airborne immunity"), Apply(S, T, Settings(), Rejected));
	ASC->RemoveLooseGameplayTag(RPGAirborne::ImmunityTag()); TestFalse(TEXT("Immune receipt cannot replay"), Apply(S, T, Settings(), Rejected));
	TestTrue(TEXT("Accepted new receipt"), Apply(S, T)); const auto Old = C->GetAirborneState().ExecutionId;
	const auto Root = FRPGAirborneTestAccess::Root(*C);
	TestTrue(TEXT("New airborne replaces"), Apply(S, T)); TestNull(TEXT("Old root gone"), ASC->GetActiveGameplayEffect(Root));
	TestFalse(TEXT("Old cancellation cannot end replacement"), C->CancelAirborne(Old));
	FRPGKnockbackSettings Horizontal; Horizontal.Distance = 100;
	TestFalse(TEXT("Horizontal hit cannot replace airborne"), Apply(S, T, Horizontal));
	const auto Stun = C->ApplyApprovedDamageStun(S, FHitResult(T, nullptr, FVector::ZeroVector, FVector::UpVector), FGuid::NewGuid(), 5);
	TestTrue(TEXT("Independent stun overlaps"), Stun.IsValid());
	ASC->RemoveActiveGameplayEffect(FRPGAirborneTestAccess::Phase(*C));
	TestFalse(TEXT("Phase cleanse ends whole owned chain"), C->GetAirborneState().IsActive());
	TestTrue(TEXT("Unrelated stun remains"), RPGCombatControl::IsStunned(T));
	TestTrue(TEXT("Does not force walking in midair"), T->GetCharacterMovement()->IsFalling());
	TestFalse(TEXT("No live retired old source"), C->HasAirborneMotion()); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGAirborneCollisionTest, "ProjectRPG.Combat.Airborne.CeilingWallAndNoLandingTimeout", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGAirborneCollisionTest::RunTest(const FString&)
{
	using namespace RPGAirborneTests;
	{
		FScope W; if (!W.Init()) return false; auto* S = W.Spawn(FVector(-1000, 0, 300)); auto* T = W.Spawn(FVector(0, 0, 300)); W.Tick(1);
		W.Box(FVector(100, 0, 150), FVector(10, 200, 150));
		TestTrue(TEXT("Start toward real wall"), Apply(S, T)); W.Tick(.3f);
		TestTrue(TEXT("Wall prevents tunnelling"), T->GetActorLocation().X < 90.f);
		TestFalse(TEXT("Wall retires motion"), T->GetCombatControlComponent()->HasAirborneMotion()); W.Tick(3);
		TestEqual(TEXT("Wall case physically lands and recovers"), T->GetCombatControlComponent()->GetAirborneState().EndReason, ERPGAirborneEndReason::Completed);
	}
	{
		FScope W; if (!W.Init()) return false; auto* S = W.Spawn(FVector(-1000, 0, 300)); auto* T = W.Spawn(FVector(0, 0, 300)); W.Tick(1);
		W.Box(FVector(0, 0, 250), FVector(500, 500, 10));
		TestTrue(TEXT("Start below real ceiling"), Apply(S, T)); W.Tick(.3f);
		TestTrue(TEXT("Ceiling prevents upward tunnelling"), T->GetActorLocation().Z < 200.f);
		TestFalse(TEXT("Ceiling retires arc"), T->GetCombatControlComponent()->HasAirborneMotion()); W.Tick(3);
		TestFalse(TEXT("Ceiling cannot leave stuck root control"), T->GetCombatControlComponent()->GetAirborneState().IsActive());
	}
	{
		FScope W; if (!W.Init(false)) return false; auto* S = W.Spawn(FVector(-1000, 0, 10000)); auto* T = W.Spawn(FVector(0, 0, 10000));
		TestTrue(TEXT("Start with no floor"), Apply(S, T)); bool bFakeDown = false;
		for (int i = 0; i < 240; ++i) { W.Tick(1.f / 60); bFakeDown |= T->GetCombatControlComponent()->GetAirborneState().Phase == ERPGAirbornePhase::Downed; }
		TestFalse(TEXT("Arc timeout never fabricates landing"), bFakeDown);
		TestEqual(TEXT("Bounded no-landing cleanup"), T->GetCombatControlComponent()->GetAirborneState().EndReason, ERPGAirborneEndReason::LandingTimeout);
		TestTrue(TEXT("Natural gravity continues"), T->GetCharacterMovement()->IsFalling());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGAirborneLifetimeTest, "ProjectRPG.Combat.Airborne.LifetimeForeignMotionAndSecurity", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGAirborneLifetimeTest::RunTest(const FString&)
{
	using namespace RPGAirborneTests; FScope W; if (!W.Init()) return false;
	auto* S = W.Spawn(FVector(-1000, 0, 300)); auto* T = W.Spawn(FVector(0, 0, 300)); W.Tick(1);
	auto* C = T->GetCombatControlComponent(); auto* ASC = T->GetRPGAbilitySystemComponent(); auto* M = T->GetCharacterMovement();
	auto* Security = NewObject<URPGSecurityValidationComponent>(T); T->AddInstanceComponent(Security); Security->RegisterComponent();
	const auto ForeignLease = Security->BeginMovementAuthorization(5, 1000, TEXT("Test.Foreign"));
	TestFalse(TEXT("No overwrite of another system's security lease"), Apply(S, T));
	Security->EndMovementAuthorization(ForeignLease, true);
	TestFalse(TEXT("Foreign issuer can revoke its lease"), Security->IsMovementAuthorizationActive());
	TestTrue(TEXT("Begin with own lease"), Apply(S, T)); TestTrue(TEXT("Bounded movement authorized"), Security->IsMovementAuthorizationActive());
	W.Tick(.1f); auto Foreign = MakeShared<FRootMotionSource_ConstantForce>(); Foreign->InstanceName = TEXT("ForeignAirborneTest"); Foreign->Duration = 4;
	M->ApplyRootMotionSource(Foreign); W.Tick(.1f);
	TestEqual(TEXT("Foreign movement retires only own execution"), C->GetAirborneState().EndReason, ERPGAirborneEndReason::ForeignMovement);
	TestFalse(TEXT("Foreign RMS preserved"), Foreign->Status.HasFlag(ERootMotionSourceStatusFlags::MarkedForRemoval));
	TestFalse(TEXT("Own security lease revoked"), Security->IsMovementAuthorizationActive()); M->RemoveRootMotionSource(Foreign->InstanceName); W.Tick(.1f);
	TestTrue(TEXT("New life test starts"), Apply(S, T)); T->AdvanceLife(); C->ResetForAvatarReuse();
	TestFalse(TEXT("Reuse clears whole chain"), C->GetAirborneState().IsActive()); TestFalse(TEXT("Reuse clears action gate"), RPGCombatControl::IsActionBlocked(T));
	TestTrue(TEXT("Fresh execution"), Apply(S, T)); ASC->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(), 0);
	TestFalse(TEXT("Death immediately clears motion and tags"), C->GetAirborneState().IsActive());
	TestFalse(TEXT("No RMS after death"), C->HasAirborneMotion()); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGAirborneReentryTest, "ProjectRPG.Combat.Airborne.ReentrantResetInhibitionAndFloorLoss", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGAirborneReentryTest::RunTest(const FString&)
{
	using namespace RPGAirborneTests; FScope W; if (!W.Init()) return false;
	auto* S = W.Spawn(FVector(-1000, 0, 300)); auto* T = W.Spawn(FVector(0, 0, 300)); W.Tick(1);
	auto* C = T->GetCombatControlComponent(); auto* ASC = T->GetRPGAbilitySystemComponent();
	const auto Ability = ASC->GiveAbility(FGameplayAbilitySpec(URPGStunTestCombatAbility::StaticClass(), 1)); ASC->TryActivateAbility(Ability);
	auto* Instance = Cast<URPGStunTestCombatAbility>(ASC->FindAbilitySpecFromHandle(Ability)->GetPrimaryInstance());
	Instance->SetInterruptible(false); TestFalse(TEXT("Uncancellable action rejects airborne"), Apply(S, T)); Instance->SetInterruptible(true);
	ASC->CancelAbilityHandle(Ability);
	const auto ResetDelegate = ASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda([C](UAbilitySystemComponent*, const FGameplayEffectSpec& Spec, FActiveGameplayEffectHandle)
	{
		if (Spec.Def->GetClass() == URPGKnockdownSequenceEffect::StaticClass()) C->ResetForAvatarReuse();
	});
	TestFalse(TEXT("Reset during root application rejects stale hit"), Apply(S, T)); ASC->OnGameplayEffectAppliedDelegateToSelf.Remove(ResetDelegate);
	TestFalse(TEXT("No pending root leaked by reentry"), ASC->HasMatchingGameplayTag(RPGAirborne::SequenceTag()));
	const auto InhibitDelegate = ASC->OnActiveGameplayEffectAddedDelegateToSelf.AddLambda([](UAbilitySystemComponent* TargetASC, const FGameplayEffectSpec& Spec, FActiveGameplayEffectHandle Handle)
	{
		if (Spec.Def->GetClass() == URPGKnockdownSequenceEffect::StaticClass()) TargetASC->SetActiveGameplayEffectInhibit(MoveTemp(Handle), true, false);
	});
	TestFalse(TEXT("Already inhibited GE cannot start motion"), Apply(S, T)); ASC->OnActiveGameplayEffectAddedDelegateToSelf.Remove(InhibitDelegate);
	TestFalse(TEXT("Inhibited application leaves no owned motion"), C->HasAirborneMotion());
	TestTrue(TEXT("Clean execution after reset"), Apply(S, T));
	ASC->SetActiveGameplayEffectInhibit(FRPGAirborneTestAccess::Phase(*C), true, false);
	TestFalse(TEXT("Phase inhibition retires chain"), C->GetAirborneState().IsActive()); W.Tick(.1f);
	TestTrue(TEXT("Start floor-loss case"), Apply(S, T));
	for (int i = 0; i < 180 && C->GetAirborneState().Phase != ERPGAirbornePhase::Downed; ++i) W.Tick(1.f / 60);
	TestEqual(TEXT("Real down entered"), C->GetAirborneState().Phase, ERPGAirbornePhase::Downed);
	W.Floor->SetCollisionEnabled(ECollisionEnabled::NoCollision); T->GetCharacterMovement()->SetMovementMode(MOVE_Falling); W.Tick(.1f);
	TestFalse(TEXT("Lost floor cancels grounded recovery"), C->GetAirborneState().IsActive());
	TestTrue(TEXT("Cleanup never restores walking"), T->GetCharacterMovement()->IsFalling()); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGAirborneSlopeTest, "ProjectRPG.Combat.Airborne.WalkableSlopeLanding", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGAirborneSlopeTest::RunTest(const FString&)
{
	using namespace RPGAirborneTests;
	FScope W; if (!W.Init()) return false;
	auto* Ramp = W.Box(FVector(250, 0, 20), FVector(250, 400, 20));
	Ramp->SetWorldRotation(FRotator(20, 0, 0));
	auto* Source = W.Spawn(FVector(-1000, 0, 300));
	auto* Target = W.Spawn(FVector(-250, 0, 300)); W.Tick(1);
	auto* Control = Target->GetCombatControlComponent(); auto* Movement = Target->GetCharacterMovement();
	const float GroundZ = Target->GetActorLocation().Z;
	auto Config = Settings(); Config.Distance = 600; Config.Height = 200;
	TestTrue(TEXT("Launch toward a walkable slope"), Apply(Source, Target, Config));
	bool bLandedOnRamp = false, bGotUp = false; FVector LandedLocation = FVector::ZeroVector;
	for (int32 Frame = 0; Frame < 240; ++Frame)
	{
		Target->AddMovementInput(FVector::ForwardVector); W.Tick(1.f / 60);
		const auto State = Control->GetAirborneState();
		if (State.Phase == ERPGAirbornePhase::Downed && !bLandedOnRamp)
		{
			bLandedOnRamp = true; LandedLocation = Target->GetActorLocation();
			TestTrue(TEXT("CMC found the slope, rather than the flat floor"), Movement->CurrentFloor.HitResult.GetComponent() == Ramp);
			TestTrue(TEXT("Slope is walkable"), Movement->IsMovingOnGround() && Movement->IsWalkable(Movement->CurrentFloor.HitResult));
			TestTrue(TEXT("Landing normal is sloped"), Movement->CurrentFloor.HitResult.ImpactNormal.Z < .99f);
			TestTrue(TEXT("Landing is above the initial floor"), LandedLocation.Z > GroundZ + 15);
			TestFalse(TEXT("Landing retires the displacement"), Control->HasAirborneMotion());
		}
		if (State.Phase == ERPGAirbornePhase::GettingUp) bGotUp = true;
		if (bLandedOnRamp && State.IsActive())
			TestTrue(TEXT("Grounded input remains blocked on the slope"), FVector::Dist2D(LandedLocation, Target->GetActorLocation()) < 5);
		if (bLandedOnRamp && !State.IsActive()) break;
	}
	TestTrue(TEXT("Observed slope landing"), bLandedOnRamp); TestTrue(TEXT("Observed slope get-up"), bGotUp);
	TestEqual(TEXT("Slope recovery completes"), Control->GetAirborneState().EndReason, ERPGAirborneEndReason::Completed);
	TestFalse(TEXT("Slope recovery releases action gate"), RPGCombatControl::IsActionBlocked(Target));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGAirborneLedgeTest, "ProjectRPG.Combat.Airborne.LedgeWaitsForLowerFloor", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGAirborneLedgeTest::RunTest(const FString&)
{
	using namespace RPGAirborneTests;
	FScope W; if (!W.Init(false)) return false;
	W.Box(FVector(-250, 0, -25), FVector(300, 500, 25));
	auto* Lower = W.Box(FVector(600, 0, -350), FVector(550, 500, 50));
	auto* Source = W.Spawn(FVector(-500, 0, 300)); auto* Target = W.Spawn(FVector(-300, 0, 300)); W.Tick(1);
	auto* Control = Target->GetCombatControlComponent(); auto* Movement = Target->GetCharacterMovement();
	auto* Security = NewObject<URPGSecurityValidationComponent>(Target); Target->AddInstanceComponent(Security); Security->RegisterComponent();
	auto* Controller = W.World->SpawnActor<APlayerController>(); Controller->SetAsLocalPlayerController(); Controller->Possess(Target);
	TestTrue(TEXT("Fixture runs authority physics without remote move RPCs"), Target->IsLocallyControlled());
	W.Tick(3.f); // Exercise the authority movement monitor after its default spawn grace.
	TestEqual(TEXT("Stable starting position has no security violation"), Security->GetTotalViolationCount(), 0);
	auto Config = Settings(); Config.Distance = 700;
	TestTrue(TEXT("Launch across the platform edge"), Apply(Source, Target, Config));
	const auto Initial = Control->GetAirborneState(); W.Tick(Initial.ArcDuration + .05f);
	AddInfo(FString::Printf(TEXT("After arc phase=%d reason=%d location=%s velocity=%s source=%s"),
		int32(Control->GetAirborneState().Phase), int32(Control->GetAirborneState().EndReason), *Target->GetActorLocation().ToCompactString(),
		*Movement->Velocity.ToCompactString(), *Source->GetActorLocation().ToCompactString()));
	TestEqual(TEXT("Arc completion does not fabricate a landing"), Control->GetAirborneState().Phase, ERPGAirbornePhase::Airborne);
	TestFalse(TEXT("RMS is finished above the lower floor"), Control->HasAirborneMotion());
	TestTrue(TEXT("Native falling physics continues"), Movement->IsFalling());
	TestTrue(TEXT("Authorization lasts until actual landing"), Security->IsMovementAuthorizationActive());
	bool bLanded = false;
	for (int32 Frame = 0; Frame < 180; ++Frame)
	{
		W.Tick(1.f / 60);
		if (Frame % 30 == 0) AddInfo(FString::Printf(TEXT("Fall frame=%d phase=%d reason=%d location=%s velocity=%s"), Frame,
			int32(Control->GetAirborneState().Phase), int32(Control->GetAirborneState().EndReason), *Target->GetActorLocation().ToCompactString(), *Movement->Velocity.ToCompactString()));
		if (Control->GetAirborneState().Phase == ERPGAirbornePhase::Downed)
		{
			bLanded = true;
			TestTrue(TEXT("Landed on the physical lower floor"), Movement->CurrentFloor.HitResult.GetComponent() == Lower);
			TestTrue(TEXT("Down starts after the arc, at actual landing"), Control->GetAirborneState().PhaseStartServerTime > Initial.StartServerTime + Initial.ArcDuration);
			TestFalse(TEXT("Landing releases authorization"), Security->IsMovementAuthorizationActive());
			break;
		}
	}
	TestTrue(TEXT("Observed lower-floor landing"), bLanded); W.Tick(1.5f);
	TestEqual(TEXT("Lower-floor recovery completes"), Control->GetAirborneState().EndReason, ERPGAirborneEndReason::Completed);
	TestFalse(TEXT("No remaining action constraint"), RPGCombatControl::IsActionBlocked(Target));
	TestEqual(TEXT("Legitimate lower-floor fall preserves security"), Security->GetTotalViolationCount(), 0);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGAirborneClockTest, "ProjectRPG.Combat.Airborne.FutureClockAndOwnedPoseResync", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGAirborneClockTest::RunTest(const FString&)
{
	using namespace RPGAirborneTests;
	FScope W; if (!W.Init()) return false;
	auto* Source=W.Spawn(FVector(-500,0,300)); auto* Target=W.Spawn(FVector(0,0,300)); W.Tick(1);
	auto* Class=LoadClass<ARPGBaseCharacter>(nullptr,TEXT("/Game/Blueprints/Character/Player/BP_Player.BP_Player_C"));
	auto* CDO=Class?Class->GetDefaultObject<ARPGBaseCharacter>():nullptr;
	if (!TestNotNull(TEXT("Authored player pose fixture"),CDO)) return false;
	Target->GetMesh()->SetSkeletalMesh(CDO->GetMesh()->GetSkeletalMeshAsset());
	Target->GetMesh()->SetAnimInstanceClass(UAnimInstance::StaticClass());
	auto* Control=Target->GetCombatControlComponent();
	FRPGAirborneTestAccess::Presentation(*Control,*CDO->GetCombatControlComponent());
	auto* Anim=Target->GetMesh()->GetAnimInstance();
	if (!TestNotNull(TEXT("Pose fixture animation instance"),Anim) || !Apply(Source,Target)) return false;
	auto State=Control->GetAirborneState(); State.ExecutionId+=100;
	State.StartServerTime=W.World->GetTimeSeconds()+1.; State.PhaseStartServerTime=State.StartServerTime;
	State.PhaseEndServerTime=State.StartServerTime+State.MaximumFlightDuration;
	FRPGAirborneTestAccess::Snapshot(*Control,State);
	TestFalse(TEXT("Future snapshot does not start displacement at zero elapsed"),Control->HasAirborneMotion());
	TestNull(TEXT("Future phase waits for the initial server clock"),FRPGAirborneTestAccess::Pose(*Control));
	// Advance only the isolated clock, as an initial GameState time correction would.
	W.World->TimeSeconds=State.StartServerTime+.25;
	FRPGAirborneTestAccess::Refresh(*Control);
	const auto Motion=Target->GetCharacterMovement()->GetRootMotionSource(FRPGAirborneTestAccess::Motion(*Control));
	if (!TestTrue(TEXT("The unchanged snapshot resumes at the corrected elapsed time"),Motion.IsValid())) return false;
	TestTrue(TEXT("Displacement uses elapsed time instead of replaying its beginning"),FMath::Abs(Motion->GetTime()-.25f)<.01f);
	const FName ReceivedMotion=FRPGAirborneTestAccess::Motion(*Control);
	FRPGAirborneTestAccess::ForgetMotionTracking(*Control);
	FRPGAirborneTestAccess::Refresh(*Control);
	TestEqual(TEXT("A source received before its snapshot is adopted for cleanup"),FRPGAirborneTestAccess::Motion(*Control),ReceivedMotion);
	TestTrue(TEXT("Adoption preserves the existing source instance"),Target->GetCharacterMovement()->GetRootMotionSource(ReceivedMotion)==Motion);
	TestEqual(TEXT("Clock correction preserves the execution id"),Control->GetAirborneState().ExecutionId,State.ExecutionId);
	TestEqual(TEXT("Clock correction preserves the server start time"),Control->GetAirborneState().StartServerTime,State.StartServerTime);
	State.Phase=ERPGAirbornePhase::Downed; ++State.PhaseRevision; State.bMotionActive=false;
	State.PhaseStartServerTime=W.World->GetTimeSeconds(); State.PhaseEndServerTime=State.PhaseStartServerTime+4.;
	FRPGAirborneTestAccess::Snapshot(*Control,State);
	auto* Montage=FRPGAirborneTestAccess::Pose(*Control);
	if (!TestNotNull(TEXT("Down pose is playing"),Montage)) return false;
	const int32 Instance=FRPGAirborneTestAccess::PoseId(*Control);
	const float Original=Anim->Montage_GetPosition(Montage);
	W.World->TimeSeconds+=.4;
	FRPGAirborneTestAccess::Refresh(*Control);
	TestEqual(TEXT("Resync keeps the original montage instance"),FRPGAirborneTestAccess::PoseId(*Control),Instance);
	TestTrue(TEXT("An already playing pose follows a corrected server clock"),FMath::Abs(Anim->Montage_GetPosition(Montage)-Original-.4f)<.02f);
	auto* OldAnim=Anim;
	Target->GetMesh()->SetAnimInstanceClass(nullptr);
	Target->GetMesh()->SetAnimInstanceClass(UAnimInstance::StaticClass());
	Anim=Target->GetMesh()->GetAnimInstance();
	if (!TestTrue(TEXT("Initial mesh setup creates a different animation instance"),Anim && Anim!=OldAnim)) return false;
	FRPGAirborneTestAccess::Refresh(*Control);
	TestEqual(TEXT("The unchanged phase follows its replacement animation instance"),FRPGAirborneTestAccess::PoseAnim(*Control),Anim);
	if (!TestTrue(TEXT("The replacement restores the current phase pose"),Anim->Montage_IsPlaying(Montage))) return false;
	TestTrue(TEXT("Replacement resumes elapsed phase time instead of replaying the start"),FMath::Abs(Anim->Montage_GetPosition(Montage)-Original-.4f)<.02f);
	const int32 ReplacementInstance=FRPGAirborneTestAccess::PoseId(*Control);
	Anim->Montage_Stop(0,Montage);
	Anim->Montage_Play(Montage,1,EMontagePlayReturnType::MontageLength,.05f,true);
	const auto* Foreign=Anim->GetActiveInstanceForMontage(Montage);
	if (!TestTrue(TEXT("Another system has a distinct playback instance"),Foreign && Foreign->GetInstanceID()!=ReplacementInstance)) return false;
	const float ForeignPosition=Anim->Montage_GetPosition(Montage);
	FRPGAirborneTestAccess::Refresh(*Control);
	TestTrue(TEXT("Clock correction preserves another system's play of the same asset"),FMath::Abs(Anim->Montage_GetPosition(Montage)-ForeignPosition)<.001f);
	FRPGAirborneTestAccess::End(*Control);
	TestTrue(TEXT("Sequence cleanup preserves the foreign montage"),Anim->Montage_IsPlaying(Montage));
	Anim->StopAllMontages(0);
	// The real common NPC Blueprint starts its entry montage from BeginPlay.
	// Initial control replication must win even before ASC tags arrive.
	auto* NPCClass=LoadClass<ARPGNonPlayerCharacter>(nullptr,TEXT("/Game/Blueprints/Character/NPC/Gruntling/Guardian/BP_Gruntling_Guardian.BP_Gruntling_Guardian_C"));
	if (!TestNotNull(TEXT("Authored NPC entry fixture"),NPCClass)) return false;
	for (const auto Phase : {ERPGAirbornePhase::Airborne,ERPGAirbornePhase::Downed,ERPGAirbornePhase::GettingUp,ERPGAirbornePhase::None})
	{
		const FTransform Transform(FVector(2000,0,300));
		auto* NPC=W.World->SpawnActorDeferred<ARPGNonPlayerCharacter>(NPCClass,Transform,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!NPC) return false;
		NPC->AutoPossessAI=EAutoPossessAI::Disabled;
		auto* NPCASC=NPC->GetRPGAbilitySystemComponent(); NPCASC->InitAbilityActorInfo(NPC,NPC);
		if (!NPCASC->GetSet<URPGAttributeSet>()) NPCASC->AddAttributeSetSubobject(NPC->GetRPGAttributeSet());
		NPCASC->SetNumericAttributeBase(URPGAttributeSet::GetMaxHealthAttribute(),100);
		NPCASC->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(),100);
		NPC->SetRole(ROLE_SimulatedProxy);
		NPC->GetCombatControlComponent()->InitializeWithAbilitySystem(*NPCASC);
		auto Entry=State; Entry.Phase=Phase; Entry.PhaseRevision=1; Entry.bMotionActive=false;
		Entry.StartServerTime=W.World->GetTimeSeconds(); Entry.PhaseStartServerTime=Entry.StartServerTime; Entry.PhaseEndServerTime=Entry.StartServerTime+4.;
		FRPGAirborneTestAccess::Snapshot(*NPC->GetCombatControlComponent(),Entry);
		NPC->FinishSpawning(Transform);
		TestEqual(TEXT("Deferred initial replication retains its phase through BeginPlay"),NPC->GetCombatControlComponent()->GetAirborneState().Phase,Phase);
		FRPGAirborneTestAccess::Refresh(*NPC->GetCombatControlComponent());
		auto* NPCAnim=NPC->GetMesh()->GetAnimInstance();
		auto* Active=NPCAnim?NPCAnim->GetCurrentActiveMontage():nullptr;
		if (!TestNotNull(TEXT("NPC has an active montage after BeginPlay"),Active)) return false;
		if (Phase==ERPGAirbornePhase::None)
			TestEqual(TEXT("An ordinary initial NPC retains its entry presentation"),Active->GetFName(),FName(TEXT("AM_Guardian_Spawn")));
		else
		{
			TestTrue(TEXT("An initial active phase preserves the shared cosmetic instance"),Active==FRPGAirborneTestAccess::Pose(*NPC->GetCombatControlComponent()));
			TestFalse(TEXT("Late entry does not introduce animation root motion"),NPC->GetMesh()->IsPlayingRootMotion());
			TestTrue(TEXT("Explicit foreign montage calls after BeginPlay are preserved"),NPC->PlayAnimMontage(Active)>0.f);
		}
		NPCAnim->StopAllMontages(0);
	}
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGAirborneOwnerClockTest, "ProjectRPG.Combat.Airborne.OwnerMoveClockBootstrap", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGAirborneOwnerClockTest::RunTest(const FString&)
{
	using namespace RPGAirborneTests;
	FScope W; if (!W.Init()) return false;
	auto* Source=W.Spawn(FVector(-500,0,300)); auto* Target=W.Spawn(FVector(0,0,300)); W.Tick(1);
	if (!Apply(Source,Target)) return false;
	auto* Control=Target->GetCombatControlComponent();
	auto* Movement=Target->GetCharacterMovement();
	auto State=Control->GetAirborneState(); State.ExecutionId+=100;
	// A position correction has already advanced the owner .4s into the arc,
	// while its initial GameState clock still places this snapshot in the future.
	State.StartServerTime=W.World->GetTimeSeconds()+1.;
	State.PhaseStartServerTime=State.StartServerTime; State.PhaseEndServerTime=State.StartServerTime+4.;
	State.MovementStartClientTime=9.f;
	Target->SetRole(ROLE_AutonomousProxy);
	Movement->GetPredictionData_Client_Character()->CurrentTimeStamp=9.4f;
	FRPGAirborneTestAccess::Snapshot(*Control,State);
	const auto Motion=Movement->GetRootMotionSource(FRPGAirborneTestAccess::Motion(*Control));
	if (!TestTrue(TEXT("Owner can bootstrap without waiting for a delayed GameState clock"),Motion.IsValid())) return false;
	TestTrue(TEXT("Owner continues the corrected move-clock arc rather than replaying its ascent"),FMath::IsNearlyEqual(Motion->GetTime(),.4f,.001f));
	TestTrue(TEXT("Owner and server use the same CMC source timestamp"),FMath::IsNearlyEqual(Motion->GetStartTime(),9.f));
	W.World->TimeSeconds=State.StartServerTime+State.ArcDuration+.1;
	TestTrue(TEXT("An ahead GameState clock does not expire a valid owner source"),Control->HasAirborneMotion());
	FRPGAirborneTestAccess::Refresh(*Control);
	TestEqual(TEXT("Clock changes preserve the existing owner source"),Movement->GetRootMotionSource(FRPGAirborneTestAccess::Motion(*Control)).Get(),Motion.Get());
	int32 QueuedOwnerSources=0;
	for (const auto& Queued : Movement->CurrentRootMotion.PendingAddRootMotionSources)
		if (Queued.IsValid() && Queued->InstanceName==Motion->InstanceName
			&& !Queued->Status.HasFlag(ERootMotionSourceStatusFlags::MarkedForRemoval)) ++QueuedOwnerSources;
	TestEqual(TEXT("No duplicate owner source is queued"),QueuedOwnerSources,1);
	// A reset/stale move timestamp cannot turn into an unbounded extrapolation.
	State.ExecutionId++; State.StartServerTime=W.World->GetTimeSeconds()-.2;
	State.PhaseStartServerTime=State.StartServerTime;
	Movement->GetPredictionData_Client_Character()->CurrentTimeStamp=.1f;
	FRPGAirborneTestAccess::Snapshot(*Control,State);
	const auto ResetMotion=Movement->GetRootMotionSource(FRPGAirborneTestAccess::Motion(*Control));
	if (!TestTrue(TEXT("A wrapped move clock uses the bounded world-clock fallback"),ResetMotion.IsValid())) return false;
	TestTrue(TEXT("Wrapped clock fallback preserves the observed age"),FMath::IsNearlyEqual(ResetMotion->GetTime(),.2f,.001f));
	Target->SetRole(ROLE_Authority);
	return true;
}

namespace RPGAirbornePlatformTests
{
	using namespace RPGAirborneTests;
	void Tick(FScope& Scope, float Delta)
	{
		Scope.Frame = FMath::Max(Scope.Frame, GFrameCounter) + 1;
		TGuardValue<uint64> FrameGuard(GFrameCounter, Scope.Frame);
		Scope.World->Tick(LEVELTICK_All, Delta);
	}

	UInterpToMovementComponent* MovePlatform(FScope& Scope, float Speed)
	{
		Scope.Floor = Scope.Box(FVector(0, 0, -50), FVector(5000, 1000, 50));
		Scope.Floor->SetMobility(EComponentMobility::Movable);
		Scope.Floor->SetCollisionObjectType(ECC_WorldDynamic);
		auto* Mover = NewObject<UInterpToMovementComponent>(Scope.Floor->GetOwner());
		Scope.Floor->GetOwner()->AddInstanceComponent(Mover);
		Mover->Duration = 20.f;
		Mover->BehaviourType = EInterpToBehaviourType::OneShot;
		Mover->bSweep = false;
		Mover->SetUpdatedComponent(Scope.Floor);
		Mover->AddControlPointPosition(FVector::ZeroVector);
		Mover->AddControlPointPosition(FVector(Speed * Mover->Duration, 0, 0));
		Mover->RegisterComponent();
		Mover->FinaliseControlPoints();
		return Mover;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGAirbornePlatformCarryTest, "ProjectRPG.Combat.AirbornePlatform.CarryThroughRecovery", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGAirbornePlatformCarryTest::RunTest(const FString&)
{
	using namespace RPGAirbornePlatformTests;
	for (const float Delta : { 1.f / 60.f, 1.f / 15.f })
	{
		FScope W; if (!W.Init(false)) return false;
		auto* Mover = MovePlatform(W, 600.f);
		auto* Source = W.Spawn(FVector(-1000, 0, 300));
		auto* Target = W.Spawn(FVector(0, 0, 300));
		Target->GetCharacterMovement()->AddTickPrerequisiteComponent(Mover);
		for (int32 Frame = 0; Frame < FMath::RoundToInt(1.f / Delta); ++Frame) Tick(W, Delta);
		if (!TestTrue(TEXT("CMC is actually based on the moving platform"), Target->GetMovementBaseObject() == W.Floor)) return false;
		auto* Control = Target->GetCombatControlComponent();
		if (!TestTrue(TEXT("Launch from an engine-driven moving platform"), Apply(Source, Target))) return false;
		bool bLanded = false, bGotUp = false, bRelativeBaselineReady = false;
		int32 LandingFrame = INDEX_NONE;
		FVector RelativeLanding = FVector::ZeroVector;
		float MaximumRelativeDrift = 0.f;
		const FVector PlatformStart = W.Floor->GetComponentLocation();
		for (int32 Frame = 0; Frame < FMath::CeilToInt(5.f / Delta); ++Frame)
		{
			Target->AddMovementInput(FVector::ForwardVector);
			Tick(W, Delta);
			const auto State = Control->GetAirborneState();
			if (State.Phase == ERPGAirbornePhase::Downed && !bLanded)
			{
				bLanded = true;
				LandingFrame = Frame;
				RelativeLanding = W.Floor->GetComponentTransform().InverseTransformPosition(Target->GetActorLocation());
				TestTrue(TEXT("Landing selects the actual moving base"), Target->GetMovementBaseObject() == W.Floor);
				TestFalse(TEXT("Landing retires airborne root motion"), Control->HasAirborneMotion());
			}
			if (State.Phase == ERPGAirbornePhase::GettingUp) bGotUp = true;
			if (bLanded && State.IsActive())
			{
				const FVector RelativeNow = W.Floor->GetComponentTransform().InverseTransformPosition(Target->GetActorLocation());
				// Landed is broadcast before CMC finishes establishing its walking
				// base. Anchor after the first complete grounded update, then verify
				// that continuous input never produces sustained relative movement.
				if (!bRelativeBaselineReady && Frame > LandingFrame)
				{
					RelativeLanding = RelativeNow; bRelativeBaselineReady = true;
				}
				if (bRelativeBaselineReady)
					MaximumRelativeDrift = FMath::Max(MaximumRelativeDrift, float(FVector::Dist2D(RelativeNow, RelativeLanding)));
				TestTrue(TEXT("Voluntary input stays blocked while the platform carries the actor"), RPGCombatControl::IsActionBlocked(Target));
			}
			if (bLanded && !State.IsActive()) break;
		}
		AddInfo(FString::Printf(TEXT("Hz=%.0f landed=%d getup=%d reason=%d platformTravel=%.2f relativeDrift=%.3f"),
			1.f / Delta, bLanded, bGotUp, int32(Control->GetAirborneState().EndReason),
			float(FVector::Dist2D(PlatformStart, W.Floor->GetComponentLocation())), MaximumRelativeDrift));
		TestTrue(TEXT("Observed physical platform landing"), bLanded);
		TestTrue(TEXT("Observed a fully established moving-base sample"), bRelativeBaselineReady);
		TestTrue(TEXT("Moving platform does not interrupt get-up"), bGotUp);
		TestEqual(TEXT("Recovery completes on the moving platform"), Control->GetAirborneState().EndReason, ERPGAirborneEndReason::Completed);
		TestTrue(TEXT("Input does not move relative to the platform during control"), MaximumRelativeDrift < 5.f);
		TestTrue(TEXT("Recovery keeps native movement based on the platform"), Target->GetMovementBaseObject() == W.Floor);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGAirbornePlatformLossTest, "ProjectRPG.Combat.AirbornePlatform.BaseLossKeepsFalling", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGAirbornePlatformLossTest::RunTest(const FString&)
{
	using namespace RPGAirbornePlatformTests;
	FScope W; if (!W.Init(false)) return false;
	auto* Mover = MovePlatform(W, 120.f);
	auto* Source = W.Spawn(FVector(-1000, 0, 300)); auto* Target = W.Spawn(FVector(0, 0, 300));
	Target->GetCharacterMovement()->AddTickPrerequisiteComponent(Mover);
	for (int32 Frame = 0; Frame < 60; ++Frame) Tick(W, 1.f / 60.f);
	auto* Control = Target->GetCombatControlComponent();
	if (!TestTrue(TEXT("Launch for moving-base removal"), Apply(Source, Target))) return false;
	for (int32 Frame = 0; Frame < 180 && Control->GetAirborneState().Phase != ERPGAirbornePhase::GettingUp; ++Frame) Tick(W, 1.f / 60.f);
	if (!TestEqual(TEXT("Moving-base recovery reached get-up"), Control->GetAirborneState().Phase, ERPGAirbornePhase::GettingUp)) return false;
	W.Floor->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	for (int32 Frame = 0; Frame < 15; ++Frame) Tick(W, 1.f / 60.f);
	TestFalse(TEXT("Losing the physical base ends the sequence"), Control->GetAirborneState().IsActive());
	TestEqual(TEXT("Base removal has an explicit unsupported-movement result"), Control->GetAirborneState().EndReason, ERPGAirborneEndReason::UnsupportedMovement);
	TestTrue(TEXT("Get-up never restores walking over empty space"), Target->GetCharacterMovement()->IsFalling());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGAirbornePlatformTeleportTest, "ProjectRPG.Combat.AirbornePlatform.ForeignTeleportStillEndsRecovery", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGAirbornePlatformTeleportTest::RunTest(const FString&)
{
	using namespace RPGAirbornePlatformTests;
	FScope W; if (!W.Init(false)) return false;
	auto* Mover = MovePlatform(W, 120.f);
	auto* Source = W.Spawn(FVector(-1000, 0, 300)); auto* Target = W.Spawn(FVector(0, 0, 300));
	Target->GetCharacterMovement()->AddTickPrerequisiteComponent(Mover);
	for (int32 Frame = 0; Frame < 60; ++Frame) Tick(W, 1.f / 60.f);
	auto* Control = Target->GetCombatControlComponent();
	if (!TestTrue(TEXT("Launch for foreign-motion boundary"), Apply(Source, Target))) return false;
	for (int32 Frame = 0; Frame < 180 && Control->GetAirborneState().Phase != ERPGAirbornePhase::Downed; ++Frame) Tick(W, 1.f / 60.f);
	if (!TestEqual(TEXT("Reached downed on the moving base"), Control->GetAirborneState().Phase, ERPGAirbornePhase::Downed)) return false;
	Target->SetActorLocation(Target->GetActorLocation() + FVector(0, 300, 0), false, nullptr, ETeleportType::TeleportPhysics);
	Tick(W, 1.f / 60.f);
	TestFalse(TEXT("A real foreign teleport still retires recovery"), Control->GetAirborneState().IsActive());
	TestEqual(TEXT("Moving-base support does not hide foreign displacement"), Control->GetAirborneState().EndReason, ERPGAirborneEndReason::ForeignMovement);
	return true;
}
namespace RPGPlatformPlayerTests
{
	using namespace RPGAirbornePlatformTests;
	URPGSecurityValidationComponent* Possess(FScope& Scope, ARPGStunTestCharacter* Target)
	{
		auto* Security = NewObject<URPGSecurityValidationComponent>(Target);
		Target->AddInstanceComponent(Security); Security->RegisterComponent();
		auto* Controller = Scope.World->SpawnActor<APlayerController>();
		Controller->SetAsLocalPlayerController();
		Controller->SetPlayerState(Scope.World->SpawnActor<APlayerState>());
		Controller->Possess(Target);
		return Security;
	}
	bool WaitForDown(FScope& Scope, URPGCombatControlComponent* Control, float Delta)
	{
		for (int32 Frame = 0; Frame < FMath::CeilToInt(3.f / Delta); ++Frame)
		{
			Tick(Scope, Delta);
			if (Control->GetAirborneState().Phase == ERPGAirbornePhase::Downed) return true;
			if (!Control->GetAirborneState().IsActive()) break;
		}
		return false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGPlatformPlayerCarryTest, "ProjectRPG.Combat.AirbornePlatformPlayer.CarryPreservesSecurity", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGPlatformPlayerCarryTest::RunTest(const FString&)
{
	using namespace RPGPlatformPlayerTests;
	for (const float Delta : {1.f / 60.f, 1.f / 15.f})
	{
		FScope W; if (!W.Init(false)) return false;
		auto* Mover = MovePlatform(W, 600.f);
		auto* Source = W.Spawn(FVector(-1000, 0, 300)); auto* Target = W.Spawn(FVector(0, 0, 300));
		Target->GetCharacterMovement()->AddTickPrerequisiteComponent(Mover);
		auto* Security = Possess(W, Target);
		for (int32 Frame = 0; Frame < FMath::CeilToInt(3.f / Delta); ++Frame) Tick(W, Delta);
		TestTrue(TEXT("Runs local authority player physics"), Target->IsPlayerControlled() && Target->IsLocallyControlled());
		TestEqual(TEXT("Unconstrained platform carry passes after default spawn grace"), Security->GetTotalViolationCount(), 0);
		auto Config = Settings(); Config.DownDuration = 1.f; Config.GetUpDuration = 1.f;
		if (!TestTrue(TEXT("Approve airborne with the real player security component"), Apply(Source, Target, Config))) return false;
		auto* Control = Target->GetCombatControlComponent(); bool bDown = false, bGetUp = false;
		for (int32 Frame = 0; Frame < FMath::CeilToInt(5.f / Delta); ++Frame)
		{
			Target->AddMovementInput(FVector::RightVector, 1.f, true);
			Tick(W, Delta);
			const auto State = Control->GetAirborneState();
			if (State.Phase == ERPGAirbornePhase::Downed) bDown = true;
			if (State.Phase == ERPGAirbornePhase::GettingUp) bGetUp = true;
			if (bDown) TestFalse(TEXT("Landing releases airborne authorization; carry needs no exemption"), Security->IsMovementAuthorizationActive());
			if (bDown && !State.IsActive()) break;
		}
		AddInfo(FString::Printf(TEXT("Hz=%.0f down=%d getup=%d reason=%d violations=%d risk=%.2f"), 1.f / Delta, bDown, bGetUp,
			int32(Control->GetAirborneState().EndReason), Security->GetTotalViolationCount(), Security->GetRiskScore()));
		TestTrue(TEXT("Observed real landing and get-up for the player"), bDown && bGetUp);
		TestEqual(TEXT("Player recovery completes on the platform"), Control->GetAirborneState().EndReason, ERPGAirborneEndReason::Completed);
		TestEqual(TEXT("Passive platform carry never becomes a speed violation"), Security->GetTotalViolationCount(), 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGPlatformPlayerTeleportTest, "ProjectRPG.Combat.AirbornePlatformPlayer.RejectsForeignTeleport", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGPlatformPlayerTeleportTest::RunTest(const FString&)
{
	using namespace RPGPlatformPlayerTests;
	FScope W; if (!W.Init(false)) return false;
	auto* Mover = MovePlatform(W, 600.f);
	auto* Source = W.Spawn(FVector(-1000, 0, 300)); auto* Target = W.Spawn(FVector(0, 0, 300));
	Target->GetCharacterMovement()->AddTickPrerequisiteComponent(Mover);
	auto* Security = Possess(W, Target);
	for (int32 Frame = 0; Frame < 180; ++Frame) Tick(W, 1.f / 60.f);
	auto Config = Settings(); Config.DownDuration = 2.f;
	if (!Apply(Source, Target, Config) || !WaitForDown(W, Target->GetCombatControlComponent(), 1.f / 60.f)) return false;
	TestFalse(TEXT("No active movement authorization after physical landing"), Security->IsMovementAuthorizationActive());
	for (int32 Frame = 0; Frame < 13; ++Frame) Tick(W, 1.f / 60.f); // Establish the post-landing sample after its legitimate baseline reset.
	const int32 Before = Security->GetTotalViolationCount();
	AddExpectedMessage(TEXT("Type=MovementDiscontinuity"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1, false);
	Target->SetActorLocation(Target->GetActorLocation() + FVector(2500, 0, 0), false, nullptr, ETeleportType::TeleportPhysics);
	for (int32 Frame = 0; Frame < 20; ++Frame) Tick(W, 1.f / 60.f);
	TestTrue(TEXT("Being based on a platform cannot hide an unapproved teleport"), Security->GetTotalViolationCount() > Before);
	TestEqual(TEXT("Combat also preserves its foreign-motion boundary"), Target->GetCombatControlComponent()->GetAirborneState().EndReason, ERPGAirborneEndReason::ForeignMovement);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGPlatformPlayerCounterMotionTest, "ProjectRPG.Combat.AirbornePlatformPlayer.RejectsRelativeCounterMotion", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGPlatformPlayerCounterMotionTest::RunTest(const FString&)
{
	using namespace RPGPlatformPlayerTests;
	FScope W; if (!W.Init(false)) return false;
	auto* Mover = MovePlatform(W, 600.f);
	auto* Source = W.Spawn(FVector(-1000, 0, 300)); auto* Target = W.Spawn(FVector(0, 0, 300));
	Target->GetCharacterMovement()->AddTickPrerequisiteComponent(Mover);
	auto* Security = Possess(W, Target);
	for (int32 Frame = 0; Frame < 180; ++Frame) Tick(W, 1.f / 60.f);
	auto Config = Settings(); Config.DownDuration = 2.f;
	if (!Apply(Source, Target, Config) || !WaitForDown(W, Target->GetCombatControlComponent(), 1.f / 60.f)) return false;
	for (int32 Frame = 0; Frame < 13; ++Frame) Tick(W, 1.f / 60.f);
	const int32 Before = Security->GetTotalViolationCount();
	AddExpectedMessage(TEXT("Type=MovementSpeed"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1, false);
	const FVector Start = Target->GetActorLocation();
	for (int32 Frame = 0; Frame < 45 && Security->GetTotalViolationCount() == Before; ++Frame)
	{
		// The platform moves +10cm/frame; the unapproved move cancels that in
		// world space while travelling 600cm/s relative to a zero-speed player.
		Target->SetActorLocation(Target->GetActorLocation() - FVector(10, 0, 0), false, nullptr, ETeleportType::TeleportPhysics);
		Tick(W, 1.f / 60.f);
	}
	AddInfo(FString::Printf(TEXT("Counter-motion worldTravel=%.3f violations=%d->%d"),
		float(FVector::Dist2D(Start, Target->GetActorLocation())), Before, Security->GetTotalViolationCount()));
	TestTrue(TEXT("Zero world displacement cannot hide movement relative to the platform"), Security->GetTotalViolationCount() > Before);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGPlatformBetweenMoveCarryTest, "ProjectRPG.Combat.AirbornePlatformPlayer.BetweenMoveCarryKeepsForeignChecks", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGPlatformBetweenMoveCarryTest::RunTest(const FString&)
{
	using namespace RPGPlatformPlayerTests;
	FScope W; if (!W.Init(false)) return false;
	auto* Mover = MovePlatform(W, 600.f);
	auto* Source = W.Spawn(FVector(-1000, 0, 300)); auto* Target = W.Spawn(FVector(0, 0, 300));
	Target->GetCharacterMovement()->AddTickPrerequisiteComponent(Mover);
	Possess(W, Target);
	for (int32 Frame = 0; Frame < 180; ++Frame) Tick(W, 1.f / 60.f);
	auto Config = Settings(); Config.DownDuration = 2.f;
	auto* Control = Target->GetCombatControlComponent();
	if (!Apply(Source, Target, Config) || !WaitForDown(W, Control, 1.f / 60.f)) return false;
	Tick(W, 1.f / 60.f);
	Mover->SetComponentTickEnabled(false);
	auto* Movement = Target->GetCharacterMovement(); const FVector Before = Target->GetActorLocation();
	// The remote authority branch performs these engine calls between RPC-driven
	// PerformMovement calls. Carry twice without broadcasting a movement update.
	for (int32 Step = 0; Step < 2; ++Step)
	{
		Mover->TickComponent(.05f, LEVELTICK_All, nullptr);
		Movement->MaybeUpdateBasedMovement(.05f); Movement->MaybeSaveBaseLocation();
	}
	TestTrue(TEXT("Actual between-move carry exceeds the unchanged 25cm discontinuity limit"), FVector::Dist2D(Before, Target->GetActorLocation()) > 50.f);
	Tick(W, 1.f / 60.f);
	TestEqual(TEXT("Measured intermediate carry preserves recovery"), Control->GetAirborneState().Phase, ERPGAirbornePhase::Downed);
	// The same carry history must not cover an unrelated 35cm teleport.
	Target->SetActorLocation(Target->GetActorLocation() + FVector(35, 0, 0), false, nullptr, ETeleportType::TeleportPhysics);
	Tick(W, 1.f / 60.f);
	TestEqual(TEXT("External motion still uses the original discontinuity boundary"), Control->GetAirborneState().EndReason, ERPGAirborneEndReason::ForeignMovement);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGPlatformPlayerOwnedBaseTest, "ProjectRPG.Combat.AirbornePlatformPlayer.PlayerOwnedBaseKeepsRawChecks", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGPlatformPlayerOwnedBaseTest::RunTest(const FString&)
{
	using namespace RPGPlatformPlayerTests;
	FScope W; if (!W.Init(false)) return false;
	auto* Mover = MovePlatform(W, 600.f);
	auto* Source = W.Spawn(FVector(-1000, 0, 300)); auto* Target = W.Spawn(FVector(0, 0, 300));
	Target->GetCharacterMovement()->AddTickPrerequisiteComponent(Mover);
	auto* Security = Possess(W, Target);
	W.Floor->GetOwner()->SetOwner(Target->GetController());
	for (int32 Frame = 0; Frame < 180; ++Frame) Tick(W, 1.f / 60.f);
	auto Config = Settings(); Config.DownDuration = 2.f;
	if (!Apply(Source, Target, Config) || !WaitForDown(W, Target->GetCombatControlComponent(), 1.f / 60.f)) return false;
	const int32 Before = Security->GetTotalViolationCount();
	AddExpectedMessage(TEXT("Type=MovementSpeed"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1, false);
	for (int32 Frame = 0; Frame < 45 && Security->GetTotalViolationCount() == Before; ++Frame) Tick(W, 1.f / 60.f);
	const auto* Movement = CastChecked<URPGCharacterMovementComponent>(Target->GetCharacterMovement());
	TestTrue(TEXT("A player-owned base receives no trusted server carry credit"), Movement->GetServerBasedMovementTotal().IsNearlyZero());
	TestTrue(TEXT("Player-owned carrier retains the original movement checks"), Security->GetTotalViolationCount() > Before);
	return true;
}
#endif
