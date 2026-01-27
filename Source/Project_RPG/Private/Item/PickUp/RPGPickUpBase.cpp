// Fill out your copyright notice in the Description page of Project Settings.


#include "Item/PickUp/RPGPickUpBase.h"
#include "Components/SphereComponent.h"
#include "Character/RPGPlayer.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "RPGGameplayTags.h"
#include "Item/RPGItemBase.h"
#include "Component/Item/RPGItemComponent.h"
#include "Component/RPGInventoryComponent.h"
#include "Controller/RPGPlayerController.h"
#include "UI/RPGItemNameWidget.h"
#include "Components/WidgetComponent.h"
#include "Components/TextBlock.h"
#include "RPGFunctionLibrary.h"
#include "Net/UnrealNetwork.h"
#include "Manager/PoolManager.h"

#include "RPGDebugHelper.h"

// Sets default values
ARPGPickUpBase::ARPGPickUpBase()
{
    PrimaryActorTick.bCanEverTick = false;

    PickupMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PickupMesh"));
    PickupMesh->SetSimulatePhysics(true);
    SetRootComponent(PickupMesh);
    //Mesh->SetupAttachment(GetRootComponent());

    //PickUpCollisionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("PickUpCollisionSphere"));
    //PickUpCollisionSphere->SetupAttachment(PickupMesh);
    //PickUpCollisionSphere->InitSphereRadius(50.f);
    //PickUpCollisionSphere->OnComponentBeginOverlap.AddUniqueDynamic(this, &ThisClass::OnPickUpCollisionSphereBeginOverlap);

    ItemTextWidgetComponent = CreateDefaultSubobject<UWidgetComponent>(TEXT("WidgetComponent"));
    ItemTextWidgetComponent->SetupAttachment(GetRootComponent());

    bReplicates = true;
}

void ARPGPickUpBase::BeginPlay()
{
    Super::BeginPlay();

    //InitializePickUp(URPGItemBase::StaticClass(), ItemQuantity);
    InteractableData = InstanceInteractableData;
}

void ARPGPickUpBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps); 

    DOREPLIFETIME(ThisClass, ItemManifest);
}

void ARPGPickUpBase::InitItemManifest(FItemManifest CopyOfManifest)
{
    ItemManifest = CopyOfManifest;
}

//void ARPGPickUpBase::InitializePickUp(const TSubclassOf<URPGItemBase> BaseClass, const int32 InQuantity)
//{
//    if (ItemDataTable&&!DesiredItemID.IsNone())
//    {
//        const FRPGItemData* ItemData = ItemDataTable->FindRow<FRPGItemData>(DesiredItemID, DesiredItemID.ToString());
//
//        ItemReference = NewObject<URPGItemBase>(this, BaseClass);
//
//        ItemReference->ID = ItemData->ID;
//        ItemReference->ItemType = ItemData->ItemType;
//        ItemReference->ItemGrade = ItemData->ItemGrade;
//        ItemReference->Statistics = ItemData->ItemStstistics;
//        ItemReference->NumericData = ItemData->ItemNumericData;
//        ItemReference->TextData = ItemData->ItemTextData;
//        ItemReference->AssetData = ItemData->ItemAssetData;
//
//        InQuantity <= 0 ? ItemReference->SetQuantity(1) : ItemReference->SetQuantity(InQuantity);
//
//        PickupMesh->SetStaticMesh(ItemData->ItemAssetData.Mesh);
//
//        //�����丵 �Ұ�
//        if (ItemTextWidgetComponent)
//        {
//            // ������ ���������� �����Ǿ� �ִ��� Ȯ��
//            UUserWidget* Widget = ItemTextWidgetComponent->GetWidget();
//            if (Widget)
//            {
//                // URPGItemNameWidget�� �ؽ�Ʈ�� �����ϰ� �ִٰ� ����
//                URPGItemNameWidget* ItemWidget = Cast<URPGItemNameWidget>(Widget);
//                if (ItemWidget)
//                {
//                    // ������ �̸��� ������ �ؽ�Ʈ�� �����Ͽ� ����
//                    FString ItemText = FString::Printf(TEXT("%s [x%d]"),
//                        *ItemReference->TextData.Name.ToString(), InQuantity);
//                    ItemWidget->ItemNameText->SetText(FText::FromString(ItemText));  // �ؽ�Ʈ ����
//                }
//            }
//        }
//
//        UpdateInteractableData();
//    }
//}


