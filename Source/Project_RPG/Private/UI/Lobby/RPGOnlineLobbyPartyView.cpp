#include "UI/Lobby/RPGOnlineLobbyPartyView.h"

#include "Components/TextBlock.h"
#include "UI/Lobby/RPGOnlineLobbyWidget.h"

namespace
{
	bool SameCharacter(const FString& Left, const FString& Right) { return Left.Equals(Right, ESearchCase::CaseSensitive); }
	bool IsCanonicalCharacter(const FString& Value)
	{
		FGuid Id;
		return FGuid::Parse(Value, Id) && Id.IsValid()
			&& Value.Equals(Id.ToString(EGuidFormats::DigitsWithHyphensLower), ESearchCase::CaseSensitive);
	}
	bool IsDisplayableName(const FString& Name)
	{
		if (Name.IsEmpty() || Name.Len() > 64) return false;
		for (TCHAR Char : Name) if (FChar::IsControl(Char)) return false;
		return true;
	}
	struct FSortedMember
	{
		FRPGOnlineLobbyPartyMemberView View;
		FDateTime JoinedAt;
		bool bHasJoinedAt = false;
	};
}

TArray<FRPGOnlineLobbyPartyMemberView> FRPGOnlineLobbyPartyPresentation::BuildMembers(const FRPGDungeonSession& Session,
	const FString& SelectedCharacterId, const TArray<FRPGBackendCharacter>& OwnedCharacters)
{
	const bool bOwnedSelection = OwnedCharacters.ContainsByPredicate([&](const FRPGBackendCharacter& Character)
		{ return SameCharacter(Character.CharacterId, SelectedCharacterId); });
	if (Session.DungeonSessionId.IsEmpty() || !bOwnedSelection
		|| !Session.Members.ContainsByPredicate([&](const FRPGDungeonSessionMember& Member)
			{ return SameCharacter(Member.CharacterId, SelectedCharacterId); })) return {};

	TArray<FSortedMember> Sorted;
	TSet<FString> Seen;
	for (const auto& Member : Session.Members)
	{
		// A malformed or duplicate snapshot is not a partial party/count to invent locally.
		if (!IsCanonicalCharacter(Member.CharacterId) || Seen.Contains(Member.CharacterId)) return {};
		Seen.Add(Member.CharacterId);
		FSortedMember Row;
		Row.View.CharacterId = Member.CharacterId;
		Row.View.bIsLocalCharacter = SameCharacter(Member.CharacterId, SelectedCharacterId);
		Row.View.bIsLeader = SameCharacter(Member.CharacterId, Session.LeaderCharacterId);
		const auto* Known = OwnedCharacters.FindByPredicate([&](const FRPGBackendCharacter& Character)
			{ return SameCharacter(Character.CharacterId, Member.CharacterId); });
		FString Name = Member.CharacterName.TrimStartAndEnd();
		if (!IsDisplayableName(Name) && Known) Name = Known->Name.TrimStartAndEnd();
		Row.View.bHasKnownName = IsDisplayableName(Name);
		Row.View.DisplayName = Row.View.bHasKnownName ? FText::AsCultureInvariant(Name)
			: FText::Format(NSLOCTEXT("RPGOnlineLobby", "UnnamedPartyCharacter", "캐릭터 {0}"), FText::AsCultureInvariant(Member.CharacterId.Left(8)));
		Row.bHasJoinedAt = FDateTime::ParseIso8601(*Member.JoinedAt, Row.JoinedAt);
		Sorted.Add(MoveTemp(Row));
	}
	Sorted.Sort([](const FSortedMember& Left, const FSortedMember& Right)
	{
		if (Left.View.bIsLeader != Right.View.bIsLeader) return Left.View.bIsLeader;
		if (Left.bHasJoinedAt != Right.bHasJoinedAt) return Left.bHasJoinedAt;
		if (Left.bHasJoinedAt && Left.JoinedAt != Right.JoinedAt) return Left.JoinedAt < Right.JoinedAt;
		return Left.View.CharacterId.Compare(Right.View.CharacterId, ESearchCase::CaseSensitive) < 0;
	});
	TArray<FRPGOnlineLobbyPartyMemberView> Result;
	Result.Reserve(Sorted.Num());
	for (auto& Row : Sorted) Result.Add(MoveTemp(Row.View));
	return Result;
}

