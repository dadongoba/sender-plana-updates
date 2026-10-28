#include "SenderWeaponStripperSubsystem.h"

#include "Components/ActorComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogSenderWeaponStripper, Log, All);

USenderWeaponStripperSubsystem::USenderWeaponStripperSubsystem()
{
	WeaponClassKeywords = { TEXT("Rifle_Actor"), TEXT("Pistol_Actor"), TEXT("BP_I_Rifle"), TEXT("BP_I_Pistol") };
	HideComponentKeywords = { TEXT("Rifle-SkeletalRoot"), TEXT("Rifle-Attach-Root"), TEXT("Pistol-SkeletalRoot"), TEXT("Pistol-Attach-Root") };
}

bool USenderWeaponStripperSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return bRemoveWeapons && World && World->IsGameWorld();
}

void USenderWeaponStripperSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	if (!bRemoveWeapons)
	{
		return;
	}

	bFinished = false;
	ElapsedSeconds = 0.f;
	RemovedCount = 0;

	// 稍微等一会儿：角色的 BeginPlay、武器 Spawn、Attach 都发生在同一帧附近。
	InWorld.GetTimerManager().SetTimer(
		ScanHandle,
		FTimerDelegate::CreateUObject(this, &USenderWeaponStripperSubsystem::StartScanning, &InWorld),
		0.3f, false);
}

void USenderWeaponStripperSubsystem::StartScanning(UWorld* InWorld)
{
	Scan();

	if (!InWorld)
	{
		return;
	}

	const float Interval = FMath::Max(ScanInterval, 0.02f);
	InWorld->GetTimerManager().SetTimer(
		ScanHandle,
		FTimerDelegate::CreateUObject(this, &USenderWeaponStripperSubsystem::Scan),
		Interval, true);
}

bool USenderWeaponStripperSubsystem::IsWeaponActor(const AActor* Actor) const
{
	if (!Actor)
	{
		return false;
	}

	const FString ClassName = Actor->GetClass()->GetName();
	for (const FString& Keyword : WeaponClassKeywords)
	{
		if (!Keyword.IsEmpty() && ClassName.Contains(Keyword, ESearchCase::IgnoreCase))
		{
			return true;
		}
	}
	return false;
}

void USenderWeaponStripperSubsystem::HidePlaceholderComponents(AActor* Actor)
{
	if (!Actor || HideComponentKeywords.Num() == 0)
	{
		return;
	}

	TArray<UPrimitiveComponent*> PrimitiveComponents;
	Actor->GetComponents<UPrimitiveComponent>(PrimitiveComponents);

	for (UPrimitiveComponent* Component : PrimitiveComponents)
	{
		if (!IsValid(Component))
		{
			continue;
		}

		const FString ComponentName = Component->GetName();
		for (const FString& Keyword : HideComponentKeywords)
		{
			if (!Keyword.IsEmpty() && ComponentName.Contains(Keyword, ESearchCase::IgnoreCase))
			{
				Component->SetVisibility(false, true);
				Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				break;
			}
		}
	}
}

void USenderWeaponStripperSubsystem::Scan()
{
	UWorld* World = GetWorld();
	if (!World || !bRemoveWeapons || bFinished)
	{
		return;
	}

	ElapsedSeconds += FMath::Max(ScanInterval, 0.02f);

	TArray<AActor*> ToRemove;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!IsValid(Actor))
		{
			continue;
		}

		if (IsWeaponActor(Actor))
		{
			ToRemove.Add(Actor);
			continue;
		}

		// 占位组件只需要在开头几轮藏一次，避免长时间每帧遍历。
		if (ElapsedSeconds <= 2.f)
		{
			HidePlaceholderComponents(Actor);
		}
	}

	for (AActor* Actor : ToRemove)
	{
		UE_LOG(LogSenderWeaponStripper, Log, TEXT("Removing weapon actor: %s (%s)"),
			*Actor->GetName(), *Actor->GetClass()->GetName());
		Actor->Destroy();
		++RemovedCount;
	}

	if (ToRemove.Num() > 0)
	{
		UE_LOG(LogSenderWeaponStripper, Log, TEXT("Removed %d weapon actor(s) so far."), RemovedCount);
	}

	if (ElapsedSeconds >= ScanDuration)
	{
		bFinished = true;
		if (UWorld* CurrentWorld = GetWorld())
		{
			CurrentWorld->GetTimerManager().ClearTimer(ScanHandle);
		}
		UE_LOG(LogSenderWeaponStripper, Log, TEXT("Scan finished, total removed: %d."), RemovedCount);
	}
}

void USenderWeaponStripperSubsystem::StripNow()
{
	Scan();
}

void USenderWeaponStripperSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ScanHandle);
	}
	Super::Deinitialize();
}
