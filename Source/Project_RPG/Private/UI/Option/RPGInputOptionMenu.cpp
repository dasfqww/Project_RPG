#include "UI/Option/RPGInputOptionMenu.h"

#include "Components/ScrollBox.h"
#include "UI/Option/RPGInputMappingRow.h"
#include "UI/Option/RPGInputOptionsRuntime.h"
#include "DataAsset/Input/DataAsset_InputConfig.h"
#include "DataAsset/Input/DataAsset_KeyIconConfig.h"
#include "Controller/RPGPlayerController.h"
#include "RPGInputTags.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/Class.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Layout/WidgetPath.h"
#include "Widgets/Layout/SBox.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

TSharedRef<SWidget> URPGInputOptionMenu::RebuildWidget()
{
	TSharedRef<SWidget> Content = Super::RebuildWidget();
	NativeFilterFocusTarget.Reset();
	NativeRestoreDefaultsButton.Reset();
	NativeDefaultsCancelButton.Reset();
	NativeDefaultsConfirmationPanel.Reset();
	if (IsDesignTime()) { return Content; }

	const TWeakObjectPtr<URPGInputOptionMenu> WeakThis(this);
	const auto MakeDeviceButton = [this, WeakThis](ERPGInputOptionDeviceFilter Device, const FText& Label)
	{
		return SNew(SButton)
			.Text(Label)
			.ContentPadding(FMargin(10.f, 5.f))
			.ButtonColorAndOpacity_Lambda([WeakThis, Device]()
			{
				const URPGInputOptionMenu* Menu = WeakThis.Get();
				return FSlateColor(Menu && Menu->BindingDeviceFilter == Device
					? FLinearColor(0.35f, 0.6f, 1.f) : FLinearColor::White);
			})
			.OnClicked_UObject(this, &ThisClass::HandleDeviceFilterClicked, Device);
	};
	TSharedRef<SEditableTextBox> SearchBox = SNew(SEditableTextBox)
		.HintText(NSLOCTEXT("RPGInputOptions", "BindingSearchHint", "명령 이름 또는 키 검색"))
		.Text_Lambda([WeakThis]()
		{
			const URPGInputOptionMenu* Menu = WeakThis.Get();
			return Menu ? Menu->BindingSearchText : FText::GetEmpty();
		})
		.SelectAllTextWhenFocused(true)
		.ClearKeyboardFocusOnCommit(false)
		.OnTextChanged_UObject(this, &ThisClass::SetBindingSearchText);
	NativeFilterFocusTarget = SearchBox;
	TSharedRef<SButton> RestoreButton = SNew(SButton)
		.Text(NSLOCTEXT("RPGInputOptions", "RestoreAllDefaultsButton", "모든 키 기본값 복원"))
		.ToolTipText(NSLOCTEXT("RPGInputOptions", "RestoreAllDefaultsTooltip",
			"추가 기능 입력과 필터로 숨긴 항목을 포함한 편집 가능한 키를 원본 기본값으로 복원합니다."))
		.Visibility_Lambda([WeakThis]()
		{
			const URPGInputOptionMenu* Menu = WeakThis.Get();
			return Menu && !Menu->IsAwaitingDefaultRestoration() ? EVisibility::Visible : EVisibility::Collapsed;
		})
		.IsEnabled_Lambda([WeakThis]()
		{
			const URPGInputOptionMenu* Menu = WeakThis.Get();
			return Menu && Menu->CanRestoreAllDefaults();
		})
		.OnClicked_UObject(this, &ThisClass::HandleRestoreDefaultsClicked);
	NativeRestoreDefaultsButton = RestoreButton;
	TSharedRef<SButton> CancelButton = SNew(SButton)
		.Text(NSLOCTEXT("RPGInputOptions", "CancelAllDefaultsButton", "취소"))
		.OnClicked_UObject(this, &ThisClass::HandleCancelDefaultsClicked);
	NativeDefaultsCancelButton = CancelButton;
	TSharedRef<SVerticalBox> ConfirmationPanel = SNew(SVerticalBox)
		.Visibility_Lambda([WeakThis]()
		{
			const URPGInputOptionMenu* Menu = WeakThis.Get();
			return Menu && Menu->IsAwaitingDefaultRestoration() ? EVisibility::Visible : EVisibility::Collapsed;
		})
		.IsEnabled_Lambda([WeakThis]()
		{
			const URPGInputOptionMenu* Menu = WeakThis.Get();
			return Menu && Menu->CanManageDefaultRestoration();
		})
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(STextBlock)
			.Text_Lambda([WeakThis]()
			{
				const URPGInputOptionMenu* Menu = WeakThis.Get();
				return Menu ? Menu->GetDefaultRestorationPrompt() : FText::GetEmpty();
			})
			.AutoWrapText(true)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 6.f, 0.f, 0.f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				CancelButton
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(8.f, 0.f))
			[
				SNew(SButton)
				.Text(NSLOCTEXT("RPGInputOptions", "ConfirmAllDefaultsButton", "기본값으로 복원"))
				.OnClicked_UObject(this, &ThisClass::HandleConfirmDefaultsClicked)
			]
		];
	NativeDefaultsConfirmationPanel = ConfirmationPanel;

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FMargin(0.f, 0.f, 0.f, 8.f))
		[
			SNew(SVerticalBox)
			.Visibility_Lambda([WeakThis]()
			{
				const URPGInputOptionMenu* Menu = WeakThis.Get();
				return Menu && Menu->bShowNativeBindingFilters ? EVisibility::Visible : EVisibility::Collapsed;
			})
			.IsEnabled_Lambda([WeakThis]()
			{
				const URPGInputOptionMenu* Menu = WeakThis.Get();
				return Menu && Menu->CanChangeBindingFilters();
			})
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SearchBox
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(FMargin(0.f, 6.f, 0.f, 0.f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()
				[
					MakeDeviceButton(ERPGInputOptionDeviceFilter::All,
						NSLOCTEXT("RPGInputOptions", "AllDevicesFilter", "전체"))
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(4.f, 0.f))
				[
					MakeDeviceButton(ERPGInputOptionDeviceFilter::KeyboardMouse,
						NSLOCTEXT("RPGInputOptions", "KeyboardMouseFilter", "키보드·마우스"))
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					MakeDeviceButton(ERPGInputOptionDeviceFilter::Gamepad,
						NSLOCTEXT("RPGInputOptions", "GamepadFilter", "게임패드"))
				]
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(FMargin(12.f, 0.f))
				[
					SNew(SCheckBox)
					.IsChecked_Lambda([WeakThis]()
					{
						const URPGInputOptionMenu* Menu = WeakThis.Get();
						return Menu && Menu->bShowChangedBindingsOnly ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
					})
					.OnCheckStateChanged_Lambda([WeakThis](ECheckBoxState State)
					{
						if (URPGInputOptionMenu* Menu = WeakThis.Get())
						{
							Menu->SetShowChangedBindingsOnly(State == ECheckBoxState::Checked);
						}
					})
					[
						SNew(STextBlock)
						.Text(NSLOCTEXT("RPGInputOptions", "ChangedBindingsOnly", "변경한 키만"))
					]
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SButton)
					.Text(NSLOCTEXT("RPGInputOptions", "ResetBindingFilters", "필터 초기화"))
					.OnClicked_UObject(this, &ThisClass::HandleResetFiltersClicked)
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(FMargin(0.f, 6.f, 0.f, 0.f))
			[
				SNew(STextBlock)
				.Text_Lambda([WeakThis]()
				{
					const URPGInputOptionMenu* Menu = WeakThis.Get();
					return Menu ? Menu->GetBindingFilterSummary() : FText::GetEmpty();
				})
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 12))
				.AutoWrapText(true)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 4.f, 0.f, 0.f))
			[
				SNew(STextBlock)
				.Text_Lambda([WeakThis]()
				{
					const URPGInputOptionMenu* Menu = WeakThis.Get();
					return Menu ? Menu->LastBindingChangeError : FText::GetEmpty();
				})
				.Visibility_Lambda([WeakThis]()
				{
					const URPGInputOptionMenu* Menu = WeakThis.Get();
					return Menu && !Menu->LastBindingChangeError.IsEmpty()
						? EVisibility::HitTestInvisible : EVisibility::Collapsed;
				})
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 12))
				.ColorAndOpacity(FSlateColor(FLinearColor(1.f, 0.35f, 0.3f)))
				.AutoWrapText(true)
			]
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.f)
		[
			SNew(SBox)
			.IsEnabled_Lambda([WeakThis]()
			{
				const URPGInputOptionMenu* Menu = WeakThis.Get();
				return Menu && !Menu->IsAwaitingDefaultRestoration() && !Menu->bRestoringDefaults;
			})
			[
				Content
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 8.f, 0.f, 0.f))
		[
			SNew(SVerticalBox)
			.Visibility_Lambda([WeakThis]()
			{
				const URPGInputOptionMenu* Menu = WeakThis.Get();
				return Menu && (Menu->bShowNativeDefaultsActions || Menu->IsAwaitingDefaultRestoration())
					? EVisibility::Visible : EVisibility::Collapsed;
			})
			+ SVerticalBox::Slot().AutoHeight()
			[
				RestoreButton
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				ConfirmationPanel
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 4.f, 0.f, 0.f))
			[
				SNew(STextBlock)
				.Text_Lambda([WeakThis]()
				{
					const URPGInputOptionMenu* Menu = WeakThis.Get();
					return Menu ? Menu->RestoreDefaultsStatusMessage : FText::GetEmpty();
				})
				.ColorAndOpacity_Lambda([WeakThis]()
				{
					const URPGInputOptionMenu* Menu = WeakThis.Get();
					return Menu && Menu->bLastRestoreFailed
						? FSlateColor(FLinearColor(1.f, 0.35f, 0.3f)) : FSlateColor::UseForeground();
				})
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 12))
				.AutoWrapText(true)
			]
		];
}

