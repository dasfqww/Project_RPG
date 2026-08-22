#include "Item/Backend/RPGItemBackendJsonCodec.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/PrimaryAssetId.h"

namespace RPGItemBackendJson
{
void SetError(FString* OutError, const TCHAR* Message)
{
	if (OutError)
	{
		*OutError = Message;
	}
}

TSharedPtr<FJsonObject> ParseObject(
	const FString& Json,
	FString* OutError)
{
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader =
		TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(
			Reader,
			Root,
			FJsonSerializer::EFlags::StoreNumbersAsStrings) ||
		!Root.IsValid())
	{
		SetError(OutError, TEXT("Backend response is not a JSON object."));
		return nullptr;
	}
	return Root;
}

bool SerializeObject(
	const TSharedRef<FJsonObject>& Object,
	FString& OutJson,
	FString* OutError)
{
	OutJson.Reset();
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>>
		Writer = TJsonWriterFactory<
			TCHAR,
			TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutJson);
	if (!FJsonSerializer::Serialize(Object, Writer))
	{
		SetError(OutError, TEXT("Failed to serialize the item request."));
		return false;
	}
	return true;
}

void SetInt64Field(
	const TSharedRef<FJsonObject>& Object,
	const TCHAR* Name,
	const int64 Value)
{
	Object->SetField(
		Name,
		MakeShared<FJsonValueNumberString>(LexToString(Value)));
}

const TCHAR* ToString(const ERPGItemOwnerType Value)
{
	switch (Value)
	{
	case ERPGItemOwnerType::Character: return TEXT("Character");
	case ERPGItemOwnerType::Account: return TEXT("Account");
	case ERPGItemOwnerType::System: return TEXT("System");
	case ERPGItemOwnerType::World: return TEXT("World");
	default: return TEXT("None");
	}
}

const TCHAR* ToString(const ERPGItemContainerType Value)
{
	switch (Value)
	{
	case ERPGItemContainerType::Inventory: return TEXT("Inventory");
	case ERPGItemContainerType::Equipment: return TEXT("Equipment");
	case ERPGItemContainerType::CharacterStorage:
		return TEXT("CharacterStorage");
	case ERPGItemContainerType::AccountStorage:
		return TEXT("AccountStorage");
	case ERPGItemContainerType::Mail: return TEXT("Mail");
	case ERPGItemContainerType::Trade: return TEXT("Trade");
	case ERPGItemContainerType::Auction: return TEXT("Auction");
	case ERPGItemContainerType::World: return TEXT("World");
	case ERPGItemContainerType::Terminal: return TEXT("Terminal");
	default: return TEXT("None");
	}
}

const TCHAR* ToString(const ERPGItemBindState Value)
{
	switch (Value)
	{
	case ERPGItemBindState::BindOnEquip: return TEXT("BindOnEquip");
	case ERPGItemBindState::CharacterBound: return TEXT("CharacterBound");
	case ERPGItemBindState::AccountBound: return TEXT("AccountBound");
	default: return TEXT("Unbound");
	}
}

const TCHAR* ToString(const ERPGItemLifecycleState Value)
{
	switch (Value)
	{
	case ERPGItemLifecycleState::Consumed: return TEXT("Consumed");
	case ERPGItemLifecycleState::Destroyed: return TEXT("Destroyed");
	case ERPGItemLifecycleState::Expired: return TEXT("Expired");
	default: return TEXT("Active");
	}
}

bool TryParseOwnerType(const FString& Value, ERPGItemOwnerType& OutValue)
{
	if (Value.Equals(TEXT("Character"), ESearchCase::IgnoreCase))
	{
		OutValue = ERPGItemOwnerType::Character;
	}
	else if (Value.Equals(TEXT("Account"), ESearchCase::IgnoreCase))
	{
		OutValue = ERPGItemOwnerType::Account;
	}
	else if (Value.Equals(TEXT("System"), ESearchCase::IgnoreCase))
	{
		OutValue = ERPGItemOwnerType::System;
	}
	else if (Value.Equals(TEXT("World"), ESearchCase::IgnoreCase))
	{
		OutValue = ERPGItemOwnerType::World;
	}
	else
	{
		return false;
	}
	return true;
}

