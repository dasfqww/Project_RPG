#include "Input/RPGPanelShortcutComponent.h"

#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "GameFramework/PlayerController.h"
#include "Input/Events.h"
#include "Layout/WidgetPath.h"
#include "Widgets/SViewport.h"

class FRPGPanelShortcutInput final : public IInputProcessor
{
public:
	FRPGPanelShortcutInput(APlayerController& InController, TMap<FInputChord, ERPGPanelShortcutAction>&& InShortcuts,
		TFunction<bool(ERPGPanelShortcutAction)> InDispatch)
		: Controller(&InController), Shortcuts(MoveTemp(InShortcuts)), Dispatch(MoveTemp(InDispatch))
	{
	}

	virtual void Tick(float, FSlateApplication&, TSharedRef<ICursor>) override {}

	virtual bool HandleKeyDownEvent(FSlateApplication& Slate, const FKeyEvent& Event) override
	{
		const FKey Key = Event.GetKey();
		const FInputChord Chord(Key, Event.IsShiftDown(), Event.IsControlDown(), Event.IsAltDown(), Event.IsCommandDown());
		const ERPGPanelShortcutAction* Action = Shortcuts.Find(Chord);
		if (!Action && !ConsumedKeys.Contains(Key)) return false;

		UGameViewportClient* Viewport = nullptr;
		FWidgetPath FocusPath;
		if (!GetGameFocusPath(Slate, Event, Viewport, FocusPath)) return false;
		if (Event.IsRepeat()) return ConsumedKeys.Contains(Key);

		// A new physical press can arrive after focus loss dropped the previous release.
		ConsumedKeys.Remove(Key);
		if (!Action || Viewport->IgnoreInput() || IsEditingText(FocusPath))
		{
			return false;
		}

		if (!Dispatch(*Action)) return false;
		// A handled press is stopped before either the widget or gameplay input stack sees it.
		ConsumedKeys.Add(Key);
		return true;
	}

	virtual bool HandleKeyUpEvent(FSlateApplication& Slate, const FKeyEvent& Event) override
	{
		const APlayerController* Owner = Controller.Get();
		const ULocalPlayer* Player = Owner ? Owner->GetLocalPlayer() : nullptr;
		const TSharedPtr<const FSlateUser> User = Player ? Player->GetSlateUser() : nullptr;
		if (!User || static_cast<uint32>(User->GetUserIndex()) != Event.GetUserIndex()
			|| !ConsumedKeys.Remove(Event.GetKey())) return false;

		UGameViewportClient* Viewport = nullptr;
		FWidgetPath FocusPath;
		return GetGameFocusPath(Slate, Event, Viewport, FocusPath);
	}

private:
	bool GetGameFocusPath(FSlateApplication& Slate, const FKeyEvent& Event,
		UGameViewportClient*& OutViewport, FWidgetPath& OutPath) const
	{
		const APlayerController* Owner = Controller.Get();
		const ULocalPlayer* Player = Owner ? Owner->GetLocalPlayer() : nullptr;
		if (!Player || Player->PlayerController != Owner || Owner->IsActorBeingDestroyed()
			|| !Owner->GetWorld() || Owner->GetWorld()->bIsTearingDown)
			return false;

		const TSharedPtr<const FSlateUser> User = Player->GetSlateUser();
		if (!User || static_cast<uint32>(User->GetUserIndex()) != Event.GetUserIndex()) return false;

		OutViewport = Player->ViewportClient;
		const TSharedPtr<SViewport> GameWidget = OutViewport ? OutViewport->GetGameViewportWidget() : nullptr;
		const TSharedPtr<SWidget> Focus = User->GetFocusedWidget();
		return GameWidget && Focus && Slate.GeneratePathToWidgetUnchecked(Focus.ToSharedRef(), OutPath)
			&& OutPath.ContainsWidget(GameWidget.Get());
	}

	static bool IsEditingText(const FWidgetPath& FocusPath)
	{
		static const FName EditableText(TEXT("SEditableText"));
		static const FName EditableTextBox(TEXT("SEditableTextBox"));
		static const FName MultiLineText(TEXT("SMultiLineEditableText"));
		static const FName MultiLineTextBox(TEXT("SMultiLineEditableTextBox"));
		for (int32 Index = 0; Index < FocusPath.Widgets.Num(); ++Index)
		{
			const FName Type = FocusPath.Widgets[Index].Widget->GetType();
			if (Type == EditableText || Type == EditableTextBox || Type == MultiLineText || Type == MultiLineTextBox)
				return true;
		}
		return false;
	}

	TWeakObjectPtr<APlayerController> Controller;
	TMap<FInputChord, ERPGPanelShortcutAction> Shortcuts;
	TFunction<bool(ERPGPanelShortcutAction)> Dispatch;
	TSet<FKey> ConsumedKeys;
};

URPGPanelShortcutComponent::URPGPanelShortcutComponent()
{
	ConfigAsset = TSoftObjectPtr<URPGPanelShortcutConfig>(FSoftObjectPath(TEXT(
		"/Game/System/Input/DA_PanelShortcuts.DA_PanelShortcuts")));
}

void URPGPanelShortcutComponent::Start(TFunction<bool(ERPGPanelShortcutAction)> Dispatch)
{
	APlayerController* Controller = Cast<APlayerController>(GetOwner());
	if (Processor || !Controller || !Controller->GetLocalPlayer() || !FSlateApplication::IsInitialized() || ConfigAsset.IsNull())
		return;

	const URPGPanelShortcutConfig* Config = ConfigAsset.LoadSynchronous();
	if (!Config)
	{
		UE_LOG(LogTemp, Warning, TEXT("Panel shortcut configuration is unavailable: %s"), *ConfigAsset.ToString());
		return;
	}

	TMap<FInputChord, ERPGPanelShortcutAction> Shortcuts;
	FText Error;
	if (!Config->BuildShortcutMap(Shortcuts, Error))
	{
		UE_LOG(LogTemp, Warning, TEXT("Panel shortcut configuration %s is invalid: %s"), *Config->GetPathName(), *Error.ToString());
		return;
	}
	if (Shortcuts.IsEmpty()) return;

	const auto NewProcessor = MakeShared<FRPGPanelShortcutInput>(*Controller, MoveTemp(Shortcuts), MoveTemp(Dispatch));
	if (FSlateApplication::Get().RegisterInputPreProcessor(NewProcessor))
	{
		Processor = NewProcessor;
	}
}

void URPGPanelShortcutComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Stop();
	Super::EndPlay(EndPlayReason);
}

void URPGPanelShortcutComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	Stop();
	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

void URPGPanelShortcutComponent::Stop()
{
	if (Processor && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(Processor);
	}
	Processor.Reset();
}
