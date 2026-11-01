#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Character/RPGPlayer.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Component/Progression/RPGTrainingObjectiveComponent.h"
#include "Component/Skill/RPGPlayerSkillComponent.h"
#include "Component/UI/QuickSlotComponent.h"
#include "Components/Button.h"
#include "Components/ComboBoxString.h"
#include "Components/TextBlock.h"
#include "Containers/Ticker.h"
#include "Controller/RPGPlayerController.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Manager/HttpWebManager.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Online/RPGDungeonSessionSubsystem.h"
#include "Online/RPGOnlineLobbySubsystem.h"
#include "Online/RPGSteamAuthSubsystem.h"
#include "Player/RPGPlayerState.h"
#include "Progression/RPGCharacterProgressionComponent.h"
#include "RPGGameplayTags.h"
#include "UI/Content/RPGDungeonResultActionsWidget.h"
#include "UI/Lobby/RPGOnlineLobbyWidget.h"
#include "UI/Lobby/RPGCharacterConfigurationWidget.h"
#include "UObject/Package.h"
#include "UObject/UObjectIterator.h"

namespace RPGTrainingPlaytest
{
// An explicit development-only process probe. No reflected type, progress setter,
// server completion call, reward request, ability cancellation, or forced end.
struct FProbe
{
	FString Gates, Character, Steam, Session, Sibling;
	double Deadline = FPlatformTime::Seconds() + 240.0, NextAction = 0, ReleasedAt = 0;
	int32 Stage = 0, Action = 0, Slot = INDEX_NONE, LastMask = -1, LastWait = -1;
	bool bHeld = false, bPassed = false;
	bool bCompletionUI = false, bReturnRequested = false, bReturned = false;
	bool bProgressionLobby = false, bProgressionChecked = false, bUnknownBlocked = false;
	int32 ProgressionLobbyStage = 0;
	bool bEquipNewSkill = false;
	int64 ConfigurationRevisionBeforeSave = 0, ConfigurationRevisionAfterSave = 0;
	TArray<FGameplayTag> ConfigurationSlotsBeforeSave;
	TWeakObjectPtr<ARPGPlayer> InputPawn;

	bool Fail(const TCHAR* Reason)
	{
		UE_LOG(LogTemp, Error, TEXT("RPG_TRAINING_PLAYTEST FAIL Reason=%s Stage=%d Action=%d Mask=%d"), Reason, Stage, Action, LastMask);
		FPlatformMisc::RequestExitWithStatus(false, 1);
		return false;
	}
	bool Wait(int32 Code, const TCHAR* Reason)
	{
		if (LastWait != Code) { LastWait = Code; UE_LOG(LogTemp, Display, TEXT("RPG_TRAINING_PLAYTEST WAIT Reason=%s Code=%d"), Reason, Code); }
		return true;
	}

