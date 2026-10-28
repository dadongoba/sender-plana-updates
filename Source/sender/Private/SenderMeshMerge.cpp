#include "SenderMeshMerge.h"

#if WITH_EDITOR

#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "IMeshMergeUtilities.h"
#include "MeshDescription.h"
#include "MeshMergeModule.h"
#include "Modules/ModuleManager.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshOperations.h"
#include "UObject/Package.h"

namespace
{
	/** Collects ImportedMaterialSlotName -> material for every mesh asset used by the actor.
	 *  Component-level material overrides take priority over the mesh asset defaults. */
	void GatherSlotMaterials(AActor* SourceActor, const TArray<UPrimitiveComponent*>& Components, TMap<FName, UMaterialInterface*>& OutMaterials)
	{
		for (UPrimitiveComponent* Component : Components)
		{
			if (const UStaticMeshComponent* StaticComp = Cast<UStaticMeshComponent>(Component))
			{
				if (const UStaticMesh* MeshAsset = StaticComp->GetStaticMesh())
				{
					const TArray<FStaticMaterial>& Slots = MeshAsset->GetStaticMaterials();
					for (int32 SlotIndex = 0; SlotIndex < Slots.Num(); ++SlotIndex)
					{
						UMaterialInterface* Material = StaticComp->GetMaterial(SlotIndex);
						if (!Material)
						{
							Material = Slots[SlotIndex].MaterialInterface;
						}
						if (Material)
						{
							OutMaterials.FindOrAdd(Slots[SlotIndex].ImportedMaterialSlotName, Material);
						}
					}
				}
			}
			else if (const USkeletalMeshComponent* SkelComp = Cast<USkeletalMeshComponent>(Component))
			{
				if (const USkeletalMesh* MeshAsset = SkelComp->GetSkeletalMeshAsset())
				{
					const TArray<FSkeletalMaterial>& Slots = MeshAsset->GetMaterials();
					for (int32 SlotIndex = 0; SlotIndex < Slots.Num(); ++SlotIndex)
					{
						UMaterialInterface* Material = SkelComp->GetMaterial(SlotIndex);
						if (!Material)
						{
							Material = Slots[SlotIndex].MaterialInterface;
						}
						if (Material)
						{
							OutMaterials.FindOrAdd(Slots[SlotIndex].ImportedMaterialSlotName, Material);
						}
					}
				}
			}
		}
	}
}

