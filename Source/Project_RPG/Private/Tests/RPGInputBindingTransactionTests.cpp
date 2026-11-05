#if WITH_DEV_AUTOMATION_TESTS
#include "Input/RPGInputBindingTransactions.h"
#include "DataAsset/Input/DataAsset_InputConfig.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "RPGGameplayTags.h"
#include "Settings/RPGGameUserSettings.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
	struct FInputHost final : IRPGInputBindingHost
	{
		APlayerController* Controller = nullptr;
		UDataAsset_InputConfig* Runtime = nullptr;
		UDataAsset_InputConfig* Source = nullptr;
		virtual APlayerController& GetInputBindingController() const override { return *Controller; }
		virtual UDataAsset_InputConfig* GetInputBindingConfig() const override { return Runtime; }
		virtual const UDataAsset_InputConfig* GetSourceInputConfig() const override { return Source; }
		virtual UInputMappingContext* GetRebuiltInputContext() const override { return nullptr; }
		virtual UQuickSlotComponent* GetInputQuickSlots() const override { return nullptr; }
		virtual URPGSkillWindowComponent* GetInputSkillWindow() const override { return nullptr; }
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGInputBindingTransactionsTest,
	"ProjectRPG.Input.Transactions.OwnerReentryAndSiblingBindings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGInputBindingTransactionsTest::RunTest(const FString&)
{
	TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>(GEngine));
	Instance->InitializeForMinimalNetRPC(FName(*FString::Printf(TEXT("/Temp/InputTransactions_%s"), *FGuid::NewGuid().ToString())));
	UWorld* World = Instance->GetWorld();
	if (!TestNotNull(TEXT("World"), World)) return false;
	auto* Viewport = NewObject<UGameViewportClient>(GEngine);
	Instance->GetWorldContext()->GameViewport = Viewport;
	Viewport->Init(*Instance->GetWorldContext(), Instance.Get(), false);
	ON_SCOPE_EXIT { Instance->Shutdown(); Viewport->DetachViewportClient(); GEngine->DestroyWorldContext(World); World->DestroyWorld(false); };
	FString PlayerError;
	auto* Player = Instance->CreateLocalPlayer(0, PlayerError, false);
	if (!TestNotNull(TEXT("Local player"), Player)) return false;
	FInputHost Host;
	Host.Controller = World->SpawnActor<APlayerController>();
	Host.Controller->SetPlayer(Player); World->AddController(Host.Controller);
	if (!TestNotNull(TEXT("Input subsystem"), Player->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())) return false;
	TStrongObjectPtr<UDataAsset_InputConfig> Source(NewObject<UDataAsset_InputConfig>());
	TStrongObjectPtr<UDataAsset_InputConfig> Runtime(NewObject<UDataAsset_InputConfig>(Host.Controller));
	Host.Source = Source.Get(); Host.Runtime = Runtime.Get();
	Source->DefaultMappingContext = NewObject<UInputMappingContext>(Source.Get());
	Runtime->DefaultMappingContext = NewObject<UInputMappingContext>(Runtime.Get());
	auto* Action = NewObject<UInputAction>(Source.Get());
	FWarriorInputActionConfig Binding;
	Binding.InputTag = RPGGameplayTags::InputTag_ShowSkillWindow; Binding.InputAction = Action;
	Source->NativeInputActions.Add(Binding); Runtime->NativeInputActions.Add(Binding);
	for (auto* Config : {Source.Get(), Runtime.Get()})
	{
		Config->DefaultMappingContext->MapKey(Action, EKeys::K);
		Config->DefaultMappingContext->MapKey(Action, EKeys::Gamepad_FaceButton_Top);
	}
	FRPGInputBindingTransactions Transactions(Host);
	FText Error;
	int32 PolicyCalls = 0;
	auto Validate = [&](FText&)
	{
		++PolicyCalls;
		FText NestedError;
		TestFalse(TEXT("Nested edit is rejected by this service's guard"),
			Transactions.ApplyKeyBindingInternal(Binding.InputTag, EKeys::K, EKeys::L, NestedError, false));
		FRPGKeyMapping Expected;
		Expected.InputTag = Binding.InputTag; Expected.DefaultKey = EKeys::K; Expected.Key = EKeys::J;
		TestFalse(TEXT("Default restoration shares the same transaction guard"),
			Transactions.TryRestoreDefaultKeyBindings({Expected}, [](const FRPGInputBindingRestoreView&, FText&) { return true; }, NestedError));
		return true;
	};
	TFunctionRef<bool(FText&)> Policy(Validate);
	TestTrue(TEXT("Edit runs through an input-only host without saving settings"),
		Transactions.ApplyKeyBindingInternal(Binding.InputTag, EKeys::K, EKeys::J, Error, false, &Policy));
	TestTrue(TEXT("Policy validates both callback boundaries"), PolicyCalls >= 2);
	TestEqual(TEXT("Live key updated"), Transactions.GetCurrentKeyForBinding(Binding.InputTag, EKeys::K), EKeys::J);
	TestEqual(TEXT("Sibling gamepad binding retained"), Runtime->DefaultMappingContext->GetMappings()[1].Key, EKeys::Gamepad_FaceButton_Top);
	TestEqual(TEXT("Authored key preserved"), Source->DefaultMappingContext->GetMappings()[0].Key, EKeys::K);
	auto Invalidate = [&](FText&) { Host.Runtime = nullptr; return true; };
	TFunctionRef<bool(FText&)> InvalidatingPolicy(Invalidate);
	TestFalse(TEXT("Changed host after policy cannot commit"),
		Transactions.ApplyKeyBindingInternal(Binding.InputTag, EKeys::K, EKeys::L, Error, false, &InvalidatingPolicy));
	Host.Runtime = Runtime.Get();
	TestEqual(TEXT("Rejected edit preserves last accepted key"), Runtime->DefaultMappingContext->GetMappings()[0].Key, EKeys::J);
	TestTrue(TEXT("Guard released after rejection"),
		Transactions.ApplyKeyBindingInternal(Binding.InputTag, EKeys::K, EKeys::K, Error, false));
	return true;
}
#endif
