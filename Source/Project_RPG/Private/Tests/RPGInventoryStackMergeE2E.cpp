#include "Tests/RPGInventoryStackMergeE2E.h"

#if !UE_BUILD_SHIPPING
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Component/RPGInventoryProjectionComponent.h"
#include "Component/UI/QuickSlotComponent.h"
#include "Components/Border.h"
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
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Input/HittestGrid.h"
#include "InputCoreTypes.h"
#include "Item/Definition/RPGItemDefinitionCatalog.h"
#include "Manager/DataManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UI/Inventory/Projection/RPGInventoryProjectionItemSlotWidget.h"
#include "UI/Inventory/Projection/RPGInventoryProjectionWidget.h"
#include "UI/MVVM/RPGInventoryProjectionViewModel.h"
#include "Widgets/SWindow.h"

bool FRPGInventoryStackMergeE2E::Drag(APlayerController& Controller, const int32 SourceSlot,
	const int32 DestinationSlot, const bool bExpectAccepted)
{
	auto Fail = [this](const TCHAR* Reason) { Error = Reason; return false; };
	auto* Screen = Bag.Get();
	auto* Grid = Screen ? Cast<UUniformGridPanel>(Screen->GetWidgetFromName(TEXT("InventoryGrid"))) : nullptr;
	auto* Source = Grid ? Cast<URPGInventoryProjectionItemSlotWidget>(Grid->GetChildAt(SourceSlot)) : nullptr;
	auto* Destination = Grid ? Cast<URPGInventoryProjectionItemSlotWidget>(Grid->GetChildAt(DestinationSlot)) : nullptr;
	auto* Viewport = Controller.GetGameInstance()->GetGameViewportClient();
	const auto Window = Viewport ? Viewport->GetWindow() : nullptr;
	const auto User = Controller.GetLocalPlayer() ? Controller.GetLocalPlayer()->GetSlateUser() : nullptr;
	if (!Source || !Destination || !Window || !User || !FSlateApplication::IsInitialized())
		return Fail(TEXT("StackMergeInputUnavailable"));
	auto& Slate = FSlateApplication::Get();
	if (Slate.IsTicking() || User->IsDragDropping()) return Fail(TEXT("StackMergeInputReentrant"));
	Slate.Tick(ESlateTickType::Widgets);
	Slate.Tick(ESlateTickType::Widgets);
	const auto SourceSlate = Source->GetCachedWidget();
	const auto DestinationSlate = Destination->GetCachedWidget();
	if (!SourceSlate || !DestinationSlate || SourceSlate->GetCachedGeometry().GetLocalSize().IsNearlyZero() ||
		DestinationSlate->GetCachedGeometry().GetLocalSize().IsNearlyZero()) return Fail(TEXT("StackMergeGeometryMissing"));
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
		!DestinationPath.ContainsWidget(DestinationSlate.Get())) return Fail(TEXT("StackMergeCellOccluded"));
	const TSet<FKey> Released, Pressed{EKeys::LeftMouseButton};
	const auto Event = [&User](FVector2D Position, FVector2D Previous, const TSet<FKey>& Keys, FKey Effect = FKey())
	{
		return FPointerEvent(User->GetUserIndex(), FSlateApplication::CursorPointerIndex,
			Position, Previous, Keys, Effect, 0.f, FModifierKeysState());
	};
	Slate.RoutePointerMoveEvent(SourcePath, Event(Start, Start, Released), false);
	if (!Slate.RoutePointerDownEvent(SourcePath, Event(Start, Start, Pressed, EKeys::LeftMouseButton)).IsEventHandled())
		return Fail(TEXT("StackMergeDownUnhandled"));
	const FVector2D Trigger = Start + FVector2D(24.f, 0.f);
	Slate.RoutePointerMoveEvent(PathAt(Trigger), Event(Trigger, Start, Pressed), false);
	if (!User->IsDragDropping())
	{
		Slate.RoutePointerUpEvent(SourcePath, Event(Start, Trigger, Released, EKeys::LeftMouseButton));
		return Fail(TEXT("StackMergeDragNotDetected"));
	}
	Slate.RoutePointerMoveEvent(DestinationPath, Event(End, Trigger, Pressed), false);
	const bool bAccepted = Slate.RoutePointerUpEvent(DestinationPath,
		Event(End, End, Released, EKeys::LeftMouseButton)).IsEventHandled();
	if (User->IsDragDropping() || bAccepted != bExpectAccepted) return Fail(TEXT("StackMergeDropResultMismatch"));
	UE_LOG(LogTemp, Display, TEXT("RPG_STACK_MERGE_UI DRAG SourceSlot=%d DestinationSlot=%d Accepted=%d HitTest=1 Routed=1 Physical=0"),
		SourceSlot, DestinationSlot, bAccepted ? 1 : 0);
	return true;
}

