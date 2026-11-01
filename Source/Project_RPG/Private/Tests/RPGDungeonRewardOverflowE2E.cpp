#include "Tests/RPGDungeonRewardE2EGameMode.h"

#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Component/RPGInventoryProjectionComponent.h"
#include "Components/Button.h"
#include "Components/VerticalBox.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "Item/Policy/RPGItemInventoryPolicy.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Player/RPGPlayerState.h"
#include "UI/Inventory/Projection/RPGRewardMailWidget.h"
#include "UI/MVVM/RPGInventoryProjectionViewModel.h"

DEFINE_LOG_CATEGORY_STATIC(LogRPGRewardOverflowE2E, Log, All);

namespace RPGRewardOverflowE2E
{
bool Flag(const TCHAR* Name) { return FParse::Param(FCommandLine::Get(), Name); }
bool GuidParam(const TCHAR* Name, FGuid& Id)
{
	FString Text;
	return FParse::Value(FCommandLine::Get(), Name, Text) && FGuid::Parse(Text, Id) && Id.IsValid();
}
}

bool ARPGDungeonRewardE2EController::ValidateOverflowRewards(const bool bClaimed) const
{
	using namespace RPGRewardOverflowE2E;
	const auto* Projection = FindComponentByClass<URPGInventoryProjectionComponent>();
	FGuid Potion = PotionId, Helm = HelmId;
	if (Flag(TEXT("RPGRewardE2ERecover")) &&
		(!GuidParam(TEXT("RPGRewardE2EPotion="), Potion) || !GuidParam(TEXT("RPGRewardE2EHelm="), Helm))) return false;
	if (!Projection || !Potion.IsValid() || !Helm.IsValid()
		|| Projection->GetProjectedItems().Num() != RPGItemInventoryPolicy::SlotCapacity
		|| Projection->GetMailItems().Num() != (bClaimed ? 0 : 2)) return false;
	const bool bPartial = Flag(TEXT("RPGRewardE2EPartialStackOverflow"));
	FRPGInventoryProjectionEntry PotionEntry, HelmEntry;
	const bool bFound = bClaimed
		? Projection->FindProjectedItem(Potion, PotionEntry) && Projection->FindProjectedItem(Helm, HelmEntry)
		: Projection->FindMailItem(Potion, PotionEntry) && Projection->FindMailItem(Helm, HelmEntry);
	const auto Container = bClaimed ? ERPGItemContainerType::Inventory : ERPGItemContainerType::Mail;
	if (!bFound || PotionEntry.GetDefinitionId().PrimaryAssetName != TEXT("GameItem.Consume.Potion.Red.Large")
		|| HelmEntry.GetDefinitionId().PrimaryAssetName != TEXT("GameItem.Equipment.Helm.Default")
		|| PotionEntry.GetQuantity() != (bPartial ? 1 : 3) || HelmEntry.GetQuantity() != 1
		|| PotionEntry.GetContainerType() != Container || HelmEntry.GetContainerType() != Container
		|| PotionEntry.GetRevision() != (bClaimed ? 2 : 1) || HelmEntry.GetRevision() != (bClaimed ? 2 : 1)
		|| PotionEntry.GetSlotIndex() != (bClaimed && bPartial ? 1 : 0)
		|| HelmEntry.GetSlotIndex() != (bClaimed && bPartial ? 2 : 1)) return false;
	if (bPartial)
	{
		FGuid Stack;
		FRPGInventoryProjectionEntry Existing;
		if (!GuidParam(TEXT("RPGRewardE2EStack="), Stack) || !Projection->FindProjectedItem(Stack, Existing)
			|| Existing.GetSlotIndex() != 0 || Existing.GetQuantity() != 10 || Existing.GetRevision() != 2) return false;
	}
	return true;
}

