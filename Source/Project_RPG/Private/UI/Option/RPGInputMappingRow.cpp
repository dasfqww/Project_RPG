#include "UI/Option/RPGInputMappingRow.h"

#include "Components/TextBlock.h"
#include "Components/InputKeySelector.h"
#include "Components/Image.h"
#include "DataAsset/Input/DataAsset_KeyIconConfig.h"
#include "DataAsset/Input/DataAsset_InputConfig.h"
#include "Controller/RPGPlayerController.h"
#include "UI/Option/RPGInputOptionsRuntime.h"
#include "Input/Events.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Framework/Application/SlateUser.h"
#include "Framework/Application/SlateApplication.h"
#include "UObject/StrongObjectPtr.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

void URPGInputMappingRow::InitializeRow(const FGameplayTag& InTag, const FText& InActionName,
	const FKey& InCurrentKey, const UDataAsset_KeyIconConfig* InIconConfig)
{
	InitializeBindingRow(InTag, InActionName, FKey(), InCurrentKey, InIconConfig, true);
}

void URPGInputMappingRow::InitializeBindingRow(const FGameplayTag& InTag, const FText& InActionName,
	const FKey& InDefaultKey, const FKey& InCurrentKey,
	const UDataAsset_KeyIconConfig* InIconConfig, bool bCanRemap)
{
	FRPGInputOptionBinding Binding;
	Binding.InputTag = InTag;
	Binding.DisplayName = InActionName;
	Binding.DefaultKey = InDefaultKey;
	Binding.CurrentKey = InCurrentKey;
	Binding.bCanRemap = bCanRemap;
	InitializeOptionBinding(Binding, InIconConfig);
}

void URPGInputMappingRow::InitializeOptionBinding(const FRPGInputOptionBinding& Binding,
	const UDataAsset_KeyIconConfig* InIconConfig)
{
	++BindingRevision;
	ActionTag = Binding.InputTag;
	DefaultKey = Binding.DefaultKey;
	CurrentKey = Binding.CurrentKey;
	BoundInputConfig = Binding.SourceConfig;
	BoundInputAction = Binding.Action;
	bBoundAdditionalMapping = Binding.bIsAdditionalMapping;
	BoundAdditionalInputProvider = Binding.AdditionalInputProvider;
	BoundSourceMappingContext = Binding.SourceMappingContext;
	BoundAppliedMappingContext = Binding.AppliedMappingContext;
	IconConfig = InIconConfig;
	bCanChangeKey = Binding.bCanRemap;
	if (ActionNameText) { ActionNameText->SetText(Binding.DisplayName); }
	SetMappingError(FText::GetEmpty());
	SynchronizeSelector();
}

TSharedRef<SWidget> URPGInputMappingRow::RebuildWidget()
{
	TSharedRef<SWidget> Content = Super::RebuildWidget();
	if (IsDesignTime()) { return Content; }

	// Keep the authored widget tree intact; the native controls share this row's lifetime.
	const TWeakObjectPtr<URPGInputMappingRow> WeakThis(this);
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			.VAlign(VAlign_Center)
			[
				Content
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(8.f, 0.f, 0.f, 0.f))
			[
				SNew(SButton)
				.Text(NSLOCTEXT("RPGInputOptions", "RestoreDefaultButton", "기본값"))
				.ContentPadding(FMargin(12.f, 6.f))
				.Visibility_Lambda([WeakThis]()
				{
					const URPGInputMappingRow* Row = WeakThis.Get();
					return Row && Row->bShowNativeResetButton && Row->bCanChangeKey && Row->DefaultKey.IsValid()
						? EVisibility::Visible : EVisibility::Collapsed;
				})
				.IsEnabled_Lambda([WeakThis]()
				{
					const URPGInputMappingRow* Row = WeakThis.Get();
					return Row && Row->CanResetToDefault();
				})
				.ToolTipText_Lambda([WeakThis]()
				{
					const URPGInputMappingRow* Row = WeakThis.Get();
					return Row && Row->DefaultKey.IsValid()
						? FText::Format(NSLOCTEXT("RPGInputOptions", "RestoreDefaultTooltip", "기본 키 {0}(으)로 복원합니다."),
							Row->DefaultKey.GetDisplayName())
						: FText::GetEmpty();
				})
				.OnClicked_UObject(this, &ThisClass::HandleResetButtonClicked)
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FMargin(0.f, 4.f, 0.f, 0.f))
		[
			SNew(STextBlock)
			.Text_Lambda([WeakThis]()
			{
				const URPGInputMappingRow* Row = WeakThis.Get();
				return Row ? Row->GetNativeFeedbackText() : FText::GetEmpty();
			})
			.Visibility_Lambda([WeakThis]()
			{
				const URPGInputMappingRow* Row = WeakThis.Get();
				return Row && !Row->GetNativeFeedbackText().IsEmpty()
					? EVisibility::HitTestInvisible : EVisibility::Collapsed;
			})
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 12))
			.ColorAndOpacity_Lambda([WeakThis]()
			{
				const URPGInputMappingRow* Row = WeakThis.Get();
				return Row && !Row->LastMappingError.IsEmpty()
					? FSlateColor(FLinearColor(1.f, 0.35f, 0.3f))
					: FSlateColor::UseForeground();
			})
			.AutoWrapText(true)
		];
}

