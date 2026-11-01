#include "Progression/RPGEquipmentMutationSubsystem.h"

#include "Progression/RPGEquipmentMutationCodec.h"
#include "Manager/HttpWebManager.h"
#include "Engine/GameInstance.h"
#include "Interfaces/IHttpRequest.h"
#include "Misc/Paths.h"
#include "Item/Backend/RPGItemBackendJsonCodec.h"
#include "Item/Projection/RPGInventoryProjectionMapper.h"

namespace RPGEquipmentMutationUI
{
	FRPGEquipmentMutationView View(FGuid Character, const FRPGEquipmentMutationCommand* Command,
		ERPGEquipmentMutationState State)
	{
		FRPGEquipmentMutationView Result; Result.CharacterId = Character; Result.State = State;
		if (Command) { Result.RequestId = Command->RequestId; Result.Operation = Command->Operation; }
		switch (State)
		{
		case ERPGEquipmentMutationState::AwaitingServer: Result.Message = NSLOCTEXT("RPGEquipment", "Awaiting", "Waiting for server confirmation."); break;
		case ERPGEquipmentMutationState::RecoveryRequired: Result.Message = NSLOCTEXT("RPGEquipment", "Recovery", "The result is unresolved. Retry the original request before another equipment command."); break;
		case ERPGEquipmentMutationState::AuthenticationRequired: Result.Message = NSLOCTEXT("RPGEquipment", "Authentication", "Sign in to the same account to confirm the original request."); break;
		case ERPGEquipmentMutationState::StorageBlocked: Result.Message = NSLOCTEXT("RPGEquipment", "Storage", "The original equipment request could not be safely stored or restored. No new request was sent."); break;
		case ERPGEquipmentMutationState::Committed: Result.Message = NSLOCTEXT("RPGEquipment", "Confirmed", "Equipment command confirmed. Refresh inventory to read the latest items."); break;
		case ERPGEquipmentMutationState::Rejected: Result.Message = NSLOCTEXT("RPGEquipment", "Rejected", "The command was not applied. Refresh items before trying again."); break;
		case ERPGEquipmentMutationState::Unavailable: Result.Message = NSLOCTEXT("RPGEquipment", "Unavailable", "Player equipment commands are unavailable in this context."); break;
		default: break;
		}
		return Result;
	}
}

void URPGEquipmentMutationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection); Collection.InitializeDependency<UHttpWebManager>();
	JournalRoot = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("EquipmentMutations/v1")));
}

void URPGEquipmentMutationSubsystem::Deinitialize()
{
	bShuttingDown = true; ++AttemptSerial;
	CancelItemsRead();
	if (ActiveRequest) { ActiveRequest->OnProcessRequestComplete().Unbind(); ActiveRequest->CancelRequest(); ActiveRequest.Reset(); }
	// Cancellation/shutdown is not a rollback or a reason to delete the durable intent.
	Views.Reset(); Super::Deinitialize();
}

UHttpWebManager* URPGEquipmentMutationSubsystem::Web() const
{
#if WITH_DEV_AUTOMATION_TESTS
	if (TestManager.IsValid()) return TestManager.Get();
#endif
	return GetGameInstance() ? GetGameInstance()->GetSubsystem<UHttpWebManager>() : nullptr;
}

bool URPGEquipmentMutationSubsystem::ScopeFor(FGuid CharacterId, FRPGEquipmentMutationScope& OutScope) const
{
	FString Endpoint, SteamId; auto* Manager = Web();
	return !bShuttingDown && IsInGameThread() && Manager && Manager->GetPlayerEquipmentContext(Endpoint, SteamId)
		&& FRPGEquipmentMutationScope::Create(Endpoint, SteamId, CharacterId, OutScope);
}

