#include "Tests/RPGInventoryStackSplitE2E.h"
#include "Tests/RPGInventoryConcurrencyE2E.h"
#include "Tests/RPGInventoryRecoveryE2E.h"

#if !UE_BUILD_SHIPPING
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Component/RPGInventoryProjectionComponent.h"
#include "Component/UI/QuickSlotComponent.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"
#include "Components/UniformGridPanel.h"
#include "Controller/RPGPlayerController.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "GameFramework/Pawn.h"
#include "HAL/PlatformTime.h"
#include "HAL/FileManager.h"
#include "Input/HittestGrid.h"
#include "InputCoreTypes.h"
#include "Item/Transaction/RPGItemActionCommandPlanner.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UI/Inventory/Projection/RPGInventoryProjectionItemSlotWidget.h"
#include "UI/Inventory/Projection/RPGInventoryProjectionWidget.h"
#include "UI/MVVM/RPGInventoryProjectionViewModel.h"
#include "Widgets/SWindow.h"

bool FRPGInventoryStackSplitE2E::Drag(APlayerController& Controller,
	const int32 DestinationSlot, const bool bExpectAccepted, const int32 SourceSlot)
{
	const auto Fail = [this](const TCHAR* Reason) { Error = Reason; return false; };
	auto* Screen = Bag.Get();
	auto* Grid = Screen ? Cast<UUniformGridPanel>(Screen->GetWidgetFromName(TEXT("InventoryGrid"))) : nullptr;
	auto* Source = Grid ? Cast<URPGInventoryProjectionItemSlotWidget>(Grid->GetChildAt(SourceSlot)) : nullptr;
	auto* Destination = Grid ? Cast<URPGInventoryProjectionItemSlotWidget>(Grid->GetChildAt(DestinationSlot)) : nullptr;
	auto* Viewport = Controller.GetGameInstance()->GetGameViewportClient();
	const auto Window = Viewport ? Viewport->GetWindow() : nullptr;
	const auto User = Controller.GetLocalPlayer() ? Controller.GetLocalPlayer()->GetSlateUser() : nullptr;
	if (!Source || !Destination || !Window || !User || !FSlateApplication::IsInitialized())
		return Fail(TEXT("StackSplitInputUnavailable"));
	auto& Slate = FSlateApplication::Get();
	if (Slate.IsTicking() || User->IsDragDropping()) return Fail(TEXT("StackSplitInputReentrant"));
	Slate.Tick(ESlateTickType::Widgets);
	Slate.Tick(ESlateTickType::Widgets);
	const auto SourceSlate = Source->GetCachedWidget();
	const auto DestinationSlate = Destination->GetCachedWidget();
	if (!SourceSlate || !DestinationSlate || SourceSlate->GetCachedGeometry().GetLocalSize().IsNearlyZero() ||
		DestinationSlate->GetCachedGeometry().GetLocalSize().IsNearlyZero()) return Fail(TEXT("StackSplitGeometryMissing"));
	const auto Center = [](const TSharedPtr<SWidget>& Widget)
	{
		return Widget->GetCachedGeometry().LocalToAbsolute(Widget->GetCachedGeometry().GetLocalSize() * .5f);
	};
	const FVector2D Start = Center(SourceSlate), End = Center(DestinationSlate);
	const auto PathAt = [&Window, &User](FVector2D Point)
	{
		return FWidgetPath(Window->GetHittestGrid().GetBubblePath(Point, 0.f, false, User->GetUserIndex()));
	};
	const FWidgetPath SourcePath = PathAt(Start), DestinationPath = PathAt(End);
	if (!SourcePath.IsValid() || !DestinationPath.IsValid() || !SourcePath.ContainsWidget(SourceSlate.Get()) ||
		!DestinationPath.ContainsWidget(DestinationSlate.Get())) return Fail(TEXT("StackSplitCellOccluded"));
	const TSet<FKey> Released, Pressed{EKeys::LeftMouseButton};
	const auto Event = [&User, this](FVector2D Position, FVector2D Previous, const TSet<FKey>& Keys, FKey Effect = FKey())
	{
		return FPointerEvent(User->GetUserIndex(), FSlateApplication::CursorPointerIndex,
			Position, Previous, Keys, Effect, 0.f, FModifierKeysState(!bQuantityMode && !bMoveSwapMode, false, bQuantityMode, false, false, false, false, false, false));
	};
	Slate.RoutePointerMoveEvent(SourcePath, Event(Start, Start, Released), false);
	if (!Slate.RoutePointerDownEvent(SourcePath, Event(Start, Start, Pressed, EKeys::LeftMouseButton)).IsEventHandled())
		return Fail(TEXT("StackSplitDownUnhandled"));
	const FVector2D Trigger = Start + FVector2D(24.f, 0.f);
	Slate.RoutePointerMoveEvent(PathAt(Trigger), Event(Trigger, Start, Pressed), false);
	if (!User->IsDragDropping())
	{
		Slate.RoutePointerUpEvent(SourcePath, Event(Start, Trigger, Released, EKeys::LeftMouseButton));
		return Fail(TEXT("StackSplitDragNotDetected"));
	}
	Slate.RoutePointerMoveEvent(DestinationPath, Event(End, Trigger, Pressed), false);
	const bool bAccepted = Slate.RoutePointerUpEvent(DestinationPath,
		Event(End, End, Released, EKeys::LeftMouseButton)).IsEventHandled();
	if (User->IsDragDropping() || bAccepted != bExpectAccepted) return Fail(TEXT("StackSplitDropResultMismatch"));
	if (bMoveSwapMode)
	{
		UE_LOG(LogTemp, Display, TEXT("RPG_MOVE_SWAP_UI DRAG SourceSlot=%d DestinationSlot=%d Accepted=%d HitTest=1 Routed=1 Physical=0"),
			SourceSlot, DestinationSlot, bAccepted ? 1 : 0);
	}
	else
	{
		UE_LOG(LogTemp, Display, TEXT("RPG_STACK_SPLIT_UI DRAG SourceSlot=%d DestinationSlot=%d Shift=%d Ctrl=%d Accepted=%d HitTest=1 Routed=1 Physical=0"),
			SourceSlot, DestinationSlot, bQuantityMode ? 0 : 1, bQuantityMode ? 1 : 0, bAccepted ? 1 : 0);
	}
	return true;
}

