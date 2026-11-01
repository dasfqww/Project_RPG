#include "Component/RPGDungeonPartyCombatComponent.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Attribute/RPGAttributeSet.h"
#include "Character/RPGPlayer.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameMode/RPGGameModeBase.h"
#include "HAL/PlatformTime.h"
#include "Net/UnrealNetwork.h"
#include "Player/RPGPlayerState.h"
#include "RPGGameplayTags.h"
#include "Security/RPGDungeonGameplayAuthority.h"

URPGDungeonPartyCombatComponent::URPGDungeonPartyCombatComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = .25f;
	SetIsReplicatedByDefault(true);
}

void URPGDungeonPartyCombatComponent::BeginPlay()
{
	Super::BeginPlay();
	SetComponentTickEnabled(GetOwner() && GetOwner()->HasAuthority());
}

void URPGDungeonPartyCombatComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(ThisClass, Snapshot, COND_OwnerOnly);
}

void URPGDungeonPartyCombatComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	RefreshServerSnapshot();
}

FRPGDungeonPartyCombatSnapshot URPGDungeonPartyCombatComponent::GetPartyCombatSnapshot() const
{
	const auto* Controller = Cast<APlayerController>(GetOwner());
	const auto* State = Controller ? Controller->GetPlayerState<ARPGPlayerState>() : nullptr;
	return IsValid(Controller) && IsValid(State) && !State->IsInactive()
		&& State->GetOwner() == Controller && !IsDungeonGameplayRevoked(Controller)
		&& (!IsValid(Controller->GetPawn()) || !IsDungeonGameplayRevoked(Controller->GetPawn()))
		&& FRPGDungeonPartyCombatPresentation::MatchesOwner(Snapshot, State->GetBackendDungeonSessionId(),
			State->GetBackendCharacterId(), State->HasAuthenticatedCharacter()) ? Snapshot : FRPGDungeonPartyCombatSnapshot();
}

