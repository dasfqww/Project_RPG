#if WITH_DEV_AUTOMATION_TESTS

#include "Ability/RPGGameplayAbility.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameplayAbilitySpec.h"
#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"

struct FRPGLocalAbilityStartupTestAccess
{
	static void Receive(URPGAbilitySystemComponent& ASC, FGameplayAbilitySpecHandle Handle)
	{
		ASC.ClientTryActivateAbility_Implementation(Handle);
	}
	static int32 DeferredCount(const URPGAbilitySystemComponent& ASC) { return ASC.DeferredLocalOnlyActivations.Num(); }
	static int32 EnginePendingCount(const URPGAbilitySystemComponent& ASC) { return ASC.PendingServerActivatedAbilities.Num(); }
	static bool IsReady(const URPGAbilitySystemComponent& ASC) { return ASC.IsLocalAbilityActorInfoReady(); }
};

namespace RPGLocalAbilityStartupTests
{
struct FFixture
{
	UWorld* World = nullptr;
	APawn* Pawn = nullptr;
	URPGAbilitySystemComponent* ASC = nullptr;
	FByteProperty* PolicyProperty = nullptr;
	FByteProperty* RoleProperty = nullptr;
	UGameplayAbility* AbilityDefaults = nullptr;
	uint8 OriginalPolicy = 0;
	bool bPolicyChanged = false;
	FGameplayAbilitySpecHandle Handle;

