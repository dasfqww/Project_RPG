#include "Online/RPGBackendPlayerAuthState.h"

namespace
{
	bool IsHeaderToken(const FString& Token)
	{
		if (Token.IsEmpty()) return false;
		for (TCHAR Character : Token)
		{
			if (Character <= 0x20 || Character >= 0x7f) return false;
		}
		return true;
	}
	bool ParseServerTime(const FString& Value, FDateTime& Parsed)
	{
		const bool bHasZone = Value.EndsWith(TEXT("Z"), ESearchCase::CaseSensitive)
			|| (Value.Len() >= 6 && (Value[Value.Len() - 6] == TEXT('+')
				|| Value[Value.Len() - 6] == TEXT('-')));
		return bHasZone && Value.Contains(TEXT("T"), ESearchCase::CaseSensitive)
			&& FDateTime::ParseIso8601(*Value, Parsed);
	}
}

bool FRPGBackendPlayerAuthState::IsSteamId(const FString& SteamId)
{
	if (SteamId.Len() != 17) return false;
	for (TCHAR Character : SteamId)
	{
		if (Character < TEXT('0') || Character > TEXT('9')) return false;
	}
	return true;
}

bool FRPGBackendPlayerAuthState::AcceptLogin(const FString& Token, const FString& SteamId,
	const FString& ExpiresAt, const FString& IssuedAt,
	const double RequestStartedSeconds, const double NowSeconds)
{
	FDateTime ParsedExpiry;
	FDateTime ParsedIssuedAt;
	if (!IsHeaderToken(Token) || !IsSteamId(SteamId)
		|| (!AccountSteamId.IsEmpty() && !AccountSteamId.Equals(SteamId, ESearchCase::CaseSensitive))
		|| !ParseServerTime(ExpiresAt, ParsedExpiry) || !ParseServerTime(IssuedAt, ParsedIssuedAt)
		|| !FMath::IsFinite(RequestStartedSeconds) || !FMath::IsFinite(NowSeconds)
		|| RequestStartedSeconds < 0.0 || NowSeconds < RequestStartedSeconds)
	{
		return false;
	}
	// Match the backend's maximum player lifetime (24 hours). Never compare to
	// the client's wall clock, and charge the entire request/response delay to
	// the lifetime: the deadline is conservative, not receive-time + lifetime.
	const double LifetimeSeconds = (ParsedExpiry - ParsedIssuedAt).GetTotalSeconds();
	const double NewDeadline = RequestStartedSeconds + LifetimeSeconds;
	if (LifetimeSeconds <= 0.0 || LifetimeSeconds > 24.0 * 60.0 * 60.0
		|| !FMath::IsFinite(NewDeadline) || NewDeadline <= NowSeconds) return false;
	AccessToken = Token;
	RequestAuthorization = TEXT("Bearer ") + Token;
	AccountSteamId = SteamId;
	DeadlineSeconds = NewDeadline;
	LastObservedSeconds = NowSeconds;
	bHasDeadline = true;
	bExpired = false;
	return true;
}

bool FRPGBackendPlayerAuthState::HasAccessToken(const double NowSeconds) const
{
	if (AccessToken.IsEmpty()) return false;
	if (!bHasDeadline) return true; // Explicit legacy/opaque token compatibility only.
	if (bExpired) return false;
	if (!FMath::IsFinite(NowSeconds) || NowSeconds < LastObservedSeconds || NowSeconds >= DeadlineSeconds)
	{
		bExpired = true; // A clock anomaly or later backward reading cannot resurrect an expired token.
		return false;
	}
	LastObservedSeconds = NowSeconds;
	return true;
}

FString FRPGBackendPlayerAuthState::Authorization(const double NowSeconds) const
{
	return HasAccessToken(NowSeconds) ? RequestAuthorization : FString();
}

bool FRPGBackendPlayerAuthState::MatchesRequest(const FString& AuthorizationHeader) const
{
	return !RequestAuthorization.IsEmpty()
		&& RequestAuthorization.Equals(AuthorizationHeader, ESearchCase::CaseSensitive);
}

bool FRPGBackendPlayerAuthState::InvalidateIfUnauthorized(const FString& AuthorizationHeader, int32 StatusCode)
{
	if (StatusCode != 401 || !MatchesRequest(AuthorizationHeader)) return false;
	Invalidate();
	return true;
}

void FRPGBackendPlayerAuthState::Invalidate()
{
	AccessToken.Reset();
	bHasDeadline = false;
	DeadlineSeconds = LastObservedSeconds = 0.0;
	bExpired = false;
}

void FRPGBackendPlayerAuthState::Reset()
{
	Invalidate();
	RequestAuthorization.Reset();
	AccountSteamId.Reset();
}

void FRPGBackendPlayerAuthState::SetOpaqueToken(const FString& Token)
{
	const FString Normalized = Token.TrimStartAndEnd();
	if (!IsHeaderToken(Normalized)) { Invalidate(); return; }
	AccessToken = Normalized;
	RequestAuthorization = TEXT("Bearer ") + Normalized;
	bHasDeadline = false;
	DeadlineSeconds = LastObservedSeconds = 0.0;
	bExpired = false;
}
