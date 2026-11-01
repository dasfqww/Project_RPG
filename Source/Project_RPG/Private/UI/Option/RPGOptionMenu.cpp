// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Option/RPGOptionMenu.h"
#include "UI/HUD/RPGHUD.h"
#include "Input/Events.h"
#include "Layout/WidgetPath.h"
#include "Components/Button.h"
#include "Components/WidgetSwitcher.h"
#include "Manager/SoundManager.h"
#include "UI/Option/RPGInputOptionMenu.h"
#include "UI/Option/GraphicOptionMenu.h"
#include "UI/Option/SoundOptionMenu.h"
#include "Blueprint/WidgetTree.h"
#include "Engine/World.h"
#include "Engine/LocalPlayer.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "GameFramework/PlayerController.h"
#include "UObject/StrongObjectPtr.h"

URPGOptionMenu::URPGOptionMenu()
{
}

void URPGOptionMenu::NativeConstruct()
{
	Super::NativeConstruct();

	InitButtonMap();
	if (MenuVisibilityHandle.IsValid()) { OnNativeVisibilityChanged.Remove(MenuVisibilityHandle); }
	MenuVisibilityHandle = OnNativeVisibilityChanged.AddUObject(this, &ThisClass::HandleMenuVisibilityChanged);
}

void URPGOptionMenu::NativeDestruct()
{
	EndInputSession();
	if (MenuVisibilityHandle.IsValid())
	{
		OnNativeVisibilityChanged.Remove(MenuVisibilityHandle);
		MenuVisibilityHandle.Reset();
	}
	UnbindTabButtons();
    TabButtonMap.Reset();
    TabIndexMap.Reset();
    Super::NativeDestruct();
}

void URPGOptionMenu::UnbindTabButtons()
{
	for (const TPair<ESettingTabType, TObjectPtr<UButton>>& Pair : TabButtonMap)
	{
		if (UButton* Button = Pair.Value.Get())
		{
			Button->OnHovered.RemoveDynamic(this, &ThisClass::OnTabButtonHovered);
			Button->OnClicked.RemoveDynamic(this, &ThisClass::OnTabButtonClicked);
			Button->OnClicked.RemoveDynamic(this, &ThisClass::OnGraphicsTabClicked);
			Button->OnClicked.RemoveDynamic(this, &ThisClass::OnSoundTabClicked);
			Button->OnClicked.RemoveDynamic(this, &ThisClass::OnControlsTabClicked);
		}
	}
}

void URPGOptionMenu::InitButtonMap()
{
	UnbindTabButtons();
	TabButtonMap.Reset();
	TabIndexMap.Reset();

	TabButtonMap.Add(ESettingTabType::Graphics, GraphicOptionButton);
	TabButtonMap.Add(ESettingTabType::Sound, SoundOptionButton);
	TabButtonMap.Add(ESettingTabType::Controls, InputOptionButton);

	TabIndexMap.Add(ESettingTabType::Graphics, 0);
	TabIndexMap.Add(ESettingTabType::Sound, 1);
	TabIndexMap.Add(ESettingTabType::Controls, 2);

	for (const TPair<ESettingTabType, TObjectPtr<UButton>>& Pair : TabButtonMap)
	{
		if (Pair.Value)
		{
			Pair.Value->OnHovered.AddUniqueDynamic(this, &ThisClass::OnTabButtonHovered);
		}
	}
	if (GraphicOptionButton)
	{
		GraphicOptionButton->OnClicked.AddUniqueDynamic(this, &ThisClass::OnGraphicsTabClicked);
	}
	if (SoundOptionButton)
	{
		SoundOptionButton->OnClicked.AddUniqueDynamic(this, &ThisClass::OnSoundTabClicked);
	}
	if (InputOptionButton)
	{
		InputOptionButton->OnClicked.AddUniqueDynamic(this, &ThisClass::OnControlsTabClicked);
	}
}

void URPGOptionMenu::OnTabButtonHovered()
{
	USoundManager::Get(this)->Play(HoverButtonSound);
}

