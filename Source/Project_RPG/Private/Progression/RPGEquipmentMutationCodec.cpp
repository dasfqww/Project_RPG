#include "Progression/RPGEquipmentMutationCodec.h"
#include "Item/Policy/RPGItemInventoryPolicy.h"

#include "Dom/JsonObject.h"
#include "String/BytesToHex.h"
#include "Item/Backend/RPGItemBackendJsonCodec.h"
#include "Online/RPGBackendPlayerAuthState.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

THIRD_PARTY_INCLUDES_START
#include <openssl/sha.h>
THIRD_PARTY_INCLUDES_END

namespace RPGEquipmentMutation
{
	FString Guid(const FGuid& Id) { return Id.ToString(EGuidFormats::DigitsWithHyphensLower); }
	bool Revision(int64 Value) { return Value > 0 && Value < MAX_int64; }
	bool Expected(const FRPGEquipmentItemExpectation& Value)
	{ return Value.ItemId.IsValid() && Revision(Value.ExpectedRevision) && Value.ExpectedLevel >= 0 && Value.ExpectedLevel <= 3; }
	bool Unset(const FRPGEquipmentItemExpectation& Value)
	{ return !Value.ItemId.IsValid() && Value.ExpectedRevision == 0 && Value.ExpectedLevel == -1; }
	const TCHAR* const EquipmentNames[] = {
		TEXT("GameItem.Equipment.Progression.Starter.GreatSword"), TEXT("GameItem.Equipment.Progression.Starter.Chest"),
		TEXT("GameItem.Equipment.Progression.Starter.Gloves"), TEXT("GameItem.Equipment.Progression.Target.EnduranceGreatSword"),
		TEXT("GameItem.Equipment.Progression.Target.CirculationGreatSword"), TEXT("GameItem.Equipment.Progression.Target.Chest"),
		TEXT("GameItem.Equipment.Progression.Target.Gloves") };
	int32 Model(const FString& Name)
	{ for (int32 Index = 0; Index < UE_ARRAY_COUNT(EquipmentNames); ++Index) if (Name.Equals(EquipmentNames[Index], ESearchCase::CaseSensitive)) return Index; return INDEX_NONE; }
	int32 Slot(int32 Index) { return Index == 0 || Index == 3 || Index == 4 ? 9 : Index == 1 || Index == 5 ? 1 : 4; }
	bool Json(const TSharedRef<FJsonObject>& Object, FString& Out)
	{ Out.Reset(); return FJsonSerializer::Serialize(Object, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out)); }
	void SetRevision(const TSharedRef<FJsonObject>& Object, const TCHAR* Name, int64 Value, bool bDomain)
	{
		if (bDomain) Object->SetField(Name, MakeShared<FJsonValueNumberString>(LexToString(Value)));
		else Object->SetStringField(Name, LexToString(Value));
	}
	TSharedRef<FJsonObject> Item(const FRPGEquipmentItemExpectation& Value, bool bDomain)
	{
		auto Object = MakeShared<FJsonObject>();
		Object->SetStringField(bDomain ? TEXT("ItemId") : TEXT("itemId"), Guid(Value.ItemId));
		SetRevision(Object, bDomain ? TEXT("Revision") : TEXT("expectedRevision"), Value.ExpectedRevision, bDomain);
		Object->SetNumberField(bDomain ? TEXT("Level") : TEXT("expectedLevel"), Value.ExpectedLevel);
		return Object;
	}
	TArray<TSharedPtr<FJsonValue>> Costs(const TArray<FRPGEquipmentCostExpectation>& Values, bool bDomain)
	{
		TArray<TSharedPtr<FJsonValue>> Result;
		for (const auto& Value : Values)
		{
			auto Object = MakeShared<FJsonObject>();
			Object->SetStringField(bDomain ? TEXT("ItemId") : TEXT("itemId"), Guid(Value.ItemId));
			SetRevision(Object, bDomain ? TEXT("Revision") : TEXT("expectedRevision"), Value.ExpectedRevision, bDomain);
			Result.Add(MakeShared<FJsonValueObject>(Object));
		}
		return Result;
	}
	bool Normalize(const FRPGEquipmentMutationCommand& In, FRPGEquipmentMutationCommand& Out)
	{
		if (!In.RequestId.IsValid() || !In.CharacterId.IsValid() || FRPGEquipmentMutationCodec::Route(In.Operation).IsEmpty()) return false;
		const auto Op = In.Operation;
		const bool bCosts = Op == ERPGEquipmentMutationOperation::Upgrade || Op == ERPGEquipmentMutationOperation::Craft;
		if (bCosts ? In.Costs.IsEmpty() || In.Costs.Num() > 15 : !In.Costs.IsEmpty()) return false;
		if (Op == ERPGEquipmentMutationOperation::Craft)
		{
			if (!Unset(In.Equipment) || !Unset(In.Target) || Model(In.TargetDefinitionName) < 3) return false;
		}
		else
		{
			if (!Expected(In.Equipment) || !In.TargetDefinitionName.IsEmpty()) return false;
			if (Op == ERPGEquipmentMutationOperation::Transfer)
			{
				if (!Expected(In.Target) || In.Target.ItemId == In.Equipment.ItemId || In.Equipment.ExpectedLevel == 0 || In.Target.ExpectedLevel != 0) return false;
			}
			else if (!Unset(In.Target) || (Op == ERPGEquipmentMutationOperation::Upgrade ? In.Equipment.ExpectedLevel == 3 : In.Equipment.ExpectedLevel != 0)) return false;
		}
		TSet<FGuid> Ids;
		if (In.Equipment.ItemId.IsValid()) Ids.Add(In.Equipment.ItemId);
		if (In.Target.ItemId.IsValid()) Ids.Add(In.Target.ItemId);
		for (const auto& Cost : In.Costs)
		{
			if (!Cost.ItemId.IsValid() || !Revision(Cost.ExpectedRevision) || Ids.Contains(Cost.ItemId)) return false;
			Ids.Add(Cost.ItemId);
		}
		Out = In;
		Out.Costs.Sort([](const auto& A, const auto& B) { return Guid(A.ItemId).Compare(Guid(B.ItemId), ESearchCase::CaseSensitive) < 0; });
		return true;
	}
	TSharedRef<FJsonObject> Body(const FRPGEquipmentMutationCommand& Command, bool bDomain)
	{
		auto Object = MakeShared<FJsonObject>();
		Object->SetStringField(bDomain ? TEXT("RequestId") : TEXT("requestId"), Guid(Command.RequestId));
		if (bDomain) Object->SetStringField(TEXT("CharacterId"), Guid(Command.CharacterId));
		Object->SetNumberField(bDomain ? TEXT("RuleVersion") : TEXT("ruleVersion"), 1);
		if (Command.Operation == ERPGEquipmentMutationOperation::Upgrade || Command.Operation == ERPGEquipmentMutationOperation::Dismantle)
			Object->SetObjectField(bDomain ? TEXT("Equipment") : TEXT("equipment"), Item(Command.Equipment, bDomain));
		if (Command.Operation == ERPGEquipmentMutationOperation::Transfer)
		{
			Object->SetObjectField(bDomain ? TEXT("Source") : TEXT("source"), Item(Command.Equipment, bDomain));
			Object->SetObjectField(bDomain ? TEXT("Target") : TEXT("target"), Item(Command.Target, bDomain));
		}
		if (Command.Operation == ERPGEquipmentMutationOperation::Craft)
		{
			Object->SetStringField(bDomain ? TEXT("TargetDefinitionName") : TEXT("targetDefinitionName"), Command.TargetDefinitionName);
			Object->SetNumberField(bDomain ? TEXT("TargetDefinitionVersion") : TEXT("targetDefinitionVersion"), 1);
		}
		if (Command.Operation == ERPGEquipmentMutationOperation::Upgrade)
			Object->SetArrayField(bDomain ? TEXT("OreStacks") : TEXT("oreStacks"), Costs(Command.Costs, bDomain));
		if (Command.Operation == ERPGEquipmentMutationOperation::Craft)
			Object->SetArrayField(bDomain ? TEXT("TokenStacks") : TEXT("tokenStacks"), Costs(Command.Costs, bDomain));
		return Object;
	}
}