void URPGInputOptionMenu::NativeConstruct()
{
	bMenuConstructed = true;
	UnbindInputOptionsChanges();
	bInputRefreshPending = false;
	bInputRefreshPaused = false;
	if (!IsDesignTime())
	{
		InputOptionsChangedHandle = IRPGInputOptionsRuntime::OnMappingsChanged().AddUObject(
			this, &ThisClass::HandleInputOptionMappingsChanged);
	}
	LastBindingChangeError = FText::GetEmpty();
	if (MenuVisibilityHandle.IsValid()) { OnNativeVisibilityChanged.Remove(MenuVisibilityHandle); }
	MenuVisibilityHandle = OnNativeVisibilityChanged.AddUObject(this, &ThisClass::HandleInputMenuVisibilityChanged);
	Super::NativeConstruct();
	if (bMenuConstructed && !IsRemovingFromParent()) { RefreshMappings(); }
}

void URPGInputOptionMenu::NativePreConstruct()
{
	Super::NativePreConstruct();
	if (IsDesignTime()) { RefreshMappings(); }
}

void URPGInputOptionMenu::UnbindRows()
{
	++BindingRowsRevision;
	BindingRows.Reset();
	VisibleBindingCount = 0;
	TotalBindingCount = 0;
	if (MappingScrollBox)
	{
		for (UWidget* Child : MappingScrollBox->GetAllChildren())
		{
			if (URPGInputMappingRow* Row = Cast<URPGInputMappingRow>(Child))
			{
				Row->OnBindingRefreshRequested.RemoveAll(this);
				Row->OnBindingChanged.RemoveAll(this);
			}
		}
	}
}

void URPGInputOptionMenu::NativeDestruct()
{
	bMenuConstructed = false;
	bInputRefreshPending = false;
	bInputRefreshPaused = true;
	UnbindInputOptionsChanges();
	LastBindingChangeError = FText::GetEmpty();
	CancelPendingKeySelection();
	OnNativeVisibilityChanged.Remove(MenuVisibilityHandle);
	MenuVisibilityHandle.Reset();
	UnbindRows();
	Super::NativeDestruct();
}

