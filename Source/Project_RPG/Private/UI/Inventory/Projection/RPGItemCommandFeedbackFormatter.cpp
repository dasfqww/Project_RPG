#include "UI/Inventory/Projection/RPGItemCommandFeedbackFormatter.h"

#include "Component/RPGItemCommandComponent.h"

namespace
{
FText GetOperationLabel(const FName Operation)
{
	if (Operation == TEXT("Item.Move"))
		return NSLOCTEXT("RPGItemFeedback", "Move", "Move item");
	if (Operation == TEXT("Item.Swap"))
		return NSLOCTEXT("RPGItemFeedback", "Swap", "Swap items");
	if (Operation == TEXT("Item.StackTransfer"))
		return NSLOCTEXT("RPGItemFeedback", "Merge", "Merge stacks");
	if (Operation == TEXT("Item.Split"))
		return NSLOCTEXT("RPGItemFeedback", "Split", "Split stack");
	if (Operation == TEXT("Item.Equip"))
		return NSLOCTEXT("RPGItemFeedback", "Equip", "Equip item");
	if (Operation == TEXT("Item.Unequip"))
		return NSLOCTEXT("RPGItemFeedback", "Unequip", "Unequip item");
	if (Operation == TEXT("Item.Consume"))
		return NSLOCTEXT("RPGItemFeedback", "Consume", "Use item");
	if (Operation == TEXT("Item.ClaimMail"))
		return NSLOCTEXT("RPGItemFeedback", "ClaimMail", "Claim reward");
	if (Operation == TEXT("Item.Pickup"))
		return NSLOCTEXT("RPGItemFeedback", "Pickup", "Pick up item");
	return NSLOCTEXT("RPGItemFeedback", "OtherAction", "Item action");
}
}

FText FRPGItemCommandFeedbackFormatter::Build(const FRPGItemCommandClientResult& Result)
{
	FText Outcome;
	switch (Result.Result)
	{
	case ERPGItemCommandResultCode::Succeeded:
		Outcome = NSLOCTEXT("RPGItemFeedback", "Succeeded", "Confirmed by the server.");
		break;
	case ERPGItemCommandResultCode::AlreadyApplied:
		Outcome = NSLOCTEXT("RPGItemFeedback", "AlreadyApplied",
			"The server confirmed this request was already processed; it was not applied twice.");
		break;
	case ERPGItemCommandResultCode::InvalidRequest:
		Outcome = NSLOCTEXT("RPGItemFeedback", "InvalidRequest",
			"This action is not valid for the current item state.");
		break;
	case ERPGItemCommandResultCode::Forbidden:
		Outcome = NSLOCTEXT("RPGItemFeedback", "Forbidden", "This action is not currently permitted.");
		break;
	case ERPGItemCommandResultCode::Conflict:
		Outcome = NSLOCTEXT("RPGItemFeedback", "Conflict",
			"The item or destination changed. Check the current slots before trying again.");
		break;
	case ERPGItemCommandResultCode::BackendRejected:
		Outcome = NSLOCTEXT("RPGItemFeedback", "BackendRejected",
			"The item service did not accept this action. Check the item state before trying again.");
		break;
	case ERPGItemCommandResultCode::ProtocolError:
		Outcome = NSLOCTEXT("RPGItemFeedback", "ProtocolError",
			"The server could not validate the item response. Check the inventory status before taking another action.");
		break;
	case ERPGItemCommandResultCode::ServerStateError:
		Outcome = NSLOCTEXT("RPGItemFeedback", "ServerStateError",
			"The server cannot complete this item action right now. Check the inventory status.");
		break;
	case ERPGItemCommandResultCode::Busy:
		Outcome = NSLOCTEXT("RPGItemFeedback", "Busy",
			"Other item requests are pending. Check the inventory status before taking another action.");
		break;
	case ERPGItemCommandResultCode::Uncertain:
		Outcome = NSLOCTEXT("RPGItemFeedback", "Uncertain",
			"The result of this item action is unknown. Check the inventory status before taking another action.");
		break;
	default:
		Outcome = NSLOCTEXT("RPGItemFeedback", "UnknownResult",
			"An unrecognized item response was received. Check the inventory status.");
		break;
	}
	return FText::Format(NSLOCTEXT("RPGItemFeedback", "ActionOutcome", "{0}: {1}"),
		GetOperationLabel(Result.Operation), Outcome);
}