FString FRPGEquipmentMutationCodec::Route(ERPGEquipmentMutationOperation Operation)
{
	switch (Operation) {
	case ERPGEquipmentMutationOperation::Upgrade: return TEXT("upgrade");
	case ERPGEquipmentMutationOperation::Transfer: return TEXT("transfer");
	case ERPGEquipmentMutationOperation::Craft: return TEXT("craft");
	case ERPGEquipmentMutationOperation::Dismantle: return TEXT("dismantle");
	default: return {}; }
}

FString FRPGEquipmentMutationCodec::OperationName(ERPGEquipmentMutationOperation Operation)
{
	switch (Operation) {
	case ERPGEquipmentMutationOperation::Upgrade: return TEXT("FirstSliceEquipmentUpgrade");
	case ERPGEquipmentMutationOperation::Transfer: return TEXT("FirstSliceEquipmentTransfer");
	case ERPGEquipmentMutationOperation::Craft: return TEXT("FirstSliceEquipmentCraft");
	case ERPGEquipmentMutationOperation::Dismantle: return TEXT("FirstSliceEquipmentDismantle");
	default: return {}; }
}

bool FRPGEquipmentMutationCodec::Sha256(const FString& Text, FString& OutDigest)
{
	OutDigest.Reset(); const FTCHARToUTF8 Bytes(*Text);
	uint8 Digest[SHA256_DIGEST_LENGTH];
	// GenericPlatformMisc::GetSHA256Signature asserts on this Windows engine.
	// Use the module's existing OpenSSL dependency, not a platform stub or a custom hash.
	if (!SHA256(reinterpret_cast<const uint8*>(Bytes.Get()), Bytes.Length(), Digest)) return false;
	OutDigest = BytesToHex(Digest, SHA256_DIGEST_LENGTH).ToLower(); return true;
}

