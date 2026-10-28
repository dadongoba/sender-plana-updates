#include "SenderFragmentSubsystem.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInterface.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogSenderFragment, Log, All);

bool USenderFragmentSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld();
}

void USenderFragmentSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	CollectBlocks();
	TotalBlocks = Blocks.Num();
	RemainingBlocks = TotalBlocks;

	if (bHideBeforeStart)
	{
		for (const TWeakObjectPtr<AActor>& Weak : Blocks)
		{
			if (AActor* Block = Weak.Get())
			{
				SetBlockSolidNone(Block, true);
			}
		}
	}

	if (bAutoStartOnBeginPlay)
	{
		if (StartDelay > KINDA_SMALL_NUMBER)
		{
			InWorld.GetTimerManager().SetTimer(
				StartTimerHandle, this, &USenderFragmentSubsystem::StartFragmentPhase, StartDelay, false);
		}
		else
		{
			StartFragmentPhase();
		}
	}

	UE_LOG(LogSenderFragment, Log, TEXT("Collected %d fragment blocks (auto start: %s)"),
		TotalBlocks, bAutoStartOnBeginPlay ? TEXT("yes") : TEXT("no"));
}

void USenderFragmentSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(StartTimerHandle);
	}
	Blocks.Reset();
	ClearedBlocks.Reset();
	Super::Deinitialize();
}

void USenderFragmentSubsystem::CollectBlocks()
{
	Blocks.Reset();
	ClearedBlocks.Reset();

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	TSet<AActor*> Seen;

	// 1) 优先按 Tag 找
	if (!BlockTag.IsNone())
	{
		TArray<AActor*> Tagged;
		UGameplayStatics::GetAllActorsWithTag(World, BlockTag, Tagged);
		for (AActor* Actor : Tagged)
		{
			if (!Actor || Seen.Contains(Actor))
			{
				continue;
			}
			TArray<UStaticMeshComponent*> Comps;
			Actor->GetComponents<UStaticMeshComponent>(Comps);
			if (Comps.Num() == 0)
			{
				continue;
			}
			Seen.Add(Actor);
			Blocks.Add(Actor);
		}
	}

	// 2) 材质名兜底，覆盖没有打 Tag 的方块（含以 fragment 为父级的材质实例）
	if (!MaterialKeyword.IsNone())
	{
		const FString Keyword = MaterialKeyword.ToString();
		for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
		{
			AStaticMeshActor* MeshActor = *It;
			if (!MeshActor || Seen.Contains(MeshActor))
			{
				continue;
			}

			bool bMatch = false;
			TArray<UStaticMeshComponent*> Comps;
			MeshActor->GetComponents<UStaticMeshComponent>(Comps);
			for (UStaticMeshComponent* Comp : Comps)
			{
				if (!Comp)
				{
					continue;
				}
				const int32 NumMats = Comp->GetNumMaterials();
				for (int32 Index = 0; Index < NumMats; ++Index)
				{
					const UMaterialInterface* Mat = Comp->GetMaterial(Index);
					if (!Mat)
					{
						continue;
					}
					if (Mat->GetPathName().Contains(Keyword, ESearchCase::IgnoreCase))
					{
						bMatch = true;
						break;
					}
					// 材质实例：再看一层父级
					if (const UMaterialInstance* MatInst = Cast<UMaterialInstance>(Mat))
					{
						const UMaterialInterface* Parent = MatInst->Parent;
						if (Parent && Parent->GetPathName().Contains(Keyword, ESearchCase::IgnoreCase))
						{
							bMatch = true;
							break;
						}
					}
				}
				if (bMatch)
				{
					break;
				}
			}

			if (bMatch)
			{
				Seen.Add(MeshActor);
				Blocks.Add(MeshActor);
			}
		}
	}
}

