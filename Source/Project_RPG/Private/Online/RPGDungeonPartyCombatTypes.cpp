#include "Online/RPGDungeonPartyCombatTypes.h"

FString FRPGDungeonPartyCombatPresentation::SanitizeDisplayName(const FString& Input)
{
	FString Name = Input.TrimStartAndEnd();
	if (Name.IsEmpty() || Name.Len() > 64) return {};
	for (TCHAR Character : Name) if (FChar::IsControl(Character)) return {};
	return Name;
}

bool FRPGDungeonPartyNotice::operator==(const FRPGDungeonPartyNotice& Other) const
{
	return MemberSlot == Other.MemberSlot && DisplayName == Other.DisplayName
		&& Kind == Other.Kind && ExpiresAtServerTime == Other.ExpiresAtServerTime;
}

TOptional<ERPGDungeonPartyNoticeKind> FRPGDungeonPartyPresenceHistory::Observe(ERPGDungeonPartyPresence Presence)
{
	TOptional<ERPGDungeonPartyNoticeKind> Notice;
	if (Presence == ERPGDungeonPartyPresence::ReconnectGrace)
	{
		if (bHasObserved && !bAwaitingReconnect) Notice = ERPGDungeonPartyNoticeKind::ConnectionLost;
		bAwaitingReconnect = true;
	}
	else if (Presence == ERPGDungeonPartyPresence::ServerObserved)
	{
		if (bHasObserved && bAwaitingReconnect) Notice = ERPGDungeonPartyNoticeKind::Reconnected;
		bAwaitingReconnect = false;
	}
	// Missing actors/reads neither start a disconnect notice nor cancel a known
	// reconnect wait. Initial roster hydration is not a new connection event.
	bHasObserved = true;
	return Notice;
}

bool FRPGDungeonPartyCombatMember::operator==(const FRPGDungeonPartyCombatMember& Other) const
{
	return PlayerNumber == Other.PlayerNumber && MemberSlot == Other.MemberSlot && DisplayName == Other.DisplayName && bIsSelf == Other.bIsSelf
		&& bHasHealth == Other.bHasHealth && CurrentHealth == Other.CurrentHealth && MaxHealth == Other.MaxHealth && State == Other.State
		&& Presence == Other.Presence && bHasReconnectDeadline == Other.bHasReconnectDeadline
		&& FMath::IsNearlyEqual(ReconnectEndServerTime, Other.ReconnectEndServerTime, .1);
}

bool FRPGDungeonPartyCombatSnapshot::operator==(const FRPGDungeonPartyCombatSnapshot& Other) const
{
	return DungeonSessionId == Other.DungeonSessionId && RecipientCharacterId == Other.RecipientCharacterId && Members == Other.Members
		&& Notices == Other.Notices
		&& bHasMembershipSnapshot == Other.bHasMembershipSnapshot && bRosterStale == Other.bRosterStale
		&& bHasUnconfirmedMembers == Other.bHasUnconfirmedMembers;
}

bool FRPGDungeonPartyCombatPresentation::MatchesOwner(const FRPGDungeonPartyCombatSnapshot& Snapshot,
	const FString& SessionId, const FString& CharacterId, bool bAuthenticated)
{
	return bAuthenticated && !SessionId.IsEmpty() && !CharacterId.IsEmpty()
		&& Snapshot.DungeonSessionId.Equals(SessionId, ESearchCase::CaseSensitive)
		&& Snapshot.RecipientCharacterId.Equals(CharacterId, ESearchCase::CaseSensitive);
}

void FRPGDungeonPartyCombatPresentation::SetVitals(FRPGDungeonPartyCombatMember& Member,
	bool bHasAttributes, float CurrentHealth, float MaxHealth, bool bDeathTag)
{
	Member.bHasHealth = bHasAttributes && FMath::IsFinite(CurrentHealth) && FMath::IsFinite(MaxHealth)
		&& CurrentHealth >= 0.f && MaxHealth > 0.f;
	Member.CurrentHealth = Member.bHasHealth ? FMath::Clamp(CurrentHealth, 0.f, MaxHealth) : 0.f;
	Member.MaxHealth = Member.bHasHealth ? MaxHealth : 0.f;
	Member.State = bDeathTag ? ERPGDungeonPartyCombatState::Incapacitated
		: !Member.bHasHealth ? ERPGDungeonPartyCombatState::Unconfirmed
		: Member.CurrentHealth <= 0.f ? ERPGDungeonPartyCombatState::Incapacitated : ERPGDungeonPartyCombatState::Alive;
}