bool FRPGEquipmentMutationScope::Create(const FString& Endpoint, const FString& SteamId, const FGuid& CharacterId, FRPGEquipmentMutationScope& OutScope)
{
	OutScope = {};
	FString Base = Endpoint.TrimStartAndEnd(); while (Base.EndsWith(TEXT("/"))) Base.LeftChopInline(1);
	if (!CharacterId.IsValid() || !FRPGBackendPlayerAuthState::IsSteamId(SteamId) || Base.Len() > 2048
		|| !(Base.StartsWith(TEXT("https://"), ESearchCase::CaseSensitive) || Base.StartsWith(TEXT("http://"), ESearchCase::CaseSensitive))
		|| Base.Contains(TEXT("@")) || Base.Contains(TEXT("?")) || Base.Contains(TEXT("#"))) return false;
	for (const TCHAR C : Base) if (FChar::IsWhitespace(C) || FChar::IsControl(C)) return false;
	if (!FRPGEquipmentMutationCodec::Sha256(TEXT("EquipmentClient.v1\n") + Base + TEXT("\n") + SteamId + TEXT("\n") + RPGEquipmentMutation::Guid(CharacterId), OutScope.Key)) return false;
	OutScope.CharacterId = CharacterId; return true;
}

bool FRPGEquipmentMutationCodec::Encode(const FRPGEquipmentMutationCommand& Command, FString& OutBody, FString& OutFingerprint)
{
	using namespace RPGEquipmentMutation;
	OutBody.Reset(); OutFingerprint.Reset(); FRPGEquipmentMutationCommand Canonical;
	if (!Normalize(Command, Canonical) || !Json(Body(Canonical, false), OutBody) || FTCHARToUTF8(*OutBody).Length() > MaximumBodyBytes) { OutBody.Reset(); return false; }
	auto Identity = MakeShared<FJsonObject>();
	Identity->SetStringField(TEXT("operation"), OperationName(Canonical.Operation));
	Identity->SetObjectField(TEXT("command"), Body(Canonical, true));
	FString JsonIdentity;
	if (!Json(Identity, JsonIdentity) || !Sha256(JsonIdentity, OutFingerprint)) { OutBody.Reset(); return false; }
	return true;
}

