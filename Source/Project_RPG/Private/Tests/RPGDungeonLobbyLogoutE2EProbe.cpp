#include "Tests/RPGDungeonFlowE2EProbe.h"

#include "Components/Button.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "Interfaces/IHttpResponse.h"
#include "Manager/HttpWebManager.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"
#include "Online/RPGDungeonSessionSubsystem.h"
#include "Online/RPGOnlineLobbySubsystem.h"
#include "Online/RPGSteamAuthSubsystem.h"
#include "UI/Lobby/RPGOnlineLobbyWidget.h"

void URPGDungeonFlowE2EClient::HandleLobbyLogout(const bool bSuccess)
{
	if (!bVerifyLobbyLogout || Stage != 3 || !bSuccess || ++LogoutCompletionCount != 1)
	{
		Fail(TEXT("LobbyLogoutCompletionMismatch"));
		return;
	}
}

bool URPGDungeonFlowE2EClient::TickLobbyLogoutProbe(UWorld* World, APlayerController* PC)
{
	UGameInstance* GI = GetGameInstance();
	auto* Web = GI->GetSubsystem<UHttpWebManager>();
	auto* Flow = GI->GetSubsystem<URPGDungeonSessionSubsystem>();
	auto* Auth = GI->GetSubsystem<URPGSteamAuthSubsystem>();
	auto* Lobby = GI->GetSubsystem<URPGOnlineLobbySubsystem>();
	auto* Widget = Lobby ? Lobby->GetLobbyWidget() : nullptr;
	if (!Web || !Flow || !Auth || !Lobby) return Fail(TEXT("LobbyLogoutSubsystemMissing"));
	if (!Widget || !Lobby->IsLobbyController(PC)) return true;
	auto* Login = Cast<UButton>(Widget->GetWidgetFromName(TEXT("LoginDevelopmentButton")));
	auto* Logout = Cast<UButton>(Widget->GetWidgetFromName(TEXT("LogoutBackendButton")));
	auto* Account = Cast<UEditableTextBox>(Widget->GetWidgetFromName(TEXT("DevelopmentSteamId")));
	auto* Characters = Cast<UComboBoxString>(Widget->GetWidgetFromName(TEXT("OwnedCharacters")));
	auto* NewName = Cast<UEditableTextBox>(Widget->GetWidgetFromName(TEXT("NewCharacterName")));
	auto* JoinId = Cast<UEditableTextBox>(Widget->GetWidgetFromName(TEXT("DevelopmentSessionId")));
	if (!Login || !Logout || !Account || !Characters || !NewName || !JoinId)
		return Fail(TEXT("LobbyLogoutNativeControlsMissing"));
	if (Stage == 0)
	{
		FString ApiUrl;
		FGuid Parsed;
		GConfig->GetString(TEXT("/Script/Project_RPG.HttpWebManager"), TEXT("ApiUrl"), ApiUrl, GGameIni);
		if (!bLobbyUI || bAbandonmentUI || bCreateCharacter || bReauthenticate || bReauthenticateAfterAdmission
			|| !FParse::Param(FCommandLine::Get(), TEXT("RPGDevelopmentLogin"))
			|| !ApiUrl.StartsWith(TEXT("http://127.0.0.1:")) || !ApiUrl.EndsWith(TEXT("/api"))
			|| !FRPGBackendPlayerAuthState::IsSteamId(SteamId) || !FRPGBackendPlayerAuthState::IsSteamId(LogoutNextSteamId)
			|| SteamId == LogoutNextSteamId || !FGuid::Parse(CharacterId, Parsed) || !Parsed.IsValid()
			|| !FGuid::Parse(LogoutNextCharacterId, Parsed) || !Parsed.IsValid() || CharacterId == LogoutNextCharacterId)
			return Fail(TEXT("LobbyLogoutInvalidLocalFixture"));
		if (!Login->GetIsEnabled() || !Account->GetIsEnabled()) return true;
		LogoutCredentialProbe = FHttpModule::Get().CreateRequest();
		LogoutCredentialProbe->SetURL(ApiUrl / TEXT("characters"));
		LogoutCredentialProbe->SetVerb(TEXT("GET"));
		LogoutCredentialProbe->SetTimeout(10.f);
		LogoutCredentialProbe->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnGameThread);
		Account->SetText(FText::FromString(SteamId));
		Stage = 1;
		Login->OnClicked.Broadcast();
		return true;
	}
	if (Stage == 1)
	{
		if (Auth->IsLoginRequestInFlight() || Widget->IsFrontendRequestInFlight() || !Characters->GetIsEnabled()) return true;
		const FString Option = Widget->GetCharacterOption(CharacterId);
		if (!Web->HasAccessToken() || Web->GetAuthenticatedSteamId() != SteamId || Option.IsEmpty()
			|| Characters->GetOptionCount() != 1 || !Widget->GetCharacterOption(LogoutNextCharacterId).IsEmpty())
			return Fail(TEXT("LobbyLogoutInitialRosterMismatch"));
		Stage = 2;
		Characters->SetSelectedOption(Option); // actual selection handler -> authenticated active-membership GET
		return true;
	}
	if (Stage == 2)
	{
		if (Flow->IsRequestInFlight() || Widget->IsFrontendRequestInFlight() || !Logout->GetIsEnabled()) return true;
		if (Flow->GetSelectedCharacterId() != CharacterId || !Flow->GetCurrentDungeonSession().DungeonSessionId.IsEmpty()
			|| Account->GetIsEnabled()) return Fail(TEXT("LobbyLogoutInitialSelectionMismatch"));
		Web->ApplyCommonHeaders(LogoutCredentialProbe.ToSharedRef()); // retained in memory only, never logged or written
		NewName->SetText(FText::FromString(TEXT("ClearOnConfirmedLogout")));
		JoinId->SetText(FText::FromString(TEXT("clear-on-confirmed-logout")));
		Stage = 3;
		Logout->OnClicked.Broadcast();
		if (!Web->IsPlayerLogoutInFlight() || !Flow->IsRequestInFlight() || Web->HasAccessToken()
			|| Logout->GetIsEnabled() || Login->GetIsEnabled() || Account->GetIsEnabled())
			return Fail(TEXT("LobbyLogoutDidNotLockConcurrentActions"));
		Logout->OnClicked.Broadcast();
		Login->OnClicked.Broadcast(); // forced dispatchers still must respect the live guard
		UE_LOG(LogTemp, Display, TEXT("LOBBY_LOGOUT_E2E REQUEST LiveMap=1 LiveButton=1 SelectedCharacter=1 DuplicateAndLoginLocked=1"));
		return true;
	}
	if (Stage == 3)
	{
		if (Web->IsPlayerLogoutInFlight() || LogoutCompletionCount == 0) return true;
		if (LogoutCompletionCount != 1 || Web->HasAccessToken() || Web->HasPlayerLogoutCredential()
			|| !Web->GetAuthenticatedSteamId().IsEmpty() || !Flow->GetSelectedCharacterId().IsEmpty()
			|| !Flow->GetCurrentDungeonSession().DungeonSessionId.IsEmpty() || Characters->GetOptionCount() != 0
			|| !NewName->GetText().IsEmpty() || !JoinId->GetText().IsEmpty() || !Account->GetText().IsEmpty()
			|| !Account->GetIsEnabled() || !Login->GetIsEnabled() || Logout->GetIsEnabled())
			return Fail(TEXT("LobbyLogoutConfirmedUiNotReset"));
		Stage = 4;
		LogoutCredentialProbe->OnProcessRequestComplete().BindWeakLambda(this,
			[this](FHttpRequestPtr, FHttpResponsePtr Response, bool bSuccess)
			{
				if (Stage != 4 || !bSuccess || !Response.IsValid() || Response->GetResponseCode() != 401)
				{
					Fail(TEXT("LobbyLogoutOldCredentialNotRejected"));
					return;
				}
				bRetiredLogoutCredentialRejected = true;
			});
		if (!LogoutCredentialProbe->ProcessRequest()) return Fail(TEXT("LobbyLogoutRevocationCheckNotSubmitted"));
		return true;
	}
	if (Stage == 4)
	{
		if (!bRetiredLogoutCredentialRejected) return true;
		LogoutCredentialProbe->OnProcessRequestComplete().Unbind();
		LogoutCredentialProbe.Reset();
		UE_LOG(LogTemp, Display, TEXT("LOBBY_LOGOUT_E2E CONFIRMED BackendRevoked=1 OldTokenHttp401=1 AccountAndSelectionCleared=1 RosterAndInputsCleared=1"));
		Account->SetText(FText::FromString(LogoutNextSteamId));
		Stage = 5;
		Login->OnClicked.Broadcast();
		return true;
	}
	if (Stage == 5)
	{
		if (Auth->IsLoginRequestInFlight() || Widget->IsFrontendRequestInFlight() || !Characters->GetIsEnabled()) return true;
		const FString Option = Widget->GetCharacterOption(LogoutNextCharacterId);
		if (!Web->HasAccessToken() || Web->GetAuthenticatedSteamId() != LogoutNextSteamId || Option.IsEmpty()
			|| Characters->GetOptionCount() != 1 || !Widget->GetCharacterOption(CharacterId).IsEmpty()
			|| !Flow->GetSelectedCharacterId().IsEmpty()) return Fail(TEXT("LobbyLogoutNextAccountRosterMismatch"));
		Stage = 6;
		Characters->SetSelectedOption(Option);
		return true;
	}
	if (Stage == 6)
	{
		if (Flow->IsRequestInFlight() || Widget->IsFrontendRequestInFlight() || !Logout->GetIsEnabled()) return true;
		if (Flow->GetSelectedCharacterId() != LogoutNextCharacterId
			|| !Flow->GetCurrentDungeonSession().DungeonSessionId.IsEmpty() || LogoutCompletionCount != 1)
			return Fail(TEXT("LobbyLogoutNextAccountSelectionMismatch"));
		UE_LOG(LogTemp, Display, TEXT("LOBBY_LOGOUT_E2E PASS SameProcess=1 OtherDevelopmentAccountLogin=1 OwnedRosterOnly=1 NewSelection=1 NoDungeonOrPartyCreated=1"));
		Stage = 7;
		FPlatformMisc::RequestExit(false);
		return false;
	}
	return true;
}