void URPGInputMappingRow::NativeConstruct()
{
	bRowConstructed = true;
	Super::NativeConstruct();
	if (!bRowConstructed || IsRemovingFromParent()) { return; }
	if (KeySelector)
	{
		KeySelector->SetAllowModifierKeys(true);
		KeySelector->SetAllowGamepadKeys(true);
		KeySelector->SetEscapeKeys({ EKeys::Escape });
		KeySelector->OnKeySelected.AddUniqueDynamic(this, &ThisClass::HandleKeySelected);
		KeySelector->OnIsSelectingKeyChanged.AddUniqueDynamic(this, &ThisClass::HandleSelectingKeyChanged);
	}
	RefreshDisplayedKey();
}

void URPGInputMappingRow::NativeDestruct()
{
	++BindingRevision;
	bRowConstructed = false;
	CancelKeySelection();
	if (KeySelector)
	{
		KeySelector->OnKeySelected.RemoveDynamic(this, &ThisClass::HandleKeySelected);
		KeySelector->OnIsSelectingKeyChanged.RemoveDynamic(this, &ThisClass::HandleSelectingKeyChanged);
	}
	Super::NativeDestruct();
}

void URPGInputMappingRow::SynchronizeSelector()
{
	TGuardValue<bool> Guard(bSynchronizingSelector, true);
	if (KeySelector)
	{
		KeySelector->SetIsEnabled(bCanChangeKey && !IsDesignTime());
		if (!KeySelector->GetIsSelectingKey()) { KeySelector->SetSelectedKey(FInputChord(CurrentKey)); }
	}
	UpdateKeyIcon(CurrentKey);
}

void URPGInputMappingRow::RefreshDisplayedKey()
{
	if (IsUnreachable() || HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) || IsRemovingFromParent()) { return; }
	const ARPGPlayerController* Controller = Cast<ARPGPlayerController>(GetOwningPlayer());
	if (IsValid(Controller) && !Controller->IsActorBeingDestroyed() && Controller->GetWorld() == GetWorld())
	{
		if (bBoundAdditionalMapping)
		{
			TStrongObjectPtr<URPGInputMappingRow> KeepAlive(this);
			TStrongObjectPtr<const ARPGPlayerController> KeepController(Controller);
			const uint64 Revision = BindingRevision;
			FRPGInputOptionBinding Binding;
			Binding.bIsAdditionalMapping = true;
			Binding.AdditionalInputProvider = BoundAdditionalInputProvider;
			Binding.SourceMappingContext = BoundSourceMappingContext;
			Binding.AppliedMappingContext = BoundAppliedMappingContext;
			Binding.Action = BoundInputAction;
			Binding.DefaultKey = DefaultKey;
			IRPGInputOptionsRuntime* Runtime = IRPGInputOptionsRuntime::Get();
			const FKey LiveKey = Runtime ? Runtime->GetAdditionalBindingKey(Controller, Binding) : FKey();
			if (Revision != BindingRevision || GetOwningPlayer() != Controller || IsRemovingFromParent()
				|| IsUnreachable() || HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed)
				|| !IsValid(Controller) || Controller->IsActorBeingDestroyed() || Controller->GetWorld() != GetWorld()) { return; }
			CurrentKey = LiveKey;
			SynchronizeSelector();
			return;
		}
		const UDataAsset_InputConfig* Config = Controller->GetSourceInputConfig();
		const bool bLegacyBinding = BoundInputConfig.IsExplicitlyNull() && BoundInputAction.IsExplicitlyNull();
		// A replacement config/action needs a new row model, not an old label with a different command's key.
		if (bLegacyBinding || (BoundInputConfig.IsValid() && BoundInputAction.IsValid()
			&& Config == BoundInputConfig.Get() && Config->FindInputActionByTag(ActionTag) == BoundInputAction.Get()))
		{
			CurrentKey = Controller->GetCurrentKeyForBinding(ActionTag, DefaultKey);
		}
	}
	SynchronizeSelector();
}

