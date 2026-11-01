#include "Online/RPGDungeonAbandonment.h"

#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

bool FRPGDungeonAbandonmentReceipt::IsValidFor(const FString& SessionId,
	const FString& SelectedCharacterId) const
{
	FGuid SessionGuid, CharacterGuid;
	FDateTime ParsedTime;
	return FGuid::ParseExact(SessionId, EGuidFormats::DigitsWithHyphens, SessionGuid) && SessionGuid.IsValid()
		&& FGuid::ParseExact(SelectedCharacterId, EGuidFormats::DigitsWithHyphens, CharacterGuid) && CharacterGuid.IsValid()
		&& DungeonSessionId.Equals(SessionId, ESearchCase::CaseSensitive)
		&& CharacterId.Equals(SelectedCharacterId, ESearchCase::CaseSensitive)
		&& bRewardExcluded
		&& (SessionStateAtAbandonment.Equals(TEXT("Loading"), ESearchCase::CaseSensitive)
			|| SessionStateAtAbandonment.Equals(TEXT("InProgress"), ESearchCase::CaseSensitive)
			|| SessionStateAtAbandonment.Equals(TEXT("Closed"), ESearchCase::CaseSensitive))
		&& AbandonedAt.Contains(TEXT("T"), ESearchCase::CaseSensitive)
		&& (AbandonedAt.EndsWith(TEXT("Z"), ESearchCase::CaseSensitive) || AbandonedAt.EndsWith(TEXT("+00:00")))
		&& FDateTime::ParseIso8601(*AbandonedAt, ParsedTime);
}

bool FRPGDungeonAbandonmentCodec::Decode(const FString& Body, int32 HttpStatus,
	const FString& SessionId, const FString& CharacterId, FRPGDungeonAbandonmentReceipt& OutReceipt)
{
	OutReceipt = FRPGDungeonAbandonmentReceipt();
	if (HttpStatus != 200) return false;
	TSharedPtr<FJsonObject> Object;
	FRPGDungeonAbandonmentReceipt Parsed;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Body), Object) || !Object.IsValid()
		|| !Object->TryGetStringField(TEXT("dungeonSessionId"), Parsed.DungeonSessionId)
		|| !Object->TryGetStringField(TEXT("characterId"), Parsed.CharacterId)
		|| !Object->TryGetStringField(TEXT("abandonedAt"), Parsed.AbandonedAt)
		|| !Object->TryGetStringField(TEXT("sessionStateAtAbandonment"), Parsed.SessionStateAtAbandonment)
		|| !Object->HasTypedField<EJson::Boolean>(TEXT("rewardExcluded"))
		|| !Object->TryGetBoolField(TEXT("rewardExcluded"), Parsed.bRewardExcluded)
		|| !Parsed.IsValidFor(SessionId, CharacterId)) return false;
	OutReceipt = MoveTemp(Parsed);
	return true;
}
