// Editor-only helper: bake a fully assembled actor (static + skeletal mesh components)
// into a single StaticMesh asset, preserving the original material slots.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "SenderMeshMerge.generated.h"

class AActor;
class UStaticMesh;

UCLASS()
class SENDER_API USenderMeshMerge : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Merge every visible mesh component of SourceActor into one StaticMesh asset.
	 * Skeletal mesh components are baked using their current (pose) vertices.
	 *
	 * @param SourceActor        Actor to bake. Its components are only read, never modified.
	 * @param PackagePath        Target content folder, e.g. "/Game/CarModels"
	 * @param AssetName          Target asset name, e.g. "SM_Car_Sedan"
	 * @param bMergePhysicsData  Generate a simple collision body from the merged geometry
	 * @return The created UStaticMesh, or nullptr on failure.
	 */
	UFUNCTION(BlueprintCallable, Category = "Sender|MeshMerge")
	static UStaticMesh* MergeActorToStaticMesh(
		AActor* SourceActor,
		const FString& PackagePath,
		const FString& AssetName,
		bool bMergePhysicsData = true);

	/**
	 * Temporarily attach extra part meshes (doors, bumpers, hood, trunk...) to an actor,
	 * merge everything, then remove the temporary components again.
	 *
	 * The DD_Vehicles vehicle body skeletal mesh does NOT contain its doors/bumpers;
	 * those live as separate meshes whose vertices are already in full-vehicle space,
	 * so simply attaching them at identity transforms reassembles the complete car.
	 *
	 * @param SourceActor            Actor providing the body (and its pivot)
	 * @param ExtraPartAssetPaths    Asset paths of the additional mesh parts
	 */
	UFUNCTION(BlueprintCallable, Category = "Sender|MeshMerge")
	static UStaticMesh* MergeActorWithParts(
		AActor* SourceActor,
		const TArray<FString>& ExtraPartAssetPaths,
		const FString& PackagePath,
		const FString& AssetName,
		bool bMergePhysicsData = true);
};
