#include "UI/Lobby/RPGCharacterConfigurationWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Manager/HttpWebManager.h"
#include "Online/RPGOnlineLobbySubsystem.h"
#include "UI/Lobby/RPGOnlineLobbyLayout.h"
#include "UI/Lobby/RPGOnlineLobbyWidget.h"
#include "UI/Lobby/RPGCharacterConfigurationPreview.h"
#include "PlayerClass/RPGPlayerClassDefinition.h"
#include "Skill/RPGSkillDefinition.h"
#include "Engine/AssetManager.h"

#define LOCTEXT_NAMESPACE "RPGCharacterConfiguration"

namespace
{
	const URPGSkillDefinition* SkillFor(const URPGPlayerClassDefinition* Class, FGameplayTag Tag)
	{
		const auto* Binding = Class ? Class->FindSkill(Tag) : nullptr;
		return Binding ? Binding->Definition.Get() : nullptr;
	}

	const FRPGSkillRefinementOption* OptionFor(const URPGSkillDefinition& Skill, int32 Level, const FString& Id)
	{
		int32 Tier, Index;
		return Skill.ResolveSavedRefinement(Level, FName(*Id), Tier, Index) ? &Skill.RefinementTiers[Tier].Options[Index] : nullptr;
	}

	int32 ChoiceIndex(const TArray<FString>& Choices, const FString& Id)
	{
		if (Id.IsEmpty()) return 0;
		const int32 Index = Choices.IndexOfByPredicate([&Id](const FString& Choice) { return Choice.Equals(Id, ESearchCase::CaseSensitive); });
		return Index == INDEX_NONE ? INDEX_NONE : Index + 1;
	}

	bool PrepareChoices(FRPGCharacterProgressionSnapshot& Record, const URPGPlayerClassDefinition& Class)
	{
		if (Record.ClassId != Class.GetPrimaryAssetId() || Record.LevelCosts.IsEmpty() || Record.RefinementLevels.Num() != 2) return false;
		int64 UsedSP = 0;
		for (const auto& Skill : Record.Skills)
		{
			const auto* Definition = SkillFor(&Class, Skill.SkillTag);
			auto* Choices = Record.ChoicesBySkill.Find(Skill.SkillTag);
			if (!Definition || !Choices || Skill.Level < 1 || Skill.Level > Definition->MaxSkillLevel
				|| Skill.Level > Record.LevelCosts.Num() || Skill.RefinementIds.Num() != 2) return false;
			for (int32 Tier = 0; Tier < 2; ++Tier)
			{
				const int32 Unlock = Record.RefinementLevels[Tier];
				Choices->Tiers[Tier].RemoveAll([&](const FString& Id) { return !OptionFor(*Definition, Unlock, Id); });
				if (ChoiceIndex(Choices->Tiers[Tier], Skill.RefinementIds[Tier]) == INDEX_NONE
					|| (!Skill.RefinementIds[Tier].IsEmpty() && Skill.Level < Unlock)) return false;
			}
			UsedSP += Record.LevelCosts[Skill.Level - 1];
		}
		return UsedSP == Record.UsedSP && UsedSP <= Record.TotalSP;
	}
	FText FailureMessage(const FRPGCharacterProgressionResult& Result)
	{
		if (Result.bOutcomeUnknown) return LOCTEXT("Unknown", "저장 결과를 확인하지 못했습니다. 같은 요청으로 결과를 다시 확인하세요.");
		if (Result.HttpStatus == 401) return LOCTEXT("Auth", "같은 계정으로 다시 로그인한 뒤 요청을 다시 시도하세요.");
		if (Result.HttpStatus == 403) return LOCTEXT("Forbidden", "성장 구성을 확인할 권한이 없어 저장을 잠갔습니다. 접속과 캐릭터 선택을 확인한 뒤 다시 읽어 주세요.");
		if (Result.HttpStatus == 404) return LOCTEXT("Missing", "성장 기록을 찾지 못해 저장을 잠갔습니다. 기록이 확인된 뒤 다시 읽어 주세요.");
		if (Result.Error == TEXT("stale_progression_revision")) return LOCTEXT("Conflict", "서버의 성장 기록이 변경되어 저장하지 않았습니다. 편집 내용은 유지됩니다. 최신 저장본으로 다시 시작해 주세요.");
		if (Result.Error == TEXT("active_dungeon_configuration_locked")) return LOCTEXT("Lease", "던전 참가 중에는 구성을 저장할 수 없습니다. 파티에서 나온 뒤 저장본을 다시 읽으세요.");
		if (Result.Error == TEXT("idempotency_conflict")) return LOCTEXT("RequestConflict", "이전 저장 요청과 충돌했습니다. 저장본을 다시 읽어 주세요.");
		return LOCTEXT("Failed", "요청을 완료하지 못했습니다. 연결 상태를 확인하고 다시 시도하세요.");
	}
}

