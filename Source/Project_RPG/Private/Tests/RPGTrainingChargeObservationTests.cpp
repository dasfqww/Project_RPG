#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Animation/AnimMontage.h"
#include "Component/Progression/RPGTrainingObjectiveComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Skill/RPGSkillExecutionPolicy.h"
#include "Skill/RPGSkillExecutionTypes.h"
#include "Skill/RPGSkillRuntimeTypes.h"
#include "Skill/RPGSkillTargetingTypes.h"
#include "Tests/RPGTestWorldCleanup.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/Package.h"

namespace
{
	struct FChargeHost final : IRPGSkillExecutionHost
	{
		UWorld* World = nullptr;
		FRPGSkillRuntimeSpec Spec;
		FRPGSkillTargetResult Target;
		bool bCancelled = false;
		bool bTargetValid = true;
		bool bMontageValid = true;
		int32 Releases = 0;
		virtual UWorld* GetSkillExecutionWorld() const override { return World; }
		virtual const FRPGSkillRuntimeSpec& GetSkillRuntimeSpec() const override { return Spec; }
		virtual const FRPGSkillTargetResult& GetSkillTargetResult() const override { return Target; }
		virtual bool RefreshSkillTarget() override { return bTargetValid; }
		virtual bool IsSkillInputPressed() const override { return true; }
		virtual bool PlaySkillMontage(FName) override { return true; }
		virtual bool JumpToSkillMontageSection(FName) override { ++Releases; return bMontageValid; }
		virtual void FinishSkillExecution(bool bWasCancelled) override { bCancelled = bWasCancelled; }
		virtual void ShowSkillProgress() override {}
		virtual void HideSkillProgress() override {}
		virtual void UpdateSkillProgress(float, float) override {}
		virtual void NotifySkillProgressCompleted() override {}
		virtual void StartSkillPersistentVFX() override {}
		virtual void StopSkillPersistentVFX() override {}
	};
	struct FFixture
	{
		FChargeHost Host;
		uint64 OriginalFrame = GFrameCounter;
		TStrongObjectPtr<URPGSkillExecutionPolicy_Charge> Policy{NewObject<URPGSkillExecutionPolicy_Charge>()};
		TStrongObjectPtr<UAnimMontage> Montage{NewObject<UAnimMontage>()};
		FFixture()
		{
			Host.World = UWorld::CreateWorld(EWorldType::Game, false, MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()));
			if (Host.World)
			{
				GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(Host.World);
				Host.World->InitializeActorsForPlay(FURL()); Host.World->BeginPlay();
				// Timer registration distinguishes a ticked frame from pending next-frame timers.
				Host.World->Tick(LEVELTICK_All, .001f);
			}
			Configure(); Policy->Initialize(Host);
		}
		void Configure(int32 Minimum = 0)
		{
			Host.Spec.Reset(); Host.Spec.Montage = Montage.Get();
			Host.Spec.ExecutionPolicyClass = URPGSkillExecutionPolicy_Charge::StaticClass();
			FRPGSkillChargeExecutionConfig Config;
			Config.MaxChargeLevel = 3; Config.ChargeTimePerLevel = .1f;
			Config.MinimumReleaseLevel = Minimum; Config.MaxChargeHoldTime = 10.f;
			Config.ChargeSection = TEXT("Charge"); Config.ReleaseSection = TEXT("Release");
			Host.Spec.ExecutionConfig.InitializeAs<FRPGSkillChargeExecutionConfig>(Config);
			Host.bCancelled = false; Host.bTargetValid = Host.bMontageValid = true; Host.Releases = 0;
		}
		void Advance(float Seconds, bool bStopTimer = true)
		{
			if (bStopTimer) Host.World->GetTimerManager().ClearAllTimersForObject(Policy.Get());
			++GFrameCounter;
			Host.World->Tick(LEVELTICK_All, Seconds);
		}
		~FFixture()
		{
			Policy->EndExecution();
			if (Host.World) RPGTestWorld::Destroy(Host.World);
			GFrameCounter = OriginalFrame;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGTrainingDefaultChargeReleaseTest,
	"ProjectRPG.Progression.TrainingCharge.DefaultReleaseAndReset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGTrainingDefaultChargeReleaseTest::RunTest(const FString&)
{
	FFixture F;
	if (!TestNotNull(TEXT("Isolated timer world"), F.Host.World)) return false;
	TestEqual(TEXT("Observer value changes preserve the reflected class allocation size"),
		URPGTrainingObjectiveComponent::StaticClass()->GetPropertiesSize(), static_cast<int32>(sizeof(URPGTrainingObjectiveComponent)));
	TestTrue(TEXT("Default partial-release charge can start without a tripod"), F.Policy->StartExecution());
	F.Advance(.15f); F.Policy->OnInputReleased();
	TestEqual(TEXT("Early base release still executes normally"), F.Host.Releases, 1);
	TestFalse(TEXT("Base partial release is not incorrectly cancelled"), F.Host.bCancelled);
	TestFalse(TEXT("Partial release is not a full-charge observation"), F.Policy->WasReleasedAtMaximumCharge());
	F.Policy->EndExecution(); F.Host.Spec.Reset();
	TestFalse(TEXT("Reset cannot turn a partial release into maximum"), F.Policy->WasReleasedAtMaximumCharge());
	F.Configure();
	TestTrue(TEXT("Second activation starts"), F.Policy->StartExecution());
	F.Advance(.31f);
	TestEqual(TEXT("No timer update pre-populates the final level"), F.Policy->GetCurrentChargeLevel(), 0);
	F.Policy->OnInputReleased();
	TestTrue(TEXT("Release itself observes the maximum between timer updates"), F.Policy->WasReleasedAtMaximumCharge());
	F.Policy->EndExecution(); F.Host.Spec.Reset();
	TestTrue(TEXT("Actual maximum release survives the ability's spec-reset-before-end-listeners order"), F.Policy->WasReleasedAtMaximumCharge());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGTrainingChargeLifetimeTest,
	"ProjectRPG.Progression.TrainingCharge.CancellationReuseAndValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGTrainingChargeLifetimeTest::RunTest(const FString&)
{
	FFixture F;
	if (!TestNotNull(TEXT("Isolated timer world"), F.Host.World)) return false;
	TestTrue(TEXT("Start charge"), F.Policy->StartExecution());
	F.Advance(.31f, false);
	TestEqual(TEXT("Timer reaches maximum while input remains held"), F.Policy->GetCurrentChargeLevel(), 3);
	TestFalse(TEXT("Holding at maximum is not a release"), F.Policy->WasReleasedAtMaximumCharge());
	F.Policy->CancelExecution();
	TestFalse(TEXT("Cancellation before release has no receipt"), F.Policy->WasReleasedAtMaximumCharge());
	F.Configure(3); TestTrue(TEXT("Existing full-only recipe can start"), F.Policy->StartExecution());
	F.Advance(.15f); F.Policy->OnInputReleased();
	TestTrue(TEXT("Full-only recipe retains early cancellation"), F.Host.bCancelled);
	TestFalse(TEXT("Cancelled early full-only release is incomplete"), F.Policy->WasReleasedAtMaximumCharge());
	F.Policy->EndExecution(); F.Configure();
	TestTrue(TEXT("Begin maximum release with failed target"), F.Policy->StartExecution());
	F.Advance(.31f); F.Host.bTargetValid = false; F.Policy->OnInputReleased();
	TestTrue(TEXT("Maximum release fact and cancellation remain distinct"), F.Policy->WasReleasedAtMaximumCharge() && F.Host.bCancelled);
	F.Policy->EndExecution(); F.Configure();
	F.Host.Spec.ExecutionConfig.GetMutable<FRPGSkillChargeExecutionConfig>().ChargeTimePerLevel = 0.f;
	TestFalse(TEXT("Invalid next attempt is rejected"), F.Policy->StartExecution());
	TestFalse(TEXT("Failed restart cannot expose the old release receipt"), F.Policy->WasReleasedAtMaximumCharge());
	F.Configure(); TestTrue(TEXT("New valid attempt"), F.Policy->StartExecution());
	TestFalse(TEXT("New attempt starts without an old receipt"), F.Policy->WasReleasedAtMaximumCharge());
	return true;
}
#endif