void URPGOptionMenu::SelectTab(ESettingTabType Tab)
{
	const int32* Index = TabIndexMap.Find(Tab);
	if (bEndingInputSession || IsRemovingFromParent() || !IsValid(TabSwitcher)
		|| !Index || *Index < 0 || *Index >= TabSwitcher->GetChildrenCount())
	{
		return;
	}

	const int32 SelectedIndex = *Index;
	TStrongObjectPtr<URPGOptionMenu> KeepAlive(this);
	TStrongObjectPtr<UWidgetSwitcher> Switcher(TabSwitcher.Get());
	if (SelectedIndex != Switcher->GetActiveWidgetIndex())
	{
		CancelPendingInputSelections();
		CancelGraphicsPreviews();
	}
	if (bEndingInputSession || IsRemovingFromParent() || TabSwitcher != Switcher.Get()
		|| SelectedIndex >= Switcher->GetChildrenCount()) { return; }
	USoundManager::Get(this)->Play(ClickButtonSound);
	if (!bEndingInputSession && !IsRemovingFromParent() && TabSwitcher == Switcher.Get()
		&& SelectedIndex < Switcher->GetChildrenCount())
	{
		Switcher->SetActiveWidgetIndex(SelectedIndex);
	}
}

void URPGOptionMenu::OnGraphicsTabClicked()
{
	SelectTab(ESettingTabType::Graphics);
}

void URPGOptionMenu::OnSoundTabClicked()
{
	SelectTab(ESettingTabType::Sound);
}

void URPGOptionMenu::OnControlsTabClicked()
{
	SelectTab(ESettingTabType::Controls);
}

void URPGOptionMenu::OnTabButtonClicked()
{
	// Compatibility entry point; native click delegates use the explicit handlers above.
	for (const TPair<ESettingTabType, TObjectPtr<UButton>>& Pair : TabButtonMap)
	{
		if (Pair.Value && Pair.Value->IsHovered())
		{
			SelectTab(Pair.Key);
			return;
		}
	}
	APlayerController* Controller = GetOwningPlayer();
	for (const TPair<ESettingTabType, TObjectPtr<UButton>>& Pair : TabButtonMap)
	{
		if (Pair.Value && (Controller ? Pair.Value->HasUserFocus(Controller) : Pair.Value->HasKeyboardFocus()))
		{
			SelectTab(Pair.Key);
			return;
		}
	}
}

void URPGOptionMenu::OnMenuShown()
{
	if (bEndingInputSession || bShowingMenu || bClosingOptions || InputState.bActive || IsRemovingFromParent()) { return; }
	TStrongObjectPtr<URPGOptionMenu> KeepAlive(this);
	TGuardValue<bool> ShowingGuard(bShowingMenu, true);
	const TWeakObjectPtr<UWorld> OpeningWorld(GetWorld());
	const TWeakObjectPtr<APlayerController> OpeningPlayer(GetOwningPlayer());

	const auto IsOpeningValid = [this, OpeningWorld, OpeningPlayer]()
	{
		return !IsRemovingFromParent() && IsInViewport() && IsVisible()
			&& OpeningWorld.IsValid() && GetWorld() == OpeningWorld.Get() && !OpeningWorld->bIsTearingDown
			&& GetOwningPlayer() == OpeningPlayer.Get();
	};

	// Synchronize controls before the runtime policy chooses the focus target.
	TArray<TStrongObjectPtr<UGraphicOptionMenu>> GraphicsMenus;
	TArray<TStrongObjectPtr<USoundOptionMenu>> SoundMenus;
	TArray<TStrongObjectPtr<URPGInputOptionMenu>> InputMenus;
	if (IsValid(InputOptionMenu)) { InputMenus.Emplace(InputOptionMenu.Get()); }
	if (WidgetTree)
	{
		WidgetTree->ForEachWidget([this, &GraphicsMenus, &SoundMenus, &InputMenus](UWidget* ChildWidget)
		{
			if (URPGInputOptionMenu* InputMenu = Cast<URPGInputOptionMenu>(ChildWidget);
				InputMenu && InputMenu != InputOptionMenu.Get())
			{
				InputMenus.Emplace(InputMenu);
			}
			if (UGraphicOptionMenu* GraphicsMenu = Cast<UGraphicOptionMenu>(ChildWidget))
			{
				GraphicsMenus.Emplace(GraphicsMenu);
			}
			if (USoundOptionMenu* SoundMenu = Cast<USoundOptionMenu>(ChildWidget))
			{
				SoundMenus.Emplace(SoundMenu);
			}
		});
	}
	for (const TStrongObjectPtr<URPGInputOptionMenu>& InputMenu : InputMenus)
	{
		InputMenu->RefreshMappings();
		if (!IsOpeningValid()) { return; }
	}
	for (const TStrongObjectPtr<UGraphicOptionMenu>& GraphicsMenu : GraphicsMenus)
	{
		GraphicsMenu->InitializeOptions();
		if (!IsOpeningValid()) { return; }
	}
	for (const TStrongObjectPtr<USoundOptionMenu>& SoundMenu : SoundMenus)
	{
		SoundMenu->InitializeFromSavedSettings();
		if (!IsOpeningValid()) { return; }
	}

	IRPGOptionsMenuRuntime* Runtime = IRPGOptionsMenuRuntime::Get();
	if (!Runtime || !Runtime->AcquireInput(this, InputState))
	{
		if (!InputState.bActive) { SetVisibility(ESlateVisibility::Collapsed); }
	}
}