void URPGDungeonPartyCombatComponent::RefreshServerSnapshot()
{
	auto* Controller = Cast<APlayerController>(GetOwner());
	if (!IsValid(Controller) || !Controller->HasAuthority()) return;
	UWorld* World = Controller->GetWorld();
	const auto* OwnerState = Controller->GetPlayerState<ARPGPlayerState>();
	const auto* GameState = World ? World->GetGameState() : nullptr;
	FRPGDungeonPartyCombatSnapshot Next;
	if (IsValid(World) && !World->bIsTearingDown && IsValid(GameState) && IsValid(OwnerState)
		&& OwnerState->GetOwner() == Controller && OwnerState->HasAuthenticatedCharacter()
		&& !OwnerState->IsInactive() && !IsDungeonGameplayRevoked(Controller)
		&& (!IsValid(Controller->GetPawn()) || !IsDungeonGameplayRevoked(Controller->GetPawn())))
	{
		Next.DungeonSessionId = OwnerState->GetBackendDungeonSessionId();
		Next.RecipientCharacterId = OwnerState->GetBackendCharacterId();
		if (CachedSessionId != Next.DungeonSessionId || CachedRecipientId != Next.RecipientCharacterId)
		{
			MemberCache.Reset(); PendingNotices.Reset(); NextMemberSlot = 0;
			CachedSessionId = Next.DungeonSessionId; CachedRecipientId = Next.RecipientCharacterId;
		}
		const double ServerTime = GameState->GetServerWorldTimeSeconds();
		PendingNotices.RemoveAll([ServerTime](const FRPGDungeonPartyNotice& Notice)
		{
			return !FRPGDungeonPartyCombatPresentation::IsNoticeVisible(Notice, ServerTime);
		});
		auto FindMember = [this](const FString& CharacterId) -> FCachedMember&
		{
			if (auto* Existing = MemberCache.Find(CharacterId)) return *Existing;
			FCachedMember Added; Added.Record.CharacterId = CharacterId; Added.Slot = NextMemberSlot++;
			return MemberCache.Add(CharacterId, MoveTemp(Added));
		};
		const auto* Mode = World->GetAuthGameMode<ARPGGameModeBase>();
		FRPGDungeonMembershipPresentation Membership;
		Next.bHasMembershipSnapshot = Mode && Mode->GetDungeonMembershipPresentation(Next.DungeonSessionId, Membership);
		Next.bRosterStale = !Next.bHasMembershipSnapshot || Membership.bStale;
		bool bAmbiguous = false;
		for (auto& Pair : MemberCache) Pair.Value.bListed = false;
		if (Next.bHasMembershipSnapshot)
		{
			TSet<FString> SeenRoster;
			for (const auto& Record : Membership.Snapshot.Members)
			{
				if (Record.CharacterId.IsEmpty() || Record.SteamId.IsEmpty() || SeenRoster.Contains(Record.CharacterId))
				{ bAmbiguous = true; break; }
				SeenRoster.Add(Record.CharacterId);
				auto& Cached = FindMember(Record.CharacterId);
				if (!Cached.Record.SteamId.IsEmpty() && Cached.Record.SteamId != Record.SteamId)
				{ bAmbiguous = true; break; }
				Cached.Record = Record; Cached.bListed = true;
			}
		}
		TMap<FString, const ARPGPlayerState*> LiveMembers;
		for (const auto& Entry : GameState->PlayerArray)
		{
			auto* Peer = Cast<ARPGPlayerState>(Entry);
			const auto* PeerController = IsValid(Peer) ? Peer->GetPlayerController() : nullptr;
			if (!IsValid(Peer) || Peer->GetWorld() != World || Peer->IsInactive() || !Peer->HasAuthenticatedCharacter()
				|| !IsValid(PeerController) || PeerController->GetWorld() != World
				|| PeerController->GetPlayerState<ARPGPlayerState>() != Peer || Peer->GetOwner() != PeerController
				|| !Peer->GetBackendDungeonSessionId().Equals(Next.DungeonSessionId, ESearchCase::CaseSensitive)
				|| IsDungeonGameplayRevoked(PeerController)
				|| (IsValid(PeerController->GetPawn()) && IsDungeonGameplayRevoked(PeerController->GetPawn()))) continue;
			const FString CharacterId = Peer->GetBackendCharacterId();
			if (LiveMembers.Contains(CharacterId)) { bAmbiguous = true; break; }
			LiveMembers.Add(CharacterId, Peer);
			auto& Cached = FindMember(CharacterId);
			if (!Cached.Record.SteamId.IsEmpty() && Cached.Record.SteamId != Peer->GetAuthenticatedSteamId())
			{ bAmbiguous = true; break; }
			Cached.Record.SteamId = Peer->GetAuthenticatedSteamId();
			Cached.LastObservedPlayer = Peer;
			Cached.HighestObservedConnectionGeneration = FMath::Max(Cached.HighestObservedConnectionGeneration, Peer->GetBackendConnectionGeneration());
			Cached.PlayerNumber = Peer->GetPlayerId();
			Cached.PublicName = FRPGDungeonPartyCombatPresentation::SanitizeDisplayName(Peer->GetPlayerName());
		}
		bool bHasSelf = false;
		if (MemberCache.Num() > 128)
		{ bAmbiguous = true; MemberCache.Reset(); NextMemberSlot = 0; }
		auto PublishRow = [this, &Next, ServerTime](FCachedMember& Cached, FRPGDungeonPartyCombatMember&& Row)
		{
			const auto NoticeKind = Cached.PresenceHistory.Observe(Row.Presence);
			if (NoticeKind.IsSet()) FRPGDungeonPartyCombatPresentation::AddNotice(PendingNotices, Row, NoticeKind.GetValue(), ServerTime);
			Next.Members.Add(MoveTemp(Row));
		};
		if (!bAmbiguous) for (auto It = MemberCache.CreateIterator(); It; ++It)
		{
			const FString& CharacterId = It.Key();
			auto& Cached = It.Value();
			FRPGDungeonPartyCombatMember Row;
			Row.MemberSlot = Cached.Slot; Row.PlayerNumber = Cached.PlayerNumber;
			Row.DisplayName = FRPGDungeonPartyCombatPresentation::SanitizeDisplayName(Cached.Record.CharacterName);
			if (Row.DisplayName.IsEmpty()) Row.DisplayName = Cached.PublicName;
			Row.bIsSelf = CharacterId == Next.RecipientCharacterId;
			if (Mode && Mode->IsDungeonPartyMemberRevoked(Next.DungeonSessionId, CharacterId, Cached.Record.SteamId))
			{
				// Removal is announced only with the existing exact immutable receipt,
				// never because a row/actor vanished or its countdown reached zero.
				if (Cached.PresenceHistory.HasObserved()) FRPGDungeonPartyCombatPresentation::AddNotice(
					PendingNotices, Row, ERPGDungeonPartyNoticeKind::DepartureConfirmed, ServerTime);
				It.RemoveCurrent(); continue;
			}
			const auto* Prior = Cached.LastObservedPlayer.Get();
			// A live server quarantine is not displayed as a dead participant. An
			// ordinary destroyed/inactive PlayerState is NOT an expulsion receipt.
			if (IsValid(Prior) && !Prior->IsActorBeingDestroyed() && !Prior->IsInactive()
				&& GameState->PlayerArray.Contains(Prior) && Prior->IsAuthenticatedIdentityRevoked()) continue;
			const auto* Found = LiveMembers.Find(CharacterId);
			const ARPGPlayerState* Peer = Found ? *Found : nullptr;
			bHasSelf |= Row.bIsSelf;
			Next.bHasUnconfirmedMembers |= !Cached.bListed;
			// Ticket consumption may already have advanced ConnectionGeneration
			// while the read still carries the previous connection's disconnect.
			const int64 DisconnectedGeneration = Cached.Record.bHasDisconnectedGeneration
				? Cached.Record.DisconnectedGeneration : Cached.Record.ConnectionGeneration;
			const bool bNewerAdmission = Cached.HighestObservedConnectionGeneration > DisconnectedGeneration;
			const bool bReconnecting = Cached.bListed && Cached.Record.bHasConnectionPresentation
				&& !Cached.Record.DisconnectedAt.IsEmpty() && !bNewerAdmission;
			if (bReconnecting)
			{
				Row.Presence = ERPGDungeonPartyPresence::ReconnectGrace;
				if (!Membership.bStale && Membership.bHasRequestClock)
					Row.bHasReconnectDeadline = FRPGDungeonPartyCombatPresentation::TryBuildReconnectDeadline(
						Membership.Snapshot.ObservedAt, Cached.Record.ReconnectExpiresAt,
						FPlatformTime::Seconds() - Membership.RequestStartedAtSeconds, GameState->GetServerWorldTimeSeconds(), Row.ReconnectEndServerTime);
				PublishRow(Cached, MoveTemp(Row));
				continue;
			}
			if (!Peer) { PublishRow(Cached, MoveTemp(Row)); continue; }
			Row.Presence = ERPGDungeonPartyPresence::ServerObserved;
			const auto* PeerController = Peer->GetPlayerController();
			const auto* Pawn = PeerController ? Cast<ARPGPlayer>(PeerController->GetPawn()) : nullptr;
			const auto* ASC = IsValid(Pawn) && Pawn->GetPlayerState() == Peer && !IsDungeonGameplayRevoked(Pawn)
				? UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Pawn) : nullptr;
			const bool bHasHealth = ASC && ASC->GetAvatarActor() == Pawn
				&& ASC->HasAttributeSetForAttribute(URPGAttributeSet::GetCurrentHealthAttribute())
				&& ASC->HasAttributeSetForAttribute(URPGAttributeSet::GetMaxHealthAttribute());
			FRPGDungeonPartyCombatPresentation::SetVitals(Row, bHasHealth,
				bHasHealth ? ASC->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute()) : 0.f,
				bHasHealth ? ASC->GetNumericAttribute(URPGAttributeSet::GetMaxHealthAttribute()) : 0.f,
				ASC && ASC->GetAvatarActor() == Pawn && ASC->HasMatchingGameplayTag(RPGGameplayTags::Shared_Status_Death));
			PublishRow(Cached, MoveTemp(Row));
		}
		if (bAmbiguous || !bHasSelf) { Next = {}; PendingNotices.Reset(); }
		else Next.Notices = PendingNotices;
		Next.Members.Sort([](const FRPGDungeonPartyCombatMember& A, const FRPGDungeonPartyCombatMember& B)
		{
			return A.bIsSelf != B.bIsSelf ? A.bIsSelf : A.MemberSlot < B.MemberSlot;
		});
	}
	else
	{
		MemberCache.Reset(); PendingNotices.Reset(); CachedSessionId.Reset(); CachedRecipientId.Reset(); NextMemberSlot = 0;
	}
	if (!(Snapshot == Next))
	{
		Snapshot = MoveTemp(Next);
		Controller->ForceNetUpdate();
	}
}