bool TryParseContainerType(
	const FString& Value,
	ERPGItemContainerType& OutValue)
{
	static const TPair<const TCHAR*, ERPGItemContainerType> Values[] =
	{
		{TEXT("Inventory"), ERPGItemContainerType::Inventory},
		{TEXT("Equipment"), ERPGItemContainerType::Equipment},
		{TEXT("CharacterStorage"), ERPGItemContainerType::CharacterStorage},
		{TEXT("AccountStorage"), ERPGItemContainerType::AccountStorage},
		{TEXT("Mail"), ERPGItemContainerType::Mail},
		{TEXT("Trade"), ERPGItemContainerType::Trade},
		{TEXT("Auction"), ERPGItemContainerType::Auction},
		{TEXT("World"), ERPGItemContainerType::World},
		{TEXT("Terminal"), ERPGItemContainerType::Terminal}
	};
	for (const TPair<const TCHAR*, ERPGItemContainerType>& Pair : Values)
	{
		if (Value.Equals(Pair.Key, ESearchCase::IgnoreCase))
		{
			OutValue = Pair.Value;
			return true;
		}
	}
	return false;
}

bool TryParseBindState(const FString& Value, ERPGItemBindState& OutValue)
{
	if (Value.Equals(TEXT("Unbound"), ESearchCase::IgnoreCase))
	{
		OutValue = ERPGItemBindState::Unbound;
	}
	else if (Value.Equals(TEXT("BindOnEquip"), ESearchCase::IgnoreCase))
	{
		OutValue = ERPGItemBindState::BindOnEquip;
	}
	else if (Value.Equals(TEXT("CharacterBound"), ESearchCase::IgnoreCase))
	{
		OutValue = ERPGItemBindState::CharacterBound;
	}
	else if (Value.Equals(TEXT("AccountBound"), ESearchCase::IgnoreCase))
	{
		OutValue = ERPGItemBindState::AccountBound;
	}
	else
	{
		return false;
	}
	return true;
}

bool TryParseLifecycleState(
	const FString& Value,
	ERPGItemLifecycleState& OutValue)
{
	if (Value.Equals(TEXT("Active"), ESearchCase::IgnoreCase))
	{
		OutValue = ERPGItemLifecycleState::Active;
	}
	else if (Value.Equals(TEXT("Consumed"), ESearchCase::IgnoreCase))
	{
		OutValue = ERPGItemLifecycleState::Consumed;
	}
	else if (Value.Equals(TEXT("Destroyed"), ESearchCase::IgnoreCase))
	{
		OutValue = ERPGItemLifecycleState::Destroyed;
	}
	else if (Value.Equals(TEXT("Expired"), ESearchCase::IgnoreCase))
	{
		OutValue = ERPGItemLifecycleState::Expired;
	}
	else
	{
		return false;
	}
	return true;
}

bool TryParseBackendStatus(
	const FString& Value,
	ERPGItemBackendStatus& OutValue)
{
	if (Value.Equals(TEXT("Committed"), ESearchCase::IgnoreCase))
	{
		OutValue = ERPGItemBackendStatus::Succeeded;
	}
	else if (Value.Equals(TEXT("AlreadyCommitted"), ESearchCase::IgnoreCase))
	{
		OutValue = ERPGItemBackendStatus::AlreadyApplied;
	}
	else if (Value.Equals(TEXT("InvalidRequest"), ESearchCase::IgnoreCase))
	{
		OutValue = ERPGItemBackendStatus::InvalidRequest;
	}
	else if (Value.Equals(TEXT("IdempotencyConflict"), ESearchCase::IgnoreCase))
	{
		OutValue = ERPGItemBackendStatus::IdempotencyConflict;
	}
	else if (Value.Equals(TEXT("NotFound"), ESearchCase::IgnoreCase))
	{
		OutValue = ERPGItemBackendStatus::NotFound;
	}
	else if (Value.Equals(TEXT("RevisionConflict"), ESearchCase::IgnoreCase))
	{
		OutValue = ERPGItemBackendStatus::RevisionConflict;
	}
	else if (Value.Equals(TEXT("LocationConflict"), ESearchCase::IgnoreCase))
	{
		OutValue = ERPGItemBackendStatus::LocationConflict;
	}
	else if (Value.Equals(TEXT("ValidationFailed"), ESearchCase::IgnoreCase))
	{
		OutValue = ERPGItemBackendStatus::ValidationFailed;
	}
	else if (Value.Equals(TEXT("InternalError"), ESearchCase::IgnoreCase))
	{
		OutValue = ERPGItemBackendStatus::ServerError;
	}
	else
	{
		return false;
	}
	return true;
}

