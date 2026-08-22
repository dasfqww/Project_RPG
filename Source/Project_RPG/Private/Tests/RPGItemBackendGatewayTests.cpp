#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Item/Backend/RPGItemBackendGateway.h"
#include "Item/Backend/RPGItemBackendJsonCodec.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "RPGItemTags.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace RPGItemBackendGatewayTests
{
bool ParseJson(const FString& Json, TSharedPtr<FJsonObject>& OutObject)
{
	const TSharedRef<TJsonReader<>> Reader =
		TJsonReaderFactory<>::Create(Json);
	return FJsonSerializer::Deserialize(
			Reader,
			OutObject,
			FJsonSerializer::EFlags::StoreNumbersAsStrings) &&
		OutObject.IsValid();
}

bool SerializeJson(
	const TSharedRef<FJsonObject>& Object,
	FString& OutJson)
{
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>>
		Writer = TJsonWriterFactory<
			TCHAR,
			TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutJson);
	return FJsonSerializer::Serialize(Object, Writer);
}

bool MakeRecord(
	const int64 Revision,
	FRPGItemRecord& OutRecord)
{
	FGameplayTagContainer InstanceTags;
	InstanceTags.AddTag(RPGGameplayTags::GameItem_Craft_fruit);
	TArray<FRPGItemStatValue> StatValues;
	FRPGItemStatValue& StatValue = StatValues.AddDefaulted_GetRef();
	StatValue.StatTag = RPGGameplayTags::Fragment_StatMod_1;
	StatValue.Value = 7.5f;

	FRPGItemInstanceState State;
	if (!FRPGItemInstanceState::TryRestore(
		FGuid::NewGuid(),
		104729,
		2,
		InstanceTags,
		StatValues,
		State))
	{
		return false;
	}

	FRPGItemOwnerRef Owner;
	Owner.Type = ERPGItemOwnerType::Character;
	Owner.OwnerId = FGuid::NewGuid().ToString(
		EGuidFormats::DigitsWithHyphensLower);
	FRPGItemLocation Location;
	Location.ContainerType = ERPGItemContainerType::Inventory;
	Location.ContainerId = Owner.OwnerId;
	Location.SlotIndex = 3;
	FRPGItemRecordMetadata Metadata;
	Metadata.CreationSource = RPGGameplayTags::GameItem_Craft_fruit;
	return FRPGItemRecord::TryRestore(
		FPrimaryAssetId(
			FPrimaryAssetType(FName(TEXT("RPGItemDefinition"))),
			FName(TEXT("GatewayPotion"))),
		1,
		Owner,
		Location,
		State,
		Revision,
		ERPGItemLifecycleState::Active,
		Metadata,
		OutRecord);
}

FRPGItemRepositoryCommitRequest MakeCommitRequest(
	const FRPGItemRecord& Record)
{
	FRPGItemRepositoryCommitRequest Request;
	Request.RequestId = FGuid::NewGuid();
	Request.Operation = TEXT("MoveItem");
	Request.CommandFingerprint = FString::Printf(
		TEXT("move|%s|%lld"),
		*Record.GetItemId().ToString(EGuidFormats::DigitsWithHyphensLower),
		Record.GetRevision());
	Request.Actor = Record.GetOwner();
	Request.AffectedQuantity = Record.GetQuantity();
	FRPGItemRecordMutation& Mutation =
		Request.Mutations.AddDefaulted_GetRef();
	Mutation.ExpectedRevision = Record.GetRevision();
	Mutation.NewRecord = Record;
	return Request;
}

bool BuildLoadResponse(
	const FString& CommitRequestJson,
	FString& OutJson)
{
	TSharedPtr<FJsonObject> RequestObject;
	if (!ParseJson(CommitRequestJson, RequestObject))
	{
		return false;
	}
	const TArray<TSharedPtr<FJsonValue>>* Mutations = nullptr;
	if (!RequestObject->TryGetArrayField(TEXT("mutations"), Mutations) ||
		!Mutations || Mutations->Num() != 1)
	{
		return false;
	}
	const TSharedPtr<FJsonObject> MutationObject = (*Mutations)[0]->AsObject();
	const TSharedPtr<FJsonObject>* RecordObject = nullptr;
	if (!MutationObject.IsValid() ||
		!MutationObject->TryGetObjectField(TEXT("newRecord"), RecordObject) ||
		!RecordObject || !RecordObject->IsValid())
	{
		return false;
	}

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Items;
	Items.Add(MakeShared<FJsonValueObject>(*RecordObject));
	Root->SetArrayField(TEXT("items"), MoveTemp(Items));
	return SerializeJson(Root, OutJson);
}

