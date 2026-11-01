#include "Tests/RPGDungeonRewardE2EGameMode.h"

#include "Component/RPGInventoryProjectionComponent.h"
#include "Component/RPGItemCommandComponent.h"
#include "Component/RPGSecurityValidationComponent.h"
#include "Components/SceneComponent.h"
#include "Components/Button.h"
#include "Components/VerticalBox.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "UI/Inventory/Projection/RPGRewardMailWidget.h"
#include "UI/MVVM/RPGInventoryProjectionViewModel.h"
#include "Economy/RPGDungeonRewardDefinition.h"
#include "Engine/GameInstance.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "Item/Persistence/RPGItemRecord.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Player/RPGPlayerState.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RPGDungeonRewardE2EGameMode)

DEFINE_LOG_CATEGORY_STATIC(LogRPGRewardE2E, Log, All);

ARPGDungeonRewardE2EPawn::ARPGDungeonRewardE2EPawn()
{
	bReplicates = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	CreateDefaultSubobject<URPGSecurityValidationComponent>(TEXT("SecurityValidation"));
}

void ARPGDungeonRewardE2EController::SetupInputComponent()
{
	// The headless fixture has no pawn or authored input mapping.
	APlayerController::SetupInputComponent();
}

void ARPGDungeonRewardE2EController::ServerConfirmInitialRewardState_Implementation()
{
#if !UE_BUILD_SHIPPING
	const ARPGPlayerState* State = GetPlayerState<ARPGPlayerState>();
	if (GetNetMode() == NM_DedicatedServer && State && State->HasAuthenticatedCharacter() &&
		FParse::Param(FCommandLine::Get(), TEXT("RPGDungeonRewardE2E")) &&
		FParse::Param(FCommandLine::Get(), TEXT("nosteam")) &&
		!FParse::Param(FCommandLine::Get(), TEXT("RPGRewardE2ERecover")))
	{
		bInitialRewardStateConfirmed = true;
	}
#endif
}

bool ARPGDungeonRewardE2EController::ValidateRestoredRewards(const bool bClaimed) const
{
	const auto* Projection = FindComponentByClass<URPGInventoryProjectionComponent>();
	FString PotionText, HelmText;
	FGuid ExpectedPotionId, ExpectedHelmId;
	FParse::Value(FCommandLine::Get(), TEXT("RPGRewardE2EPotion="), PotionText);
	FParse::Value(FCommandLine::Get(), TEXT("RPGRewardE2EHelm="), HelmText);
	if (!Projection || !FGuid::Parse(PotionText, ExpectedPotionId) ||
		!FGuid::Parse(HelmText, ExpectedHelmId)) return false;
	FRPGInventoryProjectionEntry Potion, Helm;
	const bool bInventory = bClaimed || !FParse::Param(FCommandLine::Get(), TEXT("RPGRewardE2ELegacyMail"));
	const bool bFound = bInventory
		? Projection->FindProjectedItem(ExpectedPotionId, Potion) && Projection->FindProjectedItem(ExpectedHelmId, Helm)
		: Projection->FindMailItem(ExpectedPotionId, Potion) && Projection->FindMailItem(ExpectedHelmId, Helm);
	const ERPGItemContainerType ExpectedContainer = bInventory
		? ERPGItemContainerType::Inventory : ERPGItemContainerType::Mail;
	return bFound &&
		Projection->GetProjectedItems().Num() == (bInventory ? 2 : 0) &&
		Projection->GetMailItems().Num() == (bInventory ? 0 : 2) &&
		Potion.GetDefinitionId().PrimaryAssetName == TEXT("GameItem.Consume.Potion.Red.Large") &&
		Helm.GetDefinitionId().PrimaryAssetName == TEXT("GameItem.Equipment.Helm.Default") &&
		Potion.GetContainerType() == ExpectedContainer && Helm.GetContainerType() == ExpectedContainer &&
		Potion.GetSlotIndex() == 0 && Helm.GetSlotIndex() == 1 &&
		Potion.GetQuantity() == 3 && Helm.GetQuantity() == 1 &&
		Potion.GetRevision() == (bClaimed ? 2 : 1) && Helm.GetRevision() == (bClaimed ? 2 : 1);
}

void ARPGDungeonRewardE2EGameMode::EndPlay(
	const EEndPlayReason::Type EndPlayReason)
{
#if !UE_BUILD_SHIPPING
	if (bProbeEnabled)
	{
		if (UGameInstance* GameInstance = GetGameInstance())
		{
			if (UHttpWebManager* Web =
				GameInstance->GetSubsystem<UHttpWebManager>())
			{
				Web->OnDungeonSessionUpdated.RemoveDynamic(
					this,
					&ThisClass::HandleProbeSession);
			}
		}
	}
#endif
	Super::EndPlay(EndPlayReason);
}