float FRPGDungeonPartyCombatPresentation::HealthFraction(const FRPGDungeonPartyCombatMember& Member)
{
	return Member.bHasHealth && FMath::IsFinite(Member.CurrentHealth) && FMath::IsFinite(Member.MaxHealth) && Member.MaxHealth > 0.f
		? FMath::Clamp(Member.CurrentHealth / Member.MaxHealth, 0.f, 1.f) : 0.f;
}

bool FRPGDungeonPartyCombatPresentation::IsNoticeVisible(const FRPGDungeonPartyNotice& Notice, double ServerWorldTime)
{
	if (!FMath::IsFinite(ServerWorldTime) || ServerWorldTime < 0.0 || !FMath::IsFinite(Notice.ExpiresAtServerTime)) return false;
	const double Remaining = Notice.ExpiresAtServerTime - ServerWorldTime;
	return Remaining > 0.0 && Remaining <= NoticeDurationSeconds;
}

FText FRPGDungeonPartyCombatPresentation::NoticeText(const FRPGDungeonPartyNotice& Notice)
{
	const FString Name = SanitizeDisplayName(Notice.DisplayName);
	const FText Label = !Name.IsEmpty() ? FText::AsCultureInvariant(Name)
		: Notice.MemberSlot >= 0 && Notice.MemberSlot < MAX_int32
			? FText::Format(NSLOCTEXT("RPGPartyCombat", "NoticeUnnamedSlot", "파티원 {0}"), FText::AsNumber(Notice.MemberSlot + 1))
			: NSLOCTEXT("RPGPartyCombat", "NoticeUnknownMember", "파티원");
	switch (Notice.Kind)
	{
	case ERPGDungeonPartyNoticeKind::ConnectionLost:
		return FText::Format(NSLOCTEXT("RPGPartyCombat", "ConnectionLostNotice", "{0}: 서버에서 연결 끊김 확인 · 재접속 대기"), Label);
	case ERPGDungeonPartyNoticeKind::Reconnected:
		return FText::Format(NSLOCTEXT("RPGPartyCombat", "ReconnectedNotice", "{0}: 재접속 확인"), Label);
	case ERPGDungeonPartyNoticeKind::DepartureConfirmed:
		return FText::Format(NSLOCTEXT("RPGPartyCombat", "DepartureConfirmedNotice", "{0}: 서버에서 탈퇴 확인"), Label);
	default:
		return FText::GetEmpty();
	}
}

void FRPGDungeonPartyCombatPresentation::AddNotice(TArray<FRPGDungeonPartyNotice>& Notices,
	const FRPGDungeonPartyCombatMember& Member, ERPGDungeonPartyNoticeKind Kind, double ServerWorldTime)
{
	if (!FMath::IsFinite(ServerWorldTime) || ServerWorldTime < 0.0 || Member.MemberSlot < 0) return;
	FRPGDungeonPartyNotice Notice;
	Notice.MemberSlot = Member.MemberSlot;
	Notice.DisplayName = SanitizeDisplayName(Member.DisplayName);
	Notice.Kind = Kind;
	Notice.ExpiresAtServerTime = ServerWorldTime + NoticeDurationSeconds;
	if (!FMath::IsFinite(Notice.ExpiresAtServerTime) || Notice.ExpiresAtServerTime <= ServerWorldTime
		|| NoticeText(Notice).IsEmpty()) return;
	// A reconnect/departure replaces that member's earlier wait message rather
	// than leaving contradictory notices visible at the same time.
	Notices.RemoveAll([&Member](const FRPGDungeonPartyNotice& Existing) { return Existing.MemberSlot == Member.MemberSlot; });
	Notices.Add(MoveTemp(Notice));
	if (Notices.Num() > MaxNotices) Notices.RemoveAt(0, Notices.Num() - MaxNotices);
}

