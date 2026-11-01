#include "UI/Lobby/RPGOnlineLobbyNavigation.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetNavigation.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Components/PanelWidget.h"
#include "Components/VerticalBox.h"
#include "Engine/LocalPlayer.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "GameFramework/PlayerController.h"
#include "Layout/Children.h"
#include "UI/Lobby/RPGOnlineLobbyWidget.h"
#include "Widgets/SWidget.h"

namespace
{
	struct FControls
	{
		TSet<UWidget*> Visited;
		TArray<UWidget*> All;
		TArray<UWidget*> Available;
		TSet<UWidget*> ModalControls;
		bool bFoundModal = false;
	};
	void Gather(UWidget* Node, bool bAncestorsAvailable, bool bInModal, UWidget* ModalRoot, bool bModalOpen, FControls& Out)
	{
		if (!IsValid(Node) || Out.Visited.Contains(Node)) return;
		Out.Visited.Add(Node);
		if (Node == ModalRoot)
		{
			Out.bFoundModal = true;
			// Its existing confirmation policy stays configured while closed, but
			// none of its controls join the ordinary lobby cycle.
			if (!bModalOpen) return;
			bInModal = true;
		}
		const bool bAvailable = bAncestorsAvailable && Node->GetIsEnabled() && Node->IsVisible();
		if (Node->IsA<UButton>() || Node->IsA<UEditableTextBox>() || Node->IsA<UComboBoxString>())
		{
			Out.All.Add(Node);
			if (bInModal) Out.ModalControls.Add(Node);
			if (bAvailable && (!ModalRoot || !bModalOpen || bInModal) && Node->SupportsKeyboardFocus()) Out.Available.Add(Node);
		}
		// Walk hidden/disabled branches too, so their previous links are cleared.
		// Carry ancestor state across nested UserWidget/WidgetTree boundaries.
		if (auto* Nested = Cast<UUserWidget>(Node))
		{
			if (Nested->WidgetTree) Gather(Nested->WidgetTree->RootWidget, bAvailable, bInModal, ModalRoot, bModalOpen, Out);
		}
		if (auto* Panel = Cast<UPanelWidget>(Node))
			for (UWidget* Child : Panel->GetAllChildren()) Gather(Child, bAvailable, bInModal, ModalRoot, bModalOpen, Out);
	}
	const FWidgetNavigationData* Data(const UWidget& Widget, EUINavigation Direction)
	{
		if (!Widget.Navigation) return nullptr;
		switch (Direction)
		{
		case EUINavigation::Up: return &Widget.Navigation->Up;
		case EUINavigation::Down: return &Widget.Navigation->Down;
		case EUINavigation::Next: return &Widget.Navigation->Next;
		case EUINavigation::Previous: return &Widget.Navigation->Previous;
		case EUINavigation::Left: return &Widget.Navigation->Left;
		case EUINavigation::Right: return &Widget.Navigation->Right;
		default: return nullptr;
		}
	}
	void Link(UWidget& Widget, EUINavigation Direction, UWidget* Target)
	{
		const auto* Current = Data(Widget, Direction);
		const auto Rule = Target ? EUINavigationRule::Explicit : EUINavigationRule::Stop;
		if (Current && Current->Rule == Rule && Current->Widget.Get() == Target
			&& Current->WidgetToFocus.IsNone() && !Current->CustomDelegate.IsBound()) return;
		if (Target) Widget.SetNavigationRuleExplicit(Direction, Target);
		else Widget.SetNavigationRuleBase(Direction, EUINavigationRule::Stop);
	}
	bool FindSlateAncestors(const TSharedRef<SWidget>& Node, const SWidget* Focused,
		TSet<const SWidget*>& Seen, TSet<const SWidget*>& Ancestors)
	{
		const SWidget* Address = &Node.Get();
		if (Seen.Contains(Address)) return false;
		Seen.Add(Address);
		if (Address == Focused) { Ancestors.Add(Address); return true; }
		if (FChildren* Children = Node->GetChildren())
			for (int32 Index = 0; Index < Children->Num(); ++Index)
				if (FindSlateAncestors(Children->GetChildAt(Index), Focused, Seen, Ancestors))
				{ Ancestors.Add(Address); return true; }
		return false;
	}
}