TSharedRef<FJsonObject> SerializeOwner(const FRPGItemOwnerRef& Owner)
{
	TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField(TEXT("type"), ToString(Owner.Type));
	Object->SetStringField(TEXT("ownerId"), Owner.OwnerId);
	return Object;
}

TSharedRef<FJsonObject> SerializeRecord(const FRPGItemRecord& Record)
{
	TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	const FPrimaryAssetId& DefinitionId = Record.GetDefinitionId();
	Object->SetStringField(
		TEXT("definitionType"),
		DefinitionId.PrimaryAssetType.ToString());
	Object->SetStringField(
		TEXT("definitionName"),
		DefinitionId.PrimaryAssetName.ToString());
	Object->SetNumberField(
		TEXT("definitionVersion"),
		Record.GetDefinitionVersion());
	Object->SetObjectField(TEXT("owner"), SerializeOwner(Record.GetOwner()));

	const FRPGItemLocation& Location = Record.GetLocation();
	TSharedRef<FJsonObject> LocationObject = MakeShared<FJsonObject>();
	LocationObject->SetStringField(
		TEXT("containerType"),
		ToString(Location.ContainerType));
	LocationObject->SetStringField(TEXT("containerId"), Location.ContainerId);
	LocationObject->SetNumberField(TEXT("slotIndex"), Location.SlotIndex);
	Object->SetObjectField(TEXT("location"), LocationObject);

	const FRPGItemInstanceState& State = Record.GetState();
	TSharedRef<FJsonObject> StateObject = MakeShared<FJsonObject>();
	StateObject->SetStringField(
		TEXT("instanceId"),
		State.GetInstanceId().ToString(EGuidFormats::DigitsWithHyphensLower));
	StateObject->SetNumberField(
		TEXT("generationSeed"),
		State.GetGenerationSeed());
	StateObject->SetNumberField(TEXT("quantity"), State.GetQuantity());

	TArray<TSharedPtr<FJsonValue>> InstanceTags;
	for (const FGameplayTag& Tag : State.GetInstanceTags().GetGameplayTagArray())
	{
		InstanceTags.Add(MakeShared<FJsonValueString>(Tag.ToString()));
	}
	StateObject->SetArrayField(TEXT("instanceTags"), MoveTemp(InstanceTags));

	TArray<TSharedPtr<FJsonValue>> StatValues;
	for (const FRPGItemStatValue& StatValue : State.GetStatValues())
	{
		TSharedRef<FJsonObject> StatObject = MakeShared<FJsonObject>();
		StatObject->SetStringField(TEXT("statTag"), StatValue.StatTag.ToString());
		StatObject->SetNumberField(TEXT("value"), StatValue.Value);
		StatValues.Add(MakeShared<FJsonValueObject>(StatObject));
	}
	StateObject->SetArrayField(TEXT("statValues"), MoveTemp(StatValues));
	Object->SetObjectField(TEXT("state"), StateObject);
	SetInt64Field(Object, TEXT("revision"), Record.GetRevision());
	Object->SetStringField(
		TEXT("lifecycleState"),
		ToString(Record.GetLifecycleState()));

	const FRPGItemRecordMetadata& Metadata = Record.GetMetadata();
	TSharedRef<FJsonObject> MetadataObject = MakeShared<FJsonObject>();
	MetadataObject->SetStringField(
		TEXT("bindState"),
		ToString(Metadata.BindState));
	TSharedRef<FJsonObject> DurabilityObject = MakeShared<FJsonObject>();
	DurabilityObject->SetNumberField(
		TEXT("current"),
		Metadata.Durability.Current);
	DurabilityObject->SetNumberField(
		TEXT("maximum"),
		Metadata.Durability.Maximum);
	MetadataObject->SetObjectField(TEXT("durability"), DurabilityObject);
	if (Record.HasExpiration())
	{
		MetadataObject->SetStringField(
			TEXT("expiresAtUtc"),
			Metadata.ExpiresAtUtc.ToIso8601());
	}
	else
	{
		MetadataObject->SetField(
			TEXT("expiresAtUtc"),
			MakeShared<FJsonValueNull>());
	}
	MetadataObject->SetStringField(
		TEXT("creationSource"),
		Metadata.CreationSource.IsValid()
			? Metadata.CreationSource.ToString()
			: FString());
	MetadataObject->SetBoolField(TEXT("isLocked"), Metadata.bLocked);
	Object->SetObjectField(TEXT("metadata"), MetadataObject);
	return Object;
}

