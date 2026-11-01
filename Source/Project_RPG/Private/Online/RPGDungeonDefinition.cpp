#include "Online/RPGDungeonDefinition.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

bool URPGDungeonDefinition::IsValidDungeonId(const FString& Id)
{
	if (Id.IsEmpty() || Id.Len() > 64) return false;
	for (TCHAR C : Id)
		if (!((C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9')
			|| C == '.' || C == '_' || C == '-')) return false;
	return true;
}

bool URPGDungeonDefinition::ValidateSelection(FString& OutError) const
{
	OutError.Reset();
	if (!IsValidDungeonId(DungeonId)) OutError = TEXT("invalid_dungeon_definition_id");
	else if (DisplayName.ToString().TrimStartAndEnd().IsEmpty()) OutError = TEXT("missing_dungeon_display_name");
	else if (Difficulties.IsEmpty() || Difficulties.Num() > 4) OutError = TEXT("invalid_dungeon_difficulties");
	else
	{
		TArray<ERPGGameDifficulty> Seen;
		for (const auto Value : Difficulties)
		{
			if ((Value != ERPGGameDifficulty::Easy && Value != ERPGGameDifficulty::Normal
				&& Value != ERPGGameDifficulty::Hard && Value != ERPGGameDifficulty::Hell) || Seen.Contains(Value))
			{ OutError = TEXT("invalid_dungeon_difficulties"); break; }
			Seen.Add(Value);
		}
		if (OutError.IsEmpty() && !Difficulties.Contains(DefaultDifficulty)) OutError = TEXT("invalid_dungeon_default_difficulty");
	}
	return OutError.IsEmpty();
}

#if WITH_EDITOR
EDataValidationResult URPGDungeonDefinition::IsDataValid(FDataValidationContext& Context) const
{
	const auto Result = Super::IsDataValid(Context);
	FString Error;
	if (ValidateSelection(Error)) return Result;
	Context.AddError(FText::FromString(Error));
	return EDataValidationResult::Invalid;
}
#endif
