// Fill out your copyright notice in the Description page of Project Settings.


#include "Item/RPGItemBase.h"
#include "Component/RPGInventoryComponent.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "GameplayEffect.h"
#include "GameplayEffectTypes.h"
#include "Attribute/RPGAttributeSet.h"
#include "Character/RPGPlayer.h"
#include "Net/UnrealNetwork.h"
#include "Item/Fragment/RPGItemFragment.h"

#include "RPGDebugHelper.h"

URPGItemBase::URPGItemBase() 
{
	
}



//URPGItemBase* URPGItemBase::CreateItemCopy() const
//{
//	URPGItemBase* ItemCopy = NewObject<URPGItemBase>(StaticClass());
//
//	ItemCopy->ID = this->ID;
//	ItemCopy->Quantity = this->Quantity;
//	ItemCopy->ItemGrade = this->ItemGrade;
//	ItemCopy->ItemType = this->ItemType;
//	ItemCopy->TextData = this->TextData;
//	ItemCopy->NumericData = this->NumericData;
//	ItemCopy->Statistics = this->Statistics;
//	ItemCopy->AssetData = this->AssetData;
//	ItemCopy->bIsPickup = true;
//
//	return ItemCopy;
//}

//void URPGItemBase::SetQuantity(const int32 NewQuantity)
//{
//	if (NewQuantity!=this->Quantity)
//	{
//		Quantity = FMath::Clamp(NewQuantity, 0, this->NumericData.bIsStackable ? this->NumericData.MaxStackSize : 1);
//
//		if (this->OwningInventory)
//		{
//			OwningInventory->OnInventoryUpdated.Broadcast();
//
//			if (this->Quantity<=0)
//			{
//				this->OwningInventory->RemovingSingleInstanceOfItem(this);
//			}
//			
//		}
//
//		else
//		{
//			UE_LOG(LogTemp, Warning, TEXT("ItemBase OwningInventory was null (item may be a pickup)."));
//		}
//	}
//}


void URPGItemBase::ApplyConsumableEffect(ARPGPlayer* Player)
{
	//if (!Player) return;

	//// Ability System Component ��������
	//UAbilitySystemComponent* ASC = Player->GetAbilitySystemComponent();
	//if (!ASC) return;

	//// Attribute Set ��������
	//const URPGAttributeSet* AttributeSet = Cast<URPGAttributeSet>(ASC->GetAttributeSet(URPGAttributeSet::StaticClass()));
	//if (!AttributeSet) return;

	//// ü�� ȸ���� ����
	////float HealAmount = Statistics.RestorationAmount;
	////float NewHealth = FMath::Clamp(AttributeSet->GetCurrentHealth() + HealAmount, 0.f, AttributeSet->GetMaxHealth());

	//// ü�� ���� ����
	//ASC->ApplyModToAttribute(AttributeSet->GetCurrentHealthAttribute(),
	//	EGameplayModOp::Additive, NewHealth);

	//Debug::Print("RestoreHealth : ", HealAmount);
}

void URPGItemBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	UObject::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ThisClass, ItemManifest);
	DOREPLIFETIME(ThisClass, TotalQuantity);
	DOREPLIFETIME(ThisClass, SlotIndex);
}

void URPGItemBase::SetItemManifest(const FItemManifest& Manifest)
{
	ItemManifest = FInstancedStruct::Make<FItemManifest>(Manifest);
}

bool URPGItemBase::IsStackable() const
{
	const FStackableFragment* Stackable = GetItemManifest().GetFragmentOfType<FStackableFragment>();
	return Stackable!=nullptr;
}

bool URPGItemBase::IsConsumable() const
{
	return GetItemManifest().GetItemCategory()==EItemCategory::Consume;
}
