#include "Tests/RPGInventoryRecoveryE2E.h"
#if !UE_BUILD_SHIPPING
#include "Tests/RPGInventoryStackSplitE2E.h"
#include "Tests/RPGDungeonEjectionNetworkProbe.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Component/RPGInventoryProjectionComponent.h"
#include "Component/RPGItemCommandComponent.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Component/UI/QuickSlotComponent.h"
#include "Attribute/RPGAttributeSet.h"
#include "Component/Equipment/RPGAuthoritativeEquipmentComponent.h"
#include "Components/Button.h"
#include "Components/Border.h"
#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"
#include "Components/UniformGridPanel.h"
#include "Components/SizeBox.h"
#include "Components/VerticalBox.h"
#include "Controller/RPGPlayerController.h"
#include "Engine/GameInstance.h"
#include "GameFramework/Pawn.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Item/Transaction/RPGItemActionCommandPlanner.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Player/RPGPlayerState.h"
#include "UI/Inventory/Projection/RPGInventoryProjectionItemSlotWidget.h"
#include "UI/Inventory/Projection/RPGInventoryProjectionWidget.h"
#include "UI/Inventory/Projection/RPGRewardMailWidget.h"
#include "UI/MVVM/RPGInventoryProjectionViewModel.h"
#include "UI/RPGQuickSlotWidget.h"

void FRPGInventoryRecoveryE2E::UnbindResult()
{
	if (auto* Model = BoundModel.Get())
		Model->RemoveFieldValueChangedDelegate(URPGInventoryProjectionViewModel::FFieldNotificationClassDescriptor::LastCommandResult, ResultHandle);
	ResultHandle.Reset(); BoundModel.Reset();
}
FRPGInventoryRecoveryE2E::~FRPGInventoryRecoveryE2E() { UnbindResult(); }

