#include "Manager/Component/RPGExperienceManagerComponent.h"

#include "DataAsset/Definition/RPGExperienceActionSet.h"
#include "DataAsset/Definition/RPGExperienceDefinition.h"
#include "Engine/AssetManager.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Engine.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "GameFeatureAction.h"
#include "GameFeaturePluginOperationResult.h"
#include "GameFeatureStateHandle.h"
#include "GameFeatureStateHandleReferenceController.h"
#include "GameFeaturesSubsystem.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "LoadingScreenManager.h"
#include "LoadingProcessTask.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Net/UnrealNetwork.h"
#include "Settings/RPGExperienceSettings.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/GarbageCollection.h"
#include "UI/Loading/RPGExperienceFailureNotice.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Containers/Ticker.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogRPGExperience, Log, All);

/** Retains feature ownership and action objects until asynchronous teardown completes. */
struct FRPGExperienceSession : TSharedFromThis<FRPGExperienceSession>
{
	TStrongObjectPtr<URPGExperienceDefinition> Definition;
	TArray<TStrongObjectPtr<UGameFeatureAction>> Actions;
	TSharedPtr<FStreamableHandle> DefinitionHandle;
	TSharedPtr<FStreamableHandle> AssetsHandle;
	FGameFeatureStateHandle FeatureHandle;
	FName WorldContextHandle;
	FGuid SessionId = FGuid::NewGuid();
	FString WorldName;
	int32 PendingFeatures = 0;
	int32 Registered = 0;
	int32 Loaded = 0;
	int32 Activated = 0;
	int32 ExpectedPausers = 0;
	int32 CompletedPausers = 0;
	bool bEnding = false;
	bool bCollectingPausers = false;
	bool bFinished = false;

	void TryFinish()
	{
		if (!bEnding || bCollectingPausers || bFinished || PendingFeatures > 0
			|| CompletedPausers < ExpectedPausers)
		{
			return;
		}
		bFinished = true;
		for (int32 Index = Loaded - 1; Index >= 0; --Index)
		{
			Actions[Index]->OnGameFeatureUnloading();
		}
		for (int32 Index = Registered - 1; Index >= 0; --Index)
		{
			Actions[Index]->OnGameFeatureUnregistering();
		}
		UE_LOG(LogRPGExperience, Display,
			TEXT("RPG_EXPERIENCE ACTIONS_RELEASED Session=%s World=%s Actions=%d Pausers=%d"),
			*SessionId.ToString(), *WorldName, Registered, ExpectedPausers);
		TSharedRef<FRPGExperienceSession> KeepAlive = AsShared();
		FeatureHandle.ReleaseAndUnregister([KeepAlive](bool)
		{
			KeepAlive->Actions.Reset();
			KeepAlive->AssetsHandle.Reset();
			KeepAlive->DefinitionHandle.Reset();
			KeepAlive->Definition.Reset();
			UE_LOG(LogRPGExperience, Display,
				TEXT("RPG_EXPERIENCE OWNERSHIP_RELEASED Session=%s World=%s"),
				*KeepAlive->SessionId.ToString(), *KeepAlive->WorldName);
		});
	}

	void End()
	{
		if (bEnding) { return; }
		bEnding = true;
		UE_LOG(LogRPGExperience, Display,
			TEXT("RPG_EXPERIENCE END Session=%s World=%s PendingFeatures=%d Activated=%d"),
			*SessionId.ToString(), *WorldName, PendingFeatures, Activated);
		bCollectingPausers = true;
		TSharedRef<FRPGExperienceSession> KeepAlive = AsShared();
		FGameFeatureDeactivatingContext Context(TEXT("RPGExperience"),
			[KeepAlive](FStringView)
			{
				++KeepAlive->CompletedPausers;
				KeepAlive->TryFinish();
			});
		Context.SetRequiredWorldContextHandle(WorldContextHandle);
		for (int32 Index = Activated - 1; Index >= 0; --Index)
		{
			Actions[Index]->OnGameFeatureDeactivating(Context);
		}
		ExpectedPausers = Context.GetNumPausers();
		bCollectingPausers = false;
		TryFinish();
	}
};