void URPGInputOptionMenu::BeginDestroy()
{
	bMenuConstructed = false;
	bInputRefreshPending = false;
	UnbindInputOptionsChanges();
	Super::BeginDestroy();
}

void URPGInputOptionMenu::UnbindInputOptionsChanges()
{
	if (InputOptionsChangedHandle.IsValid())
	{
		IRPGInputOptionsRuntime::OnMappingsChanged().Remove(InputOptionsChangedHandle);
		InputOptionsChangedHandle.Reset();
	}
	CancelScheduledInputRefresh();
}

void URPGInputOptionMenu::CancelScheduledInputRefresh()
{
	++InputRefreshGeneration;
	UWorld* World = InputRefreshWorld.Get();
	if (IsValid(World) && !World->IsUnreachable() && !World->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed))
	{
		World->GetTimerManager().ClearTimer(InputRefreshTimer);
	}
	InputRefreshTimer.Invalidate();
	InputRefreshWorld.Reset();
	InputRefreshPlayer.Reset();
	InputRefreshController.Reset();
}

void URPGInputOptionMenu::HandleInputOptionMappingsChanged(ULocalPlayer* Player, UWorld* World)
{
	if (!bMenuConstructed || IsRemovingFromParent() || IsDesignTime() || IsUnreachable()
		|| HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) || GetOwningLocalPlayer() != Player || GetWorld() != World) { return; }
	RequestInputMappingRefresh();
}

void URPGInputOptionMenu::RequestInputMappingRefresh()
{
	if (!bMenuConstructed || IsRemovingFromParent() || IsDesignTime() || IsUnreachable()
		|| HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed)) { return; }
	bInputRefreshPending = true;
	if (bInputRefreshPaused || !IsVisible()) { return; }
	UWorld* World = GetWorld();
	ULocalPlayer* Player = GetOwningLocalPlayer();
	APlayerController* Controller = GetOwningPlayer();
	if (!IsValid(World) || World->bIsTearingDown || !IsValid(Player) || !IsValid(Controller)
		|| Controller->IsActorBeingDestroyed() || !Controller->IsLocalController()
		|| Player->GetPlayerController(nullptr) != Controller || Controller->GetWorld() != World) { return; }
	if (InputRefreshTimer.IsValid())
	{
		if (InputRefreshWorld.Get() == World && InputRefreshPlayer.Get() == Player
			&& InputRefreshController.Get() == Controller && World->GetTimerManager().TimerExists(InputRefreshTimer)) { return; }
		CancelScheduledInputRefresh();
	}
	const uint64 Generation = ++InputRefreshGeneration;
	const TWeakObjectPtr<UWorld> ExpectedWorld(World);
	const TWeakObjectPtr<ULocalPlayer> ExpectedPlayer(Player);
	const TWeakObjectPtr<APlayerController> ExpectedController(Controller);
	InputRefreshWorld = World;
	InputRefreshPlayer = Player;
	InputRefreshController = Controller;
	InputRefreshTimer = World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this,
		[this, Generation, ExpectedWorld, ExpectedPlayer, ExpectedController]()
		{
			FlushInputMappingRefresh(Generation, ExpectedWorld, ExpectedPlayer, ExpectedController);
		}));
}

void URPGInputOptionMenu::FlushInputMappingRefresh(uint64 Generation, TWeakObjectPtr<UWorld> ExpectedWorld,
	TWeakObjectPtr<ULocalPlayer> ExpectedPlayer, TWeakObjectPtr<APlayerController> ExpectedController)
{
	if (Generation != InputRefreshGeneration) { return; }
	InputRefreshTimer.Invalidate();
	InputRefreshWorld.Reset();
	InputRefreshPlayer.Reset();
	InputRefreshController.Reset();
	TStrongObjectPtr<URPGInputOptionMenu> KeepAlive(this);
	const auto IsCurrentOwner = [&]()
	{
		return bMenuConstructed && !IsRemovingFromParent() && !IsUnreachable()
			&& !HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) && ExpectedWorld.IsValid()
			&& !ExpectedWorld->bIsTearingDown && GetWorld() == ExpectedWorld.Get()
			&& ExpectedPlayer.IsValid() && GetOwningLocalPlayer() == ExpectedPlayer.Get()
			&& ExpectedController.IsValid() && GetOwningPlayer() == ExpectedController.Get()
			&& !ExpectedController->IsActorBeingDestroyed() && ExpectedController->IsLocalController()
			&& ExpectedPlayer->GetPlayerController(nullptr) == ExpectedController.Get()
			&& ExpectedController->GetWorld() == ExpectedWorld.Get();
	};
	if (!bInputRefreshPending || bInputRefreshPaused || !IsVisible() || !IsCurrentOwner()) { return; }
	if (bRefreshingMappings || bRefreshingDisplayedKeys || bApplyingRowFilters || bCancellingSelections || bRestoringDefaults)
	{
		RequestInputMappingRefresh();
		return;
	}
	const bool bHadConfirmation = IsAwaitingDefaultRestoration();
	RefreshMappings();
	if (bHadConfirmation && !IsAwaitingDefaultRestoration() && IsCurrentOwner())
	{
		FocusDefaultRestorationControl(bShowNativeBindingFilters ? NativeFilterFocusTarget.Pin() : GetCachedWidget());
	}
}

void URPGInputOptionMenu::CancelDefaultsForInputChange()
{
	if (!IsAwaitingDefaultRestoration()) { return; }
	CancelRestoreAllDefaults();
	bLastRestoreFailed = true;
	RestoreDefaultsStatusMessage = NSLOCTEXT("RPGInputOptions", "DefaultsInvalidated",
		"입력 설정이 변경되어 기본값 복원 확인을 취소했습니다. 다시 요청해 주세요.");
}

void URPGInputOptionMenu::NativeOnActivated()
{
	bInputRefreshPaused = false;
	Super::NativeOnActivated();
	if (bMenuConstructed && !bInputRefreshPaused) { RequestInputMappingRefresh(); }
}

