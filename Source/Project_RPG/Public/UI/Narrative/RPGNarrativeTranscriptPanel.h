#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Narrative/RPGNarrativeTranscript.h"
#include "RPGNarrativeTranscriptPanel.generated.h"
class UButton;
class UTextBlock;
class USizeBox;

/** Explicit read-only review of text captured by the owning story window. */
UCLASS()
class PROJECT_RPG_API URPGNarrativeTranscriptPanel final : public UUserWidget
{
    GENERATED_BODY()
public:
    void Configure(TSharedPtr<FRPGNarrativeTranscript> InModel, FGuid InCharacter);
    FText GetDisplayedTranscript();
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeTick(const FGeometry& Geometry, float DeltaTime) override;
    virtual void NativeDestruct() override;
private:
    friend struct FRPGNarrativeTranscriptTestAccess;
    UFUNCTION() void OpenLog();
    UFUNCTION() void CloseLog();
    UFUNCTION() void ClearLog();
    void Render();
    bool bOpen = false;
    TSharedPtr<FRPGNarrativeTranscript> Model;
    FGuid Character;
    UPROPERTY(Transient) TObjectPtr<USizeBox> LogBox;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> LogText;
    UPROPERTY(Transient) TObjectPtr<UButton> ClearButton;
};