void URPGCharacterConfigurationBinding::Selected(FString Option, ESelectInfo::Type)
{
	if (Editor.IsValid() && Box) Editor->HandleChoice(*this, Box->GetSelectedIndex());
}

void URPGCharacterConfigurationWidget::SetLobby(URPGOnlineLobbyWidget* InLobby) { Lobby = InLobby; }

TSharedRef<SWidget> URPGCharacterConfigurationWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		auto* Root = WidgetTree->ConstructWidget<UVerticalBox>();
		WidgetTree->RootWidget = Root;
		SummaryText = RPGOnlineLobbyLayout::Text(*WidgetTree, *Root, TEXT("ConfigurationSummary"), FText::GetEmpty());
		ResultText = RPGOnlineLobbyLayout::Text(*WidgetTree, *Root, TEXT("ConfigurationResult"), FText::GetEmpty());
		LoadButton = RPGOnlineLobbyLayout::Action(*WidgetTree, *Root, TEXT("LoadConfigurationButton"), LOCTEXT("Read", "저장본 다시 읽기"));
		EquipUnslottedButton = RPGOnlineLobbyLayout::Action(*WidgetTree, *Root, TEXT("EquipUnslottedSkillsButton"), LOCTEXT("EquipUnslotted", "빈 슬롯에 습득 스킬 넣기"));
		RPGOnlineLobbyLayout::Text(*WidgetTree, *Root, TEXT("EquipUnslottedHint"), LOCTEXT("EquipUnslottedHint", "기존 편성과 SP 투자는 유지합니다. 변경 내용은 구성 저장을 누른 뒤 적용됩니다."));
		SaveButton = RPGOnlineLobbyLayout::Action(*WidgetTree, *Root, TEXT("SaveConfigurationButton"), LOCTEXT("Save", "구성 저장"));
		ResetButton = RPGOnlineLobbyLayout::Action(*WidgetTree, *Root, TEXT("ResetConfigurationButton"), LOCTEXT("Reset", "변경 취소"));
		Choices = WidgetTree->ConstructWidget<UVerticalBox>();
		Root->AddChildToVerticalBox(Choices);
	}
	return Super::RebuildWidget();
}

void URPGCharacterConfigurationWidget::NativeConstruct()
{
	Super::NativeConstruct();
	bAttached = true;
	if (LoadButton) LoadButton->OnClicked.AddUniqueDynamic(this, &ThisClass::ClickLoad);
	if (SaveButton) SaveButton->OnClicked.AddUniqueDynamic(this, &ThisClass::ClickSave);
	if (ResetButton) ResetButton->OnClicked.AddUniqueDynamic(this, &ThisClass::ClickReset);
	if (EquipUnslottedButton) EquipUnslottedButton->OnClicked.AddUniqueDynamic(this, &ThisClass::ClickEquipUnslotted);
	RebuildChoices();
	RefreshControls();
}

void URPGCharacterConfigurationWidget::NativeDestruct()
{
	bAttached = false;
	++RequestSerial;
	bRequestInFlight = false;
	if (LoadButton) LoadButton->OnClicked.RemoveDynamic(this, &ThisClass::ClickLoad);
	if (SaveButton) SaveButton->OnClicked.RemoveDynamic(this, &ThisClass::ClickSave);
	if (ResetButton) ResetButton->OnClicked.RemoveDynamic(this, &ThisClass::ClickReset);
	if (EquipUnslottedButton) EquipUnslottedButton->OnClicked.RemoveDynamic(this, &ThisClass::ClickEquipUnslotted);
	Super::NativeDestruct();
}