void ARPGDungeonRewardE2EController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
#if !UE_BUILD_SHIPPING
	if (bProbeFinished || GetNetMode() != NM_Client || !IsLocalController()
		|| !FParse::Param(FCommandLine::Get(), TEXT("RPGDungeonRewardE2E")))
	{
		return;
	}
	const ARPGPlayerState* State = GetPlayerState<ARPGPlayerState>();
	const auto* Projection = FindComponentByClass<URPGInventoryProjectionComponent>();
	if (Projection && Projection->GetLoadState() == ERPGInventoryProjectionLoadState::Failed)
	{
		bProbeFinished = true;
		UE_LOG(LogRPGRewardE2E, Error, TEXT("RPG_REWARD_E2E FAIL ClientItemLoadFailed"));
		return;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("RPGRewardE2EOverflow")))
	{
		TickInventoryOverflow();
		return;
	}
	const int64 ExpectedGold = FParse::Param(FCommandLine::Get(), TEXT("RPGRewardE2ERecover")) ? 500 : 0;
	if (bAwaitingLiveRewardRefresh)
	{
		if (!State || !Projection || !LiveRewardWidget) return;
		const FRPGDungeonRewardDeliveryNotice Notice = State->GetRewardDeliveryNotice();
		const auto* ViewModel = LiveRewardWidget->GetInventoryViewModel();
		const bool bInventory = !FParse::Param(FCommandLine::Get(), TEXT("RPGRewardE2ELegacyMail"));
		if (!Notice.bApplied || Notice.DungeonSessionId != State->GetBackendDungeonSessionId()
			|| State->GetCurrencyBalance(TEXT("RosterGold")) != 500 || !ViewModel
			|| ViewModel->InventoryItems.Num() != (bInventory ? 2 : 0)
			|| ViewModel->MailItems.Num() != (bInventory ? 0 : 2)
			|| ViewModel->RewardDeliveryMessage.IsEmpty()
			|| !LiveRewardWidget->GetDisplayedStatus().ToString().Contains(ViewModel->RewardDeliveryMessage.ToString())) return;
		const auto& Entries = bInventory ? ViewModel->InventoryItems : ViewModel->MailItems;
		const auto* Potion = Entries.FindByPredicate([](const FRPGInventoryProjectionEntry& Entry)
			{ return Entry.GetDefinitionId().PrimaryAssetName == TEXT("GameItem.Consume.Potion.Red.Large"); });
		const auto* Helm = Entries.FindByPredicate([](const FRPGInventoryProjectionEntry& Entry)
			{ return Entry.GetDefinitionId().PrimaryAssetName == TEXT("GameItem.Equipment.Helm.Default"); });
		if (!Potion || !Helm || Potion->GetQuantity() != 3 || Helm->GetQuantity() != 1
			|| Potion->GetRevision() != 1 || Helm->GetRevision() != 1
			|| Notice.InventoryQuantity != (bInventory ? 4 : 0) || Notice.MailQuantity != (bInventory ? 0 : 4)) return;
		UE_LOG(LogRPGRewardE2E, Display, TEXT("RPG_REWARD_E2E LIVE_REFRESH_CLIENT_PASS Gold=500 Character=%s"),
			*State->GetBackendCharacterId());
		bProbeFinished = true;
		return;
	}
	if (!State || !State->HasAuthenticatedCharacter() || State->GetBackendRosterId().IsEmpty()
		|| State->GetCurrencyBalance(TEXT("RosterGold")) != ExpectedGold || !Projection
		|| Projection->GetLoadState() != ERPGInventoryProjectionLoadState::Ready)
	{
		return;
	}
	const UNetDriver* Driver = GetWorld()->GetNetDriver();
	if (!Driver || !Driver->IsUsingIrisReplication())
	{
		UE_LOG(LogRPGRewardE2E, Error, TEXT("RPG_REWARD_E2E FAIL ClientIrisNotActive"));
		bProbeFinished = true;
		return;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("RPGRewardE2EClaimed")))
	{
		if (Projection->GetProjectedItems().Num() != 2 || !Projection->GetMailItems().IsEmpty()) return;
		if (!ValidateRestoredRewards(true))
		{
			UE_LOG(LogRPGRewardE2E, Error, TEXT("RPG_REWARD_E2E FAIL ClaimedClientRestore"));
			bProbeFinished = true;
			return;
		}
		UE_LOG(LogRPGRewardE2E, Display, TEXT("RPG_REWARD_E2E CLAIM_RESTORE_CLIENT_PASS Gold=500 Character=%s"),
			*State->GetBackendCharacterId());
		bProbeFinished = true;
		return;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("RPGRewardE2EClaimMail")))
	{
		const bool bConcurrentClaims = FParse::Param(
			FCommandLine::Get(), TEXT("RPGRewardE2EConcurrentClaims"));
		if (!GetPawn() || (!bConcurrentClaims && bAwaitingClaim)) return;
		const auto* Commands = FindComponentByClass<URPGItemCommandComponent>();
		if (!Commands) return;
		if (ClaimStep == 0)
		{
			if (Projection->GetMailItems().Num() != 2) return;
			if (!ValidateRestoredRewards(false))
			{
				UE_LOG(LogRPGRewardE2E, Error, TEXT("RPG_REWARD_E2E FAIL ClientRewardOwnerProjection"));
				bProbeFinished = true;
				return;
			}
			for (const auto& Entry : Projection->GetMailItems())
			{
				if (Entry.GetDefinitionId().PrimaryAssetName == TEXT("GameItem.Consume.Potion.Red.Large")) PotionId = Entry.GetItemId();
				if (Entry.GetDefinitionId().PrimaryAssetName == TEXT("GameItem.Equipment.Helm.Default")) HelmId = Entry.GetItemId();
			}
			ShowRewardMail(); // Build and bind the actual native mailbox even in NullRHI.
			TArray<UUserWidget*> MailWidgets;
			UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this, MailWidgets, URPGRewardMailWidget::StaticClass(), true);
			if (!PotionId.IsValid() || !HelmId.IsValid() || MailWidgets.Num() != 1)
			{
				UE_LOG(LogRPGRewardE2E, Error, TEXT("RPG_REWARD_E2E FAIL MailUIOrIdentity"));
				bProbeFinished = true;
				return;
			}
			ClaimViewModel = Cast<URPGRewardMailWidget>(MailWidgets[0])->GetInventoryViewModel();
			if (!ClaimViewModel || ClaimViewModel->MailItems.Num() != 2)
			{
				UE_LOG(LogRPGRewardE2E, Error, TEXT("RPG_REWARD_E2E FAIL MailUIBinding"));
				bProbeFinished = true;
				return;
			}
			ClaimViewModel->OnCommandCompleted.AddUniqueDynamic(this, &ThisClass::HandleClaimResult);
			bAwaitingClaim = !bConcurrentClaims;
			++ClaimStep;
			PendingClaimId.Invalidate();
			if (!ClickMailClaimButton(PotionId))
			{
				UE_LOG(LogRPGRewardE2E, Error, TEXT("RPG_REWARD_E2E FAIL PotionClaimButton"));
				bProbeFinished = true;
			}
			if (bConcurrentClaims && !bProbeFinished && !ClickMailClaimButton(HelmId))
			{
				UE_LOG(LogRPGRewardE2E, Error, TEXT("RPG_REWARD_E2E FAIL ConcurrentHelmClaimButton"));
				bProbeFinished = true;
			}
			if (bConcurrentClaims && !bProbeFinished)
			{
				UE_LOG(LogRPGRewardE2E, Display,
					TEXT("RPG_REWARD_E2E CONCURRENT_BUTTONS_SUBMITTED Count=2"));
			}
			return;
		}
		FRPGInventoryProjectionEntry Entry;
		if (bConcurrentClaims)
		{
			if (ConcurrentClaimSuccessCount != 2 ||
				Projection->GetMailItems().Num() != 0 ||
				Projection->GetProjectedItems().Num() != 2) return;
			FRPGInventoryProjectionEntry Potion, Helm;
			if (!Projection->FindProjectedItem(PotionId, Potion) ||
				!Projection->FindProjectedItem(HelmId, Helm) ||
				Potion.GetContainerType() != ERPGItemContainerType::Inventory ||
				Helm.GetContainerType() != ERPGItemContainerType::Inventory ||
				Potion.GetSlotIndex() != 0 || Helm.GetSlotIndex() != 1 ||
				Potion.GetQuantity() != 3 || Helm.GetQuantity() != 1 ||
				Potion.GetRevision() != 2 || Helm.GetRevision() != 2)
			{
				UE_LOG(LogRPGRewardE2E, Error, TEXT("RPG_REWARD_E2E FAIL ConcurrentClaimProjection"));
			}
			else
			{
				UE_LOG(LogRPGRewardE2E, Display,
					TEXT("RPG_REWARD_E2E CONCURRENT_CLAIM_PASS Mail=0 Inventory=2 Gold=500 Character=%s"),
					*State->GetBackendCharacterId());
			}
			bProbeFinished = true;
			return;
		}
		switch (ClaimStep)
		{
		case 1:
			if (!Projection->FindProjectedItem(PotionId, Entry) || Entry.GetRevision() != 2 ||
				Projection->GetMailItems().Num() != 1) return;
			SendClaim(OriginalClaimId, PotionId, 1, 0, ERPGItemCommandResultCode::AlreadyApplied);
			break;
		case 2: SendClaim(FGuid::NewGuid(), HelmId, 1, 0, ERPGItemCommandResultCode::Conflict); break;
		case 3: SendClaim(FGuid::NewGuid(), HelmId, 99, 1, ERPGItemCommandResultCode::Conflict); break;
		case 4: SendClaim(FGuid::NewGuid(), HelmId, 1, 80, ERPGItemCommandResultCode::InvalidRequest); break;
		case 5: SendClaim(FGuid::NewGuid(), PotionId, 2, 2, ERPGItemCommandResultCode::InvalidRequest); break;
		case 6:
			bAwaitingClaim = true;
			ExpectedClaimResult = ERPGItemCommandResultCode::Succeeded;
			++ClaimStep;
			PendingClaimId.Invalidate();
			if (!ClickMailClaimButton(HelmId))
			{
				UE_LOG(LogRPGRewardE2E, Error, TEXT("RPG_REWARD_E2E FAIL HelmClaimButton"));
				bProbeFinished = true;
			}
			break;
		case 7:
			if (!Projection->GetMailItems().IsEmpty() || Projection->GetProjectedItems().Num() != 2 ||
				!Projection->FindProjectedItem(HelmId, Entry) || Entry.GetRevision() != 2) return;
			UE_LOG(LogRPGRewardE2E, Display, TEXT("RPG_REWARD_E2E CLAIM_PASS Mail=0 Inventory=2 Gold=500 Character=%s"),
				*State->GetBackendCharacterId());
			bProbeFinished = true;
			break;
		}
		return;
	}
	const bool bRecover = FParse::Param(FCommandLine::Get(), TEXT("RPGRewardE2ERecover"));
	const bool bDirectInventory = bRecover && !FParse::Param(FCommandLine::Get(), TEXT("RPGRewardE2ELegacyMail"));
	if (bRecover && (bDirectInventory ? Projection->GetProjectedItems().Num() : Projection->GetMailItems().Num()) != 2) return;
	if (FParse::Param(FCommandLine::Get(), TEXT("RPGRewardE2ERecover")) &&
		!ValidateRestoredRewards(false))
	{
		UE_LOG(LogRPGRewardE2E, Error, TEXT("RPG_REWARD_E2E FAIL ClientRewardOwnerProjection"));
		bProbeFinished = true;
		return;
	}
	// Legacy unclaimed mail must not appear as usable inventory.
	if (Projection->GetProjectedItems().Num() != (bDirectInventory ? 2 : 0))
	{
		UE_LOG(LogRPGRewardE2E, Error, TEXT("RPG_REWARD_E2E FAIL MailLeakedIntoInventory"));
	}
	else
	{
		UE_LOG(LogRPGRewardE2E, Display, TEXT("RPG_REWARD_E2E CLIENT_PASS Gold=%lld Character=%s"),
			ExpectedGold, *State->GetBackendCharacterId());
		if (ExpectedGold == 0)
		{
			const bool bMail = FParse::Param(FCommandLine::Get(), TEXT("RPGRewardE2ELegacyMail"));
			if (bMail) ShowRewardMail(); else ShowInventoryProjection();
			TArray<UUserWidget*> Widgets;
			UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this, Widgets,
				URPGInventoryProjectionWidget::StaticClass(), true);
			LiveRewardWidget = Widgets.Num() == 1 ? Cast<URPGInventoryProjectionWidget>(Widgets[0]) : nullptr;
			if (!LiveRewardWidget || !LiveRewardWidget->GetInventoryViewModel())
			{
				UE_LOG(LogRPGRewardE2E, Error, TEXT("RPG_REWARD_E2E FAIL LiveRewardWidget"));
				bProbeFinished = true;
				return;
			}
			bAwaitingLiveRewardRefresh = true;
			ServerConfirmInitialRewardState();
			return;
		}
	}
	bProbeFinished = true;
