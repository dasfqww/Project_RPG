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
		const TArray<TSharedPtr<FJsonValue>>* Tripods = nullptr;
		FRPGSavedProgressionSkill Skill;
		const TSharedPtr<FJsonValue>* TagValue = Object->Values.Find(TEXT("skillTag"));
		if (!TagValue || !TagValue->IsValid() || !ReadTag(**TagValue, Skill.SkillTag, false)
			|| Acquired.Contains(Skill.SkillTag) || !ReadInteger(*Object, TEXT("level"), 1, Skill.Level) || Skill.Level > 128
			|| !Object->TryGetArrayField(TEXT("tripods"), Tripods) || !Tripods || Tripods->Num() != 2) return false;
		for (const auto& Choice : *Tripods)
		{
			FString Id;
			if (!Choice || Choice->Type != EJson::String || !Choice->TryGetString(Id) || Id.Len() > 32) return false;
			Skill.TripodIds.Add(MoveTemp(Id));
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
			|| Saved.TripodIds.Num() != 2) return Fail(TEXT("Saved skill acquisition or level requires content migration."));
		Acquired.Add(Saved.SkillTag);
		FRPGReplicatedSkillSaveEntry& Entry = Record.AcquiredSkills.AddDefaulted_GetRef();
		Entry.SkillTag = Saved.SkillTag;
		Entry.SaveData.SkillLevel = Saved.Level;
		Entry.SaveData.SelectedTripodIndices.Init(INDEX_NONE, Skill->TripodTiers.Num());
		for (int32 SavedTier = 0; SavedTier < Saved.TripodIds.Num(); ++SavedTier)
		{
			if (Saved.TripodIds[SavedTier].IsEmpty()) continue;
			int32 TierIndex = INDEX_NONE, OptionIndex = INDEX_NONE;
			if (!Skill->ResolveSavedTripod(SavedTier == 0 ? 4 : 7, FName(*Saved.TripodIds[SavedTier]), TierIndex, OptionIndex)
				|| !Skill->IsTripodSelectionAllowed(Saved.Level, TierIndex, OptionIndex))
				return Fail(TEXT("Saved tripod identity or unlock tier requires content migration."));
			Entry.SaveData.SelectedTripodIndices[TierIndex] = OptionIndex;
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
			|| Skill.TripodIds.Num() != 2) return false;
		Acquired.Add(Skill.SkillTag);
		const auto Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("skillTag"), Skill.SkillTag.ToString());
		Row->SetNumberField(TEXT("level"), Skill.Level);
		TArray<TSharedPtr<FJsonValue>> Tripods;
		for (const FString& Id : Skill.TripodIds)
		{
			if (Id.Len() > 32) return false;
			Tripods.Add(MakeShared<FJsonValueString>(Id));
		}
		Row->SetArrayField(TEXT("tripods"), Tripods);
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
