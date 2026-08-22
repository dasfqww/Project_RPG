// Fill out your copyright notice in the Description page of Project Settings.


#include "GameMode/RPGGameModeBase.h"
#include "Item/RPGItemBase.h"
#include "Character/RPGPlayer.h"
#include "Component/RPGInventoryComponent.h"
#include "Component/RPGInventoryProjectionComponent.h"
#include "Controller/RPGPlayerController.h"
#include "DataTable/DropItemData.h"
#include "Economy/RPGDungeonRewardDefinition.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "UI/RPGContentClearPanel.h"
#include "GameInstance/RPGGameInstance.h"
#include "Manager/HttpWebManager.h"
#include "Player/RPGPlayerState.h"

#include "RPGDebugHelper.h"

namespace
{
	bool IsRewardIdentifier(const FString& Value)
	{
		if (Value.IsEmpty() || Value.Len() > 64)
		{
			return false;
		}
		for (const TCHAR Character : Value)
		{
			const bool bAsciiAlphaNumeric =
				(Character >= TEXT('A') && Character <= TEXT('Z'))
				|| (Character >= TEXT('a') && Character <= TEXT('z'))
				|| (Character >= TEXT('0') && Character <= TEXT('9'));
			if (!bAsciiAlphaNumeric
				&& Character != TEXT('.')
				&& Character != TEXT('_')
				&& Character != TEXT('-'))
			{
				return false;
			}
		}
		return true;
	}
}

ARPGGameModeBase::ARPGGameModeBase()
{
	PlayerStateClass = ARPGPlayerState::StaticClass();
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
}

void ARPGGameModeBase::BeginPlay()
{
	Super::BeginPlay();

	if (IsRunningDedicatedServer())
	{
		if (UGameInstance* GameInstance = GetGameInstance())
		{
			if (UHttpWebManager* WebManager =
				GameInstance->GetSubsystem<UHttpWebManager>())
			{
				if (bRequireBackendJoinTicket)
				{
					WebManager->OnJoinTicketConsumed.RemoveDynamic(
						this,
						&ThisClass::HandleJoinTicketConsumed);
					WebManager->OnJoinTicketConsumed.AddDynamic(
						this,
						&ThisClass::HandleJoinTicketConsumed);
				}

				if (WebManager->IsConfiguredForDungeonServer())
				{
					WebManager->OnDungeonSessionUpdated.RemoveDynamic(
						this,
						&ThisClass::HandleDungeonSessionUpdated);
					WebManager->OnDungeonSessionUpdated.AddDynamic(
						this,
						&ThisClass::HandleDungeonSessionUpdated);
					WebManager->OnDungeonRewardSettlementRequested.RemoveDynamic(
						this,
						&ThisClass::HandleDungeonRewardSettlementRequested);
					WebManager->OnDungeonRewardSettlementRequested.AddDynamic(
						this,
						&ThisClass::HandleDungeonRewardSettlementRequested);
					bDungeonStartConfirmed = false;
					bDungeonFinishRequested = false;
					bDungeonFinishReported = false;
					bRequestedDungeonCleared = false;
					bRewardSettlementRequested = false;
					bRewardSettlementRequestInFlight = false;
					DungeonMemberCharacterIds.Reset();
					PendingRewardVersion.Reset();
					PendingCurrencyChanges.Reset();
					PendingItemRewards.Reset();
					WebManager->StartConfiguredDungeonSession();
					GetWorldTimerManager().SetTimer(
						DungeonHeartbeatTimer,
						this,
						&ThisClass::SendDungeonSessionHeartbeat,
						FMath::Max(5.f, DungeonHeartbeatIntervalSeconds),
						true);
				}
			}
		}
	}

	//마을이라거나 보상이 없는 맵은 제외한다.
	
	if (bGiveReward)
	{
		//InitializeRewardItems();
		if (URPGGameInstance* GI = GetGameInstance<URPGGameInstance>())
		{
			SetGameDifficulty(GI->GetPendingGameDifficulty());
		}
	}
}

void ARPGGameModeBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(DungeonHeartbeatTimer);

	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UHttpWebManager* WebManager =
			GameInstance->GetSubsystem<UHttpWebManager>())
		{
			WebManager->OnJoinTicketConsumed.RemoveDynamic(
				this,
				&ThisClass::HandleJoinTicketConsumed);
			WebManager->OnDungeonSessionUpdated.RemoveDynamic(
				this,
				&ThisClass::HandleDungeonSessionUpdated);
			WebManager->OnDungeonRewardSettlementRequested.RemoveDynamic(
				this,
				&ThisClass::HandleDungeonRewardSettlementRequested);
		}
	}

	for (TPair<FString, FPendingAdmission>& Pair : PendingAdmissions)
	{
		Pair.Value.Completion.ExecuteIfBound(TEXT("server_shutting_down"));
	}
	PendingAdmissions.Reset();
	ValidatedAdmissions.Reset();
	DungeonMemberCharacterIds.Reset();
	PendingRewardVersion.Reset();
	PendingCurrencyChanges.Reset();
	PendingItemRewards.Reset();
	bRewardSettlementRequestInFlight = false;
	Super::EndPlay(EndPlayReason);
}

void ARPGGameModeBase::PreLoginAsync(
	const FString& Options,
	const FString& Address,
	const FUniqueNetIdRepl& UniqueId,
	const FOnPreLoginCompleteDelegate& OnComplete)
{
	if (!IsRunningDedicatedServer() || !bRequireBackendJoinTicket)
	{
		Super::PreLoginAsync(
			Options,
			Address,
			UniqueId,
			OnComplete);
		return;
	}

	FString ErrorMessage;
	Super::PreLogin(Options, Address, UniqueId, ErrorMessage);
	if (!ErrorMessage.IsEmpty())
	{
		OnComplete.ExecuteIfBound(ErrorMessage);
		return;
	}

	const FString JoinTicket =
		UGameplayStatics::ParseOption(Options, TEXT("JoinTicket"));
	if (JoinTicket.IsEmpty())
	{
		OnComplete.ExecuteIfBound(TEXT("missing_join_ticket"));
		return;
	}

	if (PendingAdmissions.Contains(JoinTicket)
		|| ValidatedAdmissions.Contains(JoinTicket))
	{
		OnComplete.ExecuteIfBound(TEXT("duplicate_join_ticket"));
		return;
	}

	UGameInstance* GameInstance = GetGameInstance();
	UHttpWebManager* WebManager = GameInstance
		? GameInstance->GetSubsystem<UHttpWebManager>()
		: nullptr;
	if (!WebManager)
	{
		OnComplete.ExecuteIfBound(TEXT("backend_unavailable"));
		return;
	}

	FPendingAdmission& Pending = PendingAdmissions.Add(JoinTicket);
	Pending.Completion = OnComplete;
	Pending.PlatformId = UniqueId.IsValid()
		? UniqueId.ToString()
		: FString();
	WebManager->ConsumeJoinTicket(JoinTicket);
}

FString ARPGGameModeBase::InitNewPlayer(
	APlayerController* NewPlayerController,
	const FUniqueNetIdRepl& UniqueId,
	const FString& Options,
	const FString& Portal)
{
	FString ErrorMessage = Super::InitNewPlayer(
		NewPlayerController,
		UniqueId,
		Options,
		Portal);
	if (!ErrorMessage.IsEmpty()
		|| !IsRunningDedicatedServer()
		|| !bRequireBackendJoinTicket)
	{
		return ErrorMessage;
	}

	const FString JoinTicket =
		UGameplayStatics::ParseOption(Options, TEXT("JoinTicket"));
	FValidatedAdmission Admission;
	if (!ValidatedAdmissions.RemoveAndCopyValue(JoinTicket, Admission))
	{
		return TEXT("join_ticket_not_validated");
	}

	ARPGPlayerState* PlayerState =
		NewPlayerController
			? NewPlayerController->GetPlayerState<ARPGPlayerState>()
			: nullptr;
	if (!PlayerState)
	{
		return TEXT("rpg_player_state_unavailable");
	}

	PlayerState->SetAuthenticatedIdentity(
		Admission.CharacterId,
		Admission.SteamId,
		Admission.DungeonSessionId);
	if (URPGInventoryComponent* Inventory =
		NewPlayerController->FindComponentByClass<URPGInventoryComponent>())
	{
		Inventory->InitializePersistenceForAuthenticatedCharacter();
	}
	if (URPGInventoryProjectionComponent* Projection =
		NewPlayerController->FindComponentByClass<
			URPGInventoryProjectionComponent>())
	{
		Projection->LoadAuthenticatedCharacterItems();
	}

	return FString();
}