bool TryParseOwner(
	const TSharedPtr<FJsonObject>& Object,
	FRPGItemOwnerRef& OutOwner)
{
	FString Type;
	return Object.IsValid() &&
		Object->TryGetStringField(TEXT("type"), Type) &&
		Object->TryGetStringField(TEXT("ownerId"), OutOwner.OwnerId) &&
		TryParseOwnerType(Type, OutOwner.Type) &&
		OutOwner.IsValid();
}

bool TryParseRecord(
	const TSharedPtr<FJsonObject>& Object,
	FRPGItemRecord& OutRecord)
{
	if (!Object.IsValid())
	{
		return false;
	}

	FString DefinitionType;
	FString DefinitionName;
	int32 DefinitionVersion = 0;
	const TSharedPtr<FJsonObject>* OwnerObject = nullptr;
	const TSharedPtr<FJsonObject>* LocationObject = nullptr;
	const TSharedPtr<FJsonObject>* StateObject = nullptr;
	const TSharedPtr<FJsonObject>* MetadataObject = nullptr;
	FString LifecycleString;
	int64 Revision = 0;
	if (!Object->TryGetStringField(TEXT("definitionType"), DefinitionType) ||
		!Object->TryGetStringField(TEXT("definitionName"), DefinitionName) ||
		!Object->TryGetNumberField(
			TEXT("definitionVersion"), DefinitionVersion) ||
		!Object->TryGetObjectField(TEXT("owner"), OwnerObject) ||
		!OwnerObject || !OwnerObject->IsValid() ||
		!Object->TryGetObjectField(TEXT("location"), LocationObject) ||
		!LocationObject || !LocationObject->IsValid() ||
		!Object->TryGetObjectField(TEXT("state"), StateObject) ||
		!StateObject || !StateObject->IsValid() ||
		!Object->TryGetNumberField(TEXT("revision"), Revision) ||
		!Object->TryGetStringField(TEXT("lifecycleState"), LifecycleString) ||
		!Object->TryGetObjectField(TEXT("metadata"), MetadataObject) ||
		!MetadataObject || !MetadataObject->IsValid())
	{
		return false;
	}

	FRPGItemOwnerRef Owner;
	if (!TryParseOwner(*OwnerObject, Owner))
	{
		return false;
	}

	FString ContainerTypeString;
	FRPGItemLocation Location;
	if (!(*LocationObject)->TryGetStringField(
			TEXT("containerType"), ContainerTypeString) ||
		!TryParseContainerType(ContainerTypeString, Location.ContainerType) ||
		!(*LocationObject)->TryGetStringField(
			TEXT("containerId"), Location.ContainerId) ||
		!(*LocationObject)->TryGetNumberField(
			TEXT("slotIndex"), Location.SlotIndex))
	{
		return false;
	}

	FString InstanceIdString;
	FGuid InstanceId;
	int32 GenerationSeed = 0;
	int32 Quantity = 0;
	const TArray<TSharedPtr<FJsonValue>>* InstanceTagValues = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* StatValueObjects = nullptr;
	if (!(*StateObject)->TryGetStringField(
			TEXT("instanceId"), InstanceIdString) ||
		!FGuid::Parse(InstanceIdString, InstanceId) ||
		!(*StateObject)->TryGetNumberField(
			TEXT("generationSeed"), GenerationSeed) ||
		!(*StateObject)->TryGetNumberField(TEXT("quantity"), Quantity) ||
		!(*StateObject)->TryGetArrayField(
			TEXT("instanceTags"), InstanceTagValues) ||
		!InstanceTagValues ||
		!(*StateObject)->TryGetArrayField(
			TEXT("statValues"), StatValueObjects) ||
		!StatValueObjects)
	{
		return false;
	}

	FGameplayTagContainer InstanceTags;
	for (const TSharedPtr<FJsonValue>& TagValue : *InstanceTagValues)
	{
		FString TagString;
		if (!TagValue.IsValid() || !TagValue->TryGetString(TagString))
		{
			return false;
		}
		const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
			FName(*TagString), false);
		if (!Tag.IsValid() || InstanceTags.HasTagExact(Tag))
		{
			return false;
		}
		InstanceTags.AddTag(Tag);
	}

	TArray<FRPGItemStatValue> StatValues;
	for (const TSharedPtr<FJsonValue>& StatValue : *StatValueObjects)
	{
		const TSharedPtr<FJsonObject> StatObject =
			StatValue.IsValid() ? StatValue->AsObject() : nullptr;
		FString StatTagString;
		double Value = 0.0;
		if (!StatObject.IsValid() ||
			!StatObject->TryGetStringField(TEXT("statTag"), StatTagString) ||
			!StatObject->TryGetNumberField(TEXT("value"), Value))
		{
			return false;
		}
		FRPGItemStatValue& ParsedValue = StatValues.AddDefaulted_GetRef();
		ParsedValue.StatTag = FGameplayTag::RequestGameplayTag(
			FName(*StatTagString), false);
		ParsedValue.Value = static_cast<float>(Value);
		if (!ParsedValue.StatTag.IsValid() ||
			!FMath::IsFinite(ParsedValue.Value))
		{
			return false;
		}
	}

	FRPGItemInstanceState State;
	if (!FRPGItemInstanceState::TryRestore(
		InstanceId,
		GenerationSeed,
		Quantity,
		InstanceTags,
		StatValues,
		State))
	{
		return false;
	}

	FString BindStateString;
	FRPGItemRecordMetadata Metadata;
	const TSharedPtr<FJsonObject>* DurabilityObject = nullptr;
	if (!(*MetadataObject)->TryGetStringField(
			TEXT("bindState"), BindStateString) ||
		!TryParseBindState(BindStateString, Metadata.BindState) ||
		!(*MetadataObject)->TryGetObjectField(
			TEXT("durability"), DurabilityObject) ||
		!DurabilityObject || !DurabilityObject->IsValid() ||
		!(*DurabilityObject)->TryGetNumberField(
			TEXT("current"), Metadata.Durability.Current) ||
		!(*DurabilityObject)->TryGetNumberField(
			TEXT("maximum"), Metadata.Durability.Maximum) ||
		!(*MetadataObject)->TryGetBoolField(
			TEXT("isLocked"), Metadata.bLocked))
	{
		return false;
	}

	FString CreationSourceString;
	if (!(*MetadataObject)->TryGetStringField(
		TEXT("creationSource"), CreationSourceString))
	{
		return false;
	}
	if (!CreationSourceString.IsEmpty())
	{
		Metadata.CreationSource = FGameplayTag::RequestGameplayTag(
			FName(*CreationSourceString), false);
		if (!Metadata.CreationSource.IsValid())
		{
			return false;
		}
	}

	const TSharedPtr<FJsonValue> ExpiresValue =
		(*MetadataObject)->TryGetField(TEXT("expiresAtUtc"));
	if (!ExpiresValue.IsValid())
	{
		return false;
	}
	if (!ExpiresValue->IsNull())
	{
		FString ExpiresString;
		if (!ExpiresValue->TryGetString(ExpiresString) ||
			!FDateTime::ParseIso8601(*ExpiresString, Metadata.ExpiresAtUtc))
		{
			return false;
		}
	}

	ERPGItemLifecycleState LifecycleState;
	if (!TryParseLifecycleState(LifecycleString, LifecycleState))
	{
		return false;
	}
	const FPrimaryAssetId DefinitionId{
		FPrimaryAssetType(FName(*DefinitionType)),
		FName(*DefinitionName)};
	return FRPGItemRecord::TryRestore(
		DefinitionId,
		DefinitionVersion,
		Owner,
		Location,
		State,
		Revision,
		LifecycleState,
		Metadata,
		OutRecord);
}

