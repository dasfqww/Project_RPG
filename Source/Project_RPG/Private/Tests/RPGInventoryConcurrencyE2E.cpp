#include "Tests/RPGInventoryConcurrencyE2E.h"

#if !UE_BUILD_SHIPPING
#include "Component/RPGInventoryProjectionComponent.h"
#include "Component/RPGItemCommandComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UI/MVVM/RPGInventoryProjectionViewModel.h"

FRPGInventoryConcurrencyE2E::~FRPGInventoryConcurrencyE2E()
{
	if (auto* Model = Cast<URPGInventoryProjectionViewModel>(ModelOwner.Get()))
	{
		Model->RemoveFieldValueChangedDelegate(
			URPGInventoryProjectionViewModel::FFieldNotificationClassDescriptor::LastCommandResult, ResultHandle);
		Model->InitializeFromPlayerController(nullptr);
	}
}

int32 FRPGInventoryConcurrencyE2E::Tick(APlayerController& Controller)
{
	const auto Fail = [this](const TCHAR* Reason) { Error = Reason; return -1; };
	auto* Projection = Controller.FindComponentByClass<URPGInventoryProjectionComponent>();
	auto* Commands = Controller.FindComponentByClass<URPGItemCommandComponent>();
	if (Controller.HasAuthority() || !Controller.IsLocalController() || !Projection || !Commands)
		return Fail(TEXT("InventoryConcurrencyNeedsOwningClient"));
	if (Phase == 0)
	{
		const auto GuidArg = [](const TCHAR* Key, FGuid& Out)
		{
			FString Text;
			return FParse::Value(FCommandLine::Get(), Key, Text) && FGuid::Parse(Text, Out) && Out.IsValid();
		};
		if (!FParse::Value(FCommandLine::Get(), TEXT("RPGInventoryConcurrencyRole="), Role) || (Role != 1 && Role != 2) ||
			!GuidArg(TEXT("RPGInventoryConcurrencyOwnA="), OwnA) || !GuidArg(TEXT("RPGInventoryConcurrencyForeignA="), ForeignA) ||
			OwnA == ForeignA || !FParse::Value(FCommandLine::Get(), TEXT("RPGInventoryConcurrencyGate="), StartGate) ||
			(Role == 1 && (!GuidArg(TEXT("RPGInventoryConcurrencyOwnB="), OwnB) || OwnB == OwnA || OwnB == ForeignA)) ||
			(Role == 2 && (!GuidArg(TEXT("RPGInventoryConcurrencyForeignB="), ForeignB) || ForeignB == OwnA || ForeignB == ForeignA)))
			return Fail(TEXT("InventoryConcurrencyInvalidArguments"));
		auto* Model = NewObject<URPGInventoryProjectionViewModel>(&Controller);
		ModelOwner.Reset(Model);
		Model->InitializeFromPlayerController(&Controller);
		ResultHandle = Model->AddFieldValueChangedDelegate(
			URPGInventoryProjectionViewModel::FFieldNotificationClassDescriptor::LastCommandResult,
			INotifyFieldValueChanged::FFieldValueChangedDelegate::CreateLambda([this](UObject* Object, UE::FieldNotification::FFieldId)
			{
				const auto* Current = Cast<URPGInventoryProjectionViewModel>(Object);
				if (Current && Current->LastCommandResult.RequestId.IsValid())
				{
					if (Current->LastCommandResult.Operation != TEXT("Item.Move")) Error = TEXT("InventoryConcurrencyUnexpectedOperation");
					Results.Add(Current->LastCommandResult.RequestId, static_cast<uint8>(Current->LastCommandResult.Result));
				}
			}));
		Deadline = FPlatformTime::Seconds() + 240.0; Phase = 1;
		return 0;
	}
	if (!Error.IsEmpty()) return -1;
	if (FPlatformTime::Seconds() >= Deadline) return Fail(TEXT("InventoryConcurrencyTimeout"));
	auto* Model = Cast<URPGInventoryProjectionViewModel>(ModelOwner.Get());
	if (!Model || Model->LoadState != ERPGInventoryProjectionLoadState::Ready) return 0;
	FRPGInventoryProjectionEntry A, B, Foreign;
	if (Projection->FindProjectedItem(ForeignA, Foreign) || (ForeignB.IsValid() && Projection->FindProjectedItem(ForeignB, Foreign)))
		return Fail(TEXT("InventoryConcurrencyForeignProjectionLeak"));
	if (!Projection->FindProjectedItem(OwnA, A) || (Role == 1 && !Projection->FindProjectedItem(OwnB, B)))
		return Phase == 1 ? 0 : Fail(TEXT("InventoryConcurrencyOwnItemMissing"));
	if (Model->InventoryItems.Num() != (Role == 1 ? 2 : 1))
		return Phase == 1 ? 0 : Fail(TEXT("InventoryConcurrencyUnexpectedItemCount"));
	const auto Matches = [](const FRPGInventoryProjectionEntry& Item, int32 Slot, int64 Revision, int32 Quantity)
	{
		return Item.GetContainerType() == ERPGItemContainerType::Inventory && Item.GetSlotIndex() == Slot &&
			Item.GetRevision() == Revision && Item.GetQuantity() == Quantity;
	};
	const auto Result = [this](const FGuid Id) { return Results.Find(Id); };
	const auto Is = [](const uint8* Code, const ERPGItemCommandResultCode Expected) { return Code && *Code == static_cast<uint8>(Expected); };
	const auto Initial = [&]() { return Matches(A, 0, 1, Role == 1 ? 7 : 3) && (Role == 2 || Matches(B, 1, 1, 1)); };
	const auto RaceState = [&]()
	{
		if (Role == 2) return Matches(A, 3, 2, 3);
		return WinnerItem == OwnA ? Matches(A, 3, 2, 7) && Matches(B, 1, 1, 1)
			: WinnerItem == OwnB && Matches(A, 0, 1, 7) && Matches(B, 3, 2, 1);
	};
	if (Phase == 1)
	{
		if (!Initial()) return Fail(TEXT("InventoryConcurrencyInitialSnapshotMismatch"));
		BadRequest = FGuid::NewGuid();
		Commands->ServerMoveItem(BadRequest, ForeignA, 1, 6); // deliberate untrusted-client ingress
		Phase = 2;
	}
	else if (Phase == 2)
	{
		const auto* Code = Result(BadRequest);
		if (!Code) return 0;
		if (!Is(Code, ERPGItemCommandResultCode::InvalidRequest) && !Is(Code, ERPGItemCommandResultCode::Forbidden))
			return Fail(TEXT("InventoryConcurrencyForeignRequestNotRejected"));
		if (!Initial()) return Fail(TEXT("InventoryConcurrencyForeignRequestChangedOwnItems"));
		UE_LOG(LogTemp, Display, TEXT("RPG_ITEM_CONCURRENCY FOREIGN_REJECTED Role=%d Request=%s OwnerOnly=1"),
			Role, *BadRequest.ToString(EGuidFormats::DigitsWithHyphensLower));
		Phase = 3;
	}
	else if (Phase == 3)
	{
		if (!Initial()) return Fail(TEXT("InventoryConcurrencyChangedBeforeGate"));
		if (!IFileManager::Get().FileExists(*StartGate)) return 0;
		RequestA = FGuid::NewGuid();
		Commands->ServerMoveItem(RequestA, OwnA, 1, 3);
		if (Role == 1)
		{
			RequestB = FGuid::NewGuid();
			Commands->ServerMoveItem(RequestB, OwnB, 1, 3); // same frame/destination, bypass UI reservations only
		}
		UE_LOG(LogTemp, Display, TEXT("RPG_ITEM_CONCURRENCY DISPATCH Role=%d A=%s B=%s RpcIngress=1"),
			Role, *RequestA.ToString(EGuidFormats::DigitsWithHyphensLower), *RequestB.ToString(EGuidFormats::DigitsWithHyphensLower));
		Phase = 4;
	}
	else if (Phase == 4)
	{
		const auto* First = Result(RequestA);
		if (!First || (Role == 1 && !Result(RequestB))) return 0;
		if (Role == 2)
		{
			if (!Is(First, ERPGItemCommandResultCode::Succeeded)) return Fail(TEXT("InventoryConcurrencyOtherOwnerMoveRejected"));
			if (!RaceState()) return 0;
			UE_LOG(LogTemp, Display, TEXT("RPG_ITEM_CONCURRENCY PASS Role=2 ForeignRejected=1 IndependentSlot=3 Revision=2 RpcIngress=1 Physical=0"));
			return 1;
		}
		const auto* Second = Result(RequestB);
		if (Is(First, ERPGItemCommandResultCode::Succeeded) && Is(Second, ERPGItemCommandResultCode::Conflict))
		{ WinnerRequest = RequestA; WinnerItem = OwnA; }
		else if (Is(Second, ERPGItemCommandResultCode::Succeeded) && Is(First, ERPGItemCommandResultCode::Conflict))
		{ WinnerRequest = RequestB; WinnerItem = OwnB; }
		else return Fail(TEXT("InventoryConcurrencyExpectedOneSuccessOneConflict"));
		if (!RaceState()) return 0;
		const FGuid Loser = WinnerRequest == RequestA ? RequestB : RequestA;
		UE_LOG(LogTemp, Display, TEXT("RPG_ITEM_CONCURRENCY RACE WinnerRequest=%s LoserRequest=%s WinnerItem=%s OneSuccess=1 OneConflict=1"),
			*WinnerRequest.ToString(EGuidFormats::DigitsWithHyphensLower), *Loser.ToString(EGuidFormats::DigitsWithHyphensLower),
			*WinnerItem.ToString(EGuidFormats::DigitsWithHyphensLower));
		Results.Remove(WinnerRequest);
		Phase = 5;
	}
	else if (Phase == 5)
	{
		if (!RaceState()) return Fail(TEXT("InventoryConcurrencyChangedBeforeReplay"));
		if (!IFileManager::Get().FileExists(*(StartGate + TEXT(".replay")))) return 0;
		Commands->ServerMoveItem(WinnerRequest, WinnerItem, 1, 3); // identical cached replay, no revision upgrade
		Phase = 6;
	}
	else if (Phase == 6)
	{
		const auto* Code = Result(WinnerRequest);
		if (!Code) return 0;
		if (!Is(Code, ERPGItemCommandResultCode::AlreadyApplied)) return Fail(TEXT("InventoryConcurrencyReplayNotIdempotent"));
		if (!RaceState()) return Fail(TEXT("InventoryConcurrencyReplayChangedSnapshot"));
		UE_LOG(LogTemp, Display, TEXT("RPG_ITEM_CONCURRENCY PASS Role=1 ForeignRejected=1 OneWinner=1 OneConflict=1 CachedReplay=1 OwnerOnly=1 RpcIngress=1 Physical=0"));
		return 1;
	}
	return 0;
}
#else
FRPGInventoryConcurrencyE2E::~FRPGInventoryConcurrencyE2E() = default;
int32 FRPGInventoryConcurrencyE2E::Tick(APlayerController&) { return -1; }
#endif