namespace RPGEquipmentMutation
{
	TSharedPtr<FJsonObject> Parse(const FString& Text, int32 MaximumBytes)
	{
		TSharedPtr<FJsonObject> Object;
		if (FTCHARToUTF8(*Text).Length() > MaximumBytes || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Object)) return nullptr;
		return Object;
	}
	bool String(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, FString& Out)
	{
		const auto Value = Object ? Object->TryGetField(Key) : nullptr;
		return Value && Value->Type == EJson::String && Value->TryGetString(Out);
	}
	bool Number(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, int32& Out)
	{
		const auto Value = Object ? Object->TryGetField(Key) : nullptr;
		double Raw = 0;
		if (!Value || Value->Type != EJson::Number || !Value->TryGetNumber(Raw) || !FMath::IsFinite(Raw)
			|| Raw < 0 || Raw > MAX_int32 || Raw != FMath::FloorToDouble(Raw)) return false;
		Out = static_cast<int32>(Raw); return true;
	}
	bool ReadGuid(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, FGuid& Out)
	{
		FString Text;
		return String(Object, Key, Text) && FGuid::Parse(Text, Out) && Out.IsValid() && Guid(Out).Equals(Text, ESearchCase::CaseSensitive);
	}
	bool ReadRevision(const TSharedPtr<FJsonObject>& Object, int64& Out)
	{
		FString Text;
		return String(Object, TEXT("expectedRevision"), Text) && Text.Len() <= 19 && LexTryParseString(Out, *Text)
			&& Revision(Out) && Text.Equals(LexToString(Out), ESearchCase::CaseSensitive);
	}
	bool ReadItem(const TSharedPtr<FJsonObject>& Root, const TCHAR* Key, FRPGEquipmentItemExpectation& Out)
	{
		const TSharedPtr<FJsonObject>* Object = nullptr;
		return Root->TryGetObjectField(Key, Object) && Object && *Object && (*Object)->Values.Num() == 3
			&& ReadGuid(*Object, TEXT("itemId"), Out.ItemId) && ReadRevision(*Object, Out.ExpectedRevision)
			&& Number(*Object, TEXT("expectedLevel"), Out.ExpectedLevel);
	}
	bool ReadCosts(const TSharedPtr<FJsonObject>& Root, const TCHAR* Key, TArray<FRPGEquipmentCostExpectation>& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!Root->TryGetArrayField(Key, Values) || !Values || Values->IsEmpty() || Values->Num() > 15) return false;
		for (const auto& Value : *Values)
		{
			const auto Object = Value && Value->Type == EJson::Object ? Value->AsObject() : nullptr;
			FRPGEquipmentCostExpectation Cost;
			if (!Object || Object->Values.Num() != 2 || !ReadGuid(Object, TEXT("itemId"), Cost.ItemId) || !ReadRevision(Object, Cost.ExpectedRevision)) return false;
			Out.Add(Cost);
		}
		return true;
	}
}