void URPGOptionMenu::OnMenuHidden()
{
    EndInputSession();
}

void URPGOptionMenu::HandleMenuVisibilityChanged(ESlateVisibility NewVisibility)
{
    if (NewVisibility == ESlateVisibility::Hidden || NewVisibility == ESlateVisibility::Collapsed)
    {
        EndInputSession();
    }
}

bool URPGOptionMenu::CanCloseOptionsMenu() const
{
    const APlayerController* Controller = GetOwningPlayer();
    const UWorld* World = GetWorld();
    return !bEndingInputSession && !bShowingMenu && !bClosingOptions && !IsRemovingFromParent()
        && !IsDesignTime() && IsInViewport() && IsVisible() && GetIsEnabled()
        && IsValid(Controller) && !Controller->IsActorBeingDestroyed() && Controller->IsLocalController()
        && Controller->GetLocalPlayer() && IsValid(World) && !World->bIsTearingDown
        && Controller->GetWorld() == World;
}

bool URPGOptionMenu::CanHandleOptionsNavigation() const
{
    if (bHandlingNavigation || !CanCloseOptionsMenu()) { return false; }
    const IRPGOptionsMenuRuntime* Runtime = IRPGOptionsMenuRuntime::Get(false);
    return Runtime && Runtime->CanNavigate(this, InputState);
}

bool URPGOptionMenu::HasPendingInputSelection() const
{
    if (IsValid(InputOptionMenu) && InputOptionMenu->HasPendingKeySelection()) { return true; }
    bool bSelectingKey = false;
    if (WidgetTree)
    {
        WidgetTree->ForEachWidget([&bSelectingKey](UWidget* ChildWidget)
        {
            const URPGInputOptionMenu* Menu = Cast<URPGInputOptionMenu>(ChildWidget);
            bSelectingKey |= Menu && Menu->HasPendingKeySelection();
        });
    }
    return bSelectingKey;
}

bool URPGOptionMenu::CloseOptionsMenu()
{
    if (!CanCloseOptionsMenu()) { return false; }
    TStrongObjectPtr<URPGOptionMenu> KeepAlive(this);
    TGuardValue<bool> ClosingGuard(bClosingOptions, true);
    ARPGHUD* HUD = Cast<ARPGHUD>(GetOwningPlayer()->GetHUD());
    if (IsValid(HUD) && HUD->GetOptionMenuWidget() == this)
    {
        HUD->HideOptionMenu();
    }
    else
    {
        OnMenuHidden();
        SetVisibility(ESlateVisibility::Collapsed);
    }
    return !IsInViewport() || !IsVisible();
}

