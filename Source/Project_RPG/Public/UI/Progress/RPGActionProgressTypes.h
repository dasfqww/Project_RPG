#pragma once

#include "CoreMinimal.h"
#include "RPGActionProgressTypes.generated.h"

class UTexture2D;

UENUM(BlueprintType)
enum class ERPGActionProgressKind : uint8 { Holding, Casting, Charge, Interaction };

UENUM(BlueprintType)
enum class ERPGActionProgressPhase : uint8 { Running, Ready, AwaitingConfirmation };

UENUM(BlueprintType)
enum class ERPGActionProgressTimeDisplay : uint8 { None, Elapsed, Remaining };

UENUM(BlueprintType)
enum class ERPGActionProgressEndReason : uint8 { None, Completed, Cancelled, Interrupted, SourceLost };

/** Local presentation identity, not a gameplay authorization or replicated command. Capture per execution. */
USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGActionProgressHandle
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category="Action Progress") FGuid Id;
	bool IsValid() const { return Id.IsValid(); }
	bool operator==(const FRPGActionProgressHandle& Other) const { return Id == Other.Id; }
};

/** Atomic display input. The producer owns time, completion and cancellation; UI never advances it. */
USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGActionProgressData
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Action Progress") ERPGActionProgressKind Kind = ERPGActionProgressKind::Interaction;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Action Progress") FText Label;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Action Progress") TSoftObjectPtr<UTexture2D> Icon;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Action Progress") float ElapsedSeconds = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Action Progress") bool bHasDuration = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Action Progress") float DurationSeconds = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Action Progress") bool bReverseFill = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Action Progress") ERPGActionProgressTimeDisplay TimeDisplay = ERPGActionProgressTimeDisplay::None;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Action Progress") ERPGActionProgressPhase Phase = ERPGActionProgressPhase::Running;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Action Progress") int32 Stage = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Action Progress") int32 StageCount = 0;
};

/** Read current state on attachment and every change notification; do not cache the component's storage. */
USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGActionProgressSnapshot
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category="Action Progress") FRPGActionProgressHandle Handle;
	UPROPERTY(BlueprintReadOnly, Category="Action Progress") FRPGActionProgressData Data;
	UPROPERTY(BlueprintReadOnly, Category="Action Progress") bool bActive = false;
	UPROPERTY(BlueprintReadOnly, Category="Action Progress") float Fraction = 0.f;
	UPROPERTY(BlueprintReadOnly, Category="Action Progress") float DisplaySeconds = 0.f;
	UPROPERTY(BlueprintReadOnly, Category="Action Progress") ERPGActionProgressEndReason EndReason = ERPGActionProgressEndReason::None;
};