bool ARPGDungeonRewardE2EController::ClickFullInventoryClaimButton(const FGuid& ItemId)
{
	if (!ClaimViewModel || ClaimViewModel->InventoryItems.Num() != RPGItemInventoryPolicy::SlotCapacity) return false;
	TArray<UUserWidget*> Widgets;
	UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this, Widgets, URPGRewardMailWidget::StaticClass(), true);
	if (Widgets.Num() != 1) return false;
	auto* Rows = Cast<UVerticalBox>(Widgets[0]->GetWidgetFromName(TEXT("MailRows")));
	if (!Rows) return false;
	for (UWidget* Child : Rows->GetAllChildren())
	{
		auto* Row = Cast<URPGRewardMailEntryWidget>(Child);
		if (!Row || Row->GetAttachmentId() != ItemId) continue;
		auto* Button = Cast<UButton>(Row->GetWidgetFromName(TEXT("ClaimButton")));
		if (!Button || Button->GetIsEnabled() || !Button->OnClicked.IsBound() ||
			ClaimViewModel->GetMailClaimPresentation(ItemId).State != ERPGMailClaimState::InventoryFull) return false;
		const FGuid PreviousResult = ClaimViewModel->LastCommandResult.RequestId;
		// Direct dispatch still cannot submit when the normal user-facing button is disabled.
		Button->OnClicked.Broadcast();
		if (ClaimViewModel->IsMailClaimPending(ItemId) || ClaimViewModel->LastCommandResult.RequestId != PreviousResult
			|| !Row->GetFeedbackText().EqualTo(NSLOCTEXT("RPGRewardMail", "ClaimFullReason",
				"Bag full. Free a slot before claiming this reward."))) return false;
		UE_LOG(LogRPGRewardOverflowE2E, Display, TEXT("RPG_REWARD_E2E OVERFLOW_FULL_BAG_BLOCKED Character=%s"),
			*GetPlayerState<ARPGPlayerState>()->GetBackendCharacterId());
		return true;
	}
	return false;
}

void ARPGDungeonRewardE2EController::HandleOverflowCommandResult(FRPGItemCommandClientResult Result)
{
#if !UE_BUILD_SHIPPING
	if (!bAwaitingClaim || bProbeFinished) return;
	const bool bClaim = ClaimStep == 2 || ClaimStep == 4;
	const FName ExpectedOperation = bClaim || ClaimStep == 5 ? FName(TEXT("Item.ClaimMail")) : FName(TEXT("Item.Consume"));
	if (Result.Operation != ExpectedOperation) return;
	if (bClaim && !PendingClaimId.IsValid())
	{
		PendingClaimId = Result.RequestId;
		if (ClaimStep == 2) OriginalClaimId = Result.RequestId;
	}
	if (Result.RequestId != PendingClaimId) return;
	bAwaitingClaim = false;
	if (Result.Result != ExpectedClaimResult)
	{
		bProbeFinished = true;
		UE_LOG(LogRPGRewardOverflowE2E, Error, TEXT("RPG_REWARD_E2E FAIL OverflowCommand Step=%d Expected=%d Actual=%d"),
			ClaimStep, static_cast<int32>(ExpectedClaimResult), static_cast<int32>(Result.Result));
	}
	else UE_LOG(LogRPGRewardOverflowE2E, Display, TEXT("RPG_REWARD_E2E OVERFLOW_COMMAND_PASS Step=%d Operation=%s"),
		ClaimStep, *Result.Operation.ToString());
#endif
}

