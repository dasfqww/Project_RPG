#pragma once
#include "Components/ActorComponent.h"
#include "RPGLootInteractionComponent.generated.h"
/** RPC lives on the owning controller, never on an unowned chest or shared pickup. */
UCLASS()
class PROJECT_RPG_API URPGLootInteractionComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    URPGLootInteractionComponent();
    void Request(AActor& Target, const FGuid& Identity);
private:
    UFUNCTION(Server, Reliable) void ServerInteract(AActor* Target, FGuid Identity);
    double NextRequest = 0;
};
