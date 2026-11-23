#include "Chat/RPGRegionChat.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Camera/CameraComponent.h"
#include "Components/Button.h"
#include "Components/CapsuleComponent.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Styling/CoreStyle.h"
#include "UObject/Package.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#if UE_ENABLE_ICU
THIRD_PARTY_INCLUDES_START
#include <unicode/normalizer2.h>
#include <unicode/uchar.h>
THIRD_PARTY_INCLUDES_END
#endif

namespace
{
	constexpr int32 MaxMessageLength = 240;
	constexpr double NearbyRadiusCm = 2000.0;
	constexpr double BubbleDurationSeconds = 4.0;

	FText ChannelName(ERPGTownChatChannel Channel)
	{
		return Channel == ERPGTownChatChannel::Nearby
			? NSLOCTEXT("RPGRegionChat", "NearbyChannel", "일반")
			: NSLOCTEXT("RPGRegionChat", "RegionChannel", "지역");
	}

	bool IsValidMessage(const FString& Text)
	{
		if (Text.IsEmpty() || Text.Len() > MaxMessageLength) return false;
		for (const TCHAR Character : Text)
		{
			if (Character < 0x20 || (Character >= 0x7f && Character <= 0x9f)) return false;
		}
		return true;
	}

	std::u16string NormalizeChatComparison(const FString& Message)
	{
#if UE_ENABLE_ICU
		const auto Input = StringCast<UTF16CHAR>(*Message, Message.Len());
		const icu::UnicodeString Source(reinterpret_cast<const UChar*>(Input.Get()), Input.Length());
		icu::UnicodeString Folded;
		bool bPendingSpace = false;
		for (int32 Index = 0; Index < Source.length();)
		{
			const UChar32 CodePoint = Source.char32At(Index);
			Index += U16_LENGTH(CodePoint);
			if (u_isUWhiteSpace(CodePoint))
			{
				bPendingSpace = !Folded.isEmpty();
				continue;
			}
			if (bPendingSpace)
			{
				Folded.append(static_cast<UChar32>(' '));
				bPendingSpace = false;
			}
			Folded.append(u_foldCase(CodePoint, U_FOLD_CASE_DEFAULT));
		}
		UErrorCode Error = U_ZERO_ERROR;
		const auto* Nfc = icu::Normalizer2::getNFCInstance(Error);
		icu::UnicodeString Normalized;
		if (U_SUCCESS(Error) && Nfc != nullptr)
		{
			Nfc->normalize(Folded, Normalized, Error);
		}
		const icu::UnicodeString& Result = U_SUCCESS(Error) && Nfc != nullptr ? Normalized : Folded;
		if (Result.isEmpty()) return {};
		return std::u16string(reinterpret_cast<const char16_t*>(Result.getBuffer()), Result.length());
#else
		FString Folded;
		bool bPendingSpace = false;
		for (const TCHAR CodeUnit : Message)
		{
			if (FChar::IsWhitespace(CodeUnit))
			{
				bPendingSpace = !Folded.IsEmpty();
				continue;
			}
			if (bPendingSpace)
			{
				Folded.AppendChar(TEXT(' '));
				bPendingSpace = false;
			}
			Folded.AppendChar(FChar::ToLower(CodeUnit));
		}
		const auto Input = StringCast<UTF16CHAR>(*Folded, Folded.Len());
		return std::u16string(reinterpret_cast<const char16_t*>(Input.Get()), Input.Length());
#endif
	}
}

