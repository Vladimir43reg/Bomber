// Copyright (c) Yevhenii Selivanov

#pragma once

#include "DefaultMovementSet/Modes/WalkingMode.h"

#include "BmrMoverWalkingMode.generated.h"

/**
 * Extends the movement Walking Mode with custom functionality such as speed increase from power-ups.
 */
UCLASS()
class BOMBER_API UBmrMoverWalkingMode : public UWalkingMode
{
	GENERATED_BODY()

	/*********************************************************************************************
	 * Data
	 ********************************************************************************************* */
protected:
	/** Speed bonus per Skate powerup in units/sec.
	 * If base speed if 500, and this value is 100, then with 2 Skates the speed will be 700. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "[Bomber]", meta = (BlueprintProtected, ClampMin = "0", UIMin = "0"))
	float SkateSpeedBonus = 100.f;

	/** Height above grid plane pawn stands at. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "[Bomber]", meta = (BlueprintProtected, ClampMin = "0", UIMin = "0"))
	float StandingHalfHeight = 88.f;

	/** Cached initial max speed when the mode is registered.
	 * Used for calculating the speed bonus from Skates. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "[Bomber]", meta = (BlueprintProtected))
	float CachedMaxSpeed = 0.f;

	/** Cached common movement settings. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "[Bomber]", meta = (BlueprintProtected))
	TObjectPtr<UCommonLegacyMovementSettings> MutableLegacySettings = nullptr;

	/*********************************************************************************************
	 * Overrides
	 ********************************************************************************************* */
protected:
	/** Called when the mode is registered, initializes cached settings. */
	virtual void OnRegistered(const FName ModeName, const FMoverSimContext& SimContext) override;

	/** Is overridden to handle walking-related movement. */
	virtual void GenerateMove_Implementation(const FMoverSimContext& SimContext, const FMoverTickStartData& StartState, const FMoverTimeStep& TimeStep, FProposedMove& OutProposedMove) const override;

	/** Called every simulation tick. */
	virtual void SimulationTick_Implementation(const FSimulationTickParams& Params, FMoverTickEndData& OutputState) override;

	/*********************************************************************************************
	 * Movement
	 ********************************************************************************************* */
protected:
	/** Moves pawn one simulation step along grid plane: velocity arrives already grid-clamped, so this only orients it, steps with Z pinned to cell height, and reports flat walkable floor so pawn never falls.
	 * @param Params - Simulation step context from Mover.
	 * @param OutputState - Sync state and movement result this step writes into. */
	UFUNCTION(BlueprintCallable, Category = "[Bomber]", meta = (BlueprintProtected, AutoCreateRefTerm = "Params"))
	void MoveAlongGridPlane(const FSimulationTickParams& Params, UPARAM(ref) FMoverTickEndData& OutputState);
};