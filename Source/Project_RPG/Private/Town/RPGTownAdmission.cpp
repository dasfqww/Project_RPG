#include "UI/Lobby/RPGOnlineLobbyWidget.h"
#include "UI/Lobby/RPGCharacterConfigurationWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/ComboBoxString.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

FHttpRequestPtr UHttpWebManager::CreateTownAdmissionRequest(FGuid Character, TFunction<void(int32, FString, FString)> Complete, bool bEnterBellmarket)
{
	FString Endpoint, Account;
	if (!Character.IsValid() || !GetPlayerEquipmentContext(Endpoint, Account)) return {};
	const FString Authorization = PlayerAuth.Authorization(FPlatformTime::Seconds());
	if (Authorization.IsEmpty()) return {};
	auto Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(Endpoint / TEXT("characters") / Character.ToString(EGuidFormats::DigitsWithHyphensLower) /
		(bEnterBellmarket ? TEXT("town?destination=bellmarket") : TEXT("town")));
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Authorization"), Authorization);
	// A new channel can take up to 120 seconds to listen before the backend replies.
	constexpr float AdmissionTimeoutSeconds = 135.f;
	Request->SetTimeout(AdmissionTimeoutSeconds);
	Request->SetActivityTimeout(AdmissionTimeoutSeconds);
	Request->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnGameThread);
	Request->OnProcessRequestComplete().BindWeakLambda(this,
		[this, Endpoint, Account, Complete = MoveTemp(Complete)](FHttpRequestPtr Req, FHttpResponsePtr Res, bool bOk)
		{
			FString CurrentEndpoint, CurrentAccount, Address, Ticket;
			int32 Status = 0;
			if (GetPlayerEquipmentContext(CurrentEndpoint, CurrentAccount) && CurrentEndpoint == Endpoint &&
				CurrentAccount == Account && AcceptPlayerResponse(Req, Res, bOk) && bOk && Res)
			{
				Status = Res->GetResponseCode();
				TSharedPtr<FJsonObject> Json;
				if (Status == 200 && Res->GetContent().Num() <= 4096 &&
					FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Res->GetContentAsString()), Json) && Json)
				{
					Json->TryGetStringField(TEXT("address"), Address);
					Json->TryGetStringField(TEXT("ticket"), Ticket);
				}
			}
			Complete(Status, Address, Ticket);
		});
	return Request;
}

bool URPGOnlineLobbyWidget::CanEnterTown() const
{
	return bPreparationOpen && !bTutorialEntryActive && CurrentPresentation().bCanCreateSession &&
		(!ConfigurationPanel || !ConfigurationPanel->HasUnsavedChanges());
}

void URPGOnlineLobbyWidget::EnterTown()
{
	if (!CanEnterTown()) return;
	UIError.Reset();
	const FString Selected = Flow->GetSelectedCharacterId();
	FGuid Character;
	if (!FGuid::Parse(Selected, Character)) return;
	TWeakObjectPtr<URPGOnlineLobbyWidget> Weak(this);
	const auto* Destination = WidgetTree ? Cast<UComboBoxString>(WidgetTree->FindWidget(TEXT("TownDestination"))) : nullptr;
	TownAdmissionRequest = Web->CreateTownAdmissionRequest(Character,
		[Weak, Selected](int32 Status, FString Address, FString Ticket)
		{
			auto* Self = Weak.Get();
			if (!Self) return;
			Self->TownAdmissionRequest.Reset();
			FGuid Parsed;
			if (Status == 200 && Self->CanEnterTown() && Self->Flow->GetSelectedCharacterId() == Selected &&
				FGuid::ParseExact(Ticket, EGuidFormats::DigitsWithHyphens, Parsed) &&
				UHttpWebManager::ConnectWithJoinTicket(Self->GetOwningPlayer(), Address, Ticket)) return;
			Self->UIError = Status == 409 ? TEXT("이미 지역에 입장 중이거나 접속한 캐릭터입니다.") :
				TEXT("선택한 지역에 입장할 수 없습니다. 잠시 후 다시 시도해 주세요.");
			Self->RefreshPresentation();
		}, Destination && Destination->GetSelectedIndex() == 1);
	auto Request = TownAdmissionRequest;
	if (!Request || !Request->ProcessRequest())
	{
		TownAdmissionRequest.Reset();
		UIError = TEXT("지역 입장 요청을 보내지 못했습니다.");
	}
	RefreshPresentation();
}