bool URPGOptionMenu::RequestBack()
{
    if (!CanHandleOptionsNavigation()) { return false; }
    TStrongObjectPtr<URPGOptionMenu> KeepAlive(this);
    TGuardValue<bool> HandlingGuard(bHandlingNavigation, true);
    if (HasPendingInputSelection())
    {
        CancelPendingInputSelections();
        return true;
    }

    TArray<TWeakObjectPtr<URPGInputOptionMenu>> InputMenus;
    if (IsValid(InputOptionMenu)) { InputMenus.Add(InputOptionMenu.Get()); }
    if (WidgetTree)
    {
        WidgetTree->ForEachWidget([&InputMenus](UWidget* ChildWidget)
        {
            if (URPGInputOptionMenu* Menu = Cast<URPGInputOptionMenu>(ChildWidget)) { InputMenus.AddUnique(Menu); }
        });
    }
    for (const TWeakObjectPtr<URPGInputOptionMenu>& WeakMenu : InputMenus)
    {
        if (URPGInputOptionMenu* Menu = WeakMenu.Get(); Menu && Menu->IsAwaitingDefaultRestoration())
        {
            Menu->CancelRestoreAllDefaults();
            return true;
        }
    }

    TArray<TWeakObjectPtr<UGraphicOptionMenu>> GraphicsMenus;
    if (WidgetTree)
    {
        WidgetTree->ForEachWidget([&GraphicsMenus](UWidget* ChildWidget)
        {
            if (UGraphicOptionMenu* Menu = Cast<UGraphicOptionMenu>(ChildWidget)) { GraphicsMenus.Add(Menu); }
        });
    }
    for (const TWeakObjectPtr<UGraphicOptionMenu>& WeakMenu : GraphicsMenus)
    {
        TStrongObjectPtr<UGraphicOptionMenu> Menu(WeakMenu.Get());
        if (Menu.IsValid() && Menu->IsAwaitingDisplayConfirmation())
        {
            Menu->RevertDisplayChanges();
            // Applying settings can synchronously request back; defer cancellation through the existing owner.
            if (Menu->IsAwaitingDisplayConfirmation()) { Menu->CancelPendingGraphicsChange(); }
            return true;
        }
    }
    return CloseOptionsMenu();
}

bool URPGOptionMenu::SelectAdjacentTab(bool bNext)
{
    if (!CanHandleOptionsNavigation() || HasPendingInputSelection() || !IsValid(TabSwitcher)) { return false; }
    TStrongObjectPtr<URPGOptionMenu> KeepAlive(this);
    TStrongObjectPtr<UWidgetSwitcher> Switcher(TabSwitcher.Get());
    TGuardValue<bool> HandlingGuard(bHandlingNavigation, true);
    const FGuid SessionId = InputState.SessionId;
    TArray<TPair<int32, ESettingTabType>> Tabs;
    for (const auto& Pair : TabIndexMap)
    {
        const TObjectPtr<UButton>* Button = TabButtonMap.Find(Pair.Key);
        if (Pair.Value >= 0 && Pair.Value < Switcher->GetChildrenCount()
            && Button && IsValid(Button->Get()) && Button->Get()->IsVisible() && Button->Get()->GetIsEnabled())
        {
            Tabs.Emplace(Pair.Value, Pair.Key);
        }
    }
    Tabs.Sort([](const auto& Left, const auto& Right) { return Left.Key < Right.Key; });
    if (Tabs.Num() < 2) { return false; }
    const int32 ActiveIndex = Switcher->GetActiveWidgetIndex();
    const int32 Current = Tabs.IndexOfByPredicate([ActiveIndex](const auto& Pair) { return Pair.Key == ActiveIndex; });
    const int32 Next = Current == INDEX_NONE ? (bNext ? 0 : Tabs.Num() - 1)
        : (Current + (bNext ? 1 : Tabs.Num() - 1)) % Tabs.Num();
    const int32 TargetIndex = Tabs[Next].Key;
    SelectTab(Tabs[Next].Value);
    // Callbacks can close the menu, replace its owner, or change the selected tab again.
    const IRPGOptionsMenuRuntime* Runtime = IRPGOptionsMenuRuntime::Get(false);
    if (InputState.SessionId != SessionId || TabSwitcher != Switcher.Get()
        || Switcher->GetActiveWidgetIndex() != TargetIndex || !Runtime || !Runtime->CanNavigate(this, InputState))
    {
        return false;
    }
    ULocalPlayer* Player = InputState.LocalPlayer.Get();
    const TSharedPtr<FSlateUser> User = Player ? Player->GetSlateUser() : nullptr;
    UWidget* TargetWidget = GetSelectedTabFocusTarget();
    TSharedPtr<SWidget> Focus = IsValid(TargetWidget) ? TargetWidget->GetCachedWidget() : nullptr;
    const TSharedPtr<SWidget> Root = GetCachedWidget();
    FWidgetPath FocusPath;
    bool bUsableFocus = Focus && Focus->SupportsKeyboardFocus()
        && FSlateApplication::Get().GeneratePathToWidgetUnchecked(Focus.ToSharedRef(), FocusPath)
        && FocusPath.ContainsWidget(Root.Get());
    for (int32 Index = 0; bUsableFocus && Index < FocusPath.Widgets.Num(); ++Index)
    {
        const TSharedRef<SWidget>& Widget = FocusPath.Widgets[Index].Widget;
        bUsableFocus = Widget->IsEnabled() && Widget->GetVisibility().IsVisible();
    }
    if (!bUsableFocus) { Focus = Root; }
    if (User && Focus)
    {
        FSlateApplication::Get().SetUserFocus(User->GetUserIndex(), Focus, EFocusCause::SetDirectly);
    }
    return true;
}

