#include "Online/RPGDungeonMembershipSnapshot.h"
#include "Online/RPGDungeonPartyCombatTypes.h"
#include "Containers/UnrealString.h"

#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	bool ReadString(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, FString& Out)
	{
		return Object.IsValid() && Object->HasTypedField<EJson::String>(Field) && Object->TryGetStringField(Field, Out);
	}
	bool IsGuid(const FString& Value)
	{
		FGuid Guid;
		return FGuid::ParseExact(Value, EGuidFormats::DigitsWithHyphens, Guid) && Guid.IsValid();
	}
	bool IsSteamId(const FString& Value)
	{
		if (Value.Len() != 17) return false;
		for (TCHAR Digit : Value) if (Digit < TEXT('0') || Digit > TEXT('9')) return false;
		return Value[0] != TEXT('0');
	}
	bool IsUtc(const FString& Value)
	{
		FDateTime Parsed;
		return Value.Contains(TEXT("T"), ESearchCase::CaseSensitive)
			&& (Value.EndsWith(TEXT("Z"), ESearchCase::CaseSensitive) || Value.EndsWith(TEXT("+00:00")))
			&& FDateTime::ParseIso8601(*Value, Parsed);
	}
	bool IsState(const FString& State)
	{
		for (const TCHAR* Allowed : { TEXT("Loading"), TEXT("InProgress"), TEXT("SettlementPending"),
			TEXT("Cleared"), TEXT("Failed"), TEXT("Closed") })
			if (State.Equals(Allowed, ESearchCase::CaseSensitive)) return true;
		return false;
	}
	bool IsValidExpulsion(const FRPGDungeonMembershipExpulsion& Receipt)
	{
		FDateTime Disconnected, Expired, Processed;
		return IsGuid(Receipt.CharacterId) && IsSteamId(Receipt.SteamId)
			&& Receipt.bRewardExcluded
			&& IsUtc(Receipt.DisconnectedAt) && IsUtc(Receipt.ExpiredAt) && IsUtc(Receipt.ProcessedAt)
			&& FDateTime::ParseIso8601(*Receipt.DisconnectedAt, Disconnected)
			&& FDateTime::ParseIso8601(*Receipt.ExpiredAt, Expired)
			&& FDateTime::ParseIso8601(*Receipt.ProcessedAt, Processed)
			&& Expired - Disconnected == FTimespan::FromMinutes(10)
			&& Processed >= Expired;
	}

	bool ReadGeneration(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, int64& Out)
	{
		FString Generation;
		int64 ParsedGeneration = 0;
		if (!ReadString(Object, Field, Generation) || Generation.IsEmpty() || Generation.Len() > 19) return false;
		for (TCHAR Digit : Generation) if (Digit < TEXT('0') || Digit > TEXT('9')) return false;
		if (!LexTryParseString(ParsedGeneration, *Generation) || ParsedGeneration < 0 || LexToString(ParsedGeneration) != Generation) return false;
		Out = ParsedGeneration;
		return true;
	}

	void ReadPresentation(const TSharedPtr<FJsonObject>& Object, FRPGDungeonMembershipRecord& Record)
	{
		FString Name;
		if (Object->HasTypedField<EJson::String>(TEXT("characterName")) && Object->TryGetStringField(TEXT("characterName"), Name))
			Record.CharacterName = FRPGDungeonPartyCombatPresentation::SanitizeDisplayName(Name);
		int64 ParsedGeneration = 0;
		if (!ReadGeneration(Object, TEXT("connectionGeneration"), ParsedGeneration)) return;
		FString Disconnected, Deadline;
		const bool bDisconnected = ReadString(Object, TEXT("disconnectedAt"), Disconnected) && !Disconnected.IsEmpty();
		const bool bDeadline = ReadString(Object, TEXT("reconnectExpiresAt"), Deadline) && !Deadline.IsEmpty();
		// Missing/null is a valid connected record. Malformed optional fields only
		// disable presentation, never an otherwise valid membership/evidence read.
		for (const TCHAR* Field : {TEXT("disconnectedAt"), TEXT("reconnectExpiresAt")})
		{
			if (Object->HasField(Field) && !Object->HasTypedField<EJson::Null>(Field) && !Object->HasTypedField<EJson::String>(Field)) return;
			FString Value;
			if (Object->HasTypedField<EJson::String>(Field) && (!Object->TryGetStringField(Field, Value) || Value.IsEmpty())) return;
		}
		if (bDisconnected != bDeadline) return;
		if (bDisconnected)
		{
			FDateTime First, End;
			if (!IsUtc(Disconnected) || !IsUtc(Deadline) || !FDateTime::ParseIso8601(*Disconnected, First)
				|| !FDateTime::ParseIso8601(*Deadline, End) || End - First != FTimespan::FromMinutes(10)) return;
		}
		int64 DisconnectedGeneration = 0;
		const bool bHasDisconnectedGeneration = Object->HasField(TEXT("disconnectedGeneration"))
			&& !Object->HasTypedField<EJson::Null>(TEXT("disconnectedGeneration"));
		if (bHasDisconnectedGeneration && (!bDisconnected
			|| !ReadGeneration(Object, TEXT("disconnectedGeneration"), DisconnectedGeneration)
			|| DisconnectedGeneration > ParsedGeneration)) return;
		Record.ConnectionGeneration = ParsedGeneration;
		Record.DisconnectedGeneration = DisconnectedGeneration;
		Record.bHasDisconnectedGeneration = bHasDisconnectedGeneration;
		Record.DisconnectedAt = MoveTemp(Disconnected);
		Record.ReconnectExpiresAt = MoveTemp(Deadline);
		Record.bHasConnectionPresentation = true;
	}
}