URPGExperienceManagerComponent::URPGExperienceManagerComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetIsReplicatedByDefault(true);
	PrimaryComponentTick.bCanEverTick = false;
}

void URPGExperienceManagerComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(URPGExperienceManagerComponent, SelectedExperiencePath);
	DOREPLIFETIME(URPGExperienceManagerComponent, bServerExperienceFailed);
}

void URPGExperienceManagerComponent::BeginPlay()
{
	Super::BeginPlay();
	if (!GetOwner()->HasAuthority() && bServerExperienceFailed)
	{
		Fail(TEXT("The server could not prepare this game."));
		return;
	}
	if (GetOwner()->HasAuthority() && !SelectedExperiencePath.IsValid())
	{
		FString Override = GetWorld()->URL.GetOption(TEXT("RPGExperience="), TEXT(""));
#if !UE_BUILD_SHIPPING
		if (Override.IsEmpty())
		{
			FParse::Value(FCommandLine::Get(), TEXT("-RPGExperience="), Override);
		}
#endif
		if (!Override.IsEmpty())
		{
			const FSoftObjectPath Path = Override.StartsWith(TEXT("/"))
				? FSoftObjectPath(Override)
				: UAssetManager::Get().GetPrimaryAssetPath(FPrimaryAssetId(Override));
			if (!SetExperiencePath(Path))
			{
				Fail(FString::Printf(TEXT("Invalid experience: %s"), *Override));
			}
		}
		else
		{
			FSoftObjectPath Path;
			FString SelectionError;
			if (!GetDefault<URPGExperienceSettings>()->TryResolveForWorld(*GetWorld(), Path, SelectionError))
			{
				Fail(SelectionError);
			}
			else if (Path.IsValid() && !SetExperiencePath(Path))
			{
				Fail(FString::Printf(TEXT("Could not select Experience: %s"), *Path.ToString()));
			}
		}
	}
	if (SelectedExperiencePath.IsValid() && LoadState == ERPGExperienceLoadState::Unassigned)
	{
		StartLoading();
	}
}

bool URPGExperienceManagerComponent::SetExperience(TSoftObjectPtr<URPGExperienceDefinition> Experience)
{
	return SetExperiencePath(Experience.ToSoftObjectPath());
}

bool URPGExperienceManagerComponent::SetExperienceById(FPrimaryAssetId ExperienceId)
{
	return SetExperiencePath(UAssetManager::Get().GetPrimaryAssetPath(ExperienceId));
}

bool URPGExperienceManagerComponent::SetExperiencePath(const FSoftObjectPath& Path)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !Path.IsValid()
		|| LoadState != ERPGExperienceLoadState::Unassigned || SelectedExperiencePath.IsValid())
	{
		return false;
	}
	SelectedExperiencePath = Path;
	GetOwner()->ForceNetUpdate();
	if (HasBegunPlay()) { StartLoading(); }
	return true;
}

void URPGExperienceManagerComponent::OnRep_SelectedExperiencePath()
{
	if (HasBegunPlay() && LoadState == ERPGExperienceLoadState::Unassigned
		&& SelectedExperiencePath.IsValid())
	{
		StartLoading();
	}
}

void URPGExperienceManagerComponent::OnRep_ServerExperienceFailed()
{
    if (bServerExperienceFailed && HasBegunPlay())
    {
        Fail(TEXT("The server could not prepare this game."));
    }
}

bool URPGExperienceManagerComponent::IsCurrent(uint64 Generation) const
{
	return Generation == LoadGeneration && LoadState != ERPGExperienceLoadState::Failed
		&& LoadState != ERPGExperienceLoadState::Deactivating;
}

void URPGExperienceManagerComponent::StartLoading()
{
	LoadState = ERPGExperienceLoadState::Loading;
	const uint64 Generation = ++LoadGeneration;
	const float ConfiguredTimeout = GetDefault<URPGExperienceSettings>()->LoadTimeoutSeconds;
	const float SafeTimeout = FMath::IsFinite(ConfiguredTimeout)
		? FMath::Max(1.f, ConfiguredTimeout) : 60.f;
	GetWorld()->GetTimerManager().SetTimer(TimeoutTimer,
		FTimerDelegate::CreateWeakLambda(this, [this, Generation]
		{
			if (IsCurrent(Generation) && !IsExperienceLoaded())
			{
				Fail(TEXT("Experience loading timed out."));
			}
		}),
		SafeTimeout, false);
	AcquireLoadingScreen();
	DefinitionHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		SelectedExperiencePath,
		FStreamableDelegate::CreateWeakLambda(this, [this, Generation] { OnDefinitionLoaded(Generation); }));
	if (!DefinitionHandle.IsValid()) { Fail(TEXT("Could not load experience definition.")); }
}

