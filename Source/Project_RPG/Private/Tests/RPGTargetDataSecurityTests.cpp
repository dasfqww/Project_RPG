#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/RPGTargetDataSecurityTestFixture.h"
#include "Abilities/GameplayAbilityTargetTypes.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Security/RPGTargetDataAdmission.h"
#include "Skill/RPGSkillExecutionPolicy.h"
#include "Skill/RPGSkillTargetingTypes.h"
#include <limits>

namespace
{
	FPredictionKey Key(const int16 Value)
	{
		FPredictionKey Result;
		Result.Current = Value;
		return Result;
	}

	FGameplayAbilityTargetDataHandle AimData()
	{
		FRPGSkillTargetResult Result;
		Result.bIsValid = true;
		Result.SourceLocation = FVector::ZeroVector;
		Result.TargetLocation = FVector(500.0, 0.0, 0.0);
		Result.AimDirection = FVector::ForwardVector;
		return FRPGSkillTargetDataCodec::Encode(Result);
	}

	struct FAdmissionWorld
	{
		UWorld* World = nullptr;
		FAdmissionWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false,
				MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()));
			if (!World) return;
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->GetWorldSettings()->NotifyBeginPlay();
		}
		~FAdmissionWorld()
		{
			if (World)
			{
				World->DestroyWorld(false);
				GEngine->DestroyWorldContext(World);
			}
		}
		URPGTargetDataSecurityTestASC* MakeASC()
		{
			AActor* Owner = World->SpawnActor<AActor>();
			if (!Owner) return nullptr;
			auto* ASC = NewObject<URPGTargetDataSecurityTestASC>(Owner);
			Owner->AddInstanceComponent(ASC);
			ASC->RegisterComponent();
			ASC->InitAbilityActorInfo(Owner, Owner);
			return ASC;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGTargetDataSequenceTest,
	"ProjectRPG.Security.TargetData.OrderedPredictionKeys",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGTargetDataSequenceTest::RunTest(const FString&)
{
	FRPGTargetDataRequestSequence Sequence;
	TestFalse(TEXT("Zero token rejected"), Sequence.TryAccept(0, false));
	TestTrue(TEXT("First confirmation accepted"), Sequence.TryAccept(100, false));
	TestFalse(TEXT("Same token cannot confirm twice"), Sequence.TryAccept(100, false));
	TestTrue(TEXT("Cancellation may share its confirmation token"), Sequence.TryAccept(100, true));
	TestFalse(TEXT("Cancellation cannot repeat"), Sequence.TryAccept(100, true));
	TestTrue(TEXT("Later retarget accepted"), Sequence.TryAccept(103, false));
	TestFalse(TEXT("Reverse-order request rejected"), Sequence.TryAccept(102, false));
	TestFalse(TEXT("Rejected reverse request cannot reset sequence"), Sequence.TryAccept(103, false));
	Sequence = FRPGTargetDataRequestSequence();
	TestTrue(TEXT("High initial token accepted"), Sequence.TryAccept(32767, false));
	TestTrue(TEXT("15-bit wrap accepted"), Sequence.TryAccept(1, false));
	TestFalse(TEXT("Pre-wrap replay rejected"), Sequence.TryAccept(32767, false));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGTargetDataWindowTest,
	"ProjectRPG.Security.TargetData.BoundedRequestWindow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGTargetDataWindowTest::RunTest(const FString&)
{
	FRPGTargetDataRequestWindow Window;
	TestTrue(TEXT("First request accepted"), Window.TryConsume(10.0, 2));
	TestTrue(TEXT("Second request accepted"), Window.TryConsume(10.1, 2));
	for (int32 Index = 0; Index < 10000; ++Index)
	{
		if (Window.TryConsume(10.9, 2)) { AddError(TEXT("Flood passed request cap")); break; }
	}
	TestEqual(TEXT("Rejected burst does not grow counters"), Window.Requests, 2);
	TestTrue(TEXT("Next server window resumes"), Window.TryConsume(11.0, 2));
	TestFalse(TEXT("NaN clock rejected"), Window.TryConsume(std::numeric_limits<double>::quiet_NaN(), 2));
	TestFalse(TEXT("Negative clock rejected"), Window.TryConsume(-1.0, 2));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGTargetDataRuntimeAdmissionTest,
	"ProjectRPG.Security.TargetData.RuntimeAdmission",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGTargetDataRuntimeAdmissionTest::RunTest(const FString&)
{
	FAdmissionWorld Fixture;
	if (!TestNotNull(TEXT("Admission world"), Fixture.World)) return false;
	auto* ASC = Fixture.MakeASC();
	auto* OtherASC = Fixture.MakeASC();
	if (!TestNotNull(TEXT("Admission ASC"), ASC) || !TestNotNull(TEXT("Other ASC"), OtherASC)) return false;
	const auto Handle = ASC->GiveAbility(FGameplayAbilitySpec(URPGTargetDataSecurityTestAbility::StaticClass(), 1));
	const FPredictionKey Original = Key(50);
	const auto Data = AimData();
	ASC->Submit(Handle, Original, Data, Key(51));
	TestFalse(TEXT("Inactive ability cannot allocate GAS cache"), ASC->HasCache(Handle, Original));
	if (!TestTrue(TEXT("Predicted fixture activates"), ASC->InternalTryActivateAbility(Handle, Original))) return false;
	int32 Confirmations = 0;
	int32 Cancellations = 0;
	ASC->AbilityTargetDataSetDelegate(Handle, Original).AddLambda(
		[&](const FGameplayAbilityTargetDataHandle&, FGameplayTag)
		{
			++Confirmations;
			ASC->ConsumeClientReplicatedTargetData(Handle, Original);
			// Exercise reentrancy while GAS is broadcasting the first acceptance.
			ASC->Submit(Handle, Original, Data, Key(51));
		});
	ASC->AbilityTargetDataCancelledDelegate(Handle, Original).AddLambda([&]() { ++Cancellations; });
	ASC->Submit(Handle, Original, Data, Key(51));
	ASC->Submit(Handle, Original, Data, Key(51));
	TestEqual(TEXT("Duplicate/reentrant requests dispatch exactly once"), Confirmations, 1);
	ASC->Submit(Handle, Key(49), Data, Key(52));
	TestFalse(TEXT("Wrong activation cannot allocate cache"), ASC->HasCache(Handle, Key(49)));
	OtherASC->Submit(Handle, Original, Data, Key(52));
	OtherASC->SubmitCancellation(Handle, Original, Key(52));
	TestFalse(TEXT("A handle granted to another owner cannot allocate cache"), OtherASC->HasCache(Handle, Original));
	ASC->Submit(Handle, Original, Data, Key(54));
	ASC->Submit(Handle, Original, Data, Key(53));
	TestEqual(TEXT("Fresh retarget accepted, reversed request rejected"), Confirmations, 2);
	ASC->SubmitCancellation(Handle, Original, Key(54));
	ASC->SubmitCancellation(Handle, Original, Key(54));
	TestEqual(TEXT("Cancellation dispatches once"), Cancellations, 1);
	auto* Ability = Cast<URPGTargetDataSecurityTestAbility>(ASC->FindAbilitySpecFromHandle(Handle)->GetPrimaryInstance());
	Ability->Finish();
	ASC->Submit(Handle, Original, Data, Key(55));
	ASC->SubmitCancellation(Handle, Original, Key(55));
	TestFalse(TEXT("Ended activation replay cannot recreate GAS cache"), ASC->HasCache(Handle, Original));
	TestEqual(TEXT("Ended activation cannot dispatch damage/target callbacks"), Confirmations, 2);
	const FPredictionKey NextOriginal = Key(60);
	TestTrue(TEXT("Next activation starts"), ASC->InternalTryActivateAbility(Handle, NextOriginal));
	ASC->Submit(Handle, Original, Data, Key(61));
	TestFalse(TEXT("Old activation cannot recreate cache while next one is active"), ASC->HasCache(Handle, Original));
	ASC->Submit(Handle, NextOriginal, Data, Key(61));
	TestTrue(TEXT("New activation has independent sequence"), ASC->HasCache(Handle, NextOriginal));
	Ability->Finish();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGTargetDataPayloadAdmissionTest,
	"ProjectRPG.Security.TargetData.PayloadAndRateAdmission",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGTargetDataPayloadAdmissionTest::RunTest(const FString&)
{
	FAdmissionWorld Fixture;
	if (!TestNotNull(TEXT("Payload world"), Fixture.World)) return false;
	auto* ASC = Fixture.MakeASC();
	if (!TestNotNull(TEXT("Payload ASC"), ASC)) return false;
	const auto Handle = ASC->GiveAbility(FGameplayAbilitySpec(URPGTargetDataSecurityTestAbility::StaticClass(), 1));
	const auto Original = Key(70);
	if (!TestTrue(TEXT("Payload fixture activates"), ASC->InternalTryActivateAbility(Handle, Original))) return false;
	auto Data = AimData();
	FGameplayAbilityTargetDataHandle NullData;
	NullData.Data.Add(nullptr);
	ASC->Submit(Handle, Original, NullData, Key(71));
	TestFalse(TEXT("Null entry rejected before cache allocation"), ASC->HasCache(Handle, Original));
	FGameplayAbilityTargetDataHandle Oversized;
	for (int32 Index = 0; Index < 33; ++Index) Oversized.Add(new FGameplayAbilityTargetData_LocationInfo());
	ASC->Submit(Handle, Original, Oversized, Key(71));
	TestFalse(TEXT("Entry count bounded before cache allocation"), ASC->HasCache(Handle, Original));
	FGameplayAbilityTargetDataHandle ActorFlood;
	auto* Actors = new FGameplayAbilityTargetData_ActorArray();
	Actors->TargetActorArray.SetNum(65);
	ActorFlood.Add(Actors);
	ASC->Submit(Handle, Original, ActorFlood, Key(71));
	TestFalse(TEXT("Actor count bounded before copying/allocating GAS cache"), ASC->HasCache(Handle, Original));
	FGameplayAbilityTargetDataHandle NonFinite;
	auto* Hit = new FGameplayAbilityTargetData_SingleTargetHit();
	Hit->HitResult.ImpactPoint.X = std::numeric_limits<double>::infinity();
	NonFinite.Add(Hit);
	ASC->Submit(Handle, Original, NonFinite, Key(71));
	TestFalse(TEXT("Non-finite hit rejected before cache allocation"), ASC->HasCache(Handle, Original));
	ASC->Submit(Handle, Original, Data, Key(71));
	TestTrue(TEXT("Rejected malformed data cannot poison the next valid token"), ASC->HasCache(Handle, Original));
	CastChecked<URPGTargetDataSecurityTestAbility>(ASC->FindAbilitySpecFromHandle(Handle)->GetPrimaryInstance())->Finish();

	auto* LimitedASC = Fixture.MakeASC();
	LimitedASC->SetRequestCap(2);
	const auto LimitedHandle = LimitedASC->GiveAbility(FGameplayAbilitySpec(URPGTargetDataSecurityTestAbility::StaticClass(), 1));
	LimitedASC->InternalTryActivateAbility(LimitedHandle, Original);
	int32 Count = 0;
	LimitedASC->AbilityTargetDataSetDelegate(LimitedHandle, Original).AddLambda(
		[&](const FGameplayAbilityTargetDataHandle&, FGameplayTag) { ++Count; });
	LimitedASC->Submit(LimitedHandle, Original, Data, Key(71));
	LimitedASC->Submit(LimitedHandle, Original, Data, Key(71)); // duplicates also consume the window
	LimitedASC->Submit(LimitedHandle, Original, Data, Key(72));
	TestEqual(TEXT("Duplicate spam cannot bypass server request cap"), Count, 1);
	// Several ordinary frames: WorldSettings can clamp a single large delta.
	for (int32 Index = 0; Index < 12; ++Index) Fixture.World->Tick(LEVELTICK_All, 0.1f);
	LimitedASC->Submit(LimitedHandle, Original, Data, Key(72));
	TestEqual(TEXT("Legitimate input recovers after window rollover"), Count, 2);
	CastChecked<URPGTargetDataSecurityTestAbility>(LimitedASC->FindAbilitySpecFromHandle(LimitedHandle)->GetPrimaryInstance())->Finish();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGTargetDataLiteralCodecTest,
	"ProjectRPG.Security.TargetData.LiteralTransformCodec",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGTargetDataLiteralCodecTest::RunTest(const FString&)
{
	auto Data = AimData();
	FRPGSkillTargetResult Result;
	TestTrue(TEXT("Authored literal transform accepted"), FRPGSkillTargetDataCodec::Decode(Data, Result));
	auto* Locations = static_cast<FGameplayAbilityTargetData_LocationInfo*>(Data.Data[0].Get());
	Locations->SourceLocation.LocationType = EGameplayAbilityTargetingLocationType::ActorTransform;
	TestFalse(TEXT("Client-selected actor source transform rejected"), FRPGSkillTargetDataCodec::Decode(Data, Result));
	TestFalse(TEXT("Rejected decoding clears previous target"), Result.bIsValid);
	Locations->SourceLocation.LocationType = EGameplayAbilityTargetingLocationType::LiteralTransform;
	Locations->TargetLocation.LocationType = EGameplayAbilityTargetingLocationType::SocketTransform;
	TestFalse(TEXT("Client-selected socket target transform rejected"), FRPGSkillTargetDataCodec::Decode(Data, Result));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGSkillAuthoritativeTerminationTest,
	"ProjectRPG.Security.Skill.AuthoritativeTermination",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGSkillAuthoritativeTerminationTest::RunTest(const FString&)
{
	FAdmissionWorld Fixture;
	if (!TestNotNull(TEXT("Termination world"), Fixture.World)) return false;
	auto* ASC = Fixture.MakeASC();
	if (!TestNotNull(TEXT("Termination ASC"), ASC)) return false;
	const auto Handle = ASC->GiveAbility(FGameplayAbilitySpec(URPGSkillTerminationSecurityTestAbility::StaticClass(), 1));
	if (!TestTrue(TEXT("Policy fixture activates"), ASC->InternalTryActivateAbility(Handle, Key(80)))) return false;
	auto* Ability = CastChecked<URPGSkillTerminationSecurityTestAbility>(ASC->FindAbilitySpecFromHandle(Handle)->GetPrimaryInstance());
	TestFalse(TEXT("Instant execution does not consume input aim updates"), GetDefault<URPGSkillExecutionPolicy_Instant>()->RequiresInputTargetRefresh());
	TestTrue(TEXT("Charge release retains input aim updates"), GetDefault<URPGSkillExecutionPolicy_Charge>()->RequiresInputTargetRefresh());
	TestTrue(TEXT("Holding release retains input aim updates"), GetDefault<URPGSkillExecutionPolicy_Holding>()->RequiresInputTargetRefresh());
	TestFalse(TEXT("Instant fixture starts without an aim sample"), Ability->GetActiveSkillTargetResult().bIsValid);
	TestTrue(TEXT("Instant press still permits input replication"), Ability->PreReplicateAbilityInputPressed());
	TestTrue(TEXT("Instant release still permits input replication"), Ability->PreReplicateAbilityInputReleased());
	TestFalse(TEXT("Ignored input cannot resolve or send a new aim sample"), Ability->GetActiveSkillTargetResult().bIsValid);
	int32 Ends = 0;
	bool bLastEndCancelled = false;
	const FDelegateHandle EndedDelegate = ASC->OnAbilityEnded.AddLambda([&](const FAbilityEndedData& End)
	{
		if (End.AbilitySpecHandle == Handle) { ++Ends; bLastEndCancelled = End.bWasCancelled; }
	});
	ON_SCOPE_EXIT
	{
		ASC->OnAbilityEnded.Remove(EndedDelegate);
		Ability->Finish();
	};
	Ability->ArmRemoteInputWait();
	ASC->SubmitAbilityEnd(Handle, Ability->GetCurrentActivationInfo(), Key(81));
	TestTrue(TEXT("Client natural end cannot kill server policy or waiting input"), Ability->IsActive());
	TestEqual(TEXT("Ignored natural end cannot broadcast completion"), Ends, 0);
	ASC->Submit(Handle, Key(80), AimData(), Key(82));
	TestTrue(TEXT("Server still accepts target updates after predicted montage ends"), ASC->HasCache(Handle, Key(80)));
	FGameplayAbilityActivationInfo WrongActivation;
	WrongActivation.SetPredicting(Key(79));
	ASC->SubmitAbilityCancel(Handle, WrongActivation);
	TestTrue(TEXT("Wrong activation cannot cancel policy"), Ability->IsActive());
	ASC->SubmitAbilityCancel(Handle, Ability->GetCurrentActivationInfo());
	TestFalse(TEXT("Explicit matching cancellation remains available"), Ability->IsActive());
	TestEqual(TEXT("Cancellation ends once"), Ends, 1);
	TestTrue(TEXT("Explicit cancellation retains cancelled classification"), bLastEndCancelled);
	TestFalse(TEXT("Cancellation clears target cache"), ASC->HasCache(Handle, Key(80)));

	if (!TestTrue(TEXT("Server can start another policy"), ASC->InternalTryActivateAbility(Handle, Key(90)))) return false;
	Ability->ArmRemoteInputWait();
	Ability->Finish();
	TestFalse(TEXT("Server completion remains available"), Ability->IsActive());
	TestEqual(TEXT("Server completion dispatches once"), Ends, 2);
	TestFalse(TEXT("Server completion is not classified as cancellation"), bLastEndCancelled);

	// A control against the inherited engine implementation reproduces the race:
	// a natural remote end force-cancels an ability still waiting on remote input.
	if (!TestTrue(TEXT("Engine control activates"), ASC->InternalTryActivateAbility(Handle, Key(100)))) return false;
	Ability->ArmRemoteInputWait();
	ASC->SubmitEngineEndControl(Handle, Ability->GetCurrentActivationInfo(), Key(101));
	TestFalse(TEXT("Unprotected GAS natural end kills waiting policy"), Ability->IsActive());
	TestEqual(TEXT("Engine control ends once"), Ends, 3);
	TestTrue(TEXT("Unprotected natural end becomes forced cancellation"), bLastEndCancelled);

	const auto PlainHandle = ASC->GiveAbility(FGameplayAbilitySpec(URPGTargetDataSecurityTestAbility::StaticClass(), 1));
	if (!TestTrue(TEXT("Unrelated ability activates"), ASC->InternalTryActivateAbility(PlainHandle, Key(110)))) return false;
	auto* PlainAbility = ASC->FindAbilitySpecFromHandle(PlainHandle)->GetPrimaryInstance();
	// Standalone activation leaves remote termination disabled. Model the
	// confirmed remote activation whose inherited GAS behavior we are checking.
	PlainAbility->GetCurrentActivationInfoRef().bCanBeEndedByOtherInstance = true;
	ASC->SubmitAbilityEnd(PlainHandle, PlainAbility->GetCurrentActivationInfo(), Key(111));
	TestFalse(TEXT("Unrelated GAS natural-end behavior is unchanged"), PlainAbility->IsActive());
	return true;
}

#endif
