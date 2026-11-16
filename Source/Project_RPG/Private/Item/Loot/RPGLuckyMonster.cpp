#include "Item/Loot/RPGLuckyMonster.h"
#include "Item/Loot/RPGLootSourceComponent.h"
ARPGLuckyMonster::ARPGLuckyMonster()
{
    HitLoot = CreateDefaultSubobject<URPGLootSourceComponent>(TEXT("HitLoot"));
    HitLoot->Trigger = ERPGLootSourceTrigger::Damage; HitLoot->MaximumEvents = 10;
}
