#include "Progression/RPGCharacterProgressionSnapshot.h"

#include "Ability/RPGGameplayAbility.h"
#include "Component/Skill/RPGPlayerSkillComponent.h"
#include "Dom/JsonObject.h"
#include "PlayerClass/RPGPlayerClassDefinition.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Skill/RPGSkillDefinition.h"

namespace
{
	bool ReadInteger(const FJsonObject& Object, const TCHAR* Key, const int32 Minimum, int32& Out)
	{
		double Value = 0;
		const auto* Field = Object.Values.Find(Key);
		if (!Field || !Field->IsValid() || (*Field)->Type != EJson::Number
			|| !Object.TryGetNumberField(Key, Value) || !FMath::IsFinite(Value)
			|| Value < Minimum || Value > MAX_int32 || FMath::FloorToDouble(Value) != Value) return false;
		Out = static_cast<int32>(Value);
		return true;
	}

	bool ReadLevels(const FJsonObject& Rules, const TCHAR* Key, int32 MaxCount, TArray<int32>& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values;
		if (!Rules.TryGetArrayField(Key, Values) || Values->IsEmpty() || Values->Num() > MaxCount) return false;
		for (const auto& Item : *Values)
		{
			if (!Item || Item->Type != EJson::Number) return false;
			const double N = Item->AsNumber();
			if (!FMath::IsFinite(N) || N < 0 || N > MAX_int32 || FMath::FloorToDouble(N) != N
				|| (!Out.IsEmpty() && N < Out.Last())) return false;
			Out.Add(static_cast<int32>(N));
		}
		return true;
	}

	bool ReadConfigurationRules(const FJsonObject& Rules, FRPGCharacterProgressionSnapshot& Record)
	{
		const TSharedPtr<FJsonObject>* Choices;
		if (!ReadLevels(Rules, TEXT("levelCosts"), 128, Record.LevelCosts)
			|| !ReadLevels(Rules, TEXT("refinementLevels"), 2, Record.RefinementLevels)
			|| Record.RefinementLevels.Num() != 2 || Record.RefinementLevels[0] < 1
			|| !Rules.TryGetObjectField(TEXT("choicesBySkill"), Choices)
			|| (*Choices)->Values.Num() != Record.Skills.Num()) return false;
		for (const auto& Skill : Record.Skills)
		{
			const TArray<TSharedPtr<FJsonValue>>* Tiers;
			if (!(*Choices)->TryGetArrayField(Skill.SkillTag.ToString(), Tiers) || Tiers->Num() != 2) return false;
			auto& Row = Record.ChoicesBySkill.Add(Skill.SkillTag);
			for (int32 Tier = 0; Tier < 2; ++Tier)
			{
				const auto& Value = (*Tiers)[Tier];
				if (!Value || Value->Type != EJson::Array) return false;
				for (const auto& Choice : Value->AsArray())
				{
					if (!Choice || Choice->Type != EJson::String) return false;
					const FString Id = Choice->AsString();
					if (Id.IsEmpty() || Id.Len() > 32 || Row.Tiers[Tier].Contains(Id)) return false;
					Row.Tiers[Tier].Add(Id);
				}
			}
		}
		return true;
	}

	bool ReadTag(const FJsonValue& Value, FGameplayTag& Out, const bool bAllowEmpty)
	{
		FString Text;
		if (Value.Type != EJson::String || !Value.TryGetString(Text) || Text.Len() > 96) return false;
		if (Text.IsEmpty()) { Out = FGameplayTag(); return bAllowEmpty; }
		Out = FGameplayTag::RequestGameplayTag(FName(*Text), false);
		return Out.IsValid();
	}
}

