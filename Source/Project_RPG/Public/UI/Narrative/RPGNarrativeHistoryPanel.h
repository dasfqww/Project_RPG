#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Narrative/RPGNarrativeHistory.h"
#include "RPGNarrativeHistoryPanel.generated.h"

class UButton;
class UTextBlock;

/** Read-only account completion panel. No skip actions or journal mutations. */
UCLASS()
class PROJECT_RPG_API URPGNarrativeHistoryPanel final : public UUserWidget
{
    GENERATED_BODY()
public:
    void Configure(TSharedPtr<FRPGNarrativeHistoryClient> InClient, FGuid InCharacter);
    FText GetDisplayedHistory() const;
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeTick(const FGeometry& Geometry, float DeltaTime) override;
    virtual void NativeDestruct() override;
private:
    friend struct FRPGNarrativeHistoryUiTestAccess;
    UFUNCTION() void RefreshHistory();
    void Render();
    void Unbind();
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Summary;
    UPROPERTY(Transient) TObjectPtr<UButton> RefreshButton;
    TSharedPtr<FRPGNarrativeHistoryClient> Client;
    FDelegateHandle Changed;
    FGuid Character;
    ERPGNarrativeHistoryState LastState = ERPGNarrativeHistoryState::Idle;
    bool bLastSelected = false;
};
