// Copyright (c) Yevhenii Selivanov

#pragma once

#include "EditorSubsystem.h"

#include "GfpmPackageEditorSubsystem.generated.h"

/**
 * Packages Game Feature Plugins as separate mods from running editor and installs them into chosen build.
 */
UCLASS(Config = "GameFeaturePluginsManager", DefaultConfig, DisplayName = "Game Feature Plugins Package Editor Subsystem")
class GAMEFEATUREPLUGINSMANAGEREDITOR_API UGfpmPackageEditorSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	/** Packages given plugin as separate mod and installs it into build at given path.
	 * @param GameFeatureName Plugin to package.
	 * @param BuildPath Build to install into, or any folder for separate distribution. */
	UFUNCTION(BlueprintCallable, Category = "[Game Feature Plugins Manager Editor]", meta = (AutoCreateRefTerm = "BuildPath"))
	void PackageGameFeatureIntoBuild(FName GameFeatureName, const FString& BuildPath);

	/** Packages every registered plugin as separate mod and installs them into build at given path.
	 * @param BuildPath Build to install into, or any folder for separate distribution. */
	UFUNCTION(BlueprintCallable, Category = "[Game Feature Plugins Manager Editor]", meta = (AutoCreateRefTerm = "BuildPath"))
	void PackageAllGameFeaturesIntoBuild(const FString& BuildPath);

	/** Prompts developer for target build folder then packages given plugin into it. */
	UFUNCTION(BlueprintCallable, Category = "[Game Feature Plugins Manager Editor]")
	void PromptPackageGameFeature(FName GameFeatureName);

	/** Prompts developer for target build folder then packages every registered plugin into it. */
	UFUNCTION(BlueprintCallable, Category = "[Game Feature Plugins Manager Editor]")
	void PromptPackageAllGameFeatures();

protected:
	/** Release version mod is cooked against, must match build's release. */
	UPROPERTY(Config, EditDefaultsOnly, BlueprintReadOnly, Category = "[Game Feature Plugins Manager Editor]", meta = (BlueprintProtected))
	FString ProjectReleaseVersion;

	/** Cook arguments for one mod DLC, override per project pipeline. */
	UPROPERTY(Config, EditDefaultsOnly, BlueprintReadOnly, Category = "[Game Feature Plugins Manager Editor]", meta = (BlueprintProtected))
	FString CookModArguments;

	/** Cook arguments for full project package excluding game features, override per project pipeline. */
	UPROPERTY(Config, EditDefaultsOnly, BlueprintReadOnly, Category = "[Game Feature Plugins Manager Editor]", meta = (BlueprintProtected))
	FString CookProjectAllArguments;

	/** Cook arguments for minimal project cook (no -build, no stage) emitting release for mod, override per project pipeline. */
	UPROPERTY(Config, EditDefaultsOnly, BlueprintReadOnly, Category = "[Game Feature Plugins Manager Editor]", meta = (BlueprintProtected))
	FString CookProjectMinimalArguments;

	/** Extra cooker options appended to engine additional cooker args, override per project pipeline. */
	UPROPERTY(Config, EditDefaultsOnly, BlueprintReadOnly, Category = "[Game Feature Plugins Manager Editor]", meta = (BlueprintProtected))
	FString AdditionalCookerOptions;

private:
	/** Opens desktop folder picker with given dialog title, empty when developer cancels. Not exposed: editor desktop dialog. */
	FString PromptBuildDir(const FString& DialogTitle) const;

	/** Cooks one plugin as mod DLC into build, runs continuation after install. Not exposed: TFunction continuation. */
	void CookMod(FName GameFeatureName, const FString& BuildPath, TFunction<void()> OnComplete);

	/** Cooks full project excluding game features into build path, runs continuation after it succeeds. Not exposed: TFunction continuation. */
	void CookProjectAll(const FString& BuildPath, TFunction<void()> OnComplete);

	/** Cooks minimal project (game features excluded, no -build, no stage) so mod cook has release to cook against, runs continuation after it succeeds. Not exposed: TFunction continuation. */
	void CookProjectMinimal(TFunction<void()> OnComplete);

	/** Cooks queue head into build then recurses on completion, so plugins cook one after another. Not exposed: private recursive implementation, covered by public BP wrapper. */
	void CookModQueue(const TArray<FName>& Queue, const FString& BuildPath);

	/** Installs cooked mod into build then drives queue continuation, runs on game thread after mod cook task completes. Not exposed: TFunction continuation. */
	void HandleCookModFinished(const FString& Result, FName GameFeatureName, const FString& ArchiveDir, const FString& TargetPaksDir, const FString& CookedPlatform, const TFunction<void()>& OnComplete);

	/** Drives queue continuation once project cook succeeds, runs on game thread after project cook task completes. Not exposed: TFunction continuation. */
	void HandleCookProjectAllFinished(const FString& Result, const TFunction<void()>& OnComplete);
};
