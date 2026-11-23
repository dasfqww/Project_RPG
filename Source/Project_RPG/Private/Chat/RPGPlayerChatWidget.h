#pragma once

#include "Blueprint/UserWidget.h"
#include "HttpFwd.h"
#include "RPGPlayerChatWidget.generated.h"

class UButton;
class UEditableTextBox;
class UTextBlock;
class UVerticalBox;
class UScrollBox;
class UHttpWebManager;
class UInputComponent;
class FRPGWindowInputState;

UCLASS()
class URPGPlayerChatWidget final : public UUserWidget
{
	GENERATED_BODY()
protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual FReply NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
private:
	UFUNCTION() void TogglePanel();
	UFUNCTION() void ToggleChannel();
	UFUNCTION() void Send();
	UFUNCTION() void CommitText(const FText& Text, ETextCommit::Type Method);
	void UpdateControls();
	void OpenFromGameplay();
	void ReleaseDungeonFocus();
	void Poll();
	void ResetChat();
	void Receive(const FString& Body);
	UHttpWebManager* CurrentPlayer(FGuid& OutCharacter, FString& OutAccount, FString& OutEndpoint) const;
	bool IsCurrentPlayer() const;
	void SetStatus(const FString& Message);

	UPROPERTY(Transient) TObjectPtr<UTextBlock> ToggleLabel;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ChannelLabel;
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> Panel;
	UPROPERTY(Transient) TObjectPtr<UScrollBox> History;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Transcript;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Status;
	UPROPERTY(Transient) TObjectPtr<UButton> Channel;
	UPROPERTY(Transient) TObjectPtr<UButton> SendButton;
	UPROPERTY(Transient) TObjectPtr<UEditableTextBox> Target;
	UPROPERTY(Transient) TObjectPtr<UEditableTextBox> Input;
	UPROPERTY(Transient) TObjectPtr<UInputComponent> ChatKeys;
	TSharedPtr<FRPGWindowInputState> WindowInput;
	TArray<FString> Lines;
	FTimerHandle Timer;
	FHttpRequestPtr Reading, Writing;
	FString Account, Endpoint, LastBody, LastTarget;
	FGuid Character, Epoch, RequestId;
	int64 Cursor = 0;
	uint64 Generation = 0;
	bool bLobby = false;
	bool bWhisper = false;
	bool bOpen = false;
	bool bReady = false;
	bool bUnread = false;
};
