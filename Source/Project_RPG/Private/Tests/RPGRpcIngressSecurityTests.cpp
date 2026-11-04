#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/RPGTargetDataSecurityTestFixture.h"
#include "Component/Skill/RPGPlayerSkillComponent.h"
#include "Component/UI/QuickSlotComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "Security/RPGServerRequestBudget.h"
#include <limits>

/** Access only existing implementations/state; adds no reflected or shipping command. */
struct FRPGRpcIngressTestAccess
{
	static void SetBudget(URPGAbilitySystemComponent& ASC, int32 Burst)
	{
		ASC.GenericEventRequestBudget = FRPGServerRequestBudget(Burst, 0.001);
	}
	static void SetBudget(URPGPlayerSkillComponent& Skills, int32 Burst)
	{
		Skills.ConfigurationRequestBudget = FRPGServerRequestBudget(Burst, 0.001);
	}
	static void SetBudget(UQuickSlotComponent& Slots, int32 Burst)
	{
		Slots.SlotRequestBudget = FRPGServerRequestBudget(Burst, 0.001);
	}
	static uint32 Revision(const URPGPlayerSkillComponent& Skills) { return Skills.ReplicatedSkillDataRevision; }
	static uint32 Revision(const UQuickSlotComponent& Slots) { return Slots.SkillSlotsRevision; }
	static void SkillRequest(URPGPlayerSkillComponent& Skills, int32 Kind)
	{
		switch (Kind % 5)
		{
		case 0: Skills.ServerTryLevelUpSkill_Implementation(FGameplayTag()); break;
		case 1: Skills.ServerTryLevelDownSkill_Implementation(FGameplayTag()); break;
		case 2: Skills.ServerLevelUpToMax_Implementation(FGameplayTag(), MAX_int32); break;
		case 3: Skills.ServerResetSkillLevel_Implementation(FGameplayTag()); break;
		default: Skills.ServerSelectTripod_Implementation(FGameplayTag(), MAX_int32, MIN_int32); break;
		}
	}
	static void SlotRequest(UQuickSlotComponent& Slots, int32 Kind)
	{
		switch (Kind % 3)
		{
		case 0: Slots.Server_SetSkillSlot_Implementation(INDEX_NONE, FGameplayTag()); break;
		case 1: Slots.Server_SetItemSlotById_Implementation(INDEX_NONE, FGuid::NewGuid()); break;
		default: Slots.Server_ClearSlot_Implementation(true, INDEX_NONE); break;
		}
	}
};

