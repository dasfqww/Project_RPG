#include "Economy/RPGDungeonRewardSnapshot.h"

#include "Economy/Backend/RPGEconomyBackendJsonCodec.h"
#include "Item/Backend/RPGItemBackendJsonCodec.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

bool FRPGDungeonRewardSnapshotCodec::Decode(const FString& Json, const int32 HttpStatus,
	const FString& ExpectedSessionId, const FString& ExpectedCharacterId, FRPGDungeonRewardSnapshotResult& OutResult)
{
	OutResult = FRPGDungeonRewardSnapshotResult();
	OutResult.HttpStatus = HttpStatus;
	FGuid ExpectedSession, ExpectedCharacter, ReturnedSession, ReturnedCharacter;
	TSharedPtr<FJsonObject> Root;
	FString State, SessionId, CharacterId;
	if (!FGuid::Parse(ExpectedSessionId, ExpectedSession) || !ExpectedSession.IsValid()
		|| !FGuid::Parse(ExpectedCharacterId, ExpectedCharacter) || !ExpectedCharacter.IsValid()
		|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root,
			FJsonSerializer::EFlags::StoreNumbersAsStrings) || !Root
		|| !Root->TryGetStringField(TEXT("dungeonSessionId"), SessionId)
		|| !Root->TryGetStringField(TEXT("characterId"), CharacterId)
		|| !Root->TryGetStringField(TEXT("state"), State)
		|| !FGuid::Parse(SessionId, ReturnedSession) || ReturnedSession != ExpectedSession
		|| !FGuid::Parse(CharacterId, ReturnedCharacter) || ReturnedCharacter != ExpectedCharacter)
		return false;
	OutResult.DungeonSessionId = ExpectedSession.ToString(EGuidFormats::DigitsWithHyphensLower);
	OutResult.CharacterId = ExpectedCharacter.ToString(EGuidFormats::DigitsWithHyphensLower);
	if (HttpStatus == 202 && State == TEXT("Pending"))
	{
		OutResult.Status = ERPGDungeonRewardSnapshotStatus::Pending;
		return true;
	}
	const TSharedPtr<FJsonObject>* WalletObject = nullptr;
	FString WalletJson;
	if (HttpStatus != 200 || State != TEXT("Ready") || !Root->TryGetObjectField(TEXT("wallet"), WalletObject)
		|| !WalletObject || !WalletObject->IsValid()
		|| !FJsonSerializer::Serialize(WalletObject->ToSharedRef(), TJsonWriterFactory<>::Create(&WalletJson))
		|| !FRPGEconomyBackendJsonCodec::DeserializeWalletResponse(WalletJson, OutResult.Wallet)
		|| !FGuid::Parse(OutResult.Wallet.CharacterId, ReturnedCharacter) || ReturnedCharacter != ExpectedCharacter
		|| !FRPGItemBackendJsonCodec::DeserializeLoadResponse(Json, OutResult.Items) || OutResult.Items.Num() > 500)
		return false;
	for (const FRPGItemRecord& Item : OutResult.Items)
	{
		if (!Item.IsActive() || Item.GetOwner().Type != ERPGItemOwnerType::Character
			|| !FGuid::Parse(Item.GetOwner().OwnerId, ReturnedCharacter) || ReturnedCharacter != ExpectedCharacter)
			return false;
	}
	const TSharedPtr<FJsonValue> DeliveryValue = Root->TryGetField(TEXT("delivery"));
	if (DeliveryValue.IsValid() && DeliveryValue->Type != EJson::Null)
	{
		const TSharedPtr<FJsonObject>* Delivery = nullptr;
		FString QuantityText;
		if (!Root->TryGetObjectField(TEXT("delivery"), Delivery) || !Delivery || !Delivery->IsValid()
			|| !(*Delivery)->TryGetStringField(TEXT("rewardVersion"), OutResult.DeliveryRewardVersion)
			|| OutResult.DeliveryRewardVersion.IsEmpty() || OutResult.DeliveryRewardVersion.Len() > 64
			|| !(*Delivery)->HasTypedField<EJson::Number>(TEXT("itemQuantity"))
			|| !(*Delivery)->TryGetStringField(TEXT("itemQuantity"), QuantityText)
			|| QuantityText.IsEmpty() || QuantityText.Len() > 10) return false;
		for (const TCHAR Character : QuantityText)
			if (Character < TEXT('0') || Character > TEXT('9')) return false;
		const int64 Quantity = FCString::Strtoi64(*QuantityText, nullptr, 10);
		if (Quantity > MAX_int32) return false;
		OutResult.DeliveredItemQuantity = static_cast<int32>(Quantity);
	}
	OutResult.Wallet.Status = ERPGEconomyBackendStatus::Succeeded;
	OutResult.Status = ERPGDungeonRewardSnapshotStatus::Ready;
	return true;
}
