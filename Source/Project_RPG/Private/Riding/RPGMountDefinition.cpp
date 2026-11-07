#include "Riding/RPGMountDefinition.h"
#include "Riding/RPGRidingTypes.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

const FPrimaryAssetType URPGMountDefinition::PrimaryAssetType(TEXT("Mount"));

FPrimaryAssetId URPGMountDefinition::GetPrimaryAssetId() const
{
    return RPGRiding::IsValidMountId(MountId) ? FPrimaryAssetId(PrimaryAssetType, FName(*MountId)) : FPrimaryAssetId();
}

bool URPGMountDefinition::ValidateMetadata(TArray<FText>& Errors) const
{
    const int32 Before = Errors.Num();
    const auto Fail = [&Errors](const TCHAR* Message) { Errors.Add(FText::FromString(Message)); };
    if (!RPGRiding::IsValidMountId(MountId) || DefinitionVersion <= 0) Fail(TEXT("Mount needs a stable lowercase ID and positive definition version."));
    if (DisplayName.IsEmpty()) Fail(TEXT("Mount display name is required."));
    if (Icon.IsNull() || Mesh.IsNull() || AnimClass.IsNull() || RiderPoseLayer.IsNull()
        || DashPresentation.IsNull() || EmotePresentation.IsNull()) Fail(TEXT("Mount presentation references are required."));
    if (SaddleSocket.IsNone()) Fail(TEXT("Mount saddle socket is required."));
    const FVector Scale = RiderOffset.GetScale3D();
    if (!RiderOffset.IsValid() || Scale.X <= 0 || Scale.Y <= 0 || Scale.Z <= 0)
        Fail(TEXT("Rider offset must have finite translation/scale, a normalized rotation and positive scale."));
    const FVector MountScale = MountOffset.GetScale3D();
    if (!MountOffset.IsValid() || MountScale.X <= 0 || MountScale.Y <= 0 || MountScale.Z <= 0)
        Fail(TEXT("Mount offset must have a finite normalized transform and positive scale."));
    return Errors.Num() == Before;
}

bool URPGMountDefinition::ValidateCatalog(TConstArrayView<const URPGMountDefinition*> Definitions, TArray<FText>& Errors)
{
    const int32 Before = Errors.Num();
    if (Definitions.Num() < 1 || Definitions.Num() > RPGRiding::MaxMountDefinitions)
    {
        Errors.Add(FText::FromString(TEXT("Mount catalog must contain 1..512 definitions.")));
        return false;
    }
    TSet<FString> Ids;
    for (const auto* Definition : Definitions)
    {
        if (!IsValid(Definition)) { Errors.Add(FText::FromString(TEXT("Mount catalog contains a null/invalid definition."))); continue; }
        Definition->ValidateMetadata(Errors);
        if (Ids.Contains(Definition->MountId)) Errors.Add(FText::FromString(TEXT("Mount catalog contains duplicate stable IDs.")));
        Ids.Add(Definition->MountId);
    }
    return Errors.Num() == Before;
}

#if WITH_EDITOR
EDataValidationResult URPGMountDefinition::IsDataValid(FDataValidationContext& Context) const
{
    const auto Parent = Super::IsDataValid(Context);
    TArray<FText> Errors;
    if (ValidateMetadata(Errors))
    {
        const USkeletalMesh* LoadedMesh = Mesh.LoadSynchronous();
        if (!LoadedMesh || !LoadedMesh->FindSocket(SaddleSocket))
            Errors.Add(FText::FromString(TEXT("Mount mesh or its authored saddle socket could not be resolved.")));
        if (!Icon.LoadSynchronous() || !AnimClass.LoadSynchronous() || !RiderPoseLayer.LoadSynchronous()
            || !DashPresentation.LoadSynchronous() || !EmotePresentation.LoadSynchronous())
            Errors.Add(FText::FromString(TEXT("Mount presentation assets could not be resolved.")));
    }
    for (const auto& Error : Errors) Context.AddError(Error);
    return Errors.IsEmpty() && Parent != EDataValidationResult::Invalid ? EDataValidationResult::Valid : EDataValidationResult::Invalid;
}
#endif
