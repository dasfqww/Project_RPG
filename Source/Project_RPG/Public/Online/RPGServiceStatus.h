#pragma once

#include "CoreMinimal.h"

/** Public, unauthenticated service metadata. Never contains player/operator credentials. */
struct PROJECT_RPG_API FRPGServiceStatusSnapshot
{
	FString Mode, Notice;
	FDateTime ObservedAt;
	TOptional<FDateTime> NoticeStartsAt, NoticeEndsAt;
	static bool Decode(const FString& Body, FRPGServiceStatusSnapshot& Out);
	static bool IsDrainResponse(int32 Code, const FString& Body);
	bool IsNoticeVisible(FDateTime ServerNow) const;
};

/** Pure cache/presentation policy. Unknown is never silently converted to Open. */
struct PROJECT_RPG_API FRPGServiceStatusState
{
	FRPGServiceStatusSnapshot Snapshot;
	bool bKnown = false, bReadFailed = false, bRefused = false;
	double ReceivedAt = 0;
	void Accept(const FRPGServiceStatusSnapshot& Value, double Now);
	bool IsStale(double Now) const;
	bool IsDraining() const { return bRefused || (bKnown && Snapshot.Mode == TEXT("Drain")); }
	FText Text(double Now) const;
	static bool IsNewAdmissionAction(FName Name);
};
