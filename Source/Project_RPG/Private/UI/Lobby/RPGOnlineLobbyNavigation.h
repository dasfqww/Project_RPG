#pragma once

#include "Templates/SharedPointer.h"

class UUserWidget;
class UWidget;
class SWidget;

// Shared by the native lobby/result controls. Configure changes navigation metadata; the recovery query
// is read-only. The caller owns guarded focus changes, never input/command state.
namespace RPGOnlineLobbyNavigation
{
	void Configure(UUserWidget& Screen, UWidget* ModalRoot = nullptr, bool bModalOpen = true);
	/** Horizontal result rows also cycle left/right for buttons, never text/combo caret input. */
	void ConfigureButtonRows(UUserWidget& Screen, UWidget* ModalRoot = nullptr, bool bModalOpen = true);
	/** Null for healthy, missing or foreign focus; never guesses a destructive action. */
	UWidget* FindFocusRecoveryTarget(UUserWidget& Screen, const TSharedPtr<SWidget>& FocusedWidget,
		UWidget* ModalRoot = nullptr);
}
