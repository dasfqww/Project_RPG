#include "Narrative/RPGNarrativeTypes.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace RPGNarrativeJson
{
    bool Id(const FString& Value)
    {
        if (Value.IsEmpty() || Value.Len() > 128) return false;
        for (const TCHAR C : Value)
            if (!((C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9')
                || C == '.' || C == '_' || C == '-')) return false;
        return true;
    }
    bool Int(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, int32& Out)
    {
        double Value = 0;
        if (!Object || !Object->TryGetNumberField(Key, Value) || !FMath::IsFinite(Value)
            || Value < 1 || Value > MAX_int32 || Value != FMath::FloorToDouble(Value)) return false;
        Out = static_cast<int32>(Value); return true;
    }
    bool Strings(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, TArray<FString>& Out)
    {
        const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
        if (!Object || !Object->TryGetArrayField(Key, Values) || Values->Num() > 256) return false;
        TSet<FString> Seen;
        for (const auto& Value : *Values)
        {
            FString Text;
            if (!Value || !Value->TryGetString(Text) || !Id(Text) || Seen.Contains(Text)) return false;
            Seen.Add(Text); Out.Add(MoveTemp(Text));
        }
        return true;
    }
    bool Text(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, int32 Limit, FString& Out)
    {
        return Object && Object->TryGetStringField(Key, Out) && !Out.IsEmpty() && Out.Len() <= Limit;
    }
    bool OptionalText(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, int32 Limit, FText& Out)
    {
        const auto Field = Object->TryGetField(Key);
        if (!Field || Field->IsNull()) return true;
        FString Value;
        if (!Text(Object, Key, Limit, Value)) return false;
        Out = FText::FromString(Value); return true;
    }
}

bool FRPGNarrativeCodec::Decode(const FString& Body, const FGuid ExpectedCharacter, FRPGNarrativeSnapshot& Out)
{
    using namespace RPGNarrativeJson;
    TSharedPtr<FJsonObject> Object;
    FRPGNarrativeSnapshot Parsed;
    FString Character, Revision;
    int32 Schema = 0;
    if (!ExpectedCharacter.IsValid() || Body.Len() > 1024 * 1024
        || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Body), Object) || !Object
        || !Object->TryGetStringField(TEXT("characterId"), Character) || !FGuid::Parse(Character, Parsed.CharacterId)
        || Parsed.CharacterId != ExpectedCharacter || !Int(Object, TEXT("schemaVersion"), Schema) || Schema != 1
        || !Int(Object, TEXT("catalogVersion"), Parsed.CatalogVersion)
        || !Object->TryGetStringField(TEXT("revision"), Revision) || Revision.IsEmpty() || Revision.Len() > 19) return false;
    for (const TCHAR C : Revision)
    {
        if (C < '0' || C > '9' || Parsed.Revision > (MAX_int64 - (C - '0')) / 10) return false;
        Parsed.Revision = Parsed.Revision * 10 + (C - '0');
    }
    if (!Strings(Object, TEXT("completedChapters"), Parsed.CompletedChapters)
        || !Strings(Object, TEXT("storyUnlocks"), Parsed.StoryUnlocks)) return false;
    const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
    if (!Object->TryGetArrayField(TEXT("quests"), Values) || Values->Num() > 256) return false;
    TSet<FString> QuestIds;
    for (const auto& Value : *Values)
    {
        const TSharedPtr<FJsonObject>* Item = nullptr;
        FRPGQuestProgress Quest; FString Status;
        if (!Value || !Value->TryGetObject(Item) || !*Item
            || !(*Item)->TryGetStringField(TEXT("questId"), Quest.QuestId) || !Id(Quest.QuestId) || QuestIds.Contains(Quest.QuestId)
            || !Int(*Item, TEXT("definitionVersion"), Quest.DefinitionVersion)
            || !(*Item)->TryGetStringField(TEXT("status"), Status)
            || !Strings(*Item, TEXT("completedObjectives"), Quest.CompletedObjectives)) return false;
        if (Status == TEXT("Active")) Quest.Status = ERPGQuestStatus::Active;
        else if (Status == TEXT("ReadyToReport")) Quest.Status = ERPGQuestStatus::ReadyToReport;
        else if (Status == TEXT("Completed")) Quest.Status = ERPGQuestStatus::Completed;
        else return false;
        if (!OptionalText(*Item, TEXT("title"), 256, Quest.Title)) return false;
        if ((*Item)->HasField(TEXT("objectives")))
        {
            const TArray<TSharedPtr<FJsonValue>>* Objectives = nullptr;
            if (!(*Item)->TryGetArrayField(TEXT("objectives"), Objectives) || Objectives->IsEmpty() || Objectives->Num() > 256) return false;
            TSet<FString> ObjectiveIds;
            for (const auto& ObjectiveValue : *Objectives)
            {
                const TSharedPtr<FJsonObject>* Entry = nullptr; FRPGQuestObjectiveView Objective;
                if (!ObjectiveValue || !ObjectiveValue->TryGetObject(Entry) || !*Entry
                    || !(*Entry)->TryGetStringField(TEXT("id"), Objective.Id) || !Id(Objective.Id) || ObjectiveIds.Contains(Objective.Id)
                    || !OptionalText(*Entry, TEXT("text"), 1024, Objective.Text)
                    || !(*Entry)->TryGetBoolField(TEXT("completed"), Objective.bCompleted)
                    || Objective.bCompleted != Quest.CompletedObjectives.Contains(Objective.Id)) return false;
                ObjectiveIds.Add(Objective.Id); Quest.Objectives.Add(MoveTemp(Objective));
            }
            for (const auto& Completed : Quest.CompletedObjectives) if (!ObjectiveIds.Contains(Completed)) return false;
            const bool bAllDone = Quest.CompletedObjectives.Num() == Quest.Objectives.Num();
            if ((Quest.Status == ERPGQuestStatus::Active) == bAllDone) return false;
        }
        QuestIds.Add(Quest.QuestId); Parsed.Quests.Add(MoveTemp(Quest));
    }
    if (!Object->TryGetArrayField(TEXT("availableDialogues"), Values) || Values->Num() > 256) return false;
    TSet<FString> DialogueIds;
    for (const auto& Value : *Values)
    {
        const TSharedPtr<FJsonObject>* Item = nullptr;
        FRPGDialogueNode Dialogue; FString Speaker, Line;
        const TArray<TSharedPtr<FJsonValue>>* Choices = nullptr;
        if (!Value || !Value->TryGetObject(Item) || !*Item
            || !(*Item)->TryGetStringField(TEXT("id"), Dialogue.Id) || !Id(Dialogue.Id) || DialogueIds.Contains(Dialogue.Id)
            || !Text(*Item, TEXT("speaker"), 256, Speaker) || !Text(*Item, TEXT("text"), 4096, Line)
            || !(*Item)->TryGetArrayField(TEXT("choices"), Choices) || Choices->IsEmpty() || Choices->Num() > 64) return false;
        Dialogue.Speaker = FText::FromString(Speaker); Dialogue.Text = FText::FromString(Line);
        if (((*Item)->HasField(TEXT("informational")) && !(*Item)->TryGetBoolField(TEXT("informational"), Dialogue.bInformational))
            || ((*Item)->HasField(TEXT("entryPoint")) && !(*Item)->TryGetBoolField(TEXT("entryPoint"), Dialogue.bEntryPoint))
            || (!Dialogue.bInformational && !Dialogue.bEntryPoint)) return false;
        TSet<FString> ChoiceIds;
        for (const auto& ChoiceValue : *Choices)
        {
            const TSharedPtr<FJsonObject>* ChoiceObject = nullptr;
            FRPGDialogueChoice Choice; FString Label;
            if (!ChoiceValue || !ChoiceValue->TryGetObject(ChoiceObject) || !*ChoiceObject
                || !(*ChoiceObject)->TryGetStringField(TEXT("id"), Choice.Id) || !Id(Choice.Id) || ChoiceIds.Contains(Choice.Id)
                || !Text(*ChoiceObject, TEXT("text"), 1024, Label)) return false;
            const auto Next = (*ChoiceObject)->TryGetField(TEXT("nextDialogueId"));
            if (Next && !Next->IsNull()
                && (!Next->TryGetString(Choice.NextDialogueId) || !Id(Choice.NextDialogueId) || !Dialogue.bInformational)) return false;
            ChoiceIds.Add(Choice.Id); Choice.Text = FText::FromString(Label); Dialogue.Choices.Add(MoveTemp(Choice));
        }
        DialogueIds.Add(Dialogue.Id); Parsed.AvailableDialogues.Add(MoveTemp(Dialogue));
    }
    for (const auto& Node : Parsed.AvailableDialogues)
        for (const auto& Choice : Node.Choices)
            if (!Choice.NextDialogueId.IsEmpty() && !Parsed.AvailableDialogues.ContainsByPredicate(
                [&](const auto& Target) { return Target.Id == Choice.NextDialogueId && Target.bInformational; })) return false;
    Out = MoveTemp(Parsed); return true;
}

bool FRPGNarrativeCodec::Encode(const FRPGNarrativeCommand& Command, FString& OutBody)
{
    if (!Command.RequestId.IsValid() || Command.ExpectedRevision < 0 || Command.CatalogVersion < 1
        || !RPGNarrativeJson::Id(Command.DialogueId) || !RPGNarrativeJson::Id(Command.ChoiceId)) return false;
    auto Object = MakeShared<FJsonObject>();
    Object->SetStringField(TEXT("requestId"), Command.RequestId.ToString(EGuidFormats::DigitsWithHyphensLower));
    Object->SetStringField(TEXT("expectedRevision"), LexToString(Command.ExpectedRevision));
    Object->SetNumberField(TEXT("catalogVersion"), Command.CatalogVersion);
    Object->SetStringField(TEXT("dialogueId"), Command.DialogueId); Object->SetStringField(TEXT("choiceId"), Command.ChoiceId);
    FString Body;
    if (!FJsonSerializer::Serialize(Object, TJsonWriterFactory<>::Create(&Body))) return false;
    OutBody = MoveTemp(Body); return true;
}