bool FRPGInventoryStackSplitE2E::Click(APlayerController& Controller, UWidget* Widget, const bool bRightMouseButton)
{
	const auto Fail = [this](const TCHAR* Reason) { Error = Reason; return false; };
	auto* Viewport = Controller.GetGameInstance()->GetGameViewportClient();
	const auto Window = Viewport ? Viewport->GetWindow() : nullptr;
	const auto User = Controller.GetLocalPlayer() ? Controller.GetLocalPlayer()->GetSlateUser() : nullptr;
	if (!Widget || !Widget->GetIsEnabled() || !Window || !User || !FSlateApplication::IsInitialized())
		return Fail(TEXT("SplitQuantityClickUnavailable"));
	auto& Slate = FSlateApplication::Get();
	if (Slate.IsTicking() || User->IsDragDropping()) return Fail(TEXT("SplitQuantityClickReentrant"));
	Slate.Tick(ESlateTickType::Widgets); Slate.Tick(ESlateTickType::Widgets);
	const auto Target = Widget->GetCachedWidget();
	if (!Target || Target->GetCachedGeometry().GetLocalSize().IsNearlyZero()) return Fail(TEXT("SplitQuantityControlGeometryMissing"));
	const auto Position = Target->GetCachedGeometry().LocalToAbsolute(Target->GetCachedGeometry().GetLocalSize() * .5f);
	const FWidgetPath Path(Window->GetHittestGrid().GetBubblePath(Position, 0.f, false, User->GetUserIndex()));
	if (!Path.IsValid() || !Path.ContainsWidget(Target.Get())) return Fail(TEXT("SplitQuantityControlOccluded"));
	const auto Event = [&User, Position](const TSet<FKey>& Keys, FKey Effect = FKey())
	{
		return FPointerEvent(User->GetUserIndex(), FSlateApplication::CursorPointerIndex,
			Position, Position, Keys, Effect, 0.f, FModifierKeysState());
	};
	const FKey Button = bRightMouseButton ? EKeys::RightMouseButton : EKeys::LeftMouseButton;
	const TSet<FKey> Released, Pressed{Button};
	Slate.RoutePointerMoveEvent(Path, Event(Released), false);
	const bool bDown = Slate.RoutePointerDownEvent(Path, Event(Pressed, Button)).IsEventHandled();
	const bool bUp = Slate.RoutePointerUpEvent(Path, Event(Released, Button)).IsEventHandled();
	// Item cells intentionally handle right-button down only; always route the release too.
	if (!bDown || (!bRightMouseButton && !bUp)) return Fail(TEXT("SplitQuantityClickUnhandled"));
	UE_LOG(LogTemp, Display, TEXT("RPG_STACK_QUANTITY_UI CLICK Widget=%s HitTest=1 Routed=1 Physical=0 RightMouse=%d"), *Widget->GetName(), bRightMouseButton ? 1 : 0);
	return true;
}

