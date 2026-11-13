#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Narrative/RPGNarrativeNPCMarker.h"
#include "RPGNarrativeNPCMarkerWidget.generated.h"
class UTextBlock;

/** Per-local-player screen widget; provider exposes only that player's readable dialogue cache. */
UCLASS()
class PROJECT_RPG_API URPGNarrativeNPCMarkerWidget final : public UUserWidget
{
    GENERATED_BODY()
public:
    using FProvider = TFunction<bool(TArray<FString>&, TArray<FRPGDialogueNode>&)>;
    void Configure(FProvider InProvider);
    FText GetDisplayedMarker();
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeTick(const FGeometry& Geometry, float DeltaTime) override;
    virtual void NativeDestruct() override;
private:
    void Render();
    FProvider Provider;
    float Elapsed = 0.f;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Caption;
};