	FFixture()
	{
		AbilityDefaults = GetMutableDefault<URPGGameplayAbility>();
		PolicyProperty = FindFProperty<FByteProperty>(UGameplayAbility::StaticClass(), TEXT("NetExecutionPolicy"));
		RoleProperty = FindFProperty<FByteProperty>(AActor::StaticClass(), TEXT("Role"));
		if (!GEngine || !PolicyProperty || !RoleProperty) { return; }
		OriginalPolicy = PolicyProperty->GetPropertyValue_InContainer(AbilityDefaults);
		PolicyProperty->SetPropertyValue_InContainer(AbilityDefaults, EGameplayAbilityNetExecutionPolicy::LocalOnly);
		bPolicyChanged = true;
		World = UWorld::CreateWorld(EWorldType::Game, false,
			MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()), GetTransientPackage());
		if (!World) { return; }
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		Pawn = World->SpawnActor<APawn>();
		if (!Pawn) { return; }
		ASC = NewObject<URPGAbilitySystemComponent>(Pawn);
		Pawn->AddInstanceComponent(ASC);
		ASC->RegisterComponent();
		ASC->InitAbilityActorInfo(Pawn, Pawn);
		FGameplayAbilitySpec Spec(URPGGameplayAbility::StaticClass(), 1);
		Handle = Spec.Handle;
		ASC->GetActivatableAbilities().Add(Spec);
		// A mock owning-client role; these tests are not real RPC/network/UI proof.
		RoleProperty->SetPropertyValue_InContainer(Pawn, ROLE_AutonomousProxy);
	}
	~FFixture()
	{
		if (World)
		{
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}
		if (bPolicyChanged && PolicyProperty && AbilityDefaults)
		{
			PolicyProperty->SetPropertyValue_InContainer(AbilityDefaults, OriginalPolicy);
		}
	}
	bool IsValid() const { return World && Pawn && ASC && Handle.IsValid(); }
	void MakeOwnerReady()
	{
		APlayerController* Controller = World->SpawnActor<APlayerController>();
		if (!Controller) { return; }
		ULocalPlayer* Player = NewObject<ULocalPlayer>(GEngine);
		Controller->Player = Player;
		Player->PlayerController = Controller;
		Controller->SetAsLocalPlayerController();
		World->AddController(Controller);
		RoleProperty->SetPropertyValue_InContainer(Pawn, ROLE_Authority);
		Controller->Possess(Pawn);
		RoleProperty->SetPropertyValue_InContainer(Pawn, ROLE_AutonomousProxy);
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGLocalAbilityDeferredTest,
	"ProjectRPG.Item.QuickSlot.LocalStartup.DefersAndDeduplicates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGLocalAbilityDeferredTest::RunTest(const FString& Parameters)
{
	RPGLocalAbilityStartupTests::FFixture Fixture;
	if (!TestTrue(TEXT("Mock client fixture ready"), Fixture.IsValid())) { return false; }
	TestFalse(TEXT("No local controller yet"), FRPGLocalAbilityStartupTestAccess::IsReady(*Fixture.ASC));
	FRPGLocalAbilityStartupTestAccess::Receive(*Fixture.ASC, Fixture.Handle);
	FRPGLocalAbilityStartupTestAccess::Receive(*Fixture.ASC, Fixture.Handle);
	TestEqual(TEXT("One server-requested local-only activation is retained"), FRPGLocalAbilityStartupTestAccess::DeferredCount(*Fixture.ASC), 1);
	TestEqual(TEXT("Existing spec is not put in GAS's missing-spec queue"), FRPGLocalAbilityStartupTestAccess::EnginePendingCount(*Fixture.ASC), 0);
	Fixture.ASC->ClearActorInfo();
	TestEqual(TEXT("Actor info clear discards pending activation"), FRPGLocalAbilityStartupTestAccess::DeferredCount(*Fixture.ASC), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGLocalAbilityRemovedSpecTest,
	"ProjectRPG.Item.QuickSlot.LocalStartup.DropsRemovedSpec",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGLocalAbilityRemovedSpecTest::RunTest(const FString& Parameters)
{
	RPGLocalAbilityStartupTests::FFixture Fixture;
	if (!TestTrue(TEXT("Mock client fixture ready"), Fixture.IsValid())) { return false; }
	FRPGLocalAbilityStartupTestAccess::Receive(*Fixture.ASC, Fixture.Handle);
	TestEqual(TEXT("Deferred request is armed before testing removal"), FRPGLocalAbilityStartupTestAccess::DeferredCount(*Fixture.ASC), 1);
	Fixture.ASC->GetActivatableAbilities().Reset();
	Fixture.MakeOwnerReady();
	Fixture.ASC->RefreshLocalAbilityActorInfo();
	TestTrue(TEXT("Controller/Pawn actor info is now locally ready"), FRPGLocalAbilityStartupTestAccess::IsReady(*Fixture.ASC));
	TestEqual(TEXT("Removed spec is discarded, not re-granted"), FRPGLocalAbilityStartupTestAccess::DeferredCount(*Fixture.ASC), 0);
	Fixture.ASC->RefreshLocalAbilityActorInfo();
	TestEqual(TEXT("A second refresh cannot replay the request"), FRPGLocalAbilityStartupTestAccess::DeferredCount(*Fixture.ASC), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGLocalAbilityScopeTest,
	"ProjectRPG.Item.QuickSlot.LocalStartup.ClearsChangedAvatarScope",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGLocalAbilityScopeTest::RunTest(const FString& Parameters)
{
	RPGLocalAbilityStartupTests::FFixture Fixture;
	if (!TestTrue(TEXT("Mock client fixture ready"), Fixture.IsValid())) { return false; }
	FRPGLocalAbilityStartupTestAccess::Receive(*Fixture.ASC, Fixture.Handle);
	TestEqual(TEXT("Original scope has a deferred request"), FRPGLocalAbilityStartupTestAccess::DeferredCount(*Fixture.ASC), 1);
	APawn* Replacement = Fixture.World->SpawnActor<APawn>();
	if (!TestNotNull(TEXT("Replacement avatar"), Replacement)) { return false; }
	Fixture.ASC->InitAbilityActorInfo(Fixture.Pawn, Replacement);
	TestEqual(TEXT("A different avatar cannot inherit the old request"), FRPGLocalAbilityStartupTestAccess::DeferredCount(*Fixture.ASC), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGLocalAbilityMissingSpecTest,
	"ProjectRPG.Item.QuickSlot.LocalStartup.PreservesEngineMissingSpecQueue",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGLocalAbilityMissingSpecTest::RunTest(const FString& Parameters)
{
	RPGLocalAbilityStartupTests::FFixture Fixture;
	if (!TestTrue(TEXT("Mock client fixture ready"), Fixture.IsValid())) { return false; }
	const FGameplayAbilitySpecHandle Missing = FGameplayAbilitySpec(URPGGameplayAbility::StaticClass(), 1).Handle;
	FRPGLocalAbilityStartupTestAccess::Receive(*Fixture.ASC, Missing);
	FRPGLocalAbilityStartupTestAccess::Receive(*Fixture.ASC, Missing);
	TestEqual(TEXT("Unreplicated specs remain in GAS's existing queue"), FRPGLocalAbilityStartupTestAccess::EnginePendingCount(*Fixture.ASC), 1);
	TestEqual(TEXT("No invented spec or local presentation request"), FRPGLocalAbilityStartupTestAccess::DeferredCount(*Fixture.ASC), 0);
	return true;
}

#endif