bool FRPGInventoryStackSplitE2E::TypeQuantity(APlayerController& Controller,
	UEditableTextBox* Input, const FString& Text)
{
	if (!Click(Controller, Input)) return false;
	const auto User = Controller.GetLocalPlayer()->GetSlateUser();
	if (!Input->HasUserFocus(&Controller) && !Input->HasUserFocusedDescendants(&Controller))
	{
		Error = TEXT("SplitQuantityInputNotFocused"); return false;
	}
	auto& Slate = FSlateApplication::Get();
	const FModifierKeysState Ctrl(false, false, true, false, false, false, false, false, false);
	if (!Slate.ProcessKeyDownEvent(FKeyEvent(EKeys::A, Ctrl, User->GetUserIndex(), false, 0, 0)))
	{
		Error = TEXT("SplitQuantitySelectAllUnhandled"); return false;
	}
	Slate.ProcessKeyUpEvent(FKeyEvent(EKeys::A, FModifierKeysState(), User->GetUserIndex(), false, 0, 0));
	for (const TCHAR Character : Text)
	{
		if (!Slate.ProcessKeyCharEvent(FCharacterEvent(Character, FModifierKeysState(), User->GetUserIndex(), false)))
		{
			Error = TEXT("SplitQuantityCharacterUnhandled"); return false;
		}
	}
	if (Input->GetText().ToString() != Text) { Error = TEXT("SplitQuantityTypedTextMismatch"); return false; }
	UE_LOG(LogTemp, Display, TEXT("RPG_STACK_QUANTITY_UI TEXT Characters=%d KeyboardRouted=1 DirectSetText=0 Physical=0"), Text.Len());
	return true;
}

