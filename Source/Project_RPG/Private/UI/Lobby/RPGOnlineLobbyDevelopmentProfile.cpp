#include "UI/Lobby/RPGOnlineLobbyDevelopmentProfile.h"
#include "Misc/CommandLine.h"

RPGOnlineLobbyDevelopmentProfile::FSelection RPGOnlineLobbyDevelopmentProfile::Resolve(
	const TCHAR* CommandLine, bool bDevelopmentUIEnabled)
{
	TArray<FString> Tokens, Switches;
	FCommandLine::Parse(CommandLine, Tokens, Switches);
	int32 Count = 0;
	FString Name;
	for (const FString& Switch : Switches)
	{
		FString Key, Value;
		if (!Switch.Split(TEXT("="), &Key, &Value)) Key = Switch;
		if (!Key.Equals(TEXT("RPGDungeonProfile"), ESearchCase::IgnoreCase)) continue;
		++Count;
		Name = Value.TrimQuotes();
	}
	if (Count == 0) return {};
	if (Count != 1) return {false, TEXT("ambiguous_development_dungeon_profile")};
	if (!bDevelopmentUIEnabled) return {false, TEXT("development_dungeon_profile_disabled")};
	if (!Name.Equals(TEXT("Training"), ESearchCase::CaseSensitive))
		return {false, TEXT("unknown_development_dungeon_profile")};
	return {true, {}};
}