void URPGInputMappingRow::HandleSelectingKeyChanged()
{
	if (!KeySelector) { return; }
	if (KeySelector->GetIsSelectingKey())
	{
		SetMappingError(FText::GetEmpty());
		UpdateKeyIcon(CurrentKey);
	}
	else { SynchronizeSelector(); }
}

void URPGInputMappingRow::HandleKeySelected(FInputChord SelectedKey)
{
	if (!bRowConstructed || bSynchronizingSelector || bApplyingBinding || !bCanChangeKey || IsRemovingFromParent()) { return; }
	if (!SelectedKey.Key.IsValid()) { SynchronizeSelector(); return; }
	if (SelectedKey.bCtrl || SelectedKey.bAlt || SelectedKey.bShift || SelectedKey.bCmd)
	{
		SetMappingError(NSLOCTEXT("RPGInputOptions", "ChordUnsupported", "Choose one key without a modifier chord."));
		SynchronizeSelector();
		return;
	}
	ApplySelectedKey(SelectedKey.Key);
}

bool URPGInputMappingRow::ApplySelectedKey(FKey Key)
{
	if (!IsInGameThread() || bApplyingBinding || !bRowConstructed || IsRemovingFromParent() || IsDesignTime()
		|| IsUnreachable() || HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed)) { return false; }
	TStrongObjectPtr<URPGInputMappingRow> KeepAlive(this);
	TGuardValue<bool> ApplyingGuard(bApplyingBinding, true);
	TStrongObjectPtr<ARPGPlayerController> Controller(Cast<ARPGPlayerController>(GetOwningPlayer()));
	TStrongObjectPtr<ULocalPlayer> Player(GetOwningLocalPlayer());
	TStrongObjectPtr<UWorld> BindingWorld(GetWorld());
	const uint64 Revision = BindingRevision;
	const auto IsCurrentRow = [&]()
	{
		return bRowConstructed && !IsRemovingFromParent() && BindingRevision == Revision
			&& !IsUnreachable() && !HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed)
			&& IsValid(Controller.Get()) && !Controller->IsActorBeingDestroyed() && Controller->IsLocalController()
			&& GetOwningPlayer() == Controller.Get() && IsValid(Player.Get()) && GetOwningLocalPlayer() == Player.Get()
			&& Controller->GetLocalPlayer() == Player.Get() && Player->GetPlayerController(nullptr) == Controller.Get()
			&& IsValid(BindingWorld.Get()) && !BindingWorld->bIsTearingDown
			&& GetWorld() == BindingWorld.Get() && Controller->GetWorld() == BindingWorld.Get();
	};
	if (!IsCurrentRow()) { return false; }
	FRPGInputOptionBinding Expected;
	Expected.InputTag = ActionTag;
	Expected.DefaultKey = DefaultKey;
	Expected.CurrentKey = CurrentKey;
	Expected.bCanRemap = bCanChangeKey;
	Expected.SourceConfig = BoundInputConfig;
	Expected.Action = BoundInputAction;
	Expected.bIsAdditionalMapping = bBoundAdditionalMapping;
	Expected.AdditionalInputProvider = BoundAdditionalInputProvider;
	Expected.SourceMappingContext = BoundSourceMappingContext;
	Expected.AppliedMappingContext = BoundAppliedMappingContext;
	IRPGInputOptionsRuntime* Runtime = IRPGInputOptionsRuntime::Get();
	if (!IsCurrentRow()) { return false; }
	FText Error;
	const bool bApplied = Expected.bCanRemap && Runtime && Runtime->TryApplyBinding(Controller.Get(), Expected, Key, Error);
	if (!IsCurrentRow()) { return bApplied; }
	if (!bApplied && Error.IsEmpty()) { Error = NSLOCTEXT("RPGInputOptions", "Unavailable", "Input bindings are not ready."); }
	SetMappingError(bApplied ? FText::GetEmpty() : Error);
	if (!IsCurrentRow()) { return bApplied; }
	RefreshDisplayedKey();
	if (!IsCurrentRow()) { return bApplied; }
	// A failed persistence/rebuild request can still leave different live keys. Refresh independently of success.
	OnBindingRefreshRequested.Broadcast();
	if (bApplied && IsCurrentRow()) { OnBindingChanged.Broadcast(); }
	return bApplied;
}

bool URPGInputMappingRow::IsSelectingKey() const
{
	return KeySelector && KeySelector->GetIsSelectingKey();
}