bool BuildCommitResponse(
	const FString& CommitRequestJson,
	FString& OutJson)
{
	TSharedPtr<FJsonObject> RequestObject;
	if (!ParseJson(CommitRequestJson, RequestObject))
	{
		return false;
	}

	FString RequestId;
	FString Operation;
	FString Fingerprint;
	int32 AffectedQuantity = 0;
	const TSharedPtr<FJsonObject>* ActorObject = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* Mutations = nullptr;
	if (!RequestObject->TryGetStringField(TEXT("requestId"), RequestId) ||
		!RequestObject->TryGetStringField(TEXT("operation"), Operation) ||
		!RequestObject->TryGetStringField(
			TEXT("commandFingerprint"), Fingerprint) ||
		!RequestObject->TryGetObjectField(TEXT("actor"), ActorObject) ||
		!ActorObject || !ActorObject->IsValid() ||
		!RequestObject->TryGetNumberField(
			TEXT("affectedQuantity"), AffectedQuantity) ||
		!RequestObject->TryGetArrayField(TEXT("mutations"), Mutations) ||
		!Mutations)
	{
		return false;
	}

	TArray<TSharedPtr<FJsonValue>> Records;
	for (const TSharedPtr<FJsonValue>& MutationValue : *Mutations)
	{
		const TSharedPtr<FJsonObject> MutationObject =
			MutationValue->AsObject();
		const TSharedPtr<FJsonObject>* RecordObject = nullptr;
		int64 ExpectedRevision = 0;
		if (!MutationObject.IsValid() ||
			!MutationObject->TryGetNumberField(
				TEXT("expectedRevision"), ExpectedRevision) ||
			!MutationObject->TryGetObjectField(
				TEXT("newRecord"), RecordObject) ||
			!RecordObject || !RecordObject->IsValid())
		{
			return false;
		}
		(*RecordObject)->SetField(
			TEXT("revision"),
			MakeShared<FJsonValueNumberString>(
				LexToString(ExpectedRevision + 1)));
		Records.Add(MakeShared<FJsonValueObject>(*RecordObject));
	}

	TSharedRef<FJsonObject> Response = MakeShared<FJsonObject>();
	Response->SetStringField(TEXT("status"), TEXT("Committed"));
	Response->SetStringField(TEXT("requestId"), RequestId);
	Response->SetStringField(TEXT("operation"), Operation);
	Response->SetStringField(TEXT("commandFingerprint"), Fingerprint);
	Response->SetObjectField(TEXT("actor"), *ActorObject);
	Response->SetNumberField(TEXT("affectedQuantity"), AffectedQuantity);
	Response->SetArrayField(TEXT("records"), MoveTemp(Records));
	return SerializeJson(Response, OutJson);
}

class FFakeRetryTransport final : public IRPGItemBackendTransport
{
public:
	TArray<FRPGItemBackendHttpRequest> Requests;