bool URPGOptionMenu::NativeOnHandleBackAction()
{
    return bEnableNativeOptionNavigation && InputState.bActive
        ? RequestBack() : Super::NativeOnHandleBackAction();
}

FReply URPGOptionMenu::NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
    const IRPGOptionsMenuRuntime* Runtime = bEnableNativeOptionNavigation ? IRPGOptionsMenuRuntime::Get(false) : nullptr;
    const ERPGOptionsNavigationCommand Command = Runtime
        ? Runtime->GetNavigationCommand(Event) : ERPGOptionsNavigationCommand::None;
    if ((Command == ERPGOptionsNavigationCommand::NextTab || Command == ERPGOptionsNavigationCommand::PreviousTab)
        && CanHandleOptionsNavigation() && Event.GetUserIndex() == static_cast<uint32>(InputState.SlateUserIndex)
        && !HasPendingInputSelection())
    {
        TStrongObjectPtr<URPGOptionMenu> KeepAlive(this);
        const FGuid SessionId = InputState.SessionId;
        if (!Event.IsRepeat()) { SelectAdjacentTab(Command == ERPGOptionsNavigationCommand::NextTab); }
        if (InputState.bActive && InputState.SessionId == SessionId) { ConsumedNavigationKeys.Add(Event.GetKey()); }
        return FReply::Handled();
    }
    return Super::NativeOnPreviewKeyDown(Geometry, Event);
}

FReply URPGOptionMenu::NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
    const IRPGOptionsMenuRuntime* Runtime = bEnableNativeOptionNavigation ? IRPGOptionsMenuRuntime::Get(false) : nullptr;
    if (Runtime && Runtime->GetNavigationCommand(Event) == ERPGOptionsNavigationCommand::Back
        && CanHandleOptionsNavigation() && Event.GetUserIndex() == static_cast<uint32>(InputState.SlateUserIndex)
        && !HasPendingInputSelection())
    {
        // Child controls receive Back first; capture widgets and editable text keep their own key behavior.
        TStrongObjectPtr<URPGOptionMenu> KeepAlive(this);
        const FGuid SessionId = InputState.SessionId;
        if (!Event.IsRepeat()) { RequestBack(); }
        if (InputState.bActive && InputState.SessionId == SessionId) { ConsumedNavigationKeys.Add(Event.GetKey()); }
        return FReply::Handled();
    }
    return Super::NativeOnKeyDown(Geometry, Event);
}

FReply URPGOptionMenu::NativeOnKeyUp(const FGeometry& Geometry, const FKeyEvent& Event)
{
    if (InputState.bActive && Event.GetUserIndex() == static_cast<uint32>(InputState.SlateUserIndex)
        && ConsumedNavigationKeys.Remove(Event.GetKey()) > 0)
    {
        return FReply::Handled();
    }
    return Super::NativeOnKeyUp(Geometry, Event);
}

UWidget* URPGOptionMenu::GetSelectedTabFocusTarget() const
{
    if (!IsValid(TabSwitcher)) { return nullptr; }
    const int32 ActiveIndex = TabSwitcher->GetActiveWidgetIndex();
    for (const auto& Pair : TabIndexMap)
    {
        if (Pair.Value == ActiveIndex)
        {
            const TObjectPtr<UButton>* Button = TabButtonMap.Find(Pair.Key);
            return Button && IsValid(Button->Get()) ? Button->Get() : nullptr;
        }
    }
    return nullptr;
}

UWidget* URPGOptionMenu::NativeGetDesiredFocusTarget() const
{
    // Preserve authored Blueprint/default focus. The active tab button is the native fallback.
    if (UWidget* Desired = Super::NativeGetDesiredFocusTarget()) { return Desired; }
    return GetSelectedTabFocusTarget();
}