int32 FRPGInventoryStackSplitE2E::Tick(APlayerController& Controller)
{
	if (FParse::Param(FCommandLine::Get(), TEXT("RPGInventoryRecoveryE2E")))
	{
		if (!RecoveryProbe) RecoveryProbe = MakeShared<FRPGInventoryRecoveryE2E>();
		const int32 Status = RecoveryProbe->Tick(Controller);
		if (Status < 0) Error = RecoveryProbe->Error;
		return Status;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("RPGInventoryConcurrencyE2E")))
	{
		if (!ConcurrencyProbe) ConcurrencyProbe = MakeShared<FRPGInventoryConcurrencyE2E>();
		const int32 Status = ConcurrencyProbe->Tick(Controller);
		if (Status < 0) Error = ConcurrencyProbe->Error;
		return Status;
	}
	const auto Fail = [this](const TCHAR* Reason) { Error = Reason; return -1; };
	auto* PC = Cast<ARPGPlayerController>(&Controller);
	auto* Projection = PC ? PC->FindComponentByClass<URPGInventoryProjectionComponent>() : nullptr;
	auto* Quick = PC && PC->GetPawn() ? PC->GetPawn()->FindComponentByClass<UQuickSlotComponent>() : nullptr;
	if (!PC || PC->HasAuthority() || !PC->IsLocalController() || !Projection || !Quick)
		return Fail(TEXT("StackSplitMustUseOwningClient"));
	if (Phase == 0)
	{
		bQuantityMode = FParse::Param(FCommandLine::Get(), TEXT("RPGInventorySplitQuantityE2E"));
		bMoveSwapMode = FParse::Param(FCommandLine::Get(), TEXT("RPGInventoryMoveSwapE2E"));
		if (bMoveSwapMode)
		{
			FString OtherText;
			if (bQuantityMode || !FParse::Value(FCommandLine::Get(), TEXT("RPGInventorySwapOther="), OtherText) ||
				!FGuid::Parse(OtherText, OtherId) || !OtherId.IsValid() ||
				!FParse::Value(FCommandLine::Get(), TEXT("RPGInventoryMoveContinueFile="), MoveContinueFile))
				return Fail(TEXT("MoveSwapInvalidFixtureArguments"));
		}
		if (bQuantityMode && !FParse::Value(FCommandLine::Get(), TEXT("RPGSplitQuantityContinueFile="), QuantityContinueFile))
			return Fail(TEXT("SplitQuantityMissingVerificationGate"));
		FString SourceText;
		if (!FParse::Value(FCommandLine::Get(), TEXT("RPGStackSplitSource="), SourceText) ||
			!FGuid::Parse(SourceText, SourceId) || !SourceId.IsValid()) return Fail(TEXT("StackSplitInvalidFixtureArguments"));
		if (bMoveSwapMode && OtherId == SourceId) return Fail(TEXT("MoveSwapSameFixtureIdentity"));
		Deadline = FPlatformTime::Seconds() + 180.0;
		Quick->SetItemSlotById(0, SourceId);
		if (bMoveSwapMode) Quick->SetItemSlotById(2, SourceId); // two legal references to the same consumable
		PC->ShowInventoryProjection(); // existing controller/authored WBP path
		Phase = 1;
		return 0;
	}
	if (FPlatformTime::Seconds() >= Deadline) return Fail(TEXT("StackSplitUITimeout"));
	if (Projection->GetLoadState() != ERPGInventoryProjectionLoadState::Ready) return 0;
	if (!Bag.IsValid())
	{
		TArray<UUserWidget*> Screens;
		UWidgetBlueprintLibrary::GetAllWidgetsOfClass(PC->GetWorld(), Screens, URPGInventoryProjectionWidget::StaticClass(), false);
		for (auto* Screen : Screens)
			if (Screen->GetOwningPlayer() == PC && Screen->IsInViewport() && Screen->GetWidgetFromName(TEXT("InventoryGrid")))
				Bag = Cast<URPGInventoryProjectionWidget>(Screen);
	}
	auto* Screen = Bag.Get();
	auto* Model = Screen ? Screen->GetInventoryViewModel() : nullptr;
	if (!Model || Model->LoadState != ERPGInventoryProjectionLoadState::Ready) return 0;
	if (bMoveSwapMode) return TickMoveSwap(Controller);
	const auto CheckView = [Screen, Model](int32 Slot, const FGuid& ItemId, int32 Quantity)
	{
		const auto* Grid = Cast<UUniformGridPanel>(Screen->GetWidgetFromName(TEXT("InventoryGrid")));
		const auto* Cell = Grid ? Cast<URPGInventoryProjectionItemSlotWidget>(Grid->GetChildAt(Slot)) : nullptr;
		const auto* Label = Cell ? Cast<UTextBlock>(Cell->GetWidgetFromName(TEXT("ItemFallbackName"))) : nullptr;
		const auto* Count = Cell ? Cast<UTextBlock>(Cell->GetWidgetFromName(TEXT("ItemQuantityText"))) : nullptr;
		if (!Label || !Count) return false;
		if (!ItemId.IsValid()) return Label->GetText().IsEmpty() && Count->GetText().IsEmpty() && !Count->IsVisible();
		const auto* Entry = Model->InventoryItems.FindByPredicate([&ItemId](const auto& Value) { return Value.GetItemId() == ItemId; });
		const FString ExpectedCount = Quantity > 1 ? TEXT("x") + FText::AsNumber(Quantity).ToString() : FString();
		return Entry && Entry->GetSlotIndex() == Slot && Entry->GetQuantity() == Quantity &&
			Label->GetText().EqualTo(Model->GetItemPresentation(ItemId).DisplayName) &&
			Count->GetText().ToString() == ExpectedCount && Count->IsVisible() == (Quantity > 1);
	};
	const auto QuickMatches = [Quick, this]()
	{
		const auto* Original = Quick->GetItemSlotContent(0);
		const auto* Unassigned = Quick->GetItemSlotContent(1);
		return Original && Original->ItemId == SourceId && Unassigned && !Unassigned->ItemId.IsValid();
	};
	FRPGInventoryProjectionEntry Source, Created;
	const int32 SelectedQuantity = bQuantityMode ? 2 : 3;
	const int32 RemainingQuantity = 7 - SelectedQuantity;
	if (Phase == 1)
	{
		if (!Screen->GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint)) return Fail(TEXT("StackSplitRequiresAuthoredWBP"));
		if (!Projection->FindProjectedItem(SourceId, Source) || Source.GetQuantity() != 7 || Source.GetRevision() != 1 ||
			Source.GetSlotIndex() != 0 || Model->InventoryItems.Num() != 1) return Fail(TEXT("StackSplitInitialProjectionMismatch"));
		if (!QuickMatches() || !CheckView(0, SourceId, 7) || !CheckView(3, FGuid(), 0)) return 0;
		if (!Drag(*PC, 3, true)) return -1;
		if (!Projection->FindProjectedItem(SourceId, Source) || Source.GetQuantity() != 7 || Source.GetRevision() != 1 ||
			Model->InventoryItems.Num() != 1) return Fail(TEXT("StackSplitOptimisticMutation"));
		if (bQuantityMode)
		{
			if (!Screen->IsSplitQuantitySelectionOpen() || Model->bHasLastCommandResult)
				return Fail(TEXT("SplitQuantityDropSubmittedBeforeConfirmation"));
			if (!Click(*PC, Screen->GetWidgetFromName(TEXT("CancelSplitQuantityButton")))) return -1;
			if (Screen->IsSplitQuantitySelectionOpen() || Model->bHasLastCommandResult || !CheckView(0, SourceId, 7))
				return Fail(TEXT("SplitQuantityCancelMutatedState"));
			UE_LOG(LogTemp, Display, TEXT("RPG_STACK_QUANTITY_UI CANCELLED Authenticated=1 Iris=1 AuthoredWBP=1 NoCommand=1 Source=7 Physical=0"));
			Phase = 10;
		}
		else Phase = 2;
	}
	else if (Phase == 10)
	{
		if (Model->bHasLastCommandResult || Screen->IsSplitQuantitySelectionOpen() || !CheckView(0, SourceId, 7))
			return Fail(TEXT("SplitQuantityCancelChangedWhileWaiting"));
		if (!IFileManager::Get().FileExists(*QuantityContinueFile)) return 0;
		if (!Drag(*PC, 3, true)) return -1;
		auto* Input = Cast<UEditableTextBox>(Screen->GetWidgetFromName(TEXT("SplitQuantityInput")));
		auto* Confirm = Cast<UButton>(Screen->GetWidgetFromName(TEXT("ConfirmSplitQuantityButton")));
		if (!Screen->IsSplitQuantitySelectionOpen() || !Confirm || !TypeQuantity(*PC, Input, TEXT("7")))
			return Error.IsEmpty() ? Fail(TEXT("SplitQuantityControlsMissing")) : -1;
		if (Screen->CanConfirmSplitQuantitySelection() || Confirm->GetIsEnabled() || Model->bHasLastCommandResult)
			return Fail(TEXT("SplitQuantityFullSourceWasNotRejected"));
		if (!TypeQuantity(*PC, Input, TEXT("2"))) return -1;
		if (!Screen->CanConfirmSplitQuantitySelection() || !Confirm->GetIsEnabled())
			return Fail(TEXT("SplitQuantityValidChoiceDisabled"));
		if (!Click(*PC, Confirm)) return -1;
		if (Screen->IsSplitQuantitySelectionOpen() || !Projection->FindProjectedItem(SourceId, Source) ||
			Source.GetQuantity() != 7 || Source.GetRevision() != 1) return Fail(TEXT("SplitQuantityOptimisticMutation"));
		UE_LOG(LogTemp, Display, TEXT("RPG_STACK_QUANTITY_UI CONFIRMED Selected=2 InvalidFullSourceRejected=1 ClickRouted=1 DirectSubmit=0 Physical=0"));
		Phase = 2;
	}
	else if (Phase == 2)
	{
		if (!Model->bHasLastCommandResult) return 0;
		const auto Result = Model->LastCommandResult;
		if (Result.Operation != TEXT("Item.Split") || Result.Result != ERPGItemCommandResultCode::Succeeded)
			return Fail(TEXT("StackSplitCommandRejected"));
		RequestId = Result.RequestId;
		CreatedId = FRPGItemActionCommandPlanner::MakeSplitItemId(RequestId, SourceId);
		if (!Projection->FindProjectedItem(SourceId, Source) || Source.GetQuantity() != RemainingQuantity || Source.GetRevision() != 2 || Source.GetSlotIndex() != 0 ||
			!Projection->FindProjectedItem(CreatedId, Created) || Created.GetQuantity() != SelectedQuantity || Created.GetRevision() != 1 || Created.GetSlotIndex() != 3) return 0;
		if (Model->InventoryItems.Num() != 2 || !CheckView(0, SourceId, RemainingQuantity) || !CheckView(3, CreatedId, SelectedQuantity) ||
			!Model->CanSplitItem(SourceId, 2, 4, 1) || !Model->CanMoveItemToSlot(CreatedId, 1, 4) || !QuickMatches()) return 0;
		UE_LOG(LogTemp, Display, TEXT("RPG_STACK_SPLIT_UI SPLIT Request=%s Created=%s Source=%d NewStack=%d ViewModel=1 Cells=1 ReservationsReleased=1 OriginalShortcutRetained=1 NewShortcutUnassigned=1"),
			*RequestId.ToString(EGuidFormats::DigitsWithHyphensLower), *CreatedId.ToString(EGuidFormats::DigitsWithHyphensLower), RemainingQuantity, SelectedQuantity);
		if (!Drag(*PC, 3, false)) return -1; // occupied target must not merge/swap instead
		RejectionObservedAt = FPlatformTime::Seconds();
		Phase = 3;
	}
	else if (Phase == 3 && FPlatformTime::Seconds() - RejectionObservedAt >= 1.0)
	{
		if (Model->LastCommandResult.RequestId != RequestId || Model->InventoryItems.Num() != 2 ||
			!Projection->FindProjectedItem(SourceId, Source) || Source.GetQuantity() != RemainingQuantity || Source.GetRevision() != 2 || Source.GetSlotIndex() != 0 ||
			!Projection->FindProjectedItem(CreatedId, Created) || Created.GetQuantity() != SelectedQuantity || Created.GetRevision() != 1 || Created.GetSlotIndex() != 3 ||
			Screen->IsSplitQuantitySelectionOpen() || !QuickMatches() || !CheckView(0, SourceId, RemainingQuantity) || !CheckView(3, CreatedId, SelectedQuantity)) return Fail(TEXT("StackSplitRejectedDropMutatedState"));
		if (bQuantityMode)
		{
			UE_LOG(LogTemp, Display, TEXT("RPG_STACK_QUANTITY_UI PASS Authenticated=1 Iris=1 AuthoredWBP=1 Cancel=1 InvalidQuantityRejected=1 Selected=2 OccupiedTargetRejected=1 Commands=1 Physical=0"));
		}
		else
		{
			UE_LOG(LogTemp, Display, TEXT("RPG_STACK_SPLIT_UI PASS Authenticated=1 Iris=1 AuthoredWBP=1 Split=1 OccupiedTargetRejected=1 Commands=1 Physical=0"));
		}
		return 1;
	}
	return 0;
}