void URPGInputMappingRow::CancelKeySelection()
{
	if (!FSlateApplication::IsInitialized() || !IsSelectingKey() || bSynchronizingSelector || IsUnreachable()
		|| HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed)) { return; }
	TStrongObjectPtr<URPGInputMappingRow> KeepAlive(this);
	TStrongObjectPtr<UInputKeySelector> Selector(KeySelector.Get());
	TGuardValue<bool> Guard(bSynchronizingSelector, true);
	const TSharedPtr<SWidget> SelectorWidget = Selector->GetCachedWidget();
	if (SelectorWidget)
	{
		// The engine exposes cancellation through this focus-loss handler, without selecting a key.
		ULocalPlayer* Player = GetOwningLocalPlayer();
		const TSharedPtr<FSlateUser> User = Player ? Player->GetSlateUser() : nullptr;
		SelectorWidget->OnFocusLost(FFocusEvent(EFocusCause::Cleared, User ? User->GetUserIndex() : 0));
	}
	if (KeySelector == Selector.Get() && !Selector->GetIsSelectingKey()) { SynchronizeSelector(); }
}

bool URPGInputMappingRow::CanResetToDefault() const
{
	const ARPGPlayerController* Controller = Cast<ARPGPlayerController>(GetOwningPlayer());
	return bRowConstructed && bCanChangeKey && (ActionTag.IsValid() || bBoundAdditionalMapping)
		&& DefaultKey.IsValid() && CurrentKey != DefaultKey
		&& !IsDesignTime() && !IsRemovingFromParent() && !bSynchronizingSelector && !bApplyingBinding
		&& KeySelector && !KeySelector->GetIsSelectingKey()
		&& IsValid(Controller) && Controller->IsLocalController();
}

bool URPGInputMappingRow::ResetToDefault()
{
	return CanResetToDefault() && ApplySelectedKey(DefaultKey);
}

FReply URPGInputMappingRow::HandleResetButtonClicked()
{
	ResetToDefault();
	return FReply::Handled();
}

FText URPGInputMappingRow::GetNativeFeedbackText() const
{
	if (!bShowNativeMappingFeedback || IsRemovingFromParent()) { return FText::GetEmpty(); }
	if (!LastMappingError.IsEmpty())
	{
		// Blueprint error labels continue to own their existing presentation.
		return MappingErrorText ? FText::GetEmpty() : LastMappingError;
	}
	if (bBoundAdditionalMapping && !bCanChangeKey)
	{
		return NSLOCTEXT("RPGInputOptions", "AdditionalBindingReadOnly", "이 입력은 표시 전용입니다.");
	}
	return KeySelector && KeySelector->GetIsSelectingKey()
		? NSLOCTEXT("RPGInputOptions", "SelectingKeyFeedback", "키 입력을 기다리는 중입니다. Esc로 취소할 수 있습니다.")
		: FText::GetEmpty();
}

FReply URPGInputMappingRow::NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
	if (Event.GetKey() == EKeys::BackSpace && CanResetToDefault())
	{
		ResetToDefault();
		return FReply::Handled();
	}
	return Super::NativeOnPreviewKeyDown(Geometry, Event);
}

void URPGInputMappingRow::SetMappingError(const FText& Error)
{
	LastMappingError = Error;
	if (MappingErrorText)
	{
		MappingErrorText->SetText(Error);
		MappingErrorText->SetVisibility(Error.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
	if (KeySelector)
	{
		FText Help = bBoundAdditionalMapping
			? NSLOCTEXT("RPGInputOptions", "AdditionalBindingReadOnly", "이 입력은 표시 전용입니다.")
			: NSLOCTEXT("RPGInputOptions", "AxisHelp", "This analog binding is shown for reference.");
		if (bCanChangeKey)
		{
			Help = DefaultKey.IsValid()
				? NSLOCTEXT("RPGInputOptions", "BindingHelp", "Select one key. Escape: cancel. Backspace: restore default.")
				: NSLOCTEXT("RPGInputOptions", "LegacyBindingHelp", "Select one key. Escape: cancel.");
		}
		KeySelector->SetToolTipText(Error.IsEmpty() ? Help : Error);
	}
}

void URPGInputMappingRow::UpdateKeyIcon(const FKey& InKey)
{
	if (!KeyIconImage) { return; }
	if (KeySelector && KeySelector->GetIsSelectingKey())
	{
		KeyIconImage->SetVisibility(ESlateVisibility::Hidden);
		return;
	}
	UTexture2D* Icon = IconConfig ? IconConfig->FindIconForKey(InKey) : nullptr;
	if (Icon)
	{
		KeyIconImage->SetBrushFromTexture(Icon);
		KeyIconImage->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	else { KeyIconImage->SetVisibility(ESlateVisibility::Collapsed); }
}