void ARPGGameModeBase::HandleJoinTicketConsumed(
	const FString& JoinTicket,
	const FString& DungeonSessionId,
	const FString& CharacterId,
	const FString& SteamId,
	bool bSuccess)
{
	FPendingAdmission Pending;
	if (!PendingAdmissions.RemoveAndCopyValue(JoinTicket, Pending))
	{
		return;
	}

	if (!bSuccess
		|| DungeonSessionId.IsEmpty()
		|| CharacterId.IsEmpty()
		|| SteamId.IsEmpty())
	{
		Pending.Completion.ExecuteIfBound(TEXT("invalid_join_ticket"));
		return;
	}

	const UGameInstance* GameInstance = GetGameInstance();
	const UHttpWebManager* WebManager = GameInstance
		? GameInstance->GetSubsystem<UHttpWebManager>()
		: nullptr;
	if (!WebManager
		|| !DungeonSessionId.Equals(
			WebManager->GetConfiguredDungeonSessionId(),
			ESearchCase::CaseSensitive))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("Rejected a join ticket assigned to another dungeon session."));
		Pending.Completion.ExecuteIfBound(
			TEXT("dungeon_session_mismatch"));
		return;
	}

	if (bRequireSteamIdentityMatch
		&& (Pending.PlatformId.IsEmpty()
			|| !Pending.PlatformId.Equals(
				SteamId,
				ESearchCase::CaseSensitive)))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("Rejected a join ticket because the connection identity did not match."));
		Pending.Completion.ExecuteIfBound(TEXT("steam_identity_mismatch"));
		return;
	}

	FValidatedAdmission& Admission = ValidatedAdmissions.Add(JoinTicket);
	Admission.DungeonSessionId = DungeonSessionId;
	Admission.CharacterId = CharacterId;
	Admission.SteamId = SteamId;
	Pending.Completion.ExecuteIfBound(FString());
}

void ARPGGameModeBase::SendDungeonSessionHeartbeat()
{
	if (bDungeonFinishReported)
	{
		return;
	}

	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UHttpWebManager* WebManager =
				GameInstance->GetSubsystem<UHttpWebManager>())
		{
			if (bRewardSettlementRequested)
			{
				WebManager->HeartbeatConfiguredDungeonSession();
				SendPendingDungeonRewardSettlement();
			}
			else if (bDungeonFinishRequested)
			{
				WebManager->FinishConfiguredDungeonSession(
					bRequestedDungeonCleared);
			}
			else if (!bDungeonStartConfirmed)
			{
				WebManager->StartConfiguredDungeonSession();
			}
			else
			{
				WebManager->HeartbeatConfiguredDungeonSession();
			}
		}
	}
}

void ARPGGameModeBase::SendPendingDungeonRewardSettlement()
{
	if (!bRewardSettlementRequested
		|| bRewardSettlementRequestInFlight)
	{
		return;
	}

	UGameInstance* GameInstance = GetGameInstance();
	UHttpWebManager* WebManager = GameInstance
		? GameInstance->GetSubsystem<UHttpWebManager>()
		: nullptr;
	if (!WebManager || !WebManager->IsConfiguredForDungeonServer())
	{
		return;
	}

	bRewardSettlementRequestInFlight = true;
	WebManager->SettleConfiguredDungeonRewards(
		PendingRewardVersion,
		PendingCurrencyChanges,
		PendingItemRewards);
}

void ARPGGameModeBase::ReportDungeonFinished(bool bCleared)
{
	if (bCleared)
	{
		const TArray<FRPGCurrencyChange> NoCurrencyChanges;
		const TArray<FRPGDungeonItemReward> NoItemRewards;
		ReportDungeonClearedWithRewards(
			TEXT("no_reward"),
			NoCurrencyChanges,
			NoItemRewards);
		return;
	}

	if (!IsRunningDedicatedServer()
		|| bDungeonFinishRequested
		|| bDungeonFinishReported
		|| bRewardSettlementRequested)
	{
		return;
	}

	UGameInstance* GameInstance = GetGameInstance();
	UHttpWebManager* WebManager = GameInstance
		? GameInstance->GetSubsystem<UHttpWebManager>()
		: nullptr;
	if (!WebManager || !WebManager->IsConfiguredForDungeonServer())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("Cannot report dungeon completion: backend assignment is missing."));
		return;
	}

	bDungeonFinishRequested = true;
	bRequestedDungeonCleared = false;
	WebManager->FinishConfiguredDungeonSession(false);
}

