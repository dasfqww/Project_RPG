#if WITH_DEV_AUTOMATION_TESTS
#include "Tests/RPGRidingDashMotionTests.h"
#include "Tests/RPGTestWorldCleanup.h"
#include "Riding/RPGRidingComponent.h"
#include "Riding/RPGRidingSessionComponent.h"
#include "Riding/RPGGameplayAbility_RidingDash.h"
#include "UI/ActionBar/RPGActionBarContext.h"
#include "UI/ActionBar/RPGActionBarWidget.h"
#include "Component/RPGInputComponent.h"
#include "Component/UI/QuickSlotComponent.h"
#include "Component/UI/RPGSkillWindowComponent.h"
#include "Controller/RPGPlayerController.h"
#include "DataAsset/Input/DataAsset_InputConfig.h"
#include "UI/RPGWindowInputState.h"
#include "RPGInputTags.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

namespace
{
    constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    struct FFixture
    {
        TStrongObjectPtr<UGameInstance> Instance{NewObject<UGameInstance>(GEngine)};
        UWorld* World = nullptr;
        UGameViewportClient* Viewport = nullptr;
        ARPGPlayerController* PC = nullptr;
        ARPGRidingDashTestPawn* Pawn = nullptr;
        URPGRidingComponent* Riding = nullptr;
        URPGRidingSessionComponent* Session = nullptr;
        URPGActionBarContext* Context = nullptr;
        URPGInputComponent* Input = nullptr;
        UDataAsset_InputConfig* Config = nullptr;
        const UDataAsset_InputConfig* Authored = nullptr;
        UInputAction* Action = nullptr;
        FDelegateHandle Activated;
        int32 Activations = 0;
        uint64 Frame = 0;
        bool Initialize()
        {
            Instance->InitializeStandalone(FName(*FString::Printf(TEXT("RidingDashInput_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits))));
            World = Instance->GetWorld();
            if (!World || !World->GetPhysicsScene()) return false;
            World->InitializeActorsForPlay(FURL()); World->BeginPlay(); World->GetWorldSettings()->NotifyBeginPlay();
            Viewport = NewObject<UGameViewportClient>(GEngine); Instance->GetWorldContext()->GameViewport = Viewport;
            Viewport->Init(*Instance->GetWorldContext(), Instance.Get(), false);
            FString Error; auto* Local = Instance->CreateLocalPlayer(0, Error, false);
            if (!Local) return false;
            auto* Floor = World->SpawnActor<AActor>(); auto* Box = NewObject<UBoxComponent>(Floor);
            Floor->SetRootComponent(Box); Floor->AddInstanceComponent(Box);
            Box->SetMobility(EComponentMobility::Static); Box->SetBoxExtent(FVector(3000,2000,50)); Box->SetRelativeLocation(FVector(0,0,-50));
            Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics); Box->SetCollisionResponseToAllChannels(ECR_Block); Box->RegisterComponent();
            PC = World->SpawnActor<ARPGPlayerController>(); PC->SetPlayer(Local); World->AddController(PC);
            auto* PS = World->SpawnActor<APlayerState>(); PC->SetPlayerState(PS); PS->SetOwner(PC);
            Session = NewObject<URPGRidingSessionComponent>(PS); Session->RegisterComponent();
            FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            Pawn = World->SpawnActor<ARPGRidingDashTestPawn>(FVector(0,0,200), FRotator::ZeroRotator, P);
            auto* Slots = NewObject<UQuickSlotComponent>(Pawn); Pawn->AddInstanceComponent(Slots); Slots->RegisterComponent();
            PC->Possess(Pawn); Context = Slots->GetActionBarContext();
            Pawn->ASC->InitAbilityActorInfo(Pawn, Pawn); Pawn->ASC->AddAttributeSetSubobject(Pawn->Attributes.Get());
            Pawn->Attributes->CurrentHealth.SetBaseValue(100); Pawn->Attributes->CurrentHealth.SetCurrentValue(100);
            Riding = Pawn->FindComponentByClass<URPGRidingComponent>(); RPGPrepareRidingMovementTestSession(*Session, *Riding);
            RPGUnprotectRidingDashTestSession(*Session);
            Pawn->GetCharacterMovement()->SetMovementMode(MOVE_Falling); for (int32 I=0; I<40; ++I) Tick();
            Authored = LoadObject<UDataAsset_InputConfig>(nullptr, TEXT("/Game/Blueprints/Character/Player/Input/DA_InputConfig.DA_InputConfig"));
            Action = Authored ? Authored->FindAbilityInputActionByTag(RPGGameplayTags::InputTag_Roll) : nullptr;
            if (!Action || !Authored->DefaultMappingContext) return false;
            Config = NewObject<UDataAsset_InputConfig>(PC); Config->DefaultMappingContext = NewObject<UInputMappingContext>(Config);
            Config->NativeInputActions = Authored->NativeInputActions;
            for (const auto& Row : Authored->AbilityInputActions) if (Row.InputTag == RPGGameplayTags::InputTag_Roll) Config->AbilityInputActions.Add(Row);
            for (const auto& Mapping : Authored->DefaultMappingContext->GetMappings()) Config->DefaultMappingContext->MapKey(Mapping.Action, Mapping.Key) = Mapping;
            auto* Property = FindFProperty<FObjectProperty>(PC->GetClass(), TEXT("InputConfigDataAsset"));
            if (!Property) return false;
            Property->SetObjectPropertyValue_InContainer(PC, Config);
            Input = NewObject<URPGInputComponent>(PC); PC->AddInstanceComponent(Input); Input->RegisterComponent();
            Input->BindAbilityInputAction(Config, PC, &ARPGPlayerController::Input_AbilityInputPressed, &ARPGPlayerController::Input_AbilityInputReleased);
            Activated = Pawn->ASC->AbilityActivatedCallbacks.AddLambda([this](UGameplayAbility* A) { if (A && A->IsA<URPGGameplayAbility_RidingDash>()) ++Activations; });
            return Pawn->IsLocallyControlled() && Pawn->GetCharacterMovement()->IsMovingOnGround();
        }
        bool Mount() { Riding->RequestSetMounted(true, {}); return Context->HasOverride() && Riding->CanRequestDash(); }
        int32 Dispatch(ETriggerEvent Event)
        {
            int32 Count = 0; const FInputActionInstance Value(Action);
            for (const auto& B : Input->GetActionEventBindings())
                if (B->GetAction() == Action && B->GetTriggerEvent() == Event) { B->Execute(Value); ++Count; }
            return Count;
        }
        void Tick()
        { Frame = FMath::Max(Frame,GFrameCounter)+1; TGuardValue<uint64> Guard(GFrameCounter, Frame); World->Tick(LEVELTICK_All, 1.f/60); }
        ~FFixture()
        {
            if (Pawn && Pawn->ASC) Pawn->ASC->AbilityActivatedCallbacks.Remove(Activated);
            Instance->Shutdown(); if (Viewport) Viewport->DetachViewportClient(); RPGTestWorld::Destroy(World);
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingDashInputRouting, "ProjectRPG.Riding.DashInput.RoutingAndHeldTransitions", Flags)
bool FRidingDashInputRouting::RunTest(const FString&)
{
    FFixture F; if (!TestTrue(TEXT("Local controller, authored input and real physics fixture"), F.Initialize())) return false;
    const auto Roll = RPGGameplayTags::InputTag_Roll.GetTag();
    TestTrue(TEXT("OnFoot dodge tag passes through the action bar"), !F.Context->PressTag(Roll));
    if (!TestTrue(TEXT("Current approved mount"), F.Mount())) return false;
    const auto* Dash = F.Context->GetProfile()->Actions.FindByPredicate([](const auto& E){return E.ActionId == TEXT("Dash");});
    if (!TestNotNull(TEXT("Native Riding Dash entry"), Dash)) return false;
    TestTrue(TEXT("Dash uses Roll tag without stealing a combat slot"), Dash->InputTag == Roll && Dash->SkillSlot == INDEX_NONE && Dash->DisplaySlot == 0);
    TestEqual(TEXT("Started binding executes once"), F.Dispatch(ETriggerEvent::Started), 1);
    TestEqual(TEXT("Held foot dodge cannot start Dash after mounting"), F.Activations, 0);
    F.Dispatch(ETriggerEvent::Completed); F.Dispatch(ETriggerEvent::Started);
    if (!TestTrue(TEXT("Fresh dodge input reaches actual native GAS and server motion"), F.Activations == 1 && F.Riding->IsServerDashActive())) return false;
    TestEqual(TEXT("No Triggered repeat binding"), F.Dispatch(ETriggerEvent::Triggered), 0);
    F.Dispatch(ETriggerEvent::Started); TestEqual(TEXT("Held duplicate does not retrigger"), F.Activations, 1);
    F.Dispatch(ETriggerEvent::Canceled);
    for (int32 I=0; I<80 && F.Riding->IsServerDashActive(); ++I) F.Tick();
    TestTrue(TEXT("Key-dispatched native RMS moves forward within budget"), F.Pawn->GetActorLocation().X > 800 && F.Pawn->GetActorLocation().X <= 900.1);
    F.Dispatch(ETriggerEvent::Started); TestEqual(TEXT("Cooldown input cannot replay Dash or foot dodge"), F.Activations, 1);
    F.Riding->RequestSetMounted(false, {});
    TestFalse(TEXT("Dismount restores class bar"), F.Context->HasOverride());
    TestTrue(TEXT("Held Dash remains consumed across dismount"), F.Context->PressTag(Roll));
    F.Dispatch(ETriggerEvent::Completed);
    TestFalse(TEXT("Fresh foot dodge input passes through after release"), F.Context->PressTag(Roll)); F.Context->ReleaseTag(Roll);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingDashInputMapping, "ProjectRPG.Riding.DashInput.MappingAndWindows", Flags)
bool FRidingDashInputMapping::RunTest(const FString&)
{
    // First real controller construction loads the project's existing example/third-party assets.
    // Match only the 139 baseline asset diagnostics; input/GAS/physics warnings remain failures.
    AddExpectedMessage(TEXT("BP_ExperienceReadyComponentExample.*missing NodeGuid"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 3);
    AddExpectedMessage(TEXT("/Game/Characters/Mannequins/Rigs/CR_Mannequin_BasicFootIK"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
    AddExpectedMessage(TEXT("(?:/Game/M5VFXVOL2/BluePrints/Niagara_BP/Nbuilding_DM_Blueprint|Nbuilding_DM_Blueprint\\.uasset)"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 126);
    AddExpectedMessage(TEXT("ThirdPersonCharacter_SF\\.uasset:.*Input"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 7);
    AddExpectedMessage(TEXT("FMemberReference::ResolveMember \\((addfire|Chunks_numbers)\\)"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 2);
    FFixture F; if (!TestTrue(TEXT("Local fixture"), F.Initialize()) || !TestTrue(TEXT("Mounted"), F.Mount())) return false;
    TestTrue(TEXT("Authored dodge mapping defaults to Space"), F.Authored->DefaultMappingContext->GetMappings().ContainsByPredicate([&](const auto& M){return M.Action == F.Action && M.Key == EKeys::SpaceBar;}));
    TestEqual(TEXT("HUD reflects live Space mapping"), F.Context->ReadAction(TEXT("Dash")).KeyLabel.ToString(), EKeys::SpaceBar.GetDisplayName(false).ToString());
    F.Config->DefaultMappingContext->UnmapAllKeysFromAction(F.Action); F.Config->DefaultMappingContext->MapKey(F.Action, EKeys::K);
    TestEqual(TEXT("HUD follows remapped key without rebuilding profile"), F.Context->ReadAction(TEXT("Dash")).KeyLabel.ToString(), EKeys::K.GetDisplayName(false).ToString());
    TestTrue(TEXT("Authored mapping remains unchanged"), F.Authored->DefaultMappingContext->GetMappings().ContainsByPredicate([&](const auto& M){return M.Action == F.Action && M.Key == EKeys::SpaceBar;}));
    auto* WindowComponent = F.PC->FindComponentByClass<URPGSkillWindowComponent>();
    TStrongObjectPtr<URPGActionBarWidget> Window(CreateWidget<URPGActionBarWidget>(F.PC));
    if (!TestTrue(TEXT("Actual window boundary"), WindowComponent && Window.IsValid())) return false;
    const auto Windows = WindowComponent->GetWindowInputState(); Windows->BeginWindow(*F.PC, *Window.Get());
    F.Dispatch(ETriggerEvent::Started); TestEqual(TEXT("Open window blocks actual controller dispatch"), F.Activations, 0);
    TestFalse(TEXT("Open window blocks pointer availability too"), F.Context->ReadAction(TEXT("Dash")).bAvailable);
    F.Dispatch(ETriggerEvent::Canceled); Windows->EndWindow(*Window.Get());
    F.Dispatch(ETriggerEvent::Started);
    TestTrue(TEXT("Fresh remapped action still reaches native Dash"), F.Activations == 1 && F.Riding->IsServerDashActive());
    F.Dispatch(ETriggerEvent::Canceled);
    return true;
}
#endif