bool FRPGDungeonMembershipSnapshot::IsExplicitlyAbandoned(const FString& ExpectedSessionId,
	const FString& ExpectedServerId, const FString& CharacterId, const FString& SteamId) const
{
	if (ExpectedServerId.IsEmpty() || !IsState(State) || !DungeonSessionId.Equals(ExpectedSessionId, ESearchCase::CaseSensitive)
		|| !ServerId.Equals(ExpectedServerId, ESearchCase::CaseSensitive) || !IsSteamId(SteamId)) return false;
	// Also fail closed if a caller manually constructs an inconsistent read model.
	for (const FRPGDungeonMembershipRecord& Member : Members)
		if (Member.CharacterId.Equals(CharacterId, ESearchCase::CaseSensitive)) return false;
	for (const FRPGDungeonMembershipAbandonment& Abandonment : Abandonments)
		if (Abandonment.SteamId.Equals(SteamId, ESearchCase::CaseSensitive)
			&& Abandonment.Receipt.IsValidFor(ExpectedSessionId, CharacterId)) return true;
	return false;
}

bool FRPGDungeonMembershipSnapshot::IsDisconnectedExpelled(const FString& ExpectedSessionId,
	const FString& ExpectedServerId, const FString& CharacterId, const FString& SteamId) const
{
	if (ExpectedServerId.IsEmpty() || !IsState(State)
		|| !DungeonSessionId.Equals(ExpectedSessionId, ESearchCase::CaseSensitive)
		|| !ServerId.Equals(ExpectedServerId, ESearchCase::CaseSensitive)) return false;
	for (const FRPGDungeonMembershipRecord& Member : Members)
		if (Member.CharacterId.Equals(CharacterId, ESearchCase::CaseSensitive)) return false;
	for (const FRPGDungeonMembershipAbandonment& Abandonment : Abandonments)
		if (Abandonment.Receipt.CharacterId.Equals(CharacterId, ESearchCase::CaseSensitive)) return false;
	for (const FRPGDungeonMembershipExpulsion& Receipt : DisconnectionExpulsions)
		if (Receipt.CharacterId.Equals(CharacterId, ESearchCase::CaseSensitive)
			&& Receipt.SteamId.Equals(SteamId, ESearchCase::CaseSensitive)
			&& IsValidExpulsion(Receipt)) return true;
	return false;
}