void URPGCharacterConfigurationWidget::RefreshContext()
{
	auto* Owner = Lobby.Get();
	const FString CurrentCharacter = Owner && Owner->HasOwnedSelection() ? Owner->Flow->GetSelectedCharacterId() : FString();
	const FString CurrentSteam = Owner && Owner->Web ? Owner->Web->GetAuthenticatedSteamId() : FString();
	if (CurrentCharacter != CharacterId || CurrentSteam != SteamId || ContextWorld.Get() != GetWorld()
		|| ContextController.Get() != (Owner ? Owner->GetOwningPlayer() : nullptr))
	{
		++RequestSerial;
		CharacterId = CurrentCharacter; SteamId = CurrentSteam;
		ContextWorld = GetWorld(); ContextController = Owner ? Owner->GetOwningPlayer() : nullptr;
		LoadedClass = nullptr;
		Saved = {}; DraftSkills.Reset(); DraftSlots.Reset(); PendingCommand = {};
		bRequestInFlight = bLoaded = bNeedsReload = false;
		bInitialReadAttempted = false;
		bProgressionReadConfirmed = false;
		Message = FText::GetEmpty();
		RebuildChoices();
	}
	RefreshControls();
}

bool URPGCharacterConfigurationWidget::IsCurrentContext() const
{
	const auto* Owner = Lobby.Get();
	return bAttached && IsValid(Owner) && ContextWorld.IsValid() && ContextWorld.Get() == GetWorld()
		&& !GetWorld()->bIsTearingDown && ContextController.IsValid() && ContextController.Get() == Owner->GetOwningPlayer()
		&& Owner->Web && Owner->Flow && Owner->HasOwnedSelection() && !CharacterId.IsEmpty() && !SteamId.IsEmpty()
		&& Owner->Flow->GetSelectedCharacterId() == CharacterId && Owner->Web->GetAuthenticatedSteamId() == SteamId;
}

bool URPGCharacterConfigurationWidget::CanRequest(bool bWrite, bool bRetry) const
{
	const auto* Owner = Lobby.Get();
	return !bRequestInFlight && IsCurrentContext() && Owner->CanUseCharacterConfiguration(bWrite, bRetry)
		&& !Owner->Web->IsPlayerProgressionRequestInFlight();
}

const FRPGCharacterProgressionSnapshot* URPGCharacterConfigurationWidget::GetConfirmedProgression() const
{
	const auto* Owner = Lobby.Get();
	return bLoaded && bProgressionReadConfirmed && !bNeedsReload && !bRequestInFlight && !HasUnresolvedSave()
		&& IsCurrentContext() && Owner->Web->HasAccessToken()
		&& Saved.CharacterId.Equals(CharacterId, ESearchCase::CaseSensitive) ? &Saved : nullptr;
}

void URPGCharacterConfigurationWidget::EnsureInitialLoad()
{
	if (bLoaded || bInitialReadAttempted || HasUnresolvedSave() || HasUnsavedChanges() || !CanRequest(false)) return;
	bInitialReadAttempted = true;
	LoadSavedConfiguration();
}

bool URPGCharacterConfigurationWidget::CanEdit() const
{
	return bLoaded && !bNeedsReload && !HasUnresolvedSave() && CanRequest(true);
}

void URPGCharacterConfigurationWidget::RefreshLobby()
{
	if (Lobby.IsValid()) Lobby->RefreshPresentation();
}

