#pragma once

#include "CoreMinimal.h"
#include "Http.h"

class FRPGBackendPlayerAuthState;
class UObject;

struct FRPGBackendAuthenticationConfig
{
	FString ApiUrl;
	float TimeoutSeconds = 10.f;
	bool bServerRuntime = false;
	bool bProgressionPending = false;
};

/** Transport configuration/notifications only; gameplay logout eligibility remains with its caller. */
class IRPGBackendAuthenticationHost
{
public:
	virtual ~IRPGBackendAuthenticationHost() = default;
	virtual UObject& GetAuthenticationOwner() const = 0;
	virtual FRPGBackendAuthenticationConfig GetAuthenticationConfig() const = 0;
	virtual void ApplyAuthenticationHeaders(const FHttpRequestRef& Request) const = 0;
	virtual void ObserveAuthenticationResponse(const FHttpRequestPtr& Request, const FHttpResponsePtr& Response) = 0;
	virtual void BroadcastAuthenticationResult(bool bSuccess) = 0;
	virtual void InvalidateAuthenticationDungeonRequests() = 0;
};

/** Owns login/logout request lifetimes. Credential policy stays in the existing memory-only state.
 * Host and State outlive this service. Weak host/service callbacks do not retain either request.
 */
class PROJECT_RPG_API FRPGBackendAuthentication : public TSharedFromThis<FRPGBackendAuthentication>
{
	friend struct FRPGBackendAuthTestAccess;
	friend struct FRPGEquipmentMutationTestAccess;
	friend struct FRPGServiceStatusTestAccess;
	friend struct FRPGBackendAuthenticationServiceTestAccess;
public:
	FRPGBackendAuthentication(IRPGBackendAuthenticationHost& InHost, FRPGBackendPlayerAuthState& InState)
		: Host(InHost), State(InState) {}
	~FRPGBackendAuthentication();
	FRPGBackendAuthentication(const FRPGBackendAuthentication&) = delete;
	FRPGBackendAuthentication& operator=(const FRPGBackendAuthentication&) = delete;
	bool IsLoginActive() const { return ActiveLogin.IsValid(); }
	bool IsLogoutActive() const { return ActiveLogout.IsValid(); }
	bool HasAccessToken() const;
	void CancelLogin();
	void CancelLogout();
	void Authenticate(const FString& SteamTicketHex);
	bool RequestLogout(TFunction<bool()> CanFinalize, TFunction<void(bool)> Completion);
	/** Build-only seams: these reserve the same in-flight state without sending HTTP. */
	FHttpRequestPtr CreateLoginRequest(const FString& SteamTicketHex);
	FHttpRequestPtr CreateLogoutRequest(TFunction<bool()> CanFinalize, TFunction<void(bool)> Completion);
	void HandleLoginResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful);
	void HandleLogoutResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful);
private:
	IRPGBackendAuthenticationHost& Host;
	FRPGBackendPlayerAuthState& State;
	FHttpRequestPtr ActiveLogin;
	FHttpRequestPtr ActiveLogout;
	double LoginStartedSeconds = -1.0;
	TFunction<bool()> LogoutContextValidator;
	TFunction<void(bool)> LogoutCompletion;
};
