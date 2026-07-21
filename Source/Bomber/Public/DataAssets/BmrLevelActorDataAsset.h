// Copyright (c) Yevhenii Selivanov.

#pragma once

#include "DalPrimaryDataAsset.h"

// Bomber
#include "Bomber.h" // EBmrActorType

#include "BmrLevelActorDataAsset.generated.h"

/**
 * The base data asset for the Bomber's data.
 */
UCLASS(Abstract, Blueprintable, BlueprintType, Const, AutoExpandCategories = ("[Bomber]"))
class BOMBER_API UBmrBaseDataAsset : public UDalPrimaryDataAsset
{
	GENERATED_BODY()

protected:
#if WITH_EDITOR
	/** Called to notify on any data asset changes. */
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif // WITH_EDITOR
};

/**
 * The base data asset for any level actor that contains the main data about them.
 */
UCLASS(Abstract, Blueprintable, BlueprintType)
class BOMBER_API UBmrLevelActorDataAsset : public UBmrBaseDataAsset
{
	GENERATED_BODY()

public:
	/** Returns the row struct type used by this data asset's Data Registry */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "[Bomber]")
	const class UScriptStruct* GetRowType() const { return RowType; }

	/** Returns the class of an actor, whose data is described by this data asset. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "[Bomber]")
	TSubclassOf<class AActor> GetActorClass() const { return ActorClass; }

	/** Returns the actor type of an actor, whose data is described by this data asset. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "[Bomber]")
	FORCEINLINE EBmrActorType GetActorType() const { return ActorType; }

	/** Asset registry tag key for the actor class path */
	static inline const FName ActorClassTag = TEXT("ActorClass");

	/** Asset registry tag key for the actor type value */
	static inline const FName ActorTypeTag = TEXT("ActorType");

	/** Asset registry tag key for the row struct type */
	static inline const FName RowTypeTag = TEXT("RowType");

	/** Adds ActorClass and ActorType as asset registry tags for discovery without loading */
	virtual void GetAssetRegistryTags(class FAssetRegistryTagsContext Context) const override;

protected:
	/** Class of an actor, whose data is described by this data asset. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta = (BlueprintProtected, ShowOnlyInnerProperties))
	TSubclassOf<class AActor> ActorClass = nullptr;

	/** Actor type of an actor, whose data is described by this data asset. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta = (BlueprintProtected, ShowOnlyInnerProperties))
	EBmrActorType ActorType = EAT::None;

	/** The row struct type used by this data asset's Data Registry.
	 * Is set to associate this DA with its DR row type */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta = (BlueprintProtected, ShowOnlyInnerProperties))
	TObjectPtr<const UScriptStruct> RowType = nullptr;

	/*********************************************************************************************
	 * Collision
	 ********************************************************************************************* */
public:
	/** Returns true if the collision is enabled for an actor, whose data is described by this data asset. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "[Bomber]")
	FORCEINLINE bool IsEnabledCollision() const { return bEnableCollision; }

	/** Returns an extent size of the collision box of an actor, whose data is described by this data asset. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "[Bomber]")
	const FORCEINLINE FVector& GetCollisionExtent() const { return CollisionExtent; }

protected:
	/** If enabled, actor described by this data asset blocks its cell on grid.
	 * Blocking prevents entering cell but never pushes out pawn already standing on it, so such pawn still leaves freely, but never enters again. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Collision", meta = (BlueprintProtected, ShowOnlyInnerProperties))
	bool bEnableCollision = true;

	/** Extent size of the collision box of an actor, whose data is described by this data asset. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Collision", meta = (BlueprintProtected, ShowOnlyInnerProperties))
	FVector CollisionExtent = FVector(100.f);
};