#endif
}

bool ARPGDungeonRewardE2EController::ClickMailClaimButton(const FGuid& ItemId)
{
	TArray<UUserWidget*> MailWidgets;
	UWidgetBlueprintLibrary::GetAllWidgetsOfClass(
		this, MailWidgets, URPGRewardMailWidget::StaticClass(), true);
	if (MailWidgets.Num() != 1 || !ClaimViewModel)
	{
		return false;
	}
	URPGRewardMailWidget* MailWidget = Cast<URPGRewardMailWidget>(MailWidgets[0]);
	UVerticalBox* Rows = MailWidget
		? Cast<UVerticalBox>(MailWidget->GetWidgetFromName(TEXT("MailRows"))) : nullptr;
	if (!Rows)
	{
		return false;
	}
	for (UWidget* Child : Rows->GetAllChildren())
	{
		URPGRewardMailEntryWidget* Row = Cast<URPGRewardMailEntryWidget>(Child);
		if (!Row || Row->GetAttachmentId() != ItemId)
		{
			continue;
		}
		UButton* Button = Cast<UButton>(Row->GetWidgetFromName(TEXT("ClaimButton")));
		if (!Button || !Button->GetIsEnabled() || !Button->OnClicked.IsBound())
		{
			return false;
		}
		Button->OnClicked.Broadcast();
		const bool bPending = ClaimViewModel->IsMailClaimPending(ItemId);
		if (!bPending || Button->GetIsEnabled())
		{
			return false;
		}
		// A field update replaces every row. The replacement must stay disabled.
		MailWidget->RefreshFromViewModel();
		for (UWidget* RefreshedChild : Rows->GetAllChildren())
		{
			URPGRewardMailEntryWidget* RefreshedRow =
				Cast<URPGRewardMailEntryWidget>(RefreshedChild);
			if (RefreshedRow && RefreshedRow->GetAttachmentId() == ItemId)
			{
				UButton* RefreshedButton = Cast<UButton>(
					RefreshedRow->GetWidgetFromName(TEXT("ClaimButton")));
				if (!RefreshedButton || RefreshedButton->GetIsEnabled())
				{
					return false;
				}
				FGuid DuplicateRequestId;
				if (ClaimViewModel->RequestClaimMailToFirstFreeSlot(
					ItemId, DuplicateRequestId) || DuplicateRequestId.IsValid())
				{
					return false;
				}
				UE_LOG(LogRPGRewardE2E, Display,
					TEXT("RPG_REWARD_E2E MAIL_BUTTON_CLICKED Item=%s RedrawBlocked=true"),
					*ItemId.ToString(EGuidFormats::DigitsWithHyphens));
				return true;
			}
		}
		return false;
	}
	return false;
}

