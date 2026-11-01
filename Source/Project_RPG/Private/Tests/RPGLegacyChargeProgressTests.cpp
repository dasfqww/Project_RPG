#include "CoreMinimal.h"
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Component/UI/PlayerUIComponent.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "Skill/RPGGameplayAbility_SkillContainer.h"
#include "Skill/RPGSkillAction_Charge.h"
#include "Skill/RPGSkillDefinition_Charge.h"
#include "Tests/RPGTestWorldCleanup.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/Package.h"

namespace
{
struct FFixture
{
	UWorld* World = nullptr;
	ACharacter* Pawn = nullptr;
	UPlayerUIComponent* UI = nullptr;
	FGameplayAbilityActorInfo ActorInfo;
	TStrongObjectPtr<URPGGameplayAbility_SkillContainer> Ability;
	TStrongObjectPtr<URPGSkillDefinition_Charge> Definition{NewObject<URPGSkillDefinition_Charge>()};
	TArray<TStrongObjectPtr<URPGSkillAction_Charge>> Actions;
	uint64 Frame = GFrameCounter;
	bool Initialize()
	{
		World = UWorld::CreateWorld(EWorldType::Game, false, MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()));
		if (!World) return false;
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL()); World->BeginPlay();
		auto* PC = World->SpawnActor<APlayerController>(); Pawn = World->SpawnActor<ACharacter>();
		if (!PC || !Pawn) return false;
		auto* Player = NewObject<ULocalPlayer>(GEngine); PC->Player = Player; Player->PlayerController = PC;
		PC->SetAsLocalPlayerController(); World->AddController(PC); PC->Possess(Pawn);
		auto* ASC = NewObject<URPGAbilitySystemComponent>(Pawn); Pawn->AddInstanceComponent(ASC); ASC->RegisterComponent();
		ASC->InitAbilityActorInfo(Pawn, Pawn); ActorInfo.InitFromActor(Pawn, Pawn, ASC);
		const auto Handle = ASC->GiveAbility(FGameplayAbilitySpec(URPGGameplayAbility_SkillContainer::StaticClass(), 1));
		const auto* Spec = ASC->FindAbilitySpecFromHandle(Handle);
		Ability.Reset(Spec ? Cast<URPGGameplayAbility_SkillContainer>(Spec->GetPrimaryInstance()) : nullptr);
		if (!Ability.IsValid()) return false;
		UI = NewObject<UPlayerUIComponent>(Pawn); Pawn->AddInstanceComponent(UI); UI->RegisterComponent();
		Definition->ChargeTimePerLevel = .1f; Definition->MaxChargeLevel = 3;
		Definition->SkillName = FText::FromString(TEXT("Legacy cast"));
		Step(.001f);
		return Pawn->IsLocallyControlled();
	}
	URPGSkillAction_Charge* Make()
	{
		auto* A = NewObject<URPGSkillAction_Charge>(Ability.Get());
		FRPGSkillRuntimeSpec Spec; // No montage/VFX: exercise real charge timers and UI without combat assets.
		Spec.DisplayName = Definition->SkillName;
		A->Initialize(Ability.Get(), Definition.Get(), Spec); Actions.Emplace(A); return A;
	}
	void Step(float Seconds){++GFrameCounter; World->Tick(LEVELTICK_All, Seconds);}
	~FFixture()
	{
		if (UI) UI->OnActionProgressChangedNative.Clear();
		for(auto& A : Actions) A->CancelAction();
		RPGTestWorld::Destroy(World); GFrameCounter = Frame;
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLegacyChargeProgressTest,"ProjectRPG.UI.LegacyChargeProgress.LifetimeAndHandoff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLegacyChargeProgressTest::RunTest(const FString&)
{
	FFixture F; if(!TestTrue(TEXT("Local GAS actor info"), F.Initialize())) return false;
	auto* A = F.Make();
	F.Definition->SkillName = FText::FromString(TEXT("Changed after snapshot"));
	A->StartAction();
	TestTrue(TEXT("Legacy action opens shared progress"), F.UI->GetActionProgressSnapshot().bActive);
	TestEqual(TEXT("Authored label"), F.UI->GetActionProgressSnapshot().Data.Label.ToString(), FString(TEXT("Legacy cast")));
	F.Step(.11f); F.Step(.11f); F.Step(.11f);
	TestEqual(TEXT("Full legacy charge stays ready"), F.UI->GetActionProgressSnapshot().Data.Phase, ERPGActionProgressPhase::Ready);
	TestEqual(TEXT("Stage matches legacy charge"), F.UI->GetActionProgressSnapshot().Data.Stage, 3);
	auto* B = F.Make(); B->StartAction(); const auto Handle = F.UI->GetActionProgressSnapshot().Handle;
	A->CancelAction();
	TestTrue(TEXT("Old action cancellation cannot hide new action"), F.UI->GetActionProgressSnapshot().bActive && F.UI->GetActionProgressSnapshot().Handle == Handle);
	B->EndAction(); TestFalse(TEXT("Normal action end clears progress"), F.UI->GetActionProgressSnapshot().bActive);
	auto* Released = F.Make(); Released->StartAction(); Released->OnInputReleased();
	TestFalse(TEXT("Input release clears only its action display"), F.UI->GetActionProgressSnapshot().bActive);
	Released->OnInputReleased();
	TestFalse(TEXT("Duplicate release cannot revive progress"), F.UI->GetActionProgressSnapshot().bActive);
	auto* Automatic = F.Make(); Automatic->StartAction();
	F.Step(.11f); F.Step(.11f); F.Step(.11f);
	TestEqual(TEXT("Automatic release begins from ready"), F.UI->GetActionProgressSnapshot().Data.Phase, ERPGActionProgressPhase::Ready);
	// Timers created inside a timer callback become pending until the next frame.
	for (int32 Frame = 0; Frame < 25; ++Frame) F.Step(.1f);
	TestFalse(TEXT("Automatic overcharge release closes display"), F.UI->GetActionProgressSnapshot().bActive);
	auto* C = F.Make();
	F.UI->OnActionProgressChangedNative.AddLambda([&]{if(F.UI->GetActionProgressSnapshot().bActive) C->CancelAction();});
	C->StartAction();
	TestFalse(TEXT("Cancel during begin does not orphan the pending handle"), F.UI->GetActionProgressSnapshot().bActive);
	F.UI->OnActionProgressChangedNative.Clear(); F.Step(.2f);
	TestFalse(TEXT("Cancelled timer never revives display"), F.UI->GetActionProgressSnapshot().bActive);
	return true;
}
#endif
