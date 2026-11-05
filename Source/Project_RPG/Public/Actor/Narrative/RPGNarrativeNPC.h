#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interface/InteractionInterface.h"
#include "RPGNarrativeNPC.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UTextRenderComponent;
class URPGNarrativeDialogueWidget;
class ULocalPlayer;

/** Placeable dialogue entry point. Quest authority remains in the narrative backend. */
UCLASS()
class PROJECT_RPG_API ARPGNarrativeNPC : public AActor, public IInteractionInterface
{
    GENERATED_BODY()
public:
    ARPGNarrativeNPC();
    virtual void BeginFocus() override;
    virtual void EndFocus() override;
    virtual void Interact(APlayerController* PlayerController) override;
    UFUNCTION(BlueprintCallable, Category="RPG|Narrative") bool OpenDialogue(APlayerController* PlayerController);
    UFUNCTION(BlueprintPure, Category="RPG|Narrative") bool CanInteractWith(APlayerController* PlayerController) const;
    UFUNCTION(BlueprintPure, Category="RPG|Narrative") URPGNarrativeDialogueWidget* GetOpenDialogue(APlayerController* PlayerController) const;
    bool HasEntry(const FString& DialogueId) const { return DialogueEntryIds.Contains(DialogueId); }
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="NPC") TObjectPtr<USphereComponent> InteractionBounds;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="NPC") TObjectPtr<UStaticMeshComponent> Appearance;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="NPC") TObjectPtr<UTextRenderComponent> Nameplate;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="RPG|Narrative") FText DisplayName;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="RPG|Narrative") TArray<FString> DialogueEntryIds;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="RPG|Narrative", meta=(ClampMin="50", ClampMax="1000")) float InteractionDistance = 250.f;
private:
    friend struct FRPGNarrativeTestAccess;
    bool HasValidEntries() const;
    TMap<TWeakObjectPtr<ULocalPlayer>, TWeakObjectPtr<URPGNarrativeDialogueWidget>> OpenPanels;
};