void USenderFragmentSubsystem::StartFragmentPhase()
{
	if (bPhaseActive)
	{
		return;
	}

	if (Blocks.Num() == 0)
	{
		CollectBlocks();
	}

	TotalBlocks = Blocks.Num();
	RemainingBlocks = TotalBlocks;
	ClearedBlocks.Reset();
	bPhaseActive = true;

	for (const TWeakObjectPtr<AActor>& Weak : Blocks)
	{
		if (AActor* Block = Weak.Get())
		{
			ApplyGhostState(Block);
		}
	}

	UE_LOG(LogSenderFragment, Log, TEXT("Phase started: %d blocks"), TotalBlocks);
}

void USenderFragmentSubsystem::ResetFragmentPhase()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(StartTimerHandle);
	}

	bPhaseActive = false;
	ClearedBlocks.Reset();

	for (const TWeakObjectPtr<AActor>& Weak : Blocks)
	{
		if (AActor* Block = Weak.Get())
		{
			SetBlockSolidNone(Block, true);
		}
	}

	// 开了 bDestroyOnTouch 的话，被销毁的方块需要重新收集一遍
	CollectBlocks();
	TotalBlocks = Blocks.Num();
	RemainingBlocks = TotalBlocks;
}

void USenderFragmentSubsystem::ApplyGhostState(AActor* Block)
{
	if (!Block)
	{
		return;
	}

	Block->SetActorHiddenInGame(false);

	TArray<UStaticMeshComponent*> Comps;
	Block->GetComponents<UStaticMeshComponent>(Comps);
	for (UStaticMeshComponent* Comp : Comps)
	{
		if (!Comp)
		{
			continue;
		}

		// 没有实体：不阻挡玩家，只保留 Overlap 查询
		Comp->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Comp->SetCollisionObjectType(ECC_WorldDynamic);
		Comp->SetCollisionResponseToAllChannels(ECR_Ignore);
		Comp->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
		Comp->SetGenerateOverlapEvents(true);
		Comp->SetCanEverAffectNavigation(false);

		Comp->OnComponentBeginOverlap.RemoveDynamic(this, &USenderFragmentSubsystem::HandleBlockBeginOverlap);
		Comp->OnComponentBeginOverlap.AddDynamic(this, &USenderFragmentSubsystem::HandleBlockBeginOverlap);
	}
}

void USenderFragmentSubsystem::SetBlockSolidNone(AActor* Block, bool bInHidden)
{
	if (!Block)
	{
		return;
	}

	TArray<UStaticMeshComponent*> Comps;
	Block->GetComponents<UStaticMeshComponent>(Comps);
	for (UStaticMeshComponent* Comp : Comps)
	{
		if (!Comp)
		{
			continue;
		}
		Comp->OnComponentBeginOverlap.RemoveDynamic(this, &USenderFragmentSubsystem::HandleBlockBeginOverlap);
		Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Comp->SetGenerateOverlapEvents(false);
	}

	Block->SetActorHiddenInGame(bInHidden);
}

void USenderFragmentSubsystem::HandleBlockBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (!bPhaseActive || !OverlappedComp)
	{
		return;
	}

	AActor* Block = OverlappedComp->GetOwner();
	if (!Block || ClearedBlocks.Contains(Block))
	{
		return;
	}

	if (bOnlyPlayerTriggers)
	{
		const APawn* Pawn = Cast<APawn>(OtherActor);
		if (!Pawn || !Pawn->IsPlayerControlled())
		{
			return;
		}
	}

	ClearBlock(Block);
}

void USenderFragmentSubsystem::ClearBlock(AActor* Block)
{
	if (!Block)
	{
		return;
	}

	ClearedBlocks.Add(Block);
	RemainingBlocks = FMath::Max(0, RemainingBlocks - 1);

	SetBlockSolidNone(Block, true);

	if (bDestroyOnTouch)
	{
		Block->Destroy();
	}

	UE_LOG(LogSenderFragment, Log, TEXT("Fragment cleared, %d / %d remaining"), RemainingBlocks, TotalBlocks);

	if (RemainingBlocks <= 0)
	{
		OnAllFragmentsCleared.Broadcast(TotalBlocks);
	}
}
