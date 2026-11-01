#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "Tests/RPGAbilityActorInfoTestFixture.h"
#include "Attribute/RPGAttributeSet.h"
#include "Character/RPGPlayer.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Component/Combat/PlayerCombatComponent.h"
#include "Controller/RPGPlayerController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameplayAbilitySpec.h"
#include "Misc/AutomationTest.h"

namespace RPGAbilityActorInfoTests
{
	struct FMember
	{
		APawn* Pawn = nullptr;
		URPGAbilitySystemComponent* ASC = nullptr;
		URPGAttributeSet* Attributes = nullptr;
	};
	struct FFixture
	{
		UWorld* World = nullptr;
		FFixture()
		{
			if (!GEngine) return;
			World = UWorld::CreateWorld(EWorldType::Game, false,
				MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()));
			if (World) GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		}
		~FFixture()
		{
			if (!World) return;
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}
		FMember AddMember(float Speed, bool bAddAttributes = true)
		{
			FMember Member;
			if (!World) return Member;
			Member.Pawn = World->SpawnActor<APawn>();
			if (!Member.Pawn) return Member;
			Member.ASC = NewObject<URPGAbilitySystemComponent>(Member.Pawn);
			Member.Pawn->AddInstanceComponent(Member.ASC);
			Member.ASC->RegisterComponent();
			Member.ASC->InitAbilityActorInfo(Member.Pawn, Member.Pawn);
			Member.Attributes = NewObject<URPGAttributeSet>(Member.Pawn);
			if (bAddAttributes) Member.ASC->AddAttributeSetSubobject(Member.Attributes);
			// Set the data directly so the late-attributes case does not auto-register it.
			Member.Attributes->GetAttackSpeedAttribute().SetNumericValueChecked(Speed, Member.Attributes);
			return Member;
		}
	};
	URPGAbilityActorInfoTestAbility* ActiveInstance(URPGAbilitySystemComponent* ASC, FGameplayAbilitySpecHandle Handle)
	{
		const auto* Spec = ASC ? ASC->FindAbilitySpecFromHandle(Handle) : nullptr;
		return Spec && !Spec->NonReplicatedInstances.IsEmpty()
			? Cast<URPGAbilityActorInfoTestAbility>(Spec->NonReplicatedInstances.Last()) : nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGAbilityPerExecutionActorInfoTest,
	"ProjectRPG.Ability.ActorInfo.PerExecutionUsesCurrentOwner",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGAbilityPerExecutionActorInfoTest::RunTest(const FString& Parameters)
{
	using namespace RPGAbilityActorInfoTests;
	FFixture Fixture;
	auto First = Fixture.AddMember(1.25f);
	auto Second = Fixture.AddMember(2.5f);
	if (!TestNotNull(TEXT("First ASC"), First.ASC) || !TestNotNull(TEXT("Second ASC"), Second.ASC)) return false;
	auto* Defaults = GetMutableDefault<URPGAbilityActorInfoTestAbility>();
	const auto FirstHandle = First.ASC->GiveAbility(FGameplayAbilitySpec(Defaults->GetClass(), 1));
	TestNull(TEXT("Grant to first owner does not cache attributes on the shared CDO"), Defaults->CachedAttributes());
	const auto SecondHandle = Second.ASC->GiveAbility(FGameplayAbilitySpec(Defaults->GetClass(), 1));
	TestNull(TEXT("Grant to another owner leaves the shared CDO empty"), Defaults->CachedAttributes());
	TestTrue(TEXT("First execution activates through GAS"), First.ASC->TryActivateAbility(FirstHandle));
	TestTrue(TEXT("Second execution activates through GAS"), Second.ASC->TryActivateAbility(SecondHandle));
	auto* FirstInstance = ActiveInstance(First.ASC, FirstHandle);
	auto* SecondInstance = ActiveInstance(Second.ASC, SecondHandle);
	if (!TestNotNull(TEXT("First execution instance"), FirstInstance) || !TestNotNull(TEXT("Second execution instance"), SecondInstance)) return false;
	TestTrue(TEXT("Per-execution setup is completed at activation, not on the CDO"), !FirstInstance->bPreparedAtEntry);
	TestEqual(TEXT("First execution reads its own attributes"), FirstInstance->ObservedAttributes, static_cast<const URPGAttributeSet*>(First.Attributes));
	TestEqual(TEXT("Second execution reads its own attributes"), SecondInstance->ObservedAttributes, static_cast<const URPGAttributeSet*>(Second.Attributes));
	TestEqual(TEXT("First attack speed is current before Blueprint activation"), FirstInstance->ObservedAttackSpeed, 1.25f);
	TestEqual(TEXT("Second attack speed is not borrowed from the first owner"), SecondInstance->ObservedAttackSpeed, 2.5f);
	FirstInstance->Finish();
	SecondInstance->Finish();
#if WITH_EDITOR
	const auto HUDClass = LoadClass<URPGPlayerGameplayAbility>(nullptr,
		TEXT("/Game/Blueprints/Character/Player/Abilities/GA_Player_DrawHUD.GA_Player_DrawHUD_C"));
	if (TestNotNull(TEXT("Actual saved HUD ability"), HUDClass))
	{
		TestEqual(TEXT("Actual HUD uses per-execution instances"), HUDClass->GetDefaultObject<URPGPlayerGameplayAbility>()->GetInstancingPolicy(),
			EGameplayAbilityInstancingPolicy::InstancedPerExecution);
	}
#endif
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGAbilityLateActorInfoTest,
	"ProjectRPG.Ability.ActorInfo.LateAttributesAndInvalidContext",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGAbilityLateActorInfoTest::RunTest(const FString& Parameters)
{
	using namespace RPGAbilityActorInfoTests;
	FFixture Fixture;
	auto Member = Fixture.AddMember(1.75f, false);
	if (!TestNotNull(TEXT("Late-attribute ASC"), Member.ASC)) return false;
	const auto Handle = Member.ASC->GiveAbility(FGameplayAbilitySpec(URPGAbilityActorInfoPerActorTestAbility::StaticClass(), 1));
	auto* Ability = ActiveInstance(Member.ASC, Handle);
	if (!TestNotNull(TEXT("Per-actor instance"), Ability)) return false;
	TestNull(TEXT("Attributes are absent at grant time"), Ability->CachedAttributes());
	Member.ASC->AddAttributeSetSubobject(Member.Attributes);
	TestTrue(TEXT("GAS activation after attributes arrive"), Member.ASC->TryActivateAbility(Handle));
	TestEqual(TEXT("Activation refreshes late attributes"), Ability->ObservedAttributes, static_cast<const URPGAttributeSet*>(Member.Attributes));
	TestEqual(TEXT("Late attack speed is current"), Ability->ObservedAttackSpeed, 1.75f);
	Ability->Finish();
	auto Other = Fixture.AddMember(3.f);
	if (!TestNotNull(TEXT("Other ASC"), Other.ASC)) return false;
	TestEqual(TEXT("A valid old cache is replaced by the supplied current context"), Ability->RefreshForTest(Other.ASC->AbilityActorInfo.Get()),
		static_cast<const URPGAttributeSet*>(Other.Attributes));
	FGameplayAbilityActorInfo NoAvatar = *Other.ASC->AbilityActorInfo;
	NoAvatar.AvatarActor.Reset();
	TestNull(TEXT("Missing avatar clears an otherwise valid ASC cache"), Ability->RefreshForTest(&NoAvatar));
	FGameplayAbilityActorInfo MissingASC;
	MissingASC.AvatarActor = Member.Pawn;
	TestNull(TEXT("Missing ASC clears old attributes"), Ability->RefreshForTest(&MissingASC));
	TestFalse(TEXT("Legacy authoritative mana cannot dereference a missing ASC"), Ability->TryLegacyManaForTest(&MissingASC));
	TestNull(TEXT("Null context clears old attributes"), Ability->RefreshForTest(nullptr));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGAbilitySpawnActorInfoTest,
	"ProjectRPG.Ability.ActorInfo.OnSpawnPreparedBeforeActivation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGAbilitySpawnActorInfoTest::RunTest(const FString& Parameters)
{
	using namespace RPGAbilityActorInfoTests;
	FFixture Fixture;
	auto Member = Fixture.AddMember(2.f);
	if (!TestNotNull(TEXT("Spawn ASC"), Member.ASC)) return false;
	const auto Handle = Member.ASC->GiveAbility(FGameplayAbilitySpec(URPGAbilityActorInfoSpawnTestAbility::StaticClass(), 1));
	auto* Ability = ActiveInstance(Member.ASC, Handle);
	if (!TestNotNull(TEXT("Spawn instance"), Ability)) return false;
	TestTrue(TEXT("Existing OnSpawn activation is retained"), Ability->IsActive());
	TestTrue(TEXT("Attributes are ready before synchronous OnSpawn entry"), Ability->bPreparedAtEntry);
	TestEqual(TEXT("Spawn uses this owner's attributes"), Ability->ObservedAttributes, static_cast<const URPGAttributeSet*>(Member.Attributes));
	Ability->Finish();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGAbilityPlayerBindingsTest,
	"ProjectRPG.Ability.ActorInfo.PlayerBindingsFollowCurrentContext",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGAbilityPlayerBindingsTest::RunTest(const FString& Parameters)
{
	using namespace RPGAbilityActorInfoTests;
	FFixture Fixture;
	if (!TestNotNull(TEXT("Binding world"), Fixture.World)) return false;
	// No BeginPlay, possession, startup assets or network identity are injected.
	auto* FirstPlayer = Fixture.World->SpawnActor<ARPGPlayer>();
	auto* SecondPlayer = Fixture.World->SpawnActor<ARPGPlayer>(FVector(1000.f, 0.f, 0.f), FRotator::ZeroRotator);
	auto* FirstController = Fixture.World->SpawnActor<ARPGPlayerController>();
	auto* SecondController = Fixture.World->SpawnActor<ARPGPlayerController>();
	if (!FirstPlayer || !SecondPlayer || !FirstController || !SecondController) return false;
	auto* Ability = NewObject<URPGAbilityActorInfoTestAbility>(FirstPlayer);
	FGameplayAbilityActorInfo First;
	First.AvatarActor = FirstPlayer;
	First.PlayerController = FirstController;
	Ability->BindForTest(&First);
	TestEqual(TEXT("First player"), Ability->GetPlayerCharacterFromActorInfo(), FirstPlayer);
	TestEqual(TEXT("First controller"), Ability->GetPlayerControllerFromActorInfo(), FirstController);
	FGameplayAbilityActorInfo Second;
	Second.AvatarActor = SecondPlayer;
	Second.PlayerController = SecondController;
	Ability->BindForTest(&Second);
	TestEqual(TEXT("A still-valid old Pawn is not reused"), Ability->GetPlayerCharacterFromActorInfo(), SecondPlayer);
	TestEqual(TEXT("A still-valid old Controller is not reused"), Ability->GetPlayerControllerFromActorInfo(), SecondController);
	TestEqual(TEXT("Combat component follows the current Pawn"), Ability->GetPlayerCombatComponentFromActorInfo(), SecondPlayer->GetPlayerCombatComponent());
	Ability->BindForTest(nullptr);
	TestNull(TEXT("Cleared Pawn context"), Ability->GetPlayerCharacterFromActorInfo());
	TestNull(TEXT("Cleared Controller context"), Ability->GetPlayerControllerFromActorInfo());
	TestNull(TEXT("Cleared combat context is safe for BP callers"), Ability->GetPlayerCombatComponentFromActorInfo());
	return true;
}

#endif
