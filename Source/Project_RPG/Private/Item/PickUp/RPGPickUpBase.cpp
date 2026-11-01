// Fill out your copyright notice in the Description page of Project Settings.


#include "Item/PickUp/RPGPickUpBase.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameInstance.h"
#include "LatentActions.h"
#include "Components/SphereComponent.h"
#include "Character/RPGPlayer.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "RPGGameplayTags.h"
#include "Item/RPGItemBase.h"
#include "Component/Item/RPGItemComponent.h"
#include "Component/RPGItemCommandComponent.h"
#include "Component/RPGPickupGrantComponent.h"
#include "Controller/RPGPlayerController.h"
#include "UI/RPGItemNameWidget.h"
#include "Components/WidgetComponent.h"
#include "Components/TextBlock.h"
#include "FunctionLibrary/RPGCoreFunctionLibrary.h"
#include "Net/UnrealNetwork.h"
#include "Manager/ObjectManager.h"
#include "Item/Fragment/RPGItemFragment.h"

#include "RPGDebugHelper.h"

// Sets default values
ARPGPickUpBase::ARPGPickUpBase()
{
    PrimaryActorTick.bCanEverTick = false;
    PickupGrantComponent = CreateDefaultSubobject<URPGPickupGrantComponent>(TEXT("PickupGrant"));

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
    SetReplicateMovement(true);
}

void ARPGPickUpBase::BeginPlay()
{
    Super::BeginPlay();

    //InitializePickUp(URPGItemBase::StaticClass(), ItemQuantity);
    InteractableData = InstanceInteractableData;
}

void ARPGPickUpBase::PostInitializeComponents()
{
    Super::PostInitializeComponents();
    // Generate on the spawned authority instance, never in a CDO/asset constructor.
    if (HasAuthority() && !PickupDropId.IsValid()) PickupDropId = FGuid::NewGuid();
}

void ARPGPickUpBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps); 

    DOREPLIFETIME(ThisClass, ItemManifest);
    DOREPLIFETIME(ThisClass, PickupDropId);
    DOREPLIFETIME(ThisClass, bIsAvailableForPickup);
}

void ARPGPickUpBase::InitItemManifest(FItemManifest CopyOfManifest, FName InPoolName)
{
    if (!HasAuthority() || (PickupGrantComponent && PickupGrantComponent->IsPending())) return;
    // Pool reuse / fresh manifest means a NEW logical drop. Pending grants cannot reinitialize it.
    PickupDropId = FGuid::NewGuid();
    ItemManifest = CopyOfManifest;
    OwningPoolName = InPoolName;
    bIsAvailableForPickup = true;
    SetActorHiddenInGame(false);
    SetActorEnableCollision(true);
    ForceNetUpdate();
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

bool ARPGPickUpBase::InitializeServerDropQuantity(const int32 Quantity)
{
    if (!HasAuthority() || IsActorBeingDestroyed() || !bIsAvailableForPickup || IsHidden() ||
        (PickupGrantComponent && PickupGrantComponent->IsPending()) || Quantity <= 0 || Quantity > MAX_int8 ||
        !ItemManifest.GetItemTag().IsValid() || ItemManifest.GetItemCategory() == EItemCategory::None)
    {
        return false;
    }
    FStackableFragment* Stack = ItemManifest.GetFragmentOfTypeMutable<FStackableFragment>();
    if (Stack ? Stack->GetMaxQuantity() < 1 || Quantity > Stack->GetMaxQuantity() : Quantity != 1)
    {
        return false;
    }
    if (Stack) Stack->SetQuantity(Quantity);
    ItemQuantity = Quantity;
    InstanceInteractableData.Quantity = Quantity;
    // Preserve the authored/native pickup's name and interaction action.
    InteractableData.Quantity = Quantity;
    ForceNetUpdate();
    return true;
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
    if (!bIsAvailableForPickup || !IsValid(PlayerController)) return;
    if (URPGItemCommandComponent* Commands =
        PlayerController->FindComponentByClass<URPGItemCommandComponent>())
    {
        Commands->ServerPickupItem(this);
    }
}

//void ARPGPickUpBase::OnPickedUp()
//{
//}

void ARPGPickUpBase::PickedUp()
{
    if (!HasAuthority() || (PickupGrantComponent && PickupGrantComponent->IsPending())) return;

    bIsAvailableForPickup = false;
    ForceNetUpdate();

    if (!OwningPoolName.IsNone())
    {
        UObjectManager::Get<UObjectManager>(this)->Despawn(
            OwningPoolName, this, true);
    }
    else
    {
        Destroy();
    }
}

bool ARPGPickUpBase::TryClaimPickup()
{
    if (!HasAuthority() || !bIsAvailableForPickup || IsHidden())
    {
        return false;
    }

    bIsAvailableForPickup = false;
    ForceNetUpdate();
    return true;
}

void ARPGPickUpBase::ReleasePickupClaim()
{
    if (!HasAuthority() || IsHidden() ||
        (PickupGrantComponent && PickupGrantComponent->IsPending()))
    {
        return;
    }

    bIsAvailableForPickup = true;
    ForceNetUpdate();
}

void ARPGPickUpBase::SetPickupQuantity(int32 Quantity)
{
    if (!HasAuthority() || (PickupGrantComponent && PickupGrantComponent->IsPending()))
    {
        return;
    }

    if (FStackableFragment* Stackable =
        ItemManifest.GetFragmentOfTypeMutable<FStackableFragment>())
    {
        Stackable->SetQuantity(FMath::Max(Quantity, 1));
        ForceNetUpdate();
    }
}

void ARPGPickUpBase::OnRep_PickupAvailability()
{
    SetActorEnableCollision(bIsAvailableForPickup && !IsHidden());
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