TSharedPtr<SWidget> URPGOptionMenu::GetRememberedOptionsFocus() const
{
    return RememberedFocusPlayer.IsValid() && RememberedFocusPlayer.Get() == GetOwningLocalPlayer()
        && RememberedFocusWorld.IsValid() && RememberedFocusWorld.Get() == GetWorld()
        && !RememberedFocusWorld->bIsTearingDown && IsValid(TabSwitcher)
        && RememberedFocusTabIndex == TabSwitcher->GetActiveWidgetIndex()
        ? RememberedOptionsFocus.Pin() : nullptr;
}

void URPGOptionMenu::RememberOptionsFocus()
{
    if (!InputState.bActive || !InputState.bApplied || !FSlateApplication::IsInitialized()
        || InputState.LocalPlayer.Get() != GetOwningLocalPlayer() || InputState.World.Get() != GetWorld()
        || !IsValid(TabSwitcher)) { return; }
    ULocalPlayer* Player = InputState.LocalPlayer.Get();
    const TSharedPtr<FSlateUser> User = Player ? Player->GetSlateUser() : nullptr;
    const TSharedPtr<SWidget> Root = InputState.Root.Pin();
    if (!User || User->GetUserIndex() != InputState.SlateUserIndex || !Root
        || !User->IsWidgetInFocusPath(Root)) { return; }
    const TSharedPtr<SWidget> Focus = User->GetFocusedWidget();
    // A root-only session has no useful control to remember; prefer the selected tab fallback.
    RememberedOptionsFocus = Focus != Root ? Focus : nullptr;
    RememberedFocusPlayer = Player;
    RememberedFocusWorld = GetWorld();
    RememberedFocusTabIndex = TabSwitcher->GetActiveWidgetIndex();
}

void URPGOptionMenu::NativeOnDeactivated()
{
    EndInputSession();
    Super::NativeOnDeactivated();
}

void URPGOptionMenu::CancelPendingInputSelections()
{
    TArray<TWeakObjectPtr<URPGInputOptionMenu>> Menus;
    if (IsValid(InputOptionMenu)) { Menus.Add(InputOptionMenu.Get()); }
    if (WidgetTree)
    {
        WidgetTree->ForEachWidget([&Menus](UWidget* ChildWidget)
        {
            if (URPGInputOptionMenu* Menu = Cast<URPGInputOptionMenu>(ChildWidget)) { Menus.AddUnique(Menu); }
        });
    }
    for (const TWeakObjectPtr<URPGInputOptionMenu>& WeakMenu : Menus)
    {
        if (URPGInputOptionMenu* Menu = WeakMenu.Get()) { Menu->CancelPendingKeySelection(); }
    }
}

void URPGOptionMenu::CancelGraphicsPreviews()
{
    if (!WidgetTree) { return; }
    TArray<TWeakObjectPtr<UGraphicOptionMenu>> Menus;
    WidgetTree->ForEachWidget([&Menus](UWidget* ChildWidget)
    {
        if (UGraphicOptionMenu* Menu = Cast<UGraphicOptionMenu>(ChildWidget)) { Menus.Add(Menu); }
    });
    for (const TWeakObjectPtr<UGraphicOptionMenu>& WeakMenu : Menus)
    {
        if (UGraphicOptionMenu* Menu = WeakMenu.Get()) { Menu->CancelPendingGraphicsChange(); }
    }
}

void URPGOptionMenu::EndInputSession()
{
    if (bEndingInputSession) { return; }
    TStrongObjectPtr<URPGOptionMenu> KeepAlive(
        !HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) && !IsUnreachable() ? this : nullptr);
    TGuardValue<bool> EndingGuard(bEndingInputSession, true);
    ConsumedNavigationKeys.Reset();
    RememberOptionsFocus();
    CancelPendingInputSelections();
    CancelGraphicsPreviews();
    if (!InputState.bActive) { return; }
    if (IRPGOptionsMenuRuntime* Runtime = IRPGOptionsMenuRuntime::Get(false))
    {
        const bool bRestore = !HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) && !IsUnreachable();
        Runtime->ReleaseInput(this, InputState, bRestore);
    }
    else
    {
        if (!HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) && !IsUnreachable())
        {
            SetIsFocusable(InputState.bPreviousFocusable);
        }
        InputState = FRPGOptionsMenuInputState();
    }
}

void URPGOptionMenu::RemoveFromParent()
{
    EndInputSession();
    Super::RemoveFromParent();
}
