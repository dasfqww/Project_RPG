#include "Manager/HttpWebManager.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/PrimaryAssetId.h"

bool FRPGCharacterCreationOption::DecodeList(const FString& Body, TArray<FRPGCharacterCreationOption>& OutOptions)
{
	OutOptions.Reset();
	TArray<TSharedPtr<FJsonValue>> Values;
	if (Body.Len() > 64 * 1024 || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Body), Values)
		|| Values.Num() > 32) return false;
	TSet<FString> Ids, Labels;
	TArray<FRPGCharacterCreationOption> Parsed;
	for (const auto& Value : Values)
	{
		if (!Value || Value->Type != EJson::Object) return false;
		const auto Object = Value->AsObject();
		FRPGCharacterCreationOption Option;
		if (!Object || !Object->TryGetStringField(TEXT("classId"), Option.ClassId) || Option.ClassId.Len() > 160
			|| !Object->TryGetStringField(TEXT("displayName"), Option.DisplayName) || Option.DisplayName.TrimStartAndEnd().IsEmpty()
			|| Option.DisplayName.Len() > 128 || !Object->TryGetStringField(TEXT("description"), Option.Description)
			|| Option.Description.Len() > 1024 || Ids.Contains(Option.ClassId) || Labels.Contains(Option.DisplayName)) return false;
		const FPrimaryAssetId ClassId(Option.ClassId);
		if (!ClassId.IsValid() || ClassId.PrimaryAssetType != FPrimaryAssetType(TEXT("RPGPlayerClassDefinition"))) return false;
		Ids.Add(Option.ClassId); Labels.Add(Option.DisplayName); Parsed.Add(MoveTemp(Option));
	}
	OutOptions = MoveTemp(Parsed);
	return true;
}

bool FRPGBackendCharacter::Decode(const TSharedPtr<FJsonObject>& Object, FRPGBackendCharacter& OutCharacter)
{
	OutCharacter = {};
	if (!Object || !Object->TryGetStringField(TEXT("characterId"), OutCharacter.CharacterId)
		|| !Object->TryGetStringField(TEXT("rosterId"), OutCharacter.RosterId)
		|| !Object->TryGetStringField(TEXT("name"), OutCharacter.Name)
		|| !Object->TryGetStringField(TEXT("createdAt"), OutCharacter.CreatedAt)
		|| OutCharacter.CharacterId.IsEmpty() || OutCharacter.RosterId.IsEmpty() || OutCharacter.Name.IsEmpty()) return false;

	// Identity remains usable with an old server or an unavailable display
	// projection. This metadata never initializes or restores gameplay growth.
	const TSharedPtr<FJsonObject>* Summary = nullptr;
	if (!Object->TryGetObjectField(TEXT("progression"), Summary) || !Summary || !Summary->IsValid()) return true;
	FString State;
	if (!(*Summary)->TryGetStringField(TEXT("state"), State)) return true;
	if (State == TEXT("Missing")) { OutCharacter.ProgressionState = State; return true; }
	if (State != TEXT("Available")) return true;
	FString ClassId;
	double Level = 0;
	const auto* LevelField = (*Summary)->Values.Find(TEXT("level"));
	if (!(*Summary)->TryGetStringField(TEXT("classId"), ClassId) || ClassId.Len() > 160
		|| !LevelField || !LevelField->IsValid() || (*LevelField)->Type != EJson::Number
		|| !(*Summary)->TryGetNumberField(TEXT("level"), Level) || !FMath::IsFinite(Level)
		|| Level < 1 || Level > MAX_int32 || FMath::FloorToDouble(Level) != Level) return true;
	const FPrimaryAssetId ParsedId(ClassId);
	if (!ParsedId.IsValid() || ParsedId.PrimaryAssetType != FPrimaryAssetType(TEXT("RPGPlayerClassDefinition"))) return true;
	OutCharacter.ProgressionState = State;
	OutCharacter.ClassId = MoveTemp(ClassId);
	OutCharacter.CharacterLevel = static_cast<int32>(Level);
	return true;
}
