#pragma once
#include "Character/RPGNonPlayerCharacter.h"
#include "RPGLuckyMonster.generated.h"
class URPGLootSourceComponent;
UCLASS(Blueprintable)
class PROJECT_RPG_API ARPGLuckyMonster : public ARPGNonPlayerCharacter
{
    GENERATED_BODY()
public:
    ARPGLuckyMonster();
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Loot") TObjectPtr<URPGLootSourceComponent> HitLoot;
};