bool FRPGEquipmentMutationCodec::Decode(const FGuid& CharacterId, ERPGEquipmentMutationOperation Operation,
	const FString& FrozenBody, FRPGEquipmentMutationCommand& OutCommand)
{
	using namespace RPGEquipmentMutation;
	OutCommand = {}; const auto Root = Parse(FrozenBody, MaximumBodyBytes);
	FRPGEquipmentMutationCommand Command; Command.CharacterId = CharacterId; Command.Operation = Operation;
	int32 Rule = 0;
	if (!Root || !ReadGuid(Root, TEXT("requestId"), Command.RequestId) || !Number(Root, TEXT("ruleVersion"), Rule) || Rule != 1) return false;
	int32 Keys = 0;
	switch (Operation)
	{
	case ERPGEquipmentMutationOperation::Upgrade:
		Keys = 4; if (!ReadItem(Root, TEXT("equipment"), Command.Equipment) || !ReadCosts(Root, TEXT("oreStacks"), Command.Costs)) return false; break;
	case ERPGEquipmentMutationOperation::Transfer:
		Keys = 4; if (!ReadItem(Root, TEXT("source"), Command.Equipment) || !ReadItem(Root, TEXT("target"), Command.Target)) return false; break;
	case ERPGEquipmentMutationOperation::Craft:
	{
		int32 Version = 0; Keys = 5;
		if (!String(Root, TEXT("targetDefinitionName"), Command.TargetDefinitionName) || !Number(Root, TEXT("targetDefinitionVersion"), Version)
			|| Version != 1 || !ReadCosts(Root, TEXT("tokenStacks"), Command.Costs)) return false;
		break;
	}
	case ERPGEquipmentMutationOperation::Dismantle:
		Keys = 3; if (!ReadItem(Root, TEXT("equipment"), Command.Equipment)) return false; break;
	default: return false;
	}
	FString Reencoded, Fingerprint;
	if (Root->Values.Num() != Keys || !Encode(Command, Reencoded, Fingerprint) || !Reencoded.Equals(FrozenBody, ESearchCase::CaseSensitive)) return false;
	OutCommand = MoveTemp(Command); return true;
}

namespace RPGEquipmentMutation
{
	bool Location(const FRPGItemRecord& Record, bool bGrant)
	{
		const auto& Where = Record.GetLocation();
		if (bGrant && Where.ContainerType == ERPGItemContainerType::Mail) return true;
		if (!Where.ContainerId.Equals(Record.GetOwner().OwnerId, ESearchCase::CaseSensitive)) return false;
		if (Where.ContainerType == ERPGItemContainerType::Inventory) return Where.SlotIndex >= 0 && Where.SlotIndex < RPGItemInventoryPolicy::SlotCapacity;
		if (bGrant) return false; // New grants never auto-equip.
		const int32 Index = Model(Record.GetDefinitionId().PrimaryAssetName.ToString());
		return Index != INDEX_NONE && Where.ContainerType == ERPGItemContainerType::Equipment && Where.SlotIndex == Slot(Index);
	}
	bool Equipment(const FRPGItemRecord& Record, int32 Level, bool bGrant, bool bConsumed = false)
	{
		const auto& Growth = Record.GetState().GetEquipmentProgression();
		if (Model(Record.GetDefinitionId().PrimaryAssetName.ToString()) == INDEX_NONE
			|| !Growth.IsSet() || !Growth.IsValid() || Growth.Level != Level) return false;
		if (bConsumed) return Record.GetQuantity() == 0 && Record.GetLifecycleState() == ERPGItemLifecycleState::Consumed
			&& Record.GetLocation().ContainerType == ERPGItemContainerType::Terminal;
		return Record.IsActive() && Record.GetQuantity() == 1 && Location(Record, bGrant);
	}
	bool Cost(const FRPGItemRecord& Record, const FRPGEquipmentCostExpectation& Expected, bool bToken)
	{
		if (Record.GetItemId() != Expected.ItemId || Record.GetRevision() != Expected.ExpectedRevision + 1
			|| !Record.GetDefinitionId().PrimaryAssetName.ToString().Equals(bToken ? TEXT("GameItem.Material.Progression.Token") : TEXT("GameItem.Material.Progression.Ore"), ESearchCase::CaseSensitive)
			|| Record.GetState().GetEquipmentProgression().IsSet()) return false;
		if (!Record.IsActive()) return Record.GetLifecycleState() == ERPGItemLifecycleState::Consumed && Record.GetQuantity() == 0
			&& Record.GetLocation().ContainerType == ERPGItemContainerType::Terminal;
		return Record.GetQuantity() > 0 && Record.GetQuantity() <= 999
			&& Record.GetLocation().ContainerType == ERPGItemContainerType::Inventory && Location(Record, false);
	}
}

