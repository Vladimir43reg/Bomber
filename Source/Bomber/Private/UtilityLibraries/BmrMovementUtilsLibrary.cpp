// Copyright (c) Yevhenii Selivanov

#include "UtilityLibraries/BmrMovementUtilsLibrary.h"

// Bomber
#include "Components/BmrMapComponent.h"
#include "DataAssets/BmrLevelActorDataAsset.h"
#include "Structures/BmrCell.h"
#include "UtilityLibraries/BmrCellUtilsLibrary.h"

// UE
#include "GameFramework/Pawn.h"
#include "Math/RotationMatrix.h"
#include "MoveLibrary/FloorQueryUtils.h"
#include "MoveLibrary/MovementUtils.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(BmrMovementUtilsLibrary)

// Level actor's grid extent radius, as configured on its data asset
float UBmrMovementUtilsLibrary::GetActorExtentRadius(const AActor* Actor)
{
	const UBmrMapComponent* MapComponent = Actor ? UBmrMapComponent::GetMapComponent(Actor) : nullptr;
	return MapComponent ? MapComponent->GetActorDataAssetChecked().GetCollisionExtent().X : 0.f;
}

// Returns world height pawn center stands at above grid plane under given position, holds current height when unresolvable
float UBmrMovementUtilsLibrary::GetGridFloorHeight(const FVector& Position, float StandingHalfHeight)
{
	const FBmrCell NearestCell = UBmrCellUtilsLibrary::SnapVectorOnLevel(Position);
	if (!NearestCell.IsValid()
	    || !IsWithinExtent(Position, NearestCell.Location, FBmrCell::CellSize))
	{
		// Can happen before grid built on this endpoint, or position not resolvable to nearby cell (e.g. mid-teleport hitch): hold current height instead of snapping to distant cell
		return Position.Z;
	}

	return NearestCell.Location.Z + StandingHalfHeight;
}

// Returns true when 2D Position lies within box centered at Center, expanded by Extent on every side
bool UBmrMovementUtilsLibrary::IsWithinExtent(const FVector& Position, const FVector& Center, float Extent)
{
	return FMath::Abs(Position.X - Center.X) <= Extent
	       && FMath::Abs(Position.Y - Center.Y) <= Extent;
}

// Returns true when 2D Position lies within blocker cell's box expanded by ExtentRadius with rounded corners, exact swept-capsule-vs-box contact shape
bool UBmrMovementUtilsLibrary::IsWithinBlockerExtent(const FVector& Position, const FVector& BlockerLocation, float ExtentRadius)
{
	constexpr float HalfCellSize = FBmrCell::CellSize * 0.5f;
	const double ClosestX = FMath::Clamp(Position.X, BlockerLocation.X - HalfCellSize, BlockerLocation.X + HalfCellSize);
	const double ClosestY = FMath::Clamp(Position.Y, BlockerLocation.Y - HalfCellSize, BlockerLocation.Y + HalfCellSize);
	const FVector ClosestPoint(ClosestX, ClosestY, Position.Z);
	return FVector::DistSquared2D(Position, ClosestPoint) <= FMath::Square(ExtentRadius);
}

