#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "Combat/RPGHitReactionTypes.h"
#include "Combat/RPGHitReactionProfile.h"
#include "Ability/RPGNPCGameplayAbility.h"
#include "DataAsset/StartUpData/DataAsset_NPCStartUpData.h"
#include "Skill/RPGSkillDefinition.h"
#include "Type/RPGStructTypes.h"
#include "RPGInputTags.h"
#include "Component/Combat/RPGCombatReactionComponent.h"
#include "Character/RPGBaseCharacter.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Attribute/RPGAttributeSet.h"
#include "Animation/AnimMontage.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Tests/RPGTestWorldCleanup.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "Tests/RPGSkillDamageSecurityTestFixture.h"
#include <limits>

struct FRPGHitReactionTestAccess
{
	static void Enable(URPGCombatReactionComponent& Receiver) { Receiver.PresentationProfile = NewObject<URPGHitReactionProfile>(&Receiver); }
	static void Receive(URPGCombatReactionComponent& Receiver, uint32 Sequence)
	{
		Receiver.MulticastPresentHit_Implementation(Sequence, ERPGHitReactionDirection::Front,
			nullptr, NAME_None, 0.0, 0);
	}
	static uint32 LastSequence(const URPGCombatReactionComponent& Receiver) { return Receiver.LastReceivedSequence; }
	static int32 ReceiptCount(const URPGCombatReactionComponent& Receiver) { return Receiver.RecentApprovedHits.Num(); }
	static bool IsSafe(const UAnimMontage* Montage, FName Section)
	{
		return URPGCombatReactionComponent::IsSafeCosmeticMontage(Montage, Section);
	}
	static void SetFallback(URPGCombatReactionComponent& Receiver, UAnimMontage* Montage, FName Section)
	{
		Receiver.PresentationProfile = NewObject<URPGHitReactionProfile>(&Receiver);
		Receiver.PresentationProfile->FallbackMontage.Montage = Montage;
		Receiver.PresentationProfile->FallbackMontage.Section = Section;
	}
};