bool FRPGEquipmentMutationCodec::AcceptReceipt(const FRPGEquipmentMutationCommand& Command, const FString& BodyText,
	FRPGItemBackendCommitResult& OutReceipt)
{
	using namespace RPGEquipmentMutation;
	OutReceipt = {};
	FString Wire, Fingerprint; FRPGItemBackendCommitResult Receipt;
	const auto Root = Parse(BodyText, 1024 * 1024);
	FString RawOperation, RawStatus; int32 Affected = 0;
	if (!Encode(Command, Wire, Fingerprint) || !Root || !String(Root, TEXT("operation"), RawOperation)
		|| !RawOperation.Equals(OperationName(Command.Operation), ESearchCase::CaseSensitive)
		|| !String(Root, TEXT("status"), RawStatus)
		|| !(RawStatus.Equals(TEXT("Committed"), ESearchCase::CaseSensitive) || RawStatus.Equals(TEXT("AlreadyCommitted"), ESearchCase::CaseSensitive))
		|| !Number(Root, TEXT("affectedQuantity"), Affected)
		// This existing decoder preserves the numeric spelling of Int64 revisions.
		|| !FRPGItemBackendJsonCodec::DeserializeCommitResponse(BodyText, Receipt) || !Receipt.WasSuccessful()
		|| Receipt.RequestId != Command.RequestId || Receipt.Actor.Type != ERPGItemOwnerType::Character
		|| !Receipt.Actor.OwnerId.Equals(Guid(Command.CharacterId), ESearchCase::CaseSensitive)
		|| !Receipt.CommandFingerprint.Equals(Fingerprint, ESearchCase::CaseSensitive)
		|| Receipt.Records.Num() < 2 || Receipt.Records.Num() > 16) return false;
	const int32 ExpectedAffected = Command.Operation == ERPGEquipmentMutationOperation::Upgrade ? (Command.Equipment.ExpectedLevel + 1) * 2
		: Command.Operation == ERPGEquipmentMutationOperation::Transfer ? 0 : Command.Operation == ERPGEquipmentMutationOperation::Craft ? 8 : 1;
	if (Affected != ExpectedAffected || Receipt.AffectedQuantity != ExpectedAffected) return false;
	TSet<FGuid> Ids;
	for (const auto& Record : Receipt.Records)
	{
		if (!Record.IsStructurallyValid() || Record.GetOwner() != Receipt.Actor || Ids.Contains(Record.GetItemId())
			|| Record.GetDefinitionVersion() != 1 || !Record.GetDefinitionId().PrimaryAssetType.ToString().Equals(TEXT("RPGItemDefinition"), ESearchCase::CaseSensitive)
			|| Record.GetMetadata().BindState != ERPGItemBindState::CharacterBound) return false;
		Ids.Add(Record.GetItemId());
	}
	auto Find = [&Receipt](const FGuid& Id) { return Receipt.Records.FindByPredicate([&Id](const auto& Record) { return Record.GetItemId() == Id; }); };
	const auto* Source = Find(Command.Equipment.ItemId);
	if (Command.Operation == ERPGEquipmentMutationOperation::Transfer)
	{
		const auto* Target = Find(Command.Target.ItemId);
		if (Receipt.Records.Num() != 2 || !Source || !Target || Source->GetRevision() != Command.Equipment.ExpectedRevision + 1
			|| Target->GetRevision() != Command.Target.ExpectedRevision + 1 || !Equipment(*Source, 0, false)
			|| !Equipment(*Target, Command.Equipment.ExpectedLevel, false)
			|| Slot(Model(Source->GetDefinitionId().PrimaryAssetName.ToString())) != Slot(Model(Target->GetDefinitionId().PrimaryAssetName.ToString()))) return false;
	}
	else if (Command.Operation == ERPGEquipmentMutationOperation::Dismantle)
	{
		if (Receipt.Records.Num() != 2 || !Source || Source->GetRevision() != Command.Equipment.ExpectedRevision + 1
			|| Model(Source->GetDefinitionId().PrimaryAssetName.ToString()) < 3 || !Equipment(*Source, 0, false, true)) return false;
		for (const auto& Record : Receipt.Records) if (Record.GetItemId() != Source->GetItemId())
		{
			if (!Record.GetDefinitionId().PrimaryAssetName.ToString().Equals(TEXT("GameItem.Material.Progression.Token"), ESearchCase::CaseSensitive)
				|| !Record.IsActive() || Record.GetQuantity() < 1 || Record.GetQuantity() > 999
				|| Record.GetState().GetEquipmentProgression().IsSet() || !Location(Record, true)
				|| Record.GetLocation().ContainerType == ERPGItemContainerType::Equipment) return false;
		}
	}
	else
	{
		const bool bCraft = Command.Operation == ERPGEquipmentMutationOperation::Craft;
		const FRPGItemRecord* Granted = nullptr;
		if (bCraft)
		{
			for (const auto& Record : Receipt.Records) if (Record.GetDefinitionId().PrimaryAssetName.ToString().Equals(Command.TargetDefinitionName, ESearchCase::CaseSensitive))
			{
				if (Granted || Record.GetRevision() != 1 || !Equipment(Record, 0, true)) return false;
				Granted = &Record;
			}
			if (!Granted || Command.Costs.ContainsByPredicate([Granted](const auto& Value) { return Value.ItemId == Granted->GetItemId(); })) return false;
		}
		else if (!Source || Source->GetRevision() != Command.Equipment.ExpectedRevision + 1 || !Equipment(*Source, Command.Equipment.ExpectedLevel + 1, false)) return false;
		int32 CostCount = 0;
		for (const auto& Record : Receipt.Records)
		{
			if (&Record == (bCraft ? Granted : Source)) continue;
			const auto* Expected = Command.Costs.FindByPredicate([&Record](const auto& Value) { return Value.ItemId == Record.GetItemId(); });
			if (!Expected || !Cost(Record, *Expected, bCraft)) return false;
			++CostCount;
		}
		if (CostCount < 1) return false;
	}
	OutReceipt = MoveTemp(Receipt); return true;
}

