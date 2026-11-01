#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "Tests/RPGCombatControlTestFixture.h"
#include "Tests/RPGSkillDamageSecurityTestFixture.h"
#include "Security/RPGDamageExecutionGuard.h"
#include "Ability/RPGNPCGameplayAbility.h"
#include "Attribute/RPGAttributeSet.h"
#include "Combat/RPGCombatControlTypes.h"
#include "Component/Combat/RPGCombatControlComponent.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Component/RPGCharacterMovementComponent.h"
#include "Controller/RPGAIController.h"
#include "DataAsset/StartUpData/DataAsset_NPCStartUpData.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Tests/RPGTestWorldCleanup.h"
#include "GameFramework/WorldSettings.h"
#include "GameplayEffect/RPGStunEffect.h"
#include "Misc/AutomationTest.h"
#include "RPGInputTags.h"
#include "Skill/RPGSkillDefinition.h"
#include <limits>

struct FRPGCombatControlTestAccess
{
	static void DisableVisual(URPGCombatControlComponent& C) { C.StunVisual.Reset(); }
	static int32 Handles(const URPGCombatControlComponent& C) { return C.OwnedStuns.Num(); }
};

namespace
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
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->GetWorldSettings()->NotifyBeginPlay();
			return true;
		}
		ARPGStunTestCharacter* Spawn()
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			auto* C = World->SpawnActor<ARPGStunTestCharacter>(Params);
			C->GetRPGAbilitySystemComponent()->InitAbilityActorInfo(C, C);
			C->GetRPGAbilitySystemComponent()->SetNumericAttributeBase(URPGAttributeSet::GetMaxHealthAttribute(), 100.f);
			C->GetRPGAbilitySystemComponent()->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(), 100.f);
			FRPGCombatControlTestAccess::DisableVisual(*C->GetCombatControlComponent());
			return C;
		}
	};
	FActiveGameplayEffectHandle Apply(ARPGBaseCharacter* Source, ARPGBaseCharacter* Target, float Duration, FGuid Receipt = FGuid::NewGuid())
	{
		return Target->GetCombatControlComponent()->ApplyApprovedDamageStun(Source,
			FHitResult(Target, nullptr, Target->GetActorLocation(), FVector::ForwardVector), Receipt, Duration);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGStunValidationTest, "ProjectRPG.Combat.Control.ValidationAndImmunity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGStunValidationTest::RunTest(const FString&)
{
	FWorldScope Scope;
	if (!Scope.Initialize()) return false;
	auto* Source = Scope.Spawn(); auto* Target = Scope.Spawn();
	auto* Control = Target->GetCombatControlComponent(); auto* ASC = Target->GetRPGAbilitySystemComponent();
	TestFalse(TEXT("Zero has no effect"), Apply(Source, Target, 0.f).IsValid());
	TestFalse(TEXT("Unbounded durations rejected"), Apply(Source, Target, 11.f).IsValid());
	TestFalse(TEXT("NaN duration rejected"), Apply(Source, Target, std::numeric_limits<float>::quiet_NaN()).IsValid());
	TestFalse(TEXT("Invalid receipt rejected"), Apply(Source, Target, 1.f, FGuid()).IsValid());
	TestFalse(TEXT("Wrong native target rejected"), Control->ApplyApprovedDamageStun(Source,
		FHitResult(Source, nullptr, FVector::ZeroVector, FVector::ForwardVector), FGuid::NewGuid(), 1.f).IsValid());
	const FGuid Rejected = FGuid::NewGuid();
	ASC->AddLooseGameplayTag(RPGCombatControl::StunImmunityTag());
	TestFalse(TEXT("Temporary control immunity rejects"), Apply(Source, Target, 1.f, Rejected).IsValid());
	ASC->RemoveLooseGameplayTag(RPGCombatControl::StunImmunityTag());
	TestFalse(TEXT("Rejected approved hit is not replayed when immunity ends"), Apply(Source, Target, 1.f, Rejected).IsValid());
	Source->GetRPGAbilitySystemComponent()->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(), 0.f);
	TestFalse(TEXT("Dead source rejected"), Apply(Source, Target, 1.f).IsValid());
	Source->GetRPGAbilitySystemComponent()->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(), 100.f);
	const FGuid Approved = FGuid::NewGuid();
	TestTrue(TEXT("Live native request creates duration GE"), Apply(Source, Target, 1.f, Approved).IsValid());
	TestFalse(TEXT("Same approved hit cannot create a second GE"), Apply(Source, Target, 1.f, Approved).IsValid());
	TestEqual(TEXT("One GE owned"), FRPGCombatControlTestAccess::Handles(*Control), 1);
	TestEqual(TEXT("Stun never invents damage"), Target->GetRPGAttributeSet()->GetCurrentHealth(), 100.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGStunOverlapTest, "ProjectRPG.Combat.Control.OverlapCleanseAndInhibition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGStunOverlapTest::RunTest(const FString&)
{
	FWorldScope Scope; if (!Scope.Initialize()) return false;
	auto* Source = Scope.Spawn(); auto* Target = Scope.Spawn();
	auto* ASC = Target->GetRPGAbilitySystemComponent(); auto* Control = Target->GetCombatControlComponent();
	const auto Long = Apply(Source, Target, 3.f);
	const double End = Control->GetStunPresentationState().EndServerTime;
	const auto Short = Apply(Source, Target, .2f);
	TestTrue(TEXT("Overlapping hits have separate GE handles"), Long.IsValid() && Short.IsValid() && Long != Short);
	TestTrue(TEXT("Shorter overlap cannot shorten the stun"), Control->GetStunPresentationState().EndServerTime >= End - .01);
	TestTrue(TEXT("Remove exactly the short source"), Control->RemoveOwnedStun(Short));
	TestTrue(TEXT("Other source keeps control"), RPGCombatControl::IsStunned(Target));
	FActiveGameplayEffectHandle Inhibited = ASC->SetActiveGameplayEffectInhibit(FActiveGameplayEffectHandle(Long), true, false);
	TestFalse(TEXT("Inhibited GE has no control"), RPGCombatControl::IsStunned(Target));
	Inhibited = ASC->SetActiveGameplayEffectInhibit(MoveTemp(Inhibited), false, false);
	TestTrue(TEXT("Removing inhibition derives control from GAS"), RPGCombatControl::IsStunned(Target));
	FGameplayEffectSpec Foreign(GetDefault<URPGStunEffect>(), ASC->MakeEffectContext(), 1.f);
	Foreign.SetDuration(5.f, true);
	const auto Other = ASC->ApplyGameplayEffectSpecToSelf(Foreign);
	TestFalse(TEXT("Receiver cannot cleanse a foreign handle"), Control->RemoveOwnedStun(Other));
	Control->ResetForAvatarReuse();
	TestNull(TEXT("Owned source removed on reset"), ASC->GetActiveGameplayEffect(Long));
	TestNotNull(TEXT("Foreign source preserved"), ASC->GetActiveGameplayEffect(Other));
	ASC->RemoveActiveGameplayEffect(Other);
	TestFalse(TEXT("Last GE releases only its control"), RPGCombatControl::IsStunned(Target));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGStunAbilitiesMovementTest, "ProjectRPG.Combat.Control.AbilityScopeAndMovement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGStunAbilitiesMovementTest::RunTest(const FString&)
{
	FWorldScope Scope; if (!Scope.Initialize()) return false;
	auto* Source = Scope.Spawn(); auto* Target = Scope.Spawn(); auto* ASC = Target->GetRPGAbilitySystemComponent();
	const auto Combat = ASC->GiveAbility(FGameplayAbilitySpec(URPGStunTestCombatAbility::StaticClass(), 1));
	const auto Passive = ASC->GiveAbility(FGameplayAbilitySpec(URPGStunTestPassiveAbility::StaticClass(), 1));
	const auto Death = ASC->GiveAbility(FGameplayAbilitySpec(URPGStunTestDeathAbility::StaticClass(), 1));
	TestTrue(TEXT("Combat starts normally"), ASC->TryActivateAbility(Combat));
	TestTrue(TEXT("Death execution starts normally"), ASC->TryActivateAbility(Death));
	auto* Move = Cast<URPGCharacterMovementComponent>(Target->GetCharacterMovement());
	if (!TestNotNull(TEXT("Shared movement implementation"), Move)) return false;
	Move->SetMovementMode(MOVE_Falling);
	Move->Velocity = FVector(300.f, 0.f, -100.f);
	const auto Stun = Apply(Source, Target, 2.f);
	TestTrue(TEXT("Stun applied"), Stun.IsValid());
	TestFalse(TEXT("Selected combat execution cancelled"), ASC->FindAbilitySpecFromHandle(Combat)->IsActive());
	TestTrue(TEXT("On-grant passive remains active"), ASC->FindAbilitySpecFromHandle(Passive)->IsActive());
	TestTrue(TEXT("Death execution remains active"), ASC->FindAbilitySpecFromHandle(Death)->IsActive());
	TestFalse(TEXT("Direct server activation rejected during stun"), ASC->TryActivateAbility(Combat));
	TestEqual(TEXT("Input acceleration clamped for server/autonomous paths"), Move->ConstrainInputAcceleration(FVector(100.f, 0.f, 0.f)), FVector::ZeroVector);
	TestEqual(TEXT("Stun max speed"), Move->GetMaxSpeed(), 0.f);
	TestFalse(TEXT("Jump rejected"), Move->CanAttemptJump());
	TestEqual(TEXT("Falling mode retained"), EMovementMode(Move->MovementMode), MOVE_Falling);
	TestEqual(TEXT("Vertical fall velocity retained"), Move->Velocity.Z, -100.0);
	Move->MaxWalkSpeed = 123.f;
	Target->GetCombatControlComponent()->RemoveOwnedStun(Stun);
	TestEqual(TEXT("Current speed configuration used after stun"), Move->GetMaxSpeed(), 123.f);
	const auto Again = Apply(Source, Target, 2.f);
	Move->SetMovementMode(MOVE_None); // Another system now owns a stronger restriction.
	Target->GetCombatControlComponent()->RemoveOwnedStun(Again);
	TestEqual(TEXT("Stun expiry never restores MOVE_Walking"), EMovementMode(Move->MovementMode), MOVE_None);
	TestTrue(TEXT("Combat can activate after control ends"), ASC->TryActivateAbility(Combat));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGStunReentrancyTest, "ProjectRPG.Combat.Control.UncancellableDeathAndRecovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGStunReentrancyTest::RunTest(const FString&)
{
	FWorldScope Scope; if (!Scope.Initialize()) return false;
	auto* Source = Scope.Spawn(); auto* Target = Scope.Spawn(); auto* ASC = Target->GetRPGAbilitySystemComponent();
	const auto Combat = ASC->GiveAbility(FGameplayAbilitySpec(URPGStunTestCombatAbility::StaticClass(), 1));
	ASC->TryActivateAbility(Combat);
	auto* Instance = Cast<URPGStunTestCombatAbility>(ASC->FindAbilitySpecFromHandle(Combat)->GetPrimaryInstance());
	Instance->SetInterruptible(false);
	TestFalse(TEXT("Uncancellable combat rejects the control as a whole"), Apply(Source, Target, 1.f).IsValid());
	TestFalse(TEXT("Rejected control leaves no tag"), RPGCombatControl::IsStunned(Target));
	Instance->SetInterruptible(true);
	const auto Callback = ASC->OnAbilityEnded.AddLambda([Target](const FAbilityEndedData&)
	{
		Target->AdvanceLife();
		Target->GetCharacterMovement()->SetMovementMode(MOVE_None);
		Target->GetCombatControlComponent()->ResetForAvatarReuse();
	});
	TestFalse(TEXT("Reentrant reset cannot leave a stun in the next life"), Apply(Source, Target, 1.f).IsValid());
	ASC->OnAbilityEnded.Remove(Callback);
	TestFalse(TEXT("No stale stun after reset callback"), RPGCombatControl::IsStunned(Target));
	const auto Stun = Apply(Source, Target, 2.f);
	TestTrue(TEXT("New life admits a fresh source"), Stun.IsValid());
	ASC->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(), 0.f);
	TestNull(TEXT("Death removes the receiver's GE"), ASC->GetActiveGameplayEffect(Stun));
	TestEqual(TEXT("Death's movement constraint retained"), EMovementMode(Target->GetCharacterMovement()->MovementMode), MOVE_None);
	ASC->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(), 100.f);
	const auto OldLife = Apply(Source, Target, 2.f);
	auto* Replacement = Scope.Spawn();
	ASC->InitAbilityActorInfo(Target, Replacement);
	TestNull(TEXT("ActorInfo replacement removes the old avatar's owned stun"), ASC->GetActiveGameplayEffect(OldLife));
	TestFalse(TEXT("New avatar never inherits old control"), RPGCombatControl::IsStunned(Replacement));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGStunLateDamageTest, "ProjectRPG.Combat.Control.RejectLateDamage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGStunLateDamageTest::RunTest(const FString&)
{
	FWorldScope Scope; if (!Scope.Initialize()) return false;
	auto* Source = Scope.Spawn(); auto* Target = Scope.Spawn();
	auto* SourceASC = Source->GetRPGAbilitySystemComponent(); auto* TargetASC = Target->GetRPGAbilitySystemComponent();
	auto Context = SourceASC->MakeEffectContext(); Context.AddInstigator(Source, Source);
	FGameplayEffectSpec Damage(GetDefault<URPGSkillDamageSecurityTestEffect>(), Context, 1.f);
	int32 Out = 0;
	{
		FRPGDamageExecutionGuard Receipt(Damage, *SourceASC, *TargetASC, 100.f);
		TestTrue(TEXT("Ordinary approved execution admitted"), FRPGDamageExecutionGuard::Approve(Damage, SourceASC, TargetASC, 10.0, Out));
	}
	const auto Stun = Apply(Target, Source, 2.f);
	TestTrue(TEXT("Attacker controlled before delayed event"), Stun.IsValid());
	{
		FRPGDamageExecutionGuard Receipt(Damage, *SourceASC, *TargetASC, 100.f);
		TestFalse(TEXT("Already-created attack spec cannot execute after stun"), FRPGDamageExecutionGuard::Approve(Damage, SourceASC, TargetASC, 10.0, Out));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGStunGrantDataTest, "ProjectRPG.Combat.Control.DataDrivenGrant",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGStunGrantDataTest::RunTest(const FString&)
{
	FRPGPlayerAbilitySet Entry;
	Entry.InputTag = RPGGameplayTags::InputTag_Roll;
	Entry.AbilityToGrant = URPGStunTestCombatAbility::StaticClass(); Entry.StunDuration = 1.25f;
	FGameplayAbilitySpec Spec;
	TestTrue(TEXT("Same execution class uses grant data"), Entry.BuildAbilitySpec(2, GetTransientPackage(), Spec));
	Entry.StunDuration = 0.f;
	TestEqual(TEXT("Granted duration frozen"), RPGCombatControl::GetGrantedStunDuration(&Spec), 1.25f);
	TestTrue(TEXT("Input tag preserved"), Spec.GetDynamicSpecSourceTags().HasTagExact(Entry.InputTag));
	auto* Data = NewObject<UDataAsset_NPCStartUpData>();
	Data->AttackStunDurations.Add(URPGNPCGameplayAbility::StaticClass(), 2.f);
	TestTrue(TEXT("NPC grant metadata"), Data->BuildAttackAbilitySpec(URPGNPCGameplayAbility::StaticClass(), 1, Data, Spec));
	TestEqual(TEXT("NPC and player use the same metadata"), RPGCombatControl::GetGrantedStunDuration(&Spec), 2.f);
	auto* Definition = NewObject<URPGSkillDefinition>(); Definition->StunDuration = .75f;
	FRPGSkillRuntimeSpec Runtime; Definition->BuildRuntimeSpec(nullptr, FRPGSkillSaveData(), Runtime);
	Definition->StunDuration = 0.f;
	TestEqual(TEXT("Canonical activation duration frozen"), Runtime.StunDuration, .75f);
	return true;
}

#endif