bool FRPGCharacterProgressionSnapshotCodec::Decode(const FString& Body, const FString& ExpectedCharacterId,
	FRPGCharacterProgressionSnapshot& OutSnapshot, FString& OutError)
{
	OutSnapshot = {};
	OutError = TEXT("Invalid character progression record.");
	TSharedPtr<FJsonObject> Root;
	FGuid ExpectedGuid, RecordGuid;
	if (Body.Len() > 256 * 1024 || !FGuid::Parse(ExpectedCharacterId, ExpectedGuid) || !ExpectedGuid.IsValid()
		|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Body), Root) || !Root) return false;
	FRPGCharacterProgressionSnapshot Record;
	FString ClassText, RevisionText;
	if (!Root->TryGetStringField(TEXT("characterId"), Record.CharacterId)
		|| !FGuid::Parse(Record.CharacterId, RecordGuid) || RecordGuid != ExpectedGuid
		|| !Root->TryGetStringField(TEXT("profileId"), Record.ProfileId) || Record.ProfileId.IsEmpty() || Record.ProfileId.Len() > 96
		|| !Root->TryGetStringField(TEXT("classId"), ClassText) || ClassText.Len() > 160
		|| !Root->TryGetStringField(TEXT("revision"), RevisionText) || RevisionText.IsEmpty() || RevisionText.Len() > 16
		|| !ReadInteger(*Root, TEXT("experience"), 0, Record.Experience)
		|| !ReadInteger(*Root, TEXT("level"), 1, Record.Level)
		|| !ReadInteger(*Root, TEXT("totalSP"), 0, Record.TotalSP)
		|| !ReadInteger(*Root, TEXT("usedSP"), 0, Record.UsedSP) || Record.UsedSP > Record.TotalSP
		|| !ReadInteger(*Root, TEXT("milestones"), 0, Record.Milestones)
		|| !Root->TryGetBoolField(TEXT("identityUnlocked"), Record.bIdentityUnlocked)) return false;
	Record.ClassId = FPrimaryAssetId(ClassText);
	if (!Record.ClassId.IsValid() || Record.ClassId.PrimaryAssetType != URPGPlayerClassDefinition::PrimaryAssetType) return false;
	for (const TCHAR Character : RevisionText)
	{
		if (Character < TEXT('0') || Character > TEXT('9')) return false;
		Record.Revision = Record.Revision * 10 + Character - TEXT('0');
		if (Record.Revision > 9007199254740991LL) return false;
	}
	if (Record.Revision == 0) return false;
	Record.CharacterId = RecordGuid.ToString(EGuidFormats::DigitsWithHyphensLower);
	const TArray<TSharedPtr<FJsonValue>>* Skills = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* Slots = nullptr;
	if (!Root->TryGetArrayField(TEXT("skills"), Skills) || !Skills || Skills->Num() > 64
		|| !Root->TryGetArrayField(TEXT("skillSlots"), Slots) || !Slots || Slots->Num() != 8) return false;
	TSet<FGameplayTag> Acquired;
	for (const auto& Value : *Skills)
	{
		if (!Value || Value->Type != EJson::Object) return false;
		const auto& Object = Value->AsObject();
		const TArray<TSharedPtr<FJsonValue>>* Refinements = nullptr;
		FRPGSavedProgressionSkill Skill;
		const TSharedPtr<FJsonValue>* TagValue = Object->Values.Find(TEXT("skillTag"));
		if (!TagValue || !TagValue->IsValid() || !ReadTag(**TagValue, Skill.SkillTag, false)
			|| Acquired.Contains(Skill.SkillTag) || !ReadInteger(*Object, TEXT("level"), 1, Skill.Level) || Skill.Level > 128
			|| !Object->TryGetArrayField(TEXT("tripods"), Refinements) || !Refinements || Refinements->Num() != 2) return false;
		for (const auto& Choice : *Refinements)
		{
			FString Id;
			if (!Choice || Choice->Type != EJson::String || !Choice->TryGetString(Id) || Id.Len() > 32) return false;
			Skill.RefinementIds.Add(MoveTemp(Id));
		}
		Acquired.Add(Skill.SkillTag);
		Record.Skills.Add(MoveTemp(Skill));
	}
	TSet<FGameplayTag> Slotted;
	for (const auto& Value : *Slots)
	{
		FGameplayTag Tag;
		if (!Value || !ReadTag(*Value, Tag, true)) return false;
		if (Tag.IsValid() && (!Acquired.Contains(Tag) || Slotted.Contains(Tag))) return false;
		if (Tag.IsValid()) Slotted.Add(Tag);
		Record.SkillSlots.Add(Tag);
	}
	if (Root->HasField(TEXT("configurationRules")))
	{
		const TSharedPtr<FJsonObject>* Rules;
		if (!Root->TryGetObjectField(TEXT("configurationRules"), Rules) || !ReadConfigurationRules(**Rules, Record)) return false;
	}
	OutSnapshot = MoveTemp(Record);
	OutError.Reset();
	return true;
}