UStaticMesh* USenderMeshMerge::MergeActorToStaticMesh(
	AActor* SourceActor,
	const FString& PackagePath,
	const FString& AssetName,
	bool bMergePhysicsData)
{
	if (!SourceActor || PackagePath.IsEmpty() || AssetName.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("[SenderMeshMerge] Invalid arguments."));
		return nullptr;
	}

	UWorld* World = SourceActor->GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Warning, TEXT("[SenderMeshMerge] SourceActor has no world."));
		return nullptr;
	}

	// Gather every visible mesh component (static or skeletal) that has a mesh assigned.
	TArray<UPrimitiveComponent*> AllComponents;
	SourceActor->GetComponents<UPrimitiveComponent>(AllComponents);

	TArray<UPrimitiveComponent*> ComponentsToMerge;
	ComponentsToMerge.Reserve(AllComponents.Num());

	for (UPrimitiveComponent* Component : AllComponents)
	{
		if (!Component || !Component->IsVisible())
		{
			continue;
		}

		if (const UStaticMeshComponent* StaticComp = Cast<UStaticMeshComponent>(Component))
		{
			if (!StaticComp->GetStaticMesh())
			{
				continue;
			}
		}
		else if (const USkeletalMeshComponent* SkelComp = Cast<USkeletalMeshComponent>(Component))
		{
			if (!SkelComp->GetSkeletalMeshAsset())
			{
				continue;
			}
		}
		else
		{
			continue;
		}

		ComponentsToMerge.Add(Component);
	}

	if (ComponentsToMerge.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[SenderMeshMerge] %s has no mergeable mesh components."), *SourceActor->GetName());
		return nullptr;
	}

	IMeshMergeUtilities& Merger =
		FModuleManager::LoadModuleChecked<IMeshMergeModule>(TEXT("MeshMergeUtilities")).GetUtilities();

	// Extract one mesh description per component. RetrieveMeshDescription bakes skeletal meshes
	// using their current pose and already applies each component's world transform.
	TArray<TUniquePtr<FMeshDescription>> SourceDescriptions;
	TArray<const FMeshDescription*> SourceDescriptionPtrs;

	for (UPrimitiveComponent* Component : ComponentsToMerge)
	{
		TUniquePtr<FMeshDescription> Description = MakeUnique<FMeshDescription>();
		FStaticMeshAttributes Attributes(*Description);
		Attributes.Register();

		if (const UStaticMeshComponent* StaticComp = Cast<UStaticMeshComponent>(Component))
		{
			Merger.RetrieveMeshDescription(StaticComp, 0, *Description, /*bPropagateMeshData=*/ false);
		}
		else if (const USkeletalMeshComponent* SkelComp = Cast<USkeletalMeshComponent>(Component))
		{
			Merger.RetrieveMeshDescription(SkelComp, 0, *Description, /*bPropagateMeshData=*/ false);
		}

		if (Description->Triangles().Num() == 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("[SenderMeshMerge] Component %s produced no triangles, skipping."), *Component->GetName());
			continue;
		}

		SourceDescriptionPtrs.Add(Description.Get());
		SourceDescriptions.Add(MoveTemp(Description));
	}

	if (SourceDescriptionPtrs.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[SenderMeshMerge] No geometry extracted from %s."), *SourceActor->GetName());
		return nullptr;
	}

	// NOTE: RetrieveMeshDescription() never passes bApplyComponentTransform, so the retrieved vertices stay in
	// COMPONENT-LOCAL space. Their origin already is the vehicle origin, which is exactly the pivot we want.
	// Subtracting the actor's world location here (the engine's own merge path does that, because it retrieves
	// world-space geometry) would shift the whole car by -ActorLocation - e.g. 2 km straight down for a car
	// spawned at Z=200000 to keep it out of sight. Keep the pivot at zero.
	const FVector PivotLocation = FVector::ZeroVector;

	FMeshDescription MergedDescription;
	FStaticMeshAttributes MergedAttributes(MergedDescription);
	MergedAttributes.Register();

	FStaticMeshOperations::FAppendSettings AppendSettings;
	AppendSettings.MergedAssetPivot = PivotLocation;
	AppendSettings.bMergeVertexColor = true;
	AppendSettings.bMergeObjectName = false;
	for (int32 ChannelIndex = 0; ChannelIndex < FStaticMeshOperations::FAppendSettings::MAX_NUM_UV_CHANNELS; ++ChannelIndex)
	{
		AppendSettings.bMergeUVChannels[ChannelIndex] = true;
	}

	FStaticMeshOperations::AppendMeshDescriptions(SourceDescriptionPtrs, MergedDescription, AppendSettings);

	if (MergedDescription.Triangles().Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[SenderMeshMerge] Append produced no triangles for %s."), *SourceActor->GetName());
		return nullptr;
	}

	// Create the destination package and asset.
	FString FullPackageName = PackagePath;
	while (FullPackageName.EndsWith(TEXT("/")))
	{
		FullPackageName.LeftChopInline(1);
	}
	FullPackageName += TEXT("/") + AssetName;

	UPackage* Package = CreatePackage(*FullPackageName);

	UStaticMesh* MergedMesh = NewObject<UStaticMesh>(
		Package, FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional);

	// StaticMaterials must exist (in polygon-group order) before the build so that material
	// indices line up with the merged description's polygon groups.
	TArray<FName> SlotNamesInOrder;
	{
		FStaticMeshAttributes MergedAttrRef(MergedDescription);
		TPolygonGroupAttributesRef<FName> SlotNameAttr = MergedAttrRef.GetPolygonGroupMaterialSlotNames();
		for (const FPolygonGroupID GroupID : MergedDescription.PolygonGroups().GetElementIDs())
		{
			SlotNamesInOrder.Add(SlotNameAttr[GroupID]);
		}
	}

	MergedMesh->GetStaticMaterials().Reset();
	for (const FName& SlotName : SlotNamesInOrder)
	{
		MergedMesh->GetStaticMaterials().Add(FStaticMaterial(nullptr, SlotName, SlotName));
	}

	TArray<const FMeshDescription*> DescriptionsToBuild;
	DescriptionsToBuild.Add(&MergedDescription);

	UStaticMesh::FBuildMeshDescriptionsParams BuildParams;
	BuildParams.bBuildSimpleCollision = bMergePhysicsData;
	BuildParams.bCommitMeshDescription = true;
	BuildParams.bMarkPackageDirty = true;
	BuildParams.bUseHashAsGuid = true;
	BuildParams.bFastBuild = false;

	MergedMesh->BuildFromMeshDescriptions(DescriptionsToBuild, BuildParams);

	if (MergedMesh->GetNumTriangles(0) == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[SenderMeshMerge] BuildFromMeshDescriptions produced an empty mesh for %s."), *SourceActor->GetName());
		return nullptr;
	}

	// Re-link the original materials by slot name.
	TMap<FName, UMaterialInterface*> SlotMaterials;
	GatherSlotMaterials(SourceActor, ComponentsToMerge, SlotMaterials);

	TArray<FStaticMaterial>& TargetSlots = MergedMesh->GetStaticMaterials();
	for (int32 SlotIndex = 0; SlotIndex < TargetSlots.Num(); ++SlotIndex)
	{
		const FName SlotName = TargetSlots[SlotIndex].MaterialSlotName;
		if (UMaterialInterface* const* FoundMaterial = SlotMaterials.Find(SlotName))
		{
			MergedMesh->SetMaterial(SlotIndex, *FoundMaterial);
		}
		else if (UMaterialInterface* const* FoundByName = SlotMaterials.Find(TargetSlots[SlotIndex].ImportedMaterialSlotName))
		{
			MergedMesh->SetMaterial(SlotIndex, *FoundByName);
		}
	}

	MergedMesh->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(MergedMesh);
	MergedMesh->PostEditChange();

	UE_LOG(LogTemp, Log, TEXT("[SenderMeshMerge] Merged %d components of %s -> %s (%d tris, %d slots)"),
		ComponentsToMerge.Num(), *SourceActor->GetName(), *FullPackageName,
		MergedMesh->GetNumTriangles(0), TargetSlots.Num());

	return MergedMesh;
}