bool URPGCharacterConfigurationWidget::LoadSavedConfiguration()
{
	if (HasUnresolvedSave() || !CanRequest(false)) return false;
	bInitialReadAttempted = true;
	bProgressionReadConfirmed = false;
	bRequestInFlight = true;
	Message = LOCTEXT("Loading", "성장 구성을 읽고 있습니다.");
	const uint64 Serial = ++RequestSerial;
	const TWeakObjectPtr<ThisClass> WeakThis(this);
	RefreshLobby();
	if (!IsCurrentContext() || !Lobby->Web->LoadPlayerCharacterProgression(CharacterId,
		[WeakThis, Serial](FRPGCharacterProgressionResult Result)
		{ if (WeakThis.IsValid()) WeakThis->HandleResult(Serial, false, MoveTemp(Result)); }))
	{
		bRequestInFlight = false;
		Message = LOCTEXT("TransportUnavailable", "요청을 시작할 수 없습니다. 로그인과 연결 상태를 확인하세요.");
		RefreshLobby();
		return false;
	}
	return true;
}

bool URPGCharacterConfigurationWidget::SaveConfiguration()
{
	const bool bRetry = HasUnresolvedSave();
	if (!CanRequest(true, bRetry) || (!bRetry && (!CanEdit() || !HasUnsavedChanges()))) return false;
	if (!bRetry)
	{
		PendingCommand.RequestId = FGuid::NewGuid();
		PendingCommand.ExpectedRevision = Saved.Revision;
		PendingCommand.Skills = DraftSkills;
		PendingCommand.SkillSlots = DraftSlots;
	}
	bRequestInFlight = true;
	Message = bRetry ? LOCTEXT("Checking", "이전 저장 결과를 확인하고 있습니다.") : LOCTEXT("Saving", "구성을 저장하고 있습니다.");
	const uint64 Serial = ++RequestSerial;
	const TWeakObjectPtr<ThisClass> WeakThis(this);
	RefreshLobby();
	if (!IsCurrentContext() || !Lobby->Web->SavePlayerCharacterConfiguration(CharacterId, PendingCommand,
		[WeakThis, Serial](FRPGCharacterProgressionResult Result)
		{ if (WeakThis.IsValid()) WeakThis->HandleResult(Serial, true, MoveTemp(Result)); }))
	{
		bRequestInFlight = false;
		// Keep the same ID/body for a retry, including when a prior attempt was uncertain.
		Message = LOCTEXT("SaveNotStarted", "저장 요청을 시작하지 못했습니다. 같은 요청으로 다시 시도하세요.");
		RefreshLobby();
		return false;
	}
	return true;
}

void URPGCharacterConfigurationWidget::HandleResult(uint64 Serial, bool bWrite, FRPGCharacterProgressionResult Result)
{
	if (!bAttached || Serial != RequestSerial) return;
	bRequestInFlight = false;
	if (!IsCurrentContext()) { RefreshContext(); return; }
	if (!Result.bSuccessful)
	{
		Message = FailureMessage(Result);
		// A rejected/failed read has not supplied a replacement for the last
		// confirmed record or the player's draft. Reauthentication can resume it.
		if (!bWrite)
		{
			bProgressionReadConfirmed = false;
			if (Result.HttpStatus == 403 || Result.HttpStatus == 404) bNeedsReload = true;
		}
		else if (!Result.bOutcomeUnknown && Result.HttpStatus != 401 && Result.HttpStatus != 429)
		{
			PendingCommand = {};
			bNeedsReload = true; // No automatic merge, new Request ID or replacement write.
		}
		RefreshLobby();
		return;
	}
	const FSoftObjectPath ClassPath = UAssetManager::Get().GetPrimaryAssetPath(Result.Snapshot.ClassId);
	auto* Class = Cast<URPGPlayerClassDefinition>(ClassPath.TryLoad());
	if (!Class || !PrepareChoices(Result.Snapshot, *Class) || Result.Snapshot.Revision < Saved.Revision
		|| (bWrite && Result.Snapshot.Revision <= PendingCommand.ExpectedRevision))
	{
		PendingCommand = {};
		bNeedsReload = true;
		Message = LOCTEXT("OldOrUnsupported", "최신 저장 구성을 다시 읽어 주세요. 이 결과는 현재 편집본에 적용하지 않았습니다.");
		RefreshLobby();
		return;
	}
	PendingCommand = {};
	LoadedClass = Class;
	Saved = MoveTemp(Result.Snapshot);
	DraftSkills = Saved.Skills;
	DraftSlots = Saved.SkillSlots;
	bLoaded = true; bNeedsReload = false;
	bProgressionReadConfirmed = true;
	Message = bWrite ? LOCTEXT("Saved", "구성을 저장했습니다.") : LOCTEXT("Loaded", "저장된 구성을 읽었습니다.");
	RebuildChoices();
	RefreshLobby();
}