bool FRPGCharacterProgressionSnapshot::BuildSkillRestoreData(const URPGPlayerClassDefinition& Definition,
	const URPGPlayerSkillComponent& Growth, FRPGPlayerSkillRestoreData& OutData, FText& OutError) const
{
	OutData = {};
	OutError = FText::GetEmpty();
	const auto Fail = [&OutError](const TCHAR* Text) { OutError = FText::FromString(Text); return false; };
	if (ClassId != Definition.GetPrimaryAssetId() || !Definition.ValidateConfiguration(OutError))
		return Fail(TEXT("Saved class identity or authored class configuration is invalid."));
	if (bIdentityUnlocked && !Definition.IdentityAbility)
		return Fail(TEXT("The unlocked class identity has no authored ability."));
	FRPGPlayerSkillRestoreData Record;
	Record.ClassId = ClassId;
	Record.TotalSP = TotalSP;
	Record.bIdentityUnlocked = bIdentityUnlocked;
	Record.bConfigurationMutable = false; // This live Pawn must not diverge from the persistent configuration.
	int64 CalculatedUsedSP = 0;
	TSet<FGameplayTag> Acquired;
	for (const FRPGSavedProgressionSkill& Saved : Skills)
	{
		const auto* Binding = Definition.FindSkill(Saved.SkillTag);
		const auto* Skill = Binding ? Binding->Definition.Get() : nullptr;
		if (!Skill || Acquired.Contains(Saved.SkillTag) || Saved.Level < 1 || Saved.Level > Skill->MaxSkillLevel
			|| Saved.RefinementIds.Num() != 2) return Fail(TEXT("Saved skill acquisition or level requires content migration."));
		Acquired.Add(Saved.SkillTag);
		FRPGReplicatedSkillSaveEntry& Entry = Record.AcquiredSkills.AddDefaulted_GetRef();
		Entry.SkillTag = Saved.SkillTag;
		Entry.SaveData.SkillLevel = Saved.Level;
		Entry.SaveData.SelectedRefinementIndices.Init(INDEX_NONE, Skill->RefinementTiers.Num());
		for (int32 SavedTier = 0; SavedTier < Saved.RefinementIds.Num(); ++SavedTier)
		{
			if (Saved.RefinementIds[SavedTier].IsEmpty()) continue;
			int32 TierIndex = INDEX_NONE, OptionIndex = INDEX_NONE;
			if (!Skill->ResolveSavedRefinement(SavedTier == 0 ? 4 : 7, FName(*Saved.RefinementIds[SavedTier]), TierIndex, OptionIndex)
				|| !Skill->IsRefinementSelectionAllowed(Saved.Level, TierIndex, OptionIndex))
				return Fail(TEXT("Saved tripod identity or unlock tier requires content migration."));
			Entry.SaveData.SelectedRefinementIndices[TierIndex] = OptionIndex;
		}
		for (int32 SkillLevel = 2; SkillLevel <= Saved.Level; ++SkillLevel)
			CalculatedUsedSP += Growth.GetRequiredSPForLevel(SkillLevel);
	}
	if (CalculatedUsedSP != UsedSP || CalculatedUsedSP > TotalSP)
		return Fail(TEXT("Saved SP totals disagree with the authored skill costs."));
	TSet<FGameplayTag> Slotted;
	if (SkillSlots.Num() != URPGPlayerClassDefinition::MaxCombatSkills) return Fail(TEXT("Invalid saved slot count."));
	for (const FGameplayTag Tag : SkillSlots)
	{
		if (!Tag.IsValid()) continue;
		if (!Acquired.Contains(Tag) || Slotted.Contains(Tag)) return Fail(TEXT("Saved slots contain unknown or duplicate skills."));
		Slotted.Add(Tag);
	}
	OutData = MoveTemp(Record);
	return true;
}

bool FRPGCharacterProgressionSnapshotCodec::EncodeConfiguration(const FRPGCharacterSkillConfiguration& Command, FString& OutBody)
{
	OutBody.Reset();
	if (!Command.RequestId.IsValid() || Command.ExpectedRevision < 1 || Command.ExpectedRevision > 9007199254740991LL
		|| Command.Skills.IsEmpty() || Command.Skills.Num() > 64 || Command.SkillSlots.Num() != 8) return false;
	const auto Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("requestId"), Command.RequestId.ToString(EGuidFormats::DigitsWithHyphensLower));
	Root->SetStringField(TEXT("expectedRevision"), FString::Printf(TEXT("%lld"), Command.ExpectedRevision));
	TArray<TSharedPtr<FJsonValue>> Skills, Slots;
	TSet<FGameplayTag> Acquired, Slotted;
	for (const auto& Skill : Command.Skills)
	{
		if (!Skill.SkillTag.IsValid() || Acquired.Contains(Skill.SkillTag) || Skill.Level < 1 || Skill.Level > 128
			|| Skill.RefinementIds.Num() != 2) return false;
		Acquired.Add(Skill.SkillTag);
		const auto Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("skillTag"), Skill.SkillTag.ToString());
		Row->SetNumberField(TEXT("level"), Skill.Level);
		TArray<TSharedPtr<FJsonValue>> Refinements;
		for (const FString& Id : Skill.RefinementIds)
		{
			if (Id.Len() > 32) return false;
			Refinements.Add(MakeShared<FJsonValueString>(Id));
		}
		Row->SetArrayField(TEXT("tripods"), Refinements);
		Skills.Add(MakeShared<FJsonValueObject>(Row));
	}
	for (const FGameplayTag Tag : Command.SkillSlots)
	{
		if (Tag.IsValid() && (!Acquired.Contains(Tag) || Slotted.Contains(Tag))) return false;
		if (Tag.IsValid()) Slotted.Add(Tag);
		Slots.Add(MakeShared<FJsonValueString>(Tag.IsValid() ? Tag.ToString() : FString()));
	}
	Root->SetArrayField(TEXT("skills"), Skills);
	Root->SetArrayField(TEXT("skillSlots"), Slots);
	return FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&OutBody));
}