void URPGEquipmentMutationSubsystem::Publish(const FRPGEquipmentMutationScope& Scope, FRPGEquipmentMutationView View)
{
	auto EqualItems = [](const TArray<FRPGInventoryProjectionEntry>& A, const TArray<FRPGInventoryProjectionEntry>& B)
	{ if (A.Num() != B.Num()) return false; for (int32 I = 0; I < A.Num(); ++I) if (!A[I].HasSamePayload(B[I])) return false; return true; };
	const auto* Previous = Views.Find(Scope.Key);
	const bool Changed = !Previous || Previous->CharacterId != View.CharacterId || Previous->RequestId != View.RequestId
		|| Previous->Operation != View.Operation || Previous->State != View.State || Previous->HttpStatus != View.HttpStatus
		|| Previous->AffectedQuantity != View.AffectedQuantity || Previous->bNeedsInventoryRefresh != View.bNeedsInventoryRefresh
		|| Previous->ItemsLoadState != View.ItemsLoadState || !EqualItems(Previous->Items, View.Items) || !EqualItems(Previous->MailItems, View.MailItems)
		|| !Previous->Message.EqualTo(View.Message);
	Views.Add(Scope.Key, MoveTemp(View)); FRPGEquipmentMutationScope Current;
	// No old character/account payload is broadcast into a new login context.
	if (Changed && ScopeFor(Scope.CharacterId, Current) && Current == Scope) OnEquipmentMutationsChanged.Broadcast();
}

FRPGEquipmentMutationView URPGEquipmentMutationSubsystem::GetState(FGuid CharacterId) const
{
	FRPGEquipmentMutationScope Scope;
	if (!ScopeFor(CharacterId, Scope)) return RPGEquipmentMutationUI::View(CharacterId, nullptr, ERPGEquipmentMutationState::Unavailable);
	const auto* Found = Views.Find(Scope.Key);
	auto Result = Found ? *Found : RPGEquipmentMutationUI::View(CharacterId, nullptr, ERPGEquipmentMutationState::Idle);
	if (!Web()->HasAccessToken())
	{
		const auto Auth = RPGEquipmentMutationUI::View(CharacterId, nullptr, ERPGEquipmentMutationState::AuthenticationRequired);
		Result.State = Auth.State; Result.Message = Auth.Message;
		Result.Items.Reset(); Result.MailItems.Reset(); Result.ItemsLoadState = ERPGInventoryProjectionLoadState::Failed;
	}
	return Result;
}

FRPGEquipmentMutationView URPGEquipmentMutationSubsystem::OpenCharacter(FGuid CharacterId)
{
	FRPGEquipmentMutationScope Scope;
	if (!ScopeFor(CharacterId, Scope)) return GetState(CharacterId);
	FRPGEquipmentPendingMutation Pending;
	const auto Loaded = FRPGEquipmentMutationJournal::Load(JournalRoot, Scope, Pending);
	const auto State = Loaded == ERPGEquipmentJournalRead::Missing ? ERPGEquipmentMutationState::Idle
		: Loaded == ERPGEquipmentJournalRead::Ready ? (ActiveRequest && ActiveScope == Scope ? ERPGEquipmentMutationState::AwaitingServer : ERPGEquipmentMutationState::RecoveryRequired)
		: ERPGEquipmentMutationState::StorageBlocked;
	Publish(Scope, RPGEquipmentMutationUI::View(CharacterId, Loaded == ERPGEquipmentJournalRead::Ready ? &Pending.Command : nullptr, State));
	return GetState(CharacterId);
}

bool URPGEquipmentMutationSubsystem::UpgradeEquipment(FGuid CharacterId, const FRPGEquipmentItemExpectation& Equipment,
	const TArray<FRPGEquipmentCostExpectation>& OreStacks)
{
	FRPGEquipmentMutationCommand Command; Command.CharacterId = CharacterId; Command.Operation = ERPGEquipmentMutationOperation::Upgrade;
	Command.Equipment = Equipment; Command.Costs = OreStacks; return Begin(MoveTemp(Command));
}