ARPGRegionChatPawn::ARPGRegionChatPawn()
{
	// The temporary Entry map has no floor. Character movement replicates the
	// horizontal test movement to the server while keeping players at spawn height.
	GetCharacterMovement()->DefaultLandMovementMode = MOVE_Flying;
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);

	// Keep the local speaker's overhead UI inside the temporary town camera.
	auto* Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("TownCamera"));
	Camera->SetupAttachment(GetCapsuleComponent());
	Camera->SetRelativeLocation(FVector(-360.0, 0.0, 165.0));
	Camera->SetRelativeRotation(FRotator(-18.0, 0.0, 0.0));
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
		auto* ChannelButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("RegionChatChannel"));
		ChannelLabel = WidgetTree->ConstructWidget<UTextBlock>();
		ChannelButton->AddChild(ChannelLabel);
		ChannelButton->OnClicked.AddDynamic(this, &ThisClass::ToggleChannel);
		Compose->AddChildToHorizontalBox(ChannelButton);
		Input = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("RegionChatInput"));
		Input->SetHintText(NSLOCTEXT("RPGRegionChat", "ComposeMessage", "메시지 입력"));
		Input->SetClearKeyboardFocusOnCommit(false);
		Input->OnTextCommitted.AddDynamic(this, &ThisClass::CommitMessage);
		Compose->AddChildToHorizontalBox(Input)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		auto* Send = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("RegionChatSend"));
		auto* SendLabel = WidgetTree->ConstructWidget<UTextBlock>();
		SendLabel->SetText(NSLOCTEXT("RPGRegionChat", "Send", "전송"));
		Send->AddChild(SendLabel);
		Send->OnClicked.AddDynamic(this, &ThisClass::SendMessage);
		Compose->AddChildToHorizontalBox(Send);
		Status = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("RegionChatStatus"));
		Status->SetAutoWrapText(true);
		Root->AddChildToVerticalBox(Status);
		SetStatus(FText::GetEmpty());
		RefreshChannelLabel();
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

void URPGRegionChatWidget::ToggleChannel()
{
	if (bSendPending) return;
	SendChannel = SendChannel == ERPGTownChatChannel::Region ? ERPGTownChatChannel::Nearby : ERPGTownChatChannel::Region;
	RefreshChannelLabel();
	SetStatus(FText::GetEmpty());
}

void URPGRegionChatWidget::RefreshChannelLabel()
{
	ChannelLabel->SetText(ChannelName(SendChannel));
}

void URPGRegionChatWidget::SendMessage()
{
	if (bSendPending) return;
	const FString Text = Input->GetText().ToString().TrimStartAndEnd();
	if (!IsValidMessage(Text)) return;
	if (auto* Controller = GetOwningPlayer<ARPGRegionChatController>())
	{
		bSendPending = true;
		Input->SetIsReadOnly(true);
		Controller->ServerSendRegionMessage(SendChannel, Text);
	}
}

void URPGRegionChatWidget::CompleteSend(ERPGTownChatSendResult Result, int32 RetryAfterSeconds)
{
	if (!bSendPending) return;
	bSendPending = false;
	Input->SetIsReadOnly(false);
	switch (Result)
	{
	case ERPGTownChatSendResult::Accepted:
		Input->SetText(FText::GetEmpty());
		SetStatus(FText::GetEmpty());
		break;
	case ERPGTownChatSendResult::Duplicate:
		SetStatus(FText::Format(NSLOCTEXT("RPGRegionChat", "DuplicateWait", "같은 메시지는 {0}초 후 다시 보낼 수 있어요."),
			FText::AsNumber(RetryAfterSeconds)));
		break;
	case ERPGTownChatSendResult::RateLimited:
		SetStatus(FText::Format(NSLOCTEXT("RPGRegionChat", "RateWait", "메시지를 너무 빠르게 보내고 있어요. {0}초 후 다시 시도해 주세요."),
			FText::AsNumber(RetryAfterSeconds)));
		break;
	default:
		SetStatus(NSLOCTEXT("RPGRegionChat", "InvalidSend", "메시지를 보낼 수 없어요. 잠시 후 다시 시도해 주세요."));
		break;
	}
}