void URPGInputOptionMenu::NativeOnDeactivated()
{
	bInputRefreshPaused = true;
	bInputRefreshPending = true;
	CancelScheduledInputRefresh();
	CancelPendingKeySelection();
	Super::NativeOnDeactivated();
}

void URPGInputOptionMenu::HandleInputMenuVisibilityChanged(ESlateVisibility NewVisibility)
{
	if (NewVisibility == ESlateVisibility::Hidden || NewVisibility == ESlateVisibility::Collapsed)
	{
		bInputRefreshPending = true;
		CancelScheduledInputRefresh();
		CancelPendingKeySelection();
	}
	else if (bInputRefreshPending) { RequestInputMappingRefresh(); }
}

void URPGInputOptionMenu::CancelPendingKeySelection()
{
	CancelRestoreAllDefaults();
	if (bCancellingSelections || !FSlateApplication::IsInitialized() || IsUnreachable()
		|| HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed)) { return; }
	TStrongObjectPtr<URPGInputOptionMenu> KeepAlive(this);
	TGuardValue<bool> Guard(bCancellingSelections, true);
	TStrongObjectPtr<UScrollBox> ScrollBox(MappingScrollBox.Get());
	if (!ScrollBox.IsValid()) { return; }
	TArray<TWeakObjectPtr<URPGInputMappingRow>> Rows;
	for (UWidget* Child : ScrollBox->GetAllChildren())
	{
		if (URPGInputMappingRow* Row = Cast<URPGInputMappingRow>(Child)) { Rows.Add(Row); }
	}
	for (const TWeakObjectPtr<URPGInputMappingRow>& WeakRow : Rows)
	{
		if (URPGInputMappingRow* Row = WeakRow.Get(); Row && Row->GetParent() == ScrollBox.Get())
		{
			Row->CancelKeySelection();
		}
	}
	ScrollBox->EndInertialScrolling();
}

bool URPGInputOptionMenu::CanManageDefaultRestoration() const
{
	const ARPGPlayerController* Controller = Cast<ARPGPlayerController>(GetOwningPlayer());
	if (!bMenuConstructed || IsDesignTime() || IsRemovingFromParent() || bRestoringDefaults
		|| bRefreshingMappings || bApplyingRowFilters || bCancellingSelections || !IsVisible() || !GetIsEnabled()
		|| !IsValid(Controller) || !Controller->IsLocalController() || Controller->IsActorBeingDestroyed()
		|| !Controller->GetLocalPlayer() || !GetWorld() || GetWorld()->bIsTearingDown
		|| Controller->GetWorld() != GetWorld() || !FSlateApplication::IsInitialized()) { return false; }
	const TSharedPtr<SWidget> Root = GetCachedWidget();
	FWidgetPath Path;
	if (!Root || !FSlateApplication::Get().GeneratePathToWidgetUnchecked(Root.ToSharedRef(), Path)) { return false; }
	for (int32 Index = 0; Index < Path.Widgets.Num(); ++Index)
	{
		const TSharedRef<SWidget>& PathWidget = Path.Widgets[Index].Widget;
		if (!PathWidget->IsEnabled() || !PathWidget->GetVisibility().IsVisible()) { return false; }
	}
	return true;
}

int32 URPGInputOptionMenu::GetRestorableBindingCount() const
{
	int32 Count = 0;
	for (const FBindingRow& Entry : BindingRows)
	{
		const URPGInputMappingRow* Row = Entry.Widget.Get();
		if (Row && Row->GetParent() == MappingScrollBox.Get() && Entry.Binding.bCanRemap
			&& Entry.Binding.DefaultKey.IsValid() && Row->GetCurrentBindingKey().IsValid()
			&& Row->GetCurrentBindingKey() != Entry.Binding.DefaultKey) { ++Count; }
	}
	return Count;
}

bool URPGInputOptionMenu::CanRestoreAllDefaults() const
{
	return CanManageDefaultRestoration() && !IsAwaitingDefaultRestoration()
		&& !HasPendingKeySelection() && GetRestorableBindingCount() > 0;
}

void URPGInputOptionMenu::FocusDefaultRestorationControl(const TSharedPtr<SWidget>& Target) const
{
	if (!CanManageDefaultRestoration() || !Target || !Target->SupportsKeyboardFocus()) { return; }
	ULocalPlayer* Player = GetOwningLocalPlayer();
	const TSharedPtr<FSlateUser> User = Player ? Player->GetSlateUser() : nullptr;
	const TSharedPtr<SWidget> Root = GetCachedWidget();
	FWidgetPath Path;
	if (!User || !Root || !User->IsWidgetInFocusPath(Root)
		|| !FSlateApplication::Get().GeneratePathToWidgetUnchecked(Target.ToSharedRef(), Path)
		|| !Path.ContainsWidget(Root.Get())) { return; }
	for (int32 Index = 0; Index < Path.Widgets.Num(); ++Index)
	{
		const TSharedRef<SWidget>& PathWidget = Path.Widgets[Index].Widget;
		if (!PathWidget->IsEnabled() || !PathWidget->GetVisibility().IsVisible()) { return; }
	}
	FSlateApplication::Get().SetUserFocus(User->GetUserIndex(), Target, EFocusCause::SetDirectly);
}

bool URPGInputOptionMenu::RequestRestoreAllDefaults()
{
	if (!CanRestoreAllDefaults()) { return false; }
	IRPGInputOptionsRuntime* Runtime = IRPGInputOptionsRuntime::Get();
	if (!Runtime) { return false; }
	TStrongObjectPtr<URPGInputOptionMenu> KeepAlive(this);
	TArray<FRPGInputOptionBinding> Bindings;
	Runtime->BuildDefaultRestoration(Cast<ARPGPlayerController>(GetOwningPlayer()), Bindings);
	if (Bindings.IsEmpty() || !CanRestoreAllDefaults()) { return false; }
	LastBindingChangeError = FText::GetEmpty();
	PendingDefaultBindings = MoveTemp(Bindings);
	PendingDefaultsController = Cast<ARPGPlayerController>(GetOwningPlayer());
	PendingDefaultsWorld = GetWorld();
	++DefaultRestorationRevision;
	RestoreDefaultsStatusMessage = FText::GetEmpty();
	bLastRestoreFailed = false;
	// Default focus is cancellation; confirming requires an explicit choice.
	FocusDefaultRestorationControl(NativeDefaultsCancelButton.Pin());
	return true;
}

