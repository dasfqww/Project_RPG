#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "Tests/RPGSkillDamageSecurityTestFixture.h"
#include "AIController.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Abilities/GameplayAbilityTargetTypes.h"
#include "Attribute/RPGAttributeSet.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "DataTable/SkillData.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Tests/RPGTestWorldCleanup.h"
#include "GameFramework/WorldSettings.h"
#include "FunctionLibrary/RPGAbilityFunctionLibrary.h"
#include "GameplayEffect/GE_ExecCalc_TakeDamage.h"
#include "GameplayEffectComponents/AdditionalEffectsGameplayEffectComponent.h"
#include "GameplayEffectComponents/TargetTagRequirementsGameplayEffectComponent.h"
#include "Misc/AutomationTest.h"
#include "Security/RPGDamageExecutionGuard.h"
#include "RPGGameplayTags.h"
#include "Skill/RPGSkillDefinition.h"
#include "Component/Combat/RPGCombatContributionComponent.h"
#include "Component/Combat/RPGCombatControlComponent.h"
#include <limits>

namespace RPGSkillDamageSecurityTests
{
struct FFixture
{
	UWorld* World = nullptr;
	ARPGSkillDamageSecurityTestPawn* Source = nullptr;
	URPGSkillDamageSecurityTestAbility* Ability = nullptr;
	FGameplayAbilitySpecHandle Handle;

	~FFixture()
	{
		if (Source) Source->ASC()->CancelAllAbilities();
		if (World)
		{
			RPGTestWorld::Destroy(World);
		}
	}
	ARPGSkillDamageSecurityTestPawn* Pawn(const FVector Location, const uint8 Team)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		auto* Pawn = World->SpawnActor<ARPGSkillDamageSecurityTestPawn>(Location, FRotator::ZeroRotator, Params);
		auto* Controller = World->SpawnActor<AAIController>();
		if (!Pawn || !Controller) return nullptr;
		Controller->SetGenericTeamId(FGenericTeamId(Team));
		Controller->Possess(Pawn);
		return Pawn;
	}
	bool Initialize(FAutomationTestBase& Test)
	{
		World = UWorld::CreateWorld(EWorldType::Game, false,
			MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()));
		if (!Test.TestNotNull(TEXT("Damage world"), World)) return false;
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		World->BeginPlay();
		World->GetWorldSettings()->NotifyBeginPlay();
		Source = Pawn(FVector::ZeroVector, 1);
		if (!Test.TestNotNull(TEXT("Damage source"), Source)) return false;
		Handle = Source->ASC()->GiveAbility(FGameplayAbilitySpec(URPGSkillDamageSecurityTestAbility::StaticClass(), 1));
		const auto* Spec = Source->ASC()->FindAbilitySpecFromHandle(Handle);
		Ability = Spec ? Cast<URPGSkillDamageSecurityTestAbility>(Spec->GetPrimaryInstance()) : nullptr;
		return Test.TestNotNull(TEXT("Damage ability"), Ability);
	}
	static FRPGSkillRuntimeSpec Runtime()
	{
		FRPGSkillRuntimeSpec Spec;
		Spec.SkillTag = RPGGameplayTags::Player_Ability_Skill_AssultBlade;
		Spec.ExecutionPolicyClass = URPGSkillExecutionPolicy::StaticClass();
		Spec.BaseDamage = 10.0f;
		Spec.TargetingProfile.Shape.Type = ERPGHitQueryShape::Sector;
		Spec.TargetingProfile.Shape.Radius = 500.0f;
		Spec.TargetingProfile.Shape.AngleDegrees = 90.0f;
		Spec.TargetingProfile.Shape.HalfHeight = 100.0f;
		Spec.TargetingProfile.Filter.TeamRule = ERPGHitQueryTeamRule::HostileOnly;
		Spec.SecurityProfile.MaximumServerHitDistance = 600.0f;
		Spec.SecurityProfile.MaximumTargetsPerQuery = 8;
		Spec.SecurityProfile.MaximumHitsPerActivation = 1;
		Spec.SecurityProfile.MaximumDamagePerHit = 100.0f;
		return Spec;
	}
	bool Start(FAutomationTestBase& Test, const FRPGSkillRuntimeSpec& Spec, const FGameplayTag Event)
	{
		Ability->Configure(Spec, Event);
		World->Tick(LEVELTICK_All, 0.01f);
		return Test.TestTrue(TEXT("Native skill activation"), Source->ASC()->TryActivateAbility(Handle));
	}
	void Event(const FGameplayTag Tag, AActor* SubmittedTarget)
	{
		FGameplayEventData Payload;
		Payload.EventTag = Tag;
		Payload.Target = SubmittedTarget;
		Payload.EventMagnitude = 1.e9f;
		auto* SubmittedActors = new FGameplayAbilityTargetData_ActorArray();
		SubmittedActors->TargetActorArray.Add(SubmittedTarget);
		Payload.TargetData.Add(SubmittedActors);
		UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(Source, Tag, Payload);
	}
	bool Apply(TArray<FRPGHitQueryResult>& Hits, int32& Count, FText& Error, const float Damage = 10.0f)
	{
		return Ability->ExecuteAuthorizedSkillDamage(URPGSkillDamageSecurityTestEffect::StaticClass(),
			Damage, FGameplayTag(), Hits, Count, Error);
	}
};
}

using namespace RPGSkillDamageSecurityTests;

