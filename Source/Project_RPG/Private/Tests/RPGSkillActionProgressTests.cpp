#include "CoreMinimal.h"
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Animation/AnimMontage.h"
#include "Component/UI/PlayerUIComponent.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "Skill/RPGSkillExecutionPolicy.h"
#include "Skill/RPGSkillExecutionTypes.h"
#include "Skill/RPGSkillRuntimeTypes.h"
#include "Skill/RPGSkillTargetingTypes.h"
#include "Skill/RPGSkillDefinition_Charge.h"
#include "DataTable/SkillData.h"
#include "UI/Progress/RPGActionProgressWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Widgets/SWidget.h"
#include "Tests/RPGTestWorldCleanup.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
struct FHost final : IRPGSkillExecutionHost
{
	UWorld* World = nullptr;
	APawn* Pawn = nullptr;
	FRPGSkillRuntimeSpec Spec;
	FRPGSkillTargetResult Target;
	bool bPlay = true;
	int32 LegacyCalls = 0, Plays = 0, Jumps = 0;
	bool bCancelled = false;
	virtual UWorld* GetSkillExecutionWorld() const override { return World; }
	virtual AActor* GetSkillSourceActor() const override { return Pawn; }
	virtual const FRPGSkillRuntimeSpec& GetSkillRuntimeSpec() const override { return Spec; }
	virtual const FRPGSkillTargetResult& GetSkillTargetResult() const override { return Target; }
	virtual bool RefreshSkillTarget() override { return true; }
	virtual bool IsSkillInputPressed() const override { return true; }
	virtual bool PlaySkillMontage(FName) override { ++Plays; return bPlay; }
	virtual bool JumpToSkillMontageSection(FName) override { ++Jumps; return true; }
	virtual void FinishSkillExecution(bool bCancel) override { bCancelled = bCancel; }
	virtual void ShowSkillProgress() override { ++LegacyCalls; }
	virtual void HideSkillProgress() override { ++LegacyCalls; }
	virtual void UpdateSkillProgress(float, float) override { ++LegacyCalls; }
	virtual void NotifySkillProgressCompleted() override { ++LegacyCalls; }
	virtual void StartSkillPersistentVFX() override {}
	virtual void StopSkillPersistentVFX() override {}
};
struct FFixture
{
	FHost Host;
	UPlayerUIComponent* UI = nullptr;
	TStrongObjectPtr<UAnimMontage> Montage{NewObject<UAnimMontage>()};
	TArray<TStrongObjectPtr<URPGSkillExecutionPolicy>> Policies;
	uint64 Frame = GFrameCounter;
	bool Initialize()
	{
		Host.World = UWorld::CreateWorld(EWorldType::Game, false, MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()));
		if (!Host.World) return false;
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(Host.World);
		Host.World->InitializeActorsForPlay(FURL()); Host.World->BeginPlay();
		auto* PC = Host.World->SpawnActor<APlayerController>();
		Host.Pawn = Host.World->SpawnActor<APawn>();
		if (!PC || !Host.Pawn) return false;
		auto* Player = NewObject<ULocalPlayer>(GEngine);
		PC->Player = Player; Player->PlayerController = PC;
		PC->SetAsLocalPlayerController(); Host.World->AddController(PC);
		PC->Possess(Host.Pawn);
		UI = NewObject<UPlayerUIComponent>(Host.Pawn);
		Host.Pawn->AddInstanceComponent(UI); UI->RegisterComponent();
		for (const FName Name : {FName(TEXT("Charge")), FName(TEXT("Release")), FName(TEXT("Hold")),
			FName(TEXT("Success")), FName(TEXT("Cast")), FName(TEXT("Done"))})
		{
			FCompositeSection Section; Section.SectionName = Name; Montage->CompositeSections.Add(Section);
		}
		Host.Spec.Montage = Montage.Get();
		Step(.001f);
		return Host.Pawn->IsLocallyControlled();
	}
	template<class T> T* Make()
	{
		auto* P = NewObject<T>(); P->Initialize(Host); Policies.Emplace(P); return P;
	}
	void Charge()
	{
		Host.Spec.Montage = Montage.Get();
		FRPGSkillChargeExecutionConfig C; C.ChargeTimePerLevel = .1f; C.MaxChargeLevel = 3;
		C.MaxChargeHoldTime = 10.f; C.ChargeSection = TEXT("Charge"); C.ReleaseSection = TEXT("Release");
		Host.Spec.ExecutionConfig.InitializeAs<FRPGSkillChargeExecutionConfig>(C);
	}
	void Cast(float Duration = .3f)
	{
		Host.Spec.Montage = Montage.Get();
		FRPGSkillCastingExecutionConfig C; C.CastDuration = Duration; C.CastingSection = TEXT("Cast");
		C.CompleteSection = TEXT("Done"); C.bCancelOnInputRelease = true;
		Host.Spec.ExecutionConfig.InitializeAs<FRPGSkillCastingExecutionConfig>(C);
	}
	void Step(float Seconds) { ++GFrameCounter; Host.World->Tick(LEVELTICK_All, Seconds); }
	~FFixture()
	{
		if (UI) UI->OnActionProgressChangedNative.Clear();
		for (auto& P : Policies) P->CancelExecution();
		RPGTestWorld::Destroy(Host.World); GFrameCounter = Frame;
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSkillProgressChargeTest, "ProjectRPG.UI.SkillActionProgress.ChargeReadyAndRelease",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSkillProgressChargeTest::RunTest(const FString&)
{
	FFixture F; if (!TestTrue(TEXT("Fixture"), F.Initialize())) return false;
	F.Charge(); auto* P = F.Make<URPGSkillExecutionPolicy_Charge>();
	TestTrue(TEXT("Real charge policy starts"), P->StartExecution());
	auto S = F.UI->GetActionProgressSnapshot();
	TestTrue(TEXT("Policy publishes a valid execution"), S.bActive && S.Handle.IsValid());
	TestEqual(TEXT("Charge kind"), S.Data.Kind, ERPGActionProgressKind::Charge);
	const auto Handle = S.Handle;
	F.Step(.35f); S = F.UI->GetActionProgressSnapshot();
	TestTrue(TEXT("Full charge remains active while held"), S.bActive && S.Handle == Handle);
	TestEqual(TEXT("Ready is not end"), S.Data.Phase, ERPGActionProgressPhase::Ready);
	TestEqual(TEXT("All charge stages shown"), S.Data.Stage, 3);
	P->OnInputReleased(); S = F.UI->GetActionProgressSnapshot();
	TestFalse(TEXT("Release ends only its display"), S.bActive);
	TestTrue(TEXT("Training release receipt preserved"), P->WasReleasedAtMaximumCharge());
	TestEqual(TEXT("Release section still entered"), F.Host.Jumps, 1);
	TestEqual(TEXT("No legacy broadcast path used"), F.Host.LegacyCalls, 0);
	P->EndExecution(); F.Host.Spec.Reset();
	TestTrue(TEXT("Release fact survives reset"), P->WasReleasedAtMaximumCharge());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSkillProgressModesTest, "ProjectRPG.UI.SkillActionProgress.HoldingAndCasting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSkillProgressModesTest::RunTest(const FString&)
{
	FFixture F; if (!TestTrue(TEXT("Fixture"), F.Initialize())) return false;
	FRPGSkillHoldingExecutionConfig C; C.HoldDuration = .5f; C.PerfectZoneStartTime = .2f;
	C.PerfectZoneEndTime = .4f; C.bAutoReleaseAtPerfectZoneEnd = false;
	C.HoldingSection = TEXT("Hold"); C.SuccessSection = TEXT("Success");
	F.Host.Spec.ExecutionConfig.InitializeAs<FRPGSkillHoldingExecutionConfig>(C);
	auto* H = F.Make<URPGSkillExecutionPolicy_Holding>();
	if (!TestTrue(TEXT("Holding starts"), H->StartExecution())) return false;
	F.Step(.25f);
	TestEqual(TEXT("Perfect window is ready"), F.UI->GetActionProgressSnapshot().Data.Phase, ERPGActionProgressPhase::Ready);
	H->OnInputReleased();
	TestFalse(TEXT("Holding resolution hides display"), F.UI->GetActionProgressSnapshot().bActive);
	H->EndExecution();
	F.Cast(); auto* Cast = F.Make<URPGSkillExecutionPolicy_Casting>();
	TestTrue(TEXT("Casting starts"), Cast->StartExecution()); F.Step(.1f);
	TestEqual(TEXT("Casting kind"), F.UI->GetActionProgressSnapshot().Data.Kind, ERPGActionProgressKind::Casting);
	Cast->OnInputReleased();
	TestEqual(TEXT("Input cancellation reason"), F.UI->GetActionProgressSnapshot().EndReason, ERPGActionProgressEndReason::Cancelled);
	Cast->CancelExecution();
	F.Cast(); auto* Complete = F.Make<URPGSkillExecutionPolicy_Casting>();
	TestTrue(TEXT("Next cast starts"), Complete->StartExecution()); F.Step(.35f);
	TestFalse(TEXT("Cast timer resolves display"), F.UI->GetActionProgressSnapshot().bActive);
	Complete->EndExecution();
	F.Host.bPlay = false; F.Cast(); auto* Failed = F.Make<URPGSkillExecutionPolicy_Casting>();
	TestFalse(TEXT("Montage failure remains gameplay failure"), Failed->StartExecution());
	TestEqual(TEXT("Failed start clears progress"), F.UI->GetActionProgressSnapshot().EndReason, ERPGActionProgressEndReason::Interrupted);
	TestEqual(TEXT("Modern modes do not use legacy progress"), F.Host.LegacyCalls, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSkillProgressHoldingCancelTest, "ProjectRPG.UI.SkillActionProgress.HoldingCancellationReason",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSkillProgressHoldingCancelTest::RunTest(const FString&)
{
	FFixture F; if (!TestTrue(TEXT("Fixture"), F.Initialize())) return false;
	FRPGSkillHoldingExecutionConfig C; C.HoldDuration = .5f; C.PerfectZoneStartTime = .2f;
	C.PerfectZoneEndTime = .4f; C.bAutoReleaseAtPerfectZoneEnd = false;
	C.HoldingSection = TEXT("Hold"); C.SuccessSection = TEXT("Success");
	F.Host.Spec.ExecutionConfig.InitializeAs<FRPGSkillHoldingExecutionConfig>(C);
	auto* Cancelled = F.Make<URPGSkillExecutionPolicy_Holding>();
	if (!TestTrue(TEXT("Unresolved holding starts"), Cancelled->StartExecution())) return false;
	F.Step(.1f);
	Cancelled->CancelExecution();
	TestFalse(TEXT("External cancellation hides holding"), F.UI->GetActionProgressSnapshot().bActive);
	TestEqual(TEXT("External cancellation is not successful completion"),
		F.UI->GetActionProgressSnapshot().EndReason, ERPGActionProgressEndReason::Cancelled);
	Cancelled->OnInputReleased();
	TestEqual(TEXT("Late input release preserves cancellation"),
		F.UI->GetActionProgressSnapshot().EndReason, ERPGActionProgressEndReason::Cancelled);

	auto* Next = F.Make<URPGSkillExecutionPolicy_Holding>();
	if (!TestTrue(TEXT("Replacement holding starts"), Next->StartExecution())) return false;
	const auto NextHandle = F.UI->GetActionProgressSnapshot().Handle;
	Cancelled->CancelExecution();
	TestTrue(TEXT("Repeated old cancellation preserves replacement"),
		F.UI->GetActionProgressSnapshot().bActive && F.UI->GetActionProgressSnapshot().Handle == NextHandle);
	F.Step(.25f);
	Next->OnInputReleased();
	TestEqual(TEXT("Successful release still completes normally"),
		F.UI->GetActionProgressSnapshot().EndReason, ERPGActionProgressEndReason::Completed);
	Next->CancelExecution();
	TestEqual(TEXT("Late cleanup cannot rewrite an already completed display"),
		F.UI->GetActionProgressSnapshot().EndReason, ERPGActionProgressEndReason::Completed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSkillProgressLifetimeTest, "ProjectRPG.UI.SkillActionProgress.HandoffAndReentrantCancel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSkillProgressLifetimeTest::RunTest(const FString&)
{
	FFixture F; if (!TestTrue(TEXT("Fixture"), F.Initialize())) return false;
	F.Charge(); auto* A = F.Make<URPGSkillExecutionPolicy_Charge>(); A->StartExecution();
	// Same ability host can be reused; each activation-local policy keeps its own handle.
	auto* B = F.Make<URPGSkillExecutionPolicy_Charge>(); B->StartExecution();
	const auto NewHandle = F.UI->GetActionProgressSnapshot().Handle;
	F.Step(.05f); A->CancelExecution();
	TestTrue(TEXT("Late old policy update/end cannot hide new policy"), F.UI->GetActionProgressSnapshot().bActive && F.UI->GetActionProgressSnapshot().Handle == NewHandle);
	B->CancelExecution();
	F.Charge(); auto* Reentrant = F.Make<URPGSkillExecutionPolicy_Charge>();
	const int32 OldPlays = F.Host.Plays;
	F.UI->OnActionProgressChangedNative.AddLambda([&]
	{
		if (F.UI->GetActionProgressSnapshot().bActive) { Reentrant->CancelExecution(); F.Host.Spec.Reset(); }
	});
	TestFalse(TEXT("Cancel during begin stops start"), Reentrant->StartExecution());
	TestFalse(TEXT("Returned begin handle is cleaned after reentrant cancel"), F.UI->GetActionProgressSnapshot().bActive);
	TestEqual(TEXT("No montage starts after cancellation"), F.Host.Plays, OldPlays);
	F.UI->OnActionProgressChangedNative.Clear();
	F.Charge(); auto* Ready = F.Make<URPGSkillExecutionPolicy_Charge>(); Ready->StartExecution();
	F.UI->OnActionProgressChangedNative.AddLambda([&]
	{
		if (F.UI->GetActionProgressSnapshot().bActive && F.UI->GetActionProgressSnapshot().Data.Phase == ERPGActionProgressPhase::Ready)
		{ Ready->CancelExecution(); F.Host.Spec.Reset(); }
	});
	const int32 Jumps = F.Host.Jumps;
	F.Step(.35f);
	TestFalse(TEXT("Ready callback cancellation clears display"), F.UI->GetActionProgressSnapshot().bActive);
	TestEqual(TEXT("No release after callback reset spec"), F.Host.Jumps, Jumps);
	TestFalse(TEXT("Cancellation did not invent a training release"), Ready->WasReleasedAtMaximumCharge());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSkillProgressNameTest, "ProjectRPG.UI.SkillActionProgress.FrozenDisplayName",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSkillProgressNameTest::RunTest(const FString&)
{
	FFixture F; if (!TestTrue(TEXT("Local fixture"), F.Initialize())) return false;
	TStrongObjectPtr<URPGSkillDefinition_Charge> Definition(NewObject<URPGSkillDefinition_Charge>());
	Definition->SkillName = FText::FromString(TEXT("Frozen skill name"));
	Definition->BuildRuntimeSpec(nullptr, FRPGSkillSaveData(), F.Host.Spec);
	Definition->SkillName = FText::FromString(TEXT("Changed definition"));
	F.Charge(); auto* P = F.Make<URPGSkillExecutionPolicy_Charge>();
	TestTrue(TEXT("Named execution starts"), P->StartExecution());
	TStrongObjectPtr<URPGActionProgressWidget> View(NewObject<URPGActionProgressWidget>(F.Host.World));
	if (!TestTrue(TEXT("Real progress view initializes"), View->Initialize())) return false;
	View->SetProgressSource(F.UI);
	const TSharedRef<SWidget> Slate = View->TakeWidget();
	auto* Label = Cast<UTextBlock>(View->WidgetTree->FindWidget(TEXT("ActionLabel")));
	if (!TestNotNull(TEXT("Action text control"), Label)) return false;
	TestEqual(TEXT("Frozen skill name reaches the actual UMG label"), Label->GetText().ToString(), FString(TEXT("Frozen skill name")));
	TestEqual(TEXT("Uses activation name rather than changed definition"), F.UI->GetActionProgressSnapshot().Data.Label.ToString(), FString(TEXT("Frozen skill name")));
	F.Host.Spec.DisplayName = FText::FromString(TEXT("New execution input"));
	F.Step(.05f);
	TestEqual(TEXT("Rendered text also retains activation name"), Label->GetText().ToString(), FString(TEXT("Frozen skill name")));
	TestEqual(TEXT("Updates retain original presentation snapshot"), F.UI->GetActionProgressSnapshot().Data.Label.ToString(), FString(TEXT("Frozen skill name")));
	P->CancelExecution(); F.Host.Spec.Reset();
	TestTrue(TEXT("Reset clears the display name"), F.Host.Spec.DisplayName.IsEmpty());
	F.Charge(); auto* Fallback = F.Make<URPGSkillExecutionPolicy_Charge>(); Fallback->StartExecution();
	TestEqual(TEXT("Unnamed producer uses localized kind label"), F.UI->GetActionProgressSnapshot().Data.Label.ToString(), Fallback->GetExecutionDisplayName().ToString());
	Fallback->CancelExecution();
	TStrongObjectPtr<UDataTable> Table(NewObject<UDataTable>());
	Table->RowStruct = FRPGSkillDataTable::StaticStruct();
	FRPGSkillDataTable Row; Row.SkillName = TEXT("Table skill name"); Table->AddRow(TEXT("Skill"), Row);
	Definition->SkillName = FText::GetEmpty(); Definition->SkillDataHandle.DataTable = Table.Get(); Definition->SkillDataHandle.RowName = TEXT("Skill");
	Definition->BuildRuntimeSpec(nullptr, FRPGSkillSaveData(), F.Host.Spec);
	TestEqual(TEXT("Legacy table name fills an empty authored name"), F.Host.Spec.DisplayName.ToString(), FString(TEXT("Table skill name")));
	return true;
}
#endif
