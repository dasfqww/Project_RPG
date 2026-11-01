#pragma once

#include "CoreMinimal.h"

class APlayerController;
class UUserWidget;
class SWidget;

/** Shared local input lifetime, stacking and focus order for player windows. */
class PROJECT_RPG_API FRPGWindowInputState
{
public:
	/** Consumers can release held gameplay inputs without coupling this state to GAS. */
	FSimpleMulticastDelegate OnFirstWindowOpened;
	bool BeginWindow(APlayerController& Controller, UUserWidget& Window, int32 Layer = 20);
	void FocusWindow(UUserWidget& Window);
	void EndWindow(UUserWidget& Window);
	void Reset();
	bool HasOpenWindows() const { return !Windows.IsEmpty(); }
	bool ContainsWindow(const UUserWidget& Window) const;
	UUserWidget* GetTopWindow() const;

private:
	struct FWindowEntry
	{
		TWeakObjectPtr<UUserWidget> Window;
		int32 Layer = 20;
	};
	void RefreshZOrder();
	TWeakObjectPtr<APlayerController> Owner;
	TArray<FWindowEntry> Windows;
	TWeakPtr<SWidget> PreviousFocus;
	bool bPreviousCursorVisible = false;
	bool bInputLocksApplied = false;
};