void URPGExperienceManagerComponent::AcquireLoadingScreen()
{
	UWorld* World = GetWorld();
	if (LoadingScreenTask || !GEngine || !World || !World->IsGameWorld()
		|| World->bIsTearingDown || World->GetNetMode() == NM_DedicatedServer
		|| !SelectedExperiencePath.IsValid()
		|| !GetDefault<URPGExperienceSettings>()->bShowLoadingScreenWhileLoading)
	{
		return;
	}
	LoadingScreenTask = ULoadingProcessTask::CreateLoadingScreenProcessTask(this,
		FString::Printf(TEXT("Preparing RPG Experience: %s"), *SelectedExperiencePath.ToString()));
	if (LoadingScreenTask)
	{
		UE_LOG(LogRPGExperience, Display,
			TEXT("RPG_EXPERIENCE SCREEN_WAIT World=%s Definition=%s"),
			*World->GetName(), *SelectedExperiencePath.ToString());
	}
}

void URPGExperienceManagerComponent::ReleaseLoadingScreen(const TCHAR* Reason)
{
	ULoadingProcessTask* Task = LoadingScreenTask.Get();
	// Drop our ownership before unregistering; repeated Stop/EndPlay remains harmless.
	LoadingScreenTask = nullptr;
	if (IsValid(Task))
	{
		Task->Unregister();
		UE_LOG(LogRPGExperience, Display,
			TEXT("RPG_EXPERIENCE SCREEN_RELEASED World=%s Definition=%s Reason=%s"),
			*GetNameSafe(GetWorld()), *SelectedExperiencePath.ToString(), Reason);
	}
}

void URPGExperienceManagerComponent::OnDefinitionLoaded(uint64 Generation)
{
	if (!IsCurrent(Generation)) { return; }
	UObject* Object = SelectedExperiencePath.ResolveObject();
	if (UClass* Class = Cast<UClass>(Object))
	{
		if (Class->IsChildOf(URPGExperienceDefinition::StaticClass()))
		{
			Object = Class->GetDefaultObject();
		}
	}
	CurrentExperience = Cast<URPGExperienceDefinition>(Object);
	if (!CurrentExperience) { Fail(TEXT("Selected asset is not an RPG experience.")); return; }
	Session = MakeShared<FRPGExperienceSession>();
	Session->WorldName = GetWorld()->GetName();
	Session->Definition.Reset(const_cast<URPGExperienceDefinition*>(CurrentExperience.Get()));
	Session->DefinitionHandle = MoveTemp(DefinitionHandle);
	const FWorldContext* Context = GEngine->GetWorldContextFromWorld(GetWorld());
	if (!Context) { Fail(TEXT("Experience has no world context.")); return; }
	Session->WorldContextHandle = Context->ContextHandle;
	LoadFeatures(Generation);
}