int32 URPGCharacterConfigurationWidget::UsedDraftSP() const
{
	int32 Result = 0;
	for (const auto& Skill : DraftSkills) Result += Saved.LevelCosts[Skill.Level - 1];
	return Result;
}

bool URPGCharacterConfigurationWidget::HasUnsavedChanges() const
{
	if (!bLoaded || DraftSlots != Saved.SkillSlots || DraftSkills.Num() != Saved.Skills.Num()) return bLoaded;
	for (int32 Index = 0; Index < DraftSkills.Num(); ++Index)
		if (DraftSkills[Index].SkillTag != Saved.Skills[Index].SkillTag || DraftSkills[Index].Level != Saved.Skills[Index].Level
			|| DraftSkills[Index].RefinementIds != Saved.Skills[Index].RefinementIds) return true;
	return false;
}

bool URPGCharacterConfigurationWidget::ResetDraft()
{
	if (!CanEdit() || !HasUnsavedChanges()) return false;
	DraftSkills = Saved.Skills; DraftSlots = Saved.SkillSlots;
	Message = LOCTEXT("ResetDone", "변경 내용을 취소했습니다.");
	RefreshLobby();
	return true;
}

bool URPGCharacterConfigurationWidget::SetSkillLevel(FGameplayTag Tag, int32 Level)
{
	const auto* Definition = SkillFor(LoadedClass, Tag);
	if (!CanEdit() || !Definition || Level < 1 || Level > Definition->MaxSkillLevel || Level > Saved.LevelCosts.Num()) return false;
	auto* Skill = DraftSkills.FindByPredicate([Tag](const auto& Entry) { return Entry.SkillTag == Tag; });
	if (!Skill) return false;
	if (static_cast<int64>(UsedDraftSP()) - Saved.LevelCosts[Skill->Level - 1] + Saved.LevelCosts[Level - 1] > Saved.TotalSP)
	{
		Message = LOCTEXT("NotEnoughSP", "사용할 수 있는 SP가 부족합니다. 다른 스킬의 레벨을 먼저 낮추세요.");
		RefreshControls(); return false;
	}
	Skill->Level = Level;
	for (int32 Tier = 0; Tier < 2; ++Tier) if (Level < Saved.RefinementLevels[Tier]) Skill->RefinementIds[Tier].Reset();
	Message = LOCTEXT("DraftChanged", "변경 내용은 저장 후 적용됩니다.");
	RefreshLobby(); return true;
}

bool URPGCharacterConfigurationWidget::SetRefinementChoice(FGameplayTag Tag, int32 Tier, const FString& PersistenceId)
{
	if (!CanEdit() || Tier < 0 || Tier > 1) return false;
	auto* Skill = DraftSkills.FindByPredicate([Tag](const auto& Entry) { return Entry.SkillTag == Tag; });
	const auto* ChoicesForSkill = Saved.ChoicesBySkill.Find(Tag);
	if (!Skill || !ChoicesForSkill || ChoiceIndex(ChoicesForSkill->Tiers[Tier], PersistenceId) == INDEX_NONE
		|| (!PersistenceId.IsEmpty() && Skill->Level < Saved.RefinementLevels[Tier])) return false;
	Skill->RefinementIds[Tier] = PersistenceId;
	Message = LOCTEXT("DraftChanged", "변경 내용은 저장 후 적용됩니다.");
	RefreshLobby(); return true;
}