void ARPGDungeonRewardE2EController::SendClaim(FGuid RequestId, FGuid ItemId,
	int64 Revision, int32 Slot, ERPGItemCommandResultCode Expected)
{
	PendingClaimId = RequestId;
	ExpectedClaimResult = Expected;
	bAwaitingClaim = true;
	++ClaimStep;
	FindComponentByClass<URPGItemCommandComponent>()->ServerClaimMailItem(RequestId, ItemId, Revision, Slot);
}

void ARPGDungeonRewardE2EController::HandleClaimResult(FRPGItemCommandClientResult Result)
{
#if !UE_BUILD_SHIPPING
	if (FParse::Param(FCommandLine::Get(), TEXT("RPGRewardE2EOverflow")))
	{
		HandleOverflowCommandResult(Result);
		return;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("RPGRewardE2EConcurrentClaims")))
	{
		if (bProbeFinished || ClaimStep != 1 ||
			Result.Operation != FName(TEXT("Item.ClaimMail")) ||
			!Result.RequestId.IsValid() ||
			ConcurrentCompletedClaimIds.Contains(Result.RequestId)) return;
		ConcurrentCompletedClaimIds.Add(Result.RequestId);
		if (Result.Result != ERPGItemCommandResultCode::Succeeded)
		{
			UE_LOG(LogRPGRewardE2E, Error,
				TEXT("RPG_REWARD_E2E FAIL ConcurrentClaimResult=%d"),
				static_cast<int32>(Result.Result));
			bProbeFinished = true;
			return;
		}
		++ConcurrentClaimSuccessCount;
		UE_LOG(LogRPGRewardE2E, Display,
			TEXT("RPG_REWARD_E2E CONCURRENT_CLAIM_STEP_PASS Count=%d"),
			ConcurrentClaimSuccessCount);
		return;
	}
	if (bAwaitingClaim && !PendingClaimId.IsValid() &&
		(ClaimStep == 1 || ClaimStep == 7) &&
		Result.Operation == FName(TEXT("Item.ClaimMail")) &&
		Result.RequestId.IsValid())
	{
		PendingClaimId = Result.RequestId;
		if (ClaimStep == 1)
		{
			OriginalClaimId = Result.RequestId;
		}
	}
	if (!bAwaitingClaim || Result.RequestId != PendingClaimId) return;
	bAwaitingClaim = false;
	if (Result.Result != ExpectedClaimResult)
	{
		bProbeFinished = true;
		UE_LOG(LogRPGRewardE2E, Error, TEXT("RPG_REWARD_E2E FAIL ClaimStep=%d Expected=%d Actual=%d"),
			ClaimStep, static_cast<int32>(ExpectedClaimResult), static_cast<int32>(Result.Result));
	}
	else
	{
		UE_LOG(LogRPGRewardE2E, Display, TEXT("RPG_REWARD_E2E CLAIM_STEP_PASS Step=%d Result=%d"),
			ClaimStep, static_cast<int32>(Result.Result));
	}
