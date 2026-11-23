#include "Chat/RPGPlayerChatWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/InputComponent.h"
#include "Component/UI/RPGSkillWindowComponent.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Manager/HttpWebManager.h"
#include "Online/RPGDungeonSessionSubsystem.h"
#include "Online/RPGOnlineLobbySubsystem.h"
#include "Player/RPGPlayerState.h"
#include "UI/RPGWindowInputState.h"
#include "Serialization/JsonSerializer.h"
#include "TimerManager.h"

TSharedRef<SWidget> URPGPlayerChatWidget::RebuildWidget()
{
	if (!WidgetTree->RootWidget)
	{
		auto* Root = WidgetTree->ConstructWidget<UVerticalBox>();
		auto* Width = WidgetTree->ConstructWidget<USizeBox>();
		Width->SetMinDesiredWidth(280.f);
		Width->SetMaxDesiredWidth(520.f);
		Width->AddChild(Root);
		WidgetTree->RootWidget = Width;
		auto* Toggle = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("ChatToggle"));
		ToggleLabel = WidgetTree->ConstructWidget<UTextBlock>();
		Toggle->AddChild(ToggleLabel);
		Toggle->OnClicked.AddDynamic(this, &ThisClass::TogglePanel);
		Root->AddChildToVerticalBox(Toggle);
		Panel = WidgetTree->ConstructWidget<UVerticalBox>();
		Root->AddChildToVerticalBox(Panel);
		auto* ListSize = WidgetTree->ConstructWidget<USizeBox>();
		ListSize->SetHeightOverride(96);
		Panel->AddChildToVerticalBox(ListSize);
		History = WidgetTree->ConstructWidget<UScrollBox>();
		ListSize->AddChild(History);
		Transcript = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ChatTranscript"));
		Transcript->SetAutoWrapText(true);
		History->AddChild(Transcript);
		auto* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
		Panel->AddChildToVerticalBox(Row);
		Channel = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("ChatChannel"));
		ChannelLabel = WidgetTree->ConstructWidget<UTextBlock>();
		Channel->AddChild(ChannelLabel);
		Channel->OnClicked.AddDynamic(this, &ThisClass::ToggleChannel);
		Row->AddChildToHorizontalBox(Channel);
		Target = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("ChatRecipient"));
		Target->SetHintText(INVTEXT("받는 캐릭터 이름"));
		Row->AddChildToHorizontalBox(Target)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		Input = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("ChatInput"));
		Input->SetHintText(INVTEXT("메시지 입력"));
		Input->SetRevertTextOnEscape(true);
		Input->OnTextCommitted.AddDynamic(this, &ThisClass::CommitText);
		Panel->AddChildToVerticalBox(Input);
		SendButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("ChatSend"));
		auto* Label = WidgetTree->ConstructWidget<UTextBlock>();
		Label->SetText(INVTEXT("전송"));
		SendButton->AddChild(Label);
		SendButton->OnClicked.AddDynamic(this, &ThisClass::Send);
		Panel->AddChildToVerticalBox(SendButton);
		Status = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ChatStatus"));
		Panel->AddChildToVerticalBox(Status);
	}
	return Super::RebuildWidget();
}

void URPGPlayerChatWidget::NativeConstruct()
{
	Super::NativeConstruct();
	bLobby = GetWorld()->GetNetMode() == NM_Standalone;
	bWhisper = !bLobby;
	if (!bLobby && GetOwningPlayer())
	{
		SetIsFocusable(true);
		ChatKeys = NewObject<UInputComponent>(GetOwningPlayer());
		ChatKeys->RegisterComponent();
		ChatKeys->BindKey(EKeys::Enter, IE_Pressed, this, &ThisClass::OpenFromGameplay).bConsumeInput = false;
		GetOwningPlayer()->PushInputComponent(ChatKeys);
	}
	GetWorld()->GetTimerManager().SetTimer(Timer, this, &ThisClass::Poll, 2.f, true);
	Poll();
}

void URPGPlayerChatWidget::NativeDestruct()
{
	if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(Timer);
	if (ChatKeys)
	{
		if (auto* PC = GetOwningPlayer()) PC->PopInputComponent(ChatKeys);
		ChatKeys->DestroyComponent();
		ChatKeys = nullptr;
	}
	ResetChat();
	Super::NativeDestruct();
}

UHttpWebManager* URPGPlayerChatWidget::CurrentPlayer(FGuid& OutCharacter, FString& OutAccount, FString& OutEndpoint) const
{
	const auto* PC = GetOwningPlayer();
	auto* GI = GetGameInstance();
	if (!GI || !PC || !PC->IsLocalController() || PC->GetWorld() != GI->GetWorld()) return nullptr;
	if (bLobby)
	{
		const auto* Lobby = GI->GetSubsystem<URPGOnlineLobbySubsystem>();
		if (!Lobby || !Lobby->IsLobbyController(PC) || Lobby->IsCharacterSelectionController(PC)) return nullptr;
	}
	const auto* Flow = GI->GetSubsystem<URPGDungeonSessionSubsystem>();
	auto* Web = GI->GetSubsystem<UHttpWebManager>();
	if (!Flow || !Web || !Web->HasAccessToken() || !FGuid::Parse(Flow->GetSelectedCharacterId(), OutCharacter)
		|| !Web->GetPlayerEquipmentContext(OutEndpoint, OutAccount)) return nullptr;
	if (!bLobby)
	{
		const auto* State = PC->GetPlayerState<ARPGPlayerState>();
		if (!State || !State->HasAuthenticatedCharacter() || State->GetBackendCharacterId() != Flow->GetSelectedCharacterId())
			return nullptr;
	}
	return Web;
}