	bool CheckProgressionLobby(UWorld* World, UGameInstance* GI, URPGDungeonSessionSubsystem* Flow)
	{
		auto* Lobby = GI->GetSubsystem<URPGOnlineLobbySubsystem>();
		auto* Widget = Lobby ? Lobby->GetLobbyWidget() : nullptr;
		auto* PC = World->GetFirstPlayerController();
		if (!Widget || !Widget->IsInViewport() || !Lobby->IsLobbyController(PC)) return true;
		auto* Configuration = Widget->GetCharacterConfigurationWidget();
		auto* Start = Cast<UButton>(Widget->GetWidgetFromName(TEXT("StartTutorialButton")));
		auto* Change = Cast<UButton>(Widget->GetWidgetFromName(TEXT("ChangeCharacterButton")));
		auto* Continue = Cast<UButton>(Widget->GetWidgetFromName(TEXT("ContinueWithCharacterButton")));
		auto* Selector = Cast<UComboBoxString>(Widget->GetWidgetFromName(TEXT("OwnedCharacters")));
		if (!Configuration || !Start || !Change || !Continue || !Selector) return Fail(TEXT("ProgressionLobbyControls"));
		const auto* Saved = Configuration->GetConfirmedProgression();
		if (ProgressionLobbyStage == 0)
		{
			if (!Saved || Widget->GetCharacterOption(Sibling).IsEmpty()) return true;
			if (Saved->CharacterId != Character || !(Saved->Milestones & 1) || Start->GetIsEnabled())
				return Fail(TEXT("CompletedTutorialMenu"));
			if (!Change->GetIsEnabled()) return true;
			Change->OnClicked.Broadcast(); Selector->SetSelectedOption(Widget->GetCharacterOption(Sibling));
			ProgressionLobbyStage = 1; return true;
		}
		if (ProgressionLobbyStage == 1 || ProgressionLobbyStage == 3)
		{
			if (!Continue->GetIsEnabled()) return true;
			Continue->OnClicked.Broadcast();
			if (!Configuration->GetConfirmedProgression())
			{
				if (Start->GetIsEnabled()) return Fail(TEXT("UnknownTutorialProgressNotBlocked"));
				bUnknownBlocked = true;
			}
			++ProgressionLobbyStage; return true;
		}
		if (!Saved) return true;
		if (ProgressionLobbyStage == 2)
		{
			if (Saved->CharacterId != Sibling || Saved->Milestones != 0 || Saved->Level != 1 || !Start->GetIsEnabled())
				return Fail(TEXT("SiblingTutorialProgress"));
			if (!Change->GetIsEnabled()) return true;
			Change->OnClicked.Broadcast(); Selector->SetSelectedOption(Widget->GetCharacterOption(Character));
			ProgressionLobbyStage = 3; return true;
		}
		if (ProgressionLobbyStage == 4)
		{
			if (Saved->CharacterId != Character || !(Saved->Milestones & 1) || Saved->Level != 2
				|| !Flow->GetCurrentDungeonSession().DungeonSessionId.IsEmpty() || Start->GetIsEnabled() || !bUnknownBlocked)
				return Fail(TEXT("ReselectedTutorialProgress"));
			Start->OnClicked.Broadcast(); // The disabled action must also enforce its fresh guard.
			if (Flow->IsRequestInFlight() || !Flow->GetCurrentDungeonSession().DungeonSessionId.IsEmpty())
				return Fail(TEXT("CompletedTutorialCreatedAnotherSession"));
			if (bEquipNewSkill)
			{
				auto* Open = Cast<UButton>(Widget->GetWidgetFromName(TEXT("OpenSkillMenuButton")));
				if (!Open || !Open->GetIsEnabled()) return true;
				Open->OnClicked.Broadcast(); ProgressionLobbyStage = 5; return true;
			}
			FinishProgressionCheck();
		}
		if (ProgressionLobbyStage == 5)
		{
			auto* Fill = Cast<UButton>(Configuration->GetWidgetFromName(TEXT("EquipUnslottedSkillsButton")));
			auto* Save = Cast<UButton>(Configuration->GetWidgetFromName(TEXT("SaveConfigurationButton")));
			const FGameplayTag Acquired = FGameplayTag::RequestGameplayTag(FName(TEXT("Player.Ability.Skill.WhirlWind")));
			if (!Fill || !Save || !Fill->GetIsEnabled() || Saved->SkillSlots.Contains(Acquired))
				return Fail(TEXT("UnslottedSkillActionUnavailable"));
			ConfigurationRevisionBeforeSave = Saved->Revision; ConfigurationSlotsBeforeSave = Saved->SkillSlots;
			Fill->OnClicked.Broadcast();
			const auto* StillSaved = Configuration->GetConfirmedProgression();
			if (!StillSaved || StillSaved->SkillSlots != ConfigurationSlotsBeforeSave || StillSaved->Revision != ConfigurationRevisionBeforeSave
				|| Configuration->IsRequestInFlight() || !Configuration->HasUnsavedChanges() || Fill->GetIsEnabled() || !Save->GetIsEnabled())
				return Fail(TEXT("UnslottedSkillWasNotDraftOnly"));
			Save->OnClicked.Broadcast(); ProgressionLobbyStage = 6; return true;
		}
		if (ProgressionLobbyStage == 6 || ProgressionLobbyStage == 7)
		{
			const FGameplayTag Acquired = FGameplayTag::RequestGameplayTag(FName(TEXT("Player.Ability.Skill.WhirlWind")));
			if (Saved->Revision <= ConfigurationRevisionBeforeSave || Saved->SkillSlots.Num() != 8
				|| !Saved->SkillSlots.Contains(Acquired) || Saved->UsedSP != 0 || Saved->TotalSP != 3 || Saved->Experience != 100
				|| Configuration->HasUnsavedChanges()) return Fail(TEXT("UnslottedSkillSaveMismatch"));
			for (int32 Index = 0; Index < ConfigurationSlotsBeforeSave.Num(); ++Index)
				if (ConfigurationSlotsBeforeSave[Index].IsValid() && Saved->SkillSlots[Index] != ConfigurationSlotsBeforeSave[Index])
					return Fail(TEXT("ExistingSkillSlotChanged"));
			if (ProgressionLobbyStage == 6)
			{
				ConfigurationRevisionAfterSave = Saved->Revision;
				auto* Load = Cast<UButton>(Configuration->GetWidgetFromName(TEXT("LoadConfigurationButton")));
				if (!Load || !Load->GetIsEnabled()) return true;
				Load->OnClicked.Broadcast(); ProgressionLobbyStage = 7; return true;
			}
			if (Saved->Revision != ConfigurationRevisionAfterSave) return Fail(TEXT("UnslottedSkillReloadRevisionChanged"));
			UE_LOG(LogTemp, Display, TEXT("RPG_TRAINING_PLAYTEST EQUIP_NEW_SKILL Staged=1 Saved=1 Reloaded=1 ExistingSlotsPreserved=1 SPUnchanged=1"));
			FinishProgressionCheck();
		}
		return true;
	}