int32 FRPGInventoryStackSplitE2E::TickMoveSwap(APlayerController& Controller)
{
	const auto Fail = [this](const TCHAR* Reason) { Error = Reason; return -1; };
	auto* Projection = Controller.FindComponentByClass<URPGInventoryProjectionComponent>();
	auto* Quick = Controller.GetPawn()->FindComponentByClass<UQuickSlotComponent>();
	auto* Screen = Bag.Get();
	auto* Model = Screen->GetInventoryViewModel();
	FRPGInventoryProjectionEntry Source, Other;
	if (!Projection->FindProjectedItem(SourceId, Source) || !Projection->FindProjectedItem(OtherId, Other))
		return Phase == 1 ? 0 : Fail(TEXT("MoveSwapItemMissing"));
	const auto Matches = [](const FRPGInventoryProjectionEntry& Item, int32 Slot, int64 Revision, int32 Quantity)
	{
		return Item.GetContainerType() == ERPGItemContainerType::Inventory && Item.GetSlotIndex() == Slot &&
			Item.GetRevision() == Revision && Item.GetQuantity() == Quantity;
	};
	const auto Shortcuts = [this, Quick]()
	{
		const auto* First = Quick->GetItemSlotContent(0);
		const auto* Empty = Quick->GetItemSlotContent(1);
		const auto* Duplicate = Quick->GetItemSlotContent(2);
		return First && First->ItemId == SourceId && Duplicate && Duplicate->ItemId == SourceId && Empty && !Empty->ItemId.IsValid();
	};
	const auto Cell = [Screen, Model](int32 Slot, FGuid Id, int32 Quantity)
	{
		const auto* Grid = Cast<UUniformGridPanel>(Screen->GetWidgetFromName(TEXT("InventoryGrid")));
		const auto* Widget = Grid ? Cast<URPGInventoryProjectionItemSlotWidget>(Grid->GetChildAt(Slot)) : nullptr;
		const auto* Label = Widget ? Cast<UTextBlock>(Widget->GetWidgetFromName(TEXT("ItemFallbackName"))) : nullptr;
		const auto* Count = Widget ? Cast<UTextBlock>(Widget->GetWidgetFromName(TEXT("ItemQuantityText"))) : nullptr;
		if (!Label || !Count) return false;
		if (!Id.IsValid()) return Label->GetText().IsEmpty() && Count->GetText().IsEmpty() && !Count->IsVisible();
		const auto* Entry = Model->InventoryItems.FindByPredicate([Id](const auto& Item) { return Item.GetItemId() == Id; });
		const FString ExpectedCount = Quantity > 1 ? TEXT("x") + FText::AsNumber(Quantity).ToString() : FString();
		return Entry && Entry->GetSlotIndex() == Slot && Entry->GetQuantity() == Quantity &&
			Label->GetText().EqualTo(Model->GetItemPresentation(Id).DisplayName) &&
			Count->GetText().ToString() == ExpectedCount && Count->IsVisible() == (Quantity > 1);
	};
	if (Phase == 1 && Model->InventoryItems.Num() < 2) return 0;
	if (Screen->IsSplitQuantitySelectionOpen() || Model->InventoryItems.Num() != 2)
		return Fail(TEXT("MoveSwapUnexpectedSelectionOrCount"));
	if (Phase == 1)
	{
		if (!Screen->GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint) ||
			Source.GetDefinitionId() == Other.GetDefinitionId() || !Matches(Source, 0, 1, 7) || !Matches(Other, 1, 1, 1))
			return Fail(TEXT("MoveSwapInitialProjectionMismatch"));
		if (!Shortcuts() || !Cell(0, SourceId, 7) || !Cell(1, OtherId, 1) || !Cell(3, FGuid(), 0)) return 0;
		if (!Drag(Controller, 3, true, 0)) return -1;
		FRPGInventoryProjectionEntry Unchanged;
		if (!Projection->FindProjectedItem(SourceId, Unchanged) || !Matches(Unchanged, 0, 1, 7))
			return Fail(TEXT("MoveSwapOptimisticMove"));
		Phase = 20;
	}
	else if (Phase == 20)
	{
		if (!Model->bHasLastCommandResult) return 0;
		const auto Result = Model->LastCommandResult;
		if (Result.Operation != TEXT("Item.Move") || Result.Result != ERPGItemCommandResultCode::Succeeded)
			return Fail(TEXT("MoveSwapMoveRejected"));
		if (!Matches(Source, 3, 2, 7) || !Matches(Other, 1, 1, 1) || !Cell(0, FGuid(), 0) ||
			!Cell(3, SourceId, 7) || !Cell(1, OtherId, 1) || !Shortcuts() || !Model->CanMoveItemToSlot(SourceId, 2, 4)) return 0;
		MoveRequestId = Result.RequestId;
		UE_LOG(LogTemp, Display, TEXT("RPG_MOVE_SWAP_UI MOVED Request=%s SourceSlot=3 OtherSlot=1 Source=7 Other=1 Cells=1 ShortcutsRetained=1"),
			*MoveRequestId.ToString(EGuidFormats::DigitsWithHyphensLower));
		Phase = 21;
	}
	else if (Phase == 21)
	{
		if (!Matches(Source, 3, 2, 7) || !Matches(Other, 1, 1, 1) || !Shortcuts()) return Fail(TEXT("MoveSwapChangedBeforeDatabaseGate"));
		if (!IFileManager::Get().FileExists(*MoveContinueFile)) return 0;
		if (!Drag(Controller, 1, true, 3)) return -1;
		FRPGInventoryProjectionEntry A, B;
		if (!Projection->FindProjectedItem(SourceId, A) || !Matches(A, 3, 2, 7) ||
			!Projection->FindProjectedItem(OtherId, B) || !Matches(B, 1, 1, 1)) return Fail(TEXT("MoveSwapOptimisticSwap"));
		Phase = 22;
	}
	else if (Phase == 22)
	{
		const auto Result = Model->LastCommandResult;
		if (Result.RequestId == MoveRequestId) return 0;
		if (Result.Operation != TEXT("Item.Swap") || Result.Result != ERPGItemCommandResultCode::Succeeded)
			return Fail(TEXT("MoveSwapSwapRejectedOrWrongOperation"));
		if (!Matches(Source, 1, 3, 7) || !Matches(Other, 3, 2, 1) || !Cell(0, FGuid(), 0) ||
			!Cell(1, SourceId, 7) || !Cell(3, OtherId, 1) || !Shortcuts() ||
			!Model->CanMoveItemToSlot(SourceId, 3, 4) || !Model->CanMoveItemToSlot(OtherId, 2, 4)) return 0;
		RequestId = Result.RequestId;
		UE_LOG(LogTemp, Display, TEXT("RPG_MOVE_SWAP_UI SWAPPED Request=%s SourceSlot=1 OtherSlot=3 Source=7 Other=1 Cells=1 BothReservationsReleased=1 ShortcutsRetained=1"),
			*RequestId.ToString(EGuidFormats::DigitsWithHyphensLower));
		if (!Drag(Controller, 1, false, 1)) return -1;
		RejectionObservedAt = FPlatformTime::Seconds(); Phase = 23;
	}
	else if (Phase == 23 && FPlatformTime::Seconds() - RejectionObservedAt >= 1.0)
	{
		if (Model->LastCommandResult.RequestId != RequestId || !Matches(Source, 1, 3, 7) || !Matches(Other, 3, 2, 1) ||
			!Shortcuts() || !Cell(1, SourceId, 7) || !Cell(3, OtherId, 1)) return Fail(TEXT("MoveSwapSelfDropMutatedState"));
		UE_LOG(LogTemp, Display, TEXT("RPG_MOVE_SWAP_UI PASS Authenticated=1 Iris=1 AuthoredWBP=1 Move=1 AtomicSwap=1 SelfDropRejected=1 Commands=2 Physical=0"));
		return 1;
	}
	return 0;
}
#else
int32 FRPGInventoryStackSplitE2E::Tick(APlayerController&) { return -1; }
int32 FRPGInventoryStackSplitE2E::TickMoveSwap(APlayerController&) { return -1; }
bool FRPGInventoryStackSplitE2E::Drag(APlayerController&, int32, bool, int32) { return false; }
bool FRPGInventoryStackSplitE2E::Click(APlayerController&, UWidget*, bool) { return false; }
bool FRPGInventoryStackSplitE2E::TypeQuantity(APlayerController&, UEditableTextBox*, const FString&) { return false; }
#endif