void ARPGDungeonRewardE2EController::TickInventoryOverflow()
{
#if !UE_BUILD_SHIPPING
	using namespace RPGRewardOverflowE2E;
	const auto* State = GetPlayerState<ARPGPlayerState>();
	const auto* Projection = FindComponentByClass<URPGInventoryProjectionComponent>();
	if (!State || !State->HasAuthenticatedCharacter() || State->GetBackendRosterId().IsEmpty() || !Projection
		|| Projection->GetLoadState() != ERPGInventoryProjectionLoadState::Ready) return;
	const auto* Driver = GetWorld()->GetNetDriver();
	if (!Driver || !Driver->IsUsingIrisReplication())
	{
		bProbeFinished = true;
		UE_LOG(LogRPGRewardOverflowE2E, Error, TEXT("RPG_REWARD_E2E FAIL OverflowIris"));
		return;
	}
	const bool bRecover = Flag(TEXT("RPGRewardE2ERecover"));
	const bool bPartial = Flag(TEXT("RPGRewardE2EPartialStackOverflow"));
	auto Fail = [this](const TCHAR* Reason)
	{
		bProbeFinished = true;
		UE_LOG(LogRPGRewardOverflowE2E, Error, TEXT("RPG_REWARD_E2E FAIL Overflow%s"), Reason);
	};
	auto OpenMail = [this]()
	{
		ShowRewardMail();
		TArray<UUserWidget*> Widgets;
		UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this, Widgets, URPGRewardMailWidget::StaticClass(), true);
		LiveRewardWidget = Widgets.Num() == 1 ? Cast<URPGRewardMailWidget>(Widgets[0]) : nullptr;
		ClaimViewModel = LiveRewardWidget ? LiveRewardWidget->GetInventoryViewModel() : nullptr;
		return ClaimViewModel != nullptr;
	};
	if (!bRecover && !bAwaitingLiveRewardRefresh)
	{
		if (State->GetCurrencyBalance(TEXT("RosterGold")) != 0
			|| Projection->GetProjectedItems().Num() != RPGItemInventoryPolicy::SlotCapacity) return;
		if (!Projection->GetMailItems().IsEmpty() || !OpenMail()) { Fail(TEXT("InitialUI")); return; }
		bAwaitingLiveRewardRefresh = true;
		UE_LOG(LogRPGRewardOverflowE2E, Display, TEXT("RPG_REWARD_E2E CLIENT_PASS Gold=0 Character=%s"), *State->GetBackendCharacterId());
		ServerConfirmInitialRewardState();
		return;
	}
	if (!bRecover)
	{
		const auto Notice = State->GetRewardDeliveryNotice();
		if (!Notice.bApplied || State->GetCurrencyBalance(TEXT("RosterGold")) != 500 || !ClaimViewModel
			|| ClaimViewModel->InventoryItems.Num() != RPGItemInventoryPolicy::SlotCapacity
			|| ClaimViewModel->MailItems.Num() != 2 || ClaimViewModel->RewardDeliveryMessage.IsEmpty()
			|| !LiveRewardWidget->GetDisplayedStatus().ToString().Contains(ClaimViewModel->RewardDeliveryMessage.ToString())) return;
		for (const auto& Item : ClaimViewModel->MailItems)
		{
			if (Item.GetDefinitionId().PrimaryAssetName == TEXT("GameItem.Consume.Potion.Red.Large")) PotionId = Item.GetItemId();
			if (Item.GetDefinitionId().PrimaryAssetName == TEXT("GameItem.Equipment.Helm.Default")) HelmId = Item.GetItemId();
		}
		if (Notice.DungeonSessionId != State->GetBackendDungeonSessionId() || Notice.InventoryQuantity != (bPartial ? 2 : 0)
			|| Notice.MailQuantity != (bPartial ? 2 : 4) || !ValidateOverflowRewards(false)
			|| !ClickFullInventoryClaimButton(PotionId)) { Fail(TEXT("LiveDelivery")); return; }
		UE_LOG(LogRPGRewardOverflowE2E, Display, TEXT("RPG_REWARD_E2E LIVE_REFRESH_CLIENT_PASS Gold=500 Character=%s"), *State->GetBackendCharacterId());
		bProbeFinished = true;
		return;
	}
	if (State->GetCurrencyBalance(TEXT("RosterGold")) != 500) return;
	if (Flag(TEXT("RPGRewardE2EClaimed")))
	{
		if (Projection->GetProjectedItems().Num() != RPGItemInventoryPolicy::SlotCapacity || !Projection->GetMailItems().IsEmpty()) return;
		if (!ValidateOverflowRewards(true)) { Fail(TEXT("ClaimedRestore")); return; }
		UE_LOG(LogRPGRewardOverflowE2E, Display, TEXT("RPG_REWARD_E2E CLAIM_RESTORE_CLIENT_PASS Gold=500 Character=%s"), *State->GetBackendCharacterId());
		bProbeFinished = true;
		return;
	}
	// A fresh active session is required for mutations; the completed server stays read-only.
	if (!Flag(TEXT("RPGRewardE2EClaimMail")) || !GetPawn() || bAwaitingClaim) return;
	FGuid ConsumeA, ConsumeB;
	if (!GuidParam(TEXT("RPGRewardE2EConsumeA="), ConsumeA) || !GuidParam(TEXT("RPGRewardE2EConsumeB="), ConsumeB))
	{ Fail(TEXT("SeedIds")); return; }
	auto Consume = [this, &Fail](FGuid ItemId, int32 Step)
	{
		ClaimStep = Step;
		ExpectedClaimResult = ERPGItemCommandResultCode::Succeeded;
		bAwaitingClaim = true;
		if (!ClaimViewModel->RequestConsume(ItemId, PendingClaimId)) Fail(TEXT("ConsumeSubmission"));
	};
	auto Claim = [this, &Fail](FGuid ItemId, int32 Step)
	{
		ClaimStep = Step;
		ExpectedClaimResult = ERPGItemCommandResultCode::Succeeded;
		bAwaitingClaim = true;
		PendingClaimId.Invalidate();
		if (!ClickMailClaimButton(ItemId)) Fail(TEXT("ClaimButton"));
	};
	FRPGInventoryProjectionEntry Entry;
	switch (ClaimStep)
	{
	case 0:
		if (Projection->GetMailItems().Num() != 2) return;
		if (!ValidateOverflowRewards(false) || !OpenMail()) { Fail(TEXT("UnclaimedRestore")); return; }
		GuidParam(TEXT("RPGRewardE2EPotion="), PotionId);
		GuidParam(TEXT("RPGRewardE2EHelm="), HelmId);
		ClaimViewModel->OnCommandCompleted.AddUniqueDynamic(this, &ThisClass::HandleClaimResult);
		if (!ClickFullInventoryClaimButton(PotionId)) { Fail(TEXT("FullBagUI")); return; }
		Consume(ConsumeA, 1);
		break;
	case 1:
		if (Projection->GetProjectedItems().Num() != RPGItemInventoryPolicy::SlotCapacity - 1 || Projection->FindProjectedItem(ConsumeA, Entry)) return;
		Claim(PotionId, 2);
		break;
	case 2:
		if (Projection->GetMailItems().Num() != 1 || Projection->GetProjectedItems().Num() != RPGItemInventoryPolicy::SlotCapacity
			|| !Projection->FindProjectedItem(PotionId, Entry) || Entry.GetRevision() != 2) return;
		if (!ClickFullInventoryClaimButton(HelmId)) { Fail(TEXT("RefilledBagUI")); return; }
		Consume(ConsumeB, 3);
		break;
	case 3:
		if (Projection->GetProjectedItems().Num() != RPGItemInventoryPolicy::SlotCapacity - 1 || Projection->FindProjectedItem(ConsumeB, Entry)) return;
		Claim(HelmId, 4);
		break;
	case 4:
		if (!Projection->GetMailItems().IsEmpty() || Projection->GetProjectedItems().Num() != RPGItemInventoryPolicy::SlotCapacity) return;
		if (!ValidateOverflowRewards(true)) { Fail(TEXT("ClaimProjection")); return; }
		SendClaim(OriginalClaimId, PotionId, 1, bPartial ? 1 : 0, ERPGItemCommandResultCode::AlreadyApplied);
		break;
	case 5:
		if (!ValidateOverflowRewards(true) || ClaimViewModel->MailItems.Num() != 0
			|| ClaimViewModel->InventoryItems.Num() != RPGItemInventoryPolicy::SlotCapacity) { Fail(TEXT("ReplayProjection")); return; }
		UE_LOG(LogRPGRewardOverflowE2E, Display, TEXT("RPG_REWARD_E2E OVERFLOW_CLAIM_PASS Mail=0 Inventory=80 Gold=500 Character=%s"), *State->GetBackendCharacterId());
		bProbeFinished = true;
		break;
	}
#endif
}