void ARPGGameModeBase::ReportDungeonClearedWithCurrencyReward(
	const FString& RewardVersion,
	const TArray<FRPGCurrencyChange>& CurrencyChanges)
{
	const TArray<FRPGDungeonItemReward> NoItemRewards;
	ReportDungeonClearedWithRewards(
		RewardVersion,
		CurrencyChanges,
		NoItemRewards);
}

void ARPGGameModeBase::ReportConfiguredDungeonClear()
{
	if (!IsRunningDedicatedServer())
	{
		return;
	}

	if (!bGiveReward)
	{
		ReportDungeonFinished(true);
		return;
	}

	const TObjectPtr<URPGDungeonRewardDefinition>* RewardDefinition =
		DungeonClearRewardsByDifficulty.Find(CurrentGameDifficulty);
	if (!RewardDefinition || !IsValid(RewardDefinition->Get()))
	{
		UE_LOG(LogTemp, Error,
			TEXT("Cannot settle dungeon clear: no reward definition is "
				"configured for difficulty %d."),
			static_cast<int32>(CurrentGameDifficulty));
		return;
	}

	FString RewardVersion;
	TArray<FRPGCurrencyChange> CurrencyChanges;
	TArray<FRPGDungeonItemReward> ItemRewards;
	FString ValidationError;
	if (!RewardDefinition->Get()->BuildSettlement(
		RewardVersion,
		CurrencyChanges,
		ItemRewards,
		ValidationError))
	{
		UE_LOG(LogTemp, Error,
			TEXT("Cannot settle dungeon clear with reward definition %s: %s"),
			*RewardDefinition->Get()->GetPathName(),
			*ValidationError);
		return;
	}

	ReportDungeonClearedWithRewards(
		RewardVersion,
		CurrencyChanges,
		ItemRewards);
}

void ARPGGameModeBase::ReportDungeonClearedWithRewards(
	const FString& RewardVersion,
	const TArray<FRPGCurrencyChange>& CurrencyChanges,
	const TArray<FRPGDungeonItemReward>& ItemRewards)
{
	if (!IsRunningDedicatedServer()
		|| !bDungeonStartConfirmed
		|| bDungeonFinishRequested
		|| bDungeonFinishReported
		|| bRewardSettlementRequested)
	{
		return;
	}

	if (DungeonMemberCharacterIds.IsEmpty()
		|| !IsRewardIdentifier(RewardVersion.TrimStartAndEnd())
		|| CurrencyChanges.Num() > 16
		|| ItemRewards.Num() > 16)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("Cannot settle dungeon currency rewards: members, reward "
				"version, or currency changes are invalid."));
		return;
	}

	UGameInstance* GameInstance = GetGameInstance();
	const UHttpWebManager* WebManager = GameInstance
		? GameInstance->GetSubsystem<UHttpWebManager>()
		: nullptr;
	TSet<FString> CurrencyCodes;
	for (const FRPGCurrencyChange& Change : CurrencyChanges)
	{
		const FString CurrencyCode = Change.CurrencyCode.ToString();
		if (!IsRewardIdentifier(CurrencyCode)
			|| Change.Delta <= 0
			|| CurrencyCodes.Contains(CurrencyCode))
		{
			UE_LOG(LogTemp, Warning,
				TEXT("Cannot settle dungeon currency rewards: changes must "
					"use unique ASCII identifiers and positive deltas."));
			return;
		}
		CurrencyCodes.Add(CurrencyCode);
	}

	int64 TotalItemQuantity = 0;
	for (const FRPGDungeonItemReward& Reward : ItemRewards)
	{
		if (Reward.DefinitionType.IsNone()
			|| Reward.DefinitionName.IsNone()
			|| Reward.DefinitionVersion < 1
			|| Reward.Quantity < 1
			|| !Reward.Durability.IsValid())
		{
			UE_LOG(LogTemp, Warning,
				TEXT("Cannot settle dungeon item rewards: an item definition, "
					"quantity, or durability value is invalid."));
			return;
		}

		TotalItemQuantity += Reward.Quantity;
		if (TotalItemQuantity > MAX_int32)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("Cannot settle dungeon item rewards: total quantity is too large."));
			return;
		}

		TSet<FGameplayTag> StatTags;
		for (const FRPGDungeonItemRewardStat& Stat : Reward.StatValues)
		{
			if (!Stat.StatTag.IsValid()
				|| !FMath::IsFinite(Stat.Value)
				|| StatTags.Contains(Stat.StatTag))
			{
				UE_LOG(LogTemp, Warning,
					TEXT("Cannot settle dungeon item rewards: stat tags must be "
						"valid and unique with finite values."));
				return;
			}
			StatTags.Add(Stat.StatTag);
		}
	}

	if (!WebManager
		|| !WebManager->IsConfiguredForDungeonServer())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("Cannot settle dungeon currency rewards: the backend "
				"assignment is invalid."));
		return;
	}

	bRewardSettlementRequested = true;
	bRewardSettlementRequestInFlight = false;
	PendingRewardVersion = RewardVersion.TrimStartAndEnd();
	PendingCurrencyChanges = CurrencyChanges;
	PendingItemRewards = ItemRewards;
	SendPendingDungeonRewardSettlement();
}