bool FRPGDungeonMembershipSnapshotCodec::Decode(const FString& Body, int32 HttpStatus,
	const FString& ExpectedSessionId, const FString& ExpectedServerId, FRPGDungeonMembershipSnapshot& OutSnapshot)
{
	OutSnapshot = FRPGDungeonMembershipSnapshot();
	if (HttpStatus != 200 || Body.Len() > 64 * 1024 || !IsGuid(ExpectedSessionId) || ExpectedServerId.IsEmpty()) return false;
	TSharedPtr<FJsonObject> Object;
	FRPGDungeonMembershipSnapshot Parsed;
	const TArray<TSharedPtr<FJsonValue>>* Members = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* Abandonments = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* Expulsions = nullptr;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Body), Object)
		|| !ReadString(Object, TEXT("dungeonSessionId"), Parsed.DungeonSessionId)
		|| !Parsed.DungeonSessionId.Equals(ExpectedSessionId, ESearchCase::CaseSensitive)
		|| !ReadString(Object, TEXT("serverId"), Parsed.ServerId)
		|| !Parsed.ServerId.Equals(ExpectedServerId, ESearchCase::CaseSensitive)
		|| !ReadString(Object, TEXT("state"), Parsed.State) || !IsState(Parsed.State)
		|| !ReadString(Object, TEXT("updatedAt"), Parsed.UpdatedAt) || !IsUtc(Parsed.UpdatedAt)
		|| !ReadString(Object, TEXT("expiresAt"), Parsed.ExpiresAt) || !IsUtc(Parsed.ExpiresAt)
		|| !Object->TryGetArrayField(TEXT("members"), Members) || !Members
		|| !Object->TryGetArrayField(TEXT("abandonments"), Abandonments) || !Abandonments
		|| Members->Num() > 128 || Abandonments->Num() > 128 || Members->Num() + Abandonments->Num() > 128) return false;
	// Old backends omit this additive field. If present it must be a complete
	// array; malformed evidence invalidates the entire read, not just the entry.
	if (Object->HasField(TEXT("disconnectionExpulsions"))
		&& (!Object->TryGetArrayField(TEXT("disconnectionExpulsions"), Expulsions) || !Expulsions)) return false;
	if (Expulsions && Members->Num() + Abandonments->Num() + Expulsions->Num() > 128) return false;
	FString Observed;
	if (ReadString(Object, TEXT("observedAt"), Observed) && IsUtc(Observed)) Parsed.ObservedAt = MoveTemp(Observed);
	TSet<FGuid> SeenCharacters;
	for (const TSharedPtr<FJsonValue>& Value : *Members)
	{
		if (!Value.IsValid() || Value->Type != EJson::Object) return false;
		const TSharedPtr<FJsonObject> Member = Value->AsObject();
		FRPGDungeonMembershipRecord Record;
		if (!ReadString(Member, TEXT("characterId"), Record.CharacterId) || !IsGuid(Record.CharacterId)
			|| !ReadString(Member, TEXT("steamId"), Record.SteamId) || !IsSteamId(Record.SteamId)
			|| !ReadString(Member, TEXT("leaseExpiresAt"), Record.LeaseExpiresAt) || !IsUtc(Record.LeaseExpiresAt)) return false;
		FGuid Guid; FGuid::Parse(Record.CharacterId, Guid);
		if (SeenCharacters.Contains(Guid)) return false;
		ReadPresentation(Member, Record);
		SeenCharacters.Add(Guid); Parsed.Members.Add(MoveTemp(Record));
	}
	for (const TSharedPtr<FJsonValue>& Value : *Abandonments)
	{
		if (!Value.IsValid() || Value->Type != EJson::Object) return false;
		const TSharedPtr<FJsonObject> Receipt = Value->AsObject();
		FRPGDungeonMembershipAbandonment Record;
		Record.Receipt.DungeonSessionId = ExpectedSessionId;
		if (!ReadString(Receipt, TEXT("characterId"), Record.Receipt.CharacterId) || !IsGuid(Record.Receipt.CharacterId)
			|| !ReadString(Receipt, TEXT("steamId"), Record.SteamId) || !IsSteamId(Record.SteamId)
			|| !ReadString(Receipt, TEXT("abandonedAt"), Record.Receipt.AbandonedAt)
			|| !ReadString(Receipt, TEXT("sessionStateAtAbandonment"), Record.Receipt.SessionStateAtAbandonment)
			|| !Receipt->HasTypedField<EJson::Boolean>(TEXT("rewardExcluded"))
			|| !Receipt->TryGetBoolField(TEXT("rewardExcluded"), Record.Receipt.bRewardExcluded)
			|| !Record.Receipt.IsValidFor(ExpectedSessionId, Record.Receipt.CharacterId)) return false;
		FGuid Guid; FGuid::Parse(Record.Receipt.CharacterId, Guid);
		if (SeenCharacters.Contains(Guid)) return false;
		SeenCharacters.Add(Guid); Parsed.Abandonments.Add(MoveTemp(Record));
	}
	if (Expulsions) for (const TSharedPtr<FJsonValue>& Value : *Expulsions)
	{
		if (!Value.IsValid() || Value->Type != EJson::Object) return false;
		const TSharedPtr<FJsonObject> Receipt = Value->AsObject();
		FRPGDungeonMembershipExpulsion Record;
		if (!ReadString(Receipt, TEXT("characterId"), Record.CharacterId)
			|| !ReadString(Receipt, TEXT("steamId"), Record.SteamId)
			|| !ReadString(Receipt, TEXT("disconnectedAt"), Record.DisconnectedAt)
			|| !ReadString(Receipt, TEXT("expiredAt"), Record.ExpiredAt)
			|| !ReadString(Receipt, TEXT("processedAt"), Record.ProcessedAt)
			|| !Receipt->HasTypedField<EJson::Boolean>(TEXT("rewardExcluded"))
			|| !Receipt->TryGetBoolField(TEXT("rewardExcluded"), Record.bRewardExcluded)
			|| !IsValidExpulsion(Record)) return false;
		FGuid Guid; FGuid::Parse(Record.CharacterId, Guid);
		if (SeenCharacters.Contains(Guid)) return false;
		SeenCharacters.Add(Guid); Parsed.DisconnectionExpulsions.Add(MoveTemp(Record));
	}
	OutSnapshot = MoveTemp(Parsed);
	return true;
}