bool URPGCharacterConfigurationWidget::SetCombatSlot(int32 Index, FGameplayTag Tag)
{
	if (!CanEdit() || !DraftSlots.IsValidIndex(Index) || (Tag.IsValid()
		&& !DraftSkills.ContainsByPredicate([Tag](const auto& Entry) { return Entry.SkillTag == Tag; }))) return false;
	const int32 Other = Tag.IsValid() ? DraftSlots.IndexOfByKey(Tag) : INDEX_NONE;
	if (Other != INDEX_NONE && Other != Index) DraftSlots[Other] = DraftSlots[Index];
	DraftSlots[Index] = Tag;
	Message = LOCTEXT("DraftChanged", "변경 내용은 저장 후 적용됩니다.");
	RefreshLobby(); return true;
}

bool URPGCharacterConfigurationWidget::CanEquipUnslottedSkills() const
{
	return CanEdit() && DraftSlots.ContainsByPredicate([](FGameplayTag Tag) { return !Tag.IsValid(); })
		&& DraftSkills.ContainsByPredicate([this](const auto& Skill)
			{ return Skill.SkillTag.IsValid() && !DraftSlots.Contains(Skill.SkillTag); });
}

int32 URPGCharacterConfigurationWidget::EquipUnslottedSkills()
{
	if (!CanEquipUnslottedSkills()) return 0;
	TArray<FGameplayTag> Unslotted;
	for (const auto& Skill : DraftSkills)
		if (Skill.SkillTag.IsValid() && !DraftSlots.Contains(Skill.SkillTag)) Unslotted.Add(Skill.SkillTag);
	int32 Count = 0;
	for (FGameplayTag Skill : Unslotted)
	{
		const int32 Empty = DraftSlots.IndexOfByPredicate([](FGameplayTag Tag) { return !Tag.IsValid(); });
		if (Empty == INDEX_NONE || !SetCombatSlot(Empty, Skill)) break;
		++Count;
	}
	if (Count > 0)
	{
		Message = FText::Format(LOCTEXT("EquippedUnslotted", "습득한 스킬 {0}개를 빈 슬롯에 넣었습니다. 구성 저장을 누르면 적용됩니다."), FText::AsNumber(Count));
		RefreshLobby();
	}
	return Count;
}

void URPGCharacterConfigurationWidget::AddChoice(int32 Kind, int32 Row, int32 Tier, const FText& Label, const TArray<FString>& Options)
{
	RPGOnlineLobbyLayout::Text(*WidgetTree, *Choices, NAME_None, Label);
	auto* Binding = NewObject<URPGCharacterConfigurationBinding>(this);
	Binding->Editor = this; Binding->Kind = Kind; Binding->Row = Row; Binding->Tier = Tier;
	Binding->Box = WidgetTree->ConstructWidget<UComboBoxString>();
	for (const FString& Option : Options) Binding->Box->AddOption(Option);
	Binding->Box->OnSelectionChanged.AddUniqueDynamic(Binding, &URPGCharacterConfigurationBinding::Selected);
	RPGOnlineLobbyLayout::Control(*WidgetTree, *Choices, *Binding->Box);
	Bindings.Add(Binding);
}