UStaticMesh* USenderMeshMerge::MergeActorWithParts(
	AActor* SourceActor,
	const TArray<FString>& ExtraPartAssetPaths,
	const FString& PackagePath,
	const FString& AssetName,
	bool bMergePhysicsData)
{
	if (!SourceActor)
	{
		return nullptr;
	}

	USceneComponent* RootComponent = SourceActor->GetRootComponent();
	TArray<UActorComponent*> TempComponents;
	int32 PartIndex = 0;

	for (const FString& AssetPath : ExtraPartAssetPaths)
	{
		if (AssetPath.IsEmpty())
		{
			continue;
		}

		UObject* LoadedAsset = LoadObject<UObject>(nullptr, *AssetPath);
		if (!LoadedAsset)
		{
			UE_LOG(LogTemp, Warning, TEXT("[SenderMeshMerge] Cannot load part '%s'."), *AssetPath);
			continue;
		}

		const FName ComponentName = *FString::Printf(TEXT("MergePart_%d"), PartIndex++);

		if (USkeletalMesh* SkeletalAsset = Cast<USkeletalMesh>(LoadedAsset))
		{
			USkeletalMeshComponent* PartComp =
				NewObject<USkeletalMeshComponent>(SourceActor, ComponentName, RF_Transactional);
			PartComp->SetSkeletalMeshAsset(SkeletalAsset);
			PartComp->SetAnimationMode(EAnimationMode::AnimationSingleNode);
			PartComp->SetupAttachment(RootComponent);
			SourceActor->AddInstanceComponent(PartComp);
			PartComp->RegisterComponent();
			PartComp->SetRelativeTransform(FTransform::Identity);
			TempComponents.Add(PartComp);
		}
		else if (UStaticMesh* StaticAsset = Cast<UStaticMesh>(LoadedAsset))
		{
			UStaticMeshComponent* PartComp =
				NewObject<UStaticMeshComponent>(SourceActor, ComponentName, RF_Transactional);
			PartComp->SetStaticMesh(StaticAsset);
			PartComp->SetupAttachment(RootComponent);
			SourceActor->AddInstanceComponent(PartComp);
			PartComp->RegisterComponent();
			PartComp->SetRelativeTransform(FTransform::Identity);
			TempComponents.Add(PartComp);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[SenderMeshMerge] Part '%s' is not a mesh."), *AssetPath);
		}
	}

	UStaticMesh* MergedMesh = MergeActorToStaticMesh(SourceActor, PackagePath, AssetName, bMergePhysicsData);

	for (UActorComponent* TempComponent : TempComponents)
	{
		if (TempComponent)
		{
			TempComponent->DestroyComponent();
		}
	}

	return MergedMesh;
}

#else

UStaticMesh* USenderMeshMerge::MergeActorToStaticMesh(
	AActor* SourceActor,
	const FString& PackagePath,
	const FString& AssetName,
	bool bMergePhysicsData)
{
	return nullptr;
}

UStaticMesh* USenderMeshMerge::MergeActorWithParts(
	AActor* SourceActor,
	const TArray<FString>& ExtraPartAssetPaths,
	const FString& PackagePath,
	const FString& AssetName,
	bool bMergePhysicsData)
{
	return nullptr;
}

#endif