bool TryParseRecordArray(
	const TArray<TSharedPtr<FJsonValue>>& Values,
	TArray<FRPGItemRecord>& OutRecords)
{
	OutRecords.Reset(Values.Num());
	TSet<FGuid> ItemIds;
	for (const TSharedPtr<FJsonValue>& Value : Values)
	{
		FRPGItemRecord Record;
		if (!Value.IsValid() ||
			!TryParseRecord(Value->AsObject(), Record) ||
			ItemIds.Contains(Record.GetItemId()))
		{
			OutRecords.Reset();
			return false;
		}
		ItemIds.Add(Record.GetItemId());
		OutRecords.Add(MoveTemp(Record));
	}
	return true;
}
}

bool FRPGItemBackendJsonCodec::SerializeCommitRequest(
	const FRPGItemRepositoryCommitRequest& Request,
	FString& OutJson,
	FString* OutError)
{
	using namespace RPGItemBackendJson;
	if (!Request.RequestId.IsValid() ||
		Request.Operation.IsNone() ||
		Request.CommandFingerprint.IsEmpty() ||
		!Request.Actor.IsValid() ||
		Request.AffectedQuantity < 0 ||
		Request.Mutations.IsEmpty() ||
		Request.Mutations.Num() > 16)
	{
		SetError(OutError, TEXT("Item commit request is invalid."));
		return false;
	}

	TSet<FGuid> ItemIds;
	TArray<TSharedPtr<FJsonValue>> MutationValues;
	MutationValues.Reserve(Request.Mutations.Num());
	for (const FRPGItemRecordMutation& Mutation : Request.Mutations)
	{
		if (Mutation.ExpectedRevision < 0 ||
			Mutation.NewRecord.GetRevision() != Mutation.ExpectedRevision ||
			!Mutation.NewRecord.IsStructurallyValid() ||
			ItemIds.Contains(Mutation.NewRecord.GetItemId()))
		{
			SetError(OutError, TEXT("Item commit mutation is invalid."));
			return false;
		}
		ItemIds.Add(Mutation.NewRecord.GetItemId());

		TSharedRef<FJsonObject> MutationObject = MakeShared<FJsonObject>();
		SetInt64Field(
			MutationObject,
			TEXT("expectedRevision"),
			Mutation.ExpectedRevision);
		MutationObject->SetObjectField(
			TEXT("newRecord"),
			SerializeRecord(Mutation.NewRecord));
		MutationValues.Add(MakeShared<FJsonValueObject>(MutationObject));
	}

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(
		TEXT("requestId"),
		Request.RequestId.ToString(EGuidFormats::DigitsWithHyphensLower));
	Root->SetStringField(TEXT("operation"), Request.Operation.ToString());
	Root->SetStringField(
		TEXT("commandFingerprint"),
		Request.CommandFingerprint);
	Root->SetObjectField(TEXT("actor"), SerializeOwner(Request.Actor));
	Root->SetNumberField(
		TEXT("affectedQuantity"),
		Request.AffectedQuantity);
	Root->SetArrayField(TEXT("mutations"), MoveTemp(MutationValues));
	return SerializeObject(Root, OutJson, OutError);
}