	void FinishProgressionCheck()
	{
			bProgressionChecked = true;
			UE_LOG(LogTemp, Display, TEXT("RPG_TRAINING_PLAYTEST PROGRESSION_LOBBY StoredComplete=1 UnknownBlocked=1 SiblingRequired=1 ReselectedComplete=1 ExtraSession=0"));
			UE_LOG(LogTemp, Display, TEXT("RPG_TRAINING_PLAYTEST COMPLETION_UI_RETURNED Lobby=1 IdentityPreserved=1"));
	}

	bool Tick(float)
	{
		if (IFileManager::Get().FileExists(*FPaths::Combine(Gates, TEXT("stop.ready"))))
		{
			FPlatformMisc::RequestExitWithStatus(false, 0);
			return false;
		}
		const double Now = FPlatformTime::Seconds();
		if (Now > Deadline) return Fail(TEXT("Timeout"));
		UWorld* World = nullptr;
		if (GEngine) for (const auto& Context : GEngine->GetWorldContexts())
			if (Context.World() && Context.World()->IsGameWorld()) { World = Context.World(); break; }
		if (!World || !World->GetGameInstance()) return true;
		const bool bServer = World->GetNetMode() == NM_DedicatedServer;
		auto* GI = World->GetGameInstance();
		auto* Web = GI->GetSubsystem<UHttpWebManager>();
		auto* Flow = GI->GetSubsystem<URPGDungeonSessionSubsystem>();
		if (!Web || (!bServer && !Flow)) return Fail(TEXT("MissingSubsystem"));
		if (!bServer && bPassed && bCompletionUI)
		{
			if (bReturned) return bProgressionLobby && !bProgressionChecked ? CheckProgressionLobby(World, GI, Flow) : true;
			if (bReturnRequested && World->GetNetMode() == NM_Standalone
				&& UWorld::RemovePIEPrefix(World->GetOutermost()->GetName()) == TEXT("/Game/Maps/LobbyMap"))
			{
				if (Web->GetAuthenticatedSteamId() != Steam || Flow->GetSelectedCharacterId() != Character)
					return Fail(TEXT("CompletionReturnIdentity"));
				bReturned = true;
				if (!bProgressionLobby) UE_LOG(LogTemp, Display, TEXT("RPG_TRAINING_PLAYTEST COMPLETION_UI_RETURNED Lobby=1 IdentityPreserved=1"));
				return true;
			}
			if (bReturnRequested) return true;
			auto* Owner = World->GetFirstPlayerController();
			URPGDungeonResultActionsWidget* Result = nullptr;
			int32 Count = 0;
			for (TObjectIterator<URPGDungeonResultActionsWidget> It; It; ++It)
				if (It->GetWorld() == World && It->GetOwningPlayer() == Owner && It->IsInViewport()) { Result = *It; ++Count; }
			if (Count > 1) return Fail(TEXT("DuplicateTrainingResults"));
			if (!Result) return true;
			auto* Return = Cast<UButton>(Result->GetWidgetFromName(TEXT("ReturnToLobbyButton")));
			if (!Return || !Return->GetIsEnabled()) return true;
			bReturnRequested = true;
			UE_LOG(LogTemp, Display, TEXT("RPG_TRAINING_PLAYTEST COMPLETION_UI_READY Widgets=1 ReturnEnabled=1"));
			// Exercise the existing UMG button handler and its fresh backend check.
			// No direct travel, terminal-state injection or settlement call.
			Return->OnClicked.Broadcast();
			return true;
		}
		if (!bServer && Stage == 0)
		{
			GI->GetSubsystem<URPGSteamAuthSubsystem>()->LoginToDevelopmentBackend(Steam);
			Stage = 1;
			UE_LOG(LogTemp, Display, TEXT("RPG_TRAINING_PLAYTEST LOGIN_REQUESTED"));
			return true;
		}
		if (!bServer && Stage == 1)
		{
			if (!Web->HasAccessToken()) return true;
			if (Web->GetAuthenticatedSteamId() != Steam || !Flow->SelectCharacter(Character) || !Flow->ResumeDungeonSession())
				return Fail(TEXT("SelectOrResume"));
			Stage = 2;
			UE_LOG(LogTemp, Display, TEXT("RPG_TRAINING_PLAYTEST SESSION_RESUME_REQUESTED"));
			return true;
		}
		if (!bServer && Stage == 2)
		{
			if (Flow->IsRequestInFlight()) return true;
			const auto Current = Flow->GetCurrentDungeonSession();
			if (Current.DungeonSessionId != Session || Current.State != TEXT("InProgress")
				|| !Flow->ConnectToDungeon(World->GetFirstPlayerController(), Current.ServerAddress)) return Fail(TEXT("Connect"));
			Stage = 3;
			UE_LOG(LogTemp, Display, TEXT("RPG_TRAINING_PLAYTEST CONNECT_REQUESTED"));
			return true;
		}
		auto* PC = Cast<ARPGPlayerController>(World->GetFirstPlayerController());
		auto* Pawn = PC ? Cast<ARPGPlayer>(PC->GetPawn()) : nullptr;
		auto* State = PC ? PC->GetPlayerState<ARPGPlayerState>() : nullptr;
		if (!Pawn || !State || !State->HasAuthenticatedCharacter()) return Wait(1, TEXT("PawnAdmission"));
		if (State->GetBackendCharacterId() != Character || State->GetBackendDungeonSessionId() != Session
			|| State->GetAuthenticatedSteamId() != Steam) return Fail(TEXT("Identity"));
		auto* Objectives = Pawn->FindComponentByClass<URPGTrainingObjectiveComponent>();
		auto* Growth = Pawn->FindComponentByClass<URPGPlayerSkillComponent>();
		auto* Restore = Pawn->FindComponentByClass<URPGCharacterProgressionComponent>();
		auto* Slots = Pawn->FindComponentByClass<UQuickSlotComponent>();
		auto* ASC = Cast<URPGAbilitySystemComponent>(Pawn->GetAbilitySystemComponent());
		if (!Objectives || !Growth || !Restore || !Slots || !ASC) return Fail(TEXT("MissingPawnComponent"));
		if (Restore->GetProgressionView().State == ERPGProgressionRestoreState::Failed) return Fail(TEXT("Restore"));
		const auto Progress = Objectives->GetTrainingProgress();
		if (!Progress.bActive || Growth->GetSkillLoadState() != ERPGPlayerSkillLoadState::Ready
			|| Restore->GetProgressionView().State != ERPGProgressionRestoreState::Ready || !Slots->HasInitializedSkillSlots())
			return Wait(10 + int32(Progress.bActive) + 2 * int32(Growth->GetSkillLoadState())
				+ 20 * int32(Restore->GetProgressionView().State), TEXT("TrainingRestore"));
		if (!World->GetNetDriver() || !World->GetNetDriver()->GetReplicationSystem()) return Fail(TEXT("IrisRequired"));
		const int32 Mask = int32(Progress.bBasicAttackCompleted) | (int32(Progress.bEvadeCompleted) << 1)
			| (int32(Progress.bAssaultCompleted) << 2) | (int32(Progress.bChargeCompleted) << 3);
		if (Mask != LastMask)
		{
			LastMask = Mask;
			UE_LOG(LogTemp, Display, TEXT("RPG_TRAINING_PLAYTEST PROGRESS Role=%s Mask=%d"), bServer ? TEXT("Server") : TEXT("Client"), Mask);
		}
		if (bServer || bPassed) return true;
		if (InputPawn.IsValid() && InputPawn.Get() != Pawn) return Fail(TEXT("PawnChanged"));
		InputPawn = Pawn;
		if (Progress.IsComplete())
		{
			if (Action == 3 && ReleasedAt > 0 && !bHeld) Action = 4;
			if (Action != 4 || bHeld) return Fail(TEXT("UnexpectedCompletion"));
			bPassed = true;
			if (bCompletionUI) Deadline = Now + 45.0;
			UE_LOG(LogTemp, Display, TEXT("RPG_TRAINING_PLAYTEST INPUT_PASS Actions=4 Mask=15 ActualInput=1 Iris=1"));
			return true;
		}
		if (Action >= 4 || Now < NextAction) return true;
		if (bHeld)
		{
			if (Action < 2) ASC->OnAbilityInputReleased(Action == 0 ? RPGGameplayTags::InputTag_LightAttack_GreatSword : RPGGameplayTags::InputTag_Roll);
			else Slots->EndUseSkillSlot(Slot);
			bHeld = false; ReleasedAt = Now;
			UE_LOG(LogTemp, Display, TEXT("RPG_TRAINING_PLAYTEST RELEASE Action=%d Slot=%d"), Action, Slot);
			return true;
		}
		if (ReleasedAt > 0)
		{
			if (!(Mask & (1 << Action)))
			{
				if (Now - ReleasedAt > 20.0) return Fail(TEXT("NormalEndNotObserved"));
				return true;
			}
			++Action; ReleasedAt = 0; NextAction = Now + 0.5;
			return true;
		}
		PC->SetControlRotation(FRotator::ZeroRotator);
		Slot = INDEX_NONE;
		if (Action >= 2)
		{
			const FGameplayTag Tag = Action == 2 ? RPGGameplayTags::Player_Ability_Skill_AssultBlade : RPGGameplayTags::Player_Ability_Skill_Charge;
			for (int32 Index = 0; Index < Slots->GetMaxSkillSlots(); ++Index)
				if (const auto* Content = Slots->GetSkillSlotContent(Index); Content && Content->AbilityTag == Tag) { Slot = Index; break; }
			if (Slot == INDEX_NONE) return Fail(TEXT("MissingInitialSlot"));
			Slots->BeginUseSkillSlot(Slot);
		}
		else ASC->OnAbilityInputPressed(Action == 0 ? RPGGameplayTags::InputTag_LightAttack_GreatSword : RPGGameplayTags::InputTag_Roll);
		bHeld = true;
		NextAction = Now + (Action == 3 ? 2.0 : 0.15);
		UE_LOG(LogTemp, Display, TEXT("RPG_TRAINING_PLAYTEST PRESS Action=%d Slot=%d"), Action, Slot);
		return true;
	}
};

static const FDelegateHandle Registration = FCoreDelegates::GetOnPostEngineInit().AddLambda([]
{
	if (!FParse::Param(FCommandLine::Get(), TEXT("RPGTrainingPlaytest"))
		|| !FParse::Param(FCommandLine::Get(), TEXT("nosteam"))) return;
	auto Probe = MakeShared<FProbe>();
	Probe->bCompletionUI = FParse::Param(FCommandLine::Get(), TEXT("RPGTrainingCompletionUI"));
	Probe->bProgressionLobby = FParse::Param(FCommandLine::Get(), TEXT("RPGTrainingProgressionLobby"));
	Probe->bEquipNewSkill = FParse::Param(FCommandLine::Get(), TEXT("RPGTrainingEquipNewSkill"));
	FParse::Value(FCommandLine::Get(), TEXT("RPGTrainingGates="), Probe->Gates);
	FParse::Value(FCommandLine::Get(), TEXT("RPGTrainingCharacter="), Probe->Character);
	FParse::Value(FCommandLine::Get(), TEXT("RPGTrainingSteam="), Probe->Steam);
	FParse::Value(FCommandLine::Get(), TEXT("RPGTrainingSession="), Probe->Session);
	FParse::Value(FCommandLine::Get(), TEXT("RPGTrainingSibling="), Probe->Sibling);
	FGuid Character, Session;
	if (!FGuid::Parse(Probe->Character, Character) || !FGuid::Parse(Probe->Session, Session)
		|| Probe->Steam.IsEmpty() || Probe->Gates.IsEmpty() || FPaths::IsRelative(Probe->Gates))
	{
		Probe->Fail(TEXT("InvalidExplicitScope"));
		return;
	}
	FGuid Sibling;
	if (Probe->bProgressionLobby && (!Probe->bCompletionUI || !FGuid::Parse(Probe->Sibling, Sibling)
		|| !Sibling.IsValid() || Sibling == Character))
	{ Probe->Fail(TEXT("InvalidProgressionLobbyScope")); return; }
	if (Probe->bEquipNewSkill && !Probe->bProgressionLobby)
	{ Probe->Fail(TEXT("InvalidEquipNewSkillScope")); return; }
	if (FParse::Param(FCommandLine::Get(), TEXT("RPGActionProgressCancelPIE")))
	{
		// Cancellation observes the authored charge only; the normal four-action
		// settlement scenario keeps its original sequence and completion checks.
		Probe->Action = 3;
		UE_LOG(LogTemp, Display, TEXT("RPG_TRAINING_PLAYTEST CANCELLATION_INPUT_ONLY Action=3 ActualInput=1"));
	}
	UE_LOG(LogTemp, Display, TEXT("RPG_TRAINING_PLAYTEST START DevelopmentOnly=1"));
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Probe](float Delta) { return Probe->Tick(Delta); }), 0.025f);
});
}
#endif