// Walks one step's samples from StartLocation along ProposedVelocity, reporting the first blocked sample and which axes it clamps
bool UBmrMovementUtilsLibrary::FindStepClamp(const FVector& StartLocation, const FVector& ProposedVelocity, float DeltaSeconds, float ExtentRadius, bool& bOutClampX, bool& bOutClampY)
{
	constexpr float SampleSpacing = FBmrCell::CellSize * 0.5f;
	// Matches snap-precision tolerance used to resolve grid position back onto its owning cell
	constexpr float CellSnapToleranceSq = 1.f;

	bOutClampX = false;
	bOutClampY = false;

	const FVector StepDisplacement = ProposedVelocity * DeltaSeconds;
	const float StepDistance = StepDisplacement.Size2D();
	if (FMath::IsNearlyZero(StepDistance))
	{
		// Pawn does not cross grid plane this step, no cell can be entered
		return false;
	}

	const FRotator GridRotation = UBmrCellUtilsLibrary::GetLevelGridRotation();
	const FBmrCell StartCell = UBmrCellUtilsLibrary::SnapVectorOnLevel(StartLocation);
	const FVector StepDirection = StepDisplacement.GetSafeNormal2D();
	const int32 SamplesNum = FMath::CeilToInt(StepDistance / SampleSpacing);

	for (int32 SampleIndex = 1; SampleIndex <= SamplesNum; ++SampleIndex)
	{
		const float SampleDistance = FMath::Min(SampleIndex * SampleSpacing, StepDistance);
		const FVector SamplePosition = StartLocation + StepDirection * SampleDistance;
		const FBmrCell SampleCell = UBmrCellUtilsLibrary::SnapVectorOnLevel(SamplePosition);

		// Evaluate full 3x3 neighborhood before deciding: two blockers meeting at same corner (orthogonal or diagonal) must combine into full stop, neither may starve other by resolving first
		bool bAnyViolation = false;
		bool bXAlonePenetrates = false;
		bool bYAlonePenetrates = false;
		for (int32 OffsetX = -1; OffsetX <= 1; ++OffsetX)
		{
			for (int32 OffsetY = -1; OffsetY <= 1; ++OffsetY)
			{
				const FVector LocalOffset(OffsetX * FBmrCell::CellSize, OffsetY * FBmrCell::CellSize, 0.f);
				const FVector CandidateLocation = SampleCell.Location + GridRotation.RotateVector(LocalOffset);
				const FBmrCell NearestRealCell = UBmrCellUtilsLibrary::SnapVectorOnLevel(CandidateLocation);
				const bool bRealCellHere = NearestRealCell.IsValid() && FVector::DistSquared2D(NearestRealCell.Location, CandidateLocation) < CellSnapToleranceSq;
				// Absent real cell here is off-grid, replaces removed border boxes
				const bool bBlocked = !bRealCellHere || UBmrCellUtilsLibrary::IsCellBlocked(NearestRealCell);
				if (!bBlocked)
				{
					continue;
				}

				// Blocker cell blocks sample ONLY when step START lies outside its extent (exit always allowed, entry blocked): pawn already inside extent (e.g. standing on its own bomb cell) never gets shoved off it
				const FVector& BlockerLocation = bRealCellHere ? NearestRealCell.Location : CandidateLocation;
				const bool bStartOutside = !IsWithinBlockerExtent(StartLocation, BlockerLocation, ExtentRadius);
				const bool bSampleInside = IsWithinBlockerExtent(SamplePosition, BlockerLocation, ExtentRadius);
				const bool bExtentViolation = bStartOutside && bSampleInside;
				// Pawn legitimately within blocker reach (e.g. stopped straddling own bomb's edge after walking off) may hover and leave, but crossing back over blocker's own cell edge is blocked, matching physics that re-blocked as soon as leaving pawn's cell changed
				const bool bStartCellOutside = bRealCellHere && StartCell != NearestRealCell;
				const bool bCellCrossViolation = !bStartOutside && bStartCellOutside && SampleCell == NearestRealCell;
				if (!bExtentViolation && !bCellCrossViolation)
				{
					continue;
				}

				// Per-axis penetration tied to this sample's own progress (tunneling-safe, matches outer sweep), not full step displacement
				bAnyViolation = true;
				const FVector XOnlyPosition(SamplePosition.X, StartLocation.Y, StartLocation.Z);
				const FVector YOnlyPosition(StartLocation.X, SamplePosition.Y, StartLocation.Z);
				if (bExtentViolation)
				{
					bXAlonePenetrates |= IsWithinBlockerExtent(XOnlyPosition, BlockerLocation, ExtentRadius);
					bYAlonePenetrates |= IsWithinBlockerExtent(YOnlyPosition, BlockerLocation, ExtentRadius);
				}
				else
				{
					bXAlonePenetrates |= UBmrCellUtilsLibrary::SnapVectorOnLevel(XOnlyPosition) == NearestRealCell;
					bYAlonePenetrates |= UBmrCellUtilsLibrary::SnapVectorOnLevel(YOnlyPosition) == NearestRealCell;
				}
			}
		}

		if (bAnyViolation)
		{
			bOutClampX = bXAlonePenetrates;
			bOutClampY = bYAlonePenetrates;
			return true;
		}
	}

	return false;
}

