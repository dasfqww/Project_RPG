#pragma once
#include "GameFramework/Actor.h"
#include "Interface/InteractionInterface.h"
#include "RPGTreasureChest.generated.h"
class URPGLootSourceComponent;
class UStaticMeshComponent;
UCLASS(Blueprintable)
class PROJECT_RPG_API ARPGTreasureChest : public AActor, public IInteractionInterface
{
    GENERATED_BODY()
public:
    ARPGTreasureChest();
    virtual void Interact(APlayerController* Controller) override;
    virtual bool CanOfferInteraction() const override;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Loot") TObjectPtr<URPGLootSourceComponent> LootSource;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Loot") TObjectPtr<UStaticMeshComponent> ChestMesh;
};
