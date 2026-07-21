// Copyright (c) Yevhenii Selivanov

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "MoveLibrary/FloorQueryUtils.h"

#include "BmrMovementUtilsLibrary.generated.h"

/**
 * Stateless movement math consumed by the Mover: orientation, grid floor height and delta, extent geometry,
 * and grid velocity clamping. Owns no state and takes no subsystem, resolving cell occupancy internally when needed.
 */
UCLASS()
class BOMBER_API UBmrMovementUtilsLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Level actor's grid extent radius, as configured on its data asset. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "[Bomber]")
	static float GetActorExtentRadius(const class AActor* Actor);

	/** Returns world height pawn center stands at above grid plane under given position, holds current height when unresolvable.
	 * @param Position - world position to resolve floor under.
	 * @param StandingHalfHeight - pawn half height added above cell plane. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "[Bomber]", meta = (AutoCreateRefTerm = "Position"))
	static float GetGridFloorHeight(const FVector& Position, float StandingHalfHeight);

	/** Returns true when 2D Position lies within box centered at Center, expanded by Extent on every side.
	 * @param Position - world position to test.
	 * @param Center - box center.
	 * @param Extent - expansion applied on every side. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "[Bomber]", meta = (AutoCreateRefTerm = "Position,Center"))
	static bool IsWithinExtent(const FVector& Position, const FVector& Center, float Extent);

	/** Returns true when 2D Position lies within blocker cell's box expanded by ExtentRadius with rounded corners, exact swept-capsule-vs-box contact shape.
	 * @param Position - world position to test.
	 * @param BlockerLocation - blocker cell center.
	 * @param ExtentRadius - pawn extent radius expanding blocker box on every side. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "[Bomber]", meta = (AutoCreateRefTerm = "Position,BlockerLocation"))
	static bool IsWithinBlockerExtent(const FVector& Position, const FVector& BlockerLocation, float ExtentRadius);

	/** Walks one step's samples from StartLocation along ProposedVelocity, reporting the first blocked sample and which axes it clamps.
	 * @param StartLocation - pawn world position at step start.
	 * @param ProposedVelocity - candidate linear velocity for this step.
	 * @param DeltaSeconds - simulation step seconds.
	 * @param ExtentRadius - pawn grid extent radius.
	 * @param bOutClampX - true when the X velocity component must zero.
	 * @param bOutClampY - true when the Y velocity component must zero.
	 * @return true when a blocker was hit, false when the whole step is clear (out flags meaningless). */
	UFUNCTION(BlueprintCallable, Category = "[Bomber]", meta = (AutoCreateRefTerm = "StartLocation,ProposedVelocity"))
	static bool FindStepClamp(const FVector& StartLocation, const FVector& ProposedVelocity, float DeltaSeconds, float ExtentRadius, bool& bOutClampX, bool& bOutClampY);

	/** Returns portion of desired axis displacement keeping pawn extent just outside every nearby blocker, so pawn settles flush against it.
	 * @param FromPosition - position after the kept axis displacement.
	 * @param bAlongX - true settles X, false settles Y.
	 * @param DesiredSignedAdvance - full desired signed displacement along the settle axis.
	 * @param ExtentRadius - pawn grid extent radius. */
	UFUNCTION(BlueprintCallable, Category = "[Bomber]", meta = (AutoCreateRefTerm = "FromPosition"))
	static float GetAxisAdvanceToContact(const FVector& FromPosition, bool bAlongX, float DesiredSignedAdvance, float ExtentRadius);

	/** Returns Pawn's proposed velocity clamped so its extent never enters a blocked cell and settles flush at contact.
	 * @param Pawn - moving pawn, resolves own extent radius.
	 * @param StartLocation - pawn world position at step start.
	 * @param ProposedVelocity - desired linear velocity before grid clamp.
	 * @param DeltaSeconds - simulation step seconds.
	 * @param bOutClamped - true when grid blocked and velocity changed. */
	UFUNCTION(BlueprintCallable, Category = "[Bomber]", meta = (AutoCreateRefTerm = "StartLocation,ProposedVelocity"))
	static FVector GetGridClampedVelocity(const class APawn* Pawn, const FVector& StartLocation, const FVector& ProposedVelocity, float DeltaSeconds, bool& bOutClamped);

	/** Returns orientation this step ends at, kept upright when pawn must remain vertical.
	 * @param StartOrientation - orientation at step start.
	 * @param AngularVelocityDegrees - proposed angular velocity for this step.
	 * @param DeltaSeconds - simulation step seconds.
	 * @param UpDirection - world up of the simulation.
	 * @param bRemainVertical - true keeps pawn upright, discarding pitch and roll. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "[Bomber]", meta = (AutoCreateRefTerm = "StartOrientation,AngularVelocityDegrees,UpDirection"))
	static FQuat GetTargetOrientation(const FRotator& StartOrientation, const FVector& AngularVelocityDegrees, float DeltaSeconds, const FVector& UpDirection, bool bRemainVertical);

	/** Returns this step's world displacement: planar part comes from already grid-clamped velocity, while Z only corrects back onto grid plane.
	 * @param StartLocation - pawn world position at step start.
	 * @param LinearVelocity - grid-clamped linear velocity for this step.
	 * @param DeltaSeconds - simulation step seconds.
	 * @param StandingHalfHeight - pawn half height above cell plane. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "[Bomber]", meta = (AutoCreateRefTerm = "StartLocation,LinearVelocity"))
	static FVector GetGridMoveDelta(const FVector& StartLocation, const FVector& LinearVelocity, float DeltaSeconds, float StandingHalfHeight);

	/** Returns floor result standing in for a swept floor query: grid plane is always flat and always walkable, so pawn can never fall through it. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "[Bomber]", meta = (AutoCreateRefTerm = "UpDirection"))
	static FFloorCheckResult MakeGridFloorResult(const FVector& UpDirection);
};