int32 FRPGInventoryStackMergeE2E::Tick(APlayerController& Controller)
{
	auto Fail = [this](const TCHAR* Reason) { Error = Reason; return -1; };
	auto* PC = Cast<ARPGPlayerController>(&Controller);
	auto* Projection = PC ? PC->FindComponentByClass<URPGInventoryProjectionComponent>() : nullptr;
	auto* Quick = PC && PC->GetPawn() ? PC->GetPawn()->FindComponentByClass<UQuickSlotComponent>() : nullptr;
	if (!PC || PC->HasAuthority() || !PC->IsLocalController() || !Projection || !Quick)
		return Fail(TEXT("StackMergeMustUseOwningClient"));
	if (Phase == 0)
	{
		FString SourceText, DestinationText, ExtraText;
		if (!FParse::Value(FCommandLine::Get(), TEXT("RPGStackMergeSource="), SourceText) ||
			!FParse::Value(FCommandLine::Get(), TEXT("RPGStackMergeDestination="), DestinationText) ||
			!FParse::Value(FCommandLine::Get(), TEXT("RPGStackMergeExtra="), ExtraText) ||
			!FParse::Value(FCommandLine::Get(), TEXT("RPGStackMergeContinueFile="), ContinueFile) ||
			!FGuid::Parse(SourceText, SourceId) || !FGuid::Parse(DestinationText, DestinationId) ||
			!FGuid::Parse(ExtraText, ExtraId) || SourceId == DestinationId || SourceId == ExtraId || DestinationId == ExtraId)
			return Fail(TEXT("StackMergeInvalidFixtureArguments"));
		Deadline = FPlatformTime::Seconds() + 180.0;
		Quick->SetItemSlotById(0, SourceId); Quick->SetItemSlotById(1, DestinationId); Quick->SetItemSlotById(2, ExtraId);
		PC->ShowInventoryProjection(); // normal authored WBP/controller path, no test widget creation
		Phase = 1;
		return 0;
	}
	if (FPlatformTime::Seconds() >= Deadline) return Fail(TEXT("StackMergeUITimeout"));
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
	FRPGInventoryProjectionEntry Source, Destination, Extra;
	const bool bSource = Projection->FindProjectedItem(SourceId, Source);
	if (!Projection->FindProjectedItem(DestinationId, Destination) || !Projection->FindProjectedItem(ExtraId, Extra))
		return Fail(TEXT("StackMergePersistentItemMissing"));
	const auto Matches = [](const FRPGInventoryProjectionEntry& Entry, int32 Quantity, int64 Revision, int32 Slot)
	{
		return Entry.GetQuantity() == Quantity && Entry.GetRevision() == Revision && Entry.GetSlotIndex() == Slot;
	};
	const auto QuickMatches = [Quick](int32 Slot, const FGuid& ItemId)
	{
		const auto* Content = Quick->GetItemSlotContent(Slot);
		return Content && Content->ItemId == ItemId;
	};
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
	if (Phase == 1)
	{
		if (!Screen->GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint))
			return Fail(TEXT("StackMergeRequiresAuthoredWBP"));
		if (!bSource || !Matches(Source, 7, 1, 0) || !Matches(Destination, 8, 1, 1) || !Matches(Extra, 3, 1, 2))
			return Fail(TEXT("StackMergeInitialProjectionMismatch"));
		auto* Data = PC->GetGameInstance()->GetSubsystem<UDataManager>();
		FRPGItemDefinitionSnapshot Definition;
		if (!Data || !Data->GetItemDefinitionCatalog().TryFindDefinition(Source.GetDefinitionId(), Definition) ||
			Definition.MaxStackSize != 10 || Definition.DefinitionVersion != 1)
			return Fail(TEXT("StackMergeAuthoredStackLimitMismatch"));
		if (!QuickMatches(0, SourceId) || !QuickMatches(1, DestinationId) || !QuickMatches(2, ExtraId)) return 0;
		if (!CheckView(0, SourceId, 7) || !CheckView(1, DestinationId, 8) || !CheckView(2, ExtraId, 3)) return 0;
		if (!Drag(*PC, 0, 1, true)) return -1;
		FRPGInventoryProjectionEntry Unchanged;
		if (!Projection->FindProjectedItem(SourceId, Unchanged) || Unchanged.GetQuantity() != 7 || Unchanged.GetRevision() != 1)
			return Fail(TEXT("StackMergeOptimisticMutation"));
		Phase = 2;
	}
	else if (Phase == 2)
	{
		if (!Model->bHasLastCommandResult) return 0;
		const auto Result = Model->LastCommandResult;
		if (Result.Operation != TEXT("Item.StackTransfer") || Result.Result != ERPGItemCommandResultCode::Succeeded)
			return Fail(TEXT("StackMergePartialCommandRejected"));
		if (!bSource || !Matches(Source, 5, 2, 0) || !Matches(Destination, 10, 2, 1) || !Matches(Extra, 3, 1, 2)) return 0;
		if (!CheckView(0, SourceId, 5) || !CheckView(1, DestinationId, 10) || !CheckView(2, ExtraId, 3)) return 0;
		if (!QuickMatches(0, SourceId) || !QuickMatches(1, DestinationId) || !QuickMatches(2, ExtraId))
			return Fail(TEXT("StackMergePartialClearedShortcut"));
		PartialRequest = Result.RequestId;
		UE_LOG(LogTemp, Display, TEXT("RPG_STACK_MERGE_UI PARTIAL Request=%s Source=5 Destination=10 Extra=3 ViewModel=1 Cells=1 QuickSlotsRetained=1"),
			*PartialRequest.ToString(EGuidFormats::DigitsWithHyphensLower));
		Phase = 3;
	}
	else if (Phase == 3 && IFileManager::Get().FileExists(*ContinueFile))
	{
		if (!Drag(*PC, 0, 2, true)) return -1;
		Phase = 4;
	}
	else if (Phase == 4)
	{
		const auto Result = Model->LastCommandResult;
		if (Result.RequestId == PartialRequest) return 0;
		if (Result.Operation != TEXT("Item.StackTransfer") || Result.Result != ERPGItemCommandResultCode::Succeeded)
			return Fail(TEXT("StackMergeFullCommandRejected"));
		if (bSource || !Matches(Destination, 10, 2, 1) || !Matches(Extra, 8, 2, 2)) return 0;
		if (!QuickMatches(0, FGuid()) || !QuickMatches(1, DestinationId) || !QuickMatches(2, ExtraId)) return 0;
		if (!CheckView(0, FGuid(), 0) || !CheckView(1, DestinationId, 10) || !CheckView(2, ExtraId, 8)) return 0;
		FullRequest = Result.RequestId;
		UE_LOG(LogTemp, Display, TEXT("RPG_STACK_MERGE_UI FULL Request=%s SourceRemoved=1 Destination=10 Extra=8 ViewModel=1 Cells=1 SourceShortcutCleared=1 OtherShortcutsRetained=1"),
			*FullRequest.ToString(EGuidFormats::DigitsWithHyphensLower));
		if (!Drag(*PC, 2, 1, false)) return -1; // a full target must not silently swap
		RejectionObservedAt = FPlatformTime::Seconds();
		Phase = 5;
	}
	else if (Phase == 5 && FPlatformTime::Seconds() - RejectionObservedAt >= 1.0)
	{
		if (Model->LastCommandResult.RequestId != FullRequest || bSource || !Matches(Destination, 10, 2, 1) || !Matches(Extra, 8, 2, 2))
			return Fail(TEXT("StackMergeRejectedDropMutatedState"));
		UE_LOG(LogTemp, Display, TEXT("RPG_STACK_MERGE_UI PASS Authenticated=1 Iris=1 AuthoredWBP=1 Partial=1 Full=1 FullTargetRejected=1 Commands=2 Physical=0"));
		return 1;
	}
	return 0;
}
#else
int32 FRPGInventoryStackMergeE2E::Tick(APlayerController&) { return -1; }
bool FRPGInventoryStackMergeE2E::Drag(APlayerController&, int32, int32, bool) { return false; }
#endif
