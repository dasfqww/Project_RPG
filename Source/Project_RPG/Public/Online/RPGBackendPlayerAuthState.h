#pragma once

#include "CoreMinimal.h"

/** Memory-only player credential with a monotonic deadline. Expiry/401 retain account binding. */
class PROJECT_RPG_API FRPGBackendPlayerAuthState
{
public:
	bool AcceptLogin(const FString& Token, const FString& SteamId,
		const FString& ExpiresAt, const FString& IssuedAt,
		double RequestStartedSeconds, double NowSeconds);
	bool HasAccessToken(double NowSeconds) const;
	FString Authorization(double NowSeconds) const;
	/** Only for revoking the last player credential, including after local expiry/401. */
	const FString& LogoutAuthorization() const { return RequestAuthorization; }
	bool MatchesRequest(const FString& AuthorizationHeader) const;
	bool InvalidateIfUnauthorized(const FString& AuthorizationHeader, int32 StatusCode);
	void Invalidate();
	void Reset();
	/** Compatibility only: manually supplied tokens have no local expiry metadata. */
	void SetOpaqueToken(const FString& Token);
	const FString& GetSteamId() const { return AccountSteamId; }
	static bool IsSteamId(const FString& SteamId);

private:
	FString AccessToken;
	// Retained until the next login so all concurrent requests can finish with failure after a 401.
	FString RequestAuthorization;
	FString AccountSteamId;
	double DeadlineSeconds = 0.0;
	mutable double LastObservedSeconds = 0.0;
	bool bHasDeadline = false;
	mutable bool bExpired = false;
};