namespace
{
	FPredictionKey RpcKey(int16 Value) { FPredictionKey Key; Key.Current = Value; return Key; }
	struct FRpcWorld
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
			MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()));
		FRpcWorld()
		{
			if (!World) return;
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->GetWorldSettings()->NotifyBeginPlay();
		}
		~FRpcWorld()
		{
			if (!World) return;
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}
		URPGTargetDataSecurityTestASC* MakeASC()
		{
			AActor* Owner = World->SpawnActor<AActor>();
			auto* ASC = NewObject<URPGTargetDataSecurityTestASC>(Owner);
			Owner->AddInstanceComponent(ASC);
			ASC->RegisterComponent();
			ASC->InitAbilityActorInfo(Owner, Owner);
			return ASC;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGServerBudgetTest, "ProjectRPG.Security.RpcIngress.MonotonicBudget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGServerBudgetTest::RunTest(const FString&)
{
	FRPGServerRequestBudget Budget(2, 2.0);
	TestTrue(TEXT("Burst one"), Budget.TryConsume(10.0));
	TestTrue(TEXT("Burst two"), Budget.TryConsume(10.0));
	for (int32 Index = 0; Index < 10000; ++Index)
		if (Budget.TryConsume(10.0)) { AddError(TEXT("Frozen-time flood escaped budget")); break; }
	TestFalse(TEXT("Partial refill insufficient"), Budget.TryConsume(10.25));
	TestTrue(TEXT("Rejected calls retain fractional refill"), Budget.TryConsume(10.5));
	TestFalse(TEXT("Clock rollback cannot replenish"), Budget.TryConsume(9.0));
	TestFalse(TEXT("NaN cannot poison clock"), Budget.TryConsume(std::numeric_limits<double>::quiet_NaN()));
	TestFalse(TEXT("Infinity cannot replenish"), Budget.TryConsume(std::numeric_limits<double>::infinity()));
	TestTrue(TEXT("Budget recovers after invalid clock"), Budget.TryConsume(11.0));
	TestTrue(TEXT("Long idle restores a bounded burst"), Budget.TryConsume(1000000.0));
	TestTrue(TEXT("Second idle token"), Budget.TryConsume(1000000.0));
	TestFalse(TEXT("Idle never accumulates unlimited tokens"), Budget.TryConsume(1000000.0));
	FRPGServerRequestBudget OtherPlayer(2, 2.0);
	TestTrue(TEXT("Another player has independent capacity"), OtherPlayer.TryConsume(10.0));
	FRPGServerRequestBudget InvalidBurst(0, 1.0), InvalidRate(2, 0.0);
	TestFalse(TEXT("Invalid capacity fails closed"), InvalidBurst.TryConsume(10.0));
	TestFalse(TEXT("Invalid refill fails closed"), InvalidRate.TryConsume(10.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGGenericEventAdmissionTest, "ProjectRPG.Security.RpcIngress.GenericEventAdmission",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGGenericEventAdmissionTest::RunTest(const FString&)
{
	FRpcWorld F;
	if (!TestNotNull(TEXT("World"), F.World)) return false;
	auto* ASC = F.MakeASC();
	auto* Other = F.MakeASC();
	const auto Handle = ASC->GiveAbility(FGameplayAbilitySpec(URPGTargetDataSecurityTestAbility::StaticClass(), 1));
	const auto Original = RpcKey(100);
	ASC->SubmitGeneric(EAbilityGenericReplicatedEvent::InputPressed, Handle, Original, RpcKey(101));
	TestFalse(TEXT("Inactive handle cannot create cache"), ASC->HasCache(Handle, Original));
	if (!TestTrue(TEXT("Ability activates"), ASC->InternalTryActivateAbility(Handle, Original))) return false;
	ASC->SubmitGeneric(EAbilityGenericReplicatedEvent::InputPressed, Handle, RpcKey(99), RpcKey(101));
	TestFalse(TEXT("Stale activation cannot create cache"), ASC->HasCache(Handle, RpcKey(99)));
	Other->SubmitGeneric(EAbilityGenericReplicatedEvent::InputPressed, Handle, Original, RpcKey(101));
	TestFalse(TEXT("Foreign handle cannot create cache"), Other->HasCache(Handle, Original));
	ASC->SubmitGenericPayload(EAbilityGenericReplicatedEvent::InputPressed, Handle, Original, RpcKey(101),
		FVector(std::numeric_limits<double>::infinity(), 0, 0));
	TestFalse(TEXT("Non-finite payload cannot create cache"), ASC->HasCache(Handle, Original));
	ASC->SubmitGeneric(static_cast<EAbilityGenericReplicatedEvent::Type>(255), Handle, Original, RpcKey(101));
	ASC->SubmitGeneric(static_cast<EAbilityGenericReplicatedEvent::Type>(256), Handle, Original, RpcKey(101));
	ASC->SubmitGeneric(static_cast<EAbilityGenericReplicatedEvent::Type>(-1), Handle, Original, RpcKey(101));
	TestFalse(TEXT("Invalid event cannot index engine cache"), ASC->HasCache(Handle, Original));
	int32 Presses = 0, Releases = 0;
	ASC->AbilityReplicatedEventDelegate(EAbilityGenericReplicatedEvent::InputPressed, Handle, Original).AddLambda([&]()
	{
		++Presses;
		ASC->ConsumeGenericReplicatedEvent(EAbilityGenericReplicatedEvent::InputPressed, Handle, Original);
	});
	ASC->AbilityReplicatedEventDelegate(EAbilityGenericReplicatedEvent::InputReleased, Handle, Original).AddLambda([&]()
	{
		++Releases;
		ASC->ConsumeGenericReplicatedEvent(EAbilityGenericReplicatedEvent::InputReleased, Handle, Original);
	});
	FRPGRpcIngressTestAccess::SetBudget(*ASC, 3);
	ASC->SubmitGeneric(EAbilityGenericReplicatedEvent::InputPressed, Handle, Original, RpcKey(101));
	ASC->SubmitGenericPayload(EAbilityGenericReplicatedEvent::InputReleased, Handle, Original, RpcKey(102), FVector(1, 2, 3));
	TestEqual(TEXT("Normal press reaches GAS"), Presses, 1);
	TestEqual(TEXT("Normal payload release reaches GAS"), Releases, 1);
	// An invalid request consumes the last token; switching RPC variants cannot bypass it.
	ASC->SubmitGeneric(EAbilityGenericReplicatedEvent::InputPressed, Handle, RpcKey(99), RpcKey(103));
	for (int32 Index = 0; Index < 256; ++Index)
	{
		ASC->SubmitGeneric(EAbilityGenericReplicatedEvent::InputPressed, Handle, Original, RpcKey(104));
		ASC->SubmitGenericPayload(EAbilityGenericReplicatedEvent::InputReleased, Handle, Original, RpcKey(105), FVector::ZeroVector);
	}
	TestEqual(TEXT("Both variants share flood budget"), Presses + Releases, 2);
	FRPGRpcIngressTestAccess::SetBudget(*ASC, 2);
	auto* Ability = CastChecked<URPGTargetDataSecurityTestAbility>(ASC->FindAbilitySpecFromHandle(Handle)->GetPrimaryInstance());
	Ability->Finish();
	ASC->SubmitGeneric(EAbilityGenericReplicatedEvent::InputReleased, Handle, Original, RpcKey(106));
	TestFalse(TEXT("Ended ability cannot recreate cache"), ASC->HasCache(Handle, Original));
	const auto NextOriginal = RpcKey(120);
	if (!TestTrue(TEXT("Next activation starts"), ASC->InternalTryActivateAbility(Handle, NextOriginal))) return false;
	ASC->SubmitGeneric(EAbilityGenericReplicatedEvent::InputPressed, Handle, Original, RpcKey(121));
	TestFalse(TEXT("Old key cannot recreate cache during a new cast"), ASC->HasCache(Handle, Original));
	FRPGRpcIngressTestAccess::SetBudget(*ASC, 2);
	ASC->SubmitGeneric(EAbilityGenericReplicatedEvent::InputReleased, Handle, NextOriginal, RpcKey(122));
	TestTrue(TEXT("New cast accepts its own release"), ASC->HasCache(Handle, NextOriginal));
	Ability->Finish();
	// GAS replaces an absent incoming key with a server-generated activation key.
	const auto OtherHandle = Other->GiveAbility(FGameplayAbilitySpec(URPGTargetDataSecurityTestAbility::StaticClass(), 1));
	if (!TestTrue(TEXT("Server-originated activation starts"), Other->InternalTryActivateAbility(OtherHandle, FPredictionKey()))) return false;
	auto* OtherAbility = CastChecked<URPGTargetDataSecurityTestAbility>(Other->FindAbilitySpecFromHandle(OtherHandle)->GetPrimaryInstance());
	const auto ServerOriginal = OtherAbility->GetCurrentActivationInfo().GetActivationPredictionKey();
	TestTrue(TEXT("Engine supplied the authoritative key"), ServerOriginal.IsValidKey());
	Other->SubmitGeneric(EAbilityGenericReplicatedEvent::InputReleased, OtherHandle, FPredictionKey(), RpcKey(123));
	TestFalse(TEXT("An absent key cannot replace the generated key"), Other->HasCache(OtherHandle, FPredictionKey()));
	Other->SubmitGeneric(EAbilityGenericReplicatedEvent::InputReleased, OtherHandle, ServerOriginal, RpcKey(124));
	TestTrue(TEXT("Server-generated keys and another player remain independent"), Other->HasCache(OtherHandle, ServerOriginal));
	OtherAbility->Finish();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGConfigurationIngressTest, "ProjectRPG.Security.RpcIngress.ConfigurationBudgets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGConfigurationIngressTest::RunTest(const FString&)
{
	FRpcWorld F;
	if (!TestNotNull(TEXT("World"), F.World)) return false;
	AActor* Owner = F.World->SpawnActor<AActor>();
	auto* Skills = NewObject<URPGPlayerSkillComponent>(Owner);
	auto* Slots = NewObject<UQuickSlotComponent>(Owner);
	Owner->AddInstanceComponent(Skills); Owner->AddInstanceComponent(Slots);
	FRPGRpcIngressTestAccess::SetBudget(*Skills, 2);
	FRPGRpcIngressTestAccess::SetBudget(*Slots, 2);
	for (int32 Index = 0; Index < 100; ++Index)
	{
		FRPGRpcIngressTestAccess::SkillRequest(*Skills, Index);
		FRPGRpcIngressTestAccess::SlotRequest(*Slots, Index);
	}
	TestEqual(TEXT("Five skill RPC variants share two attempts, even malformed"), FRPGRpcIngressTestAccess::Revision(*Skills), 2u);
	TestEqual(TEXT("Set/clear/item variants share budget before revision work"), FRPGRpcIngressTestAccess::Revision(*Slots), 1u);
	TestEqual(TEXT("Spam cannot grant skill points"), Skills->GetTotalSP(), 50);
	TestEqual(TEXT("Spam cannot spend skill points"), Skills->GetRemainingSP(), 50);
	auto* Other = NewObject<URPGPlayerSkillComponent>(F.World->SpawnActor<AActor>());
	FRPGRpcIngressTestAccess::SkillRequest(*Other, 0);
	TestEqual(TEXT("Another player's requests still run"), FRPGRpcIngressTestAccess::Revision(*Other), 1u);
	return true;
}

#endif