bool URPGPlayerChatWidget::IsCurrentPlayer() const
{
	FGuid Id;
	FString User, Url;
	return CurrentPlayer(Id, User, Url) && Id == Character && Account == User && Endpoint == Url;
}

void URPGPlayerChatWidget::ResetChat()
{
	++Generation;
	ReleaseDungeonFocus();
	for (auto* Pending : {&Reading, &Writing})
	{
		if (*Pending) { (*Pending)->OnProcessRequestComplete().Unbind(); (*Pending)->CancelRequest(); Pending->Reset(); }
	}
	Lines.Reset();
	Character.Invalidate(); Epoch.Invalidate(); RequestId.Invalidate();
	Cursor = 0;
	Account.Reset(); Endpoint.Reset(); LastBody.Reset(); LastTarget.Reset();
	bOpen = false; bReady = false; bUnread = false; bWhisper = !bLobby;
	if (Input) Input->SetText(FText::GetEmpty());
	if (Target) Target->SetText(FText::GetEmpty());
	if (Transcript) Transcript->SetText(FText::GetEmpty());
	SetStatus({});
}

void URPGPlayerChatWidget::Poll()
{
	FGuid Id;
	FString User, Url;
	auto* Web = CurrentPlayer(Id, User, Url);
	if (!Web)
	{
		if (Character.IsValid()) ResetChat();
		SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	if (Id != Character || User != Account || Url != Endpoint)
	{
		ResetChat();
		Character = Id; Account = User; Endpoint = Url;
		bOpen = bLobby;
	}
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	UpdateControls();
	if (Reading) return;
	auto Done = [WeakThis = TWeakObjectPtr<ThisClass>(this), Serial = Generation](int32 Code, FString Body)
	{
		auto* Self = WeakThis.Get();
		if (!Self || Self->Generation != Serial) return;
		Self->Reading.Reset();
		if (!Self->IsCurrentPlayer()) return;
		if (Code == 200) Self->Receive(Body);
		else
		{
			Self->bReady = false;
			Self->SetStatus(TEXT("채팅에 다시 연결하는 중입니다."));
		}
		Self->UpdateControls();
	};
	Reading = Web->CreatePlayerChatRequest(Character, bLobby, Epoch, Cursor, {}, Done);
	if (!Reading || !Reading->ProcessRequest()) Done(0, {});
}

void URPGPlayerChatWidget::Receive(const FString& Body)
{
	TSharedPtr<FJsonObject> Json;
	FString EpochString;
	FGuid NewEpoch;
	int64 Next = 0;
	const TArray<TSharedPtr<FJsonValue>>* Messages = nullptr;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Body), Json) || !Json
		|| !Json->TryGetStringField(TEXT("epoch"), EpochString) || !FGuid::Parse(EpochString, NewEpoch)
		|| !Json->TryGetNumberField(TEXT("cursor"), Next) || !Json->TryGetArrayField(TEXT("messages"), Messages)) return;
	if (!bReady) SetStatus({});
	bReady = true;
	if (Epoch != NewEpoch)
	{
		Epoch = NewEpoch;
		Lines.Reset();
		bUnread = false;
		Transcript->SetText(FText::GetEmpty());
	}
	Cursor = Next;
	for (const auto& Value : *Messages)
	{
		const TSharedPtr<FJsonObject>* Message;
		FString From, To, BodyText;
		if (!Value || !Value->TryGetObject(Message) || !(*Message)->TryGetStringField(TEXT("sender"), From)
			|| !(*Message)->TryGetStringField(TEXT("text"), BodyText)) continue;
		(*Message)->TryGetStringField(TEXT("recipient"), To);
		const FString Prefix = To.IsEmpty() ? TEXT("[일반] ") + From : FString::Printf(TEXT("[귓속말] %s → %s"), *From, *To);
		Lines.Add(Prefix + TEXT("  ") + BodyText);
		if (!bOpen) bUnread = true;
	}
	if (!Messages->IsEmpty())
	{
		if (Lines.Num() > 64) Lines.RemoveAt(0, Lines.Num() - 64);
		Transcript->SetText(FText::FromString(FString::Join(Lines, TEXT("\n"))));
		History->ScrollToEnd();
	}
}