void ARPGGameModeBase::HandleDungeonRewardSettlementRequested(
	const FString& State,
	int32 HttpStatus,
	bool bSuccess)
{
	if (!bRewardSettlementRequested)
	{
		return;
	}
	bRewardSettlementRequestInFlight = false;

	if (!bSuccess)
	{
		const bool bPermanentFailure = HttpStatus >= 400
			&& HttpStatus < 500
			&& HttpStatus != 408
			&& HttpStatus != 425
			&& HttpStatus != 429;
		UE_LOG(LogTemp, Warning,
			TEXT("Dungeon reward settlement was not accepted (HTTP %d, "
				"permanent=%s)."),
			HttpStatus,
			bPermanentFailure ? TEXT("true") : TEXT("false"));
		if (bPermanentFailure)
		{
			bRewardSettlementRequested = false;
			PendingRewardVersion.Reset();
			PendingCurrencyChanges.Reset();
			PendingItemRewards.Reset();
			if (HttpStatus == 400 || HttpStatus == 409)
			{
				ReportDungeonFinished(false);
			}
			else
			{
				bDungeonFinishReported = true;
				GetWorldTimerManager().ClearTimer(DungeonHeartbeatTimer);
			}
		}
		return;
	}

	bRewardSettlementRequested = false;
	bDungeonFinishRequested = true;
	bRequestedDungeonCleared = true;
	bDungeonFinishReported = true;
	PendingRewardVersion.Reset();
	PendingCurrencyChanges.Reset();
	PendingItemRewards.Reset();
	GetWorldTimerManager().ClearTimer(DungeonHeartbeatTimer);
	UE_LOG(LogTemp, Log,
		TEXT("Dungeon reward settlement accepted by the backend (state %s)."),
		*State);
}

void ARPGGameModeBase::HandleDungeonSessionUpdated(
	const FRPGDungeonSession& DungeonSession,
	bool bSuccess)
{
	if (!bSuccess)
	{
		return;
	}

	const UGameInstance* GameInstance = GetGameInstance();
	const UHttpWebManager* WebManager = GameInstance
		? GameInstance->GetSubsystem<UHttpWebManager>()
		: nullptr;
	if (!WebManager
		|| !DungeonSession.DungeonSessionId.Equals(
			WebManager->GetConfiguredDungeonSessionId(),
			ESearchCase::CaseSensitive))
	{
		return;
	}

	if (DungeonSession.State.Equals(
		TEXT("InProgress"),
		ESearchCase::CaseSensitive))
	{
		bDungeonStartConfirmed = true;
		DungeonMemberCharacterIds.Reset();
		for (const FRPGDungeonSessionMember& Member : DungeonSession.Members)
		{
			if (!Member.CharacterId.IsEmpty())
			{
				DungeonMemberCharacterIds.Add(Member.CharacterId);
			}
		}
	}

	const TCHAR* ExpectedOutcome = bRequestedDungeonCleared
		? TEXT("Cleared")
		: TEXT("Failed");
	if (bDungeonFinishRequested
		&& DungeonSession.State.Equals(
			ExpectedOutcome,
			ESearchCase::CaseSensitive))
	{
		bDungeonFinishReported = true;
		GetWorldTimerManager().ClearTimer(DungeonHeartbeatTimer);
		UE_LOG(LogTemp, Log,
			TEXT("Dungeon session completion confirmed: %s."),
			ExpectedOutcome);
	}
}

