#include "CoreMinimal.h"
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Component/UI/RPGInteractionProgressComponent.h"
#include "Component/UI/PlayerUIComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "Tests/RPGTestWorldCleanup.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
struct FInteractionFixture
{
    TStrongObjectPtr<UGameInstance> Instance{NewObject<UGameInstance>(GEngine)};
    UWorld* World = nullptr;
    APlayerController* PC = nullptr;
    APawn* Pawn = nullptr;
    AActor* Target = nullptr;
    UPlayerUIComponent* UI = nullptr;
    URPGInteractionProgressComponent* Producer = nullptr;
    bool Initialize()
    {
        Instance->InitializeForMinimalNetRPC(FName(*FString::Printf(TEXT("/Temp/InteractionProgress_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits))));
        World = Instance->GetWorld();
        if (!World) return false;
#if UE_SUPPORT_FOR_ACTOR_TICK_DISABLE
        World->EnableActorTickAndUserCallbacks(true);
#endif
        World->InitializeActorsForPlay(FURL());
        PC = World->SpawnActor<APlayerController>(); Pawn = World->SpawnActor<APawn>(); Target = World->SpawnActor<AActor>();
        PC->SetPlayer(NewObject<ULocalPlayer>(GEngine)); World->AddController(PC);
        PC->Possess(Pawn);
        UI = NewObject<UPlayerUIComponent>(Pawn); Pawn->AddInstanceComponent(UI); UI->RegisterComponent();
        Producer = NewObject<URPGInteractionProgressComponent>(PC); PC->AddInstanceComponent(Producer); Producer->RegisterComponent();
        Pawn->DispatchBeginPlay(); PC->DispatchBeginPlay(); Target->DispatchBeginPlay();
        return PC->IsLocalController();
    }
    ~FInteractionFixture() { Instance->Shutdown(); RPGTestWorld::Destroy(World); }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGInteractionDurationTest, "ProjectRPG.UI.InteractionProgress.DurationAndCompletion",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGInteractionDurationTest::RunTest(const FString&)
{
    FInteractionFixture F;
    if (!TestTrue(TEXT("Local producer fixture"), F.Initialize())) return false;
    F.Producer->BeginInteraction(F.Pawn, F.Target, FText::FromString(TEXT("Open")), 4.f);
    auto State = F.UI->GetActionProgressSnapshot();
    TestTrue(TEXT("Begins common interaction display"), State.bActive && State.Data.Kind == ERPGActionProgressKind::Interaction);
    TestEqual(TEXT("Remaining seconds initially"), State.DisplaySeconds, 4.f);
    F.Producer->UpdateInteraction(1.f);
    State = F.UI->GetActionProgressSnapshot();
    TestEqual(TEXT("Fraction follows elapsed producer time"), State.Fraction, .25f);
    TestEqual(TEXT("Remaining seconds update"), State.DisplaySeconds, 3.f);
    F.Producer->UpdateInteraction(10.f);
    TestTrue(TEXT("Full display waits for gameplay completion"), F.UI->GetActionProgressSnapshot().bActive);
    F.Producer->EndInteraction(ERPGActionProgressEndReason::Completed);
    State = F.UI->GetActionProgressSnapshot();
    TestTrue(TEXT("Gameplay completion ends only its display"), !State.bActive && State.EndReason == ERPGActionProgressEndReason::Completed);
    TestFalse(TEXT("Producer acquires no component Tick"), F.Producer->PrimaryComponentTick.bCanEverTick);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGInteractionReplacementTest, "ProjectRPG.UI.InteractionProgress.ReplacementOwnership",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGInteractionReplacementTest::RunTest(const FString&)
{
    FInteractionFixture F;
    if (!TestTrue(TEXT("Local producer fixture"), F.Initialize())) return false;
    F.Producer->BeginInteraction(F.Pawn, F.Target, FText::FromString(TEXT("Open")), 4.f);
    FRPGActionProgressData Skill; Skill.Kind = ERPGActionProgressKind::Casting; Skill.Label = FText::FromString(TEXT("New skill"));
    const auto Newer = F.UI->BeginActionProgress(F.Target, Skill);
    F.Producer->UpdateInteraction(1.f);
    F.Producer->EndInteraction(ERPGActionProgressEndReason::Cancelled);
    auto State = F.UI->GetActionProgressSnapshot();
    TestTrue(TEXT("Old interaction cannot hide newer skill"), State.bActive && State.Handle == Newer);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGInteractionSourceLostTest, "ProjectRPG.UI.InteractionProgress.TargetPawnAndComponentLifetime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGInteractionSourceLostTest::RunTest(const FString&)
{
    FInteractionFixture F;
    if (!TestTrue(TEXT("Local producer fixture"), F.Initialize())) return false;
    F.Producer->BeginInteraction(F.Pawn, F.Target, FText::GetEmpty(), 4.f);
    F.Target->Destroy();
    TestFalse(TEXT("Destroyed target ends display immediately"), F.UI->GetActionProgressSnapshot().bActive);
    TestEqual(TEXT("Target loss uses the public interruption contract"), F.UI->GetActionProgressSnapshot().EndReason, ERPGActionProgressEndReason::Interrupted);
    F.Target = F.World->SpawnActor<AActor>();
    F.Producer->BeginInteraction(F.Pawn, F.Target, FText::GetEmpty(), 4.f);
    F.PC->UnPossess();
    TestFalse(TEXT("Unpossess cannot retain old Pawn display"), F.UI->GetActionProgressSnapshot().bActive);
    F.PC->Possess(F.Pawn);
    F.Producer->BeginInteraction(F.Pawn, F.Target, FText::GetEmpty(), 4.f);
    F.Producer->UnregisterComponent();
    TestFalse(TEXT("Unregister clears owned display"), F.UI->GetActionProgressSnapshot().bActive);
    return true;
}
#endif
