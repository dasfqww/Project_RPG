#include "Tests/RPGItemCommandE2EProbeComponent.h"

#include "Component/RPGInventoryProjectionComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Item/Projection/RPGInventoryProjectionTypes.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RPGItemCommandE2EProbeComponent)

DEFINE_LOG_CATEGORY_STATIC(LogRPGItemE2E, Log, All);

namespace
{
	constexpr double ProbeTimeoutSeconds = 30.0;
	constexpr int32 UnequipInventorySlotIndex = 1;
}

URPGItemCommandE2EProbeComponent::URPGItemCommandE2EProbeComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void URPGItemCommandE2EProbeComponent::BeginPlay()
{
	Super::BeginPlay();

#if UE_BUILD_SHIPPING
	return;
#else
	APlayerController* Controller = Cast<APlayerController>(GetOwner());
	if (!Controller ||
		!Controller->IsLocalController() ||
		Controller->GetNetMode() != NM_Client)
	{
		return;
	}

	FString ConsumableItemIdText;
	FString EquipmentItemIdText;
	const bool bHasConsumableParameter = FParse::Value(
		FCommandLine::Get(),
		TEXT("RPGItemE2EConsume="),
		ConsumableItemIdText);
	const bool bHasEquipmentParameter = FParse::Value(
		FCommandLine::Get(),
		TEXT("RPGItemE2EEquip="),
		EquipmentItemIdText);
	if (!bHasConsumableParameter && !bHasEquipmentParameter)
	{
		return;
	}
	if (!bHasConsumableParameter ||
		!bHasEquipmentParameter ||
		!FGuid::Parse(ConsumableItemIdText, ConsumableItemId) ||
		!FGuid::Parse(EquipmentItemIdText, EquipmentItemId) ||
		ConsumableItemId == EquipmentItemId)
	{
		FailProbe(TEXT("InvalidCommandLineItemIds"));
		return;
	}

	if (URPGItemCommandComponent* Commands =
		Controller->FindComponentByClass<URPGItemCommandComponent>())
	{
		Commands->OnItemCommandCompleted.RemoveDynamic(
			this,
			&ThisClass::HandleCommandCompleted);
		Commands->OnItemCommandCompleted.AddDynamic(
			this,
			&ThisClass::HandleCommandCompleted);
	}

	DeadlineSeconds = FPlatformTime::Seconds() + ProbeTimeoutSeconds;
	SetComponentTickEnabled(true);
	UE_LOG(LogRPGItemE2E, Display,
		TEXT("RPG_ITEM_E2E WAIT Consume=%s Equip=%s"),
		*ConsumableItemId.ToString(EGuidFormats::DigitsWithHyphensLower),
		*EquipmentItemId.ToString(EGuidFormats::DigitsWithHyphensLower));
#endif
}

void URPGItemCommandE2EProbeComponent::EndPlay(
	const EEndPlayReason::Type EndPlayReason)
{
	FinishProbe();
	Super::EndPlay(EndPlayReason);
}