void URPGExperienceManagerComponent::LoadFeatures(uint64 Generation)
{
	TArray<FString> Names = CurrentExperience->GameFeaturesToEnable;
	TSet<const URPGExperienceActionSet*> UniqueSets;
	for (const URPGExperienceActionSet* Set : CurrentExperience->ActionSets)
	{
		if (!Set) { Fail(TEXT("Experience contains a null ActionSet.")); return; }
		if (UniqueSets.Contains(Set))
		{
			Fail(FString::Printf(TEXT("Experience repeats ActionSet: %s"), *Set->GetPathName()));
			return;
		}
		UniqueSets.Add(Set);
		for (const FString& Name : Set->GameFeaturesToEnable) { Names.AddUnique(Name); }
	}
	TArray<FString> URLs;
	for (const FString& Name : Names)
	{
		FString URL;
		if (Name.IsEmpty() || !UGameFeaturesSubsystem::Get().GetPluginURLByName(Name, URL))
		{
			Fail(FString::Printf(TEXT("Unknown GameFeature: %s"), *Name));
			return;
		}
		URLs.AddUnique(URL);
	}
	if (URLs.IsEmpty()) { LoadAssets(Generation); return; }
	TSharedRef<FRPGExperienceSession> Active = Session.ToSharedRef();
	Active->FeatureHandle.InitAndRegister(GetPathName(),
		EGameFeatureStateHandleOptions::TrackDependencies | EGameFeatureStateHandleOptions::KeepRegistered);
	if (!Active->FeatureHandle.IsValid())
	{
		Fail(TEXT("GameFeatureStateHandle lifetime control is disabled."));
		return;
	}
	FGameFeatureStateHandleReferenceController& Controller =
		FGameFeatureStateHandleReferenceController::Get();
	for (const FString& URL : URLs)
	{
		Controller.AddOrUpdateReference(Active->FeatureHandle, URL, EGameFeaturePluginState::Active);
	}
	Active->PendingFeatures = URLs.Num();
	int32 Issued = 0;
	TWeakObjectPtr<URPGExperienceManagerComponent> WeakThis(this);
	for (const FString& URL : URLs)
	{
		if (!IsCurrent(Generation))
		{
			Active->PendingFeatures -= URLs.Num() - Issued;
			Active->TryFinish();
			break;
		}
		++Issued;
		Controller.LoadAndActivateGameFeaturePlugin(Active->FeatureHandle, URL,
			FGameFeatureProtocolOptions(),
			UGameFeatureStateHandleLoadComplete::CreateLambda(
				[WeakThis, Active, Generation, URL](const UE::GameFeatures::FResult& Result)
				{
                    auto Complete = [WeakThis, Active, Generation, URL](const UE::GameFeatures::FResult& Result)
                    {
                        --Active->PendingFeatures;
                        if (WeakThis.IsValid() && WeakThis->IsCurrent(Generation))
                        {
                            if (Result.HasError())
                            {
                                WeakThis->Fail(FString::Printf(TEXT("GameFeature %s: %s"),
                                    *URL, *Result.GetError()));
                            }
                            else if (Active->PendingFeatures == 0)
                            {
                                WeakThis->LoadAssets(Generation);
                            }
                        }
                        Active->TryFinish();
                    };
#if WITH_DEV_AUTOMATION_TESTS
                    // Opt-in proof: deliver this old world's feature completions after travel.
                    // The normal completion body and Shipping behavior stay synchronous here.
                    UWorld* ProofWorld = WeakThis.IsValid() ? WeakThis->GetWorld() : nullptr;
                    FString ProofOutput, ProofRole, ProofCase;
                    FParse::Value(FCommandLine::Get(), TEXT("RPGExperienceNetworkProofOutput="), ProofOutput);
                    FParse::Value(FCommandLine::Get(), TEXT("RPGExperienceNetworkProofRole="), ProofRole);
                    FParse::Value(FCommandLine::Get(), TEXT("RPGExperienceNetworkProofCase="), ProofCase);
                    if (FParse::Param(FCommandLine::Get(), TEXT("RPGExperienceNetworkProof"))
                        && ProofWorld && ProofWorld->GetPackage()->GetName() == TEXT("/Game/System/Experiences/Development/Maps/L_ExperienceDevelopment")
                        && (ProofCase == TEXT("LoadingTravel") || ProofCase == TEXT("LoadingSeamlessTravel"))
                        && (ProofRole == TEXT("Server") || ProofRole == TEXT("Client") || ProofRole == TEXT("LateClient"))
                        && FPaths::IsUnderDirectory(ProofOutput, FPaths::Combine(FPaths::ProjectDir(), TEXT("Saved"))))
                    {
                        // Initial server URL options are not forwarded to joining clients.
                        // A run-specific Session marker selects only the first world's completions.
                        const FString HeldFile = FPaths::Combine(ProofOutput, ProofRole, TEXT("DeferredStarted.txt"));
                        FString HeldSession;
                        FFileHelper::LoadFileToString(HeldSession, *HeldFile);
                        HeldSession.TrimStartAndEndInline();
                        if (HeldSession.IsEmpty() || HeldSession == Active->SessionId.ToString())
                        {
                            FFileHelper::SaveStringToFile(Active->SessionId.ToString(),
                                *FPaths::Combine(ProofOutput, ProofRole, TEXT("DeferredStarted.txt")));
                            UE_LOG(LogRPGExperience, Display,
                                TEXT("RPG_EXPERIENCE PROOF_DEFERRED Session=%s Role=%s Pending=%d"),
                                *Active->SessionId.ToString(), *ProofRole, Active->PendingFeatures);
                            FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
                                [Complete, DeferredResult = Result, Active, WeakThis, Generation, ProofOutput, ProofRole](float)
                                {
                                    if (IFileManager::Get().FileSize(*FPaths::Combine(ProofOutput, TEXT("Control"), TEXT("ReleaseDeferred.txt"))) <= 0) { return true; }
                                    const bool bWasCurrent = WeakThis.IsValid() && WeakThis->IsCurrent(Generation);
                                    Complete(DeferredResult);
                                    UE_LOG(LogRPGExperience, Display,
                                        TEXT("RPG_EXPERIENCE PROOF_DEFERRED_RELEASE Session=%s Role=%s Current=%d Ending=%d Pending=%d"),
                                        *Active->SessionId.ToString(), *ProofRole, bWasCurrent, Active->bEnding, Active->PendingFeatures);
                                    if (Active->bEnding && Active->PendingFeatures == 0)
                                    {
                                        FFileHelper::SaveStringToFile(Active->SessionId.ToString(),
                                            *FPaths::Combine(ProofOutput, ProofRole, TEXT("DeferredCompleted.txt")));
                                    }
                                    return false;
                                }));
                            return;
                        }
                    }
#endif
                    Complete(Result);
				}));
	}
}