bool URPGEquipmentMutationSubsystem::TransferEquipment(FGuid CharacterId, const FRPGEquipmentItemExpectation& Source,
	const FRPGEquipmentItemExpectation& Target)
{
	FRPGEquipmentMutationCommand Command; Command.CharacterId = CharacterId; Command.Operation = ERPGEquipmentMutationOperation::Transfer;
	Command.Equipment = Source; Command.Target = Target; return Begin(MoveTemp(Command));
}

bool URPGEquipmentMutationSubsystem::CraftEquipment(FGuid CharacterId, const FString& TargetDefinitionName,
	const TArray<FRPGEquipmentCostExpectation>& TokenStacks)
{
	FRPGEquipmentMutationCommand Command; Command.CharacterId = CharacterId; Command.Operation = ERPGEquipmentMutationOperation::Craft;
	Command.TargetDefinitionName = TargetDefinitionName; Command.Costs = TokenStacks; return Begin(MoveTemp(Command));
}

bool URPGEquipmentMutationSubsystem::DismantleEquipment(FGuid CharacterId, const FRPGEquipmentItemExpectation& Equipment)
{
	FRPGEquipmentMutationCommand Command; Command.CharacterId = CharacterId; Command.Operation = ERPGEquipmentMutationOperation::Dismantle;
	Command.Equipment = Equipment; return Begin(MoveTemp(Command));
}

bool URPGEquipmentMutationSubsystem::Begin(FRPGEquipmentMutationCommand Command)
{
	FRPGEquipmentMutationScope Scope;
	if (ActiveRequest || !ScopeFor(Command.CharacterId, Scope)) return false;
	if (!Web()->HasAccessToken())
	{ Publish(Scope, RPGEquipmentMutationUI::View(Command.CharacterId, nullptr, ERPGEquipmentMutationState::AuthenticationRequired)); return false; }
	FRPGEquipmentPendingMutation Pending;
	const auto Loaded = FRPGEquipmentMutationJournal::Load(JournalRoot, Scope, Pending);
	if (Loaded != ERPGEquipmentJournalRead::Missing)
	{
		OpenCharacter(Command.CharacterId); return false; // Never silently substitute another ID/body/cost set.
	}
	Command.RequestId = FGuid::NewGuid(); FString Body, Fingerprint;
	if (!FRPGEquipmentMutationCodec::Encode(Command, Body, Fingerprint))
	{ Publish(Scope, RPGEquipmentMutationUI::View(Command.CharacterId, &Command, ERPGEquipmentMutationState::Rejected)); return false; }
	if (!FRPGEquipmentMutationJournal::Create(JournalRoot, Scope, Command))
	{
		if (FRPGEquipmentMutationJournal::Load(JournalRoot, Scope, Pending) == ERPGEquipmentJournalRead::Missing)
			Publish(Scope, RPGEquipmentMutationUI::View(Command.CharacterId, &Command, ERPGEquipmentMutationState::StorageBlocked));
		else OpenCharacter(Command.CharacterId);
		return false;
	}
	Pending.Scope = Scope; Pending.Command = MoveTemp(Command); Pending.Body = MoveTemp(Body); Pending.Fingerprint = MoveTemp(Fingerprint);
	CancelItemsRead();
	return Dispatch(Pending);
}

bool URPGEquipmentMutationSubsystem::RetryPending(FGuid CharacterId)
{
	FRPGEquipmentMutationScope Scope;
	if (ActiveRequest || !ScopeFor(CharacterId, Scope)) return false;
	FRPGEquipmentPendingMutation Pending;
	if (!Web()->HasAccessToken() || FRPGEquipmentMutationJournal::Load(JournalRoot, Scope, Pending) != ERPGEquipmentJournalRead::Ready)
	{ OpenCharacter(CharacterId); return false; }
	CancelItemsRead();
	return Dispatch(Pending);
}

