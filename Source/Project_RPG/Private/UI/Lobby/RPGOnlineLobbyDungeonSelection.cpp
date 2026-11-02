#include "UI/Lobby/RPGOnlineLobbyWidget.h"
#include "Components/TextBlock.h"
#include "Online/RPGDungeonDefinition.h"
#include "UI/Lobby/RPGOnlineLobbyDevelopmentProfile.h"
#include "Misc/CommandLine.h"

namespace
{
	bool SameId(const FString& A, const FString& B) { return A.Equals(B, ESearchCase::CaseSensitive); }
	FString Option(const URPGOnlineLobbyWidget& Widget, const FString& Id)
	{
		return Widget.GetDungeonDisplayName(Id).ToString() + TEXT(" [") + Id + TEXT("]");
	}
	void UpdateOptions(UComboBoxString& Box, const TArray<FString>& Options)
	{
		bool bSame = Box.GetOptionCount() == Options.Num();
		for (int32 Index = 0; bSame && Index < Options.Num(); ++Index)
			bSame = SameId(Box.GetOptionAtIndex(Index), Options[Index]);
		if (bSame) return; // preserve focus/open dropdown during ordinary presentation ticks
		Box.ClearOptions();
		for (const FString& Value : Options) Box.AddOption(Value);
	}
}

void URPGOnlineLobbyWidget::LoadDungeonDefinitions()
{
	LoadedDungeonDefinitions.Reset();
	DungeonDefinitionsLoadError.Reset();
	const auto Profile = RPGOnlineLobbyDevelopmentProfile::Resolve(FCommandLine::Get(), IsDevelopmentUIEnabled());
	if (!Profile.Error.IsEmpty()) { DungeonDefinitionsLoadError = Profile.Error; return; }
	if (Profile.bTraining)
	{
		// Instance-only selection; never edit the CDO, INI, authored assets or a
		// backend-owned unfinished session. The ordinary validators still apply.
		DungeonDefinitions = {TSoftObjectPtr<URPGDungeonDefinition>(FSoftObjectPath(
			TEXT("/Game/System/Dungeons/Progression/DA_Dungeon_Training.DA_Dungeon_Training")))};
		if (Profile.bIntro1) DungeonDefinitions.Add(TSoftObjectPtr<URPGDungeonDefinition>(FSoftObjectPath(
			TEXT("/Game/System/Dungeons/Progression/DA_Dungeon_Intro1.DA_Dungeon_Intro1"))));
	}
	if (DungeonDefinitions.Num() > 64) { DungeonDefinitionsLoadError = TEXT("too_many_dungeon_definitions"); return; }
	for (const auto& Reference : DungeonDefinitions)
	{
		auto* Definition = Reference.LoadSynchronous();
		if (!Definition) { DungeonDefinitionsLoadError = TEXT("dungeon_definition_unavailable"); continue; }
		LoadedDungeonDefinitions.Add(Definition);
	}
	if (Profile.bTraining && DungeonDefinitionsLoadError.IsEmpty())
	{
		const TArray<FString> Expected = Profile.bIntro1
			? TArray<FString>{TEXT("Progression.Training"), TEXT("Progression.Intro1")}
			: TArray<FString>{TEXT("Progression.Training")};
		bool bValid = LoadedDungeonDefinitions.Num() == Expected.Num();
		for (int32 Index = 0; bValid && Index < Expected.Num(); ++Index)
		{
			const auto* Definition = LoadedDungeonDefinitions[Index].Get();
			bValid = Definition && Definition->DungeonId.Equals(Expected[Index], ESearchCase::CaseSensitive)
				&& Definition->DefaultDifficulty == ERPGGameDifficulty::Normal
				&& Definition->Difficulties == TArray<ERPGGameDifficulty>{ERPGGameDifficulty::Normal};
		}
		if (!bValid)
		{
			DungeonDefinitionsLoadError = TEXT("development_dungeon_profile_definition_mismatch");
			return;
		}
		DungeonId = Expected[0];
		Difficulty = TEXT("Normal");
	}
}