bool FRPGDungeonMembershipMonitor::Configure(const FString& InSessionId, const FString& InServerId)
{
	Reset();
	if (!IsGuid(InSessionId) || InServerId.IsEmpty() || InServerId.Len() > 256
		|| InServerId != InServerId.TrimStartAndEnd()) return false;
	for (const TCHAR Character : InServerId)
		if (Character < TEXT(' ') || Character == TEXT('\x7f')) return false;
	SessionId = InSessionId;
	ServerId = InServerId;
	return true;
}

void FRPGDungeonMembershipMonitor::Reset()
{
	SessionId.Reset();
	ServerId.Reset();
	AbandonedCharacters.Reset();
	ExpelledCharacters.Reset();
	ActiveReadSerial = 0;
	// Do not reset the serial: a callback from the old world/scope may still arrive.
}

bool FRPGDungeonMembershipMonitor::BeginRead(uint64& OutReadSerial)
{
	OutReadSerial = 0;
	if (SessionId.IsEmpty() || ServerId.IsEmpty() || ActiveReadSerial != 0) return false;
	ActiveReadSerial = ++NextReadSerial;
	if (ActiveReadSerial == 0) ActiveReadSerial = ++NextReadSerial;
	OutReadSerial = ActiveReadSerial;
	return true;
}

bool FRPGDungeonMembershipMonitor::FinishRead(uint64 ReadSerial, const FRPGDungeonMembershipSnapshotResult& Result)
{
	if (ReadSerial == 0 || ReadSerial != ActiveReadSerial) return false;
	ActiveReadSerial = 0;
	if (!Result.bSuccess || Result.HttpStatus != 200
		|| Result.Snapshot.DungeonSessionId != SessionId || Result.Snapshot.ServerId != ServerId) return false;
	TMap<FGuid, FString> Evidence = AbandonedCharacters;
	TMap<FGuid, FString> ExpulsionEvidence = ExpelledCharacters;
	for (const FRPGDungeonMembershipAbandonment& Entry : Result.Snapshot.Abandonments)
	{
		if (!Result.Snapshot.IsExplicitlyAbandoned(SessionId, ServerId, Entry.Receipt.CharacterId, Entry.SteamId)) return false;
		FGuid CharacterGuid;
		if (!FGuid::ParseExact(Entry.Receipt.CharacterId, EGuidFormats::DigitsWithHyphens, CharacterGuid)) return false;
		if (const FString* ExistingSteamId = Evidence.Find(CharacterGuid))
			if (*ExistingSteamId != Entry.SteamId) return false;
		if (ExpulsionEvidence.Contains(CharacterGuid)) return false;
		Evidence.Add(CharacterGuid, Entry.SteamId);
	}
	for (const FRPGDungeonMembershipExpulsion& Entry : Result.Snapshot.DisconnectionExpulsions)
	{
		if (!Result.Snapshot.IsDisconnectedExpelled(SessionId, ServerId, Entry.CharacterId, Entry.SteamId)) return false;
		FGuid CharacterGuid;
		if (!FGuid::ParseExact(Entry.CharacterId, EGuidFormats::DigitsWithHyphens, CharacterGuid)) return false;
		if (Evidence.Contains(CharacterGuid)) return false;
		if (const FString* ExistingSteamId = ExpulsionEvidence.Find(CharacterGuid))
			if (*ExistingSteamId != Entry.SteamId) return false;
		ExpulsionEvidence.Add(CharacterGuid, Entry.SteamId);
	}
	if (Evidence.Num() + ExpulsionEvidence.Num() > 128) return false;
	// Only immutable receipts are accumulated. A later empty/error view cannot
	// re-admit a character, and member records never grant gameplay authority.
	AbandonedCharacters = MoveTemp(Evidence);
	ExpelledCharacters = MoveTemp(ExpulsionEvidence);
	return true;
}

bool FRPGDungeonMembershipMonitor::IsExplicitlyAbandoned(const FString& CharacterId, const FString& SteamId) const
{
	FGuid CharacterGuid;
	if (!FGuid::ParseExact(CharacterId, EGuidFormats::DigitsWithHyphens, CharacterGuid) || !CharacterGuid.IsValid()) return false;
	const FString* Account = AbandonedCharacters.Find(CharacterGuid);
	return Account && *Account == SteamId;
}

bool FRPGDungeonMembershipMonitor::IsDisconnectedExpelled(const FString& CharacterId, const FString& SteamId) const
{
	FGuid CharacterGuid;
	if (!FGuid::ParseExact(CharacterId, EGuidFormats::DigitsWithHyphens, CharacterGuid) || !CharacterGuid.IsValid()) return false;
	const FString* Account = ExpelledCharacters.Find(CharacterGuid);
	return Account && *Account == SteamId;
}