void URPGItemCommandE2EProbeComponent::TickComponent(
	const float DeltaTime,
	const ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

#if !UE_BUILD_SHIPPING
	if (FPlatformTime::Seconds() >= DeadlineSeconds)
	{
		FailProbe(TEXT("Timeout"));
		return;
	}

	APlayerController* Controller = Cast<APlayerController>(GetOwner());
	URPGInventoryProjectionComponent* Projection = Controller
		? Controller->FindComponentByClass<
			URPGInventoryProjectionComponent>()
		: nullptr;
	if (!Projection ||
		Projection->GetLoadState() !=
			ERPGInventoryProjectionLoadState::Ready)
	{
		return;
	}

	FRPGInventoryProjectionEntry ConsumableEntry;
	FRPGInventoryProjectionEntry EquipmentEntry;
	if (!Projection->FindProjectedItem(ConsumableItemId, ConsumableEntry) ||
		!Projection->FindProjectedItem(EquipmentItemId, EquipmentEntry))
	{
		FailProbe(TEXT("ItemMissingFromReadyProjection"));
		return;
	}

	URPGItemCommandComponent* Commands = Controller
		? Controller->FindComponentByClass<URPGItemCommandComponent>()
		: nullptr;
	if (!Commands)
	{
		FailProbe(TEXT("CommandComponentUnavailable"));
		return;
	}

	switch (Phase)
	{
	case EProbePhase::WaitingForInitialProjection:
		if (ConsumableEntry.GetContainerType() !=
				ERPGItemContainerType::Inventory ||
			EquipmentEntry.GetContainerType() !=
				ERPGItemContainerType::Inventory ||
			ConsumableEntry.GetQuantity() <= 1 ||
			EquipmentEntry.GetQuantity() != 1)
		{
			FailProbe(TEXT("InvalidInitialProjection"));
			return;
		}

		InitialConsumableQuantity = ConsumableEntry.GetQuantity();
		InitialConsumableRevision = ConsumableEntry.GetRevision();
		InitialEquipmentRevision = EquipmentEntry.GetRevision();
		RequestId = FGuid::NewGuid();
		Phase = EProbePhase::WaitingForConsumeResult;
		Commands->ServerConsumeItem(
			RequestId,
			ConsumableItemId,
			InitialConsumableRevision);
		UE_LOG(LogRPGItemE2E, Display,
			TEXT("RPG_ITEM_E2E CONSUME_SENT Request=%s Item=%s Quantity=%d Revision=%lld"),
			*RequestId.ToString(EGuidFormats::DigitsWithHyphensLower),
			*ConsumableItemId.ToString(EGuidFormats::DigitsWithHyphensLower),
			InitialConsumableQuantity,
			InitialConsumableRevision);
		return;

	case EProbePhase::WaitingForConsumeResult:
		return;

	case EProbePhase::WaitingForConsumeProjection:
		if (ConsumableEntry.GetContainerType() !=
				ERPGItemContainerType::Inventory ||
			ConsumableEntry.GetQuantity() !=
				InitialConsumableQuantity - 1 ||
			ConsumableEntry.GetRevision() !=
				InitialConsumableRevision + 1 ||
			EquipmentEntry.GetContainerType() !=
				ERPGItemContainerType::Inventory ||
			EquipmentEntry.GetRevision() != InitialEquipmentRevision)
		{
			return;
		}

		RequestId = FGuid::NewGuid();
		Phase = EProbePhase::WaitingForEquipResult;
		Commands->ServerEquipItem(
			RequestId,
			EquipmentItemId,
			InitialEquipmentRevision,
			EEquipmentSlotType::Head);
		UE_LOG(LogRPGItemE2E, Display,
			TEXT("RPG_ITEM_E2E EQUIP_SENT Request=%s Item=%s Revision=%lld Slot=Head"),
			*RequestId.ToString(EGuidFormats::DigitsWithHyphensLower),
			*EquipmentItemId.ToString(EGuidFormats::DigitsWithHyphensLower),
			InitialEquipmentRevision);
		return;

	case EProbePhase::WaitingForEquipResult:
		return;

	case EProbePhase::WaitingForEquipProjection:
		if (ConsumableEntry.GetContainerType() !=
				ERPGItemContainerType::Inventory ||
			ConsumableEntry.GetSlotIndex() != 0 ||
			ConsumableEntry.GetQuantity() !=
				InitialConsumableQuantity - 1 ||
			ConsumableEntry.GetRevision() !=
				InitialConsumableRevision + 1 ||
			EquipmentEntry.GetContainerType() !=
				ERPGItemContainerType::Equipment ||
			EquipmentEntry.GetSlotIndex() !=
				static_cast<int32>(EEquipmentSlotType::Head) ||
			EquipmentEntry.GetQuantity() != 1 ||
			EquipmentEntry.GetRevision() != InitialEquipmentRevision + 1)
		{
			return;
		}

		RequestId = FGuid::NewGuid();
		Phase = EProbePhase::WaitingForUnequipResult;
		Commands->ServerUnequipItem(
			RequestId,
			EquipmentItemId,
			EquipmentEntry.GetRevision(),
			UnequipInventorySlotIndex);
		UE_LOG(LogRPGItemE2E, Display,
			TEXT("RPG_ITEM_E2E UNEQUIP_SENT Request=%s Item=%s Revision=%lld Slot=%d"),
			*RequestId.ToString(EGuidFormats::DigitsWithHyphensLower),
			*EquipmentItemId.ToString(EGuidFormats::DigitsWithHyphensLower),
			EquipmentEntry.GetRevision(),
			UnequipInventorySlotIndex);
		return;

	case EProbePhase::WaitingForUnequipResult:
		return;

	case EProbePhase::WaitingForUnequipProjection:
		if (ConsumableEntry.GetContainerType() !=
				ERPGItemContainerType::Inventory ||
			ConsumableEntry.GetSlotIndex() != 0 ||
			ConsumableEntry.GetQuantity() !=
				InitialConsumableQuantity - 1 ||
			ConsumableEntry.GetRevision() !=
				InitialConsumableRevision + 1 ||
			EquipmentEntry.GetContainerType() !=
				ERPGItemContainerType::Inventory ||
			EquipmentEntry.GetSlotIndex() != UnequipInventorySlotIndex ||
			EquipmentEntry.GetQuantity() != 1 ||
			EquipmentEntry.GetRevision() != InitialEquipmentRevision + 2)
		{
			return;
		}

		UE_LOG(LogRPGItemE2E, Display,
			TEXT("RPG_ITEM_E2E PASS ConsumeItem=%s Quantity=%d Revision=%lld EquipItem=%s InventorySlot=%d Revision=%lld"),
			*ConsumableItemId.ToString(EGuidFormats::DigitsWithHyphensLower),
			ConsumableEntry.GetQuantity(),
			ConsumableEntry.GetRevision(),
			*EquipmentItemId.ToString(EGuidFormats::DigitsWithHyphensLower),
			EquipmentEntry.GetSlotIndex(),
			EquipmentEntry.GetRevision());
		FinishProbe();
		return;
	}
#endif
}

