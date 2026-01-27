// Fill out your copyright notice in the Description page of Project Settings.


#include "Component/Combat/PlayerCombatComponent.h"
#include "Item/Weapon/RPGPlayerWeapon.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "RPGGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "Attribute/RPGAttributeSet.h"
#include "Component/UI/PlayerUIComponent.h"
#include "Interface/PawnUIInterface.h"

#include "RPGDebugHelper.h"

ARPGPlayerWeapon* UPlayerCombatComponent::GetPlayerHasWeaponByTag(FGameplayTag InWeaponTag) const
{
    return Cast<ARPGPlayerWeapon>(GetCharacterHasWeaponByTag(InWeaponTag));
}

ARPGPlayerWeapon* UPlayerCombatComponent::GetPlayerCurrentEquippedWeapon() const
{
    return Cast<ARPGPlayerWeapon>(GetCharacterCurrentEquippedWeapon());
}

float UPlayerCombatComponent::GetPlayerCurrentEquipWeaponDamageAtLevel(float InLevel) const
{
    return GetPlayerCurrentEquippedWeapon()->PlayerWeaponData.WeaponBaseDamage.GetValueAtLevel(InLevel);
}

void UPlayerCombatComponent::OnHitTargetActor(AActor* HitActor)
{
    if (OverlappedActors.Contains(HitActor))
    {
        return;
    }

    OverlappedActors.AddUnique(HitActor);

    FGameplayEventData Data;
    Data.Instigator = GetOwningPawn();
    Data.Target = HitActor;

    UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(
        GetOwningPawn(),
        RPGGameplayTags::Shared_Event_Hit,
        Data
    );

    UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(
        GetOwningPawn(),
        RPGGameplayTags::Player_Event_HitPause,
        FGameplayEventData()
    );
}

void UPlayerCombatComponent::OnWeaponPulledFromTargetActor(AActor* InteractedActor)
{
    UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(
        GetOwningPawn(),
        RPGGameplayTags::Player_Event_HitPause,
        FGameplayEventData()
    );
}

void UPlayerCombatComponent::StartManaRecovery()
{
    if (!bIsManaRecoveryActive && !IsManaFull())
    {
        bIsManaRecoveryActive = true;
        GetWorld()->GetTimerManager().SetTimer(
            ManaRecoveryTimerHandle, 
            this, 
            &UPlayerCombatComponent::RecoverMana, 
            1.0f, 
            true);
    }
}

void UPlayerCombatComponent::StopManaRecovery()
{
    Debug::Print("Mana recovery Stop..");

    bIsManaRecoveryActive = false;
    GetWorld()->GetTimerManager().ClearTimer(ManaRecoveryTimerHandle);
}

void UPlayerCombatComponent::RecoverMana()
{
    if (UAbilitySystemComponent* ASC = GetOwner()->FindComponentByClass<UAbilitySystemComponent>())
    {
        const URPGAttributeSet* AttributeSet = ASC->GetSet<URPGAttributeSet>();
        if (AttributeSet)
        {
            float CurrentMana = AttributeSet->GetCurrentMana();
            float MaxMana = AttributeSet->GetMaxMana();

            float RecoveryAmount = MaxMana * ManaRecoveryRatio;  // 최대 마나의 15% 회복

            CurrentMana += RecoveryAmount;  // 회복량(예시: 10씩 회복)
            if (CurrentMana >= MaxMana)
            {
                CurrentMana = MaxMana;
                StopManaRecovery();
            }

            // 마나 값 업데이트
            ASC->ApplyModToAttribute(AttributeSet->GetCurrentManaAttribute(),
                EGameplayModOp::Override, CurrentMana);

            // Pawn UI 인터페이스를 캐스팅하여 사용
            if (IPawnUIInterface* PawnUIInterface = Cast<IPawnUIInterface>(GetOwner()))
            {
                // UI 컴포넌트에 접근
                if (UPlayerUIComponent* PlayerUIComponent = PawnUIInterface->GetPlayerUIComponent())
                {
                    // 프로그레스 바 UI를 보이도록 델리게이트 호출
                    PlayerUIComponent->OnCurrentManaChanged.Broadcast
                    (AttributeSet->GetCurrentMana() / AttributeSet->GetMaxMana());  // 프로그레스 바 표시

                    FString ManaText = FString::Printf(TEXT("%.0f/%.0f"),
                        AttributeSet->GetCurrentMana(), AttributeSet->GetMaxMana());
                    PlayerUIComponent->OnManaTextChanged.Broadcast(ManaText);
                }
            }

            Debug::Print("Mana recovering..");
        }
    }
}

bool UPlayerCombatComponent::IsManaFull() const
{
    if (UAbilitySystemComponent* ASC = GetOwner()->FindComponentByClass<UAbilitySystemComponent>())
    {
        const URPGAttributeSet* AttributeSet = ASC->GetSet<URPGAttributeSet>();
        if (AttributeSet)
        {
            return AttributeSet->GetCurrentMana() >= AttributeSet->GetMaxMana();
        }
    }
    return false;
}

void UPlayerCombatComponent::BeginPlay()
{
    Super::BeginPlay();

    if (UAbilitySystemComponent* ASC = GetOwner()->FindComponentByClass<UAbilitySystemComponent>())
    {
        // 마나 변경 감지 델리게이트 등록
        ASC->GetGameplayAttributeValueChangeDelegate(URPGAttributeSet::GetCurrentManaAttribute())
            .AddLambda([this](const FOnAttributeChangeData& Data)
                {
                    StartManaRecovery();
                });
    }
}