static void ConfigureControls(UUserWidget& Screen, UWidget* ModalRoot, bool bModalOpen, bool bButtonRows)
{
	if (!Screen.WidgetTree || !Screen.WidgetTree->RootWidget) return;
	FControls Controls;
	Gather(Screen.WidgetTree->RootWidget, Screen.GetIsEnabled() && Screen.IsVisible(), false, ModalRoot, bModalOpen, Controls);
	// An unrelated dialog must not rewrite another screen's navigation.
	if (ModalRoot && !Controls.bFoundModal) return;
	for (UWidget* Control : Controls.All)
	{
		if (auto* Combo = Cast<UComboBoxString>(Control); Combo && !Combo->IsEnableGamepadNavigationMode())
			Combo->SetEnableGamepadNavigationMode(true);
		const int32 Index = Controls.Available.IndexOfByKey(Control);
		const int32 Count = Controls.Available.Num();
		UWidget* Previous = Index != INDEX_NONE && Count > 1 ? Controls.Available[(Index + Count - 1) % Count] : nullptr;
		UWidget* Next = Index != INDEX_NONE && Count > 1 ? Controls.Available[(Index + 1) % Count] : nullptr;
		Link(*Control, EUINavigation::Up, Previous);
		Link(*Control, EUINavigation::Previous, Previous);
		Link(*Control, EUINavigation::Down, Next);
		Link(*Control, EUINavigation::Next, Next);
		// The lobby keeps ordinary spatial/caret navigation. Result button rows
		// and explicit confirmations opt into horizontal containment as well.
		if ((bButtonRows && Control->IsA<UButton>())
			|| (ModalRoot && bModalOpen && Controls.ModalControls.Contains(Control)))
		{
			Link(*Control, EUINavigation::Left, Previous);
			Link(*Control, EUINavigation::Right, Next);
		}
	}
}

void RPGOnlineLobbyNavigation::Configure(UUserWidget& Screen, UWidget* ModalRoot, bool bModalOpen)
{
	ConfigureControls(Screen, ModalRoot, bModalOpen, false);
}

void RPGOnlineLobbyNavigation::ConfigureButtonRows(UUserWidget& Screen, UWidget* ModalRoot, bool bModalOpen)
{
	ConfigureControls(Screen, ModalRoot, bModalOpen, true);
}

UWidget* RPGOnlineLobbyNavigation::FindFocusRecoveryTarget(UUserWidget& Screen,
	const TSharedPtr<SWidget>& FocusedWidget, UWidget* ModalRoot)
{
	const auto RootSlate = Screen.GetCachedWidget();
	if (!RootSlate || !FocusedWidget || !Screen.WidgetTree || !Screen.GetIsEnabled() || !Screen.IsVisible()) return nullptr;
	TSet<const SWidget*> Seen, Ancestors;
	// Walk cached children, not just an arranged/visible path: a newly disabled
	// or hidden control can still own Slate focus. Detached/foreign trees cannot.
	if (!FindSlateAncestors(RootSlate.ToSharedRef(), FocusedWidget.Get(), Seen, Ancestors)) return nullptr;
	FControls Controls;
	Gather(Screen.WidgetTree->RootWidget, true, false, ModalRoot, true, Controls);
	if (ModalRoot && !Controls.bFoundModal) return nullptr;
	UWidget* Current = nullptr;
	for (UWidget* Control : Controls.All)
	{
		if (const auto* Combo = Cast<UComboBoxString>(Control); Combo && Combo->IsOpen()) return nullptr;
		if (const auto Cached = Control->GetCachedWidget(); Cached && Ancestors.Contains(Cached.Get())) Current = Control;
	}
	if (!Current || Controls.Available.Contains(Current)) return nullptr;
	UWidget* Preferred = Screen.GetDesiredFocusWidget();
	if (Controls.Available.Contains(Preferred)) return Preferred;
	// While every action is busy, root focus is inert. The existing safe default
	// policy can resume on a later refresh, without remembering/stealing focus.
	return Screen.SupportsKeyboardFocus() ? &Screen : nullptr;
}

void URPGOnlineLobbyWidget::RefreshInputNavigation()
{
	// These fields are created only by this class's native layout. A custom
	// Blueprint root retains its authored navigation and input implementation.
	if (!WidgetTree || !LoginContent || !CharacterContent || !LobbyContent
		|| (bAbandonmentConfirmationOpen && !AbandonmentConfirmation)) return;
	RPGOnlineLobbyNavigation::Configure(*this, AbandonmentConfirmation, bAbandonmentConfirmationOpen);
	auto* PC = GetOwningPlayer();
	auto* LocalPlayer = PC ? PC->GetLocalPlayer() : nullptr;
	if (!PC || !PC->IsLocalController() || !LocalPlayer || !IsInViewport() || !FSlateApplication::IsInitialized()) return;
	auto& Application = FSlateApplication::Get();
	if (!Application.IsActive()) return;
	const auto User = LocalPlayer->GetSlateUser();
	if (!User) return;
	const auto Focused = Application.GetUserFocusedWidget(User->GetUserIndex());
	if (!Focused) return;
	const auto Window = Application.FindWidgetWindow(Focused.ToSharedRef());
	if (!Window || Window != Application.GetActiveTopLevelWindow()) return;
	UWidget* Target = RPGOnlineLobbyNavigation::FindFocusRecoveryTarget(*this, Focused,
		bAbandonmentConfirmationOpen ? AbandonmentConfirmation.Get() : nullptr);
	if (Target && Application.GetUserFocusedWidget(User->GetUserIndex()) == Focused)
		Target->SetUserFocus(PC);
}