void URPGCharacterConfigurationWidget::RebuildChoices()
{
	if (!Choices || !WidgetTree) return;
	for (URPGCharacterConfigurationBinding* Binding : Bindings) if (Binding && Binding->Box) Binding->Box->OnSelectionChanged.RemoveAll(Binding);
	Bindings.Reset(); PreviewTexts.Reset(); Choices->ClearChildren();
	PreviewTexts.SetNum(DraftSkills.Num());
	for (int32 Row = 0; Row < DraftSkills.Num(); ++Row)
	{
		const auto* Definition = SkillFor(LoadedClass, DraftSkills[Row].SkillTag);
		const auto& Allowed = Saved.ChoicesBySkill.FindChecked(DraftSkills[Row].SkillTag);
		TArray<FString> Levels;
		for (int32 Level = 1; Level <= FMath::Min(Saved.LevelCosts.Num(), Definition->MaxSkillLevel); ++Level) Levels.Add(FString::Printf(TEXT("%d"), Level));
		AddChoice(0, Row, 0, FText::Format(LOCTEXT("SkillLevel", "{0} — 레벨"), Definition->SkillName), Levels);
		for (int32 Tier = 0; Tier < 2; ++Tier)
		{
			if (Allowed.Tiers[Tier].IsEmpty()) continue;
			TArray<FString> Options{LOCTEXT("NoTripod", "선택 안 함").ToString()};
			for (const auto& Id : Allowed.Tiers[Tier])
				Options.Add(OptionFor(*Definition, Saved.RefinementLevels[Tier], Id)->OptionName.ToString());
			AddChoice(1, Row, Tier, FText::Format(LOCTEXT("TripodTier", "{0}레벨 스킬 연마"), FText::AsNumber(Saved.RefinementLevels[Tier])), Options);
		}
		auto* Preview = RPGOnlineLobbyLayout::Text(*WidgetTree, *Choices, NAME_None, GetSkillPreview(DraftSkills[Row].SkillTag));
		Preview->SetToolTipText(LOCTEXT("PreviewHelp", "편집 중인 선택의 기본 수치입니다. 저장 후 다음 전투 입장부터 적용되며, 전투 중 상태 보정과 적 방어는 포함하지 않습니다."));
		PreviewTexts[Row] = Preview;
	}
	if (DraftSlots.Num() == 8)
	{
		TArray<FString> Options{LOCTEXT("Empty", "빈 슬롯").ToString()};
		for (const auto& Skill : DraftSkills) Options.Add(SkillFor(LoadedClass, Skill.SkillTag)->SkillName.ToString());
		for (int32 Index = 0; Index < 8; ++Index)
			AddChoice(2, Index, 0, FText::Format(LOCTEXT("Slot", "전투 슬롯 {0}"), FText::AsNumber(Index + 1)), Options);
	}
}

void URPGCharacterConfigurationWidget::RefreshControls()
{
	TGuardValue<bool> Guard(bUpdating, true);
	const bool bEditable = CanEdit();
	if (SummaryText)
	{
		FText Summary = LOCTEXT("SelectFirst", "캐릭터를 선택한 뒤 저장된 성장 구성을 읽어 오세요.");
		if (bLoaded)
		{
			Summary = FText::Format(LOCTEXT("Summary", "레벨 {0} · 사용 SP {1} / {2}{3}"),
				FText::AsNumber(Saved.Level), FText::AsNumber(UsedDraftSP()), FText::AsNumber(Saved.TotalSP),
				HasUnsavedChanges() ? LOCTEXT("Unsaved", " · 저장하지 않은 변경") : FText::GetEmpty());
			if (bNeedsReload)
				Summary = FText::Format(LOCTEXT("CachedSummary", "{0}\n이전 조회 정보입니다. 최신 저장본을 확인해야 편집할 수 있습니다."), Summary);
		}
		SummaryText->SetText(Summary);
	}
	if (ResultText)
	{
		FText Display = Message;
		if (bLoaded && !bEditable && !bRequestInFlight && !HasUnresolvedSave() && !bNeedsReload
			&& Lobby.IsValid() && (Lobby->HasUnfinishedDungeonSelection() || (Lobby->Steam && Lobby->Steam->IsInPartyLobby())))
			Display = FText::Format(LOCTEXT("ReadOnly", "{0}\n던전 또는 Steam 파티에 참가한 동안에는 구성을 편집할 수 없습니다."), Message);
		ResultText->SetText(Display);
	}
	if (LoadButton)
	{
		LoadButton->SetIsEnabled(!HasUnresolvedSave() && CanRequest(false));
		if (auto* Text = Cast<UTextBlock>(LoadButton->GetContent()))
			Text->SetText(bNeedsReload && bLoaded ? LOCTEXT("RestartFromLatest", "최신 저장본으로 다시 시작") : LOCTEXT("Read", "저장본 다시 읽기"));
	}
	if (SaveButton)
	{
		SaveButton->SetIsEnabled(HasUnresolvedSave() ? CanRequest(true, true) : bEditable && HasUnsavedChanges());
		if (auto* Text = Cast<UTextBlock>(SaveButton->GetContent()))
			Text->SetText(HasUnresolvedSave() ? LOCTEXT("Retry", "저장 결과 재확인") : LOCTEXT("Save", "구성 저장"));
	}
	if (ResetButton) ResetButton->SetIsEnabled(bEditable && HasUnsavedChanges());
	if (EquipUnslottedButton) EquipUnslottedButton->SetIsEnabled(CanEquipUnslottedSkills());
	for (int32 Row = 0; Row < PreviewTexts.Num(); ++Row)
		if (PreviewTexts[Row] && DraftSkills.IsValidIndex(Row))
			PreviewTexts[Row]->SetText(GetSkillPreview(DraftSkills[Row].SkillTag));
	for (URPGCharacterConfigurationBinding* Binding : Bindings)
	{
		if (!Binding || !Binding->Box) continue;
		int32 Selected = INDEX_NONE;
		bool bUnlocked = true;
		if (Binding->Kind == 2 && DraftSlots.IsValidIndex(Binding->Row))
		{
			const FGameplayTag Tag = DraftSlots[Binding->Row];
			Selected = Tag.IsValid() ? DraftSkills.IndexOfByPredicate([Tag](const auto& Skill) { return Skill.SkillTag == Tag; }) + 1 : 0;
		}
		else if (DraftSkills.IsValidIndex(Binding->Row))
		{
			const auto& Skill = DraftSkills[Binding->Row];
			if (Binding->Kind == 0) Selected = Skill.Level - 1;
			else if (const auto* Allowed = Saved.ChoicesBySkill.Find(Skill.SkillTag))
			{
				Selected = ChoiceIndex(Allowed->Tiers[Binding->Tier], Skill.RefinementIds[Binding->Tier]);
				bUnlocked = Skill.Level >= Saved.RefinementLevels[Binding->Tier];
			}
		}
		Binding->Box->SetSelectedIndex(Selected);
		Binding->Box->SetIsEnabled(bEditable && bUnlocked);
	}
}