// Returns portion of desired axis displacement keeping pawn extent just outside every nearby blocker, so pawn settles flush against it
float UBmrMovementUtilsLibrary::GetAxisAdvanceToContact(const FVector& FromPosition, bool bAlongX, float DesiredSignedAdvance, float ExtentRadius)
{
	constexpr float HalfCellSize = FBmrCell::CellSize * 0.5f;
	// Small separation keeps settled contact strictly outside blocker reach, so start-inside exemption never engages from resting flush
	constexpr float ContactOffset = 0.1f;
	// One sample spacing bounds how far 3x3 blocker scan around target stays exhaustive
	constexpr float MaxSettleDistance = FBmrCell::CellSize * 0.5f;
	// Matches snap-precision tolerance used to resolve grid position back onto its owning cell
	constexpr float CellSnapToleranceSq = 1.f;

	const float AdvanceSign = FMath::Sign(DesiredSignedAdvance);
	float AllowedAdvance = FMath::Min(FMath::Abs(DesiredSignedAdvance), MaxSettleDistance);

	const FVector TargetPosition = FromPosition + FVector(bAlongX ? DesiredSignedAdvance : 0.f, bAlongX ? 0.f : DesiredSignedAdvance, 0.f);
	const FBmrCell TargetCell = UBmrCellUtilsLibrary::SnapVectorOnLevel(TargetPosition);
	const FRotator GridRotation = UBmrCellUtilsLibrary::GetLevelGridRotation();
	for (int32 OffsetX = -1; OffsetX <= 1; ++OffsetX)
	{
		for (int32 OffsetY = -1; OffsetY <= 1; ++OffsetY)
		{
			const FVector LocalOffset(OffsetX * FBmrCell::CellSize, OffsetY * FBmrCell::CellSize, 0.f);
			const FVector CandidateLocation = TargetCell.Location + GridRotation.RotateVector(LocalOffset);
			const FBmrCell NearestRealCell = UBmrCellUtilsLibrary::SnapVectorOnLevel(CandidateLocation);
			const bool bRealCellHere = NearestRealCell.IsValid() && FVector::DistSquared2D(NearestRealCell.Location, CandidateLocation) < CellSnapToleranceSq;
			// Absent real cell here means position is off-grid, always treated as blocked
			const bool bBlocked = !bRealCellHere || UBmrCellUtilsLibrary::IsCellBlocked(NearestRealCell);
			if (!bBlocked)
			{
				// Can be free neighbor cell, contributes no contact distance
				continue;
			}

			const FVector& BlockerLocation = bRealCellHere ? NearestRealCell.Location : CandidateLocation;
			const float AlongFrom = bAlongX ? FromPosition.X : FromPosition.Y;
			const float AlongBlocker = bAlongX ? BlockerLocation.X : BlockerLocation.Y;
			const bool bBlockerAhead = FMath::Sign(AlongBlocker - AlongFrom) == AdvanceSign;
			if (!bBlockerAhead)
			{
				// Blocker sits behind advance direction, cannot bind this settle
				continue;
			}

			const float CrossDistance = FMath::Abs(bAlongX ? FromPosition.Y - BlockerLocation.Y : FromPosition.X - BlockerLocation.X);
			const float CrossOverhang = CrossDistance - HalfCellSize;
			if (CrossOverhang >= ExtentRadius)
			{
				// Extent passes fully beside blocker on cross axis, cannot bind this settle
				continue;
			}

			// Rounded extent reach past blocker face: full radius while cross axis overlaps face, arc falloff past corner
			const float ReachPastFace = CrossOverhang > 0.f ? FMath::Sqrt(FMath::Square(ExtentRadius) - FMath::Square(CrossOverhang)) : ExtentRadius;
			const float ContactDistance = HalfCellSize + ReachPastFace + ContactOffset;
			const float BlockerAllowed = FMath::Abs(AlongBlocker - AlongFrom) - ContactDistance;
			AllowedAdvance = FMath::Min(AllowedAdvance, FMath::Max(BlockerAllowed, 0.f));
		}
	}

	return AdvanceSign * AllowedAdvance;
}

