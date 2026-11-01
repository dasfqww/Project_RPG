#include "Component/UI/RPGSkillWindowComponent.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Component/Skill/RPGPlayerSkillComponent.h"
#include "Component/UI/QuickSlotComponent.h"
#include "DataAsset/Input/DataAsset_InputConfig.h"
#include "Engine/LocalPlayer.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "RPGInputTags.h"
#include "Settings/RPGGameUserSettings.h"
#include "UI/Skill/RPGSkillWindow.h"
#include "UI/RPGWindowInputState.h"
#include "UI/RPGWindowPlacement.h"
#include "UObject/StrongObjectPtr.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RPGSkillWindowComponent)

URPGSkillWindowComponent::URPGSkillWindowComponent()
{
	WindowInputState = MakeShared<FRPGWindowInputState>();
	WindowInputState->OnFirstWindowOpened.AddUObject(this, &ThisClass::ReleaseHeldAbilityInputs);
	WindowClass = TSoftClassPtr<URPGSkillWindow>(FSoftObjectPath(TEXT(
		"/Game/Blueprints/UI/Skill/WBP_SkillWindow.WBP_SkillWindow_C")));
}

void URPGSkillWindowComponent::BindInput(
	UEnhancedInputComponent* Input, const UDataAsset_InputConfig* Config)
{
	UnbindInput();
	APlayerController* Controller = Cast<APlayerController>(GetOwner());
	ULocalPlayer* LocalPlayer = Controller ? Controller->GetLocalPlayer() : nullptr;
	if (!Input || !LocalPlayer)
	{
		return;
	}
	InputSubsystem = LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
	if (!InputSubsystem.IsValid())
	{
		return;
	}
	ToggleAction = Config
		? Config->FindNativeInputActionByTag(RPGGameplayTags::InputTag_ShowSkillWindow)
		: nullptr;
	if (!ToggleAction)
	{
		ToggleAction = NewObject<UInputAction>(this);
		ToggleAction->ValueType = EInputActionValueType::Boolean;
		FallbackMappingContext = NewObject<UInputMappingContext>(this);
		FKey Key = DefaultToggleKey;
		if (const URPGGameUserSettings* Settings = URPGGameUserSettings::GetRPGGameUserSettings())
		{
			const FKey SavedKey = Settings->GetKeyMapping(RPGGameplayTags::InputTag_ShowSkillWindow);
			if (SavedKey.IsValid()) Key = SavedKey;
		}
		FallbackMappingContext->MapKey(ToggleAction, Key);
		InputSubsystem->AddMappingContext(FallbackMappingContext, 1);
	}
	BoundInput = Input;
	// Started produces one toggle per press, including a long-held key.
	InputBindingHandle = Input->BindAction(ToggleAction, ETriggerEvent::Started,
		this, &ThisClass::ToggleWindow).GetHandle();
}

bool URPGSkillWindowComponent::RemapFallbackKey(const FKey Key)
{
	if (!Key.IsValid() || !FallbackMappingContext || !InputSubsystem.IsValid())
	{
		return false;
	}
	FallbackMappingContext->UnmapAllKeysFromAction(ToggleAction);
	FallbackMappingContext->MapKey(ToggleAction, Key);
	FModifyContextOptions Options;
	Options.bForceImmediately = true;
	Options.bIgnoreAllPressedKeysUntilRelease = true;
	InputSubsystem->RequestRebuildControlMappings(Options);
	RefreshCloseKeys();
	return true;
}

FKey URPGSkillWindowComponent::GetToggleKey() const
{
	return GetToggleKeys()[0];
}

TArray<FKey> URPGSkillWindowComponent::GetToggleKeys() const
{
	const TArray<FKey> Keys = InputSubsystem.IsValid() && ToggleAction
		? InputSubsystem->QueryKeysMappedToAction(ToggleAction) : TArray<FKey>();
	return Keys.IsEmpty() ? TArray<FKey>{DefaultToggleKey} : Keys;
}

void URPGSkillWindowComponent::SetWindowInputState(const TSharedRef<FRPGWindowInputState>& State)
{
	check(!bWindowOpen);
	WindowInputState->OnFirstWindowOpened.RemoveAll(this);
	WindowInputState = State;
	WindowInputState->OnFirstWindowOpened.AddUObject(this, &ThisClass::ReleaseHeldAbilityInputs);
}

