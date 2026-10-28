#include "SenderCableCarRoute.h"
#include "Components/SplineComponent.h"

ASenderCableCarRoute::ASenderCableCarRoute()
{
    PrimaryActorTick.bCanEverTick=false;
    RouteSpline=CreateDefaultSubobject<USplineComponent>(TEXT("RouteSpline"));
    SetRootComponent(RouteSpline);
    RouteSpline->SetMobility(EComponentMobility::Static);
    RouteSpline->ReparamStepsPerSegment=32;
#if WITH_EDITOR
    RouteSpline->SetUnselectedSplineSegmentColor(FLinearColor(0.1f,1.f,0.3f));
    RouteSpline->SetSelectedSplineSegmentColor(FLinearColor(1.f,0.7f,0.05f));
#endif
    // Baked once from the approved loop. No mesh inspection occurs during gameplay.
    SeedPolyline={
        FVector(-4034.596864,841.750915,3037.399771),
        FVector(-9376.694431,1829.419702,1077.067390),
        FVector(-9355.685789,1884.995617,1082.510706),
        FVector(-9355.374524,1768.746426,1082.208685),
        FVector(-9344.310589,1800.673013,1082.951375),
        FVector(-9539.280527,2873.308105,896.734610),
        FVector(-9523.750000,2813.000000,903.000000),
        FVector(-9554.007676,2862.202034,903.000000),
        FVector(-9730.008105,2915.369464,903.000000),
        FVector(-9961.196113,2886.804270,903.000000),
        FVector(-10031.326228,2833.362552,903.000000),
        FVector(-10036.250000,2813.000000,903.000000),
        FVector(-10036.590535,2861.990666,903.483863),
        FVector(-10033.904283,1858.747648,900.877395),
        FVector(-10037.465671,1878.567482,901.984550),
        FVector(-9681.217684,907.878986,899.298193),
        FVector(-9685.374524,878.746426,907.208685),
        FVector(-9685.685789,994.995617,907.510706),
        FVector(-9708.875996,938.396319,898.471322),
        FVector(-3874.700521,-140.249838,3039.376267),
        FVector(-3898.000000,-135.125000,3029.000000),
        FVector(-2599.250000,-135.125000,3029.000000),
        FVector(-2608.000000,-135.125000,3029.000000),
        FVector(-2102.500011,-135.125000,3029.000000),
        FVector(-2109.763872,-193.425505,3028.000000),
        FVector(-2059.316931,-171.191915,3028.000000),
        FVector(-1999.516234,-76.220869,3028.000000),
        FVector(-1943.462600,173.031569,3028.000000),
        FVector(-1949.208058,520.514447,3028.000000),
        FVector(-1999.516248,722.057363,3028.000000),
        FVector(-2059.316949,817.028408,3028.000000),
        FVector(-2109.763890,839.261995,3028.000000),
        FVector(-2062.250012,814.875000,3029.000000),
        FVector(-3908.000000,814.875000,3029.000000),
        FVector(-3887.780559,813.928559,3029.000000),
        FVector(-4058.644704,844.056518,3029.000000)
    };
    RebuildSmoothRoute();
}

void ASenderCableCarRoute::RebuildSmoothRoute()
{
    if(!RouteSpline || SeedPolyline.Num()<3) return;
    RouteSpline->ClearSplinePoints(false);
    const int32 N=SeedPolyline.Num();
    TArray<FVector> Entries,Exits;
    for(int32 I=0;I<N;++I)
    {
        const FVector P=SeedPolyline[I];
        const FVector In=P-SeedPolyline[(I+N-1)%N];
        const FVector Out=SeedPolyline[(I+1)%N]-P;
        const float Cut=FMath::Min3(FMath::Max(CornerBlendDistance,1.f),float(In.Size()*0.35),float(Out.Size()*0.35));
        Entries.Add(P-In.GetSafeNormal()*Cut);
        Exits.Add(P+Out.GetSafeNormal()*Cut);
        RouteSpline->AddSplinePoint(Entries.Last(),ESplineCoordinateSpace::Local,false);
        RouteSpline->AddSplinePoint(Exits.Last(),ESplineCoordinateSpace::Local,false);
    }
    RouteSpline->SetClosedLoop(true,false);
    for(int32 I=0;I<N;++I)
    {
        const FVector P=SeedPolyline[I];
        const FVector Arrive=Entries[I]-Exits[(I+N-1)%N];
        const FVector Leave=Entries[(I+1)%N]-Exits[I];
        RouteSpline->SetSplinePointType(2*I,ESplinePointType::CurveCustomTangent,false);
        RouteSpline->SetSplinePointType(2*I+1,ESplinePointType::CurveCustomTangent,false);
        // Quadratic corner converted exactly to cubic Hermite. Entry/exit tangent
        // directions match the neighbouring straight segments, including the seam.
        RouteSpline->SetTangentsAtSplinePoint(2*I,Arrive,2.*(P-Entries[I]),ESplineCoordinateSpace::Local,false);
        RouteSpline->SetTangentsAtSplinePoint(2*I+1,2.*(Exits[I]-P),Leave,ESplineCoordinateSpace::Local,false);
    }
    RouteSpline->UpdateSpline();
}
