#include "Online/RPGSteamAuthSubsystem.h"

#include "Manager/HttpWebManager.h"
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
	if (bLoginRequestInFlight || IsRunningDedicatedServer()
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
#if UE_BUILD_SHIPPING
	UE_LOG(LogTemp, Error,
		TEXT("Development backend authentication is disabled in Shipping builds."));
	CompleteLogin(false);
#else
	if (bLoginRequestInFlight || SteamId.IsEmpty())
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

	WebManager->ClearAccessToken();
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