bool FRPGEquipmentMutationCodec::IsDefinitiveRejection(const FRPGItemBackendHttpResponse& Response)
{
	using namespace RPGEquipmentMutation;
	if (!Response.bTransportSuccessful || Response.StatusCode != 409) return false;
	const auto Root = Parse(Response.Body, 8192); FString Error;
	if (!Root || Root->Values.Num() != 1 || !String(Root, TEXT("error"), Error)) return false;
	// These checks run AFTER exact successful-receipt recovery, under the same
	// repository lock. A queued copy cannot repair its frozen operand revisions.
	// Authentication, lease contention, missing items and unknown codes do NOT
	// establish non-commit and must retain the original command.
	static const TCHAR* const Codes[] = {
		TEXT("item_revision_conflict"), TEXT("equipment_level_conflict"), TEXT("item_owner_mismatch"),
		TEXT("item_not_active"), TEXT("item_not_character_bound"), TEXT("item_locked"), TEXT("item_expired"),
		TEXT("unsupported_equipment_definition"), TEXT("invalid_equipment_quantity"), TEXT("invalid_equipment_location"),
		TEXT("equipment_not_initialized"), TEXT("unsupported_ore_definition"), TEXT("invalid_ore_stack"), TEXT("insufficient_ore"),
		TEXT("unsupported_token_definition"), TEXT("invalid_token_stack"), TEXT("insufficient_tokens"), TEXT("equipment_slot_mismatch"),
		TEXT("transfer_source_not_enhanced"), TEXT("transfer_target_already_enhanced"), TEXT("equipment_at_maximum_level"),
		TEXT("starter_equipment_not_dismantlable"), TEXT("enhanced_equipment_not_dismantlable") };
	for (const TCHAR* Code : Codes) if (Error.Equals(Code, ESearchCase::CaseSensitive)) return true;
	return false;
}
