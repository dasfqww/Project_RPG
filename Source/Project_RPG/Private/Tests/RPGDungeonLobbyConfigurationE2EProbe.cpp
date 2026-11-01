#include "Tests/RPGDungeonFlowE2EProbe.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Manager/HttpWebManager.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Online/RPGDungeonSessionSubsystem.h"
#include "Online/RPGOnlineLobbySubsystem.h"
#include "Online/RPGSteamAuthSubsystem.h"
#include "Skill/RPGSkillCatalogSubsystem.h"
#include "UI/Lobby/RPGCharacterConfigurationWidget.h"
#include "UI/Lobby/RPGCharacterConfigurationPreview.h"
#include "UI/Lobby/RPGOnlineLobbyWidget.h"

bool URPGDungeonFlowE2EClient::TickLobbyConfigurationProbe(UWorld*, APlayerController* PC)
{
	const bool bRecovery = FParse::Param(FCommandLine::Get(), TEXT("RPGFlowE2EConfigurationRecovery"));
	const bool bConflict = FParse::Param(FCommandLine::Get(), TEXT("RPGFlowE2EConfigurationConflict"));
	const bool bReadFailures = FParse::Param(FCommandLine::Get(), TEXT("RPGFlowE2EConfigurationReadFailures"));
	const bool bPreview = !bRecovery && !bConflict && !bReadFailures;
	FString Gates;
	FParse::Value(FCommandLine::Get(), TEXT("RPGFlowE2EConfigurationGates="), Gates);
	auto* GI = GetGameInstance();
	auto* Web = GI->GetSubsystem<UHttpWebManager>();
	auto* Flow = GI->GetSubsystem<URPGDungeonSessionSubsystem>();
	auto* Auth = GI->GetSubsystem<URPGSteamAuthSubsystem>();
	auto* Lobby = GI->GetSubsystem<URPGOnlineLobbySubsystem>();
	auto* Widget = Lobby ? Lobby->GetLobbyWidget() : nullptr;
	if (!Web || !Flow || !Auth || !Lobby) return Fail(TEXT("ConfigurationSubsystemMissing"));
	if (!Widget || !Lobby->IsLobbyController(PC)) return true;
	auto* Configuration = Widget->GetCharacterConfigurationWidget();
	auto* Login = Cast<UButton>(Widget->GetWidgetFromName(TEXT("LoginDevelopmentButton")));
	auto* Account = Cast<UEditableTextBox>(Widget->GetWidgetFromName(TEXT("DevelopmentSteamId")));
	auto* Characters = Cast<UComboBoxString>(Widget->GetWidgetFromName(TEXT("OwnedCharacters")));
	auto* Continue = Cast<UButton>(Widget->GetWidgetFromName(TEXT("ContinueWithCharacterButton")));
	auto* Back = Cast<UButton>(Widget->GetWidgetFromName(TEXT("ChangeCharacterButton")));
	auto* Load = Configuration ? Cast<UButton>(Configuration->GetWidgetFromName(TEXT("LoadConfigurationButton"))) : nullptr;
	auto* Save = Configuration ? Cast<UButton>(Configuration->GetWidgetFromName(TEXT("SaveConfigurationButton"))) : nullptr;
	auto* Reset = Configuration ? Cast<UButton>(Configuration->GetWidgetFromName(TEXT("ResetConfigurationButton"))) : nullptr;
	if (!Configuration || !Login || !Account || !Characters || !Continue || !Back || !Load || !Save || !Reset)
		return Fail(TEXT("ConfigurationNativeControlsMissing"));
	if (Stage == 0)
	{
		FString ApiUrl;
		FGuid First, Other;
		GConfig->GetString(TEXT("/Script/Project_RPG.HttpWebManager"), TEXT("ApiUrl"), ApiUrl, GGameIni);
		if (!bLobbyUI || bLobbyCreationOnly || bVerifyLobbyLogout || bCreateCharacter || bLobbyDifficultyOnly
			|| bAbandonmentUI || bReauthenticate || bReauthenticateAfterAdmission || bNaturalPlayerAttack
			|| bGameplayAbandonment || bResultAbandonment || bVerifyAbandonedReturn
			|| !FParse::Param(FCommandLine::Get(), TEXT("RPGDevelopmentLogin"))
			|| !ApiUrl.StartsWith(TEXT("http://127.0.0.1:")) || !ApiUrl.EndsWith(TEXT("/api"))
			|| !FRPGBackendPlayerAuthState::IsSteamId(SteamId)
			|| !FGuid::Parse(CharacterId, First) || !First.IsValid()
			|| !FGuid::Parse(ConfigurationOtherCharacterId, Other) || !Other.IsValid() || First == Other
			|| (bRecovery && bConflict)
			|| (bReadFailures && (bRecovery || bConflict))
			|| ((bRecovery || bConflict || bReadFailures) && (Gates.IsEmpty() || FPaths::IsRelative(Gates) || !IFileManager::Get().DirectoryExists(*Gates))))
			return Fail(TEXT("ConfigurationInvalidLocalFixture"));
		if (!Login->GetIsEnabled() || !Account->GetIsEnabled()) return true;
		UE_LOG(LogTemp, Display, TEXT("LOBBY_CONFIGURATION_E2E START LiveMap=1"));
		Account->SetText(FText::FromString(SteamId));
		Stage = 1;
		Login->OnClicked.Broadcast();
		return true;
	}
	if (bReadFailures && Stage >= 40)
	{
		if (Auth->IsLoginRequestInFlight() || Widget->IsFrontendRequestInFlight() || Flow->IsRequestInFlight()) return true;
		if (!Web->HasAccessToken() || Web->GetAuthenticatedSteamId() != SteamId
			|| !Flow->GetCurrentDungeonSession().DungeonSessionId.IsEmpty()) return Fail(TEXT("ReadFailuresLostAuthenticationOrLobby"));
		const auto Ready = [&](const TCHAR* Name) { return IFileManager::Get().FileExists(*(Gates / Name)); };
		if (Stage <= 46)
		{
			auto* Classes = Cast<UComboBoxString>(Widget->GetWidgetFromName(TEXT("CharacterCreationClass")));
			auto* Name = Cast<UEditableTextBox>(Widget->GetWidgetFromName(TEXT("NewCharacterName")));
			auto* Create = Cast<UButton>(Widget->GetWidgetFromName(TEXT("CreateCharacterButton")));
			auto* Reload = Cast<UButton>(Widget->GetWidgetFromName(TEXT("ReloadCreationOptionsButton")));
			auto* Notice = Cast<UTextBlock>(Widget->GetWidgetFromName(TEXT("CharacterCreationClassStatus")));
			if (!Classes || !Name || !Create || !Reload || !Notice) return Fail(TEXT("ReadFailuresCreationControlsMissing"));
			if (Stage == 41 || Stage == 43 || Stage == 45)
			{
				const TCHAR* Gate = Stage == 41 ? TEXT("catalog-ready.ready") : Stage == 43 ? TEXT("catalog-empty.ready") : TEXT("catalog-release.ready");
				if (!Ready(Gate)) return true;
				++Stage;
				Reload->OnClicked.Broadcast();
				if (Reload->GetIsEnabled() || Create->GetIsEnabled()) return Fail(TEXT("ReadFailuresCatalogRequestNotLocked"));
				Reload->OnClicked.Broadcast();
				return true;
			}
			if (!Reload->GetIsEnabled()) return true;
			if (Characters->GetOptionCount() != 2 || !Characters->GetIsEnabled() || !Flow->GetSelectedCharacterId().IsEmpty()
				|| Notice->GetText().IsEmpty()) return Fail(TEXT("ReadFailuresCatalogLostOwnedRoster"));
			if (Stage == 40 || Stage == 44)
			{
				if (Classes->GetOptionCount() != 0 || Classes->GetIsEnabled() || Create->GetIsEnabled())
					return Fail(TEXT("ReadFailuresCatalogAllowedFallbackCreation"));
				if (Stage == 40) Name->SetText(FText::FromString(TEXT("KeepDuringRetry")));
				if (Name->GetText().ToString() != TEXT("KeepDuringRetry")) return Fail(TEXT("ReadFailuresCatalogLostName"));
				Create->OnClicked.Broadcast();
				if (Widget->IsFrontendRequestInFlight()) return Fail(TEXT("ReadFailuresForcedCreationStarted"));
				if (Stage == 40)
				{
					UE_LOG(LogTemp, Display, TEXT("LOBBY_READ_FAILURES CATALOG_FAILED NoFallback=1 NamePreserved=1 OwnedRosterAvailable=1"));
				}
				else
				{
					UE_LOG(LogTemp, Display, TEXT("LOBBY_READ_FAILURES CATALOG_EMPTY NoFallback=1 NamePreserved=1"));
				}
				++Stage;
				return true;
			}
			if (Stage == 42 || Stage == 46)
			{
				if (Classes->GetOptionCount() != 1 || Classes->GetSelectedIndex() != 0 || !Classes->GetIsEnabled()
					|| !Create->GetIsEnabled() || Name->GetText().ToString() != TEXT("KeepDuringRetry"))
					return Fail(TEXT("ReadFailuresCatalogRetryDidNotRecover"));
				if (Stage == 42)
				{
					UE_LOG(LogTemp, Display, TEXT("LOBBY_READ_FAILURES CATALOG_RESTORED AllowedClass=1 NamePreserved=1"));
					Stage = 43;
				}
				else
				{
					Stage = 2;
					Characters->SetSelectedOption(Widget->GetCharacterOption(CharacterId));
				}
				return true;
			}
			return Fail(TEXT("ReadFailuresUnexpectedCatalogStage"));
		}
		TArray<UComboBoxString*> ReadChoices;
		Configuration->WidgetTree->ForEachWidget([&](UWidget* Child)
		{ if (auto* Box = Cast<UComboBoxString>(Child)) ReadChoices.Add(Box); });
		if (ReadChoices.Num() != 17 || Flow->GetSelectedCharacterId() != CharacterId)
			return Fail(TEXT("ReadFailuresLostSelectedConfiguration"));
		if (Stage == 50 || Stage == 52 || Stage == 54)
		{
			const TCHAR* Gate = Stage == 50 ? TEXT("read-forbidden.ready") : Stage == 52 ? TEXT("read-missing.ready") : TEXT("read-release.ready");
			if (!Ready(Gate)) return true;
			++Stage;
			Load->OnClicked.Broadcast();
			if (!Configuration->IsRequestInFlight()) return Fail(TEXT("ReadFailuresReadNotSubmitted"));
			return true;
		}
		if (Stage == 51 || Stage == 53)
		{
			auto* Notice = Cast<UTextBlock>(Configuration->GetWidgetFromName(TEXT("ConfigurationResult")));
			auto* Summary = Cast<UTextBlock>(Configuration->GetWidgetFromName(TEXT("ConfigurationSummary")));
			if (!Configuration->HasUnsavedChanges() || Configuration->HasUnresolvedSave() || !Load->GetIsEnabled()
				|| Save->GetIsEnabled() || Reset->GetIsEnabled() || Back->GetIsEnabled() || ReadChoices[3]->GetIsEnabled()
				|| ReadChoices[3]->GetSelectedIndex() != 3 || ReadChoices[4]->GetSelectedIndex() != 1
				|| ReadChoices[9]->GetSelectedIndex() != 2 || ReadChoices[10]->GetSelectedIndex() != 1
				|| ReadChoices[16]->GetSelectedIndex() != 3 || !Notice || Notice->GetText().IsEmpty()
				|| !Summary || Summary->GetText().IsEmpty()) return Fail(TEXT("ReadFailuresDidNotPreserveAndLockDraft"));
			if (Configuration->SetSkillLevel(FGameplayTag::RequestGameplayTag(TEXT("Player.Ability.Skill.Charge")), 1))
				return Fail(TEXT("ReadFailuresAllowedEditWithoutConfirmedRead"));
			Save->OnClicked.Broadcast();
			if (Configuration->IsRequestInFlight() || Configuration->HasUnresolvedSave()) return Fail(TEXT("ReadFailuresAllowedWriteWithoutConfirmedRead"));
			if (Stage == 51)
			{
				UE_LOG(LogTemp, Display, TEXT("LOBBY_READ_FAILURES FORBIDDEN DraftPreserved=1 NoDefault=1 SaveLocked=1"));
			}
			else
			{
				UE_LOG(LogTemp, Display, TEXT("LOBBY_READ_FAILURES MISSING DraftPreserved=1 NoDefault=1 SaveLocked=1"));
			}
			++Stage;
			return true;
		}
		if (Stage == 55)
		{
			if (Configuration->HasUnsavedChanges() || Configuration->HasUnresolvedSave() || Save->GetIsEnabled()
				|| !Back->GetIsEnabled() || !ReadChoices[3]->GetIsEnabled() || ReadChoices[3]->GetSelectedIndex() != 0
				|| ReadChoices[4]->GetSelectedIndex() != 0 || ReadChoices[9]->GetSelectedIndex() != 1
				|| ReadChoices[10]->GetSelectedIndex() != 2 || ReadChoices[16]->GetSelectedIndex() != 0)
				return Fail(TEXT("ReadFailuresConfirmedRetryDidNotRecover"));
			ReadChoices[3]->SetSelectedIndex(3); ReadChoices[4]->SetSelectedIndex(1);
			ReadChoices[9]->SetSelectedIndex(2); ReadChoices[16]->SetSelectedIndex(3);
			if (!Save->GetIsEnabled()) return Fail(TEXT("ReadFailuresCannotEditAfterConfirmedRead"));
			Stage = 56;
			Save->OnClicked.Broadcast();
			if (!Configuration->IsRequestInFlight()) return Fail(TEXT("ReadFailuresRecoveredSaveNotSubmitted"));
			return true;
		}
		if (Stage == 56)
		{
			if (Configuration->HasUnsavedChanges() || Configuration->HasUnresolvedSave() || !Back->GetIsEnabled())
				return Fail(TEXT("ReadFailuresRecoveredSaveNotApplied"));
			UE_LOG(LogTemp, Display, TEXT("LOBBY_READ_FAILURES PASS Catalog503=1 EmptyCatalog=1 Backend403=1 Backend404=1 Preserved=1 Recovered=1"));
			Stage = 4;
			return true;
		}
		return Fail(TEXT("ReadFailuresUnexpectedReadStage"));
	}
	if (bRecovery && Stage >= 20)
	{
		if (Auth->IsLoginRequestInFlight() || Configuration->IsRequestInFlight() || Flow->IsRequestInFlight()) return true;
		TArray<UComboBoxString*> RecoveryChoices;
		Configuration->WidgetTree->ForEachWidget([&](UWidget* Child)
		{ if (auto* Box = Cast<UComboBoxString>(Child)) RecoveryChoices.Add(Box); });
		if (RecoveryChoices.Num() != 17 || Flow->GetSelectedCharacterId() != CharacterId
			|| Web->GetAuthenticatedSteamId() != SteamId || !Flow->GetCurrentDungeonSession().DungeonSessionId.IsEmpty())
			return Fail(TEXT("ConfigurationRecoveryContextLost"));
		const bool bDraftPreserved = Configuration->HasUnsavedChanges()
			&& RecoveryChoices[3]->GetSelectedIndex() == 3 && RecoveryChoices[4]->GetSelectedIndex() == 1
			&& RecoveryChoices[9]->GetSelectedIndex() == 2 && RecoveryChoices[10]->GetSelectedIndex() == 1
			&& RecoveryChoices[16]->GetSelectedIndex() == 3;
		const auto Ready = [&](const TCHAR* Name) { return IFileManager::Get().FileExists(*(Gates / Name)); };
		if (Stage == 20)
		{
			if (!Ready(TEXT("read-revoked.ready"))) return true;
			Stage = 21;
			Load->OnClicked.Broadcast();
			if (!Configuration->IsRequestInFlight()) return Fail(TEXT("ConfigurationExpiredReadNotSubmitted"));
			return true;
		}
		if (Stage == 21 || Stage == 25)
		{
			const bool bPendingExpected = Stage == 25;
			if (Web->HasAccessToken() || !bDraftPreserved || Configuration->HasUnresolvedSave() != bPendingExpected
				|| !Login->GetIsEnabled() || Account->GetIsEnabled() || Account->GetText().ToString() != SteamId
				|| Back->GetIsEnabled() || Save->GetIsEnabled())
				return Fail(TEXT("ConfigurationUnauthorizedLostDraftOrLogin"));
			UE_LOG(LogTemp, Display, TEXT("LOBBY_CONFIGURATION_RECOVERY REAUTHENTICATE Pending=%d DraftPreserved=1 SameCharacter=1"), bPendingExpected);
			++Stage;
			Login->OnClicked.Broadcast();
			return true;
		}
		if (Stage == 22 || Stage == 26)
		{
			if (!Web->HasAccessToken()) return Fail(TEXT("ConfigurationRecoveryLoginFailed"));
			if (!Save->GetIsEnabled()) return true; // real owned-roster reload must finish
			if (!bDraftPreserved || Configuration->HasUnresolvedSave() != (Stage == 26) || Back->GetIsEnabled())
				return Fail(TEXT("ConfigurationReauthenticationLostDraftOrCommand"));
			++Stage;
			Save->OnClicked.Broadcast();
			if (!Configuration->IsRequestInFlight() || !Configuration->HasUnresolvedSave()
				|| Save->GetIsEnabled() || Load->GetIsEnabled() || Reset->GetIsEnabled())
				return Fail(TEXT("ConfigurationRecoveryWriteNotLocked"));
			return true;
		}
		if (Stage == 23)
		{
			auto* Logout = Cast<UButton>(Widget->GetWidgetFromName(TEXT("LogoutBackendButton")));
			if (!Web->HasAccessToken() || !bDraftPreserved || !Configuration->HasUnresolvedSave()
				|| !Save->GetIsEnabled() || Load->GetIsEnabled() || Reset->GetIsEnabled()
				|| Back->GetIsEnabled() || !Logout || Logout->GetIsEnabled())
				return Fail(TEXT("ConfigurationUnknownOutcomeNotPreserved"));
			UE_LOG(LogTemp, Display, TEXT("LOBBY_CONFIGURATION_RECOVERY UNKNOWN DraftPreserved=1 Pending=1 NavigationLocked=1"));
			Stage = 24;
			return true;
		}
		if (Stage == 24)
		{
			if (!Ready(TEXT("write-revoked.ready"))) return true;
			Stage = 25;
			Save->OnClicked.Broadcast();
			if (!Configuration->IsRequestInFlight()) return Fail(TEXT("ConfigurationOriginalRetryNotSubmitted"));
			return true;
		}
		if (Stage == 27)
		{
			if (Configuration->HasUnresolvedSave() || Configuration->HasUnsavedChanges()
				|| RecoveryChoices[3]->GetSelectedIndex() != 3 || RecoveryChoices[4]->GetSelectedIndex() != 1
				|| !Back->GetIsEnabled()) return Fail(TEXT("ConfigurationRecoveredReceiptNotApplied"));
			UE_LOG(LogTemp, Display, TEXT("LOBBY_CONFIGURATION_RECOVERY RECOVERED Read401Draft=1 UnknownWrite401=1 SameAccountReauthentication=2"));
			Stage = 4; // resume the existing reload/sibling/return-to-saved checks
			return true;
		}
		return Fail(TEXT("ConfigurationRecoveryUnexpectedStage"));
	}
	if (bConflict && Stage >= 30)
	{
		if (Auth->IsLoginRequestInFlight() || Configuration->IsRequestInFlight() || Flow->IsRequestInFlight()) return true;
		TArray<UComboBoxString*> ConflictChoices;
		Configuration->WidgetTree->ForEachWidget([&](UWidget* Child)
		{ if (auto* Box = Cast<UComboBoxString>(Child)) ConflictChoices.Add(Box); });
		if (ConflictChoices.Num() != 17 || !Web->HasAccessToken() || Web->GetAuthenticatedSteamId() != SteamId
			|| Flow->GetSelectedCharacterId() != CharacterId || !Flow->GetCurrentDungeonSession().DungeonSessionId.IsEmpty())
			return Fail(TEXT("ConfigurationConflictContextLost"));
		const auto Ready = [&](const TCHAR* Name) { return IFileManager::Get().FileExists(*(Gates / Name)); };
		if (Stage == 30)
		{
			if (!Ready(TEXT("competitor-stored.ready"))) return true;
			Stage = 31;
			Save->OnClicked.Broadcast();
			if (!Configuration->IsRequestInFlight() || !Configuration->HasUnresolvedSave())
				return Fail(TEXT("ConfigurationConflictingWriteNotSubmitted"));
			return true;
		}
		if (Stage == 31)
		{
			auto* Notice = Cast<UTextBlock>(Configuration->GetWidgetFromName(TEXT("ConfigurationResult")));
			if (Configuration->HasUnresolvedSave() || !Configuration->HasUnsavedChanges() || !Load->GetIsEnabled()
				|| Save->GetIsEnabled() || Reset->GetIsEnabled() || Back->GetIsEnabled() || ConflictChoices[3]->GetIsEnabled()
				|| ConflictChoices[3]->GetSelectedIndex() != 3 || ConflictChoices[4]->GetSelectedIndex() != 1
				|| ConflictChoices[9]->GetSelectedIndex() != 2 || ConflictChoices[10]->GetSelectedIndex() != 1
				|| ConflictChoices[16]->GetSelectedIndex() != 3 || !Notice || Notice->GetText().IsEmpty())
				return Fail(TEXT("ConfigurationConflictDidNotPreserveAndLockDraft"));
			const auto Charge = FGameplayTag::RequestGameplayTag(TEXT("Player.Ability.Skill.Charge"));
			if (Configuration->SetSkillLevel(Charge, 1)) return Fail(TEXT("ConfigurationConflictAllowedEditBeforeReload"));
			Save->OnClicked.Broadcast(); // forced dispatcher must not turn denial into a fresh write
			if (Configuration->IsRequestInFlight() || Configuration->HasUnresolvedSave())
				return Fail(TEXT("ConfigurationConflictAllowedWriteBeforeReload"));
			UE_LOG(LogTemp, Display, TEXT("LOBBY_CONFIGURATION_CONFLICT BLOCKED DraftPreserved=1 ReloadRequired=1 ForcedRetryLocked=1"));
			Stage = 32;
			return true;
		}
		if (Stage == 32)
		{
			if (!Ready(TEXT("competitor-preserved.ready"))) return true;
			Stage = 33;
			Load->OnClicked.Broadcast();
			if (!Configuration->IsRequestInFlight()) return Fail(TEXT("ConfigurationConflictReloadNotSubmitted"));
			return true;
		}
		if (Stage == 33)
		{
			if (Configuration->HasUnsavedChanges() || Configuration->HasUnresolvedSave() || Save->GetIsEnabled()
				|| Reset->GetIsEnabled() || !Back->GetIsEnabled() || !ConflictChoices[3]->GetIsEnabled()
				|| ConflictChoices[3]->GetSelectedIndex() != 3 || ConflictChoices[4]->GetSelectedIndex() != 2
				|| ConflictChoices[9]->GetSelectedIndex() != 1 || ConflictChoices[10]->GetSelectedIndex() != 2
				|| ConflictChoices[16]->GetSelectedIndex() != 0)
				return Fail(TEXT("ConfigurationConflictReloadDidNotShowLatestServerRecord"));
			ConflictChoices[4]->SetSelectedIndex(1);
			ConflictChoices[9]->SetSelectedIndex(2);
			ConflictChoices[16]->SetSelectedIndex(3);
			if (!Configuration->HasUnsavedChanges() || !Save->GetIsEnabled() || Back->GetIsEnabled())
				return Fail(TEXT("ConfigurationConflictCouldNotEditLatestRecord"));
			UE_LOG(LogTemp, Display, TEXT("LOBBY_CONFIGURATION_CONFLICT RELOADED LatestVariant=1 ExplicitReedit=1"));
			Stage = 34;
			Save->OnClicked.Broadcast();
			if (!Configuration->IsRequestInFlight() || !Configuration->HasUnresolvedSave())
				return Fail(TEXT("ConfigurationConflictNewWriteNotSubmitted"));
			return true;
		}
		if (Stage == 34)
		{
			if (Configuration->HasUnsavedChanges() || Configuration->HasUnresolvedSave() || Save->GetIsEnabled()
				|| !Back->GetIsEnabled() || ConflictChoices[4]->GetSelectedIndex() != 1)
				return Fail(TEXT("ConfigurationConflictResolutionNotApplied"));
			UE_LOG(LogTemp, Display, TEXT("LOBBY_CONFIGURATION_CONFLICT RESOLVED ServerConflict=1 Reloaded=1 ExplicitNewIntent=1"));
			Stage = 4;
			return true;
		}
		return Fail(TEXT("ConfigurationConflictUnexpectedStage"));
	}
	if (Auth->IsLoginRequestInFlight() || Widget->IsFrontendRequestInFlight() || Flow->IsRequestInFlight()) return true;
	if (!Web->HasAccessToken() || Web->GetAuthenticatedSteamId() != SteamId
		|| !Flow->GetCurrentDungeonSession().DungeonSessionId.IsEmpty())
		return Fail(TEXT("ConfigurationLostLobbyContext"));
	if (Stage == 1)
	{
		if (!Characters->GetIsEnabled()) return true;
		const FString First = Widget->GetCharacterOption(CharacterId), Other = Widget->GetCharacterOption(ConfigurationOtherCharacterId);
		if (Characters->GetOptionCount() != 2 || First.IsEmpty() || Other.IsEmpty())
			return Fail(TEXT("ConfigurationOwnedRosterMismatch"));
		if (bReadFailures) { Stage = 40; return true; }
		Stage = 2;
		Characters->SetSelectedOption(First);
		return true;
	}
	if (Stage == 2 || Stage == 6 || Stage == 8)
	{
		if (!Continue->GetIsEnabled()) return true;
		const FString& ExpectedCharacter = Stage == 6 ? ConfigurationOtherCharacterId : CharacterId;
		if (Flow->GetSelectedCharacterId() != ExpectedCharacter) return Fail(TEXT("ConfigurationSelectedCharacterMismatch"));
		Continue->OnClicked.Broadcast();
		if (!Load->GetIsEnabled() || Configuration->GetVisibility() == ESlateVisibility::Collapsed)
			return Fail(TEXT("ConfigurationPreparationUnavailable"));
		if (bPreview)
		{
			bool bHasOldPreview = false;
			Configuration->WidgetTree->ForEachWidget([&](UWidget* Child)
			{
				if (const auto* Text = Cast<UTextBlock>(Child))
					bHasOldPreview |= Text->GetText().ToString().StartsWith(TEXT("기본 상태 미리보기:"));
			});
			if (bHasOldPreview || Configuration->GetSkillPreview(FGameplayTag::RequestGameplayTag(
				TEXT("Player.Ability.Skill.Charge"))).ToString().StartsWith(TEXT("기본 상태 미리보기:")))
				return Fail(TEXT("ConfigurationPreviewLeakedBeforeRead"));
		}
		++Stage;
		Load->OnClicked.Broadcast();
		if (!Configuration->IsRequestInFlight() || !Web->IsPlayerProgressionRequestInFlight()
			|| Load->GetIsEnabled() || Save->GetIsEnabled() || Back->GetIsEnabled())
			return Fail(TEXT("ConfigurationReadDidNotLockActions"));
		Load->OnClicked.Broadcast();
		return true;
	}

	// The fixture's confirmed recipe has three acquired skills in server order,
	// or two for the untouched sibling. Drive their actual dynamic UI controls.
	if (bPreview && !GI->GetSubsystem<URPGSkillCatalogSubsystem>()->IsCatalogReady()) return true;
	TArray<UComboBoxString*> Choices;
	Configuration->WidgetTree->ForEachWidget([&Choices](UWidget* Child)
	{ if (auto* Box = Cast<UComboBoxString>(Child)) Choices.Add(Box); });
	const int32 SkillCount = Stage == 7 ? 2 : 3;
	if (Choices.Num() != SkillCount * 3 + 8) return Fail(TEXT("ConfigurationLoadedChoicesMismatch"));
	const auto Charge = FGameplayTag::RequestGameplayTag(TEXT("Player.Ability.Skill.Charge"));
	const auto PreviewRowsMatch = [&]()
	{
		TArray<FString> Visible;
		Configuration->WidgetTree->ForEachWidget([&](UWidget* Child)
		{
			if (const auto* Text = Cast<UTextBlock>(Child))
			{
				const FString Value = Text->GetText().ToString();
				if (Value.StartsWith(TEXT("기본 상태 미리보기:"))) Visible.Add(Value);
			}
		});
		if (Visible.Num() != SkillCount) return false;
		const TCHAR* Tags[] = { TEXT("Player.Ability.Skill.AssultBlade"), TEXT("Player.Ability.Skill.Charge"),
			TEXT("Player.Ability.Skill.WhirlWind") };
		for (int32 Index = 0; Index < SkillCount; ++Index)
		{
			const FString Expected = Configuration->GetSkillPreview(FGameplayTag::RequestGameplayTag(Tags[Index])).ToString();
			const int32 Found = Visible.Find(Expected);
			if (Found == INDEX_NONE) return false;
			Visible.RemoveAt(Found);
		}
		return Visible.IsEmpty();
	};
	const auto HasChargeTime = [&](float Seconds)
	{
		const FString Expected = FText::Format(FText::FromString(TEXT("완충 {0}초")), FText::AsNumber(Seconds)).ToString();
		return Configuration->GetSkillPreview(Charge).ToString().Contains(Expected) && PreviewRowsMatch();
	};
	if (bPreview && !PreviewRowsMatch()) return Fail(TEXT("ConfigurationVisiblePreviewDoesNotMatchCurrentDraft"));
	auto* AssaultLevel = Choices[0];
	auto* ChargeLevel = Choices[3];
	auto* ChargeTier0 = Choices[4];
	auto* ChargeTier1 = Choices[5];
	auto* Slot0 = Choices[SkillCount * 3];
	auto* Slot1 = Choices[SkillCount * 3 + 1];
	auto* Slot7 = Choices[SkillCount * 3 + 7];
	const auto MatchesSaved = [&]()
	{
		return ChargeLevel->GetSelectedIndex() == 3 && ChargeTier0->GetSelectedIndex() == 1
			&& Slot0->GetSelectedIndex() == 2 && Slot1->GetSelectedIndex() == 1 && Slot7->GetSelectedIndex() == 3
			&& !Configuration->HasUnsavedChanges() && !Configuration->HasUnresolvedSave()
			&& !Save->GetIsEnabled() && Back->GetIsEnabled();
	};
	const auto Edit = [&]()
	{
		ChargeLevel->SetSelectedIndex(3);
		ChargeTier0->SetSelectedIndex(1);
		Slot0->SetSelectedIndex(2); // swap Charge with Assault
		Slot7->SetSelectedIndex(3); // learned Whirlwind, preserve the other empty slots
	};
	if (Stage == 3)
	{
		const FString InitialChargePreview = Configuration->GetSkillPreview(Charge).ToString();
		if (ChargeLevel->GetSelectedIndex() != 0 || ChargeTier0->GetIsEnabled() || ChargeTier1->GetIsEnabled()
			|| Save->GetIsEnabled() || Reset->GetIsEnabled() || !Back->GetIsEnabled())
			return Fail(TEXT("ConfigurationInitialRecordMismatch"));
		ChargeLevel->SetSelectedIndex(3);
		if (Back->GetIsEnabled() || !Configuration->HasUnsavedChanges() || !ChargeTier0->GetIsEnabled())
			return Fail(TEXT("ConfigurationDraftNavigationNotRefreshed"));
		AssaultLevel->SetSelectedIndex(1); // would exceed the confirmed three SP
		if (AssaultLevel->GetSelectedIndex() != 0 || ChargeTier1->GetIsEnabled()
			|| Configuration->SetTripodChoice(Charge, 1, TEXT("Stagger")))
			return Fail(TEXT("ConfigurationEligibilityGuardFailed"));
		ChargeTier0->SetSelectedIndex(1);
		if (bPreview)
		{
			if (!HasChargeTime(1.2f)) return Fail(TEXT("ConfigurationFullChargePreviewNotRefreshed"));
			ChargeTier0->SetSelectedIndex(2);
			if (!HasChargeTime(.7f)) return Fail(TEXT("ConfigurationQuickChargePreviewNotRefreshed"));
			ChargeTier0->SetSelectedIndex(1);
			if (!HasChargeTime(1.2f)) return Fail(TEXT("ConfigurationFullChargePreviewNotRestored"));
		}
		ChargeLevel->SetSelectedIndex(2); // lowering below four clears the choice
		if (ChargeTier0->GetSelectedIndex() != 0 || ChargeTier0->GetIsEnabled())
			return Fail(TEXT("ConfigurationLockedTripodNotCleared"));
		ChargeLevel->SetSelectedIndex(3);
		if (ChargeTier0->GetSelectedIndex() != 0) return Fail(TEXT("ConfigurationTripodAutoRestored"));
		Edit();
		Reset->OnClicked.Broadcast();
		if (Configuration->HasUnsavedChanges() || ChargeLevel->GetSelectedIndex() != 0
			|| ChargeTier0->GetSelectedIndex() != 0 || Slot0->GetSelectedIndex() != 1
			|| Slot1->GetSelectedIndex() != 2 || Slot7->GetSelectedIndex() != 0 || !Back->GetIsEnabled())
			return Fail(TEXT("ConfigurationResetDidNotRestoreSavedDraft"));
		if (bPreview && (Configuration->GetSkillPreview(Charge).ToString() != InitialChargePreview || !PreviewRowsMatch()))
			return Fail(TEXT("ConfigurationResetDidNotRestoreVisiblePreview"));
		Edit();
		if (bPreview)
		{
			if (!HasChargeTime(1.2f)) return Fail(TEXT("ConfigurationEditedPreviewLostBeforeSave"));
			UE_LOG(LogTemp, Display, TEXT("LOBBY_SKILL_PREVIEW_E2E DRAFT VisibleRows=3 FullCharge=1 QuickCharge=1 Reset=1 NoSaveFromPreview=1"));
		}
		if (!Save->GetIsEnabled() || !Reset->GetIsEnabled() || Back->GetIsEnabled())
			return Fail(TEXT("ConfigurationEditedControlsMismatch"));
		if (bReadFailures)
		{
			UE_LOG(LogTemp, Display, TEXT("LOBBY_READ_FAILURES DRAFT_READY SameCharacter=1 Dirty=1"));
			Stage = 50;
			return true;
		}
		if (bRecovery || bConflict)
		{
			if (bConflict)
			{
				UE_LOG(LogTemp, Display, TEXT("LOBBY_CONFIGURATION_CONFLICT DRAFT_READY Dirty=1 SameCharacter=1"));
			}
			else
			{
				UE_LOG(LogTemp, Display, TEXT("LOBBY_CONFIGURATION_RECOVERY DRAFT_READY Dirty=1 SameCharacter=1"));
			}
			Stage = bConflict ? 30 : 20;
			return true;
		}
		Stage = 4;
		Save->OnClicked.Broadcast();
		if (!Configuration->IsRequestInFlight() || !Configuration->HasUnresolvedSave()
			|| Save->GetIsEnabled() || Load->GetIsEnabled() || Reset->GetIsEnabled() || Back->GetIsEnabled()
			|| Configuration->SetSkillLevel(Charge, 1)) return Fail(TEXT("ConfigurationWriteDidNotLockActions"));
		Save->OnClicked.Broadcast();
		UE_LOG(LogTemp, Display, TEXT("LOBBY_CONFIGURATION_E2E SAVING SPGuard=1 TripodUnlock=1 LowerClears=1 Reset=1 SlotSwap=1 PendingLocked=1"));
		return true;
	}
	if (Stage == 4)
	{
		if (!MatchesSaved()) return Fail(TEXT("ConfigurationSavedUiMismatch"));
		if (bPreview && !HasChargeTime(1.2f)) return Fail(TEXT("ConfigurationSavedPreviewMismatch"));
		Stage = 5;
		Load->OnClicked.Broadcast();
		if (!Configuration->IsRequestInFlight() || !Web->IsPlayerProgressionRequestInFlight())
			return Fail(TEXT("ConfigurationReloadNotSubmitted"));
		return true;
	}
	if (Stage == 5 || Stage == 9)
	{
		if (!MatchesSaved()) return Fail(TEXT("ConfigurationReloadDidNotPreserveSavedValues"));
		if (bPreview && !HasChargeTime(1.2f)) return Fail(TEXT("ConfigurationReloadedPreviewMismatch"));
		if (Stage == 9)
		{
			if (bPreview) UE_LOG(LogTemp, Display, TEXT("LOBBY_SKILL_PREVIEW_E2E PASS SavedAndReloaded=1 SiblingIndependent=1 ReturnedPreview=1 ClearedBeforeRead=1"));
			UE_LOG(LogTemp, Display, TEXT("LOBBY_CONFIGURATION_E2E PASS LiveMap=1 SavedAndReloaded=1 SiblingIndependent=1 ReturnedToSavedCharacter=1 NoDungeonCreatedInUI=1"));
			Stage = 10;
			FPlatformMisc::RequestExit(false);
			return false;
		}
		ChargeLevel->SetSelectedIndex(2);
		Reset->OnClicked.Broadcast();
		if (!MatchesSaved()) return Fail(TEXT("ConfigurationResetAfterSaveMismatch"));
		if (bPreview && !HasChargeTime(1.2f)) return Fail(TEXT("ConfigurationResetAfterSavePreviewMismatch"));
		Back->OnClicked.Broadcast();
		if (!Characters->GetIsEnabled()) return Fail(TEXT("ConfigurationCannotReturnToRoster"));
		Stage = 6;
		Characters->SetSelectedOption(Widget->GetCharacterOption(ConfigurationOtherCharacterId));
		return true;
	}
	if (Stage == 7)
	{
		if (bPreview)
		{
			FRPGSavedProgressionSkill SiblingCharge;
			SiblingCharge.SkillTag = Charge;
			SiblingCharge.Level = 1;
			SiblingCharge.TripodIds = { TEXT(""), TEXT("") };
			const FText Expected = RPGCharacterConfigurationPreview::Describe(
				GI->GetSubsystem<URPGSkillCatalogSubsystem>()->FindSkillDefinition(Charge), SiblingCharge);
			if (Configuration->GetSkillPreview(Charge).ToString() != Expected.ToString())
				return Fail(TEXT("ConfigurationSavedChargePreviewLeakedToSibling"));
		}
		if (ChargeLevel->GetSelectedIndex() != 0 || Slot0->GetSelectedIndex() != 1 || Slot1->GetSelectedIndex() != 2
			|| Slot7->GetSelectedIndex() != 0 || Save->GetIsEnabled() || Configuration->HasUnsavedChanges()
			|| Configuration->HasUnresolvedSave()) return Fail(TEXT("ConfigurationDraftLeakedToSibling"));
		ChargeLevel->SetSelectedIndex(3); // sibling has zero SP
		if (ChargeLevel->GetSelectedIndex() != 0 || Configuration->HasUnsavedChanges())
			return Fail(TEXT("ConfigurationSiblingSPGuardFailed"));
		Back->OnClicked.Broadcast();
		Stage = 8;
		Characters->SetSelectedOption(Widget->GetCharacterOption(CharacterId));
		return true;
	}
	return Fail(TEXT("ConfigurationUnexpectedStage"));
}
