#include "Economy/Backend/RPGEconomyBackendJsonCodec.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace RPGEconomyBackendJson
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
	TSharedPtr<FJsonObject> Object;
	const TSharedRef<TJsonReader<>> Reader =
		TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(
			Reader,
			Object,
			FJsonSerializer::EFlags::StoreNumbersAsStrings) ||
		!Object.IsValid())
	{
		SetError(OutError, TEXT("The economy backend response is not valid JSON."));
		return nullptr;
	}
	return Object;
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
		SetError(OutError, TEXT("The economy request could not be serialized."));
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

bool TryParseScope(const FString& Value, ERPGCurrencyScope& OutScope)
{
	if (Value.Equals(TEXT("Account"), ESearchCase::IgnoreCase))
	{
		OutScope = ERPGCurrencyScope::Account;
	}
	else if (Value.Equals(TEXT("Roster"), ESearchCase::IgnoreCase))
	{
		OutScope = ERPGCurrencyScope::Roster;
	}
	else if (Value.Equals(TEXT("Character"), ESearchCase::IgnoreCase))
	{
		OutScope = ERPGCurrencyScope::Character;
	}
	else
	{
		return false;
	}
	return true;
}

bool TryParseStatus(
	const FString& Value,
	ERPGEconomyBackendStatus& OutStatus)
{
	if (Value.Equals(TEXT("Committed"), ESearchCase::IgnoreCase))
	{
		OutStatus = ERPGEconomyBackendStatus::Succeeded;
	}
	else if (Value.Equals(TEXT("AlreadyCommitted"), ESearchCase::IgnoreCase))
	{
		OutStatus = ERPGEconomyBackendStatus::AlreadyApplied;
	}
	else if (Value.Equals(TEXT("InvalidRequest"), ESearchCase::IgnoreCase))
	{
		OutStatus = ERPGEconomyBackendStatus::InvalidRequest;
	}
	else if (Value.Equals(TEXT("IdempotencyConflict"), ESearchCase::IgnoreCase))
	{
		OutStatus = ERPGEconomyBackendStatus::IdempotencyConflict;
	}
	else if (Value.Equals(TEXT("CharacterNotFound"), ESearchCase::IgnoreCase) ||
		Value.Equals(TEXT("DefinitionNotFound"), ESearchCase::IgnoreCase))
	{
		OutStatus = ERPGEconomyBackendStatus::NotFound;
	}
	else if (Value.Equals(TEXT("CurrencyDisabled"), ESearchCase::IgnoreCase))
	{
		OutStatus = ERPGEconomyBackendStatus::CurrencyDisabled;
	}
	else if (Value.Equals(TEXT("InsufficientBalance"), ESearchCase::IgnoreCase))
	{
		OutStatus = ERPGEconomyBackendStatus::InsufficientBalance;
	}
	else if (Value.Equals(TEXT("BalanceLimitExceeded"), ESearchCase::IgnoreCase))
	{
		OutStatus = ERPGEconomyBackendStatus::BalanceLimitExceeded;
	}
	else if (Value.Equals(TEXT("InternalError"), ESearchCase::IgnoreCase))
	{
		OutStatus = ERPGEconomyBackendStatus::ServerError;
	}
	else
	{
		return false;
	}
	return true;
}

bool TryParseBalance(
	const TSharedPtr<FJsonObject>& Object,
	FRPGCurrencyBalance& OutBalance)
{
	FString CurrencyCode;
	FString Scope;
	FRPGCurrencyBalance Balance;
	if (!Object.IsValid() ||
		!Object->TryGetStringField(TEXT("currencyCode"), CurrencyCode) ||
		CurrencyCode.IsEmpty() ||
		!Object->TryGetStringField(TEXT("displayName"), Balance.DisplayName) ||
		!Object->TryGetStringField(TEXT("scope"), Scope) ||
		!TryParseScope(Scope, Balance.Scope) ||
		!Object->TryGetStringField(TEXT("ownerId"), Balance.OwnerId) ||
		!Object->TryGetNumberField(TEXT("balance"), Balance.Balance) ||
		!Object->TryGetNumberField(TEXT("maxBalance"), Balance.MaxBalance) ||
		!Object->TryGetNumberField(TEXT("revision"), Balance.Revision))
	{
		return false;
	}

	Balance.CurrencyCode = FName(*CurrencyCode);
	if (!Balance.IsValid())
	{
		return false;
	}
	OutBalance = MoveTemp(Balance);
	return true;
}