void URPGExperienceManagerComponent::LoadAssets(uint64 Generation)
{
	if (!IsCurrent(Generation)) { return; }
	UAssetManager& Manager = UAssetManager::Get();
	TArray<FPrimaryAssetId> IDs;
	IDs.Add(CurrentExperience->GetPrimaryAssetId());
	for (const URPGExperienceActionSet* Set : CurrentExperience->ActionSets)
	{
		IDs.AddUnique(Set->GetPrimaryAssetId());
	}
	TArray<FName> Roles;
	if (GetNetMode() != NM_DedicatedServer) { Roles.Add(TEXT("Client")); }
	if (GetNetMode() != NM_Client) { Roles.Add(TEXT("Server")); }
	FAssetBundleData Bundles;
	for (const FPrimaryAssetId& ID : IDs)
	{
		for (FName Role : Roles)
		{
			const FAssetBundleEntry Entry = Manager.GetAssetBundleEntry(ID, Role);
			Bundles.AddBundleAssets(Role, Entry.AssetPaths);
		}
	}
	Manager.RecursivelyExpandBundleData(Bundles);
	TArray<FSoftObjectPath> Paths;
	for (const FAssetBundleEntry& Bundle : Bundles.Bundles)
	{
		if (Roles.Contains(Bundle.BundleName))
		{
			for (const FTopLevelAssetPath& Path : Bundle.AssetPaths)
			{
				Paths.AddUnique(FSoftObjectPath(Path));
			}
		}
	}
	if (Paths.IsEmpty()) { ActivateActions(Generation); return; }
	TSharedRef<FRPGExperienceSession> Active = Session.ToSharedRef();
	Active->AssetsHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(Paths,
		FStreamableDelegate::CreateWeakLambda(this, [this, Generation, Paths]
		{
			if (!IsCurrent(Generation)) { return; }
			for (const FSoftObjectPath& Path : Paths)
			{
				if (!Path.ResolveObject())
				{
					Fail(FString::Printf(TEXT("Missing action asset: %s"), *Path.ToString()));
					return;
				}
			}
			ActivateActions(Generation);
		}));
	if (!Active->AssetsHandle.IsValid()) { Fail(TEXT("Could not load action assets.")); }
}