	virtual void Send(
		const FRPGItemBackendHttpRequest& Request,
		FRPGItemBackendHttpCompletion Completion) override
	{
		Requests.Add(Request);
		FRPGItemBackendHttpResponse Response;
		if (Requests.Num() == 1)
		{
			Completion(MoveTemp(Response));
			return;
		}

		Response.bTransportSuccessful = true;
		Response.StatusCode = 200;
		BuildCommitResponse(Request.Body, Response.Body);
		Completion(MoveTemp(Response));
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGItemBackendCodecTest,
	"ProjectRPG.Item.Backend.CodecPreservesRecordsAndInt64Revisions",
	EAutomationTestFlags::EditorContext |
		EAutomationTestFlags::EngineFilter)

bool FRPGItemBackendCodecTest::RunTest(const FString& Parameters)
{
	using namespace RPGItemBackendGatewayTests;
	constexpr int64 LargeRevision = 9007199254740993LL;
	FRPGItemRecord Record;
	TestTrue(TEXT("A test record can be restored"),
		MakeRecord(LargeRevision, Record));
	const FRPGItemRepositoryCommitRequest Request =
		MakeCommitRequest(Record);

	FString RequestJson;
	FString Error;
	TestTrue(TEXT("The commit request serializes"),
		FRPGItemBackendJsonCodec::SerializeCommitRequest(
			Request,
			RequestJson,
			&Error));
	TSharedPtr<FJsonObject> Root;
	TestTrue(TEXT("Serialized request is valid JSON"),
		ParseJson(RequestJson, Root));
	const TArray<TSharedPtr<FJsonValue>>* Mutations = nullptr;
	int64 SerializedRevision = 0;
	if (Root.IsValid() &&
		Root->TryGetArrayField(TEXT("mutations"), Mutations) &&
		Mutations && Mutations->Num() == 1)
	{
		(*Mutations)[0]->AsObject()->TryGetNumberField(
			TEXT("expectedRevision"),
			SerializedRevision);
	}
	TestEqual(TEXT("Revision does not lose JSON double precision"),
		SerializedRevision, LargeRevision);

	FString LoadJson;
	TestTrue(TEXT("A load response fixture can be produced"),
		BuildLoadResponse(RequestJson, LoadJson));
	TArray<FRPGItemRecord> LoadedRecords;
	TestTrue(TEXT("The load response restores item records"),
		FRPGItemBackendJsonCodec::DeserializeLoadResponse(
			LoadJson,
			LoadedRecords,
			&Error));
	TestEqual(TEXT("One record is restored"), LoadedRecords.Num(), 1);
	if (LoadedRecords.Num() == 1)
	{
		TestEqual(TEXT("Restored revision remains exact"),
			LoadedRecords[0].GetRevision(), LargeRevision);
		TestEqual(TEXT("Restored identity remains exact"),
			LoadedRecords[0].GetItemId(), Record.GetItemId());
		TestTrue(TEXT("Persistent instance tags are restored"),
			LoadedRecords[0].GetState().HasInstanceTag(
				RPGGameplayTags::GameItem_Craft_fruit));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGItemBackendRetryTest,
	"ProjectRPG.Item.Backend.CommitRetryReusesExactRequest",
	EAutomationTestFlags::EditorContext |
		EAutomationTestFlags::EngineFilter)

bool FRPGItemBackendRetryTest::RunTest(const FString& Parameters)
{
	using namespace RPGItemBackendGatewayTests;
	FRPGItemRecord Record;
	TestTrue(TEXT("A new record can be restored"), MakeRecord(0, Record));
	const FRPGItemRepositoryCommitRequest Request =
		MakeCommitRequest(Record);

	const TSharedRef<FFakeRetryTransport, ESPMode::ThreadSafe> Transport =
		MakeShared<FFakeRetryTransport, ESPMode::ThreadSafe>();
	FRPGItemBackendGateway Gateway(
		StaticCastSharedRef<IRPGItemBackendTransport>(Transport),
		2);
	bool bCompleted = false;
	FRPGItemBackendCommitResult Result;
	Gateway.Commit(
		Request,
		[&bCompleted, &Result](FRPGItemBackendCommitResult InResult)
		{
			bCompleted = true;
			Result = MoveTemp(InResult);
		});

	TestTrue(TEXT("The fake asynchronous request completes"), bCompleted);
	TestEqual(TEXT("A transient failure is retried once"),
		Transport->Requests.Num(), 2);
	if (Transport->Requests.Num() == 2)
	{
		TestEqual(TEXT("Retry reuses the exact serialized command"),
			Transport->Requests[1].Body,
			Transport->Requests[0].Body);
	}
	TestTrue(TEXT("The retry returns a successful receipt"),
		Result.WasSuccessful());
	TestEqual(TEXT("The receipt is correlated by RequestId"),
		Result.RequestId, Request.RequestId);
	TestEqual(TEXT("The backend advances revision exactly once"),
		Result.Records.Num() == 1
			? Result.Records[0].GetRevision()
			: static_cast<int64>(-1),
		static_cast<int64>(1));
	return true;
}

#endif