bool FRPGItemBackendJsonCodec::DeserializeCommitResponse(
	const FString& Json,
	FRPGItemBackendCommitResult& OutResult,
	FString* OutError)
{
	using namespace RPGItemBackendJson;
	const TSharedPtr<FJsonObject> Root = ParseObject(Json, OutError);
	if (!Root.IsValid())
	{
		return false;
	}

	FString StatusString;
	FString RequestIdString;
	FString OperationString;
	const TSharedPtr<FJsonObject>* ActorObject = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* RecordValues = nullptr;
	FRPGItemBackendCommitResult Result;
	if (!Root->TryGetStringField(TEXT("status"), StatusString) ||
		!TryParseBackendStatus(StatusString, Result.Status) ||
		!Root->TryGetStringField(TEXT("requestId"), RequestIdString) ||
		!FGuid::Parse(RequestIdString, Result.RequestId) ||
		!Root->TryGetStringField(TEXT("operation"), OperationString) ||
		OperationString.IsEmpty() ||
		!Root->TryGetStringField(
			TEXT("commandFingerprint"), Result.CommandFingerprint) ||
		!Root->TryGetObjectField(TEXT("actor"), ActorObject) ||
		!ActorObject || !ActorObject->IsValid() ||
		!TryParseOwner(*ActorObject, Result.Actor) ||
		!Root->TryGetNumberField(
			TEXT("affectedQuantity"), Result.AffectedQuantity) ||
		!Root->TryGetArrayField(TEXT("records"), RecordValues) ||
		!RecordValues ||
		!TryParseRecordArray(*RecordValues, Result.Records))
	{
		SetError(OutError, TEXT("Backend commit response is invalid."));
		return false;
	}

	Result.Operation = FName(*OperationString);
	OutResult = MoveTemp(Result);
	return true;
}

bool FRPGItemBackendJsonCodec::DeserializeLoadResponse(
	const FString& Json,
	TArray<FRPGItemRecord>& OutRecords,
	FString* OutError)
{
	using namespace RPGItemBackendJson;
	const TSharedPtr<FJsonObject> Root = ParseObject(Json, OutError);
	const TArray<TSharedPtr<FJsonValue>>* ItemValues = nullptr;
	if (!Root.IsValid() ||
		!Root->TryGetArrayField(TEXT("items"), ItemValues) ||
		!ItemValues || !TryParseRecordArray(*ItemValues, OutRecords))
	{
		OutRecords.Reset();
		SetError(OutError, TEXT("Backend item list response is invalid."));
		return false;
	}
	return true;
}
