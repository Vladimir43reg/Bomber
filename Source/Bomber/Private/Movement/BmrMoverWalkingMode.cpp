// Copyright (c) Yevhenii Selivanov

#include "Movement/BmrMoverWalkingMode.h"

// Bomber
#include "Structures/BmrMoverSyncState.h"
#include "UtilityLibraries/BmrMovementUtilsLibrary.h"

// UE
#include "Components/SceneComponent.h"
#include "DefaultMovementSet/Settings/CommonLegacyMovementSettings.h"
#include "Engine/EngineTypes.h"
#include "Engine/HitResult.h"
#include "MoveLibrary/FloorQueryUtils.h"
#include "MoveLibrary/MovementRecord.h"
#include "MoveLibrary/MovementUtils.h"
#include "MoveLibrary/MovementUtilsTypes.h"
#include "MoverComponent.h"
#include "MoverDataModelTypes.h"
#include "MoverSimulationTypes.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(BmrMoverWalkingMode)

// Called when the mode is registered, initializes cached settings
void UBmrMoverWalkingMode::OnRegistered(const FName ModeName, const FMoverSimContext& SimContext)
{
	Super::OnRegistered(ModeName, SimContext);

	const UMoverComponent* MoverComponent = GetMoverComponent();
	checkf(MoverComponent, TEXT("ERROR: [%i] %hs:\n'MoverComponent' is null!"), __LINE__, __FUNCTION__);
	MutableLegacySettings = MoverComponent->FindSharedSettings_Mutable<UCommonLegacyMovementSettings>();
	checkf(MutableLegacySettings, TEXT("ERROR: [%i] %hs:\n'CachedCommonSettings' is null!"), __LINE__, __FUNCTION__);
	CachedMaxSpeed = MutableLegacySettings->MaxSpeed;
}

// Is overridden to handle walking-related movement
void UBmrMoverWalkingMode::GenerateMove_Implementation(const FMoverSimContext& SimContext, const FMoverTickStartData& StartState, const FMoverTimeStep& TimeStep, FProposedMove& OutProposedMove) const
{
	// We need to modify MaxSpeed before calling Super because:
	// 1. Base class uses UCommonLegacyMovementSettings::MaxSpeed in multiple places (Params.MaxSpeed, friction logic, etc.)
	// 2. FGroundMoveParams doesn't allow selective parameter override after construction
	// 3. Alternative would require copying entire super method just to change one value
	// 4. Temporary modification preserves all base class logic and future updates
	const FBmrMoverSyncState* PowerupsState = StartState.SyncState.SyncStateCollection.FindDataByType<FBmrMoverSyncState>();
	const float BonusSpeed = PowerupsState ? PowerupsState->SkatePowerupAttribute * SkateSpeedBonus : 0.f;
	checkf(MutableLegacySettings, TEXT("ERROR: [%i] %hs:\n'MutableLegacySettings' is null!"), __LINE__, __FUNCTION__);
	const float OriginalMaxSpeed = MutableLegacySettings->MaxSpeed;
	MutableLegacySettings->MaxSpeed += BonusSpeed;

	Super::GenerateMove_Implementation(SimContext, StartState, TimeStep, OutProposedMove);

	// Restore original max speed after Super call
	MutableLegacySettings->MaxSpeed = OriginalMaxSpeed;
}

// Called every simulation tick
void UBmrMoverWalkingMode::SimulationTick_Implementation(const FSimulationTickParams& Params, FMoverTickEndData& OutputState)
{
	MoveAlongGridPlane(Params, OutputState);
}

// Moves pawn one simulation step along grid plane: velocity arrives already grid-clamped, so this only orients it, steps with Z pinned to cell height, and reports flat walkable floor so pawn never falls
void UBmrMoverWalkingMode::MoveAlongGridPlane(const FSimulationTickParams& Params, FMoverTickEndData& OutputState)
{
	USceneComponent* UpdatedComponent = Params.MovingComps.UpdatedComponent.Get();
	const FMoverDefaultSyncState* StartingSyncState = Params.StartState.SyncState.SyncStateCollection.FindDataByType<FMoverDefaultSyncState>();
	const UMoverComponent* MoverComponent = GetMoverComponent();
	if (!ensureMsgf(UpdatedComponent, TEXT("ASSERT: [%i] %hs:\n'UpdatedComponent' is null!"), __LINE__, __FUNCTION__)
	    || !ensureMsgf(StartingSyncState, TEXT("ASSERT: [%i] %hs:\n'StartingSyncState' is null!"), __LINE__, __FUNCTION__)
	    || !ensureMsgf(MoverComponent, TEXT("ASSERT: [%i] %hs:\n'MoverComponent' is null!"), __LINE__, __FUNCTION__)
	    || !ensureMsgf(CommonLegacySettings, TEXT("ASSERT: [%i] %hs:\n'CommonLegacySettings' is null!"), __LINE__, __FUNCTION__))
	{
		return;
	}

	const FProposedMove& ProposedMove = Params.ProposedMove;
	const float DeltaSeconds = Params.TimeStep.StepMs * 0.001f;
	const FVector UpDirection = MoverComponent->GetUpDirection();
	const FVector StartLocation = UpdatedComponent->GetComponentLocation();

	const FQuat TargetOrientation = UBmrMovementUtilsLibrary::GetTargetOrientation(StartingSyncState->GetOrientation_WorldSpace(), ProposedMove.AngularVelocityDegrees, DeltaSeconds, UpDirection, CommonLegacySettings->bShouldRemainVertical);
	const FVector MoveDelta = UBmrMovementUtilsLibrary::GetGridMoveDelta(StartLocation, ProposedMove.LinearVelocity, DeltaSeconds, StandingHalfHeight);

	FMovementRecord MoveRecord;
	MoveRecord.SetDeltaSeconds(DeltaSeconds);
	FHitResult MoveHitResult(1.f);
	// Unswept: cell occupancy already proved destination free, live sweep would reintroduce physics nondeterminism grid collision exists to remove
	UMovementUtils::TrySafeMoveUpdatedComponent(Params.MovingComps, MoveDelta, TargetOrientation, /*bSweep*/ false, /*out*/ MoveHitResult, ETeleportType::None, /*out*/ MoveRecord);

	FMoverDefaultSyncState& OutputSyncStateRef = OutputState.SyncState.SyncStateCollection.FindOrAddMutableDataByType<FMoverDefaultSyncState>();
	OutputSyncStateRef.MoveDirectionIntent = ProposedMove.bHasDirIntent ? ProposedMove.DirectionIntent : FVector::ZeroVector;
	CaptureFinalState(StartingSyncState, UpdatedComponent, /*bDidAttemptMovement*/ true, UBmrMovementUtilsLibrary::MakeGridFloorResult(UpDirection), MoveRecord, Params.TimeStep, ProposedMove.AngularVelocityDegrees, OutputSyncStateRef);
}