void URPGCharacterConfigurationWidget::HandleChoice(const URPGCharacterConfigurationBinding& Binding, int32 Choice)
{
	if (bUpdating || Choice == INDEX_NONE || !CanEdit()) return;
	if (Binding.Kind == 2 && Choice >= 0 && Choice <= DraftSkills.Num())
		SetCombatSlot(Binding.Row, Choice == 0 ? FGameplayTag() : DraftSkills[Choice - 1].SkillTag);
	else if (DraftSkills.IsValidIndex(Binding.Row))
	{
		const FGameplayTag Tag = DraftSkills[Binding.Row].SkillTag;
		if (Binding.Kind == 0) SetSkillLevel(Tag, Choice + 1);
		else if (Binding.Tier >= 0 && Binding.Tier < 2)
		{
			const auto& Allowed = Saved.ChoicesBySkill.FindChecked(Tag).Tiers[Binding.Tier];
			if (Choice >= 0 && Choice <= Allowed.Num())
				SetRefinementChoice(Tag, Binding.Tier, Choice == 0 ? FString() : Allowed[Choice - 1]);
		}
	}
	RefreshControls();
}

void URPGCharacterConfigurationWidget::ClickLoad() { LoadSavedConfiguration(); }
void URPGCharacterConfigurationWidget::ClickEquipUnslotted() { EquipUnslottedSkills(); }

FText URPGCharacterConfigurationWidget::GetSkillPreview(FGameplayTag Skill) const
{
	const auto* Draft = DraftSkills.FindByPredicate([Skill](const auto& Entry) { return Entry.SkillTag == Skill; });
	if (!bLoaded || !Draft) return LOCTEXT("PreviewReadFirst", "저장된 구성을 읽은 뒤 수치를 확인할 수 있습니다.");
	return RPGCharacterConfigurationPreview::Describe(SkillFor(LoadedClass, Skill), *Draft, Saved.RefinementLevels);
}
void URPGCharacterConfigurationWidget::ClickSave() { SaveConfiguration(); }
void URPGCharacterConfigurationWidget::ClickReset() { ResetDraft(); }

#undef LOCTEXT_NAMESPACE