void URPGItemCommandE2EProbeComponent::HandleCommandCompleted(
	const FRPGItemCommandClientResult Result)
{
#if !UE_BUILD_SHIPPING
	if (Result.RequestId != RequestId)
	{
		return;
	}

	const bool bResultSucceeded =
		Result.Result == ERPGItemCommandResultCode::Succeeded ||
		Result.Result == ERPGItemCommandResultCode::AlreadyApplied;
	const FString ResultName = StaticEnum<ERPGItemCommandResultCode>()
		? StaticEnum<ERPGItemCommandResultCode>()->GetNameStringByValue(
			static_cast<int64>(Result.Result))
		: TEXT("Unknown");
	if (!bResultSucceeded)
	{
		const FString FailureReason = FString::Printf(
			TEXT("CommandResult=%s"),
			*ResultName);
		FailProbe(*FailureReason);
		return;
	}

	switch (Phase)
	{
	case EProbePhase::WaitingForConsumeResult:
		Phase = EProbePhase::WaitingForConsumeProjection;
		return;
	case EProbePhase::WaitingForEquipResult:
		Phase = EProbePhase::WaitingForEquipProjection;
		return;
	case EProbePhase::WaitingForUnequipResult:
		Phase = EProbePhase::WaitingForUnequipProjection;
		return;
	default:
		FailProbe(TEXT("UnexpectedCommandCompletion"));
		return;
	}
#endif
}

void URPGItemCommandE2EProbeComponent::FailProbe(const TCHAR* Reason)
{
	const TCHAR* Stage = TEXT("Unknown");
	switch (Phase)
	{
	case EProbePhase::WaitingForInitialProjection:
		Stage = TEXT("InitialProjection");
		break;
	case EProbePhase::WaitingForConsumeResult:
		Stage = TEXT("ConsumeResult");
		break;
	case EProbePhase::WaitingForConsumeProjection:
		Stage = TEXT("ConsumeProjection");
		break;
	case EProbePhase::WaitingForEquipResult:
		Stage = TEXT("EquipResult");
		break;
	case EProbePhase::WaitingForEquipProjection:
		Stage = TEXT("EquipProjection");
		break;
	case EProbePhase::WaitingForUnequipResult:
		Stage = TEXT("UnequipResult");
		break;
	case EProbePhase::WaitingForUnequipProjection:
		Stage = TEXT("UnequipProjection");
		break;
	}
	UE_LOG(LogRPGItemE2E, Error,
		TEXT("RPG_ITEM_E2E FAIL %s Stage=%s Consume=%s Equip=%s"),
		Reason,
		Stage,
		*ConsumableItemId.ToString(EGuidFormats::DigitsWithHyphensLower),
		*EquipmentItemId.ToString(EGuidFormats::DigitsWithHyphensLower));
	FinishProbe();
}

void URPGItemCommandE2EProbeComponent::FinishProbe()
{
	SetComponentTickEnabled(false);
	if (APlayerController* Controller = Cast<APlayerController>(GetOwner()))
	{
		if (URPGItemCommandComponent* Commands =
			Controller->FindComponentByClass<URPGItemCommandComponent>())
		{
			Commands->OnItemCommandCompleted.RemoveDynamic(
				this,
				&ThisClass::HandleCommandCompleted);
		}
	}
}