// Returns Pawn's proposed velocity clamped so its extent never enters a blocked cell and settles flush at contact
FVector UBmrMovementUtilsLibrary::GetGridClampedVelocity(const APawn* Pawn, const FVector& StartLocation, const FVector& ProposedVelocity, float DeltaSeconds, bool& bOutClamped)
{
	checkf(Pawn, TEXT("ERROR: [%i] %hs:\n'Pawn' is null!"), __LINE__, __FUNCTION__);

	const float ExtentRadius = GetActorExtentRadius(Pawn);

	FVector Velocity = ProposedVelocity;
	bOutClamped = false;

	// Per-axis clamp redirects remaining motion along surviving axis, and that redirected step is itself unvalidated: without own sweep it can slip pawn into neighboring blocker's extent on corner graze, where start-inside exemption below then stops blocking that cell entirely. Sweep therefore repeats on clamped velocity until whole pass is clean, each violating pass zeroes at least one axis so passes are bounded by axis count
	constexpr int32 MaxSweepPasses = 3;
	for (int32 SweepPass = 0; SweepPass < MaxSweepPasses; ++SweepPass)
	{
		bool bClampX = false;
		bool bClampY = false;
		if (!FindStepClamp(StartLocation, Velocity, DeltaSeconds, ExtentRadius, /*out*/ bClampX, /*out*/ bClampY))
		{
			// Whole step verified clean against every nearby blocker
			break;
		}

		// Clamp per-axis: zero velocity component carrying pawn into whichever blocked face(s) fired, keep tangential to preserve wall slide. Two diagonal blockers' extents both cover their shared corner for any radius above zero, so pure diagonal cut penetrates neither axis alone and gets full stop instead
		if (bClampX)
		{
			Velocity.X = 0.f;
		}
		if (bClampY)
		{
			Velocity.Y = 0.f;
		}
		if (!bClampX && !bClampY)
		{
			Velocity = FVector::ZeroVector;
		}
		bOutClamped = true;
	}

	if (!bOutClamped)
	{
		// Proposed move already clean, settle only needed when pass clamped it
		return Velocity;
	}

	// Contact settle: clamped axis parks pawn up to one step short of blocker instead of resting flush against it, advance that axis to exact contact. Single-axis only, sequential two-axis settle could re-enter corner arc
	const bool bSettleX = Velocity.X == 0.f && ProposedVelocity.X != 0.f;
	const bool bSettleY = Velocity.Y == 0.f && ProposedVelocity.Y != 0.f;
	if (bSettleX != bSettleY
	    && ensureMsgf(!FMath::IsNearlyZero(DeltaSeconds), TEXT("ASSERT: [%i] %hs:\n'DeltaSeconds' is nearly zero!"), __LINE__, __FUNCTION__))
	{
		const FVector KeptDisplacement = Velocity * DeltaSeconds;
		const FVector SettleFromPosition = StartLocation + KeptDisplacement;
		const float DesiredAdvance = (bSettleX ? ProposedVelocity.X : ProposedVelocity.Y) * DeltaSeconds;
		const float SettledAdvance = GetAxisAdvanceToContact(SettleFromPosition, bSettleX, DesiredAdvance, ExtentRadius);
		if (bSettleX)
		{
			Velocity.X = SettledAdvance / DeltaSeconds;
		}
		else
		{
			Velocity.Y = SettledAdvance / DeltaSeconds;
		}
	}

	return Velocity;
}

// Returns orientation this step ends at, kept upright when pawn must remain vertical
FQuat UBmrMovementUtilsLibrary::GetTargetOrientation(const FRotator& StartOrientation, const FVector& AngularVelocityDegrees, float DeltaSeconds, const FVector& UpDirection, bool bRemainVertical)
{
	const FRotator TargetOrient = UMovementUtils::ApplyAngularVelocityToRotator(StartOrientation, AngularVelocityDegrees, DeltaSeconds);
	FQuat TargetOrientQuat = TargetOrient.Quaternion();
	if (bRemainVertical)
	{
		TargetOrientQuat = FRotationMatrix::MakeFromZX(UpDirection, TargetOrientQuat.GetForwardVector()).ToQuat();
	}

	return TargetOrientQuat;
}

// Returns this step's world displacement: planar part comes from already grid-clamped velocity, while Z only corrects back onto grid plane
FVector UBmrMovementUtilsLibrary::GetGridMoveDelta(const FVector& StartLocation, const FVector& LinearVelocity, float DeltaSeconds, float StandingHalfHeight)
{
	FVector MoveDelta = LinearVelocity * DeltaSeconds;
	MoveDelta.Z = GetGridFloorHeight(StartLocation + MoveDelta, StandingHalfHeight) - StartLocation.Z;

	return MoveDelta;
}

// Returns floor result standing in for a swept floor query: grid plane is always flat and always walkable, so pawn can never fall through it
FFloorCheckResult UBmrMovementUtilsLibrary::MakeGridFloorResult(const FVector& UpDirection)
{
	FFloorCheckResult GridFloor;
	GridFloor.bBlockingHit = true;
	GridFloor.bWalkableFloor = true;
	GridFloor.FloorDist = 0.f;
	GridFloor.HitResult.bBlockingHit = true;
	GridFloor.HitResult.Normal = UpDirection;
	GridFloor.HitResult.ImpactNormal = UpDirection;

	return GridFloor;
}
