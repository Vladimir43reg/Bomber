// Copyright (c) Yevhenii Selivanov.

#include "Actors/BmrBombAbilityActor.h"

// Bomber
#include "AbilitySystem/Attributes/BmrPowerupsAttributeSet.h"
#include "Actors/BmrGeneratedMap.h"
#include "Bomber.h"
#include "Components/BmrMapComponent.h"
#include "DataRegistries/BmrBombRow.h"
#include "GameFramework/BmrGameState.h"
#include "UtilityLibraries/BmrCellUtilsLibrary.h"

#if WITH_EDITOR
#include "BmrUnrealEdEngine.h"
#include "MyEditorUtilsLibraries/EditorUtilsLibrary.h"
#endif

// UE
#include "Engine/StaticMesh.h"
#include "GameFramework/PlayerState.h"
#include "Materials/MaterialInstance.h"
#include "Net/UnrealNetwork.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(BmrBombAbilityActor)

// Sets default values
ABmrBombAbilityActor::ABmrBombAbilityActor()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = false;
	PrimaryActorTick.bStartWithTickEnabled = false;

	// Replicate an actor
	bReplicates = true;
	static constexpr float NewUpdateFrequency = 10.f;
	SetNetUpdateFrequency(NewUpdateFrequency);
	bAlwaysRelevant = true;
	SetReplicatingMovement(true);

	// Initialize Root Component
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("DefaultSceneRoot"));

	// Initialize MapComponent
	MapComponent = CreateDefaultSubobject<UBmrMapComponent>(TEXT("MapComponent"));
}

// Returns true when bomb is fully initialized: both bomb is initialized and added to level
bool ABmrBombAbilityActor::IsBombReady() const
{
	return MapComponent && MapComponent->GetCell().IsValid() // Is added to level
	       && InstigatorAbilitySystemComponent; // Is initialized
}

/*********************************************************************************************
 * Detonation
 ********************************************************************************************* */

// Initiates the explosion: starts countdown and initializes the data (fire radius, explosion cells, etc.)
void ABmrBombAbilityActor::InitBomb(UAbilitySystemComponent* InASC)
{
	if (!ensureMsgf(InASC, TEXT("ASSERT: [%i] %hs:\n'InstigatorAbilitySystemComponent' is null, can not init the bomb!"), __LINE__, __FUNCTION__))
	{
		return;
	}

	InstigatorAbilitySystemComponent = InASC;
	MARK_PROPERTY_DIRTY_FROM_NAME(ThisClass, InstigatorAbilitySystemComponent, this);

	if (IsBombReady())
	{
		OnBombReady();
	}
}

// Returns explosion radius from instigator, or -1 if can not be obtained
int32 ABmrBombAbilityActor::GetFireRadius() const
{
	constexpr int32 DefaultFireRadius = 1;
	const UBmrPowerupsAttributeSet* PowerupsAttributeSet = UBmrPowerupsAttributeSet::GetPowerupsAttributeSet(InstigatorAbilitySystemComponent);
	return PowerupsAttributeSet ? PowerupsAttributeSet->GetPowerup_Fire() : DefaultFireRadius;
}

// Show current explosion cells if the bomb type is allowed to be displayed, is not available in shipping build
void ABmrBombAbilityActor::TryDisplayExplosionCells()
{
#if !UE_BUILD_SHIPPING
	FBmrDisplayCellsParams Params = FBmrDisplayCellsParams::EmptyParams;
	Params.bClearPreviousDisplays = true;
	Params.TextColor = FLinearColor::Yellow;
	Params.TextSize += 50.f;
	Params.TextHeight += 1.f;
	UBmrCellUtilsLibrary::DisplayCells(this, ExplosionCells, Params);
#endif // !UE_BUILD_SHIPPING
}

// Calculates the explosion cells based on current fire radius
void ABmrBombAbilityActor::UpdateExplosionCells()
{
	if (!HasAuthority()
	    || !ExplosionCells.IsEmpty())
	{
		// Already calculated
		return;
	}

	ExplosionCells = UBmrCellUtilsLibrary::GetCellsAround(MapComponent->GetCell(), EPathType::Explosion, GetFireRadius());

	TryDisplayExplosionCells();
}

/*********************************************************************************************
 * Cue Visuals: VFXs, SFXs, Materials
 ********************************************************************************************* */

// Updates current mesh for this bomb actor, based on instigator type, or randomly if no instigator
void ABmrBombAbilityActor::ApplyMesh()
{
	const AActor* InstigatorActor = InstigatorAbilitySystemComponent ? InstigatorAbilitySystemComponent->GetAvatarActor() : nullptr;
	const FBmrBombRow& BombRow = FBmrBombRow::GetBombRow(InstigatorActor);
	UStreamableRenderAsset* BombMesh = BombRow.Mesh.Get();
	if (!ensureMsgf(BombMesh, TEXT("ASSERT: [%i] %hs:\n'BombMesh' is not valid!"), __LINE__, __FUNCTION__))
	{
		return;
	}

	checkf(MapComponent, TEXT("ERROR: [%i] %hs:\n'MapComponentInternal' is null!"), __LINE__, __FUNCTION__);
	MapComponent->SetLocalMesh(BombMesh);
}

