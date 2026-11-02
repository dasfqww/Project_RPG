#include "CoreMinimal.h"
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Component/RPGInputComponent.h"
#include "Component/UI/PlayerUIComponent.h"
#include "Component/UI/RPGInteractionProgressComponent.h"
#include "Controller/RPGPlayerController.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/Pawn.h"
#include "InputAction.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "RPGInputTags.h"
#include "Tests/RPGTestWorldCleanup.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGInteractionInputTest, "ProjectRPG.UI.InteractionInput.ReflectionAndReleaseRouting",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGInteractionInputTest::RunTest(const FString&)
{
    TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>(GEngine));
    Instance->InitializeForMinimalNetRPC(FName(*FString::Printf(TEXT("/Temp/InteractionInput_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits))));
    UWorld* World = Instance->GetWorld();
    if (!TestNotNull(TEXT("World"), World)) return false;
    ON_SCOPE_EXIT { Instance->Shutdown(); RPGTestWorld::Destroy(World); };
    // Actor ProcessEvent rejects even native functions before this world is initialized.
    World->InitializeActorsForPlay(FURL());
    auto* PC = World->SpawnActor<ARPGPlayerController>();
    auto* Pawn = World->SpawnActor<APawn>();
    auto* Target = World->SpawnActor<AActor>();
    if (!TestTrue(TEXT("Actors"), PC && Pawn && Target)) return false;
    auto* Player = NewObject<ULocalPlayer>(GEngine);
    PC->Player = Player; Player->PlayerController = PC;
    PC->SetAsLocalPlayerController(); PC->SetPawn(Pawn);
    auto* UI = NewObject<UPlayerUIComponent>(Pawn);
    Pawn->AddInstanceComponent(UI); UI->RegisterComponent();
    auto* Producer = PC->FindComponentByClass<URPGInteractionProgressComponent>();
    auto* Input = NewObject<URPGInputComponent>(PC);
    auto* Config = NewObject<UDataAsset_InputConfig>(PC);
    auto* Action = NewObject<UInputAction>(Config);
    FWarriorInputActionConfig Entry; Entry.InputTag = RPGGameplayTags::InputTag_PickUp_Items; Entry.InputAction = Action;
    Config->NativeInputActions.Add(Entry);
    Input->BindNativeInputActions(Config, PC);
    const auto& Bindings = Input->GetActionEventBindings();
    if (!TestEqual(TEXT("Nested pickup tag binds press, release and cancellation"), Bindings.Num(), 3)) return false;
    auto Dispatch = [&](ETriggerEvent Event)
    {
        int32 Count = 0;
        // Deliberately empty action data: button routing must not depend on raw Started/Completed values.
        const FInputActionInstance Empty(Action);
        for (const auto& Binding : Bindings)
            if (Binding->GetAction() == Action && Binding->GetTriggerEvent() == Event) { Binding->Execute(Empty); ++Count; }
        return Count;
    };
    Producer->BeginInteraction(Pawn, Target, FText::FromString(TEXT("Input fixture")), 2.f);
    if (!TestTrue(TEXT("Presentation seed active"), UI->GetActionProgressSnapshot().bActive)) return false;
    TestEqual(TEXT("Started runs once"), Dispatch(ETriggerEvent::Started), 1);
    TestTrue(TEXT("Started receives true, not an empty action value that would cancel"), UI->GetActionProgressSnapshot().bActive);
    TestEqual(TEXT("No repeated Triggered interaction"), Dispatch(ETriggerEvent::Triggered), 0);
    TestEqual(TEXT("Completed routes release"), Dispatch(ETriggerEvent::Completed), 1);
    TestTrue(TEXT("Real controller EndInteract cancels the display"), !UI->GetActionProgressSnapshot().bActive
        && UI->GetActionProgressSnapshot().EndReason == ERPGActionProgressEndReason::Cancelled);
    Producer->BeginInteraction(Pawn, Target, FText::FromString(TEXT("Cancellation fixture")), 2.f);
    TestEqual(TEXT("Canceled also routes release"), Dispatch(ETriggerEvent::Canceled), 1);
    TestTrue(TEXT("Cancelled input cannot retain interaction UI"), !UI->GetActionProgressSnapshot().bActive
        && UI->GetActionProgressSnapshot().EndReason == ERPGActionProgressEndReason::Cancelled);
    return true;
}
#endif
