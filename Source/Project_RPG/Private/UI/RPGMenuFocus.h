#pragma once

#include "Blueprint/UserWidget.h"
#include "Components/PanelWidget.h"
#include "GameFramework/PlayerController.h"
#include <initializer_list>

namespace RPGMenuFocus
{
	// Candidates are explicitly ordered by each screen; destructive actions are
	// not implicit defaults. DesiredFocus also supports existing BP root-focus calls.
	inline void Update(UUserWidget& Screen, std::initializer_list<UWidget*> Candidates)
	{
		UWidget* Target = nullptr;
		if (Screen.GetIsEnabled() && Screen.IsVisible())
			for (auto* Candidate : Candidates)
			{
				if (!Candidate || !Candidate->GetIsEnabled() || !Candidate->IsVisible()
					|| !Candidate->SupportsKeyboardFocus()) continue;
				bool bAncestorsAvailable = true;
				for (auto* Parent = Candidate->GetParent(); Parent; Parent = Parent->GetParent())
					bAncestorsAvailable &= Parent->GetIsEnabled() && Parent->IsVisible();
				if (bAncestorsAvailable && Screen.SetDesiredFocusWidget(Candidate))
				{ Target = Candidate; break; } // engine checks membership in this screen's tree
			}
		if (!Target) Screen.SetDesiredFocusWidget(NAME_None);

		// Late data may enable the first action after root focus was assigned.
		// Do not steal focus from a child, text editor, another window or gameplay.
		auto* PC = Screen.GetOwningPlayer();
		if (Target && PC && PC->IsLocalController() && Screen.IsInViewport() && Screen.HasUserFocus(PC))
			Target->SetUserFocus(PC);
	}
}