// Updates current material for this bomb actor, based on this bomb and Player placer types
void ABmrBombAbilityActor::ApplyMaterial()
{
	TObjectPtr<UMaterialInterface> NewBombMaterial = nullptr;

	const APawn* OwnedPawn = InstigatorAbilitySystemComponent ? Cast<APawn>(InstigatorAbilitySystemComponent->GetAvatarActor()) : nullptr;
	const APlayerState* OwnerPlayerState = OwnedPawn ? OwnedPawn->GetPlayerState<APlayerState>() : nullptr;
	const FBmrBombRow& BombRow = FBmrBombRow::GetBombRow(OwnedPawn);
	if (BombRow.Material.Get())
	{
		if (const int32 BombMaterialsNum = FBmrBombRow::GetBombMaterialsNum())
		{
			// Cycle by player index so each player gets own material when sharing the same bomb row tag
			const int32 PlayerIndex = OwnerPlayerState ? OwnerPlayerState->GetPlayerId() : FMath::RandRange(0, BombMaterialsNum - 1);
			const int32 MaterialIndex = FMath::Abs(PlayerIndex) % BombMaterialsNum;
			NewBombMaterial = FBmrBombRow::GetBombMaterial(MaterialIndex);
		}
	}

	if (!NewBombMaterial)
	{
		// No material set in entry, fall back to the mesh default slot
		checkf(MapComponent, TEXT("ERROR: [%i] %hs:\n'MapComponentInternal' is null!"), __LINE__, __FUNCTION__);
		const UStaticMesh* BombMesh = MapComponent->GetMesh<UStaticMesh>();
		if (ensureMsgf(BombMesh, TEXT("ASSERT: [%i] %hs:\n'BombMesh' is not found"), __LINE__, __FUNCTION__))
		{
			NewBombMaterial = BombMesh->GetMaterial(0);
		}
	}

	// Apply material
	if (NewBombMaterial)
	{
		checkf(MapComponent, TEXT("ERROR: [%i] %hs:\n'MapComponent' is null!"), __LINE__, __FUNCTION__);
		MapComponent->SetLocalMeshMaterial(NewBombMaterial);
	}
}

/*********************************************************************************************
 * Overrides
 ********************************************************************************************* */

// Called when an instance of this class is placed (in editor) or spawned.
void ABmrBombAbilityActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	BIND_ON_ADDED_TO_LEVEL(this, ThisClass::OnAddedToLevel);
	ABmrGeneratedMap::Get().AddToGrid(MapComponent);
}

// Returns properties that are replicated for the lifetime of the actor channel
void ABmrBombAbilityActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	FDoRepLifetimeParams Params;
	Params.bIsPushBased = true;

	DOREPLIFETIME_WITH_PARAMS_FAST(ThisClass, InstigatorAbilitySystemComponent, Params);
}

// Called on client to init bomb on clients when instigator's ASC is replicated
void ABmrBombAbilityActor::OnRep_InstigatorAbilitySystemComponent()
{
	if (InstigatorAbilitySystemComponent)
	{
		InitBomb(InstigatorAbilitySystemComponent);
	}
}

/*********************************************************************************************
 * Events
 ********************************************************************************************* */

// Called when this level actor is reconstructed or added on the Generated Map
void ABmrBombAbilityActor::OnAddedToLevel_Implementation(UBmrMapComponent* InMapComponent)
{
	checkf(InMapComponent, TEXT("ERROR: [%i] %hs:\n'MapComponent' is null!"), __LINE__, __FUNCTION__);
	InMapComponent->OnPostRemovedFromLevel.AddUniqueDynamic(this, &ThisClass::OnPostRemovedFromLevel);

	if (IsBombReady())
	{
		OnBombReady();
	}

#if WITH_EDITOR //[IsEditorNotPieWorld]
	if (FEditorUtilsLibrary::IsEditorNotPieWorld()) // [IsEditorNotPieWorld]
	{
		UBmrUnrealEdEngine::GOnAIUpdatedDelegate.Broadcast();
	}
#endif // WITH_EDITOR [IsEditorNotPieWorld]
}

// Called when bomb is fully initialized: both cell is valid and instigator ASC is set
void ABmrBombAbilityActor::OnBombReady_Implementation()
{
	UpdateExplosionCells();

	ApplyMesh();

	ApplyMaterial();
}

// Is used for cleaning up the bomb's data after it was removed from the level
void ABmrBombAbilityActor::OnPostRemovedFromLevel_Implementation(UBmrMapComponent* InMapComponent, UObject* DestroyCauser)
{
	checkf(InMapComponent, TEXT("ERROR: [%i] %hs:\n'MapComponentInternal' is null!"), __LINE__, __FUNCTION__);
	InMapComponent->OnPostRemovedFromLevel.RemoveAll(this);

	InstigatorAbilitySystemComponent = nullptr;
	MARK_PROPERTY_DIRTY_FROM_NAME(ThisClass, InstigatorAbilitySystemComponent, this);

	ExplosionCells = FBmrCell::EmptyCells;
}
