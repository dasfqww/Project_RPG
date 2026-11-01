#include "Online/RPGSteamAuthSubsystem.h"

#include "Manager/HttpWebManager.h"
#include "Engine/GameInstance.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemNames.h"

void URPGSteamAuthSubsystem::Deinitialize()
{
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UHttpWebManager* WebManager =
			GameInstance->GetSubsystem<UHttpWebManager>())
		{
			WebManager->OnBackendAuthenticationCompleted.RemoveDynamic(
				this, &ThisClass::HandleBackendAuthenticationCompleted);
		}
	}

	bLoginRequestInFlight = false;
	OnLoginCompleted.Clear();
	Super::Deinitialize();
}

void URPGSteamAuthSubsystem::LoginToBackendWithSteam(const int32 LocalUserNum)
{
	if (bLoginRequestInFlight) return;
	if (const auto* Web = GetGameInstance() ? GetGameInstance()->GetSubsystem<UHttpWebManager>() : nullptr)
		if (Web->IsPlayerLogoutInFlight()) return;
	if (IsRunningDedicatedServer()
		|| RemoteServiceIdentity.IsEmpty())
	{
		CompleteLogin(false);
		return;
	}

	IOnlineSubsystem* SteamSubsystem = IOnlineSubsystem::Get(STEAM_SUBSYSTEM);
	IOnlineIdentityPtr IdentityInterface = SteamSubsystem
		? SteamSubsystem->GetIdentityInterface()
		: nullptr;
	if (!IdentityInterface.IsValid())
	{
		UE_LOG(LogTemp, Error,
			TEXT("OnlineSubsystemSteam identity interface is unavailable."));
		CompleteLogin(false);
		return;
	}

	bLoginRequestInFlight = true;
	const FString TokenType = FString::Printf(
		TEXT("WebAPI:%s"), *RemoteServiceIdentity);
	IdentityInterface->GetLinkedAccountAuthToken(
		LocalUserNum,
		TokenType,
		IOnlineIdentity::FOnGetLinkedAccountAuthTokenCompleteDelegate::CreateUObject(
			this, &ThisClass::HandleSteamTicketReceived));
}

void URPGSteamAuthSubsystem::LoginToDevelopmentBackend(const FString& SteamId)
{
	if (bLoginRequestInFlight) return;
	if (const auto* Web = GetGameInstance() ? GetGameInstance()->GetSubsystem<UHttpWebManager>() : nullptr)
		if (Web->IsPlayerLogoutInFlight()) return;
#if UE_BUILD_SHIPPING
	UE_LOG(LogTemp, Error,
		TEXT("Development backend authentication is disabled in Shipping builds."));
	CompleteLogin(false);
#else
	if (IsRunningDedicatedServer() || !FRPGBackendPlayerAuthState::IsSteamId(SteamId))
	{
		CompleteLogin(false);
		return;
	}
	const UHttpWebManager* Web = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UHttpWebManager>() : nullptr;
	if (Web && !Web->GetAuthenticatedSteamId().IsEmpty()
		&& !Web->GetAuthenticatedSteamId().Equals(SteamId, ESearchCase::CaseSensitive))
	{
		CompleteLogin(false);
		return;
	}

	bLoginRequestInFlight = true;
	BeginBackendExchange(TEXT("dev:") + SteamId);
#endif
}

void URPGSteamAuthSubsystem::HandleSteamTicketReceived(
	const int32 LocalUserNum,
	const bool bWasSuccessful,
	const FExternalAuthToken& AuthToken)
{
	if (!bLoginRequestInFlight)
	{
		return;
	}

	if (!bWasSuccessful || !AuthToken.HasTokenString())
	{
		UE_LOG(LogTemp, Error,
			TEXT("Failed to obtain a Steam Web API ticket for local user %d."),
			LocalUserNum);
		CompleteLogin(false);
		return;
	}

	BeginBackendExchange(AuthToken.TokenString);
}

void URPGSteamAuthSubsystem::BeginBackendExchange(const FString& Ticket)
{
	UGameInstance* GameInstance = GetGameInstance();
	UHttpWebManager* WebManager = GameInstance
		? GameInstance->GetSubsystem<UHttpWebManager>()
		: nullptr;
	if (!WebManager)
	{
		CompleteLogin(false);
		return;
	}

	WebManager->OnBackendAuthenticationCompleted.RemoveDynamic(
		this, &ThisClass::HandleBackendAuthenticationCompleted);
	WebManager->OnBackendAuthenticationCompleted.AddDynamic(
		this, &ThisClass::HandleBackendAuthenticationCompleted);
	WebManager->AuthenticateWithSteamTicket(Ticket);
}

void URPGSteamAuthSubsystem::HandleBackendAuthenticationCompleted(const bool bSuccess)
{
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UHttpWebManager* WebManager =
			GameInstance->GetSubsystem<UHttpWebManager>())
		{
			WebManager->OnBackendAuthenticationCompleted.RemoveDynamic(
				this, &ThisClass::HandleBackendAuthenticationCompleted);
		}
	}

	CompleteLogin(bSuccess);
}

void URPGSteamAuthSubsystem::CompleteLogin(const bool bSuccess)
{
	bLoginRequestInFlight = false;
	OnLoginCompleted.Broadcast(bSuccess);
}