void URPGRegionChatWidget::SetStatus(const FText& Message)
{
	Status->SetText(Message);
	Status->SetVisibility(Message.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
}

void URPGRegionChatWidget::AppendMessage(ERPGTownChatChannel Channel, const FString& Sender, const FString& Text)
{
	if (RecentLines.Num() >= 50) RecentLines.RemoveAt(0);
	RecentLines.Add(FString::Printf(TEXT("[%s] %s  %s"), *ChannelName(Channel).ToString(), *Sender, *Text));
	Transcript->SetText(FText::FromString(FString::Join(RecentLines, TEXT("\n"))));
	Scroll->ScrollToEnd();
}

TSharedRef<SWidget> URPGRegionChatBubble::RebuildWidget()
{
	static const FLinearColor Fill(0.025f, 0.030f, 0.042f, 0.96f);
	static const FLinearColor Edge(0.54f, 0.44f, 0.29f, 0.9f);
	static const FSlateRoundedBoxBrush Frame(Fill, 8.0f, Edge, 1.0f);
	static const FSlateRoundedBoxBrush Tail(Fill, 1.0f, Edge, 1.0f, FVector2D(9.0, 9.0));

	return SNew(SOverlay)
		+ SOverlay::Slot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Bottom)
		.Padding(FMargin(0.0f, 0.0f, 0.0f, 2.0f))
		[
			SNew(SImage)
			.Image(&Tail)
			.RenderTransform(FSlateRenderTransform(FQuat2D(PI / 4.0f)))
			.RenderTransformPivot(FVector2D(0.5, 0.5))
		]
		+ SOverlay::Slot()
		.Padding(FMargin(0.0f, 0.0f, 0.0f, 9.0f))
		[
			SNew(SBorder)
			.BorderImage(&Frame)
			.Padding(FMargin(12.0f, 7.0f))
			[
				SAssignNew(MessageBlock, STextBlock)
				.Text(Message)
				.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 14))
				.ColorAndOpacity(FLinearColor(1.0f, 0.97f, 0.88f))
				.WrapTextAt(236.0f)
				.WrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping)
			]
		];
}

void URPGRegionChatBubble::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	MessageBlock.Reset();
}

void URPGRegionChatBubble::ShowMessage(const FString& Text, double Now)
{
	Message = FText::FromString(Text);
	ExpiresAt = Now + BubbleDurationSeconds;
	if (MessageBlock) MessageBlock->SetText(Message);
}

bool URPGRegionChatBubble::UpdatePosition(int32 SpeakerId, double Now)
{
	if (Now >= ExpiresAt || Speaker.IsStale()) return false;

	if (!Speaker.IsValid())
	{
		// The chat RPC can arrive before the speaker's pawn is replicated.
		if (const auto* State = GetWorld()->GetGameState())
		{
			for (const APlayerState* Player : State->PlayerArray)
			{
				if (IsValid(Player) && Player->GetPlayerId() == SpeakerId)
				{
					Speaker = Cast<ARPGRegionChatPawn>(Player->GetPawn());
					break;
				}
			}
		}
	}

	FVector2D ScreenPosition;
	if (const auto* Pawn = Speaker.Get())
	{
		const double Height = Pawn->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 20.0;
		const FVector Anchor = Pawn->GetActorLocation() + FVector(0.0, 0.0, Height);
		if (UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(GetOwningPlayer(), Anchor, ScreenPosition, true))
		{
			SetPositionInViewport(ScreenPosition, false);
			SetVisibility(ESlateVisibility::HitTestInvisible);
			return true;
		}
	}

	SetVisibility(ESlateVisibility::Collapsed);
	return true;
}

void ARPGRegionChatController::PlayerTick(float DeltaSeconds)
{
	Super::PlayerTick(DeltaSeconds);
	if (!IsLocalController() || !ChatWidget) return;
	if (APawn* ControlledPawn = GetPawn())
	{
		const int32 Forward = static_cast<int32>(IsInputKeyDown(EKeys::W)) - static_cast<int32>(IsInputKeyDown(EKeys::S));
		const int32 Right = static_cast<int32>(IsInputKeyDown(EKeys::D)) - static_cast<int32>(IsInputKeyDown(EKeys::A));
		ControlledPawn->AddMovementInput(FVector::ForwardVector, Forward);
		ControlledPawn->AddMovementInput(FVector::RightVector, Right);
	}

	const double Now = FPlatformTime::Seconds();
	for (auto It = ChatBubbles.CreateIterator(); It; ++It)
	{
		if (!It.Value()->UpdatePosition(It.Key(), Now))
		{
			It.Value()->RemoveFromParent();
			It.RemoveCurrent();
		}
	}
}