FText FRPGOnlineLobbyPartyPresentation::Summary(const TArray<FRPGOnlineLobbyPartyMemberView>& Members, bool bCachedSnapshot)
{
	if (Members.IsEmpty()) return FText::GetEmpty();
	// No capacity, online/offline state or grace timer is inferred from this membership DTO.
	return FText::Format(bCachedSnapshot
		? NSLOCTEXT("RPGOnlineLobby", "CachedPartyRosterSummary", "파티원 {0}명 · 마지막 조회 정보")
		: NSLOCTEXT("RPGOnlineLobby", "PartyRosterSummary", "파티원 {0}명 · 서버 조회 기준"), FText::AsNumber(Members.Num()));
}

FText FRPGOnlineLobbyPartyPresentation::RosterText(const TArray<FRPGOnlineLobbyPartyMemberView>& Members)
{
	TArray<FText> Lines;
	Lines.Reserve(Members.Num());
	for (const auto& Member : Members)
	{
		const FText Role = Member.bIsLeader
			? (Member.bIsLocalCharacter ? NSLOCTEXT("RPGOnlineLobby", "LocalPartyLeader", "[방장 · 나]")
				: NSLOCTEXT("RPGOnlineLobby", "PartyLeader", "[방장]"))
			: (Member.bIsLocalCharacter ? NSLOCTEXT("RPGOnlineLobby", "LocalPartyMember", "[나]")
				: NSLOCTEXT("RPGOnlineLobby", "PartyMember", "[파티원]"));
		Lines.Add(FText::Format(NSLOCTEXT("RPGOnlineLobby", "PartyMemberLine", "{0} {1}"), Role, Member.DisplayName));
	}
	return FText::Join(FText::FromString(TEXT("\n")), Lines);
}

TArray<FRPGOnlineLobbyPartyMemberView> URPGOnlineLobbyWidget::GetDungeonPartyMembers() const
{
	if (!Web || Web->GetAuthenticatedSteamId().IsEmpty() || !Flow || !Flow->HasActiveDungeonSession() || !HasOwnedSelection()) return {};
	return FRPGOnlineLobbyPartyPresentation::BuildMembers(Flow->GetCurrentDungeonSession(), Flow->GetSelectedCharacterId(), OwnedCharacters);
}

bool URPGOnlineLobbyWidget::IsDungeonPartySnapshotCached() const
{
	return !Web || !Web->HasAccessToken() || !Flow || Flow->IsRequestInFlight()
		|| Flow->GetFlowState() == ERPGDungeonClientFlowState::Error || bLoadingCharacters || bLoggingIn || bLoggingOut;
}

FText URPGOnlineLobbyWidget::GetDungeonPartySummaryText() const
{
	return FRPGOnlineLobbyPartyPresentation::Summary(GetDungeonPartyMembers(), IsDungeonPartySnapshotCached());
}

FText URPGOnlineLobbyWidget::GetDungeonPartyRosterText() const
{
	return FRPGOnlineLobbyPartyPresentation::RosterText(GetDungeonPartyMembers());
}

void URPGOnlineLobbyWidget::RefreshDungeonPartyPresentation()
{
	const auto Members = GetDungeonPartyMembers();
	const auto PartyVisibility = Members.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible;
	if (PartyRosterHeader)
	{
		PartyRosterHeader->SetVisibility(PartyVisibility);
		PartyRosterHeader->SetText(FRPGOnlineLobbyPartyPresentation::Summary(Members, IsDungeonPartySnapshotCached()));
	}
	if (PartyRosterText)
	{
		PartyRosterText->SetVisibility(PartyVisibility);
		PartyRosterText->SetText(FRPGOnlineLobbyPartyPresentation::RosterText(Members));
	}
}