#endif
}

ARPGDungeonRewardE2EGameMode::ARPGDungeonRewardE2EGameMode()
{
	PlayerControllerClass = ARPGDungeonRewardE2EController::StaticClass();
	DefaultPawnClass = nullptr;
	bStartPlayersAsSpectators = true;
}

void ARPGDungeonRewardE2EGameMode::BeginPlay()
{
#if !UE_BUILD_SHIPPING
	bProbeEnabled = IsRunningDedicatedServer()
		&& FParse::Param(FCommandLine::Get(), TEXT("RPGDungeonRewardE2E"))
		&& FParse::Param(FCommandLine::Get(), TEXT("nosteam"));
	if (bProbeEnabled)
	{
		FParse::Value(FCommandLine::Get(), TEXT("RPGRewardE2EPlayers="), ExpectedPlayers);
		if (ExpectedPlayers < 1 || ExpectedPlayers > 4)
		{
			UE_LOG(LogRPGRewardE2E, Error, TEXT("RPG_REWARD_E2E FAIL InvalidPartySize"));
			bProbeFinished = true;
		}
		if (FParse::Param(FCommandLine::Get(), TEXT("RPGRewardE2ERecover")))
		{
			FString RewardsText;
			FParse::Value(FCommandLine::Get(), TEXT("RPGRewardE2ERewards="), RewardsText, false);
			TArray<FString> Members;
			RewardsText.ParseIntoArray(Members, TEXT(";"), true);
			TSet<FGuid> RewardIds;
			for (const FString& Member : Members)
			{
				TArray<FString> Ids;
				Member.ParseIntoArray(Ids, TEXT(","), false);
				FGuid CharacterId, PotionId, HelmId;
				if (Ids.Num() != 3 || !FGuid::Parse(Ids[0], CharacterId) || !CharacterId.IsValid() ||
					!FGuid::Parse(Ids[1], PotionId) || !PotionId.IsValid() ||
					!FGuid::Parse(Ids[2], HelmId) || !HelmId.IsValid() || PotionId == HelmId ||
					ExpectedRewardItems.Contains(CharacterId) || RewardIds.Contains(PotionId) || RewardIds.Contains(HelmId))
				{
					UE_LOG(LogRPGRewardE2E, Error, TEXT("RPG_REWARD_E2E FAIL InvalidRewardExpectations"));
					bProbeFinished = true;
					break;
				}
				ExpectedRewardItems.Add(CharacterId, TPair<FGuid, FGuid>(PotionId, HelmId));
				RewardIds.Add(PotionId);
				RewardIds.Add(HelmId);
			}
			if (ExpectedRewardItems.Num() != ExpectedPlayers)
			{
				UE_LOG(LogRPGRewardE2E, Error, TEXT("RPG_REWARD_E2E FAIL MissingRewardExpectations"));
				bProbeFinished = true;
			}
		}
		// Only this explicit local fixture accepts a dev-ticket identity without Steam.
		bRequireSteamIdentityMatch = false;
		bRequireBackendJoinTicket = true;
		bGiveReward = true;
		DungeonClearRewardsByDifficulty.Add(ERPGGameDifficulty::Hard,
			LoadObject<URPGDungeonRewardDefinition>(nullptr,
				TEXT("/Game/Blueprints/GameData/DungeonReward/DA_DungeonReward_Hard.DA_DungeonReward_Hard")));
		if (auto* Web = GetGameInstance()->GetSubsystem<UHttpWebManager>())
		{
			Web->OnDungeonSessionUpdated.AddDynamic(this, &ThisClass::HandleProbeSession);
		}
	}
#endif
	Super::BeginPlay();
#if !UE_BUILD_SHIPPING
	if (bProbeEnabled)
	{
		CurrentGameDifficulty = ERPGGameDifficulty::Hard;
		UE_LOG(LogRPGRewardE2E, Display, TEXT("RPG_REWARD_E2E DEDICATED_READY"));
	}
#endif
}

