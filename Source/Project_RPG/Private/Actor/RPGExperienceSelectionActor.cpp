#include "Actor/RPGExperienceSelectionActor.h"

#include "DataAsset/Definition/RPGExperienceDefinition.h"
#include "Engine/World.h"
#include "EngineUtils.h"

#if WITH_EDITOR
#include "Logging/MessageLog.h"
#include "Misc/DataValidation.h"
#include "Misc/UObjectToken.h"
#endif

ARPGExperienceSelectionActor::ARPGExperienceSelectionActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// AInfo already disables ticking, collision, spatial loading and data-layer membership.
	bNetLoadOnClient = false;
	bIsMainWorldOnly = true;
}

const ARPGExperienceSelectionActor* ARPGExperienceSelectionActor::FindForWorld(
	const UWorld& World, FString& OutError)
{
	OutError.Reset();
	const ARPGExperienceSelectionActor* Selected = nullptr;
	for (TActorIterator<ARPGExperienceSelectionActor> It(&World); It; ++It)
	{
		const ARPGExperienceSelectionActor* Candidate = *It;
		if (!Candidate->bEnabled || Candidate->IsActorBeingDestroyed()) { continue; }
		if (Candidate->GetLevel() != World.PersistentLevel)
		{
			OutError = FString::Printf(
				TEXT("Experience selection actor %s must belong to the persistent level."),
				*Candidate->GetPathName());
			return nullptr;
		}
		if (Selected)
		{
			OutError = FString::Printf(
				TEXT("Multiple enabled Experience selection actors: %s and %s. Enable only one."),
				*Selected->GetPathName(), *Candidate->GetPathName());
			return nullptr;
		}
		Selected = Candidate;
	}
	return Selected;
}

#if WITH_EDITOR
EDataValidationResult ARPGExperienceSelectionActor::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = CombineDataValidationResults(
		Super::IsDataValid(Context), EDataValidationResult::Valid);
	if (!bEnabled) { return Result; }

	if (const UWorld* World = GetWorld())
	{
		FString SelectionError;
		if (GetLevel() != World->PersistentLevel)
		{
			SelectionError = TEXT("Experience selection must be placed in the persistent level.");
		}
		else
		{
			FindForWorld(*World, SelectionError);
		}
		if (!SelectionError.IsEmpty())
		{
			Context.AddError(FText::FromString(SelectionError));
			Result = EDataValidationResult::Invalid;
		}
	}

	switch (SelectionMode)
	{
	case ERPGExperienceSelectionMode::DisableExperience:
		return Result;
	case ERPGExperienceSelectionMode::LoadExperience:
		break;
	default:
		Context.AddError(FText::FromString(TEXT("Unknown Experience selection mode.")));
		return EDataValidationResult::Invalid;
	}

	UObject* Asset = Experience.ToSoftObjectPath().TryLoad();
	if (UClass* Class = Cast<UClass>(Asset))
	{
		Asset = Class->IsChildOf(URPGExperienceDefinition::StaticClass())
			&& !Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated)
			? Class->GetDefaultObject() : nullptr;
	}
	const URPGExperienceDefinition* Definition = Cast<URPGExperienceDefinition>(Asset);
	if (!Definition)
	{
		Context.AddError(FText::FromString(
			TEXT("LoadExperience requires an existing RPGExperienceDefinition asset.")));
		return EDataValidationResult::Invalid;
	}
	return CombineDataValidationResults(Result, Definition->IsDataValid(Context));
}

void ARPGExperienceSelectionActor::CheckForErrors()
{
	Super::CheckForErrors();
	FDataValidationContext Context;
	IsDataValid(Context);
	TArray<FText> Warnings;
	TArray<FText> Errors;
	Context.SplitIssues(Warnings, Errors);
	for (const FText& Error : Errors)
	{
		FMessageLog("MapCheck").Error()
			->AddToken(FUObjectToken::Create(this))
			->AddToken(FTextToken::Create(Error));
	}
	for (const FText& Warning : Warnings)
	{
		FMessageLog("MapCheck").Warning()
			->AddToken(FUObjectToken::Create(this))
			->AddToken(FTextToken::Create(Warning));
	}
}
#endif
