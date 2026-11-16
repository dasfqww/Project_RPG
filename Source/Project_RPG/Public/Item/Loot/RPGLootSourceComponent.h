#pragma once
#include "Components/ActorComponent.h"
#include "RPGLootSourceComponent.generated.h"
class APlayerController;
class UDataTable;
struct FGameplayEffectModCallbackData;

UENUM(BlueprintType)
enum class ERPGLootSourceTrigger : uint8 { Open, Damage };

/** Server-only event budget, shared by all members. Never reset on healing or Blueprint callbacks. */
UCLASS(ClassGroup=(RPG), meta=(BlueprintSpawnableComponent))
class PROJECT_RPG_API URPGLootSourceComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    URPGLootSourceComponent();
    virtual void OnRegister() override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    bool TryOpen(APlayerController* Controller, const FGuid& ExpectedId);
    void NotifyDamage(const FGameplayEffectModCallbackData& Data, float PreviousHealth, float CurrentHealth);
    FGuid GetSourceId() const { return SourceId; }
    UFUNCTION(BlueprintPure, Category="Loot") int32 GetUsedEvents() const { return UsedEvents; }
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Loot") ERPGLootSourceTrigger Trigger = ERPGLootSourceTrigger::Open;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Loot") TObjectPtr<UDataTable> DropTable;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Loot") FName DropRow;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Loot", meta=(ClampMin="1", ClampMax="100")) int32 MaximumEvents = 1;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Loot", meta=(ClampMin="0.05", ClampMax="60")) float DamageInterval = .15f;
private:
    bool Emit();
    UPROPERTY(Replicated) FGuid SourceId;
    UPROPERTY(Replicated) int32 UsedEvents = 0;
    double NextDamageTime = 0;
};