void ARPGDungeonRewardE2EGameMode::HandleProbeSession(const FRPGDungeonSession& Session, bool bSuccess)
{
#if !UE_BUILD_SHIPPING
	if (bProbeEnabled && bSuccess && Session.State == TEXT("InProgress"))
	{
		bSessionStarted = Session.Difficulty == TEXT("Hard");
		if (!bSessionStarted)
		{
			UE_LOG(LogRPGRewardE2E, Error, TEXT("RPG_REWARD_E2E FAIL WrongSessionDifficulty"));
		}
	}
#endif
}

void ARPGDungeonRewardE2EGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
#if !UE_BUILD_SHIPPING
	if (!bProbeEnabled || !bSessionStarted || bProbeFinished)
	{
		return;
	}
	if (!bServerTickReadyReported)
	{
		const UNetDriver* ReadyDriver = GetWorld()->GetNetDriver();
		if (!ReadyDriver || !ReadyDriver->IsUsingIrisReplication()) return;
		bServerTickReadyReported = true;
		// BeginPlay can precede blocking editor startup work. Issue short-lived
		// tickets only after the assigned server is actually ticking with Iris.
		UE_LOG(LogRPGRewardE2E, Display, TEXT("RPG_REWARD_E2E SERVER_TICK_READY Iris=true"));
	}
	const bool bRecover = FParse::Param(FCommandLine::Get(), TEXT("RPGRewardE2ERecover"));
	TArray<APlayerController*> ReadyControllers;
	TSet<FString> ReadyCharacterIds;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* Controller = It->Get();
		const ARPGPlayerState* State = Controller ? Controller->GetPlayerState<ARPGPlayerState>() : nullptr;
		const auto* Projection = Controller ? Controller->FindComponentByClass<URPGInventoryProjectionComponent>() : nullptr;
		if (Projection && Projection->GetLoadState() == ERPGInventoryProjectionLoadState::Failed)
		{
			bProbeFinished = true;
			UE_LOG(LogRPGRewardE2E, Error, TEXT("RPG_REWARD_E2E FAIL ServerItemLoadFailed"));
			return;
		}
		if (!State || !State->HasAuthenticatedCharacter() || State->GetBackendRosterId().IsEmpty()
			|| !Projection || Projection->GetLoadState() != ERPGInventoryProjectionLoadState::Ready ||
			State->GetCurrencyBalance(TEXT("RosterGold")) != (bRecover ? 500 : 0))
		{
			return;
		}
		if (!bRecover)
		{
			const auto* ProbeController = Cast<ARPGDungeonRewardE2EController>(Controller);
			if (!ProbeController || !ProbeController->HasConfirmedInitialRewardState()) return;
		}
		if (ReadyCharacterIds.Contains(State->GetBackendCharacterId()))
		{
			bProbeFinished = true;
			UE_LOG(LogRPGRewardE2E, Error, TEXT("RPG_REWARD_E2E FAIL DuplicatePartyCharacter"));
			return;
		}
		ReadyCharacterIds.Add(State->GetBackendCharacterId());
		ReadyControllers.Add(Controller);
	}
	if (ReadyControllers.Num() < ExpectedPlayers) return;
	bProbeFinished = true;
	const UNetDriver* Driver = GetWorld()->GetNetDriver();
	if (ReadyControllers.Num() != ExpectedPlayers || !Driver || !Driver->IsUsingIrisReplication())
	{
		UE_LOG(LogRPGRewardE2E, Error, TEXT("RPG_REWARD_E2E FAIL PartyCountOrIris"));
		return;
	}
	if (!bRecover)
	{
		if (FParse::Param(FCommandLine::Get(), TEXT("RPGRewardE2ELegacyMail")))
		{
			// Explicit compatibility fixture for the existing mailbox UI/claim tests.
			// Production and the default E2E retain definition-derived stack limits.
			FString Version, Error;
			TArray<FRPGCurrencyChange> Currency;
			TArray<FRPGDungeonItemReward> Items;
			const URPGDungeonRewardDefinition* Definition = DungeonClearRewardsByDifficulty.FindRef(ERPGGameDifficulty::Hard);
			if (!Definition || !Definition->BuildSettlement(Version, Currency, Items, Error))
			{
				UE_LOG(LogRPGRewardE2E, Error, TEXT("RPG_REWARD_E2E FAIL LegacyRewardBuild"));
				return;
			}
			for (FRPGDungeonItemReward& Item : Items) Item.MaxStackSize = 0;
			ReportDungeonClearedWithRewards(Version, Currency, Items);
			ReportDungeonClearedWithRewards(Version, Currency, Items);
		}
		else
		{
			ReportConfiguredDungeonClear();
			ReportConfiguredDungeonClear(); // A duplicate clear must not double-grant.
		}
		UE_LOG(LogRPGRewardE2E, Display,
			TEXT("RPG_REWARD_E2E CLEAR_SUBMITTED Twice=true Iris=true Players=%d"), ExpectedPlayers);
		return;
	}
	const bool bClaimed = FParse::Param(FCommandLine::Get(), TEXT("RPGRewardE2EClaimed"));
	const bool bOverflow = FParse::Param(FCommandLine::Get(), TEXT("RPGRewardE2EOverflow"));
	const bool bPartialStack = FParse::Param(FCommandLine::Get(), TEXT("RPGRewardE2EPartialStackOverflow"));
	const bool bInventory = bClaimed || (!bOverflow && !FParse::Param(FCommandLine::Get(), TEXT("RPGRewardE2ELegacyMail")));
	for (APlayerController* Controller : ReadyControllers)
	{
		const auto* State = Controller->GetPlayerState<ARPGPlayerState>();
		const auto* Projection = Controller->FindComponentByClass<URPGInventoryProjectionComponent>();
		const IRPGItemRecordSource* Records = Projection->GetAuthoritativeRecordSource();
		FGuid CharacterId;
		const TPair<FGuid, FGuid>* ExpectedIds = FGuid::Parse(State->GetBackendCharacterId(), CharacterId)
			? ExpectedRewardItems.Find(CharacterId) : nullptr;
		FRPGItemOwnerRef RewardOwner;
		RewardOwner.Type = ERPGItemOwnerType::Character;
		RewardOwner.OwnerId = State->GetBackendCharacterId();
		FRPGItemRecord Potion, Helm;
		const int64 ExpectedRevision = bClaimed ? 2 : 1;
		const ERPGItemContainerType ExpectedContainer = bInventory
			? ERPGItemContainerType::Inventory : ERPGItemContainerType::Mail;
		const bool bRestored = Records && ExpectedIds
			&& Records->Find(ExpectedIds->Key, Potion) && Records->Find(ExpectedIds->Value, Helm)
			&& Potion.GetOwner() == RewardOwner && Helm.GetOwner() == RewardOwner
			&& Potion.GetDefinitionId() == FPrimaryAssetId(
				TEXT("RPGItemDefinition"),
				TEXT("GameItem.Consume.Potion.Red.Large"))
			&& Helm.GetDefinitionId() == FPrimaryAssetId(
				TEXT("RPGItemDefinition"),
				TEXT("GameItem.Equipment.Helm.Default"))
			&& Potion.GetDefinitionVersion() == 1
			&& Helm.GetDefinitionVersion() == 1
			&& Potion.IsActive() && Helm.IsActive()
			&& Potion.GetRevision() == ExpectedRevision && Helm.GetRevision() == ExpectedRevision
			&& Potion.GetMetadata().CreationSource == TEXT("DungeonClear.pve_hard_v1")
			&& Helm.GetMetadata().CreationSource == TEXT("DungeonClear.pve_hard_v1")
			&& Potion.GetLocation().ContainerType == ExpectedContainer
			&& Helm.GetLocation().ContainerType == ExpectedContainer
			&& Potion.GetLocation().SlotIndex == (bOverflow && bClaimed && bPartialStack ? 1 : 0)
			&& Helm.GetLocation().SlotIndex == (bOverflow && bClaimed && bPartialStack ? 2 : 1)
			&& Potion.GetQuantity() == (bOverflow && bPartialStack ? 1 : 3) && Helm.GetQuantity() == 1
			&& Projection->GetProjectedItems().Num() == (bOverflow ? 80 : (bInventory ? 2 : 0))
			&& Projection->GetMailItems().Num() == (bInventory ? 0 : 2)
			&& State->GetCurrencyBalance(TEXT("RosterGold")) == 500;
		if (bRestored)
		{
			UE_LOG(LogRPGRewardE2E, Display, TEXT("RPG_REWARD_E2E %s SameItemIds=true Gold=500 Potion=%d Helm=%d Iris=true Character=%s"),
				bClaimed ? TEXT("CLAIM_RESTORE_PASS") : TEXT("RESTORE_PASS"),
				Potion.GetQuantity(), Helm.GetQuantity(), *State->GetBackendCharacterId());
		}
		else
		{
			UE_LOG(LogRPGRewardE2E, Error, TEXT("RPG_REWARD_E2E FAIL RestoreMismatch"));
			return;
		}
	}
	UE_LOG(LogRPGRewardE2E, Display,
		TEXT("RPG_REWARD_E2E PARTY_RESTORE_PASS Players=%d Claimed=%s Iris=true"),
		ExpectedPlayers, bClaimed ? TEXT("true") : TEXT("false"));
	if (FParse::Param(FCommandLine::Get(), TEXT("RPGRewardE2EClaimMail")))
	{
		for (APlayerController* Controller : ReadyControllers)
		{
			Controller->Possess(GetWorld()->SpawnActor<ARPGDungeonRewardE2EPawn>());
		}
	}
#endif
}