bool FRPGDungeonPartyCombatPresentation::TryBuildReconnectDeadline(const FString& BackendObservedAt,
	const FString& BackendDeadline, double RequestAgeSeconds, double ServerWorldTime, double& OutEndServerTime)
{
	OutEndServerTime = 0.0;
	FDateTime Observed, Deadline;
	if (!FMath::IsFinite(RequestAgeSeconds) || RequestAgeSeconds < 0.0
		|| !FMath::IsFinite(ServerWorldTime) || ServerWorldTime <= 0.0
		|| !FDateTime::ParseIso8601(*BackendObservedAt, Observed) || !FDateTime::ParseIso8601(*BackendDeadline, Deadline)) return false;
	const double RemainingAtObservation = (Deadline - Observed).GetTotalSeconds();
	if (RemainingAtObservation > 600.0) return false;
	// Charge the full GET request age, including latency before backend observation.
	// This is an approximate display deadline, never permission to expire membership.
	OutEndServerTime = ServerWorldTime + FMath::Max(0.0, RemainingAtObservation - RequestAgeSeconds);
	if (!FMath::IsFinite(OutEndServerTime)) { OutEndServerTime = 0.0; return false; }
	return true;
}

FText FRPGDungeonPartyCombatPresentation::ReconnectText(const FRPGDungeonPartyCombatMember& Member, double ServerWorldTime)
{
	if (!Member.bHasReconnectDeadline || !FMath::IsFinite(ServerWorldTime) || ServerWorldTime <= 0.0
		|| !FMath::IsFinite(Member.ReconnectEndServerTime))
		return NSLOCTEXT("RPGPartyCombat", "ReconnectUnknownTime", "재접속 대기 · 남은 시간 확인 중");
	const int32 Remaining = FMath::CeilToInt(FMath::Clamp(Member.ReconnectEndServerTime - ServerWorldTime, 0.0, 600.0));
	if (Remaining == 0)
		return NSLOCTEXT("RPGPartyCombat", "ReconnectAwaitingReceipt", "유예 경과 · 서버 확인 대기");
	FNumberFormattingOptions Digits;
	Digits.SetMinimumIntegralDigits(2).SetUseGrouping(false);
	return FText::Format(NSLOCTEXT("RPGPartyCombat", "ReconnectRemaining", "재접속 대기 · 약 {0}:{1}"),
		FText::AsNumber(Remaining / 60, &Digits), FText::AsNumber(Remaining % 60, &Digits));
}

FText FRPGDungeonPartyCombatPresentation::MemberText(const FRPGDungeonPartyCombatMember& Member, double ServerWorldTime)
{
	const FString Name = SanitizeDisplayName(Member.DisplayName);
	const FText NameText = !Name.IsEmpty() ? FText::AsCultureInvariant(Name)
		: Member.MemberSlot >= 0 ? FText::Format(NSLOCTEXT("RPGPartyCombat", "UnnamedSlot", "파티원 {0}"), FText::AsNumber(Member.MemberSlot + 1))
		: Member.PlayerNumber >= 0 ? FText::Format(NSLOCTEXT("RPGPartyCombat", "UnnamedMember", "파티원 {0}"), FText::AsNumber(Member.PlayerNumber))
		: NSLOCTEXT("RPGPartyCombat", "UnknownMember", "파티원");
	const FText Label = Member.bIsSelf
		? FText::Format(NSLOCTEXT("RPGPartyCombat", "SelfMember", "[나] {0}"), NameText) : NameText;
	if (Member.Presence == ERPGDungeonPartyPresence::ReconnectGrace)
		return FText::Format(NSLOCTEXT("RPGPartyCombat", "ReconnectingMember", "{0}\n{1}"), Label, ReconnectText(Member, ServerWorldTime));
	const FText State = Member.State == ERPGDungeonPartyCombatState::Incapacitated
		? NSLOCTEXT("RPGPartyCombat", "Incapacitated", "전투 불능")
		: Member.State == ERPGDungeonPartyCombatState::Alive ? NSLOCTEXT("RPGPartyCombat", "Alive", "생존")
		: NSLOCTEXT("RPGPartyCombat", "Unconfirmed", "상태 확인 중");
	if (!Member.bHasHealth)
		return FText::Format(NSLOCTEXT("RPGPartyCombat", "MemberWithoutHealth", "{0} · {1}"), Label, State);
	FNumberFormattingOptions Numbers;
	Numbers.SetMaximumFractionalDigits(1);
	return FText::Format(NSLOCTEXT("RPGPartyCombat", "MemberWithHealth", "{0} · {1}\n체력 {2} / {3}"),
		Label, State, FText::AsNumber(Member.CurrentHealth, &Numbers), FText::AsNumber(Member.MaxHealth, &Numbers));
}