void URPGEquipmentMutationSubsystem::CancelItemsRead()
{
	++ReadSerial;
	if (ActiveReadRequest) { ActiveReadRequest->OnProcessRequestComplete().Unbind(); ActiveReadRequest->CancelRequest(); ActiveReadRequest.Reset(); }
}

bool URPGEquipmentMutationSubsystem::RefreshItems(FGuid CharacterId)
{
	FRPGEquipmentMutationScope Scope;
	if (ActiveRequest || ActiveReadRequest || !ScopeFor(CharacterId, Scope) || !Web()->HasAccessToken()) return false;
	const uint64 Serial = ++ReadSerial; const TWeakObjectPtr<URPGEquipmentMutationSubsystem> WeakThis(this);
	ActiveReadRequest = Web()->CreatePlayerEquipmentItemsRequest(CharacterId,
		[WeakThis, Serial, Scope](FRPGEquipmentMutationHttpResult Result) { if (auto* Self = WeakThis.Get()) Self->CompleteItems(Serial, Scope, MoveTemp(Result)); });
	if (!ActiveReadRequest) return false;
	const FHttpRequestRef Wire = ActiveReadRequest.ToSharedRef();
	auto View = GetState(CharacterId); View.ItemsLoadState = ERPGInventoryProjectionLoadState::Loading; View.Items.Reset(); View.MailItems.Reset();
	Publish(Scope, MoveTemp(View)); FRPGEquipmentMutationScope Current;
	if (bShuttingDown || Serial != ReadSerial || ActiveReadRequest != Wire || !ScopeFor(CharacterId, Current) || !(Current == Scope) || !Web()->HasAccessToken())
	{ if (ActiveReadRequest == Wire) CancelItemsRead(); return true; }
	bool Started = false;
#if WITH_DEV_AUTOMATION_TESTS
	if (TestDispatch) Started = TestDispatch(Wire); else
#endif
	Started = Wire->ProcessRequest();
	if (!Started) CompleteItems(Serial, Scope, {});
	return true;
}

void URPGEquipmentMutationSubsystem::CompleteItems(uint64 Serial, FRPGEquipmentMutationScope Scope, FRPGEquipmentMutationHttpResult Result)
{
	if (bShuttingDown || Serial != ReadSerial || !ActiveReadRequest) return;
	ActiveReadRequest.Reset(); FRPGEquipmentMutationScope Current;
	if (!Result.bCurrentPlayerContext || !ScopeFor(Scope.CharacterId, Current) || !(Current == Scope)) return;
	auto View = GetState(Scope.CharacterId); View.ItemsLoadState = ERPGInventoryProjectionLoadState::Failed;
	View.Items.Reset(); View.MailItems.Reset(); TArray<FRPGItemRecord> Records;
	const FRPGItemOwnerRef Owner { ERPGItemOwnerType::Character, Scope.CharacterId.ToString(EGuidFormats::DigitsWithHyphensLower) };
	// The current API has no pagination cursor. Never present a possibly truncated page as a complete snapshot.
	if (Result.Response.bTransportSuccessful && Result.Response.StatusCode == 200
		&& FRPGItemBackendJsonCodec::DeserializeLoadResponse(Result.Response.Body, Records) && Records.Num() < 500
		&& FRPGInventoryProjectionMapper::BuildInventorySnapshot(Owner, Records, View.Items)
		&& FRPGInventoryProjectionMapper::BuildMailSnapshot(Owner, Records, View.MailItems))
	{ View.ItemsLoadState = ERPGInventoryProjectionLoadState::Ready; View.bNeedsInventoryRefresh = false; }
	else { View.Items.Reset(); View.MailItems.Reset(); }
	Publish(Scope, MoveTemp(View));
}

