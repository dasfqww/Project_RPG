#pragma once

#include "Components/Button.h"
#include "Controller/RPGPlayerController.h"
#include "UObject/UnrealType.h"

namespace RPGBossBattleUI
{

// The authored RetryButton is a UserWidget wrapper with an OnButtonClicked
// dispatcher, not a UButton. Keep its internal animation/click forwarding intact.
inline FMulticastDelegateProperty* FindRetryClick(UWidget* RetryButton)
{
	if (!IsValid(RetryButton)) { return nullptr; }
	const FName EventName = RetryButton->IsA<UButton>()
		? TEXT("OnClicked") : TEXT("OnButtonClicked");
	FMulticastDelegateProperty* Event = FindFProperty<FMulticastDelegateProperty>(
		RetryButton->GetClass(), EventName);
	return Event && Event->ArrayDim == 1 && Event->SignatureFunction &&
		Event->SignatureFunction->NumParms == 0 ? Event : nullptr;
}

inline bool BindResultButton(UWidget* Button, ARPGPlayerController* Controller, FName FunctionName)
{
	FMulticastDelegateProperty* Event = FindRetryClick(Button);
	if (!Event || !IsValid(Controller)) { return false; }
	const UFunction* Function = Controller->FindFunction(FunctionName);
	if (!Function || Function->NumParms != 0) { return false; }
	FScriptDelegate Handler;
	Handler.BindUFunction(Controller, FunctionName);
	if (!Handler.IsBound()) { return false; }
	// Replace legacy OpenLevel on this instance only; never mutate the asset/CDO.
	Event->ClearDelegate(Button);
	Event->AddDelegate(Handler, Button);
	return true;
}

inline bool BindRetryButton(UWidget* Button, ARPGPlayerController* Controller)
{
	return BindResultButton(Button, Controller,
		GET_FUNCTION_NAME_CHECKED(ARPGPlayerController, RequestBossBattleRestart));
}

inline bool BindReturnButton(UWidget* Button, ARPGPlayerController* Controller)
{
	return BindResultButton(Button, Controller,
		GET_FUNCTION_NAME_CHECKED(ARPGPlayerController, RequestDungeonReturnFromResult));
}

}