void URPGInputOptionMenu::CancelRestoreAllDefaults()
{
	if (!IsAwaitingDefaultRestoration()) { return; }
	TStrongObjectPtr<URPGInputOptionMenu> KeepAlive(
		!IsUnreachable() && !HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ? this : nullptr);
	bool bRestoreFocus = false;
	if (FSlateApplication::IsInitialized())
	{
		ULocalPlayer* Player = GetOwningLocalPlayer();
		const TSharedPtr<FSlateUser> User = Player ? Player->GetSlateUser() : nullptr;
		const TSharedPtr<SWidget> Panel = NativeDefaultsConfirmationPanel.Pin();
		bRestoreFocus = User && Panel && User->IsWidgetInFocusPath(Panel);
	}
	PendingDefaultBindings.Reset();
	PendingDefaultsController.Reset();
	PendingDefaultsWorld.Reset();
	++DefaultRestorationRevision;
	if (bRestoreFocus)
	{
		FocusDefaultRestorationControl(bShowNativeDefaultsActions
			? NativeRestoreDefaultsButton.Pin() : GetCachedWidget());
	}
}

bool URPGInputOptionMenu::ConfirmRestoreAllDefaults()
{
	if (!CanManageDefaultRestoration() || !IsAwaitingDefaultRestoration() || HasPendingKeySelection()) { return false; }
	TStrongObjectPtr<URPGInputOptionMenu> KeepAlive(this);
	TStrongObjectPtr<ARPGPlayerController> Controller(PendingDefaultsController.Get());
	if (!Controller.IsValid() || GetOwningPlayer() != Controller.Get() || PendingDefaultsWorld.Get() != GetWorld())
	{
		CancelRestoreAllDefaults();
		return false;
	}
	const uint64 Revision = DefaultRestorationRevision;
	const TWeakObjectPtr<UWorld> RequestWorld(PendingDefaultsWorld);
	const TArray<FRPGInputOptionBinding> Bindings(PendingDefaultBindings);
	FText Error;
	bool bSucceeded = false;
	{
		TGuardValue<bool> RestoringGuard(bRestoringDefaults, true);
		IRPGInputOptionsRuntime* Runtime = IRPGInputOptionsRuntime::Get();
		if (Runtime) { bSucceeded = Runtime->TryRestoreDefaults(Controller.Get(), Bindings, Error); }
	}
	// Callbacks may close/rebuild the menu or hand the widget to another player.
	if (Revision != DefaultRestorationRevision || !bMenuConstructed || IsRemovingFromParent()
		|| GetOwningPlayer() != Controller.Get() || PendingDefaultsWorld.Get() != GetWorld()) { return bSucceeded; }
	{
		// Keep focus on the old panel until refreshed rows provide a usable destination.
		TGuardValue<bool> RestoringGuard(bRestoringDefaults, true);
		CancelRestoreAllDefaults();
	}
	const uint64 CompletedRevision = DefaultRestorationRevision;
	bLastRestoreFailed = !bSucceeded;
	RestoreDefaultsStatusMessage = bSucceeded
		? NSLOCTEXT("RPGInputOptions", "DefaultsRestored", "기본 키를 적용했습니다.")
		: (!Error.IsEmpty() ? Error : NSLOCTEXT("RPGInputOptions", "DefaultsUnavailable", "키 설정을 복원할 수 없습니다. 다시 시도해 주세요."));
	RefreshMappings();
	if (CompletedRevision == DefaultRestorationRevision && bMenuConstructed && !IsRemovingFromParent()
		&& GetOwningPlayer() == Controller.Get() && RequestWorld.Get() == GetWorld())
	{
		FocusDefaultRestorationControl(bShowNativeBindingFilters ? NativeFilterFocusTarget.Pin() : GetCachedWidget());
	}
	return bSucceeded;
}

FText URPGInputOptionMenu::GetDefaultRestorationPrompt() const
{
	return FText::Format(NSLOCTEXT("RPGInputOptions", "RestoreAllDefaultsWithFeaturesPrompt",
		"변경한 키 {0}개를 기본값으로 복원하시겠습니까? 추가 기능 입력과 필터로 숨긴 항목도 포함됩니다."),
		FText::AsNumber(PendingDefaultBindings.Num()));
}

FReply URPGInputOptionMenu::HandleRestoreDefaultsClicked()
{
	RequestRestoreAllDefaults();
	return FReply::Handled();
}

FReply URPGInputOptionMenu::HandleConfirmDefaultsClicked()
{
	ConfirmRestoreAllDefaults();
	return FReply::Handled();
}

FReply URPGInputOptionMenu::HandleCancelDefaultsClicked()
{
	CancelRestoreAllDefaults();
	return FReply::Handled();
}

void URPGInputOptionMenu::HandleBindingRefreshRequested(TWeakObjectPtr<URPGInputMappingRow> RequestedRow)
{
	URPGInputMappingRow* Row = RequestedRow.Get();
	if (!bMenuConstructed || IsRemovingFromParent() || !IsValid(Row) || Row->GetParent() != MappingScrollBox.Get()
		|| Row->GetOwningPlayer() != GetOwningPlayer()) { return; }
	LastBindingChangeError = Row->LastMappingError;
	RefreshMappings();
}