bool URPGEquipmentMutationSubsystem::Dispatch(const FRPGEquipmentPendingMutation& Pending)
{
	FRPGEquipmentMutationScope Current;
	if (ActiveRequest || !ScopeFor(Pending.Command.CharacterId, Current) || !(Current == Pending.Scope)) return false;
	const uint64 Attempt = ++AttemptSerial; const TWeakObjectPtr<URPGEquipmentMutationSubsystem> WeakThis(this);
	ActiveRequest = Web()->CreatePlayerEquipmentMutationRequest(Pending.Command,
		[WeakThis, Attempt, Scope = Pending.Scope, Command = Pending.Command](FRPGEquipmentMutationHttpResult Result) mutable
		{ if (auto* Self = WeakThis.Get()) Self->Complete(Attempt, MoveTemp(Scope), MoveTemp(Command), MoveTemp(Result)); });
	if (!ActiveRequest)
	{
		Publish(Pending.Scope, RPGEquipmentMutationUI::View(Pending.Command.CharacterId, &Pending.Command,
			Web()->HasAccessToken() ? ERPGEquipmentMutationState::RecoveryRequired : ERPGEquipmentMutationState::AuthenticationRequired));
		return true; // Intent is durable even if authentication/dispatch just became unavailable.
	}
	ActiveScope = Pending.Scope;
	const FHttpRequestRef WireRequest = ActiveRequest.ToSharedRef();
	Publish(Pending.Scope, RPGEquipmentMutationUI::View(Pending.Command.CharacterId, &Pending.Command, ERPGEquipmentMutationState::AwaitingServer));
	// Blueprint notifications can synchronously close a world or change accounts.
	if (bShuttingDown || AttemptSerial != Attempt || ActiveRequest != WireRequest
		|| !ScopeFor(Pending.Command.CharacterId, Current) || !(Current == Pending.Scope) || !Web()->HasAccessToken())
	{
		if (ActiveRequest == WireRequest) { ActiveRequest->OnProcessRequestComplete().Unbind(); ActiveRequest.Reset(); }
		return true; // The saved intent remains recoverable; do not dispatch stale UI work.
	}
	bool Started = false;
#if WITH_DEV_AUTOMATION_TESTS
	if (TestDispatch) Started = TestDispatch(WireRequest); else
#endif
	Started = WireRequest->ProcessRequest();
	if (!Started) Complete(Attempt, Pending.Scope, Pending.Command, {});
	return true;
}

void URPGEquipmentMutationSubsystem::Complete(uint64 Attempt, FRPGEquipmentMutationScope Scope,
	FRPGEquipmentMutationCommand Command, FRPGEquipmentMutationHttpResult Result)
{
	if (bShuttingDown || Attempt != AttemptSerial || !ActiveRequest) return;
	ActiveRequest.Reset(); FRPGEquipmentMutationScope Current;
	auto View = RPGEquipmentMutationUI::View(Command.CharacterId, &Command, ERPGEquipmentMutationState::RecoveryRequired);
	View.HttpStatus = Result.Response.StatusCode;
	if (Result.bCurrentPlayerContext && ScopeFor(Command.CharacterId, Current) && Current == Scope)
	{
		FRPGItemBackendCommitResult Receipt;
		const bool Applied = Result.Response.bTransportSuccessful && Result.Response.StatusCode == 200
			&& FRPGEquipmentMutationCodec::AcceptReceipt(Command, Result.Response.Body, Receipt);
		const bool Rejected = !Applied && FRPGEquipmentMutationCodec::IsDefinitiveRejection(Result.Response);
		if ((Applied || Rejected) && FRPGEquipmentMutationJournal::Clear(JournalRoot, Scope, Command))
		{
			View = RPGEquipmentMutationUI::View(Command.CharacterId, &Command, Applied ? ERPGEquipmentMutationState::Committed : ERPGEquipmentMutationState::Rejected);
			View.HttpStatus = Result.Response.StatusCode; View.AffectedQuantity = Applied ? Receipt.AffectedQuantity : 0;
			View.bNeedsInventoryRefresh = true;
			// Receipt records are historical snapshots. Never overwrite current owner
			// inventory, equipment, currency or GAS from this response.
		}
	}
	Publish(Scope, MoveTemp(View));
}