int32 FRPGInventoryRecoveryE2E::Tick(APlayerController& Controller)
{
	const auto Fail = [this](const TCHAR* Reason) { Error = Reason; return -1; };
	auto* PC = Cast<ARPGPlayerController>(&Controller);
	auto* Projection = PC ? PC->FindComponentByClass<URPGInventoryProjectionComponent>() : nullptr;
	auto* Commands = PC ? PC->FindComponentByClass<URPGItemCommandComponent>() : nullptr;
	if (!PC || PC->HasAuthority() || !PC->IsLocalController() || !Projection || !Commands)
		return Fail(TEXT("InventoryRecoveryNeedsOwningClient"));
	if (Phase == 0)
	{
		bRetryFailure = FParse::Param(FCommandLine::Get(), TEXT("RPGInventoryRecoveryRetryFailureE2E"));
		bCredentialExpiry = FParse::Param(FCommandLine::Get(), TEXT("RPGInventoryRecoveryCredentialExpiryE2E"));
		bRestored = FParse::Param(FCommandLine::Get(), TEXT("RPGInventoryRecoveryRestoredE2E"));
		bSwap = FParse::Param(FCommandLine::Get(), TEXT("RPGInventoryRecoverySwapE2E"));
		bMerge = FParse::Param(FCommandLine::Get(), TEXT("RPGInventoryRecoveryMergeE2E"));
		bSplit = FParse::Param(FCommandLine::Get(), TEXT("RPGInventoryRecoverySplitE2E"));
		bClaimMail = FParse::Param(FCommandLine::Get(), TEXT("RPGInventoryRecoveryClaimMailE2E"));
		bEquip = FParse::Param(FCommandLine::Get(), TEXT("RPGInventoryRecoveryEquipE2E"));
		bUnequip = FParse::Param(FCommandLine::Get(), TEXT("RPGInventoryRecoveryUnequipE2E"));
		bConsume = FParse::Param(FCommandLine::Get(), TEXT("RPGInventoryRecoveryConsumeE2E"));
		bConsumeQuickSlot = FParse::Param(FCommandLine::Get(), TEXT("RPGInventoryRecoveryConsumeQuickSlotE2E"));
		bConsumeDeath = FParse::Param(FCommandLine::Get(), TEXT("RPGInventoryRecoveryConsumeDeathE2E"));
		if (bConsumeDeath && (!bConsume || bConsumeQuickSlot)) return Fail(TEXT("ConsumeDeathRecoveryNeedsBagConsume"));
		if ((bConsumeQuickSlot && !bConsume) || (bConsume && (bRetryFailure || bCredentialExpiry || bRestored ||
			FParse::Param(FCommandLine::Get(), TEXT("RPGInventoryRecoveryRestartE2E")) || FParse::Param(FCommandLine::Get(), TEXT("RPGInventoryRecoveryRevocationE2E")))))
			return Fail(TEXT("ConsumeRecoveryNeedsCurrentSingleSession"));
		if (int32(bSwap) + int32(bMerge) + int32(bSplit) + int32(bClaimMail) + int32(bEquip) + int32(bUnequip) + int32(bConsume) > 1 || ((bSwap || bMerge || bSplit || bEquip || bUnequip) && (!FParse::Param(FCommandLine::Get(), TEXT("RPGInventoryRecoveryRestartE2E")) || bRetryFailure || bCredentialExpiry ||
			FParse::Param(FCommandLine::Get(), TEXT("RPGInventoryRecoveryRevocationE2E"))))) return Fail(TEXT("InventoryRecoveryPairNeedsSeparateRestartMode"));
		if (bClaimMail && (bRetryFailure || bCredentialExpiry ||
			(bRestored && !FParse::Param(FCommandLine::Get(), TEXT("RPGInventoryRecoveryRestartE2E"))) ||
			FParse::Param(FCommandLine::Get(), TEXT("RPGInventoryRecoveryRevocationE2E")))) return Fail(TEXT("MailRecoveryMixedFailureOrRestoreMode"));
		if (bRetryFailure && bCredentialExpiry) return Fail(TEXT("InventoryRecoveryMixedFailureModes"));
		if (bRestored)
		{
			FString Original;
			if (bRetryFailure || bCredentialExpiry || FParse::Param(FCommandLine::Get(), TEXT("RPGInventoryRecoveryRevocationE2E")) ||
				!FParse::Value(FCommandLine::Get(), TEXT("RPGRecoveryOriginalRequest="), Original) ||
				!FGuid::Parse(Original, Request) || !Request.IsValid()) return Fail(TEXT("InventoryRecoveryInvalidRestoreArguments"));
		}
		if (FParse::Param(FCommandLine::Get(), TEXT("RPGInventoryRecoveryRevocationE2E")))
		{
			const auto* State = PC->GetPlayerState<ARPGPlayerState>();
			auto* Observer = PC->GetGameInstance()->GetSubsystem<URPGDungeonEjectionNetworkClient>();
			if (bRetryFailure || bCredentialExpiry || !State || !Observer) return Fail(TEXT("InventoryRevocationObserverMissingOrMixed"));
			Observer->PlayerId = State->GetPlayerId();
		}
		FString A, B;
		if (!FParse::Value(FCommandLine::Get(), TEXT("RPGRecoverySource="), A) || !FGuid::Parse(A, Source) || !Source.IsValid() ||
			!FParse::Value(FCommandLine::Get(), TEXT("RPGRecoveryOther="), B) || !FGuid::Parse(B, Other) || !Other.IsValid() || Source == Other ||
			!FParse::Value(FCommandLine::Get(), TEXT("RPGRecoveryContinueFile="), ContinueFile))
			return Fail(TEXT("InventoryRecoveryInvalidArguments"));
		if (bSplit && bRestored)
		{
			Created = FRPGItemActionCommandPlanner::MakeSplitItemId(Request, Source);
			if (!Created.IsValid() || Created == Source || Created == Other) return Fail(TEXT("InventoryRestoredSplitIdentityInvalid"));
		}
		Deadline = FPlatformTime::Seconds() + 240.0;
		PC->ShowInventoryProjection();
		if (bClaimMail) PC->ShowRewardMail();
		if (bEquip || bUnequip)
		{
			PC->ShowEquipmentProjection();
			if (bEquip && !bRestored) PC->ShowInventoryProjection();
		}
		Phase = bRestored ? 10 : 1; return 0;
	}
	if (FPlatformTime::Seconds() >= Deadline) return Fail(TEXT("InventoryRecoveryUITimeout"));
	if (!Bag.IsValid())
	{
		TArray<UUserWidget*> Screens;
		UWidgetBlueprintLibrary::GetAllWidgetsOfClass(PC->GetWorld(), Screens, URPGInventoryProjectionWidget::StaticClass(), false);
		for (auto* Screen : Screens)
			if (Screen->GetOwningPlayer() == PC && Screen->IsInViewport() && Screen->GetWidgetFromName(TEXT("InventoryGrid")))
				Bag = Cast<URPGInventoryProjectionWidget>(Screen);
	}
	auto* Screen = Bag.Get(); auto* Model = Screen ? Screen->GetInventoryViewModel() : nullptr;
	if (!Model || Model->LoadState != ERPGInventoryProjectionLoadState::Ready) return 0;
	auto* Abilities = PC->GetPawn() ? PC->GetPawn()->FindComponentByClass<URPGAbilitySystemComponent>() : nullptr;
	auto* QuickSlots = PC->GetPawn() ? PC->GetPawn()->FindComponentByClass<UQuickSlotComponent>() : nullptr;
	const float Health = Abilities ? Abilities->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute()) : 0.f;
	const float MaxHealth = Abilities ? Abilities->GetNumericAttribute(URPGAttributeSet::GetMaxHealthAttribute()) : 0.f;
	if (bConsumeDeath && Phase > 1 && (!ConsumePawn.IsValid() || ConsumePawn->IsActorBeingDestroyed() ||
		PC->GetPawn() != ConsumePawn.Get() || !ConsumeAbilities.IsValid() || Abilities != ConsumeAbilities.Get()))
		return Fail(TEXT("ConsumeDeathRecoveryOriginalAvatarChanged"));
	if (bConsume && (!Abilities || !FMath::IsFinite(Health) || !FMath::IsFinite(MaxHealth) || MaxHealth <= 0.f))
	{
		if (bConsumeDeath && Phase > 1) return Fail(TEXT("ConsumeDeathRecoveryHealthUnavailable"));
		return 0;
	}
	if (bConsume && Health <= 0.f && (!bConsumeDeath || Phase < 5)) return 0;
	if (bConsumeDeath && Phase >= 5 && (Health < 0.f || (bConsumeDeathObserved && Health != 0.f)))
		return Fail(TEXT("ConsumeDeathRecoveryHealthChangedAfterDeath"));
	if (bClaimMail && !Mail.IsValid())
	{
		TArray<UUserWidget*> Screens;
		UWidgetBlueprintLibrary::GetAllWidgetsOfClass(PC->GetWorld(), Screens, URPGRewardMailWidget::StaticClass(), false);
		for (auto* Candidate : Screens)
			if (Candidate->GetOwningPlayer() == PC && Candidate->IsInViewport()) Mail = Cast<URPGInventoryProjectionWidget>(Candidate);
	}
	auto* MailScreen = Mail.Get();
	if (bClaimMail && (!MailScreen || MailScreen->GetInventoryViewModel() != Model)) return 0;
	const bool bEquipment = bEquip || bUnequip;
	if (bEquipment && !Equipment.IsValid())
	{
		TArray<UUserWidget*> Screens;
		UWidgetBlueprintLibrary::GetAllWidgetsOfClass(PC->GetWorld(), Screens, URPGInventoryProjectionWidget::StaticClass(), false);
		for (auto* Candidate : Screens)
			if (Candidate->GetOwningPlayer() == PC && Candidate->IsInViewport() && Candidate->GetWidgetFromName(TEXT("EquipmentLeftSlots")))
				Equipment = Cast<URPGInventoryProjectionWidget>(Candidate);
	}
	auto* EquipmentScreen = Equipment.Get();
	if (bEquipment && (!EquipmentScreen || EquipmentScreen->GetInventoryViewModel() != Model)) return 0;
	if (BoundModel.Get() != Model || !ResultHandle.IsValid())
	{
		UnbindResult(); BoundModel = Model;
		ResultHandle = Model->AddFieldValueChangedDelegate(
			URPGInventoryProjectionViewModel::FFieldNotificationClassDescriptor::LastCommandResult,
			INotifyFieldValueChanged::FFieldValueChangedDelegate::CreateLambda([this](UObject* Object, UE::FieldNotification::FFieldId)
			{
				const auto* Value = Cast<URPGInventoryProjectionViewModel>(Object);
				if (Value && Value->LastCommandResult.RequestId.IsValid()) ++Acknowledgements;
			}));
	}
	FRPGInventoryProjectionEntry A, B, C;
	const bool bHasSource = Projection->FindProjectedItem(Source, A) || (bClaimMail && Projection->FindMailItem(Source, A));
	const bool bHasCreated = Created.IsValid() && Projection->FindProjectedItem(Created, C);
	if ((!bHasSource && !(bRestored && bMerge) && !(bClaimMail && Phase >= 5)) || !Projection->FindProjectedItem(Other, B) ||
		(bClaimMail ? Model->InventoryItems.Num() < 1 || Model->InventoryItems.Num() > 2 || Model->MailItems.Num() > 1
		: bEquipment ? Model->InventoryItems.Num() < 1 || Model->InventoryItems.Num() > 2 || !Model->MailItems.IsEmpty()
		: Model->InventoryItems.Num() != (bRestored && bSplit ? 3 : bRestored && bMerge ? 1 : 2)))
		return Fail(TEXT("InventoryRecoveryOwnItemsMissing"));
	const auto ClaimButton = [MailScreen, this]() -> UButton*
	{
		const auto* Rows = MailScreen ? Cast<UVerticalBox>(MailScreen->GetWidgetFromName(TEXT("MailRows"))) : nullptr;
		if (!Rows) return nullptr;
		for (UWidget* Child : Rows->GetAllChildren())
			if (auto* Row = Cast<URPGRewardMailEntryWidget>(Child); Row && Row->GetAttachmentId() == Source)
				return Cast<UButton>(Row->GetWidgetFromName(TEXT("ClaimButton")));
		return nullptr;
	};
	const auto CellMatches = [Screen, Model](int32 Slot, const FGuid Id, int32 Quantity)
	{
		const auto* Grid = Cast<UUniformGridPanel>(Screen->GetWidgetFromName(TEXT("InventoryGrid")));
		const auto* Cell = Grid ? Cast<URPGInventoryProjectionItemSlotWidget>(Grid->GetChildAt(Slot)) : nullptr;
		const auto* Label = Cell ? Cast<UTextBlock>(Cell->GetWidgetFromName(TEXT("ItemFallbackName"))) : nullptr;
		const auto* Count = Cell ? Cast<UTextBlock>(Cell->GetWidgetFromName(TEXT("ItemQuantityText"))) : nullptr;
		if (!Label || !Count) return false;
		if (!Id.IsValid()) return Label->GetText().IsEmpty() && Count->GetText().IsEmpty() && !Count->IsVisible();
		const auto* Entry = Model->InventoryItems.FindByPredicate([Id](const auto& Value) { return Value.GetItemId() == Id; });
		const FString ExpectedCount = Quantity > 1 ? TEXT("x") + FText::AsNumber(Quantity).ToString() : FString();
		return Entry && Entry->GetSlotIndex() == Slot && Entry->GetQuantity() == Quantity &&
			Label->GetText().EqualTo(Model->GetItemPresentation(Id).DisplayName) &&
			Count->GetText().ToString() == ExpectedCount && Count->IsVisible() == (Quantity > 1);
	};
	const auto HeadCell = [EquipmentScreen]() -> URPGInventoryProjectionItemSlotWidget*
	{
		const auto* Column = EquipmentScreen ? Cast<UVerticalBox>(EquipmentScreen->GetWidgetFromName(TEXT("EquipmentLeftSlots"))) : nullptr;
		const auto* Bounds = Column ? Cast<USizeBox>(Column->GetChildAt(0)) : nullptr;
		return Bounds ? Cast<URPGInventoryProjectionItemSlotWidget>(Bounds->GetContent()) : nullptr;
	};
	const auto EquipmentMatches = [&Controller, &HeadCell, Model, this](const bool bEquipped, const int64 Revision)
	{
		const auto* Runtime = Controller.GetPawn() ? Controller.GetPawn()->FindComponentByClass<URPGAuthoritativeEquipmentComponent>() : nullptr;
		const auto* Cell = HeadCell();
		const auto* Label = Cell ? Cast<UTextBlock>(Cell->GetWidgetFromName(TEXT("ItemFallbackName"))) : nullptr;
		if (!Runtime || !Label) return false;
		FRPGEquipmentSnapshot Snapshot;
		const bool bHasHead = Runtime->FindSnapshotInSlot(EEquipmentSlotType::Head, Snapshot);
		const auto* Entry = Model->EquipmentItems.FindByPredicate([this](const auto& Value) { return Value.GetItemId() == Source; });
		return bEquipped ? bHasHead && Snapshot.ItemId == Source && Snapshot.Revision == Revision && Entry &&
			Entry->GetSlotIndex() == static_cast<int32>(EEquipmentSlotType::Head) && Entry->GetRevision() == Revision &&
			Label->GetText().EqualTo(Model->GetItemPresentation(Source).DisplayName)
			: !bHasHead && !Entry && Label->GetText().ToString() == TEXT("HEAD");
	};
	const auto OldSnapshot = [&]()
	{
		if (bConsume && Phase > 1 && !(bConsumeDeath && Phase >= 5) && !FMath::IsNearlyEqual(Health, ConsumeHealthBefore)) return false;
		if (bEquipment)
			return A.GetContainerType() == (bUnequip ? ERPGItemContainerType::Equipment : ERPGItemContainerType::Inventory) &&
				A.GetSlotIndex() == 0 && A.GetRevision() == 1 && A.GetQuantity() == 1 &&
				Model->InventoryItems.Num() == (bUnequip ? 1 : 2) && B.GetSlotIndex() == 1 && B.GetRevision() == 1 && B.GetQuantity() == 1 &&
				CellMatches(0, bUnequip ? FGuid() : Source, bUnequip ? 0 : 1) && CellMatches(1, Other, 1) && EquipmentMatches(bUnequip, 1);
		if (bClaimMail)
			return bHasSource && A.GetContainerType() == ERPGItemContainerType::Mail && A.GetSlotIndex() == 0 &&
				A.GetRevision() == 1 && A.GetQuantity() == 7 && Model->InventoryItems.Num() == 1 && Model->MailItems.Num() == 1 &&
				B.GetSlotIndex() == 1 && B.GetRevision() == 1 && B.GetQuantity() == 1 &&
				CellMatches(0, FGuid(), 0) && CellMatches(1, Other, 1) && ClaimButton();
		return A.GetSlotIndex() == 0 && A.GetRevision() == 1 && A.GetQuantity() == 7 &&
			B.GetSlotIndex() == 1 && B.GetRevision() == 1 && B.GetQuantity() == 1 &&
			CellMatches(0, Source, 7) && CellMatches(1, Other, 1) && CellMatches(3, FGuid(), 0);
	};
	const auto FindConsumeShortcut = [PC, this]() -> URPGQuickSlotWidget*
	{
		TArray<UUserWidget*> Widgets;
		UWidgetBlueprintLibrary::GetAllWidgetsOfClass(PC->GetWorld(), Widgets, URPGQuickSlotWidget::StaticClass(), false);
		URPGQuickSlotWidget* Found = nullptr;
		for (auto* Widget : Widgets)
			if (auto* Slot = Cast<URPGQuickSlotWidget>(Widget); Slot && Slot->GetOwningPlayer() == PC && Slot->IsVisible() && Slot->GetSlotItemId() == Source)
			{
				if (Found) return nullptr;
				Found = Slot;
			}
		return Found;
	};
	const auto RecoveryButton = [RecoveryScreen = bClaimMail ? MailScreen : bEquipment ? EquipmentScreen : Screen]()
	{
		UButton* Found = nullptr;
		RecoveryScreen->WidgetTree->ForEachWidget([&Found](UWidget* Widget)
		{
			auto* Button = Cast<UButton>(Widget); const auto* Label = Button ? Cast<UTextBlock>(Button->GetContent()) : nullptr;
			if (Label && Label->GetText().EqualTo(NSLOCTEXT("RPGItemUI", "RetryInventoryCommand", "Recover pending item command"))) Found = Button;
		});
		return Found;
	};
	FRPGInventoryStackSplitE2E Input;
	Input.Bag = Bag; Input.bMoveSwapMode = !bSplit; Input.bQuantityMode = bSplit; // routed Slate helpers only, no direct UI/RPC requests
	const FName ExpectedOperation(bConsume ? TEXT("Item.Consume") : bEquip ? TEXT("Item.Equip") : bUnequip ? TEXT("Item.Unequip") : bClaimMail ? TEXT("Item.ClaimMail") : bSplit ? TEXT("Item.Split") : bMerge ? TEXT("Item.StackTransfer") : bSwap ? TEXT("Item.Swap") : TEXT("Item.Move"));
	if (bRestored)
	{
		const bool bFreshSnapshot = bEquipment
			? A.GetContainerType() == (bEquip ? ERPGItemContainerType::Equipment : ERPGItemContainerType::Inventory) &&
				A.GetSlotIndex() == 0 && A.GetRevision() == 2 && A.GetQuantity() == 1 &&
				Model->InventoryItems.Num() == (bEquip ? 1 : 2) && B.GetSlotIndex() == 1 && B.GetRevision() == 1 && B.GetQuantity() == 1 &&
				CellMatches(0, bEquip ? FGuid() : Source, bEquip ? 0 : 1) && CellMatches(1, Other, 1) && EquipmentMatches(bEquip, 2)
			: bClaimMail
			? bHasSource && A.GetContainerType() == ERPGItemContainerType::Inventory && A.GetSlotIndex() == 0 &&
				A.GetRevision() == 2 && A.GetQuantity() == 7 && Model->InventoryItems.Num() == 2 && Model->MailItems.IsEmpty() &&
				!Model->IsMailClaimPending(Source) && !ClaimButton() &&
				B.GetSlotIndex() == 1 && B.GetRevision() == 1 && B.GetQuantity() == 1 &&
				CellMatches(0, Source, 7) && CellMatches(1, Other, 1) && CellMatches(3, FGuid(), 0)
			: bSplit
			? bHasCreated && A.GetSlotIndex() == 0 && A.GetRevision() == 2 && A.GetQuantity() == 5 &&
				B.GetSlotIndex() == 1 && B.GetRevision() == 1 && B.GetQuantity() == 1 &&
				C.GetSlotIndex() == 3 && C.GetRevision() == 1 && C.GetQuantity() == 2 &&
				CellMatches(0, Source, 5) && CellMatches(1, Other, 1) && CellMatches(3, Created, 2)
			: bMerge
			? !bHasSource && B.GetSlotIndex() == 1 && B.GetRevision() == 2 && B.GetQuantity() == 8 &&
				CellMatches(0, FGuid(), 0) && CellMatches(1, Other, 8) && CellMatches(3, FGuid(), 0)
			: bSwap
			? A.GetSlotIndex() == 1 && A.GetRevision() == 2 && A.GetQuantity() == 7 &&
				B.GetSlotIndex() == 0 && B.GetRevision() == 2 && B.GetQuantity() == 1 &&
				CellMatches(1, Source, 7) && CellMatches(0, Other, 1) && CellMatches(3, FGuid(), 0)
			: A.GetSlotIndex() == 3 && A.GetRevision() == 2 && A.GetQuantity() == 7 &&
				B.GetSlotIndex() == 1 && B.GetRevision() == 1 && B.GetQuantity() == 1 &&
				CellMatches(0, FGuid(), 0) && CellMatches(3, Source, 7) && CellMatches(1, Other, 1);
		if (!Screen->GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint) || Model->bInventoryRecoveryStorageBlocked ||
			Model->bInventoryAuthorityRevoked) return Fail(TEXT("InventoryRestoredAuthorityOrStorageBlocked"));
		if (bEquipment && !EquipmentScreen->GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint)) return Fail(TEXT("EquipmentRecoveryRequiresAuthoredWBP"));
		if (Phase == 10 || Phase == 11)
		{
			// Initial load is current DB state, not the old client's optimistic/reservation state.
			// Owner-only recovery replication can arrive after the initial ready snapshot.
			if (!Model->bHasInventoryRecovery || !Model->bCanRetryInventoryRecovery || !bFreshSnapshot) return 0;
			auto* Button = RecoveryButton();
			if (Commands->InventoryRecovery.Num() != 1 || Commands->InventoryRecovery[0].RequestId != Request ||
				Commands->InventoryRecovery[0].Operation != ExpectedOperation || Model->CanMoveItemToSlot(Source, 2, 4) ||
				((bSwap || bMerge) && Model->CanMoveItemToSlot(Other, 2, 5)) ||
				(bClaimMail && Model->CanMoveItemToSlot(Other, 1, 5)) ||
				(bEquipment && (Model->CanMoveItemToSlot(Other, 1, 5) ||
					Model->GetItemSlotPresentationState(Source, A.GetContainerType(), 0) != ERPGItemSlotPresentationState::RecoveryRequired)) ||
				(bSplit && Model->CanMoveItemToSlot(Created, 1, 5)) ||
				Model->bHasLastCommandResult || !Button || !Button->IsVisible() || !Button->GetIsEnabled())
				return Fail(TEXT("InventoryRestoredOriginalOrUiLockMissing"));
			if (Phase == 10)
			{
				UE_LOG(LogTemp, Display, TEXT("RPG_INVENTORY_RECOVERY_UI RESTORED Request=%s FreshProjection=1 Locked=1 Button=1 NoDrag=1"),
					*Request.ToString(EGuidFormats::DigitsWithHyphensLower));
				if (bClaimMail) UE_LOG(LogTemp, Display, TEXT("RPG_INVENTORY_RECOVERY_UI CLAIM_MAIL_RESTORED SameItemId=1 Quantity=7 Revision=2 MailEmpty=1 InventoryLocked=1 ClaimClick=0"));
				if (bEquipment) UE_LOG(LogTemp, Display, TEXT("RPG_INVENTORY_RECOVERY_UI EQUIPMENT_RESTORED Operation=%s SameItemId=1 Quantity=1 Revision=2 RuntimeSnapshot=1 Locked=1 ActionClicks=0"), *ExpectedOperation.ToString());
				Phase = 11;
			}
			if (!IFileManager::Get().FileExists(*ContinueFile)) return 0;
			if (!Input.Click(Controller, Button)) { Error = Input.Error; return -1; }
			if (!Model->bInventoryRecoveryInFlight || Model->bCanRetryInventoryRecovery || Button->GetIsEnabled())
				return Fail(TEXT("InventoryRestoredClickDidNotDisableLocally"));
			UE_LOG(LogTemp, Display, TEXT("RPG_INVENTORY_RECOVERY_UI RESTORED_CLICKED Request=%s HitTest=1 Routed=1 Physical=0"),
				*Request.ToString(EGuidFormats::DigitsWithHyphensLower));
			Phase = 12;
		}
		const bool bUnlocked = bEquipment ? Model->GetItemSlotPresentationState(Source, A.GetContainerType(), 0) == ERPGItemSlotPresentationState::Idle &&
			Model->PendingItemCommandCount == 0 && Model->CanMoveItemToSlot(Other, 1, 5) && (bEquip || Model->CanMoveItemToSlot(Source, 2, 4))
			: bClaimMail ? Model->CanMoveItemToSlot(Source, 2, 4) && Model->CanMoveItemToSlot(Other, 1, 5)
			: bSplit ? Model->CanMoveItemToSlot(Source, 2, 4) && Model->CanMoveItemToSlot(Created, 1, 5)
			: bMerge ? !Model->CanMoveItemToSlot(Source, 2, 4) && Model->CanMoveItemToSlot(Other, 2, 5)
			: Model->CanMoveItemToSlot(Source, 2, 4) && (!bSwap || Model->CanMoveItemToSlot(Other, 2, 5));
		if (Phase == 12 && !Model->bHasInventoryRecovery && Commands->InventoryRecovery.IsEmpty() &&
			Model->bHasLastCommandResult && Model->LastCommandResult.RequestId == Request &&
			Model->LastCommandResult.Operation == ExpectedOperation && Model->LastCommandResult.Result == ERPGItemCommandResultCode::AlreadyApplied &&
			bFreshSnapshot && bUnlocked)
		{
			UE_LOG(LogTemp, Display, TEXT("RPG_INVENTORY_RECOVERY_UI RESTART_PASS Authenticated=1 Iris=1 AuthoredWBP=1 FreshProjection=1 SameRequest=1 AlreadyApplied=1 Unlocked=1 Physical=0"));
			if (bSwap) UE_LOG(LogTemp, Display, TEXT("RPG_INVENTORY_RECOVERY_UI SWAP_RESTART_PASS Records=2 AffectedQuantity=0 BothRevision=2 BothUnlocked=1"));
			if (bMerge) UE_LOG(LogTemp, Display, TEXT("RPG_INVENTORY_RECOVERY_UI MERGE_RESTART_PASS Records=2 AffectedQuantity=7 SourceConsumed=1 DestinationQuantity=8 DestinationRevision=2 DestinationUnlocked=1"));
			if (bSplit) UE_LOG(LogTemp, Display, TEXT("RPG_INVENTORY_RECOVERY_UI SPLIT_RESTART_PASS Records=2 AffectedQuantity=2 SourceQuantity=5 SourceRevision=2 CreatedQuantity=2 CreatedRevision=1 SameCreatedId=1 BothUnlocked=1"));
			if (bClaimMail) UE_LOG(LogTemp, Display, TEXT("RPG_INVENTORY_RECOVERY_UI CLAIM_MAIL_RESTART_PASS NativeMail=1 SameRequest=1 SameItemId=1 Quantity=7 Revision=2 MailEmpty=1 InventoryUnlocked=1"));
			if (bEquip) UE_LOG(LogTemp, Display, TEXT("RPG_INVENTORY_RECOVERY_UI EQUIP_RESTART_PASS AuthoredEquipmentWBP=1 SameRequest=1 SameItemId=1 Quantity=1 Revision=2 RuntimeSnapshot=1 Unlocked=1"));
			if (bUnequip) UE_LOG(LogTemp, Display, TEXT("RPG_INVENTORY_RECOVERY_UI UNEQUIP_RESTART_PASS AuthoredEquipmentWBP=1 SameRequest=1 SameItemId=1 Quantity=1 Revision=2 RuntimeSnapshot=1 Unlocked=1"));
			return 1;
		}
		return 0;
	}
	if (Phase == 1)
	{
		if (bConsume)
		{
			// Only the dedicated server's approved damage fixture makes this possible.
			if (Health >= MaxHealth - .01f) return 0;
			ConsumeHealthBefore = Health;
			if (bConsumeQuickSlot)
			{
				if (!QuickSlots) return 0;
				if (!bConsumeShortcutAssigned)
				{
					QuickSlots->SetItemSlotById(0, Source); // normal client-local shortcut only, not an item/command injection
					bConsumeShortcutAssigned = true;
				}
				if (!FindConsumeShortcut()) return 0;
			}
		}
		if (!Screen->GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint)) return Fail(TEXT("InventoryRecoveryRequiresAuthoredWBP"));
		if (bEquipment && (!EquipmentScreen->GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint) || !OldSnapshot())) return 0;
		if (!OldSnapshot() || Model->bHasInventoryRecovery) return Fail(TEXT("InventoryRecoveryInitialSnapshotMismatch"));
		const int32 Destination = bSwap || bMerge ? 1 : 3;
		if (bConsume)
		{
			const auto* Grid = Cast<UUniformGridPanel>(Screen->GetWidgetFromName(TEXT("InventoryGrid")));
			UWidget* Control = bConsumeQuickSlot ? FindConsumeShortcut() : Grid ? Grid->GetChildAt(0) : nullptr;
			if (bConsumeDeath) { ConsumePawn = PC->GetPawn(); ConsumeAbilities = Abilities; }
			if (!Input.Click(Controller, Control, !bConsumeQuickSlot)) { Error = Input.Error; return -1; }
			UE_LOG(LogTemp, Display, TEXT("RPG_INVENTORY_RECOVERY_UI CONSUME_CLICK Input=%s Quantity=7 Revision=1 Health=%g HitTest=1 Routed=1 Physical=0"),
				bConsumeQuickSlot ? TEXT("QuickSlot") : TEXT("Bag"), Health);
		}
		else if (bEquipment)
		{
			const auto* Grid = Cast<UUniformGridPanel>(Screen->GetWidgetFromName(TEXT("InventoryGrid")));
			auto* Cell = bUnequip ? HeadCell() : Grid ? Grid->GetChildAt(0) : nullptr;
			if (!Input.Click(Controller, Cell, true)) { Error = Input.Error; return -1; }
			if (Model->PendingItemCommandCount != 1 || !OldSnapshot()) return Fail(TEXT("EquipmentRecoveryActionDidNotReserveOriginal"));
			UE_LOG(LogTemp, Display, TEXT("RPG_INVENTORY_RECOVERY_UI EQUIPMENT_ACTION Operation=%s Quantity=1 Destination=0 RightMouse=1 HitTest=1 Routed=1 Physical=0"), *ExpectedOperation.ToString());
		}
		else if (bClaimMail)
		{
			if (Model->GetMailClaimPresentation(Source).State != ERPGMailClaimState::Available ||
				!Input.Click(Controller, ClaimButton())) { Error = Input.Error.IsEmpty() ? TEXT("MailRecoveryClaimNotAvailable") : Input.Error; return -1; }
			if (!Model->IsMailClaimPending(Source) || !OldSnapshot()) return Fail(TEXT("MailRecoveryClaimDidNotReserveOriginal"));
			UE_LOG(LogTemp, Display, TEXT("RPG_INVENTORY_RECOVERY_UI CLAIM_CLICKED Quantity=7 Destination=0 NativeMail=1 HitTest=1 Routed=1 Physical=0"));
		}
		else if (!Input.Drag(Controller, Destination, true)) { Error = Input.Error; return -1; }
		if (bSplit)
		{
			auto* QuantityInput = Cast<UEditableTextBox>(Screen->GetWidgetFromName(TEXT("SplitQuantityInput")));
			auto* Confirm = Cast<UButton>(Screen->GetWidgetFromName(TEXT("ConfirmSplitQuantityButton")));
			if (!Screen->IsSplitQuantitySelectionOpen() || Model->bHasLastCommandResult || !Confirm)
				return Fail(TEXT("InventoryRecoverySplitSelectionMissingOrSubmittedEarly"));
			if (!Input.TypeQuantity(Controller, QuantityInput, TEXT("2"))) { Error = Input.Error; return -1; }
			if (!Screen->CanConfirmSplitQuantitySelection() || !Confirm->GetIsEnabled()) return Fail(TEXT("InventoryRecoverySplitChoiceDisabled"));
			if (!Input.Click(Controller, Confirm)) { Error = Input.Error; return -1; }
			if (Screen->IsSplitQuantitySelectionOpen() || !OldSnapshot()) return Fail(TEXT("InventoryRecoverySplitConfirmationMutatedProjection"));
			UE_LOG(LogTemp, Display, TEXT("RPG_INVENTORY_RECOVERY_UI SPLIT_CONFIRMED Quantity=2 HitTest=1 Routed=1 DirectSetText=0 Physical=0"));
		}
		if (!bClaimMail && !bEquipment && !bConsume) UE_LOG(LogTemp, Display, TEXT("RPG_INVENTORY_RECOVERY_UI DRAG Source=0 Destination=%d HitTest=1 Routed=1 Physical=0"), Destination);
		Phase = 2;
	}
	else if (Phase == 2)
	{
		if (!Model->bHasInventoryRecovery || !Model->bCanRetryInventoryRecovery || !Model->bHasLastCommandResult ||
			Model->LastCommandResult.Result != ERPGItemCommandResultCode::Uncertain) return 0;
		Request = Model->LastCommandResult.RequestId;
		if (bSplit)
		{
			Created = FRPGItemActionCommandPlanner::MakeSplitItemId(Request, Source);
			if (!Created.IsValid() || Created == Source || Created == Other) return Fail(TEXT("InventoryRecoverySplitIdentityInvalid"));
		}
		if (!OldSnapshot() || Commands->InventoryRecovery.Num() != 1 || Commands->InventoryRecovery[0].RequestId != Request ||
			Commands->InventoryRecovery[0].Operation != ExpectedOperation || Model->CanMoveItemToSlot(Source, 1, 4) ||
			((bSwap || bMerge) && Model->CanMoveItemToSlot(Other, 1, 5)))
			return Fail(TEXT("InventoryRecoveryUncertainNotLocked"));
		if (bClaimMail && (!Model->IsMailClaimPending(Source) || !ClaimButton() || ClaimButton()->GetIsEnabled() ||
			Model->GetMailClaimPresentation(Source).State != ERPGMailClaimState::RecoveryRequired)) return Fail(TEXT("MailRecoveryUncertainReservationLost"));
		if (bEquipment && (Model->PendingItemCommandCount != 1 || Model->CanMoveItemToSlot(Other, 1, 5))) return Fail(TEXT("EquipmentRecoveryUncertainReservationLost"));
		UE_LOG(LogTemp, Display, TEXT("RPG_INVENTORY_RECOVERY_UI UNCERTAIN Request=%s OldProjection=1 Locked=1"),
			*Request.ToString(EGuidFormats::DigitsWithHyphensLower));
		// Actual widget teardown/rebind, not a projection or pending-state injection.
		auto* ReopenedScreen = bClaimMail ? MailScreen : bEquipment ? EquipmentScreen : Screen;
		UnbindResult(); ReopenedScreen->DeactivateWidget(); ReopenedScreen->RemoveFromParent();
		if (bClaimMail) Mail.Reset(); else if (bEquipment) Equipment.Reset(); else Bag.Reset();
		ReopenedScreen->AddToViewport(20);
		if (bClaimMail) PC->ShowRewardMail(); else if (bEquipment) PC->ShowEquipmentProjection(); else PC->ShowInventoryProjection();
		Phase = 3;
	}
	else if (Phase == 3 || Phase == 4)
	{
		auto* Button = RecoveryButton();
		if (!OldSnapshot() || !Model->bHasInventoryRecovery || !Model->bCanRetryInventoryRecovery ||
			Commands->InventoryRecovery.Num() != 1 || Commands->InventoryRecovery[0].RequestId != Request ||
			!Button || !Button->IsVisible() || !Button->GetIsEnabled() || Model->CanMoveItemToSlot(Source, 1, 4) ||
			((bSwap || bMerge) && Model->CanMoveItemToSlot(Other, 1, 5)))
			return Fail(TEXT("InventoryRecoveryReopenedStateLost"));
		if (bClaimMail && (!Model->IsMailClaimPending(Source) || !ClaimButton() || ClaimButton()->GetIsEnabled()))
			return Fail(TEXT("MailRecoveryReopenedReservationLost"));
		if (bEquipment && (Model->PendingItemCommandCount != 1 || Model->CanMoveItemToSlot(Other, 1, 5))) return Fail(TEXT("EquipmentRecoveryReopenedReservationLost"));
		if (Phase == 3)
		{
			UE_LOG(LogTemp, Display, TEXT("RPG_INVENTORY_RECOVERY_UI BLOCKED Request=%s Reopened=1 OldProjection=1 Locked=1 Button=1"),
				*Request.ToString(EGuidFormats::DigitsWithHyphensLower));
			Phase = 4;
		}
		if (!IFileManager::Get().FileExists(*ContinueFile)) return 0;
		if (FParse::Param(FCommandLine::Get(), TEXT("RPGInventoryRecoveryRevocationE2E")))
		{
			UE_LOG(LogTemp, Display, TEXT("RPG_INVENTORY_RECOVERY_UI REVOCATION_WAIT Request=%s Pending=1 OldProjection=1 Locked=1 ManualReplay=0"),
				*Request.ToString(EGuidFormats::DigitsWithHyphensLower));
			return 1; // production membership polling/ejection owns the next transition, not this UI probe
		}
		BeforeRetryAcknowledgements = Acknowledgements;
		if (!Input.Click(Controller, Button)) { Error = Input.Error; return -1; }
		if (!Model->bInventoryRecoveryInFlight || Model->bCanRetryInventoryRecovery || Button->GetIsEnabled())
			return Fail(TEXT("InventoryRecoveryClickDidNotDisableLocally"));
		UE_LOG(LogTemp, Display, TEXT("RPG_INVENTORY_RECOVERY_UI CLICKED Request=%s HitTest=1 Routed=1 Physical=0"),
			*Request.ToString(EGuidFormats::DigitsWithHyphensLower));
		Phase = 5;
	}
	else if (Phase == 5)
	{
		if (bConsumeDeath && !bConsumeDeathObserved && Health == 0.f)
		{
			// The runner holds the original replay response until this owning-client
			// observation; death alone must not unlock the old item projection.
			if (!OldSnapshot() || !Model->bHasInventoryRecovery || !Model->bInventoryRecoveryInFlight ||
				Model->bCanRetryInventoryRecovery || Commands->InventoryRecovery.Num() != 1 ||
				Commands->InventoryRecovery[0].RequestId != Request || Commands->InventoryRecovery[0].Operation != ExpectedOperation ||
				Model->CanMoveItemToSlot(Source, 1, 4)) return Fail(TEXT("ConsumeDeathRecoveryUnlockedBeforeReceipt"));
			bConsumeDeathObserved = true;
			UE_LOG(LogTemp, Display, TEXT("RPG_INVENTORY_RECOVERY_UI DEAD_WAIT Request=%s Health=0 Pending=1 OldProjection=1 SamePawn=1"),
				*Request.ToString(EGuidFormats::DigitsWithHyphensLower));
		}
		if (bRetryFailure || bCredentialExpiry)
		{
			if (Acknowledgements <= BeforeRetryAcknowledgements) return 0;
			if (Model->LastCommandResult.RequestId != Request || Model->LastCommandResult.Result != ERPGItemCommandResultCode::Uncertain ||
				!OldSnapshot() || !Model->bHasInventoryRecovery || Model->bInventoryRecoveryInFlight || Model->bCanRetryInventoryRecovery ||
				Commands->InventoryRecovery.Num() != 1 || Commands->InventoryRecovery[0].RequestId != Request)
				return Fail(TEXT("InventoryRecoveryFailedRetryLostOriginalOrCooldown"));
			UE_LOG(LogTemp, Display, TEXT("RPG_INVENTORY_RECOVERY_UI RETRY_FAILED Request=%s Pending=1 OldProjection=1 Cooldown=1 SameAckObserved=1"),
				*Request.ToString(EGuidFormats::DigitsWithHyphensLower));
			Phase = 6; return 0;
		}
	}
	if (Phase == 6 || Phase == 7)
	{
		if (!OldSnapshot() || !Model->bHasInventoryRecovery || Commands->InventoryRecovery.Num() != 1 ||
			Commands->InventoryRecovery[0].RequestId != Request || Model->CanMoveItemToSlot(Source, 1, 4))
			return Fail(TEXT("InventoryRecoveryFailedRetryUnlockedItems"));
		if (!Model->bCanRetryInventoryRecovery) return 0;
		if (Phase == 6)
		{
			UE_LOG(LogTemp, Display, TEXT("RPG_INVENTORY_RECOVERY_UI RETRY_READY Request=%s Pending=1 CooldownFinished=1"),
				*Request.ToString(EGuidFormats::DigitsWithHyphensLower));
			Phase = 7;
		}
		if (bCredentialExpiry)
		{
			if (!IFileManager::Get().FileExists(*(ContinueFile + TEXT(".expired")))) return 0;
			const auto* State = PC->GetPlayerState<ARPGPlayerState>();
			if (!State || !State->HasAuthenticatedCharacter() || State->IsAuthenticatedIdentityRevoked())
				return Fail(TEXT("InventoryCredentialExpiryIncorrectlyRevokedOwner"));
			UE_LOG(LogTemp, Display, TEXT("RPG_INVENTORY_RECOVERY_UI EXPIRED_PASS Authenticated=1 Pending=1 OldProjection=1 OwnerIdentityRetained=1 NoForcedUnlock=1 Physical=0"));
			return 1; // credentials remain expired; do not manufacture a successful replay or renewal
		}
		if (!IFileManager::Get().FileExists(*(ContinueFile + TEXT(".retry")))) return 0;
		if (!Input.Click(Controller, RecoveryButton())) { Error = Input.Error; return -1; }
		UE_LOG(LogTemp, Display, TEXT("RPG_INVENTORY_RECOVERY_UI CLICKED_AGAIN Request=%s HitTest=1 Routed=1 Physical=0"),
			*Request.ToString(EGuidFormats::DigitsWithHyphensLower));
		Phase = 8;
	}
	if ((Phase == 5 && !bRetryFailure && !bCredentialExpiry) || Phase == 8)
	{
		if (Model->bHasInventoryRecovery || !Commands->InventoryRecovery.IsEmpty() || !Model->bHasLastCommandResult ||
			Model->LastCommandResult.RequestId != Request || Model->LastCommandResult.Result != ERPGItemCommandResultCode::AlreadyApplied) return 0;
		if (bConsume)
		{
			if (A.GetSlotIndex() != 0 || A.GetRevision() != 2 || A.GetQuantity() != 6 ||
				B.GetSlotIndex() != 1 || B.GetRevision() != 1 || B.GetQuantity() != 1 ||
				!CellMatches(0, Source, 6) || !CellMatches(1, Other, 1) || !Model->CanMoveItemToSlot(Source, 2, 4) ||
				(bConsumeDeath ? !bConsumeDeathObserved || Health != 0.f : Health <= ConsumeHealthBefore) ||
				Model->LastCommandResult.Operation != ExpectedOperation) return 0;
			if (bConsumeQuickSlot)
			{
				const auto* Slot = FindConsumeShortcut();
				const auto* Count = Slot ? Cast<UTextBlock>(Slot->GetWidgetFromName(TEXT("SlotItemCountText"))) : nullptr;
				FRPGInventoryProjectionEntry Assigned;
				if (!QuickSlots->FindAssignedItem(0, Assigned) || Assigned.GetItemId() != Source || Assigned.GetQuantity() != 6 || !Count || Count->GetText().ToString() != FText::AsNumber(6).ToString()) return 0;
			}
			if (bConsumeDeath)
			{
				UE_LOG(LogTemp, Display, TEXT("RPG_INVENTORY_RECOVERY_UI CONSUME_DEATH_PASS Input=Bag SameRequest=1 SameItemId=1 Quantity=6 Revision=2 HealthAfter=0 Unlocked=1 SamePawn=1"));
			}
			else
			{
				UE_LOG(LogTemp, Display, TEXT("RPG_INVENTORY_RECOVERY_UI CONSUME_PASS Input=%s SameRequest=1 SameItemId=1 Quantity=6 Revision=2 HealthBefore=%g HealthAfter=%g Unlocked=1"),
					bConsumeQuickSlot ? TEXT("QuickSlot") : TEXT("Bag"), ConsumeHealthBefore, Health);
			}
		}
		else if (bClaimMail)
		{
			// Mail removal and inventory addition can arrive separately. Neither ACK nor one snapshot alone retires the reservation.
			if (!bHasSource || A.GetContainerType() != ERPGItemContainerType::Inventory || A.GetSlotIndex() != 0 ||
				A.GetRevision() != 2 || A.GetQuantity() != 7 || !Model->MailItems.IsEmpty() || Model->IsMailClaimPending(Source) ||
				!CellMatches(0, Source, 7) || !CellMatches(1, Other, 1) || !Model->CanMoveItemToSlot(Source, 2, 4) ||
				B.GetSlotIndex() != 1 || B.GetRevision() != 1 || B.GetQuantity() != 1) return 0;
			UE_LOG(LogTemp, Display, TEXT("RPG_INVENTORY_RECOVERY_UI CLAIM_MAIL_PASS NativeMail=1 ClaimClick=1 MailReopened=1 SameRequest=1 AlreadyApplied=1 SameItemId=1 Quantity=7 Revision=2 MailRemoved=1 InventoryAdded=1 ReservationReleased=1"));
		}
		else if (A.GetSlotIndex() != 3 || A.GetRevision() != 2 || A.GetQuantity() != 7 ||
			B.GetSlotIndex() != 1 || B.GetRevision() != 1 || B.GetQuantity() != 1 || !CellMatches(0, FGuid(), 0) ||
			!CellMatches(3, Source, 7) || !CellMatches(1, Other, 1) || !Model->CanMoveItemToSlot(Source, 2, 4)) return 0;
		UE_LOG(LogTemp, Display, TEXT("RPG_INVENTORY_RECOVERY_UI PASS Authenticated=1 Iris=1 AuthoredWBP=1 OldSnapshotHeld=1 Reopened=1 SameRequest=1 AlreadyApplied=1 ReservationsReleased=1 Physical=0"));
		return 1;
	}
	return 0;
}
#else
FRPGInventoryRecoveryE2E::~FRPGInventoryRecoveryE2E() = default;
void FRPGInventoryRecoveryE2E::UnbindResult() {}
int32 FRPGInventoryRecoveryE2E::Tick(APlayerController&) { return -1; }
#endif