namespace RPGHitReactionTests
{
struct FWorldScope
{
	UWorld* World = nullptr;
	~FWorldScope()
	{
		if (World)
		{
			RPGTestWorld::Destroy(World);
		}
	}
	bool Initialize()
	{
		World = UWorld::CreateWorld(EWorldType::Game, false,
			MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()));
		if (!World) return false;
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		World->BeginPlay();
		World->GetWorldSettings()->NotifyBeginPlay();
		return true;
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGHitReactionPolicyDataTest,
	"ProjectRPG.Combat.Reaction.DataDrivenPolicy", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGHitReactionPolicyDataTest::RunTest(const FString&)
{
	TestTrue(TEXT("Reaction configuration is no longer a GA class flag"),
		URPGGameplayAbility::StaticClass()->FindPropertyByName(TEXT("bUseSharedHitPresentation")) == nullptr);
	auto* Data = NewObject<UDataAsset_NPCStartUpData>();
	UObject* Owner = Data;
	const auto SharedClass = URPGNPCGameplayAbility::StaticClass();
	FGameplayAbilitySpec Legacy, Cosmetic, Disabled, Player;
	TestTrue(TEXT("Unconfigured NPC attacks preserve legacy behavior"), Data->BuildAttackAbilitySpec(SharedClass, 1, Owner, Legacy));
	Data->AttackHitReactionPolicies.Add(SharedClass, ERPGAttackHitReactionPolicy::Cosmetic);
	TestTrue(TEXT("Same NPC GA can be granted with cosmetic data"), Data->BuildAttackAbilitySpec(SharedClass, 1, Owner, Cosmetic));
	TestEqual(TEXT("Changing data never requires another GA class"), Legacy.Ability->GetClass(), Cosmetic.Ability->GetClass());
	TestEqual(TEXT("Legacy grant remains independent"), RPGHitReaction::GetGrantedPolicy(&Legacy), ERPGAttackHitReactionPolicy::Legacy);
	TestEqual(TEXT("Cosmetic metadata comes from the grant data"), RPGHitReaction::GetGrantedPolicy(&Cosmetic), ERPGAttackHitReactionPolicy::Cosmetic);
	Data->AttackHitReactionPolicies.Add(SharedClass, ERPGAttackHitReactionPolicy::None);
	TestTrue(TEXT("Same attack supports disabled reactions through data"), Data->BuildAttackAbilitySpec(SharedClass, 1, Owner, Disabled));
	TestEqual(TEXT("Existing grant is frozen when its data changes"), RPGHitReaction::GetGrantedPolicy(&Cosmetic), ERPGAttackHitReactionPolicy::Cosmetic);
	TestEqual(TEXT("Explicit no-reaction policy"), RPGHitReaction::GetGrantedPolicy(&Disabled), ERPGAttackHitReactionPolicy::None);
	UClass* AttackChild = LoadClass<URPGNPCGameplayAbility>(nullptr,
		TEXT("/Game/Blueprints/Character/NPC/Gruntling/GameplayAbility/GA_Guardian_Melee1.GA_Guardian_Melee1_C"));
	if (!TestNotNull(TEXT("Existing authored NPC attack"), AttackChild)) return false;
	Data->AttackHitReactionPolicies.Add(SharedClass, ERPGAttackHitReactionPolicy::Cosmetic);
	FGameplayAbilitySpec Family, Override;
	TestTrue(TEXT("One parent entry configures an attack family"), Data->BuildAttackAbilitySpec(AttackChild, 1, Owner, Family));
	TestEqual(TEXT("Inherited family policy"), RPGHitReaction::GetGrantedPolicy(&Family), ERPGAttackHitReactionPolicy::Cosmetic);
	Data->AttackHitReactionPolicies.Add(AttackChild, ERPGAttackHitReactionPolicy::Legacy);
	TestTrue(TEXT("Specific attack can override its family"), Data->BuildAttackAbilitySpec(AttackChild, 1, Owner, Override));
	TestEqual(TEXT("Nearest data entry wins"), RPGHitReaction::GetGrantedPolicy(&Override), ERPGAttackHitReactionPolicy::Legacy);
	FRPGPlayerAbilitySet Entry;
	Entry.InputTag = RPGGameplayTags::InputTag_LightAttack_GreatSword;
	Entry.AbilityToGrant = SharedClass;
	Entry.HitReactionPolicy = ERPGAttackHitReactionPolicy::Cosmetic;
	TestTrue(TEXT("Player grant data uses the same policy"), Entry.BuildAbilitySpec(3, Owner, Player));
	TestTrue(TEXT("Input tag is preserved"), Player.GetDynamicSpecSourceTags().HasTagExact(Entry.InputTag));
	TestTrue(TEXT("Source object is preserved for weapon authorization"), Player.SourceObject.Get() == Owner);
	TestEqual(TEXT("Grant level preserved"), Player.Level, 3);
	TestFalse(TEXT("Invalid policy is rejected before changing a grant"),
		RPGHitReaction::SetGrantedPolicy(Player, static_cast<ERPGAttackHitReactionPolicy>(255)));
	TestEqual(TEXT("Rejected policy cannot replace the valid policy"), RPGHitReaction::GetGrantedPolicy(&Player), ERPGAttackHitReactionPolicy::Cosmetic);
	auto* Definition = NewObject<URPGSkillDefinition>();
	Definition->HitReactionPolicy = ERPGAttackHitReactionPolicy::Cosmetic;
	FRPGSkillRuntimeSpec Runtime;
	Definition->BuildRuntimeSpec(nullptr, FRPGSkillSaveData(), Runtime);
	Definition->HitReactionPolicy = ERPGAttackHitReactionPolicy::Legacy;
	TestEqual(TEXT("Skill definition policy is frozen in the activation spec"), Runtime.HitReactionPolicy, ERPGAttackHitReactionPolicy::Cosmetic);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGHitReactionDirectionTest,
	"ProjectRPG.Combat.Reaction.PlanarDirections", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGHitReactionDirectionTest::RunTest(const FString&)
{
	using namespace RPGHitReaction;
	const FVector Forward(1, 0, 0);
	TestEqual(TEXT("Front"), ResolveDirection(Forward, FVector(1, 0, 0)), ERPGHitReactionDirection::Front);
	TestEqual(TEXT("Right"), ResolveDirection(Forward, FVector(0, 1, 0)), ERPGHitReactionDirection::Right);
	TestEqual(TEXT("Back"), ResolveDirection(Forward, FVector(-1, 0, 0)), ERPGHitReactionDirection::Back);
	TestEqual(TEXT("Left"), ResolveDirection(Forward, FVector(0, -1, 0)), ERPGHitReactionDirection::Left);
	TestEqual(TEXT("Height does not change planar side"),
		ResolveDirection(Forward, FVector(0, 100, 10000)), ERPGHitReactionDirection::Right);
	TestEqual(TEXT("Rotated victim uses its own forward"),
		ResolveDirection(FVector(0, 1, 0), FVector(1, 0, 0)), ERPGHitReactionDirection::Left);
	TestEqual(TEXT("Coincident impact has deterministic fallback"),
		ResolveDirection(Forward, FVector::ZeroVector), ERPGHitReactionDirection::Front);
	TestEqual(TEXT("Malformed direction cannot select arbitrary animation"),
		ResolveDirection(Forward, FVector(std::numeric_limits<double>::quiet_NaN(), 1, 0)), ERPGHitReactionDirection::Front);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGHitReactionSequenceTest,
	"ProjectRPG.Combat.Reaction.TransientSequence", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGHitReactionSequenceTest::RunTest(const FString&)
{
	TestTrue(TEXT("Newer event"), RPGHitReaction::IsNewerSequence(8, 7));
	TestFalse(TEXT("Duplicate event"), RPGHitReaction::IsNewerSequence(7, 7));
	TestFalse(TEXT("Delayed old event"), RPGHitReaction::IsNewerSequence(6, 7));
	TestTrue(TEXT("Counter wrap"), RPGHitReaction::IsNewerSequence(0, MAX_uint32));
	TestFalse(TEXT("Pre-wrap event after wrap"), RPGHitReaction::IsNewerSequence(MAX_uint32, 0));
	auto* Receiver = NewObject<URPGCombatReactionComponent>();
	FRPGHitReactionTestAccess::Receive(*Receiver, 12);
	FRPGHitReactionTestAccess::Receive(*Receiver, 11);
	TestEqual(TEXT("Skipped expired/unready events still consume sequence"),
		FRPGHitReactionTestAccess::LastSequence(*Receiver), 12u);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGHitReactionReceiverTest,
	"ProjectRPG.Combat.Reaction.ReceiverOptInAndLifetime", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGHitReactionReceiverTest::RunTest(const FString&)
{
	RPGHitReactionTests::FWorldScope Scope;
	if (!TestTrue(TEXT("Native world"), Scope.Initialize())) return false;
	auto* Source = Scope.World->SpawnActor<ARPGSkillDamageSecurityTestPawn>();
	auto* Target = Scope.World->SpawnActor<ARPGBaseCharacter>();
	if (!TestNotNull(TEXT("Source"), Source) || !TestNotNull(TEXT("Target"), Target)) return false;
	TestNull(TEXT("Removed health facade has no reflected native class"),
		FindObject<UClass>(nullptr, TEXT("/Script/Project_RPG.RPGHealthComponent")));
	Source->ASC()->InitAbilityActorInfo(Source, Source);
	Source->Attributes()->InitCurrentHealth(100.f);
	Target->GetRPGAbilitySystemComponent()->InitAbilityActorInfo(Target, Target);
	Target->GetRPGAttributeSet()->InitCurrentHealth(100.f);
	auto* Receiver = Target->GetCombatReactionComponent();
	if (!TestNotNull(TEXT("Shared receiver on base character"), Receiver)) return false;
	const FHitResult Hit(Target, nullptr, FVector::ZeroVector, FVector::ForwardVector);
	const FGuid Receipt = FGuid::NewGuid();
	TestEqual(TEXT("No profile declares an unsupported cosmetic receiver"), Receiver->PresentApprovedDamageHit(Source, Hit, Receipt),
		ERPGHitReactionRoute::Unsupported);
	FRPGHitReactionTestAccess::Enable(*Receiver);
	TestEqual(TEXT("Missing profile never falls back to gameplay cancellation"),
		Receiver->PresentApprovedDamageHit(Source, Hit, Receipt), ERPGHitReactionRoute::Suppressed);
	TestEqual(TEXT("Same native approval cannot present twice"),
		Receiver->PresentApprovedDamageHit(Source, Hit, Receipt), ERPGHitReactionRoute::Duplicate);
	TestEqual(TEXT("Wrong hit target is not admitted"),
		Receiver->PresentApprovedDamageHit(Source, FHitResult(Source, nullptr, FVector::ZeroVector, FVector::ForwardVector),
			FGuid::NewGuid()), ERPGHitReactionRoute::Suppressed);
	TestEqual(TEXT("Invalid approval identifier is not admitted"),
		Receiver->PresentApprovedDamageHit(Source, Hit, FGuid()), ERPGHitReactionRoute::Suppressed);
	Source->Attributes()->InitCurrentHealth(0.f);
	TestEqual(TEXT("Dead source is not admitted"), Receiver->PresentApprovedDamageHit(Source, Hit, FGuid::NewGuid()),
		ERPGHitReactionRoute::Suppressed);
	Source->Attributes()->InitCurrentHealth(100.f);
	Target->GetRPGAttributeSet()->InitCurrentHealth(0.f);
	TestEqual(TEXT("Lethal receiver does not start living reaction"),
		Receiver->PresentApprovedDamageHit(Source, Hit, FGuid::NewGuid()), ERPGHitReactionRoute::Suppressed);
	TestEqual(TEXT("Only accepted live approval was recorded"), FRPGHitReactionTestAccess::ReceiptCount(*Receiver), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGHitReactionSavedMontageTest,
	"ProjectRPG.Combat.Reaction.AuthoredCosmeticSafety", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGHitReactionSavedMontageTest::RunTest(const FString&)
{
	auto* PlayerMontage = LoadObject<UAnimMontage>(nullptr,
		TEXT("/Game/Blueprints/Character/Player/Anim/Montage/AM_Player_HitReaction.AM_Player_HitReaction"));
	auto* GuardianMontage = LoadObject<UAnimMontage>(nullptr,
		TEXT("/Game/Blueprints/Character/NPC/Gruntling/Anim/Montage/AM_Guardian_HitReact.AM_Guardian_HitReact"));
	if (!TestNotNull(TEXT("Saved player montage"), PlayerMontage)
		|| !TestNotNull(TEXT("Saved Guardian montage"), GuardianMontage)) return false;
	TestFalse(TEXT("Legacy player sound Notify requires a separate cosmetic profile"),
		FRPGHitReactionTestAccess::IsSafe(PlayerMontage, TEXT("Front")));
	TestFalse(TEXT("Legacy Guardian root motion/Notify cannot enter cosmetic playback"),
		FRPGHitReactionTestAccess::IsSafe(GuardianMontage, TEXT("React1")));
	// Validate the opt-in authored profiles while retaining the legacy assets.
	const int32 OriginalNotifyCount = PlayerMontage->Notifies.Num();
	auto* CosmeticMontage = LoadObject<UAnimMontage>(nullptr,
		TEXT("/Game/Blueprints/Combat/Reactions/AM_Player_LightHit.AM_Player_LightHit"));
	auto* NPCCosmeticMontage = LoadObject<UAnimMontage>(nullptr,
		TEXT("/Game/Blueprints/Combat/Reactions/AM_Guardian_LightHit.AM_Guardian_LightHit"));
	if (!TestTrue(TEXT("Saved player profile satisfies the cosmetic contract"),
		FRPGHitReactionTestAccess::IsSafe(CosmeticMontage, TEXT("Front")))) return false;
	if (!TestTrue(TEXT("Saved Guardian profile satisfies the cosmetic contract"),
		FRPGHitReactionTestAccess::IsSafe(NPCCosmeticMontage, TEXT("React1")))) return false;
	auto* RootMotionMontage = DuplicateObject<UAnimMontage>(GuardianMontage, GetTransientPackage());
	RootMotionMontage->Notifies.Reset();
	TestFalse(TEXT("Root motion remains rejected after removing montage Notifies"),
		FRPGHitReactionTestAccess::IsSafe(RootMotionMontage, TEXT("React1")));
	TestFalse(TEXT("Unknown section cannot silently play the wrong reaction"),
		FRPGHitReactionTestAccess::IsSafe(CosmeticMontage, TEXT("MissingReactionSection")));
	RPGHitReactionTests::FWorldScope Scope;
	if (!TestTrue(TEXT("Native presentation world"), Scope.Initialize())) return false;
	auto* Source = Scope.World->SpawnActor<ARPGSkillDamageSecurityTestPawn>();
	auto* Target = Scope.World->SpawnActor<ARPGBaseCharacter>();
	if (!TestNotNull(TEXT("Presentation source"), Source) || !TestNotNull(TEXT("Presentation target"), Target)) return false;
	Source->ASC()->InitAbilityActorInfo(Source, Source);
	Source->Attributes()->InitCurrentHealth(100.f);
	Target->GetRPGAbilitySystemComponent()->InitAbilityActorInfo(Target, Target);
	Target->GetRPGAttributeSet()->InitCurrentHealth(100.f);
	auto* Receiver = Target->GetCombatReactionComponent();
	FRPGHitReactionTestAccess::Enable(*Receiver);
	FRPGHitReactionTestAccess::SetFallback(*Receiver, CosmeticMontage, TEXT("Front"));
	const FGuid Receipt = FGuid::NewGuid();
	const FHitResult Hit(Target, nullptr, FVector::ZeroVector, FVector::ForwardVector);
	TestEqual(TEXT("Safe transient profile enters server dispatch"),
		Receiver->PresentApprovedDamageHit(Source, Hit, Receipt), ERPGHitReactionRoute::Dispatched);
	TestEqual(TEXT("Dispatch consumes the approval before redelivery"),
		Receiver->PresentApprovedDamageHit(Source, Hit, Receipt), ERPGHitReactionRoute::Duplicate);
	TestEqual(TEXT("Cosmetic receiver does not apply new damage"), Target->GetRPGAttributeSet()->GetCurrentHealth(), 100.f);
	TestEqual(TEXT("Legacy player Notifies remain unchanged"), PlayerMontage->Notifies.Num(), OriginalNotifyCount);
	AddInfo(TEXT("Legacy asset rejection, saved profile validation and native dispatch; rendered/network playback is verified separately."));
	return true;
}

#endif
