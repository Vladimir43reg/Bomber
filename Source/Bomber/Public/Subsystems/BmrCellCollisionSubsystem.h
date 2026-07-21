// Copyright (c) Yevhenii Selivanov

#pragma once

#include "Subsystems/WorldSubsystem.h"

// Bomber
#include "Structures/BmrCell.h"
#include "Structures/BmrCellOverlapListeners.h"

#include "BmrCellCollisionSubsystem.generated.h"

class UBmrMapComponent;

DECLARE_DYNAMIC_DELEGATE_TwoParams(FBmrCellOverlapDelegate, UBmrMapComponent*, OverlappingActor, const FBmrCell&, Cell);

/**
 * Broadcasts per-cell overlap events whenever a level actor's cell changes, so each listener reacts to own cell.
 * Blocking is driven by mover component. Replaces physics collision for three reasons:
 * - Any number of players: bomb overlaps placer until placer leaves cell, then blocks,
 *   with physics it required own collision channel per player, while number of channels is limited.
 * - Deterministic networking: physics is live state that rollback never restores, so resimulated
 *   frames saw different collision than original ticks, causing corrections near bombs.
 *   Grid collision is resolved from simulation state, so it is same on server and client
 *   under resimulation.
 * - No collision actors around level: with physics, scalable collision asset was required
 *   and rebuilt after each level generation, while off-grid cells block by themselves,
 *   so level of any shape is supported.
 */
UCLASS()
class BOMBER_API UBmrCellCollisionSubsystem final : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Returns the subsystem, checked and will crash if it can not be obtained. */
	static UBmrCellCollisionSubsystem& GetCellCollisionSubsystemChecked();

	/** Returns the pointer to the subsystem, nullptr if the world has none yet. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "[Bomber]")
	static UBmrCellCollisionSubsystem* GetCellCollisionSubsystem();

	/*********************************************************************************************
	 * Overlap events
	 ********************************************************************************************* */
public:
	/** Broadcasts enter and exit listeners for the cell an actor left and the one it now occupies, if any listen to them.
	 * @param OverlappingActor - map component whose cell changed.
	 * @param OldCell - previous cell, pass invalid when actor is fresh. */
	UFUNCTION(BlueprintCallable, Category = "[Bomber]", meta = (AutoCreateRefTerm = "OldCell"))
	void TryBroadcastCellOverlap(UBmrMapComponent* OverlappingActor, const FBmrCell& OldCell);

	/** Starts listening overlaps of specified cell, fired when any level actor enters it. Multiple listeners per cell supported.
	 * @param Cell - cell to listen.
	 * @param Delegate - called on each enter. */
	UFUNCTION(BlueprintCallable, Category = "[Bomber]", meta = (AutoCreateRefTerm = "Cell,Delegate"))
	void OnCellBeginOverlap(const FBmrCell& Cell, const FBmrCellOverlapDelegate& Delegate);

	/** Starts listening overlaps of specified cell, fired when any level actor leaves it. Multiple listeners per cell supported.
	 * @param Cell - cell to listen.
	 * @param Delegate - called on each leave. */
	UFUNCTION(BlueprintCallable, Category = "[Bomber]", meta = (AutoCreateRefTerm = "Cell,Delegate"))
	void OnCellEndOverlap(const FBmrCell& Cell, const FBmrCellOverlapDelegate& Delegate);

	/** Stops listening enter and exit overlaps on every cell for given listener, single teardown call for any consumer. */
	UFUNCTION(BlueprintCallable, Category = "[Bomber]")
	void RemoveCellListener(const UObject* Listener);

	/*********************************************************************************************
	 * Overrides
	 ********************************************************************************************* */
protected:
	/** Drops listeners when the world tears down so pooled actors do not leak across travels. */
	virtual void Deinitialize() override;

	/*********************************************************************************************
	 * Data
	 ********************************************************************************************* */
protected:
	/** Per-cell enter listeners, fired for every cell actor enter. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadWrite, Transient, AdvancedDisplay, Category = "[Bomber]", meta = (BlueprintProtected))
	TMap<FBmrCell, FBmrCellOverlapListeners> CellEnteredListeners;

	/** Per-cell exit listeners, fired for every cell actor leave. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadWrite, Transient, AdvancedDisplay, Category = "[Bomber]", meta = (BlueprintProtected))
	TMap<FBmrCell, FBmrCellOverlapListeners> CellExitedListeners;

private:
	/** Drops every delegate bound to given listener from per-cell listeners map. */
	static void RemoveListenerFromCells(TMap<FBmrCell, FBmrCellOverlapListeners>& InOutListenersMap, const UObject* Listener);
};
