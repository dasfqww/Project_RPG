#include "Online/RPGDungeonServerDefinition.h"
#include "Online/RPGDungeonDefinition.h"
#include "Economy/RPGDungeonRewardDefinition.h"
#include "Engine/World.h"
#include "Misc/PackageName.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

bool URPGDungeonServerDefinition::ValidateDefinition(FString& OutError) const
{
	OutError.Reset();
	if (!URPGDungeonDefinition::IsValidDungeonId(DungeonId)) { OutError = TEXT("invalid_server_dungeon_id"); return false; }
	const auto Path = DungeonMap.ToSoftObjectPath();
	const FString Package = Path.GetLongPackageName();
	if (Path.IsNull() || !FPackageName::IsValidLongPackageName(Package) || !Path.GetSubPathString().IsEmpty()
		|| Path.GetAssetName() != FPackageName::GetShortName(Package))
	{ OutError = TEXT("invalid_server_dungeon_map"); return false; }
	if (ClearRewardsByDifficulty.IsEmpty() || ClearRewardsByDifficulty.Num() > 4)
	{ OutError = TEXT("invalid_server_dungeon_rewards"); return false; }
	for (const auto& Pair : ClearRewardsByDifficulty)
	{
		if ((Pair.Key != ERPGGameDifficulty::Easy && Pair.Key != ERPGGameDifficulty::Normal
			&& Pair.Key != ERPGGameDifficulty::Hard && Pair.Key != ERPGGameDifficulty::Hell) || !IsValid(Pair.Value.Get()))
		{ OutError = TEXT("invalid_server_dungeon_rewards"); return false; }
		FString Version; TArray<FRPGCurrencyChange> Currency; TArray<FRPGDungeonItemReward> Items;
		if (!Pair.Value->BuildSettlement(Version, Currency, Items, OutError)) return false;
	}
	return true;
}

bool URPGDungeonServerDefinition::ValidateCatalog(const TArray<TObjectPtr<URPGDungeonServerDefinition>>& Definitions, FString& OutError)
{
	OutError.Reset();
	if (Definitions.IsEmpty() || Definitions.Num() > 64) { OutError = TEXT("invalid_server_dungeon_catalog"); return false; }
	TArray<FString> Seen;
	for (const URPGDungeonServerDefinition* Definition : Definitions)
	{
		if (!IsValid(Definition)) { OutError = TEXT("missing_server_dungeon_definition"); return false; }
		if (!Definition->ValidateDefinition(OutError)) return false;
		if (Seen.ContainsByPredicate([&](const FString& Id) { return Id.Equals(Definition->DungeonId, ESearchCase::CaseSensitive); }))
		{ OutError = TEXT("duplicate_server_dungeon_id"); return false; }
		Seen.Add(Definition->DungeonId);
	}
	return true;
}

bool URPGDungeonServerDefinition::BuildAssignedSelection(const TArray<TObjectPtr<URPGDungeonServerDefinition>>& Definitions,
	const FString& Id, const FString& ActualMapPackage, ERPGGameDifficulty Difficulty,
	FRPGDungeonServerSelection& OutSelection, FString& OutError)
{
	OutSelection = {};
	if (!ValidateCatalog(Definitions, OutError)) return false;
	for (const URPGDungeonServerDefinition* Definition : Definitions)
	{
		if (!Definition->DungeonId.Equals(Id, ESearchCase::CaseSensitive)) continue;
		if (!Definition->DungeonMap.ToSoftObjectPath().GetLongPackageName().Equals(
			UWorld::RemovePIEPrefix(ActualMapPackage), ESearchCase::CaseSensitive))
		{ OutError = TEXT("server_dungeon_map_mismatch"); return false; }
		const auto* Reward = Definition->ClearRewardsByDifficulty.Find(Difficulty);
		if (!Reward || !IsValid(Reward->Get())) { OutError = TEXT("server_dungeon_difficulty_unsupported"); return false; }
		FRPGDungeonServerSelection Built;
		if (!Reward->Get()->BuildSettlement(Built.RewardVersion, Built.CurrencyChanges, Built.ItemRewards, OutError)) return false;
		Built.Definition = Definition; Built.RewardDefinition = Reward->Get();
		OutSelection = MoveTemp(Built);
		return true;
	}
	OutError = TEXT("server_dungeon_id_unknown");
	return false;
}

#if WITH_EDITOR
EDataValidationResult URPGDungeonServerDefinition::IsDataValid(FDataValidationContext& Context) const
{
	const auto Result = Super::IsDataValid(Context);
	FString Error;
	if (ValidateDefinition(Error)) return Result;
	Context.AddError(FText::FromString(Error));
	return EDataValidationResult::Invalid;
}
#endif