void URPGExperienceManagerComponent::ActivateActions(uint64 Generation)
{
	if (!IsCurrent(Generation)) { return; }
	TArray<UGameFeatureAction*> Templates;
	for (UGameFeatureAction* Action : CurrentExperience->Actions) { Templates.Add(Action); }
	for (const URPGExperienceActionSet* Set : CurrentExperience->ActionSets)
	{
		for (UGameFeatureAction* Action : Set->Actions) { Templates.Add(Action); }
	}
	TSharedRef<FRPGExperienceSession> Active = Session.ToSharedRef();
	for (UGameFeatureAction* Template : Templates)
	{
		if (!Template) { Fail(TEXT("Experience contains a null action.")); return; }
		Active->Actions.Emplace(DuplicateObject<UGameFeatureAction>(
			Template, GetTransientPackage(),
			MakeUniqueObjectName(GetTransientPackage(), Template->GetClass(), Template->GetFName())));
	}
	FGameFeatureActivatingContext Context;
	Context.SetRequiredWorldContextHandle(Active->WorldContextHandle);
	for (const TStrongObjectPtr<UGameFeatureAction>& Action : Active->Actions)
	{
		if (!IsCurrent(Generation)) { return; }
		++Active->Registered;
		Action->OnGameFeatureRegistering();
		if (!IsCurrent(Generation)) { return; }
		++Active->Loaded;
		Action->OnGameFeatureLoading();
		if (!IsCurrent(Generation)) { return; }
		++Active->Activated;
		Action->OnGameFeatureActivating(Context);
		if (!IsCurrent(Generation)) { return; }
		Action->OnGameFeatureActivated();
		if (!IsCurrent(Generation)) { return; }
	}
	GetWorld()->GetTimerManager().ClearTimer(TimeoutTimer);
	LoadState = ERPGExperienceLoadState::Loaded;
	ReleaseLoadingScreen(TEXT("Ready"));
	UE_LOG(LogRPGExperience, Display,
		TEXT("RPG_EXPERIENCE READY World=%s Definition=%s NetMode=%d Session=%s"),
		*GetWorld()->GetName(), *SelectedExperiencePath.ToString(),
		int32(GetNetMode()), *Active->SessionId.ToString());
	TArray<FSimpleDelegate> Callbacks = MoveTemp(ReadyCallbacks);
	OnExperienceLoaded.Broadcast(CurrentExperience);
	for (FSimpleDelegate& Callback : Callbacks)
	{
		if (!IsCurrent(Generation)) { break; }
		Callback.ExecuteIfBound();
	}
}

void URPGExperienceManagerComponent::CallOrRegisterOnExperienceLoaded(FSimpleDelegate Callback)
{
	if (IsExperienceLoaded()) { Callback.ExecuteIfBound(); }
	else if (Callback.IsBound() && LoadState != ERPGExperienceLoadState::Failed
		&& LoadState != ERPGExperienceLoadState::Deactivating)
	{
		ReadyCallbacks.Add(MoveTemp(Callback));
	}
}

void URPGExperienceManagerComponent::Stop()
{
	ClearFailureNotices();
	++LoadGeneration;
	ReleaseLoadingScreen(LoadState == ERPGExperienceLoadState::Failed ? TEXT("Failed") : TEXT("WorldEnding"));
	if (UWorld* World = GetWorld()) { World->GetTimerManager().ClearTimer(TimeoutTimer); }
	if (DefinitionHandle.IsValid())
	{
		DefinitionHandle->CancelHandle();
		DefinitionHandle.Reset();
	}
	ReadyCallbacks.Reset();
	if (Session.IsValid())
	{
		TSharedRef<FRPGExperienceSession> Old = Session.ToSharedRef();
		Session.Reset();
		Old->End();
	}
	CurrentExperience = nullptr;
}