void URPGPlayerChatWidget::TogglePanel()
{
	if (!bLobby && !bOpen)
	{
		if (!IsCurrentPlayer()) return;
		auto* PC = GetOwningPlayer();
		const auto* Skills = PC->FindComponentByClass<URPGSkillWindowComponent>();
		WindowInput = Skills ? Skills->GetWindowInputState() : nullptr;
		if (!WindowInput || WindowInput->HasOpenWindows() || !WindowInput->BeginWindow(*PC, *this, 6, true))
		{
			WindowInput.Reset();
			return;
		}
		bOpen = true;
		UpdateControls();
		WindowInput->FocusWindow(*this);
		(Target->GetText().IsEmpty() ? Target : Input)->SetUserFocus(PC);
	}
	else
	{
		bOpen = !bOpen;
		if (!bOpen) ReleaseDungeonFocus();
	}
	bUnread = false;
	UpdateControls();
}

void URPGPlayerChatWidget::OpenFromGameplay()
{
	if (!bOpen && IsCurrentPlayer() && !GetOwningPlayer()->bShowMouseCursor) TogglePanel();
}

void URPGPlayerChatWidget::ReleaseDungeonFocus()
{
	if (auto State = MoveTemp(WindowInput)) State->EndWindow(*this);
}

FReply URPGPlayerChatWidget::NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
	if (!bLobby && bOpen && Event.GetKey() == EKeys::Escape)
	{
		TogglePanel();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(Geometry, Event);
}

void URPGPlayerChatWidget::ToggleChannel()
{
	if (!bLobby || Writing) return;
	bWhisper = !bWhisper;
	SetStatus({});
	UpdateControls();
}

void URPGPlayerChatWidget::UpdateControls()
{
	Panel->SetVisibility(bOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	ToggleLabel->SetText(FText::FromString(FString(bLobby ? TEXT("로비 채팅") : TEXT("귓속말")) + (bUnread ? TEXT(" · 새 메시지") : TEXT(""))));
	Target->SetVisibility(bWhisper ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	ChannelLabel->SetText(bWhisper ? INVTEXT("귓속말") : INVTEXT("일반"));
	Channel->SetIsEnabled(bLobby && !Writing);
	SendButton->SetIsEnabled(bReady && !Writing);
	Target->SetIsReadOnly(Writing.IsValid());
	Input->SetIsReadOnly(Writing.IsValid());
}

void URPGPlayerChatWidget::SetStatus(const FString& Message)
{
	if (!Status) return;
	Status->SetText(FText::FromString(Message));
	Status->SetVisibility(Message.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
}

void URPGPlayerChatWidget::CommitText(const FText&, ETextCommit::Type Method)
{
	if (Method == ETextCommit::OnEnter) Send();
	else if (!bLobby && bOpen && Method == ETextCommit::OnCleared) TogglePanel();
}

void URPGPlayerChatWidget::Send()
{
	if (!bReady || Writing || !IsCurrentPlayer()) return;
	const FString Text = Input->GetText().ToString().TrimStartAndEnd();
	const FString Name = bWhisper ? Target->GetText().ToString().TrimStartAndEnd() : FString();
	if (Text.IsEmpty() || Text.Len() > 240) { SetStatus(TEXT("메시지는 1~240자로 입력하세요.")); return; }
	if (bWhisper && Name.IsEmpty()) { SetStatus(TEXT("받는 캐릭터 이름을 입력하세요.")); return; }
	if (!RequestId.IsValid() || !Text.Equals(LastBody, ESearchCase::CaseSensitive)
		|| !Name.Equals(LastTarget, ESearchCase::CaseSensitive)) RequestId = FGuid::NewGuid();
	LastBody = Text; LastTarget = Name;
	auto Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("requestId"), RequestId.ToString(EGuidFormats::DigitsWithHyphensLower));
	Json->SetStringField(TEXT("channel"), bWhisper ? TEXT("whisper") : TEXT("lobby"));
	Json->SetStringField(TEXT("recipient"), Name);
	Json->SetStringField(TEXT("text"), Text);
	FString Body;
	FJsonSerializer::Serialize(Json, TJsonWriterFactory<>::Create(&Body));
	auto Done = [WeakThis = TWeakObjectPtr<ThisClass>(this), Serial = Generation](int32 Code, FString)
	{
		auto* Self = WeakThis.Get();
		if (!Self || Self->Generation != Serial) return;
		Self->Writing.Reset();
		if (!Self->IsCurrentPlayer()) return;
		if (Code == 204)
		{
			Self->Input->SetText(FText::GetEmpty());
			Self->RequestId.Invalidate();
			Self->SetStatus({});
			Self->Poll();
		}
		else if (Code == 404) Self->SetStatus(TEXT("받는 캐릭터의 이름과 접속 상태를 확인하세요."));
		else if (Code == 429) Self->SetStatus(TEXT("잠시 후 다시 보내세요."));
		else Self->SetStatus(TEXT("전송을 확인할 수 없습니다. 다시 보내세요."));
		Self->UpdateControls();
	};
	auto* Web = GetGameInstance()->GetSubsystem<UHttpWebManager>();
	Writing = Web->CreatePlayerChatRequest(Character, bLobby, Epoch, Cursor, Body, Done);
	UpdateControls();
	if (!Writing || !Writing->ProcessRequest()) Done(0, {});
}