bool TryParseChangeResult(
	const TSharedPtr<FJsonObject>& Object,
	FRPGCurrencyChangeResult& OutChange)
{
	FString CurrencyCode;
	FString Scope;
	FRPGCurrencyChangeResult Change;
	if (!Object.IsValid() ||
		!Object->TryGetStringField(TEXT("currencyCode"), CurrencyCode) ||
		CurrencyCode.IsEmpty() ||
		!Object->TryGetStringField(TEXT("scope"), Scope) ||
		!TryParseScope(Scope, Change.Scope) ||
		!Object->TryGetStringField(TEXT("ownerId"), Change.OwnerId) ||
		Change.OwnerId.IsEmpty() ||
		!Object->TryGetNumberField(TEXT("delta"), Change.Delta) ||
		Change.Delta == 0 ||
		!Object->TryGetNumberField(
			TEXT("previousBalance"), Change.PreviousBalance) ||
		!Object->TryGetNumberField(TEXT("newBalance"), Change.NewBalance) ||
		!Object->TryGetNumberField(TEXT("revision"), Change.Revision) ||
		Change.PreviousBalance < 0 ||
		Change.NewBalance < 0 ||
		Change.Revision <= 0)
	{
		return false;
	}

	Change.CurrencyCode = FName(*CurrencyCode);
	OutChange = MoveTemp(Change);
	return true;
}
}

bool FRPGEconomyBackendJsonCodec::SerializeTransactionRequest(
	const FRPGEconomyTransactionRequest& Request,
	FString& OutJson,
	FString* OutError)
{
	using namespace RPGEconomyBackendJson;
	FGuid CharacterId;
	FGuid DungeonSessionId;
	if (!Request.RequestId.IsValid() ||
		!FGuid::Parse(Request.CharacterId, CharacterId) ||
		!FGuid::Parse(Request.DungeonSessionId, DungeonSessionId) ||
		Request.Operation.IsNone() ||
		Request.CommandFingerprint.IsEmpty() ||
		Request.Reason.IsEmpty() ||
		Request.Changes.Num() < 1 ||
		Request.Changes.Num() > 16)
	{
		SetError(OutError, TEXT("The economy transaction request is invalid."));
		return false;
	}

	TSet<FName> CurrencyCodes;
	TArray<TSharedPtr<FJsonValue>> Changes;
	Changes.Reserve(Request.Changes.Num());
	for (const FRPGCurrencyChange& Change : Request.Changes)
	{
		if (Change.CurrencyCode.IsNone() ||
			Change.Delta == 0 ||
			CurrencyCodes.Contains(Change.CurrencyCode))
		{
			SetError(OutError, TEXT("An economy currency change is invalid."));
			return false;
		}
		CurrencyCodes.Add(Change.CurrencyCode);

		TSharedRef<FJsonObject> ChangeObject = MakeShared<FJsonObject>();
		ChangeObject->SetStringField(
			TEXT("currencyCode"),
			Change.CurrencyCode.ToString());
		SetInt64Field(ChangeObject, TEXT("delta"), Change.Delta);
		Changes.Add(MakeShared<FJsonValueObject>(ChangeObject));
	}

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(
		TEXT("requestId"),
		Request.RequestId.ToString(EGuidFormats::DigitsWithHyphensLower));
	Root->SetStringField(
		TEXT("characterId"),
		CharacterId.ToString(EGuidFormats::DigitsWithHyphensLower));
	Root->SetStringField(
		TEXT("dungeonSessionId"),
		DungeonSessionId.ToString(EGuidFormats::DigitsWithHyphensLower));
	Root->SetStringField(TEXT("operation"), Request.Operation.ToString());
	Root->SetStringField(
		TEXT("commandFingerprint"),
		Request.CommandFingerprint);
	Root->SetStringField(TEXT("reason"), Request.Reason);
	Root->SetArrayField(TEXT("changes"), MoveTemp(Changes));
	return SerializeObject(Root, OutJson, OutError);
}