void ARPGRegionChatController::ServerSendRegionMessage_Implementation(ERPGTownChatChannel Channel, const FString& Text)
{
	const auto* Mode = GetWorld()->GetAuthGameMode<ARPGRegionChatGameMode>();
	const APlayerState* State = GetPlayerState<APlayerState>();
	const bool bNearby = Channel == ERPGTownChatChannel::Nearby;
	const APawn* SenderPawn = bNearby ? GetPawn() : nullptr;
	if (!Mode || !Mode->CanUseRegionChat() || !State || Text.Len() > MaxMessageLength
		|| (!bNearby && Channel != ERPGTownChatChannel::Region) || (bNearby && !IsValid(SenderPawn)))
	{
		ClientCompleteRegionSend(ERPGTownChatSendResult::Invalid, 0);
		return;
	}
	const FString Message = Text.TrimStartAndEnd();
	if (!IsValidMessage(Message))
	{
		ClientCompleteRegionSend(ERPGTownChatSendResult::Invalid, 0);
		return;
	}
	const auto RateChannel = bNearby ? ERPGTownChatRateChannel::Nearby : ERPGTownChatRateChannel::Region;
	const auto Comparison = NormalizeChatComparison(Message);
	if (Comparison.empty())
	{
		ClientCompleteRegionSend(ERPGTownChatSendResult::Invalid, 0);
		return;
	}
	const auto Decision = RateLimiter.TryAccept(RateChannel, Comparison, FPlatformTime::Seconds());
	if (Decision.Reason != ERPGTownChatRateReason::Accepted)
	{
		const auto Result = Decision.Reason == ERPGTownChatRateReason::Duplicate
			? ERPGTownChatSendResult::Duplicate : ERPGTownChatSendResult::RateLimited;
		ClientCompleteRegionSend(Result, FMath::CeilToInt(Decision.RetryAfterSeconds));
		return;
	}
	for (auto It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (auto* Recipient = Cast<ARPGRegionChatController>(It->Get()); Recipient && Recipient->PlayerState)
		{
			if (bNearby)
			{
				const APawn* RecipientPawn = Recipient->GetPawn();
				if (!IsValid(RecipientPawn) || FVector::DistSquared(SenderPawn->GetActorLocation(),
					RecipientPawn->GetActorLocation()) > FMath::Square(NearbyRadiusCm)) continue;
			}
			Recipient->ClientReceiveRegionMessage(Channel, State->GetPlayerId(), State->GetPlayerName(), Message);
		}
	}
	ClientCompleteRegionSend(ERPGTownChatSendResult::Accepted, 0);
}

void ARPGRegionChatController::ClientCompleteRegionSend_Implementation(ERPGTownChatSendResult Result, int32 RetryAfterSeconds)
{
	if (ChatWidget) ChatWidget->CompleteSend(Result, RetryAfterSeconds);
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

void ARPGRegionChatController::ClientReceiveRegionMessage_Implementation(
	ERPGTownChatChannel Channel, int32 SpeakerId, const FString& Sender, const FString& Text)
{
	if (!ChatWidget) return;
	ChatWidget->AppendMessage(Channel, Sender, Text);
	if (Channel != ERPGTownChatChannel::Nearby) return;

	auto* Bubble = ChatBubbles.FindRef(SpeakerId).Get();
	if (!Bubble)
	{
		Bubble = CreateWidget<URPGRegionChatBubble>(this);
		if (!Bubble) return;
		Bubble->SetVisibility(ESlateVisibility::Collapsed);
		if (!Bubble->AddToPlayerScreen(-1)) return;
		Bubble->SetAlignmentInViewport(FVector2D(0.5, 1.0));
		ChatBubbles.Add(SpeakerId, Bubble);
	}
	Bubble->ShowMessage(Text, FPlatformTime::Seconds());
}

void ARPGRegionChatController::EndPlay(const EEndPlayReason::Type Reason)
{
	for (const auto& Pair : ChatBubbles)
	{
		Pair.Value->RemoveFromParent();
	}
	ChatBubbles.Reset();

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
	DefaultPawnClass = ARPGRegionChatPawn::StaticClass();
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
