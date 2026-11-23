#pragma once

#include "Blueprint/UserWidget.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "RPGRegionChat.generated.h"

class UEditableTextBox;
class UScrollBox;
class UTextBlock;

UCLASS()
class URPGRegionChatWidget final : public UUserWidget
{
	GENERATED_BODY()
public:
	void SetChannel(int32 Channel);
	void AppendMessage(const FString& Sender, const FString& Text);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	UFUNCTION() void CommitMessage(const FText& Text, ETextCommit::Type Method);
	UFUNCTION() void SendMessage();

	UPROPERTY(Transient) TObjectPtr<UTextBlock> Heading;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Transcript;
	UPROPERTY(Transient) TObjectPtr<UEditableTextBox> Input;
	UPROPERTY(Transient) TObjectPtr<UScrollBox> Scroll;
	TArray<FString> RecentLines;
};

UCLASS()
class ARPGRegionChatController final : public APlayerController
{
	GENERATED_BODY()
public:
	UFUNCTION(Server, Reliable)
	void ServerSendRegionMessage(const FString& Text);

	UFUNCTION(Client, Reliable)
	void ClientOpenRegionChat(int32 Channel);

	UFUNCTION(Client, Reliable)
	void ClientReceiveRegionMessage(const FString& Sender, const FString& Text);

protected:
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	UPROPERTY(Transient) TObjectPtr<URPGRegionChatWidget> ChatWidget;
	double NextSendTime = 0;
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
