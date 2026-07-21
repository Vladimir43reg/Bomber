// Copyright (c) Yevhenii Selivanov.

#pragma once

// Bomber
#include "BmrCell.h"

#include "BmrCellOverlapListeners.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnCellOverlapDelegate, class UBmrMapComponent*, OverlappingActor, const FBmrCell&, Cell);

/**
 * Cell's overlap listeners, all fired on that cell's enter or leave.
 */
USTRUCT(BlueprintType)
struct BOMBER_API FBmrCellOverlapListeners
{
	GENERATED_BODY()

	/** Listeners subscribed to one cell. */
	UPROPERTY(BlueprintCallable, BlueprintAssignable, Transient, Category = "[Bomber]")
	FOnCellOverlapDelegate OnOverlap;
};