void URPGInputOptionMenu::RefreshMappings()
{
	if (bRefreshingMappings || bRefreshingDisplayedKeys || bApplyingRowFilters || bCancellingSelections || bRestoringDefaults
		|| !MappingScrollBox || !RowWidgetClass || IsRemovingFromParent()
		|| (!IsDesignTime() && !bMenuConstructed)) { return; }
	TStrongObjectPtr<URPGInputOptionMenu> KeepAlive(this);
	TGuardValue<bool> Guard(bRefreshingMappings, true);
	CancelScheduledInputRefresh();
	const uint64 InputGeneration = InputRefreshGeneration;
	const bool bDesignTime = IsDesignTime();
	TStrongObjectPtr<ARPGPlayerController> Controller(Cast<ARPGPlayerController>(GetOwningPlayer()));
	TStrongObjectPtr<const UDataAsset_InputConfig> Config(
		Controller.IsValid() ? Controller->GetSourceInputConfig() : InputConfig.Get());
	TStrongObjectPtr<UScrollBox> ScrollBox(MappingScrollBox.Get());
	TStrongObjectPtr<UWorld> BindingWorld(GetWorld());
	const TStrongObjectPtr<ULocalPlayer> BindingPlayer(GetOwningLocalPlayer());
	const TStrongObjectPtr<UClass> RequestedRowClass(RowWidgetClass.Get());
	const TStrongObjectPtr<UDataAsset_KeyIconConfig> RequestedIcons(KeyIconConfig.Get());
	const auto IsCurrentMenu = [&]()
	{
		return !IsUnreachable() && !HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed)
			&& MappingScrollBox == ScrollBox.Get() && RowWidgetClass.Get() == RequestedRowClass.Get()
			&& KeyIconConfig.Get() == RequestedIcons.Get() && !IsRemovingFromParent()
			&& (bDesignTime || (bMenuConstructed && IsValid(Controller.Get()) && !Controller->IsActorBeingDestroyed()
				&& GetOwningPlayer() == Controller.Get() && Controller->IsLocalController()
				&& IsValid(BindingPlayer.Get()) && GetOwningLocalPlayer() == BindingPlayer.Get()
				&& Controller->GetLocalPlayer() == BindingPlayer.Get()
				&& BindingPlayer->GetPlayerController(nullptr) == Controller.Get() && IsValid(BindingWorld.Get()) && !BindingWorld->bIsTearingDown
				&& GetWorld() == BindingWorld.Get() && Controller->GetWorld() == BindingWorld.Get()
				&& Controller->GetSourceInputConfig() == Config.Get()));
	};
	if (!Config.IsValid() || !IsCurrentMenu()) { return; }
	IRPGInputOptionsRuntime* Runtime = IRPGInputOptionsRuntime::Get();
	if (!Runtime || !IsCurrentMenu()) { return; }
	TArray<FRPGInputOptionBinding> Rows;
	Runtime->BuildRows(bDesignTime ? nullptr : Controller.Get(), Config.Get(), Rows);
	if (!IsCurrentMenu()) { return; }
	if (InputGeneration == InputRefreshGeneration) { bInputRefreshPending = false; }
	if (!bDesignTime && Controller.IsValid()
		&& !Config->FindNativeInputActionByTag(RPGGameplayTags::InputTag_ShowSkillWindow))
	{
		const FKey Key = Controller->GetCurrentKeyForTag(RPGGameplayTags::InputTag_ShowSkillWindow);
		if (Key.IsValid())
		{
			FRPGInputOptionBinding& Row = Rows.AddDefaulted_GetRef();
			Row.InputTag = RPGGameplayTags::InputTag_ShowSkillWindow;
			Row.DisplayName = NSLOCTEXT("RPGSkillUI", "OpenSkillWindow", "Skill Window");
			Row.CurrentKey = Key;
			Row.bCanRemap = true;
		}
	}
	// Reopening or refreshing values must not recreate unchanged row widgets and discard their focus/scroll state.
	bool bReuseRows = BuiltRowsController.Get() == Controller.Get()
		&& BuiltRowWidgetClass.Get() == RowWidgetClass.Get() && BuiltKeyIconConfig.Get() == KeyIconConfig.Get()
		&& Rows.Num() == BindingRows.Num() && MappingScrollBox->GetChildrenCount() == Rows.Num();
	for (int32 Index = 0; bReuseRows && Index < Rows.Num(); ++Index)
	{
		const FBindingRow& Entry = BindingRows[Index];
		const URPGInputMappingRow* Row = Entry.Widget.Get();
		const FRPGInputOptionBinding& Binding = Rows[Index];
		bReuseRows = Row && Row->GetParent() == MappingScrollBox.Get()
			&& MappingScrollBox->GetChildAt(Index) == Row && Row->GetClass() == RowWidgetClass.Get()
			&& Entry.Binding.SourceConfig == Binding.SourceConfig && Entry.Binding.Action == Binding.Action
			&& Entry.Binding.bIsAdditionalMapping == Binding.bIsAdditionalMapping
			&& Entry.Binding.AdditionalInputProvider == Binding.AdditionalInputProvider
			&& Entry.Binding.SourceMappingContext == Binding.SourceMappingContext
			&& Entry.Binding.AppliedMappingContext == Binding.AppliedMappingContext
			&& Entry.Binding.InputTag == Binding.InputTag && Entry.Binding.DefaultKey == Binding.DefaultKey
			&& Entry.Binding.DisplayName.EqualTo(Binding.DisplayName) && Entry.Binding.bCanRemap == Binding.bCanRemap;
	}
	if (bReuseRows)
	{
		bool bKeysChanged = false;
		for (int32 Index = 0; Index < Rows.Num(); ++Index)
		{
			bKeysChanged |= BindingRows[Index].Binding.CurrentKey != Rows[Index].CurrentKey;
		}
		if (bKeysChanged && (IsAwaitingDefaultRestoration() || HasPendingKeySelection()))
		{
			const uint64 Revision = BindingRowsRevision;
			CancelDefaultsForInputChange();
			CancelPendingKeySelection();
			if (Revision != BindingRowsRevision || !IsCurrentMenu()) { return; }
			bool bSameRows = Rows.Num() == BindingRows.Num() && ScrollBox->GetChildrenCount() == Rows.Num();
			for (int32 Index = 0; bSameRows && Index < Rows.Num(); ++Index)
			{
				bSameRows = BindingRows[Index].Widget.IsValid()
					&& ScrollBox->GetChildAt(Index) == BindingRows[Index].Widget.Get();
			}
			if (!bSameRows) { RequestInputMappingRefresh(); return; }
		}
		for (int32 Index = 0; Index < Rows.Num(); ++Index) { BindingRows[Index].Binding = Rows[Index]; }
		// Value refresh now owns reentry protection, allowing the filter's focus repair after a row disappears.
		TGuardValue<bool> ValuesGuard(bRefreshingMappings, false);
		RefreshDisplayedKeys();
		return;
	}

	const float ExistingOffset = ScrollBox->GetScrollOffset();
	const float RestoreOffset = FMath::IsFinite(ExistingOffset) ? FMath::Max(0.f, ExistingOffset) : 0.f;
	CancelDefaultsForInputChange();
	CancelPendingKeySelection();
	if (!IsCurrentMenu()) { return; }
	UnbindRows();
	const uint64 Revision = BindingRowsRevision;
	ScrollBox->ClearChildren();
	for (const FRPGInputOptionBinding& Binding : Rows)
	{
		if (Revision != BindingRowsRevision || !IsCurrentMenu()) { return; }
		TStrongObjectPtr<URPGInputMappingRow> Row(CreateWidget<URPGInputMappingRow>(this, RequestedRowClass.Get()));
		if (Revision != BindingRowsRevision || !IsCurrentMenu()) { return; }
		if (!Row.IsValid()) { continue; }
		Row->InitializeOptionBinding(Binding, RequestedIcons.Get());
		if (Revision != BindingRowsRevision || !IsCurrentMenu()) { return; }
		Row->OnBindingRefreshRequested.AddUObject(this, &ThisClass::HandleBindingRefreshRequested,
			TWeakObjectPtr<URPGInputMappingRow>(Row.Get()));
		FBindingRow& Entry = BindingRows.AddDefaulted_GetRef();
		Entry.Widget = Row.Get();
		Entry.Binding = Binding;
		Entry.UnfilteredVisibility = Row->GetVisibility();
		ScrollBox->AddChild(Row.Get());
	}
	if (Revision != BindingRowsRevision || !IsCurrentMenu()) { return; }
	BuiltRowsController = Controller.Get();
	BuiltRowWidgetClass = RowWidgetClass.Get();
	BuiltKeyIconConfig = KeyIconConfig.Get();
	ApplyRowFilters();
	if (Revision == BindingRowsRevision && IsCurrentMenu())
	{
		ScrollBox->SetScrollOffset(RestoreOffset);
	}
}

