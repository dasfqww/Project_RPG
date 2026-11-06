#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "RPGMountDefinition.generated.h"

class UAnimInstance;
class UAnimMontage;
class USkeletalMesh;
class UTexture2D;
class FDataValidationContext;

/** Cosmetic identity only. Shared performance is owned by URPGRidingPolicy. */
UCLASS(BlueprintType, Const)
class PROJECT_RPG_API URPGMountDefinition final : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    static const FPrimaryAssetType PrimaryAssetType;
    virtual FPrimaryAssetId GetPrimaryAssetId() const override;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Riding") FString MountId;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Riding") int32 DefinitionVersion = 1;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Riding") FText DisplayName;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Riding") TSoftObjectPtr<UTexture2D> Icon;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Riding") TSoftObjectPtr<USkeletalMesh> Mesh;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Riding") TSoftClassPtr<UAnimInstance> AnimClass;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Riding") TSoftClassPtr<UAnimInstance> RiderPoseLayer;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Riding") FName SaddleSocket;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Riding") FTransform RiderOffset;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Riding") FGameplayTagContainer RegionTags;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Riding") TSoftObjectPtr<UAnimMontage> DashPresentation;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Riding") TSoftObjectPtr<UAnimMontage> EmotePresentation;

    /** No loading: safe for server catalog admission. Editor validation additionally resolves assets/socket. */
    bool ValidateMetadata(TArray<FText>& Errors) const;
    static bool ValidateCatalog(TConstArrayView<const URPGMountDefinition*> Definitions, TArray<FText>& Errors);
#if WITH_EDITOR
    virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