namespace
{
	URPGCombatContributionComponent* AddContributionReceiver(ARPGSkillDamageSecurityTestPawn& Pawn,
		float Stagger = 100.f, float Part = 100.f)
	{
		auto* Receiver = NewObject<URPGCombatContributionComponent>(&Pawn);
		Pawn.AddInstanceComponent(Receiver);
		Receiver->RegisterComponent();
		FRPGCombatContributionReceiverProfile Profile;
		Profile.bEnabled = true;
		Profile.StaggerThreshold = Stagger;
		Profile.PartBreakThreshold = Part;
		return Receiver->ConfigureForNewAttempt(Profile) ? Receiver : nullptr;
	}
	FRPGSkillRuntimeSpec ContributionRuntime()
	{
		auto Runtime = FFixture::Runtime();
		Runtime.SecurityProfile.MaximumHitsPerActivation = 16;
		Runtime.Contributions.Stagger = 20.f;
		Runtime.Contributions.PartBreak = 20.f;
		Runtime.StatScalars.Add(FGameplayTag::RequestGameplayTag(TEXT("Shared.Stat.StaggerContribution")), 1.25f);
		return Runtime;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGContributionQueryTest,
	"ProjectRPG.Combat.Contribution.ApprovedQueryDedupAndParty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGContributionQueryTest::RunTest(const FString&)
{
	FFixture F;
	if (!F.Initialize(*this)) return false;
	auto* Enemy = F.Pawn(FVector(200,0,0), 2);
	auto* Second = F.Pawn(FVector(300,60,0), 2);
	auto* Friendly = F.Pawn(FVector(250,-60,0), 1);
	auto* Far = F.Pawn(FVector(900,0,0), 2);
	if (!Enemy || !Second || !Friendly || !Far) return false;
	auto* Meter = AddContributionReceiver(*Enemy, 50, 40);
	auto* OtherMeter = AddContributionReceiver(*Second);
	auto* FriendlyMeter = AddContributionReceiver(*Friendly);
	auto* FarMeter = AddContributionReceiver(*Far);
	if (!Meter || !OtherMeter || !FriendlyMeter || !FarMeter) return false;
	const auto Runtime = ContributionRuntime();
	if (!F.Start(*this, Runtime, RPGGameplayTags::Player_Event_AOE)) return false;
	F.Event(RPGGameplayTags::Player_Event_AOE, Far); // malicious payload target/magnitude are ignored
	TestEqual(TEXT("Approved enemy receives the frozen stagger units"), Meter->GetContributionState().Stagger, 25.f);
	TestEqual(TEXT("Independent part units do not inherit the stagger scalar"), Meter->GetContributionState().PartBreak, 20.f);
	TestEqual(TEXT("Another server-query target receives its own units"), OtherMeter->GetContributionState().Stagger, 25.f);
	TestEqual(TEXT("Friendly payload/geometry cannot contribute"), FriendlyMeter->GetContributionState().Stagger, 0.f);
	TestEqual(TEXT("Out-of-range payload cannot contribute"), FarMeter->GetContributionState().Stagger, 0.f);
	const float FirstHP = Enemy->Attributes()->GetCurrentHealth();
	F.Event(RPGGameplayTags::Player_Event_AOE, Friendly);
	TestTrue(TEXT("A second allowed hit really deals damage"), Enemy->Attributes()->GetCurrentHealth() < FirstHP);
	TestEqual(TEXT("Multi-hit damage cannot duplicate one activation's contribution"), Meter->GetContributionState().Stagger, 25.f);
	F.Ability->Finish();
	if (!F.Start(*this, Runtime, RPGGameplayTags::Player_Event_AOE)) return false;
	F.Event(RPGGameplayTags::Player_Event_AOE, Enemy);
	TestTrue(TEXT("A fresh activation completes the stagger threshold"), Meter->GetContributionState().bStaggerComplete);
	TestTrue(TEXT("Part completion is independent and clamped"), Meter->GetContributionState().bPartBroken);
	TestEqual(TEXT("The second target has not reached its larger threshold"), OtherMeter->GetContributionState().Stagger, 50.f);

	auto* PartySource = F.Pawn(FVector::ZeroVector, 1);
	if (!PartySource) return false;
	const auto Handle = PartySource->ASC()->GiveAbility(FGameplayAbilitySpec(URPGSkillDamageSecurityTestAbility::StaticClass(), 1));
	const auto* Spec = PartySource->ASC()->FindAbilitySpecFromHandle(Handle);
	auto* Ability = Spec ? Cast<URPGSkillDamageSecurityTestAbility>(Spec->GetPrimaryInstance()) : nullptr;
	if (!Ability) return false;
	Ability->Configure(Runtime, RPGGameplayTags::Player_Event_AOE);
	if (!TestTrue(TEXT("Another party source starts its own ability"), PartySource->ASC()->TryActivateAbility(Handle))) return false;
	FGameplayEventData Event;
	UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(PartySource, RPGGameplayTags::Player_Event_AOE, Event);
	TestEqual(TEXT("Contributions from another source accumulate on the same receiver"), OtherMeter->GetContributionState().Stagger, 75.f);
	TestEqual(TEXT("Completed meters never overflow"), Meter->GetContributionState().Stagger, 50.f);
	const uint32 Revision = Meter->GetContributionState().Revision;
	Enemy->SetRole(ROLE_SimulatedProxy);
	TestFalse(TEXT("A client cannot configure or reset the receiver"), Meter->ConfigureForNewAttempt(FRPGCombatContributionReceiverProfile()));
	Meter->ResetForAvatarReuse();
	TestEqual(TEXT("Client reset leaves the projection intact"), Meter->GetContributionState().Revision, Revision);
	Enemy->SetRole(ROLE_Authority);
	PartySource->ASC()->CancelAllAbilities();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGContributionLifetimeTest,
	"ProjectRPG.Combat.Contribution.CallbackLifetimeAndInvalidData",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGContributionLifetimeTest::RunTest(const FString&)
{
	FFixture F;
	if (!F.Initialize(*this)) return false;
	auto* Enemy = F.Pawn(FVector(200,0,0), 2);
	if (!Enemy) return false;
	auto* Meter = AddContributionReceiver(*Enemy);
	if (!Meter) return false;
	const auto Runtime = ContributionRuntime();
	if (!F.Start(*this, Runtime, RPGGameplayTags::Player_Event_AOE)) return false;
	const uint32 Generation = Meter->GetContributionState().Generation;
	const auto Reset = Enemy->ASC()->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
		[Meter](UAbilitySystemComponent*, const FGameplayEffectSpec&, FActiveGameplayEffectHandle) { Meter->ResetForAvatarReuse(); });
	F.Event(RPGGameplayTags::Player_Event_AOE, Enemy);
	Enemy->ASC()->OnGameplayEffectAppliedDelegateToSelf.Remove(Reset);
	TestTrue(TEXT("The approved effect actually dealt damage before its callback"), Enemy->Attributes()->GetCurrentHealth() < 1000.f);
	TestTrue(TEXT("The target generation changed inside the damage callback"), Meter->GetContributionState().Generation != Generation);
	TestEqual(TEXT("An old hit cannot contribute after receiver reset, even without a control component"), Meter->GetContributionState().Stagger, 0.f);
	F.Event(RPGGameplayTags::Player_Event_AOE, Enemy);
	TestEqual(TEXT("A genuinely later hit may contribute to the new target generation"), Meter->GetContributionState().Stagger, 25.f);
	auto* Control = NewObject<URPGCombatControlComponent>(Enemy);
	Enemy->AddInstanceComponent(Control);
	Control->RegisterComponent();
	Control->ResetForAvatarReuse();
	TestEqual(TEXT("Common avatar reuse clears only the receiver meter"), Meter->GetContributionState().Stagger, 0.f);
	F.Ability->Finish();
	if (!F.Start(*this, Runtime, RPGGameplayTags::Player_Event_AOE)) return false;
	const auto End = Enemy->ASC()->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
		[&F](UAbilitySystemComponent*, const FGameplayEffectSpec&, FActiveGameplayEffectHandle) { F.Ability->Finish(); });
	F.Event(RPGGameplayTags::Player_Event_AOE, Enemy);
	Enemy->ASC()->OnGameplayEffectAppliedDelegateToSelf.Remove(End);
	TestEqual(TEXT("An ended activation cannot award units after its damage callback"), Meter->GetContributionState().Stagger, 0.f);
	auto Invalid = Runtime;
	Invalid.Contributions.PartBreak = std::numeric_limits<float>::quiet_NaN();
	if (!F.Start(*this, Invalid, RPGGameplayTags::Player_Event_AOE)) return false;
	const float HP = Enemy->Attributes()->GetCurrentHealth();
	F.Event(RPGGameplayTags::Player_Event_AOE, Enemy);
	TestEqual(TEXT("Invalid frozen contribution data is rejected before damage"), Enemy->Attributes()->GetCurrentHealth(), HP);
	F.Ability->Finish();
	Enemy->Attributes()->SetCurrentHealth(1.f);
	if (!F.Start(*this, Runtime, RPGGameplayTags::Player_Event_AOE)) return false;
	F.Event(RPGGameplayTags::Player_Event_AOE, Enemy);
	TestEqual(TEXT("A lethal approved hit ends this target's life"), Enemy->Attributes()->GetCurrentHealth(), 0.f);
	TestEqual(TEXT("Lethal damage cannot refill a meter cleared by the death lifecycle"), Meter->GetContributionState().Stagger, 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGSkillFrozenDamageTest,
	"ProjectRPG.Security.SkillDamage.FrozenTableDamage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGSkillFrozenDamageTest::RunTest(const FString&)
{
	auto* Table = NewObject<UDataTable>();
	Table->RowStruct = FRPGSkillDataTable::StaticStruct();
	FRPGSkillDataTable Row;
	Row.AttackPower = 25.0f;
	Table->AddRow(TEXT("Damage"), Row);
	auto* Definition = NewObject<URPGSkillDefinition>();
	Definition->SkillDataHandle.DataTable = Table;
	Definition->SkillDataHandle.RowName = TEXT("Damage");
	Definition->SecurityProfile.MaximumFinalDamagePerHit = 50.0f;
	FRPGSkillRuntimeSpec First, Next;
	Definition->BuildRuntimeSpec(nullptr, FRPGSkillSaveData(), First);
	Row.AttackPower = 40.0f;
	Table->AddRow(TEXT("Damage"), Row);
	Definition->SecurityProfile.MaximumFinalDamagePerHit = 75.0f;
	Definition->BuildRuntimeSpec(nullptr, FRPGSkillSaveData(), Next);
	TestEqual(TEXT("Active snapshot unchanged by table mutation"), First.BaseDamage, 25.0f);
	TestEqual(TEXT("Next activation uses current table"), Next.BaseDamage, 40.0f);
	TestEqual(TEXT("Final HP cap is frozen with the activation"), First.SecurityProfile.MaximumFinalDamagePerHit, 50.0f);
	TestEqual(TEXT("Next activation uses the new authored HP cap"), Next.SecurityProfile.MaximumFinalDamagePerHit, 75.0f);
	First.Reset();
	TestEqual(TEXT("End/reset clears damage"), First.BaseDamage, 0.0f);
	Definition->SkillDataHandle = FDataTableRowHandle();
	Definition->BuildRuntimeSpec(nullptr, FRPGSkillSaveData(), Next);
	TestEqual(TEXT("Missing table is fail-closed, not stale legacy damage"), Next.BaseDamage, 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGSkillDamageEventQueryTest,
	"ProjectRPG.Security.SkillDamage.EventsUseServerQueryAndBudget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGSkillDamageEventQueryTest::RunTest(const FString&)
{
	// Both authored event types must dispatch through the same frozen query.
	const FGameplayTag Events[] = { RPGGameplayTags::Player_Event_AOE, RPGGameplayTags::Shared_Event_Hit };
	for (const FGameplayTag Tag : Events)
	{
		FFixture F;
		if (!F.Initialize(*this)) return false;
		auto* Eligible = F.Pawn(FVector(300, 0, 0), 2);
		auto* Behind = F.Pawn(FVector(-200, 0, 0), 2);
		auto* Far = F.Pawn(FVector(800, 0, 0), 2);
		auto* Friendly = F.Pawn(FVector(300, 70, 0), 1);
		auto* Immune = F.Pawn(FVector(250, -60, 0), 2);
		if (!Eligible || !Behind || !Far || !Friendly || !Immune) return false;
		Immune->ASC()->AddLooseGameplayTag(RPGGameplayTags::Shared_Status_Invincible);
		auto Runtime = FFixture::Runtime();
		Runtime.StatScalars.Add(FGameplayTag::RequestGameplayTag(TEXT("Shared.Stat.Damage")), 2.0f);
		if (!F.Start(*this, Runtime, Tag)) return false;
		F.Event(Tag, Far);
		TestEqual(TEXT("Server query, not submitted target/magnitude, applies scaled real HP damage"), Eligible->Attributes()->GetCurrentHealth(), 980.0f);
		for (const auto* Rejected : { Behind, Far, Friendly, Immune })
			TestEqual(TEXT("Behind/far/friendly/invincible retain HP"), Rejected->Attributes()->GetCurrentHealth(), 1000.0f);
		TestEqual(TEXT("One approved hit consumes one activation budget"), F.Ability->HitCount(), 1);
		F.Event(Tag, Eligible);
		TestEqual(TEXT("Repeated notify cannot exceed cumulative budget"), Eligible->Attributes()->GetCurrentHealth(), 980.0f);
		F.Ability->Finish();
		F.Event(Tag, Eligible);
		TestEqual(TEXT("Late event after end cannot fall back to legacy damage"), Eligible->Attributes()->GetCurrentHealth(), 980.0f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGSkillDamageRejectedMagnitudeTest,
	"ProjectRPG.Security.SkillDamage.RejectedMagnitudeAndRangePreserveHealth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGSkillDamageRejectedMagnitudeTest::RunTest(const FString&)
{
	FFixture F;
	if (!F.Initialize(*this)) return false;
	auto* Target = F.Pawn(FVector(300, 0, 0), 2);
	if (!Target) return false;
	auto Runtime = FFixture::Runtime();
	Runtime.SecurityProfile.MaximumDamagePerHit = 15.0f;
	Runtime.StatScalars.Add(FGameplayTag::RequestGameplayTag(TEXT("Shared.Stat.Damage")), 2.0f);
	if (!F.Start(*this, Runtime, RPGGameplayTags::Player_Event_AOE)) return false;
	F.Event(RPGGameplayTags::Player_Event_AOE, Target);
	TArray<FRPGHitQueryResult> Hits;
	int32 Count = 123;
	FText Error;
	for (const float Bad : { 0.0f, -1.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity() })
	{
		TestFalse(TEXT("Invalid damage rejected"), F.Apply(Hits, Count, Error, Bad));
		TestEqual(TEXT("Failure resets approved count"), Count, 0);
	}
	TestEqual(TEXT("Rejected magnitudes do not reserve hit budget"), F.Ability->HitCount(), 0);
	TestEqual(TEXT("Over-cap scaled damage preserves HP"), Target->Attributes()->GetCurrentHealth(), 1000.0f);
	F.Ability->Finish();
	Runtime = FFixture::Runtime();
	Runtime.SecurityProfile.MaximumServerHitDistance = 100.0f;
	if (!F.Start(*this, Runtime, RPGGameplayTags::Player_Event_AOE)) return false;
	F.Event(RPGGameplayTags::Player_Event_AOE, Target);
	TestEqual(TEXT("Spatial rejection preserves HP"), Target->Attributes()->GetCurrentHealth(), 1000.0f);
	TestEqual(TEXT("Failed GE/hit does not consume budget"), F.Ability->HitCount(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGSkillDamageReentrancyTest,
	"ProjectRPG.Security.SkillDamage.ReentrantEffectCannotSpendTwice",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGSkillDamageReentrancyTest::RunTest(const FString&)
{
	FFixture F;
	if (!F.Initialize(*this)) return false;
	auto* First = F.Pawn(FVector(200, 0, 0), 2);
	auto* Second = F.Pawn(FVector(350, 0, 0), 2);
	if (!First || !Second) return false;
	auto Runtime = FFixture::Runtime();
	Runtime.SecurityProfile.MaximumHitsPerActivation = 2;
	if (!F.Start(*this, Runtime, RPGGameplayTags::Player_Event_AOE)) return false;
	bool bNestedAccepted = true;
	int32 NestedCount = -1, ObservedReserved = 0, CallbackCount = 0;
	First->ASC()->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
		[&](UAbilitySystemComponent*, const FGameplayEffectSpec&, FActiveGameplayEffectHandle)
		{
			++CallbackCount;
			ObservedReserved = F.Ability->HitCount();
			TArray<FRPGHitQueryResult> Nested;
			FText Error;
			bNestedAccepted = F.Apply(Nested, NestedCount, Error);
		});
	F.Event(RPGGameplayTags::Player_Event_AOE, Second);
	TestEqual(TEXT("Real instant GE invokes delegate once"), CallbackCount, 1);
	TestEqual(TEXT("Budget reserved before external delegate"), ObservedReserved, 1);
	TestFalse(TEXT("Nested damage rejected"), bNestedAccepted);
	TestEqual(TEXT("Nested approved count cleared"), NestedCount, 0);
	TestEqual(TEXT("First target damaged once"), First->Attributes()->GetCurrentHealth(), 990.0f);
	TestEqual(TEXT("Second target damaged once"), Second->Attributes()->GetCurrentHealth(), 990.0f);
	TestEqual(TEXT("Only outer effects consume two hits"), F.Ability->HitCount(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGSkillDamageLifetimeTest,
	"ProjectRPG.Security.SkillDamage.EffectEndStopsRemainingHits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGSkillDamageLifetimeTest::RunTest(const FString&)
{
	FFixture F;
	if (!F.Initialize(*this)) return false;
	auto* First = F.Pawn(FVector(200, 0, 0), 2);
	auto* Second = F.Pawn(FVector(350, 0, 0), 2);
	if (!First || !Second) return false;
	auto Runtime = FFixture::Runtime();
	Runtime.SecurityProfile.MaximumHitsPerActivation = 2;
	if (!F.Start(*this, Runtime, RPGGameplayTags::Player_Event_AOE)) return false;
	First->ASC()->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
		[&](UAbilitySystemComponent*, const FGameplayEffectSpec&, FActiveGameplayEffectHandle) { F.Ability->Finish(); });
	TArray<FRPGHitQueryResult> Hits;
	int32 Count = 0;
	FText Error;
	TestTrue(TEXT("First instant effect was applied before end"), F.Apply(Hits, Count, Error));
	TestEqual(TEXT("Only one approved effect"), Count, 1);
	TestEqual(TEXT("Second hit never executes after end"), Second->Attributes()->GetCurrentHealth(), 1000.0f);
	TestFalse(TEXT("Ended activation cannot apply damage"), F.Apply(Hits, Count, Error));
	TestEqual(TEXT("Ended result resets count"), Count, 0);
	TestFalse(TEXT("Ended activation cannot query hits"), F.Ability->ExecuteActiveSkillHitQuery(Hits));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGSkillDamageReactivationTest,
	"ProjectRPG.Security.SkillDamage.EffectReactivationCannotInheritOldHits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGSkillDamageReactivationTest::RunTest(const FString&)
{
	FFixture F;
	if (!F.Initialize(*this)) return false;
	auto* First = F.Pawn(FVector(200, 0, 0), 2);
	auto* Second = F.Pawn(FVector(350, 0, 0), 2);
	if (!First || !Second) return false;
	auto Runtime = FFixture::Runtime();
	Runtime.SecurityProfile.MaximumHitsPerActivation = 2;
	if (!F.Start(*this, Runtime, RPGGameplayTags::Player_Event_AOE)) return false;
	bool bNewActivation = false;
	const FDelegateHandle Delegate = First->ASC()->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
		[&](UAbilitySystemComponent*, const FGameplayEffectSpec&, FActiveGameplayEffectHandle)
		{
			F.Ability->Finish();
			F.Ability->Configure(Runtime, RPGGameplayTags::Player_Event_AOE);
			bNewActivation = F.Source->ASC()->TryActivateAbility(F.Handle);
		});
	TArray<FRPGHitQueryResult> Hits;
	int32 Count = 0;
	FText Error;
	TestTrue(TEXT("First application completes"), F.Apply(Hits, Count, Error));
	First->ASC()->OnGameplayEffectAppliedDelegateToSelf.Remove(Delegate);
	TestTrue(TEXT("Delegate starts a new activation"), bNewActivation);
	TestEqual(TEXT("Old loop stops at generation change"), Count, 1);
	TestEqual(TEXT("Old loop cannot damage next target using new snapshot"), Second->Attributes()->GetCurrentHealth(), 1000.0f);
	TestEqual(TEXT("New activation has independent budget"), F.Ability->HitCount(), 0);
	TestTrue(TEXT("New activation may apply its own query afterwards"), F.Apply(Hits, Count, Error));
	TestEqual(TEXT("New activation applies two hits"), Count, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGFinalDamageAmplificationTest,
	"ProjectRPG.Security.SkillDamage.FinalDamageRejectsAmplification",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGFinalDamageAmplificationTest::RunTest(const FString&)
{
	for (int32 Case = 0; Case < 3; ++Case)
	{
		FFixture F;
		if (!F.Initialize(*this)) return false;
		auto* Target = F.Pawn(FVector(300,0,0), 2);
		if (!Target) return false;
		auto Runtime = FFixture::Runtime();
		Runtime.SecurityProfile.MaximumFinalDamagePerHit = 15.0f;
		if (!F.Start(*this, Runtime, RPGGameplayTags::Player_Event_AOE)) return false;
		if (Case == 0) F.Source->Attributes()->SetAttack(2.0f);
		if (Case == 1) Target->Attributes()->SetDefense(0.1f);
		if (Case == 2)
		{
			F.Source->Attributes()->SetCriticalChance(1.0f);
			F.Source->Attributes()->SetCriticalDamage(2.0f);
		}
		TArray<FRPGHitQueryResult> Hits;
		int32 Count = 0;
		FText Error;
		TestFalse(TEXT("SBC 10 is legal but amplified final damage is rejected"), F.Apply(Hits, Count, Error));
		TestEqual(TEXT("No HP decrease"), Target->Attributes()->GetCurrentHealth(), 1000.0f);
		TestEqual(TEXT("No approved target/reward path"), Count, 0);
		TestEqual(TEXT("Rejected calculation refunds reserved hit"), F.Ability->HitCount(), 0);
		TestTrue(TEXT("Rejection is reported"), !Error.IsEmpty());
		F.Source->Attributes()->SetAttack(1.0f);
		Target->Attributes()->SetDefense(1.0f);
		F.Source->Attributes()->SetCriticalChance(0.0f);
		TestTrue(TEXT("Same activation may recover with normal server stats"), F.Apply(Hits, Count, Error));
		TestEqual(TEXT("Recovery actual HP"), Target->Attributes()->GetCurrentHealth(), 990.0f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGFinalDamageBoundaryTest,
	"ProjectRPG.Security.SkillDamage.FinalDamageExactAndGlobalCeilings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGFinalDamageBoundaryTest::RunTest(const FString&)
{
	for (int32 Case = 0; Case < 3; ++Case)
	{
		FFixture F;
		if (!F.Initialize(*this)) return false;
		auto* Target = F.Pawn(FVector(300,0,0), 2);
		if (!Target) return false;
		auto Runtime = FFixture::Runtime();
		Runtime.SecurityProfile.MaximumFinalDamagePerHit = Case == 2 ? 1.e9f : (Case == 1 ? 10.5f : 20.0f);
		if (!F.Start(*this, Runtime, RPGGameplayTags::Player_Event_AOE)) return false;
		TArray<FRPGHitQueryResult> Hits;
		int32 Count = 0;
		FText Error;
		if (Case == 0)
		{
			F.Source->Attributes()->SetAttack(2.0f);
			TestTrue(TEXT("Exact final cap allowed"), F.Apply(Hits, Count, Error));
			TestEqual(TEXT("Exact boundary actual HP"), Target->Attributes()->GetCurrentHealth(), 980.0f);
		}
		if (Case == 1)
		{
			F.Source->Attributes()->SetAttack(1.075f);
			TestFalse(TEXT("Reject pre-truncation 10.75 > cap 10.5"), F.Apply(Hits, Count, Error));
			TestEqual(TEXT("Above fractional cap retains HP"), Target->Attributes()->GetCurrentHealth(), 1000.0f);
			F.Source->Attributes()->SetAttack(1.025f);
			TestTrue(TEXT("Valid fractional calculation truncates like production"), F.Apply(Hits, Count, Error));
			TestEqual(TEXT("Stored HP decrease is integer 10"), Target->Attributes()->GetCurrentHealth(), 990.0f);
		}
		if (Case == 2)
		{
			Target->Attributes()->SetMaxHealth(2.e9f);
			Target->Attributes()->SetCurrentHealth(2.e9f);
			F.Source->Attributes()->SetAttack(100000016.0f);
			TestFalse(TEXT("Global final limit cannot be crossed by multiplication"), F.Apply(Hits, Count, Error));
			TestEqual(TEXT("Overflow/over-limit preserves HP"), Target->Attributes()->GetCurrentHealth(), 2.e9f);
			F.Source->Attributes()->SetAttack(1.e8f);
			TestTrue(TEXT("Exact global integer/float boundary safe"), F.Apply(Hits, Count, Error));
			TestEqual(TEXT("No float-storage round-up beyond cap"), Target->Attributes()->GetCurrentHealth(), 1.e9f);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGFinalDamageForgedCapTest,
	"ProjectRPG.Security.SkillDamage.FinalDamageIgnoresForgedMetadata",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGFinalDamageForgedCapTest::RunTest(const FString&)
{
	FFixture F;
	if (!F.Initialize(*this)) return false;
	auto* Target = F.Pawn(FVector(300,0,0), 2);
	if (!Target) return false;
	F.Source->Attributes()->SetAttack(2.0f);
	const FHitResult Hit(Target, nullptr, Target->GetActorLocation(), FVector::UpVector);
	auto Spec = F.Source->ASC()->MakeOutgoingSpec(URPGSkillDamageSecurityTestEffect::StaticClass(), 1, F.Source->ASC()->MakeEffectContext());
	Spec.Data->SetSetByCallerMagnitude(RPGGameplayTags::Shared_SetByCaller_BaseDamage, 10.0f);
	Spec.Data->SetSetByCallerMagnitude(FName(TEXT("MaximumFinalDamagePerHit")), 1.e9f);
	Spec.Data->SetSetByCallerMagnitude(FGameplayTag::RequestGameplayTag(TEXT("Shared.Stat.Damage")), 1.e9f);
	auto Profile = FFixture::Runtime().SecurityProfile;
	Profile.MaximumFinalDamagePerHit = 15;
	FActiveGameplayEffectHandle Handle;
	TestFalse(TEXT("Caller metadata cannot raise the native server cap"), URPGAbilityFunctionLibrary::ApplyGameplayEffectSpecHandleToServerHit(F.Source, Hit, Spec, Profile, &Handle));
	TestFalse(TEXT("Rejected calculation clears handle success"), Handle.WasSuccessfullyApplied());
	TestEqual(TEXT("Forged cap cannot reduce HP"), Target->Attributes()->GetCurrentHealth(), 1000.0f);
	Profile.MaximumFinalDamagePerHit = 20;
	TestTrue(TEXT("A new server permit, not metadata, changes the cap"), URPGAbilityFunctionLibrary::ApplyGameplayEffectSpecHandleToServerHit(F.Source, Hit, Spec, Profile, &Handle));
	TestEqual(TEXT("Approved compatibility HP"), Target->Attributes()->GetCurrentHealth(), 980.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGFinalDamageShapeTest,
	"ProjectRPG.Security.SkillDamage.FinalDamageRejectsExtraEffectPaths",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGFinalDamageShapeTest::RunTest(const FString&)
{
	FFixture F;
	if (!F.Initialize(*this)) return false;
	auto* Target = F.Pawn(FVector(300,0,0), 2);
	if (!Target || !F.Start(*this, FFixture::Runtime(), RPGGameplayTags::Player_Event_AOE)) return false;
	TArray<FRPGHitQueryResult> Hits;
	int32 Count = 0;
	FText Error;
	TestFalse(TEXT("An extra direct HP modifier is rejected before GE application"),
		F.Ability->ExecuteAuthorizedSkillDamage(URPGSkillDamageSecurityExtraModifierEffect::StaticClass(), 10, FGameplayTag(), Hits, Count, Error));
	TestEqual(TEXT("Direct override cannot kill the target"), Target->Attributes()->GetCurrentHealth(), 1000.0f);
	TestEqual(TEXT("Unsupported definition spends no budget"), F.Ability->HitCount(), 0);
	for (int32 Case = 0; Case < 7; ++Case)
	{
		auto Spec = F.Source->ASC()->MakeOutgoingSpec(URPGSkillDamageSecurityTestEffect::StaticClass(), 1, F.Source->ASC()->MakeEffectContext());
		auto* Definition = NewObject<UGameplayEffect>(); // Never mutate an authored/shared CDO.
		Definition->DurationPolicy = EGameplayEffectDurationType::Instant;
		FGameplayEffectExecutionDefinition Execution;
		Execution.CalculationClass = UGE_ExecCalc_TakeDamage::StaticClass();
		Definition->Executions.Add(Execution);
		Spec.Data->Def = Definition;
		Spec.Data->SetSetByCallerMagnitude(RPGGameplayTags::Shared_SetByCaller_BaseDamage, 10.0f);
		if (Case == 0) Definition->DurationPolicy = EGameplayEffectDurationType::Infinite;
		if (Case == 1) Definition->Executions.Add(Execution);
		if (Case == 2) Definition->Executions[0].CalculationClass = UGameplayEffectExecutionCalculation::StaticClass();
		if (Case == 3) Definition->Executions[0].ConditionalGameplayEffects.AddDefaulted();
		if (Case == 4) Definition->AddComponent<UAdditionalEffectsGameplayEffectComponent>();
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		if (Case == 5) Spec.Data->TargetEffectSpecs.Add(Spec);
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
		if (Case == 6)
		{
			// An allowed native tag gate cannot conceal another effect path.
			Definition->AddComponent<UTargetTagRequirementsGameplayEffectComponent>();
			Definition->AddComponent<UAdditionalEffectsGameplayEffectComponent>();
		}
		TestFalse(TEXT("Duration/multiple/custom/conditional/component/linked effect rejected"), FRPGDamageExecutionGuard::Supports(*Spec.Data, *F.Source->ASC()));
		const FHitResult Hit(Target, nullptr, Target->GetActorLocation(), FVector::UpVector);
		TestFalse(TEXT("Compatibility path cannot bypass unsupported-shape guard"), URPGAbilityFunctionLibrary::ApplyGameplayEffectSpecHandleToServerHit(F.Source, Hit, Spec, FFixture::Runtime().SecurityProfile));
		TestEqual(TEXT("Unsupported effect cannot change HP"), Target->Attributes()->GetCurrentHealth(), 1000.0f);
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		Spec.Data->TargetEffectSpecs.Reset(); // Break deliberately malformed shared-pointer cycle.
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGFinalDamageReceiptTest,
	"ProjectRPG.Security.SkillDamage.FinalDamageReceiptIsolationAndReplay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGFinalDamageReceiptTest::RunTest(const FString&)
{
	FFixture F;
	if (!F.Initialize(*this)) return false;
	auto* A = F.Pawn(FVector(300,0,0), 2);
	auto* B = F.Pawn(FVector(350,0,0), 2);
	if (!A || !B) return false;
	auto Spec = F.Source->ASC()->MakeOutgoingSpec(URPGSkillDamageSecurityTestEffect::StaticClass(), 1, F.Source->ASC()->MakeEffectContext());
	Spec.Data->SetSetByCallerMagnitude(RPGGameplayTags::Shared_SetByCaller_BaseDamage, 10.0f);
	auto Other = F.Source->ASC()->MakeOutgoingSpec(URPGSkillDamageSecurityTestEffect::StaticClass(), 1, F.Source->ASC()->MakeEffectContext());
	Other.Data->SetSetByCallerMagnitude(RPGGameplayTags::Shared_SetByCaller_BaseDamage, 10.0f);
	{
		FRPGDamageExecutionGuard Outer(*Spec.Data, *F.Source->ASC(), *A->ASC(), 10);
		F.Source->ASC()->ApplyGameplayEffectSpecToTarget(*Spec.Data, B->ASC());
		TestEqual(TEXT("Wrong target cannot borrow outer permit"), B->Attributes()->GetCurrentHealth(), 1000.0f);
		TestFalse(TEXT("Wrong identity does not approve the permit"), Outer.WasApproved());
		F.Source->ASC()->ApplyGameplayEffectSpecToTarget(*Spec.Data, A->ASC());
		TestTrue(TEXT("Matching context/source/target approved"), Outer.WasApproved());
		F.Source->ASC()->ApplyGameplayEffectSpecToTarget(*Spec.Data, A->ASC());
		TestEqual(TEXT("Same permit cannot replay even through direct ASC"), A->Attributes()->GetCurrentHealth(), 990.0f);
		F.Source->ASC()->ApplyGameplayEffectSpecToTarget(*Other.Data, B->ASC());
		TestEqual(TEXT("New context cannot fall back to global cap while guarded"), B->Attributes()->GetCurrentHealth(), 1000.0f);
		{
			FRPGDamageExecutionGuard Inner(*Other.Data, *F.Source->ASC(), *B->ASC(), 10);
			F.Source->ASC()->ApplyGameplayEffectSpecToTarget(*Other.Data, B->ASC());
			TestTrue(TEXT("Explicit nested server permit is independent"), Inner.WasApproved());
		}
		F.Source->ASC()->ApplyGameplayEffectSpecToTarget(*Other.Data, B->ASC());
		TestEqual(TEXT("Nested receipt does not leak after scope exit"), B->Attributes()->GetCurrentHealth(), 990.0f);
	}
	F.Source->ASC()->ApplyGameplayEffectSpecToTarget(*Spec.Data, A->ASC());
	TestEqual(TEXT("Outside this API legacy direct server execution retains only the global ceiling"), A->Attributes()->GetCurrentHealth(), 980.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGFinalDamageInvalidTest,
	"ProjectRPG.Security.SkillDamage.FinalDamageInvalidStatsAndProfiles",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGFinalDamageInvalidTest::RunTest(const FString&)
{
	FFixture F;
	if (!F.Initialize(*this)) return false;
	auto* Target = F.Pawn(FVector(300,0,0), 2);
	if (!Target || !F.Start(*this, FFixture::Runtime(), RPGGameplayTags::Player_Event_AOE)) return false;
	TArray<FRPGHitQueryResult> Hits;
	int32 Count = 0;
	FText Error;
	for (int32 Case = 0; Case < 3; ++Case)
	{
		F.Source->Attributes()->SetAttack(Case == 0 ? -1.0f : 1.0f);
		Target->Attributes()->SetDefense(Case == 1 ? 0.0f : 1.0f);
		F.Source->Attributes()->SetCriticalDamage(Case == 2 ? -1.0f : 2.0f);
		TestFalse(TEXT("Malformed captured stats do not report a successful hit"), F.Apply(Hits, Count, Error));
		TestEqual(TEXT("Malformed stats retain HP"), Target->Attributes()->GetCurrentHealth(), 1000.0f);
		TestEqual(TEXT("Malformed stats retain budget"), F.Ability->HitCount(), 0);
	}
	for (const float Cap : { 0.0f, -1.0f, 2.e9f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity() })
	{
		auto Profile = FFixture::Runtime().SecurityProfile;
		Profile.MaximumFinalDamagePerHit = Cap;
		TestFalse(TEXT("Invalid authored final cap rejected"), Profile.IsValid());
	}
	auto Spec = F.Source->ASC()->MakeOutgoingSpec(URPGSkillDamageSecurityTestEffect::StaticClass(), 1, F.Source->ASC()->MakeEffectContext());
	// Nonfinite-calculation branch is a native guard unit check, not malformed attribute replication.
	for (const double Value : { 0.0, -1.0, 1.e10, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity() })
	{
		FRPGDamageExecutionGuard Guard(*Spec.Data, *F.Source->ASC(), *Target->ASC(), 100);
		int32 Damage = 123;
		TestFalse(TEXT("Unsafe calculation cannot reach integer storage"), FRPGDamageExecutionGuard::Approve(*Spec.Data, F.Source->ASC(), Target->ASC(), Value, Damage));
		TestEqual(TEXT("Invalid calculation clears damage output"), Damage, 0);
		TestFalse(TEXT("Invalid calculation is not approved"), Guard.WasApproved());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDeadCombatantDamageTest,
	"ProjectRPG.Security.SkillDamage.DeadCombatantsRejectExecution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGDeadCombatantDamageTest::RunTest(const FString&)
{
	for (int32 Case = 0; Case < 8; ++Case)
	{
		FFixture F;
		if (!F.Initialize(*this)) return false;
		auto* Target = F.Pawn(FVector(300, 0, 0), 2);
		if (!TestNotNull(TEXT("Liveness target"), Target) ||
			!F.Start(*this, FFixture::Runtime(), RPGGameplayTags::Player_Event_AOE)) return false;
		// Freeze the effect first: subsequent live death/HP changes must still be checked.
		auto Spec = F.Source->ASC()->MakeOutgoingSpec(URPGSkillDamageSecurityTestEffect::StaticClass(),
			1, F.Source->ASC()->MakeEffectContext());
		Spec.Data->SetSetByCallerMagnitude(RPGGameplayTags::Shared_SetByCaller_BaseDamage, 10.f);
		if (Case == 0) Target->Attributes()->SetCurrentHealth(0.f); // Tag has not arrived yet.
		if (Case == 1) Target->ASC()->AddLooseGameplayTag(RPGGameplayTags::Shared_Status_Death);
		if (Case == 2) F.Source->Attributes()->SetCurrentHealth(0.f);
		if (Case == 3) F.Source->ASC()->AddLooseGameplayTag(RPGGameplayTags::Shared_Status_Death);
		if (Case == 4) Target->Attributes()->InitCurrentHealth(std::numeric_limits<float>::quiet_NaN());
		if (Case == 5) F.Source->Attributes()->InitCurrentHealth(std::numeric_limits<float>::infinity());
		if (Case == 6) Target->Attributes()->SetCurrentHealth(-1.f);
		if (Case == 7) F.Source->Attributes()->SetCurrentHealth(-1.f);
		const float Before = Target->Attributes()->GetCurrentHealth();
		F.Source->ASC()->ApplyGameplayEffectSpecToTarget(*Spec.Data, Target->ASC());
		if (Case == 4) TestTrue(TEXT("Invalid HP remains unmodified"), FMath::IsNaN(Target->Attributes()->GetCurrentHealth()));
		else TestEqual(TEXT("Direct GAS execution cannot damage a dead/invalid combatant"), Target->Attributes()->GetCurrentHealth(), Before);
		TArray<FRPGHitQueryResult> Hits;
		int32 Count = 0;
		FText Error;
		TestFalse(TEXT("Authorized path does not approve a rejected liveness hit"), F.Apply(Hits, Count, Error));
		TestEqual(TEXT("Rejected liveness has no hit/reward list"), Count, 0);
		TestTrue(TEXT("Rejected liveness clears applied hits"), Hits.IsEmpty());
		TestEqual(TEXT("Rejected liveness does not spend the hit budget"), F.Ability->HitCount(), 0);
		TestEqual(TEXT("Rejected execution does not set damage metadata"), Target->Attributes()->GetTakeDamage(), 0.f);
		F.Source->Attributes()->InitCurrentHealth(1000.f);
		Target->Attributes()->InitCurrentHealth(1000.f);
		if (Case == 1) Target->ASC()->RemoveLooseGameplayTag(RPGGameplayTags::Shared_Status_Death);
		if (Case == 3) F.Source->ASC()->RemoveLooseGameplayTag(RPGGameplayTags::Shared_Status_Death);
		TestTrue(TEXT("Live server state still permits normal damage"), F.Apply(Hits, Count, Error));
		TestEqual(TEXT("Normal recovery damage remains unchanged"), Target->Attributes()->GetCurrentHealth(), 990.f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGCorpseDamageBudgetTest,
	"ProjectRPG.Security.SkillDamage.LethalHitRejectsCorpseReplay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGCorpseDamageBudgetTest::RunTest(const FString&)
{
	FFixture F;
	if (!F.Initialize(*this)) return false;
	auto* Target = F.Pawn(FVector(300, 0, 0), 2);
	if (!TestNotNull(TEXT("Lethal target"), Target)) return false;
	Target->Attributes()->SetCurrentHealth(10.f);
	auto Runtime = FFixture::Runtime();
	Runtime.SecurityProfile.MaximumHitsPerActivation = 4;
	if (!F.Start(*this, Runtime, RPGGameplayTags::Player_Event_AOE)) return false;
	TArray<FRPGHitQueryResult> Hits;
	int32 Count = 0;
	FText Error;
	TestTrue(TEXT("First lethal hit is authorized"), F.Apply(Hits, Count, Error));
	TestEqual(TEXT("Lethal hit reaches zero HP"), Target->Attributes()->GetCurrentHealth(), 0.f);
	TestEqual(TEXT("Production AttributeSet publishes one death tag"), Target->ASC()->GetTagCount(RPGGameplayTags::Shared_Status_Death), 1);
	for (int32 Replay = 0; Replay < 3; ++Replay)
	{
		TestFalse(TEXT("Remaining activation budget cannot authorize corpse hits"), F.Apply(Hits, Count, Error));
		TestEqual(TEXT("Corpse HP remains zero"), Target->Attributes()->GetCurrentHealth(), 0.f);
		TestEqual(TEXT("Only the actual lethal hit spends budget"), F.Ability->HitCount(), 1);
		TestEqual(TEXT("Corpse replay has no approved targets"), Count, 0);
		TestEqual(TEXT("Death tag count does not grow on replay"), Target->ASC()->GetTagCount(RPGGameplayTags::Shared_Status_Death), 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGRespawnDamageIsolationTest,
	"ProjectRPG.Security.SkillDamage.DestroyedHitCannotAliasReplacement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGRespawnDamageIsolationTest::RunTest(const FString&)
{
	FFixture F;
	if (!F.Initialize(*this)) return false;
	auto* OldTarget = F.Pawn(FVector(300, 0, 0), 2);
	if (!TestNotNull(TEXT("Old target"), OldTarget)) return false;
	const FHitResult OldHit(OldTarget, nullptr, OldTarget->GetActorLocation(), FVector::ForwardVector);
	const UAbilitySystemComponent* OldASC = OldTarget->ASC();
	auto Spec = F.Source->ASC()->MakeOutgoingSpec(URPGSkillDamageSecurityTestEffect::StaticClass(),
		1, F.Source->ASC()->MakeEffectContext());
	Spec.Data->SetSetByCallerMagnitude(RPGGameplayTags::Shared_SetByCaller_BaseDamage, 10.f);
	OldTarget->Destroy();
	auto* Replacement = F.Pawn(FVector(300, 0, 0), 2);
	if (!TestNotNull(TEXT("Replacement target"), Replacement)) return false;
	TestTrue(TEXT("Replacement owns a new ASC"), Replacement->ASC() != OldASC);
	TestFalse(TEXT("Destroyed server hit cannot address a replacement at the same coordinates"),
		URPGAbilityFunctionLibrary::ApplyGameplayEffectSpecHandleToServerHit(F.Source, OldHit, Spec, FFixture::Runtime().SecurityProfile));
	TestEqual(TEXT("Replacement initial HP is isolated"), Replacement->Attributes()->GetCurrentHealth(), 1000.f);
	TestEqual(TEXT("Replacement has no inherited death tag"), Replacement->ASC()->GetTagCount(RPGGameplayTags::Shared_Status_Death), 0);
	const FHitResult NewHit(Replacement, nullptr, Replacement->GetActorLocation(), FVector::ForwardVector);
	TestTrue(TEXT("Fresh authoritative hit may damage the replacement"),
		URPGAbilityFunctionLibrary::ApplyGameplayEffectSpecHandleToServerHit(F.Source, NewHit, Spec, FFixture::Runtime().SecurityProfile));
	TestEqual(TEXT("Replacement receives only its fresh hit"), Replacement->Attributes()->GetCurrentHealth(), 990.f);
	return true;
}

#endif
