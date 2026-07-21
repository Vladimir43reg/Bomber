// Copyright (c) Yevhenii Selivanov

#include "Subsystems/BmrCellCollisionSubsystem.h"

// Bomber
#include "Components/BmrMapComponent.h"
#include "MyUtilsLibraries/UtilsLibrary.h"

// UE
#include "Engine/World.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(BmrCellCollisionSubsystem)

// Returns the subsystem, checked and will crash if it can not be obtained
UBmrCellCollisionSubsystem& UBmrCellCollisionSubsystem::GetCellCollisionSubsystemChecked()
{
	UBmrCellCollisionSubsystem* Subsystem = GetCellCollisionSubsystem();
	checkf(Subsystem, TEXT("ERROR: [%i] %hs:\n'Subsystem' is null!"), __LINE__, __FUNCTION__);
	return *Subsystem;
}

// Returns the pointer to the subsystem, nullptr if the world has none yet
UBmrCellCollisionSubsystem* UBmrCellCollisionSubsystem::GetCellCollisionSubsystem()
{
	const UWorld* World = UUtilsLibrary::GetPlayWorld();
	return World ? World->GetSubsystem<UBmrCellCollisionSubsystem>() : nullptr;
}

/*********************************************************************************************
 * Overlap events
 ********************************************************************************************* */

// Broadcasts enter and exit listeners for the cell an actor left and the one it now occupies, if any listen to them
void UBmrCellCollisionSubsystem::TryBroadcastCellOverlap(UBmrMapComponent* OverlappingActor, const FBmrCell& OldCell)
{
	if (!ensureMsgf(OverlappingActor, TEXT("ASSERT: [%i] %hs:\n'OverlappingActor' is not valid!"), __LINE__, __FUNCTION__))
	{
		return;
	}

	const FBmrCell& NewCell = OverlappingActor->GetCell();
	if (OldCell == NewCell)
	{
		// No cell change to broadcast
		return;
	}

	if (OldCell.IsValid())
	{
		if (const FBmrCellOverlapListeners* ExitListeners = CellExitedListeners.Find(OldCell))
		{
			// Copy before broadcasting: listener can re-entrantly churn other cells' listeners, which can rehash this map and invalidate ExitListeners mid-broadcast
			const FOnCellOverlapDelegate OnOverlapCopy = ExitListeners->OnOverlap;
			OnOverlapCopy.Broadcast(OverlappingActor, OldCell);
		}
	}

	if (NewCell.IsValid())
	{
		if (const FBmrCellOverlapListeners* EnterListeners = CellEnteredListeners.Find(NewCell))
		{
			// Copy before broadcasting: listener can re-entrantly churn other cells' listeners, which can rehash this map and invalidate EnterListeners mid-broadcast
			const FOnCellOverlapDelegate OnOverlapCopy = EnterListeners->OnOverlap;
			OnOverlapCopy.Broadcast(OverlappingActor, NewCell);
		}
	}
}

// Starts listening overlaps of specified cell, fired when any level actor enters it. Multiple listeners per cell supported
void UBmrCellCollisionSubsystem::OnCellBeginOverlap(const FBmrCell& Cell, const FBmrCellOverlapDelegate& Delegate)
{
	if (ensureMsgf(Delegate.IsBound(), TEXT("ASSERT: [%i] %hs:\n'Delegate' is not bound!"), __LINE__, __FUNCTION__))
	{
		CellEnteredListeners.FindOrAdd(Cell).OnOverlap.AddUnique(Delegate);
	}
}

// Starts listening overlaps of specified cell, fired when any level actor leaves it. Multiple listeners per cell supported
void UBmrCellCollisionSubsystem::OnCellEndOverlap(const FBmrCell& Cell, const FBmrCellOverlapDelegate& Delegate)
{
	if (ensureMsgf(Delegate.IsBound(), TEXT("ASSERT: [%i] %hs:\n'Delegate' is not bound!"), __LINE__, __FUNCTION__))
	{
		CellExitedListeners.FindOrAdd(Cell).OnOverlap.AddUnique(Delegate);
	}
}

// Stops listening enter and exit overlaps on every cell for given listener, single teardown call for any consumer
void UBmrCellCollisionSubsystem::RemoveCellListener(const UObject* Listener)
{
	RemoveListenerFromCells(/*InOut*/ CellEnteredListeners, Listener);
	RemoveListenerFromCells(/*InOut*/ CellExitedListeners, Listener);
}

// Drops every delegate bound to given listener from per-cell listeners map
void UBmrCellCollisionSubsystem::RemoveListenerFromCells(TMap<FBmrCell, FBmrCellOverlapListeners>& InOutListenersMap, const UObject* Listener)
{
	for (auto It = InOutListenersMap.CreateIterator(); It; ++It)
	{
		It->Value.OnOverlap.RemoveAll(Listener);
		if (!It->Value.OnOverlap.IsBound())
		{
			It.RemoveCurrent();
		}
	}
}

/*********************************************************************************************
 * Overrides
 ********************************************************************************************* */

// Drops listeners when the world tears down so pooled actors do not leak across travels
void UBmrCellCollisionSubsystem::Deinitialize()
{
	CellEnteredListeners.Empty();
	CellExitedListeners.Empty();

	Super::Deinitialize();
}
