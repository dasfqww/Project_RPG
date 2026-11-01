#pragma once

#include "CoreMinimal.h"
#include "RPGDungeonPartyCombatTypes.generated.h"

UENUM(BlueprintType)
enum class ERPGDungeonPartyCombatState : uint8
{
	Unconfirmed,
	Alive,
	Incapacitated
};

UENUM(BlueprintType)
enum class ERPGDungeonPartyPresence : uint8
{
	Unconfirmed,
	ServerObserved,
	ReconnectGrace
};

UENUM(BlueprintType)
enum class ERPGDungeonPartyNoticeKind : uint8
{
	ConnectionLost,
	Reconnected,
	DepartureConfirmed
};

/** Short-lived presentation only, scoped by the enclosing owner snapshot. */
USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGDungeonPartyNotice
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) int32 MemberSlot = INDEX_NONE;
	UPROPERTY(BlueprintReadOnly) FString DisplayName;
	UPROPERTY(BlueprintReadOnly) ERPGDungeonPartyNoticeKind Kind = ERPGDungeonPartyNoticeKind::ConnectionLost;
	UPROPERTY(BlueprintReadOnly) double ExpiresAtServerTime = 0.0;

	bool operator==(const FRPGDungeonPartyNotice& Other) const;
};

/** Server-only transition memory. Unknown presence is never a disconnect/departure. */
struct PROJECT_RPG_API FRPGDungeonPartyPresenceHistory
{
	TOptional<ERPGDungeonPartyNoticeKind> Observe(ERPGDungeonPartyPresence Presence);
	bool HasObserved() const { return bHasObserved; }
private:
	bool bHasObserved = false;
	bool bAwaitingReconnect = false;
};

/** Public combat presentation only; no peer account, character, inventory or reward identity. */
USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGDungeonPartyCombatMember
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) int32 PlayerNumber = INDEX_NONE;
	/** Stable within this recipient/session; not a backend identity or permission. */
	UPROPERTY(BlueprintReadOnly) int32 MemberSlot = INDEX_NONE;
	UPROPERTY(BlueprintReadOnly) FString DisplayName;
	UPROPERTY(BlueprintReadOnly) bool bIsSelf = false;
	UPROPERTY(BlueprintReadOnly) bool bHasHealth = false;
	UPROPERTY(BlueprintReadOnly) float CurrentHealth = 0.f;
	UPROPERTY(BlueprintReadOnly) float MaxHealth = 0.f;
	UPROPERTY(BlueprintReadOnly) ERPGDungeonPartyCombatState State = ERPGDungeonPartyCombatState::Unconfirmed;
	UPROPERTY(BlueprintReadOnly) ERPGDungeonPartyPresence Presence = ERPGDungeonPartyPresence::Unconfirmed;
	UPROPERTY(BlueprintReadOnly) bool bHasReconnectDeadline = false;
	/** Approximate display deadline in GameState server time. Zero is not an expulsion receipt. */
	UPROPERTY(BlueprintReadOnly) double ReconnectEndServerTime = 0.0;

	bool operator==(const FRPGDungeonPartyCombatMember& Other) const;
};

/** Entire property is owner-only. Scope IDs belong to the recipient, not peers. */
USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGDungeonPartyCombatSnapshot
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) FString DungeonSessionId;
	UPROPERTY(BlueprintReadOnly) FString RecipientCharacterId;
	UPROPERTY(BlueprintReadOnly) TArray<FRPGDungeonPartyCombatMember> Members;
	UPROPERTY(BlueprintReadOnly) TArray<FRPGDungeonPartyNotice> Notices;
	UPROPERTY(BlueprintReadOnly) bool bHasMembershipSnapshot = false;
	UPROPERTY(BlueprintReadOnly) bool bRosterStale = true;
	UPROPERTY(BlueprintReadOnly) bool bHasUnconfirmedMembers = false;

	bool operator==(const FRPGDungeonPartyCombatSnapshot& Other) const;
};

struct PROJECT_RPG_API FRPGDungeonPartyCombatPresentation
{
	static FString SanitizeDisplayName(const FString& Name);
	static bool MatchesOwner(const FRPGDungeonPartyCombatSnapshot& Snapshot,
		const FString& SessionId, const FString& CharacterId, bool bAuthenticated);
	static void SetVitals(FRPGDungeonPartyCombatMember& Member, bool bHasAttributes,
		float CurrentHealth, float MaxHealth, bool bDeathTag);
	static FText MemberText(const FRPGDungeonPartyCombatMember& Member, double ServerWorldTime = 0.0);
	static FText ReconnectText(const FRPGDungeonPartyCombatMember& Member, double ServerWorldTime);
	static bool TryBuildReconnectDeadline(const FString& BackendObservedAt, const FString& BackendDeadline,
		double RequestAgeSeconds, double ServerWorldTime, double& OutEndServerTime);
	static float HealthFraction(const FRPGDungeonPartyCombatMember& Member);
	static constexpr double NoticeDurationSeconds = 8.0;
	static constexpr int32 MaxNotices = 3;
	static bool IsNoticeVisible(const FRPGDungeonPartyNotice& Notice, double ServerWorldTime);
	static FText NoticeText(const FRPGDungeonPartyNotice& Notice);
	static void AddNotice(TArray<FRPGDungeonPartyNotice>& Notices, const FRPGDungeonPartyCombatMember& Member,
		ERPGDungeonPartyNoticeKind Kind, double ServerWorldTime);
};
