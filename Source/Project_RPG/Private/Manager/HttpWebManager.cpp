#include "Manager/HttpWebManager.h"
#include "JsonObjectConverter.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "HttpModule.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "RPGDebugHelper.h"

void UHttpWebManager::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UE_LOG(LogTemp, Log, TEXT("HttpWebManager Initialized"));
}

void UHttpWebManager::Deinitialize()
{
	Super::Deinitialize();
}

void UHttpWebManager::SaveInventoryToWeb(const TArray<FItemSaveData>& InventoryData, const FString& CharacterID)
{
	// 1. JSON 객체 생성 (Root Object)
	TSharedPtr<FJsonObject> RootObject = MakeShareable(new FJsonObject);
	
	// 캐릭터 ID 추가
	RootObject->SetStringField(TEXT("characterId"), CharacterID);

	// 2. 인벤토리 배열을 JSON 배열로 변환
	TArray<TSharedPtr<FJsonValue>> InventoryJsonArray;
	for (const FItemSaveData& Item : InventoryData)
	{
		//업데이트 해주는 로직도 필요할듯. 이게 한번 저장되면 갱신이 안되는거같음

		// FItemSaveData 구조체를 JSON Object로 변환
		TSharedPtr<FJsonObject> ItemObj = MakeShareable(new FJsonObject());
		
		// 수동 변환: 서버가 기대하는 키 값(item_id, quantity 등)으로 명시적 설정
		// FName은 ToString()으로 변환
		ItemObj->SetStringField(TEXT("item_id"), Item.ItemID.ToString());
		ItemObj->SetNumberField(TEXT("quantity"), Item.Quantity);
		ItemObj->SetNumberField(TEXT("slot_index"), Item.SlotIndex);
		ItemObj->SetStringField(TEXT("category"), Item.Category);

		InventoryJsonArray.Add(MakeShareable(new FJsonValueObject(ItemObj)));
	}

	// Root Object에 배열 필드 추가
	RootObject->SetArrayField(TEXT("inventory"), InventoryJsonArray);

	// 3. JSON 객체를 문자열로 직렬화 (Stringify)
	FString RequestBody;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&RequestBody);
	FJsonSerializer::Serialize(RootObject.ToSharedRef(), Writer);

	// 4. HTTP 요청 생성 및 전송
	FHttpRequestRef Request = FHttpModule::Get().CreateRequest();
	
	// 응답을 받을 함수 연결 (수정됨: BindUObject 사용)
	Request->OnProcessRequestComplete().BindUObject(this, &UHttpWebManager::OnSaveInventoryResponseReceived);
	
	// URL 및 메서드 설정
	Request->SetURL(ApiUrl + TEXT("/saveInventory"));
	Request->SetVerb(TEXT("POST"));
	
	// 헤더 설정 (JSON을 보낸다고 명시)
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetContentAsString(RequestBody);

	// 전송 시작
	Request->ProcessRequest();

	Debug::Print(FString::Printf(TEXT("Sending Inventory Data to Web: %s"), *RequestBody), FColor::Cyan);
}

void UHttpWebManager::LoadInventoryFromWeb(const FString& CharacterID)
{
	FHttpRequestRef Request = FHttpModule::Get().CreateRequest();
	
	// 수정됨: BindUObject 사용
	Request->OnProcessRequestComplete().BindUObject(this, &UHttpWebManager::OnLoadInventoryResponseReceived);
	
	// GET 요청은 URL에 파라미터를 붙여서 보냅니다.
	FString RequestUrl = FString::Printf(TEXT("%s/loadInventory?characterId=%s"), *ApiUrl, *CharacterID);
	
	Request->SetURL(RequestUrl);
	Request->SetVerb(TEXT("GET"));
	
	Request->ProcessRequest();

	Debug::Print(FString::Printf(TEXT("Requesting Inventory Data from Web for Character: %s"), *CharacterID), FColor::Cyan);
}

void UHttpWebManager::OnSaveInventoryResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
	if (bWasSuccessful && Response.IsValid())
	{
		// HTTP 200 OK 인지 확인
		if (Response->GetResponseCode() == 200)
		{
			Debug::Print(FString::Printf(TEXT("Inventory Saved Successfully! Server Response: %s"), *Response->GetContentAsString()), FColor::Green);
			OnInventorySaved.Broadcast(true);
			return;
		}
		else
		{
			Debug::Print(FString::Printf(TEXT("Server Error: %d, Content: %s"), Response->GetResponseCode(), *Response->GetContentAsString()), FColor::Yellow);
		}
	}
	else
	{
		Debug::Print(TEXT("Failed to connect to server."), FColor::Red);
	}

	OnInventorySaved.Broadcast(false);
}

void UHttpWebManager::OnLoadInventoryResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
	TArray<FItemSaveData> LoadedData;

	if (bWasSuccessful && Response.IsValid())
	{
		if (Response->GetResponseCode() == 200)
		{
			Debug::Print(TEXT("Inventory Loaded! Parsing JSON..."), FColor::Green);
			
			// JSON 파싱
			TSharedPtr<FJsonObject> RootObject;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());

			if (FJsonSerializer::Deserialize(Reader, RootObject) && RootObject.IsValid())
			{
				const TArray<TSharedPtr<FJsonValue>>* InventoryArray;
				if (RootObject->TryGetArrayField(TEXT("inventory"), InventoryArray))
				{
					for (const TSharedPtr<FJsonValue>& Val : *InventoryArray)
					{
						TSharedPtr<FJsonObject> ItemObj = Val->AsObject();
						if (ItemObj.IsValid())
						{
							FItemSaveData ItemData;
							// JSON -> Struct 자동 변환 시도
							// 수정됨: 구조체 타입 정보(StaticStruct) 전달
							if (FJsonObjectConverter::JsonObjectToUStruct(ItemObj.ToSharedRef(), FItemSaveData::StaticStruct(), &ItemData))
							{
								LoadedData.Add(ItemData);
							}
						}
					}
				}
			}
			
			OnInventoryLoaded.Broadcast(LoadedData);
			return;
		}
	}

	Debug::Print(TEXT("Failed to load inventory from server."), FColor::Red);
	OnInventoryLoaded.Broadcast(LoadedData); // 실패 시 빈 배열 반환
}
