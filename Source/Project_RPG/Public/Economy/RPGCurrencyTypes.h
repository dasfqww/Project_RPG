#pragma once

#include "CoreMinimal.h"
#include "RPGCurrencyTypes.generated.h"

UENUM(BlueprintType)
enum class ERPGCurrencyScope : uint8
{
	Account,
	Roster,
	Character
};

USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGCurrencyBalance
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "RPG|Economy")
	FName CurrencyCode;

	UPROPERTY(BlueprintReadOnly, Category = "RPG|Economy")
	FString DisplayName;

	UPROPERTY(BlueprintReadOnly, Category = "RPG|Economy")
	ERPGCurrencyScope Scope = ERPGCurrencyScope::Character;

	UPROPERTY(BlueprintReadOnly, Category = "RPG|Economy")
	FString OwnerId;

	UPROPERTY(BlueprintReadOnly, Category = "RPG|Economy")
	int64 Balance = 0;

	UPROPERTY(BlueprintReadOnly, Category = "RPG|Economy")
	int64 MaxBalance = 0;

	UPROPERTY(BlueprintReadOnly, Category = "RPG|Economy")
	int64 Revision = 0;

	bool IsValid() const
	{
		return !CurrencyCode.IsNone() &&
			!OwnerId.IsEmpty() &&
			Balance >= 0 &&
			MaxBalance > 0 &&
			Revision >= 0;
	}
};

USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGCurrencyChange
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Economy")
	FName CurrencyCode;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Economy")
	int64 Delta = 0;
};

USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGCurrencyChangeResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "RPG|Economy")
	FName CurrencyCode;

	UPROPERTY(BlueprintReadOnly, Category = "RPG|Economy")
	ERPGCurrencyScope Scope = ERPGCurrencyScope::Character;

	UPROPERTY(BlueprintReadOnly, Category = "RPG|Economy")
	FString OwnerId;

	UPROPERTY(BlueprintReadOnly, Category = "RPG|Economy")
	int64 Delta = 0;

	UPROPERTY(BlueprintReadOnly, Category = "RPG|Economy")
	int64 PreviousBalance = 0;

	UPROPERTY(BlueprintReadOnly, Category = "RPG|Economy")
	int64 NewBalance = 0;

	UPROPERTY(BlueprintReadOnly, Category = "RPG|Economy")
	int64 Revision = 0;
};