void URPGExperienceManagerComponent::Fail(const FString& Reason)
{
    AActor* Owner = GetOwner();
    if (LoadState == ERPGExperienceLoadState::Failed || LoadState == ERPGExperienceLoadState::Deactivating
        || !IsValid(Owner) || Owner->IsActorBeingDestroyed() || IsGarbageCollecting()
        || IsUnreachable() || HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed))
    {
        return;
    }

    TStrongObjectPtr<URPGExperienceManagerComponent> KeepAlive(this);
    const FString ReasonCopy(Reason);
    LastError = ReasonCopy;
    LoadState = ERPGExperienceLoadState::Failed;
    if (Owner->HasAuthority())
    {
        bServerExperienceFailed = true;
        Owner->ForceNetUpdate();
    }
    Stop();
    UE_LOG(LogRPGExperience, Error, TEXT("RPG_EXPERIENCE FAILED Definition=%s Reason=%s"),
        *SelectedExperiencePath.ToString(), *ReasonCopy);
    const UWorld* World = GetWorld();
    if (LoadState != ERPGExperienceLoadState::Failed || !World || World->bIsTearingDown) { return; }

    QueueFailureNotices();
    if (LoadState == ERPGExperienceLoadState::Failed && !World->bIsTearingDown)
    {
        OnExperienceLoadFailed.Broadcast(ReasonCopy);
    }
}

void URPGExperienceManagerComponent::QueueFailureNotices()
{
    UWorld* World = GetWorld();
    if (LoadState != ERPGExperienceLoadState::Failed || !World || !World->IsGameWorld()
        || World->bIsTearingDown || World->GetNetMode() == NM_DedicatedServer
        || !GetDefault<URPGExperienceSettings>()->bShowLoadingFailureNotice)
    {
        return;
    }


    UGameInstance* Instance = World->GetGameInstance();
    ULoadingScreenManager* Loading = IsValid(Instance) ? Instance->GetSubsystem<ULoadingScreenManager>() : nullptr;
    if (IsValid(Loading) && !LoadingVisibilityHandle.IsValid())
    {
        FailureNoticeLoadingManager = Loading;
        LoadingVisibilityHandle = Loading->OnLoadingScreenVisibilityChangedDelegate().AddWeakLambda(this,
            [this](bool bVisible)
            {
                // Show is broadcast before Slate draws the global screen. Hide our existing notices immediately.
                // Hide is broadcast before the subsystem updates its status; the timer handles showing afterwards.
                if (bVisible) { HideFailureNoticesForLoadingScreen(); }
            });
    }

    // Controllers may arrive after a selection failure or after the loading screen closes.
    World->GetTimerManager().SetTimer(FailureNoticeTimer,
        FTimerDelegate::CreateWeakLambda(this, [this] { RefreshFailureNotices(); }), 0.25f, true);
    RefreshFailureNotices();
}

void URPGExperienceManagerComponent::RefreshFailureNotices()
{
    if (bRefreshingFailureNotices || IsGarbageCollecting() || IsUnreachable()
        || HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed))
    {
        return;
    }
    UWorld* World = GetWorld();
    const URPGExperienceSettings* Settings = GetDefault<URPGExperienceSettings>();
    if (LoadState != ERPGExperienceLoadState::Failed || !World || !World->IsGameWorld()
        || World->bIsTearingDown || World->GetNetMode() == NM_DedicatedServer
        || !Settings->bShowLoadingFailureNotice)
    {
        ClearFailureNotices();
        return;
    }
    UGameInstance* Instance = World->GetGameInstance();
    if (!IsValid(Instance)) { return; }

    TStrongObjectPtr<URPGExperienceManagerComponent> KeepAlive(this);
    TGuardValue<bool> RefreshingGuard(bRefreshingFailureNotices, true);
    const TArray<ULocalPlayer*> Players = Instance->GetLocalPlayers();
    const ULoadingScreenManager* Loading = Instance->GetSubsystem<ULoadingScreenManager>();
    const bool bLoadingScreenVisible = Loading && Loading->GetLoadingScreenDisplayStatus();

    for (int32 Index = FailureNotices.Num() - 1; Index >= 0; --Index)
    {
        URPGExperienceFailureNotice* Notice = FailureNotices[Index];
        ULocalPlayer* Player = IsValid(Notice) ? Notice->GetOwningLocalPlayer() : nullptr;
        APlayerController* Controller = IsValid(Notice) ? Notice->GetOwningPlayer() : nullptr;
        if (!IsValid(Notice) || !Notice->IsInViewport() || !IsValid(Player)
            || !Players.Contains(Player) || !IsValid(Controller) || Controller->IsActorBeingDestroyed()
            || Controller->GetWorld() != World || Player->GetPlayerController(World) != Controller)
        {
            TStrongObjectPtr<URPGExperienceFailureNotice> KeepNotice(IsValid(Notice) ? Notice : nullptr);
            FailureNotices.RemoveAt(Index);
            if (IsValid(Notice)) { Notice->RemoveFromParent(); }
        }
        else
        {
            Notice->SetVisibility(bLoadingScreenVisible
                ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
        }
    }
    // The loading subsystem owns its own blocking input and minimum display time.
    if (bLoadingScreenVisible) { return; }

    for (ULocalPlayer* Player : Players)
    {
        if (!IsValid(Player)) { continue; }
        APlayerController* Controller = Player->GetPlayerController(World);
        if (!IsValid(Controller) || Controller->IsActorBeingDestroyed() || !Controller->IsLocalController()
            || Controller->GetWorld() != World || Controller->GetLocalPlayer() != Player)
        {
            continue;
        }
        const bool bAlreadyShown = FailureNotices.ContainsByPredicate(
            [Player](const TObjectPtr<URPGExperienceFailureNotice>& Notice)
            {
                return IsValid(Notice) && Notice->GetOwningLocalPlayer() == Player;
            });
        if (bAlreadyShown) { continue; }
        URPGExperienceFailureNotice* Notice =
            CreateWidget<URPGExperienceFailureNotice>(Controller, URPGExperienceFailureNotice::StaticClass());
        if (!Notice) { continue; }
        Notice->SetNoticeMessage(Settings->LoadingFailureMessage);
        FailureNotices.Add(Notice);
        if (!Notice->AddToPlayerScreen(9000))
        {
            FailureNotices.Remove(Notice);
            continue;
        }
        UE_LOG(LogRPGExperience, Display, TEXT("RPG_EXPERIENCE FAILURE_NOTICE World=%s Player=%s"),
            *World->GetName(), *Player->GetName());
    }
}


