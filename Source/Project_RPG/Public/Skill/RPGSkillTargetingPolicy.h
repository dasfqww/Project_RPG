#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "RPGSkillTargetingPolicy.generated.h"

struct FInstancedStruct;
struct FRPGHitQueryFilter;
struct FRPGSkillTargetResult;

/** Narrow player/AI service surface required by targeting strategies. */
class PROJECT_RPG_API IRPGSkillTargetingHost
{
public:
	virtual ~IRPGSkillTargetingHost() = default;

	virtual UWorld* GetSkillTargetingWorld() const = 0;
	virtual AActor* GetSkillSourceActor() const = 0;
	virtual bool GetSkillCameraAimRay(
		FVector& OutOrigin,
		FVector& OutDirection) const = 0;
	virtual AActor* GetSkillLockedTarget() const = 0;
	virtual const FRPGHitQueryFilter& GetSkillTargetValidationFilter() const = 0;
};

/** One activation-local strategy for resolving where a skill is aimed. */
UCLASS(Abstract)
class PROJECT_RPG_API URPGSkillTargetingPolicy : public UObject
{
	GENERATED_BODY()

public:
	void Initialize(
		IRPGSkillTargetingHost& InHost,
		const FInstancedStruct& InConfig);

	virtual bool ValidateTargetingConfig(
		const FInstancedStruct& Config,
		FText& OutError) const;
	virtual bool ResolveTarget(FRPGSkillTargetResult& OutResult) const;
	virtual bool ValidateReplicatedTarget(
		const FRPGSkillTargetResult& SubmittedResult,
		FRPGSkillTargetResult& OutValidatedResult,
		FText& OutError) const;

protected:
	IRPGSkillTargetingHost* GetHost() const { return TargetingHost; }
	const FInstancedStruct& GetConfig() const;
	UWorld* GetWorld() const override;

private:
	IRPGSkillTargetingHost* TargetingHost = nullptr;
	const FInstancedStruct* TargetingConfig = nullptr;
};

UCLASS()
class PROJECT_RPG_API URPGSkillTargetingPolicy_CameraDirection
	: public URPGSkillTargetingPolicy
{
	GENERATED_BODY()

public:
	virtual bool ValidateTargetingConfig(
		const FInstancedStruct& Config,
		FText& OutError) const override;
	virtual bool ResolveTarget(FRPGSkillTargetResult& OutResult) const override;
	virtual bool ValidateReplicatedTarget(
		const FRPGSkillTargetResult& SubmittedResult,
		FRPGSkillTargetResult& OutValidatedResult,
		FText& OutError) const override;
};

UCLASS()
class PROJECT_RPG_API URPGSkillTargetingPolicy_SoftTarget
	: public URPGSkillTargetingPolicy
{
	GENERATED_BODY()

public:
	virtual bool ValidateTargetingConfig(
		const FInstancedStruct& Config,
		FText& OutError) const override;
	virtual bool ResolveTarget(FRPGSkillTargetResult& OutResult) const override;
	virtual bool ValidateReplicatedTarget(
		const FRPGSkillTargetResult& SubmittedResult,
		FRPGSkillTargetResult& OutValidatedResult,
		FText& OutError) const override;
};

UCLASS()
class PROJECT_RPG_API URPGSkillTargetingPolicy_GroundPoint
	: public URPGSkillTargetingPolicy
{
	GENERATED_BODY()

public:
	virtual bool ValidateTargetingConfig(
		const FInstancedStruct& Config,
		FText& OutError) const override;
	virtual bool ResolveTarget(FRPGSkillTargetResult& OutResult) const override;
	virtual bool ValidateReplicatedTarget(
		const FRPGSkillTargetResult& SubmittedResult,
		FRPGSkillTargetResult& OutValidatedResult,
		FText& OutError) const override;
};