bool FRPGEconomyBackendJsonCodec::DeserializeWalletResponse(
	const FString& Json,
	FRPGEconomyWalletResult& OutResult,
	FString* OutError)
{
	using namespace RPGEconomyBackendJson;
	const TSharedPtr<FJsonObject> Root = ParseObject(Json, OutError);
	const TArray<TSharedPtr<FJsonValue>>* BalanceValues = nullptr;
	FRPGEconomyWalletResult Result;
	FGuid CharacterId;
	FGuid RosterId;
	if (!Root.IsValid() ||
		!Root->TryGetStringField(TEXT("characterId"), Result.CharacterId) ||
		!FGuid::Parse(Result.CharacterId, CharacterId) ||
		!Root->TryGetStringField(TEXT("rosterId"), Result.RosterId) ||
		!FGuid::Parse(Result.RosterId, RosterId) ||
		!Root->TryGetStringField(TEXT("accountId"), Result.AccountId) ||
		Result.AccountId.IsEmpty() ||
		!Root->TryGetArrayField(TEXT("balances"), BalanceValues) ||
		!BalanceValues)
	{
		SetError(OutError, TEXT("The economy wallet response is invalid."));
		return false;
	}

	TSet<FName> CurrencyCodes;
	for (const TSharedPtr<FJsonValue>& Value : *BalanceValues)
	{
		FRPGCurrencyBalance Balance;
		if (!Value.IsValid() ||
			!TryParseBalance(Value->AsObject(), Balance) ||
			CurrencyCodes.Contains(Balance.CurrencyCode))
		{
			SetError(OutError, TEXT("The economy wallet contains an invalid balance."));
			return false;
		}
		CurrencyCodes.Add(Balance.CurrencyCode);
		Result.Balances.Add(MoveTemp(Balance));
	}

	Result.Status = ERPGEconomyBackendStatus::Succeeded;
	OutResult = MoveTemp(Result);
	return true;
}

bool FRPGEconomyBackendJsonCodec::DeserializeCommitResponse(
	const FString& Json,
	FRPGEconomyCommitResult& OutResult,
	FString* OutError)
{
	using namespace RPGEconomyBackendJson;
	const TSharedPtr<FJsonObject> Root = ParseObject(Json, OutError);
	const TArray<TSharedPtr<FJsonValue>>* ChangeValues = nullptr;
	FString Status;
	FString RequestId;
	FString Operation;
	FGuid CharacterId;
	FRPGEconomyCommitResult Result;
	if (!Root.IsValid() ||
		!Root->TryGetStringField(TEXT("status"), Status) ||
		!TryParseStatus(Status, Result.Status) ||
		!Root->TryGetStringField(TEXT("requestId"), RequestId) ||
		!FGuid::Parse(RequestId, Result.RequestId) ||
		!Root->TryGetStringField(TEXT("characterId"), Result.CharacterId) ||
		!FGuid::Parse(Result.CharacterId, CharacterId) ||
		!Root->TryGetStringField(TEXT("operation"), Operation) ||
		Operation.IsEmpty() ||
		!Root->TryGetStringField(
			TEXT("commandFingerprint"), Result.CommandFingerprint) ||
		!Root->TryGetStringField(TEXT("reason"), Result.Reason) ||
		!Root->TryGetArrayField(TEXT("changes"), ChangeValues) ||
		!ChangeValues)
	{
		SetError(OutError, TEXT("The economy transaction response is invalid."));
		return false;
	}

	TSet<FName> CurrencyCodes;
	for (const TSharedPtr<FJsonValue>& Value : *ChangeValues)
	{
		FRPGCurrencyChangeResult Change;
		if (!Value.IsValid() ||
			!TryParseChangeResult(Value->AsObject(), Change) ||
			CurrencyCodes.Contains(Change.CurrencyCode))
		{
			SetError(
				OutError,
				TEXT("The economy transaction contains an invalid result."));
			return false;
		}
		CurrencyCodes.Add(Change.CurrencyCode);
		Result.Changes.Add(MoveTemp(Change));
	}

	if (Result.WasSuccessful() && Result.Changes.IsEmpty())
	{
		SetError(
			OutError,
			TEXT("A successful economy transaction has no changes."));
		return false;
	}

	Result.Operation = FName(*Operation);
	OutResult = MoveTemp(Result);
	return true;
}