void URPGExperienceManagerComponent::HideFailureNoticesForLoadingScreen()
{
    UWorld* World = GetWorld();
    if (LoadState != ERPGExperienceLoadState::Failed || !World || World->bIsTearingDown
        || IsGarbageCollecting() || IsUnreachable() || HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed))
    {
        return;
    }
    TStrongObjectPtr<URPGExperienceManagerComponent> KeepAlive(this);
    TArray<TStrongObjectPtr<URPGExperienceFailureNotice>> Notices;
    for (URPGExperienceFailureNotice* Notice : FailureNotices)
    {
        if (IsValid(Notice)) { Notices.Emplace(Notice); }
    }
    for (const TStrongObjectPtr<URPGExperienceFailureNotice>& Notice : Notices)
    {
        if (LoadState != ERPGExperienceLoadState::Failed || World->bIsTearingDown) { return; }
        if (IsValid(Notice.Get())) { Notice->SetVisibility(ESlateVisibility::Collapsed); }
    }
}

void URPGExperienceManagerComponent::ClearFailureNotices()
{
    if (ULoadingScreenManager* Loading = FailureNoticeLoadingManager.Get())
    {
        Loading->OnLoadingScreenVisibilityChangedDelegate().Remove(LoadingVisibilityHandle);
    }
    LoadingVisibilityHandle.Reset();
    FailureNoticeLoadingManager.Reset();
    if (UWorld* World = GetWorld()) { World->GetTimerManager().ClearTimer(FailureNoticeTimer); }
    TArray<TObjectPtr<URPGExperienceFailureNotice>> Removed = MoveTemp(FailureNotices);
    // Keep callbacks from collecting subsequent widgets during normal world teardown.
    TArray<TStrongObjectPtr<URPGExperienceFailureNotice>> KeepNotices;
    if (!IsGarbageCollecting() && !IsUnreachable() && !HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed))
    {
        for (URPGExperienceFailureNotice* Notice : Removed)
        {
            if (IsValid(Notice)) { KeepNotices.Emplace(Notice); }
        }
    }
    for (URPGExperienceFailureNotice* Notice : Removed)
    {
        if (IsValid(Notice)) { Notice->RemoveFromParent(); }
    }
}

void URPGExperienceManagerComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	LoadState = ERPGExperienceLoadState::Deactivating;
	Stop();
	OnExperienceEnded.Broadcast();
	Super::EndPlay(Reason);
}