void URPGInputOptionMenu::RefreshDisplayedKeys()
{
	if (bRefreshingDisplayedKeys || !MappingScrollBox || bCancellingSelections || IsRemovingFromParent()
		|| (!IsDesignTime() && !bMenuConstructed)) { return; }
	TStrongObjectPtr<URPGInputOptionMenu> KeepAlive(this);
	TGuardValue<bool> RefreshingGuard(bRefreshingDisplayedKeys, true);
	TStrongObjectPtr<UScrollBox> ScrollBox(MappingScrollBox.Get());
	TStrongObjectPtr<ARPGPlayerController> Controller(Cast<ARPGPlayerController>(GetOwningPlayer()));
	TStrongObjectPtr<UWorld> BindingWorld(GetWorld());
	const TStrongObjectPtr<ULocalPlayer> BindingPlayer(GetOwningLocalPlayer());
	const uint64 Revision = BindingRowsRevision;
	const bool bDesignTime = IsDesignTime();
	const auto IsCurrentRefresh = [&]()
	{
		return !IsUnreachable() && !HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed)
			&& Revision == BindingRowsRevision && MappingScrollBox == ScrollBox.Get() && !IsRemovingFromParent()
			&& (bDesignTime || (bMenuConstructed && IsValid(Controller.Get()) && !Controller->IsActorBeingDestroyed()
				&& GetOwningPlayer() == Controller.Get() && Controller->IsLocalController()
				&& IsValid(BindingPlayer.Get()) && GetOwningLocalPlayer() == BindingPlayer.Get()
				&& Controller->GetLocalPlayer() == BindingPlayer.Get()
				&& BindingPlayer->GetPlayerController(nullptr) == Controller.Get() && IsValid(BindingWorld.Get()) && !BindingWorld->bIsTearingDown
				&& GetWorld() == BindingWorld.Get() && Controller->GetWorld() == BindingWorld.Get()));
	};
	if (!IsCurrentRefresh()) { return; }
	TArray<TStrongObjectPtr<URPGInputMappingRow>> Rows;
	for (UWidget* Child : ScrollBox->GetAllChildren())
	{
		if (URPGInputMappingRow* Row = Cast<URPGInputMappingRow>(Child)) { Rows.Emplace(Row); }
	}
	for (const TStrongObjectPtr<URPGInputMappingRow>& Row : Rows)
	{
		if (!IsCurrentRefresh()) { return; }
		if (Row->GetParent() == ScrollBox.Get()) { Row->RefreshDisplayedKey(); }
	}
	if (IsCurrentRefresh()) { ApplyRowFilters(); }
}

bool URPGInputOptionMenu::HasPendingKeySelection() const
{
	for (const FBindingRow& Entry : BindingRows)
	{
		const URPGInputMappingRow* Row = Entry.Widget.Get();
		if (Row && Row->IsSelectingKey()) { return true; }
	}
	return false;
}

bool URPGInputOptionMenu::CanChangeBindingFilters() const
{
	if (bRefreshingMappings || bApplyingRowFilters || bCancellingSelections || bRestoringDefaults
		|| IsAwaitingDefaultRestoration() || IsRemovingFromParent()
		|| (!IsDesignTime() && !bMenuConstructed)) { return false; }
	return !HasPendingKeySelection();
}

