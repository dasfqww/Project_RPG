#include "Chat/RPGRegionChat.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerState.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace
{
	constexpr int32 MaxMessageLength = 240;

	bool IsValidMessage(const FString& Text)
	{
		if (Text.IsEmpty() || Text.Len() > MaxMessageLength) return false;
		for (const TCHAR Character : Text)
		{
			if (Character < 0x20 || (Character >= 0x7f && Character <= 0x9f)) return false;
		}
		return true;
	}
}

TSharedRef<SWidget> URPGRegionChatWidget::RebuildWidget()
{
	if (!WidgetTree->RootWidget)
	{
		auto* Width = WidgetTree->ConstructWidget<USizeBox>();
		Width->SetWidthOverride(350.f);
		auto* Root = WidgetTree->ConstructWidget<UVerticalBox>();
		Width->AddChild(Root);
		WidgetTree->RootWidget = Width;

		Heading = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("RegionChatHeading"));
		Root->AddChildToVerticalBox(Heading);

		auto* History = WidgetTree->ConstructWidget<USizeBox>();
		History->SetHeightOverride(110.f);
		Root->AddChildToVerticalBox(History);
		Scroll = WidgetTree->ConstructWidget<UScrollBox>();
		History->AddChild(Scroll);
		Transcript = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("RegionChatTranscript"));
		Transcript->SetAutoWrapText(true);
		Scroll->AddChild(Transcript);

		auto* Compose = WidgetTree->ConstructWidget<UHorizontalBox>();
		Root->AddChildToVerticalBox(Compose);
		Input = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("RegionChatInput"));
		Input->SetHintText(NSLOCTEXT("RPGRegionChat", "Compose", "지역 메시지 입력"));
		Input->SetClearKeyboardFocusOnCommit(false);
		Input->OnTextCommitted.AddDynamic(this, &ThisClass::CommitMessage);
		Compose->AddChildToHorizontalBox(Input)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		auto* Send = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("RegionChatSend"));
		auto* SendLabel = WidgetTree->ConstructWidget<UTextBlock>();
		SendLabel->SetText(NSLOCTEXT("RPGRegionChat", "Send", "전송"));
		Send->AddChild(SendLabel);
		Send->OnClicked.AddDynamic(this, &ThisClass::SendMessage);
		Compose->AddChildToHorizontalBox(Send);
	}
	return Super::RebuildWidget();
}

void URPGRegionChatWidget::SetChannel(int32 Channel)
{
	Heading->SetText(FText::Format(NSLOCTEXT("RPGRegionChat", "Title", "임시 마을 · 지역 {0}채널"), FText::AsNumber(Channel)));
}

void URPGRegionChatWidget::CommitMessage(const FText&, ETextCommit::Type Method)
{
	if (Method == ETextCommit::OnEnter) SendMessage();
}

void URPGRegionChatWidget::SendMessage()
{
	const FString Text = Input->GetText().ToString().TrimStartAndEnd();
	if (!IsValidMessage(Text)) return;
	if (auto* Controller = GetOwningPlayer<ARPGRegionChatController>())
	{
		Controller->ServerSendRegionMessage(Text);
		Input->SetText(FText::GetEmpty());
	}
}

void URPGRegionChatWidget::AppendMessage(const FString& Sender, const FString& Text)
{
	if (RecentLines.Num() >= 50) RecentLines.RemoveAt(0);
	RecentLines.Add(Sender + TEXT("  ") + Text);
	Transcript->SetText(FText::FromString(FString::Join(RecentLines, TEXT("\n"))));
	Scroll->ScrollToEnd();
}

void ARPGRegionChatController::ServerSendRegionMessage_Implementation(const FString& Text)
{
	const auto* Mode = GetWorld()->GetAuthGameMode<ARPGRegionChatGameMode>();
	const APlayerState* State = GetPlayerState<APlayerState>();
	const double Now = FPlatformTime::Seconds();
	if (!Mode || !Mode->CanUseRegionChat() || !State || Text.Len() > MaxMessageLength || Now < NextSendTime) return;
	const FString Message = Text.TrimStartAndEnd();
	if (!IsValidMessage(Message)) return;
	NextSendTime = Now + .5;
	for (auto It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (auto* Recipient = Cast<ARPGRegionChatController>(It->Get()); Recipient && Recipient->PlayerState)
		{
			Recipient->ClientReceiveRegionMessage(State->GetPlayerName(), Message);
		}
	}
}

void ARPGRegionChatController::ClientOpenRegionChat_Implementation(int32 Channel)
{
	if (!IsLocalController() || ChatWidget || Channel < 1) return;
	ChatWidget = CreateWidget<URPGRegionChatWidget>(this);
	if (!ChatWidget || !ChatWidget->AddToPlayerScreen())
	{
		ChatWidget = nullptr;
		return;
	}
	ChatWidget->SetPositionInViewport(FVector2D(16.f, -16.f), false);
	ChatWidget->SetAnchorsInViewport(FAnchors(0.f, 1.f));
	ChatWidget->SetAlignmentInViewport(FVector2D(0.f, 1.f));
	ChatWidget->SetChannel(Channel);
	SetInputMode(FInputModeGameAndUI());
	SetShowMouseCursor(true);
}

void ARPGRegionChatController::ClientReceiveRegionMessage_Implementation(const FString& Sender, const FString& Text)
{
	if (ChatWidget) ChatWidget->AppendMessage(Sender, Text);
}

void ARPGRegionChatController::EndPlay(const EEndPlayReason::Type Reason)
{
	if (ChatWidget)
	{
		ChatWidget->RemoveFromParent();
		ChatWidget = nullptr;
	}
	Super::EndPlay(Reason);
}

ARPGRegionChatGameMode::ARPGRegionChatGameMode()
{
	PlayerControllerClass = ARPGRegionChatController::StaticClass();
}

void ARPGRegionChatGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);
#if !UE_BUILD_SHIPPING
	if (GetWorld()->GetOutermost()->GetName() == TEXT("/Engine/Maps/Entry"))
	{
		FParse::Value(FCommandLine::Get(), TEXT("RPGRegionChannel="), Channel);
	}
#endif
}

bool ARPGRegionChatGameMode::CanUseRegionChat() const
{
	return GetNetMode() == NM_DedicatedServer && Channel > 0;
}

void ARPGRegionChatGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);
	auto* Controller = Cast<ARPGRegionChatController>(NewPlayer);
	auto* State = NewPlayer->GetPlayerState<APlayerState>();
	if (!CanUseRegionChat() || !Controller || !State) return;
	State->SetPlayerName(FString::Printf(TEXT("마을 주민 %d"), State->GetPlayerId()));
	Controller->ClientOpenRegionChat(Channel);
}