void ARPGPickUpBase::InitializeDrop(URPGItemBase* ItemToDrop, const int32 InQuantity)
{
    //ItemReference = ItemToDrop;
    ////InQuantity <= 0 ? ItemReference->SetQuantity(1) : ItemReference->SetQuantity(InQuantity);
    //ItemReference->NumericData.Weight = ItemToDrop->GetItemSingleWeight();
    //// �޽� ����
    //if (PickupMesh && ItemToDrop->AssetData.Mesh)
    //{
    //    PickupMesh->SetStaticMesh(ItemToDrop->AssetData.Mesh);
    //}

    //if (ItemTextWidgetComponent)
    //{
    //    // ������ ���������� �����Ǿ� �ִ��� Ȯ��
    //    UUserWidget* Widget = ItemTextWidgetComponent->GetWidget();
    //    if (Widget)
    //    {
    //        // URPGItemNameWidget�� �ؽ�Ʈ�� �����ϰ� �ִٰ� ����
    //        URPGItemNameWidget* ItemWidget = Cast<URPGItemNameWidget>(Widget);
    //        if (ItemWidget)
    //        {
    //            // ������ �̸��� ������ �ؽ�Ʈ�� �����Ͽ� ����
    //            FString ItemText = FString::Printf(TEXT("%s [x%d]"),
    //                *ItemReference->TextData.Name.ToString(), InQuantity);
    //            ItemWidget->ItemNameText->SetText(FText::FromString(ItemText));  // �ؽ�Ʈ ����
    //        }
    //    }
    //}

    UpdateInteractableData();
}

//void ARPGPickUpBase::OnPickUpCollisionSphereBeginOverlap(UPrimitiveComponent* OverlappedComponent,
//    AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, 
//    const FHitResult& SweepResult)
//{
//
//}

void ARPGPickUpBase::BeginFocus()
{
    if (PickupMesh)
    {
        PickupMesh->SetRenderCustomDepth(true);
    }
}

void ARPGPickUpBase::EndFocus()
{
    if (PickupMesh)
    {
        PickupMesh->SetRenderCustomDepth(false);
    }
}

void ARPGPickUpBase::BeginInteract()
{
    Debug::Print("begininteract..");
}

void ARPGPickUpBase::EndInteract()
{
    Debug::Print("endinteract..");

}

void ARPGPickUpBase::Interact(APlayerController* PlayerController)
{        
    TakePickup(PlayerController);
}

void ARPGPickUpBase::UpdateInteractableData()
{
    InstanceInteractableData.InteractableType = EInteractableType::PickUp;
    /*InstanceInteractableData.Action = ItemReference->TextData.InteractionText;
    InstanceInteractableData.Name = ItemReference->TextData.Name;
    InstanceInteractableData.Quantity = ItemReference->Quantity;*/
    InteractableData = InstanceInteractableData;
}

void ARPGPickUpBase::TakePickup(APlayerController* PlayerController)
{ 
    //URPGInventoryComponent* InventoryComp = URPGFunctionLibrary::GetInventoryComponent(PlayerController);
    URPGInventoryComponent* InventoryComp = 
        URPGFunctionLibrary::GetComponentFromPlayerController<URPGInventoryComponent>(PlayerController);

    if (/*!IsValid(ItemComp) ||*/ !IsValid(InventoryComp)) return;
    
    InventoryComp->TryAddItem(this);
        
    /*if (!IsPendingKillPending())
    {
        if (ItemReference)
        {
            if (URPGInventoryComponent* PlayerInventory=Taker->GetRPGInventory())
            {
                const FItemAddResult AddResult = PlayerInventory->HandleAddItem(ItemReference);

                switch (AddResult.OperationResult)
                {                    
                case EItemAddResult::IAR_NoItemAdded:
                    break;
                case EItemAddResult::IAR_PartialAmountItemAdded:
                    UpdateInteractableData();
                    if (ARPGPlayerController* PC =Cast<ARPGPlayerController>(Taker->Controller))
                    {
                        PC->UpdateInteractionWidget();
                    }
                    break;
                case EItemAddResult::IAR_AllItemAdded:
                    Destroy();
                    break;
                }

                UE_LOG(LogTemp, Warning, TEXT("%s"), *AddResult.ResultMessage.ToString());
            }
            else
            {
                Debug::Print("Player Inventory component is null..");
            }
        }
        else
        {
            Debug::Print("Pickup internal item reference was somehow null..");
        }
    }*/
}

//void ARPGPickUpBase::OnPickedUp()
//{
//}

void ARPGPickUpBase::PickedUp()
{
    //OnPickedUp();
	//TODO: object pooling
	FName PoolName = FName("Item");
    
	UPoolManager::Get<UPoolManager>(this)->ReleaseToPool(PoolName, this);
    //Destroy();
}

//#if WITH_EDITOR
//void ARPGPickUpBase::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
//{
//    Super::PostEditChangeProperty(PropertyChangedEvent);
//
//    const FName ChangedPropertyName = 
//        PropertyChangedEvent.Property ? PropertyChangedEvent.Property->GetFName() : NAME_None;
//
//    if (ChangedPropertyName == GET_MEMBER_NAME_CHECKED(ARPGPickUpBase, DesiredItemID))
//    {
//        if (ItemDataTable)
//        {
//            const FString ContextString{ DesiredItemID.ToString() };
//
//            if (const FRPGItemData* ItemData=
//                ItemDataTable->FindRow<FRPGItemData>(DesiredItemID, DesiredItemID.ToString()))
//            {
//                PickupMesh->SetStaticMesh(ItemData->ItemAssetData.Mesh);
//            }
//        }
//    }
//}
//#endif