bool URPGOnlineLobbyWidget::ValidateDungeonDefinitions(FString& OutError) const
{
	OutError = DungeonDefinitionsLoadError;
	if (!OutError.IsEmpty()) return false;
	if (DungeonDefinitions.IsEmpty())
	{
		if (!URPGDungeonDefinition::IsValidDungeonId(DungeonId)) OutError = TEXT("invalid_legacy_dungeon_id");
		return OutError.IsEmpty();
	}
	if (DungeonDefinitions.Num() > 64 || LoadedDungeonDefinitions.Num() != DungeonDefinitions.Num())
	{ OutError = TEXT("dungeon_definitions_not_loaded"); return false; }
	TArray<FString> Seen;
	for (const URPGDungeonDefinition* Definition : LoadedDungeonDefinitions)
	{
		if (!IsValid(Definition)) { OutError = TEXT("dungeon_definition_unavailable"); return false; }
		if (!Definition->ValidateSelection(OutError)) return false;
		if (Seen.ContainsByPredicate([&](const FString& Id) { return SameId(Id, Definition->DungeonId); }))
		{ OutError = TEXT("duplicate_dungeon_definition_id"); return false; }
		Seen.Add(Definition->DungeonId);
	}
	return true;
}

const URPGDungeonDefinition* URPGOnlineLobbyWidget::FindDungeonDefinition(const FString& InDungeonId) const
{
	for (const URPGDungeonDefinition* Definition : LoadedDungeonDefinitions)
		if (IsValid(Definition) && SameId(Definition->DungeonId, InDungeonId)) return Definition;
	return nullptr;
}

TArray<FString> URPGOnlineLobbyWidget::GetDungeonOptions() const
{
	FString Error;
	if (!ValidateDungeonDefinitions(Error)) return {};
	if (DungeonDefinitions.IsEmpty()) return {DungeonId};
	TArray<FString> Ids;
	for (const URPGDungeonDefinition* Definition : LoadedDungeonDefinitions) Ids.Add(Definition->DungeonId);
	return Ids;
}

FText URPGOnlineLobbyWidget::GetDungeonDisplayName(const FString& InDungeonId) const
{
	FString Error;
	if (ValidateDungeonDefinitions(Error))
		if (const auto* Definition = FindDungeonDefinition(InDungeonId)) return Definition->DisplayName;
	return FText::FromString(InDungeonId); // an unknown existing server session is never relabelled as the default
}

bool URPGOnlineLobbyWidget::GetSelectedDungeonId(FString& OutDungeonId) const
{
	OutDungeonId.Reset();
	const FString Id = GetDisplayedDungeonId();
	if (HasUnfinishedDungeonSelection())
	{
		if (!URPGDungeonDefinition::IsValidDungeonId(Id)) return false;
	}
	else if (!GetDungeonOptions().ContainsByPredicate([&](const FString& Candidate) { return SameId(Id, Candidate); })) return false;
	OutDungeonId = Id;
	return true;
}

bool URPGOnlineLobbyWidget::CanSelectDungeon() const
{
	FString Error;
	return CurrentPresentation().bCanCreateSession && Flow && !HasUnfinishedDungeonSelection()
		&& (!Steam || !Steam->IsInPartyLobby()) && ValidateDungeonDefinitions(Error);
}

TArray<ERPGGameDifficulty> URPGOnlineLobbyWidget::GetAvailableDungeonDifficultyOptions() const
{
	// A session received from the backend remains readable even if the local catalog changed.
	if (HasUnfinishedDungeonSelection()) return GetDungeonDifficultyOptions();
	FString Id;
	if (!GetSelectedDungeonId(Id)) return {};
	if (const auto* Definition = FindDungeonDefinition(Id)) return Definition->Difficulties;
	return GetDungeonDifficultyOptions();
}