void URPGSkillWindowComponent::RefreshCloseKeys()
{
	if (Window)
	{
		Window->SetCloseKeys(GetToggleKeys());
	}
}

void URPGSkillWindowComponent::ToggleWindow()
{
	if (bWindowOpen) CloseWindow();
	else ShowWindow();
}

bool URPGSkillWindowComponent::ShowWindow()
{
	if (bWindowOpen) return true;
	APlayerController* Controller = Cast<APlayerController>(GetOwner());
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	if (!Controller || !Controller->IsLocalController() || !Pawn
		|| !Pawn->FindComponentByClass<URPGPlayerSkillComponent>())
	{
		return false;
	}
	const TSubclassOf<URPGSkillWindow> LoadedClass = WindowClass.LoadSynchronous();
	if (!LoadedClass) return false;
	Window = CreateWidget<URPGSkillWindow>(Controller, LoadedClass);
	if (!Window) return false;

	bWindowOpen = true;
	Window->OnWindowClosed.AddUObject(this, &ThisClass::CloseWindow);
	Window->OnWindowInteraction.AddUObject(this, &ThisClass::FocusWindow);
	Window->SetIsFocusable(true);
	if (!WindowInputState->BeginWindow(*Controller, *Window))
	{
		CloseWindow();
		return false;
	}
	Window->AddToViewport(40);
	if (!bWindowOpen || !Window) return false;
	RefreshCloseKeys();
	if (!bWindowOpen || !Window || Controller->GetPawn() != Pawn)
	{
		CloseWindow();
		return false;
	}
	Window->ActivateWidget();
	if (!bWindowOpen || !Window) return false;
	RPGWindowPlacement::Apply(*Window, ERPGWindowSide::Left);
	WindowInputState->FocusWindow(*Window);
	return true;
}

void URPGSkillWindowComponent::CloseWindow()
{
	if (!bWindowOpen) return;
	bWindowOpen = false;
	const TStrongObjectPtr<URPGSkillWindow> ClosingWindow(Window.Get());
	Window = nullptr;
	if (ClosingWindow)
	{
		ClosingWindow->OnWindowClosed.RemoveAll(this);
		ClosingWindow->OnWindowInteraction.RemoveAll(this);
		ClosingWindow->DeactivateWidget();
		ClosingWindow->RemoveFromParent();
		WindowInputState->EndWindow(*ClosingWindow);
	}
}

void URPGSkillWindowComponent::FocusWindow()
{
	if (bWindowOpen && Window) WindowInputState->FocusWindow(*Window);
}

void URPGSkillWindowComponent::ReleaseHeldAbilityInputs()
{
	const APlayerController* Controller = Cast<APlayerController>(GetOwner());
	if (Controller && Controller->GetPawn())
	{
		if (UQuickSlotComponent* Slots = Controller->GetPawn()->FindComponentByClass<UQuickSlotComponent>())
			Slots->ReleaseAllSkillInputs();
	}
	URPGAbilitySystemComponent* ASC = Controller
		? Cast<URPGAbilitySystemComponent>(
			UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Controller->GetPawn())) : nullptr;
	if (!ASC) return;
	TArray<FGameplayAbilitySpecHandle> HeldHandles;
	for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
	{
		if (Spec.InputPressed) HeldHandles.Add(Spec.Handle);
	}
	for (const FGameplayAbilitySpecHandle Handle : HeldHandles)
	{
		ASC->OnAbilitySpecInputReleased(Handle);
	}
}

void URPGSkillWindowComponent::UnbindInput()
{
	if (BoundInput.IsValid()) BoundInput->RemoveBindingByHandle(InputBindingHandle);
	if (FallbackMappingContext && InputSubsystem.IsValid())
	{
		InputSubsystem->RemoveMappingContext(FallbackMappingContext);
	}
	BoundInput.Reset();
	InputSubsystem.Reset();
	ToggleAction = nullptr;
	FallbackMappingContext = nullptr;
	InputBindingHandle = 0;
}

void URPGSkillWindowComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CloseWindow();
	WindowInputState->OnFirstWindowOpened.RemoveAll(this);
	UnbindInput();
	Super::EndPlay(EndPlayReason);
}
