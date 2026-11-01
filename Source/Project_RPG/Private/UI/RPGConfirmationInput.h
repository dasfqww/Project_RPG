#pragma once

#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"

// Shared by the two native opt-out confirmations. No mutation or input-mode ownership.
namespace RPGConfirmationInput
{
	inline void ConfigureNavigation(UButton* Confirm, UButton* Cancel)
	{
		if (!Confirm || !Cancel) return;
		for (const auto Direction : {EUINavigation::Up, EUINavigation::Down, EUINavigation::Left,
			EUINavigation::Right, EUINavigation::Next, EUINavigation::Previous})
		{
			Confirm->SetNavigationRuleExplicit(Direction, Cancel);
			Cancel->SetNavigationRuleExplicit(Direction, Confirm);
		}
	}

	inline bool IsBack(const FKeyEvent& Event)
	{
		// Respect the owning Slate user's platform-specific accept/back mapping.
		return FSlateApplication::IsInitialized()
			&& FSlateApplication::Get().GetNavigationActionFromKey(Event) == EUINavigationAction::Back;
	}

	inline void Focus(UUserWidget& Screen, UButton* Button)
	{
		auto* Controller = Screen.GetOwningPlayer();
		if (Screen.IsInViewport() && Controller && Controller->IsLocalController()
			&& Button && Button->IsVisible() && Button->GetIsEnabled())
			Button->SetUserFocus(Controller);
	}
}