bool URPGOnlineLobbyWidget::SelectDungeon(const FString& InDungeonId)
{
	if (!CanSelectDungeon() || !GetDungeonOptions().ContainsByPredicate(
		[&](const FString& Candidate) { return SameId(Candidate, InDungeonId); }))
	{ RefreshPresentation(); return false; }
	DungeonId = InDungeonId;
	if (const auto* Definition = FindDungeonDefinition(InDungeonId))
	{
		ERPGGameDifficulty Previous;
		if (!GetSelectedDungeonDifficulty(Previous))
			Difficulty = StaticEnum<ERPGGameDifficulty>()->GetNameStringByValue(static_cast<int64>(Definition->DefaultDifficulty));
	}
	UIError.Reset();
	RefreshPresentation();
	return true; // selection alone never starts HTTP, travel, settlement or server configuration
}

void URPGOnlineLobbyWidget::HandleDungeonSelected(FString SelectedOption, ESelectInfo::Type)
{
	if (bUpdatingDungeonOptions) return;
	for (const FString& Id : GetDungeonOptions())
		if (SameId(SelectedOption, Option(*this, Id))) { SelectDungeon(Id); return; }
	RefreshPresentation();
}

bool URPGOnlineLobbyWidget::UpdateDungeonSelectionPresentation()
{
	TGuardValue<bool> UpdatingDungeon(bUpdatingDungeonOptions, true);
	TGuardValue<bool> UpdatingDifficulty(bUpdatingDifficultyOptions, true);
	const FString Id = GetDisplayedDungeonId();
	FString SelectedId;
	const bool bValidId = GetSelectedDungeonId(SelectedId);
	if (DungeonsBox)
	{
		TArray<FString> Options;
		for (const FString& Candidate : GetDungeonOptions()) Options.Add(Option(*this, Candidate));
		const FString SelectedOption = Option(*this, Id);
		if (HasUnfinishedDungeonSelection() && bValidId
			&& !Options.ContainsByPredicate([&](const FString& Value) { return SameId(Value, SelectedOption); })) Options.Add(SelectedOption);
		UpdateOptions(*DungeonsBox, Options);
		if (bValidId) DungeonsBox->SetSelectedOption(SelectedOption);
		else DungeonsBox->ClearSelection();
		DungeonsBox->SetIsEnabled(CanSelectDungeon());
	}
	ERPGGameDifficulty Value;
	const bool bValidDifficulty = GetSelectedDungeonDifficulty(Value);
	const FString DisplayedDifficulty = GetDisplayedDungeonDifficulty();
	if (DifficultyBox)
	{
		TArray<FString> Options;
		for (const auto DifficultyValue : GetAvailableDungeonDifficultyOptions())
			Options.Add(StaticEnum<ERPGGameDifficulty>()->GetNameStringByValue(static_cast<int64>(DifficultyValue)));
		UpdateOptions(*DifficultyBox, Options);
		if (bValidDifficulty) DifficultyBox->SetSelectedOption(DisplayedDifficulty);
		else DifficultyBox->ClearSelection();
		DifficultyBox->SetIsEnabled(CanSelectDungeonDifficulty());
	}
	if (DungeonSelectionText)
	{
		FString Error;
		if (!HasUnfinishedDungeonSelection() && !ValidateDungeonDefinitions(Error))
			DungeonSelectionText->SetText(FText::Format(NSLOCTEXT("RPGOnlineLobby", "InvalidDungeonCatalog", "던전 목록 설정 오류: {0}"), FText::FromString(Error)));
		else DungeonSelectionText->SetText(FText::Format(NSLOCTEXT("RPGOnlineLobby", "AuthoredDungeonSelection", "던전: {0} / 난이도: {1} — 생성 후 변경할 수 없습니다."),
			bValidId ? GetDungeonDisplayName(Id) : NSLOCTEXT("RPGOnlineLobby", "DungeonRequired", "던전을 선택하세요"),
			bValidDifficulty ? FText::FromString(DisplayedDifficulty) : NSLOCTEXT("RPGOnlineLobby", "DifficultyRequired", "난이도를 선택하세요")));
	}
	return bValidId && bValidDifficulty;
}