void URPGInputOptionMenu::SetBindingSearchText(const FText& InSearchText)
{
	if (!CanChangeBindingFilters() || BindingSearchText.EqualTo(InSearchText)) { return; }
	BindingSearchText = InSearchText;
	ApplyRowFilters();
}

void URPGInputOptionMenu::SetBindingDeviceFilter(ERPGInputOptionDeviceFilter InDeviceFilter)
{
	if (!CanChangeBindingFilters() || BindingDeviceFilter == InDeviceFilter) { return; }
	if (InDeviceFilter != ERPGInputOptionDeviceFilter::All
		&& InDeviceFilter != ERPGInputOptionDeviceFilter::KeyboardMouse
		&& InDeviceFilter != ERPGInputOptionDeviceFilter::Gamepad) { return; }
	BindingDeviceFilter = InDeviceFilter;
	ApplyRowFilters();
}

void URPGInputOptionMenu::SetShowChangedBindingsOnly(bool bInChangedOnly)
{
	if (!CanChangeBindingFilters() || bShowChangedBindingsOnly == bInChangedOnly) { return; }
	bShowChangedBindingsOnly = bInChangedOnly;
	ApplyRowFilters();
}

void URPGInputOptionMenu::ResetBindingFilters()
{
	if (!CanChangeBindingFilters()) { return; }
	BindingSearchText = FText::GetEmpty();
	BindingDeviceFilter = ERPGInputOptionDeviceFilter::All;
	bShowChangedBindingsOnly = false;
	ApplyRowFilters();
}

FReply URPGInputOptionMenu::HandleDeviceFilterClicked(ERPGInputOptionDeviceFilter DeviceFilter)
{
	SetBindingDeviceFilter(DeviceFilter);
	return FReply::Handled();
}

FReply URPGInputOptionMenu::HandleResetFiltersClicked()
{
	ResetBindingFilters();
	return FReply::Handled();
}

void URPGInputOptionMenu::ApplyRowFilters()
{
	if (bApplyingRowFilters || bCancellingSelections || !MappingScrollBox || IsRemovingFromParent()
		|| (!IsDesignTime() && !bMenuConstructed)) { return; }
	IRPGInputOptionsRuntime* Runtime = IRPGInputOptionsRuntime::Get();
	if (!Runtime) { return; }

	TStrongObjectPtr<URPGInputOptionMenu> KeepAlive(this);
	const uint64 Revision = BindingRowsRevision;
	TSharedPtr<SWidget> HiddenRowFocus;
	TWeakObjectPtr<ULocalPlayer> FocusOwner;
	{
		TGuardValue<bool> Guard(bApplyingRowFilters, true);
		const TArray<FBindingRow> Snapshot(BindingRows);
		int32 VisibleCount = 0;
		int32 TotalCount = 0;
		for (const FBindingRow& Entry : Snapshot)
		{
			TStrongObjectPtr<URPGInputMappingRow> Row(Entry.Widget.Get());
			if (!Row.IsValid() || Row->GetParent() != MappingScrollBox.Get()) { continue; }
			FRPGInputOptionBinding Binding = Entry.Binding;
			Binding.CurrentKey = Row->GetCurrentBindingKey();
			const bool bMatches = Row->IsSelectingKey()
				|| Runtime->MatchesFilter(Binding, BindingSearchText, BindingDeviceFilter, bShowChangedBindingsOnly);
			const ESlateVisibility TargetRowVisibility = bMatches ? Entry.UnfilteredVisibility : ESlateVisibility::Collapsed;
			if (Row->GetVisibility() != TargetRowVisibility)
			{
				ULocalPlayer* Player = GetOwningLocalPlayer();
				const TSharedPtr<FSlateUser> User = Player ? Player->GetSlateUser() : nullptr;
				const TSharedPtr<SWidget> RowWidget = Row->GetCachedWidget();
				if (!bMatches && User && RowWidget && User->IsWidgetInFocusPath(RowWidget))
				{
					HiddenRowFocus = User->GetFocusedWidget();
					FocusOwner = Player;
				}
				Row->SetVisibility(TargetRowVisibility);
				if (Revision != BindingRowsRevision || IsRemovingFromParent()
					|| (!IsDesignTime() && !bMenuConstructed)) { return; }
			}
			++TotalCount;
			if (bMatches) { ++VisibleCount; }
		}
		TotalBindingCount = TotalCount;
		VisibleBindingCount = VisibleCount;
	}

	// A restored row may disappear under "changed only". Keep this player's focus in the menu.
	ULocalPlayer* Player = GetOwningLocalPlayer();
	const TSharedPtr<FSlateUser> User = Player ? Player->GetSlateUser() : nullptr;
	const TSharedPtr<SWidget> FocusTarget = NativeFilterFocusTarget.Pin();
	if (HiddenRowFocus && Player == FocusOwner.Get() && User && FocusTarget && bShowNativeBindingFilters
		&& Revision == BindingRowsRevision && CanChangeBindingFilters() && IsVisible()
		&& FocusTarget->IsEnabled() && FSlateApplication::IsInitialized()
		&& (!User->GetFocusedWidget() || User->GetFocusedWidget() == HiddenRowFocus))
	{
		FSlateApplication::Get().SetUserFocus(User->GetUserIndex(), FocusTarget);
	}
}

FText URPGInputOptionMenu::GetBindingFilterSummary() const
{
	if (TotalBindingCount == 0) { return NSLOCTEXT("RPGInputOptions", "NoBindings", "표시할 키 설정이 없습니다."); }
	if (VisibleBindingCount == 0)
	{
		return FText::Format(NSLOCTEXT("RPGInputOptions", "NoMatchingBindings", "조건에 맞는 키 설정이 없습니다. (전체 {0}개)"),
			FText::AsNumber(TotalBindingCount));
	}
	return FText::Format(NSLOCTEXT("RPGInputOptions", "BindingFilterCount", "키 설정 {0} / {1}개"),
		FText::AsNumber(VisibleBindingCount), FText::AsNumber(TotalBindingCount));
}