void ARPGGameModeBase::SetGameDifficulty(ERPGGameDifficulty InGameDifficulty)
{
	switch (InGameDifficulty)
	{
	case ERPGGameDifficulty::Easy:
		CurrentGameDifficulty = ERPGGameDifficulty::Easy;
		InitializeRewardItems("EasyReward");
		break;
	case ERPGGameDifficulty::Normal:
		CurrentGameDifficulty = ERPGGameDifficulty::Normal;
		InitializeRewardItems("NormalReward");
		break;
	case ERPGGameDifficulty::Hard:
		CurrentGameDifficulty = ERPGGameDifficulty::Hard;
		InitializeRewardItems("HardReward");
		break;
	case ERPGGameDifficulty::Hell:
		CurrentGameDifficulty = ERPGGameDifficulty::Hell;
		InitializeRewardItems("HellReward");
		break;
	default:
		break;
	}
	
}

void ARPGGameModeBase::GiveContentReward(ARPGPlayer* Player)
{
	//if (!RewardDataTable || !Player) return;

	//URPGInventoryComponent* PlayerInventory = Player->GetRPGInventory();

	//if (!PlayerInventory) return;

	////TODO:인벤토리에 아이템 지급

	//if (ClearPanelClass)
	//{
	//	URPGContentClearPanel* ClearPanel = CreateWidget<URPGContentClearPanel>(GetWorld(), ClearPanelClass);

	//	if (ClearPanel)
	//	{
	//		ClearPanel->AddToViewport();
	//		ClearPanel->SetRenderTranslation(FVector2D(CoordX, 0.f));
	//	}
	//}

	//for (URPGItemBase* RewardItem : RewardItems)
	//{
	//	if (RewardItem)
	//	{
	//		const FItemAddResult AddResult = PlayerInventory->HandleAddItem(RewardItem);

	//		switch (AddResult.OperationResult)
	//		{
	//		case EItemAddResult::IAR_NoItemAdded:
	//			break;
	//		case EItemAddResult::IAR_PartialAmountItemAdded:

	//			break;
	//		case EItemAddResult::IAR_AllItemAdded:
	//			Destroy();
	//			break;
	//		}

	//		UE_LOG(LogTemp, Warning, TEXT("%s"), *AddResult.ResultMessage.ToString());
	//	}

	//	else
	//	{
	//		Debug::Print("Player Inventory component is null..");
	//	}
	//}
}

void ARPGGameModeBase::InitializeRewardItems(FName InName)
{	
	//FName RowName("SurvivalReward");

	//FString ContextString = TEXT("Reward Lookup");

	/*const FItemDropTable* RewardData = RewardDataTable->FindRow<FItemDropTable>(InName, InName.ToString());

	if (!RewardData)
	{
		UE_LOG(LogTemp, Warning, TEXT("보상 데이터를 찾을 수 없습니다: %s"), *InName.ToString());
		return;
	}

	TArray<FName> ItemRowNames;
	TArray<int32> ItemQuantities;

	const TArray<FRewardItem>& Rewards = RewardData->RewardItemList;

	for (const FRewardItem& Reward : Rewards)
	{
		UE_LOG(LogTemp, Warning, TEXT("보상 아이템: %s, 수량: %d, 확률: %f"),
			*Reward.ItemRowName.ToString(), Reward.DropQuantity, Reward.DropChance);
		ItemRowNames.Add(Reward.ItemRowName);
		ItemQuantities.Add(Reward.DropQuantity);
	}
	
	for (int32 i = 0; i < ItemRowNames.Num(); i++)
	{
		const FRPGItemData* ItemData = ItemDataTable->FindRow<FRPGItemData>(ItemRowNames[i], ItemRowNames[i].ToString());
	
		if (ItemData)
		{
			URPGItemBase* RewardItem = NewObject<URPGItemBase>();

			

			RewardItems.Add(RewardItem);
		}
	}*/
}
