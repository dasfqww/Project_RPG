#pragma once

#include "Blueprint/UserWidget.h"
#include "GameFramework/Character.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "Chat/RPGTownChatRateLimiter.h"
#include "RPGRegionChat.generated.h"

class UEditableTextBox;
class UScrollBox;
class UTextBlock;
class STextBlock;

UENUM()
enum class ERPGTownChatChannel : uint8
{
	Region,
	Nearby
};

UENUM()
enum class ERPGTownChatSendResult : uint8
{
	Accepted,
	Invalid,
	Duplicate,
	RateLimited
};

UCLASS()
class ARPGRegionChatPawn final : public ACharacter
{
	GENERATED_BODY()
public:
	ARPGRegionChatPawn();
};

UCLASS()
class URPGRegionChatWidget final : public UUserWidget
{
	GENERATED_BODY()
public:
	void SetChannel(int32 Channel);
	void CompleteSend(ERPGTownChatSendResult Result, int32 RetryAfterSeconds);
	void AppendMessage(ERPGTownChatChannel Channel, const FString& Sender, const FString& Text);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	UFUNCTION() void CommitMessage(const FText& Text, ETextCommit::Type Method);
	UFUNCTION() void SendMessage();
	UFUNCTION() void ToggleChannel();
	void RefreshChannelLabel();
	void SetStatus(const FText& Message);

	UPROPERTY(Transient) TObjectPtr<UTextBlock> Heading;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ChannelLabel;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Transcript;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Status;
	UPROPERTY(Transient) TObjectPtr<UEditableTextBox> Input;
	UPROPERTY(Transient) TObjectPtr<UScrollBox> Scroll;
	TArray<FString> RecentLines;
	ERPGTownChatChannel SendChannel = ERPGTownChatChannel::Region;
	bool bSendPending = false;
};

UCLASS()
class URPGRegionChatBubble final : public UUserWidget
{
	GENERATED_BODY()
public:
	void ShowMessage(const FString& Text, double Now);
	bool UpdatePosition(int32 SpeakerId, double Now);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

private:
	FText Message;
	TSharedPtr<STextBlock> MessageBlock;
	TWeakObjectPtr<ARPGRegionChatPawn> Speaker;
	double ExpiresAt = 0.0;
};

UCLASS()
class ARPGRegionChatController final : public APlayerController
{
	GENERATED_BODY()
public:
	UFUNCTION(Server, Reliable)
	void ServerSendRegionMessage(ERPGTownChatChannel Channel, const FString& Text);

	UFUNCTION(Client, Reliable)
	void ClientCompleteRegionSend(ERPGTownChatSendResult Result, int32 RetryAfterSeconds);

	UFUNCTION(Client, Reliable)
	void ClientOpenRegionChat(int32 Channel);

	UFUNCTION(Client, Reliable)
	void ClientReceiveRegionMessage(ERPGTownChatChannel Channel, int32 SpeakerId, const FString& Sender, const FString& Text);

protected:
	virtual void PlayerTick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	UPROPERTY(Transient) TObjectPtr<URPGRegionChatWidget> ChatWidget;
	UPROPERTY(Transient) TMap<int32, TObjectPtr<URPGRegionChatBubble>> ChatBubbles;
	FRPGTownChatRateLimiter RateLimiter;
};

/** Opt-in dedicated development map. Each server process hosts one region channel. */
UCLASS()
class ARPGRegionChatGameMode final : public AGameModeBase
{
	GENERATED_BODY()
public:
	ARPGRegionChatGameMode();
	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	bool CanUseRegionChat() const;

private:
	int32 Channel = 0;
};
