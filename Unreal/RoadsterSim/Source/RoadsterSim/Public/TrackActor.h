#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TrackActor.generated.h"

class UProceduralMeshComponent;
class UInstancedStaticMeshComponent;
class UPointLightComponent;
class UMaterialInterface;

UENUM(BlueprintType)
enum class ETrackPiece : uint8
{
	Straight,
	Turn,        // yaw by Angle over Radius, banking towards Bank
	Loop,        // vertical loop of Radius, offset sideways by Offset so it never self-intersects
	Corkscrew,   // roll Angle around an axis Radius above the road (open air, can leave the car inverted)
	Pitch,       // climb / descend by Angle over Length (hills, kickers, landings)
	Gap,         // no road: a jump gap of Length, dropping by Offset
	Tube,        // enclosed circular tunnel of Radius (drive anywhere on its inner wall)
	BarrelRoll,  // inside a tube: the road rolls Angle around the tube axis - 180 puts you on the ceiling
};

USTRUCT(BlueprintType)
struct FTrackPiece
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite) ETrackPiece Type = ETrackPiece::Straight;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float Length = 100.f;    // m (Straight, Pitch, Gap, Tube, BarrelRoll, Corkscrew)
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float Angle = 90.f;      // deg (Turn: + right; Pitch: + up; roll pieces: + clockwise)
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float Radius = 60.f;     // m (Turn, Loop, Corkscrew axis height, Tube)
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float Bank = 0.f;        // deg, bank reached at the end of the piece
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float Offset = 0.f;      // m (Loop sideways offset, Gap drop)
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float Width = 14.f;      // m road width
	UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bWalls = true;
};

USTRUCT()
struct FTrackSample
{
	GENERATED_BODY()
	FVector P = FVector::ZeroVector;     // road centre (cm, actor space)
	FQuat Q = FQuat::Identity;           // road frame: X along, Y right, Z road normal
	FVector Axis = FVector::ZeroVector;  // tube centre (cm)
	FQuat QPath = FQuat::Identity;       // unrolled path frame (tube orientation)
	float S = 0.f;                       // distance along track (m)
	float Width = 14.f;
	float TubeR = 0.f;                   // m, 0 = open road
	bool bWalls = true;
	bool bGap = false;
	int32 Piece = 0;
};

UCLASS()
class ROADSTERSIM_API ATrackActor : public AActor
{
	GENERATED_BODY()

public:
	ATrackActor();
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Track") FString TrackName = TEXT("Proving Ground");
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Track") TArray<FTrackPiece> Pieces;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Track") bool bCloseLoop = true;

	/** Regenerate samples, meshes, supports and lights from Pieces. */
	void Rebuild();
	void LoadPreset(int32 Index);
	static TArray<FString> PresetNames();
	bool SaveToFile(const FString& Name) const;
	bool LoadFromFile(const FString& Name);
	static TArray<FString> SavedTracks();

	bool GetStartFrame(FTransform& Out) const;
	bool FindNearestFrame(const FVector& WorldPos, FTransform& Out) const;
	bool GetFrameAtPiece(int32 PieceIndex, FTransform& Out) const;
	bool CrossedStartLine(const FVector& WorldPos);
	const TArray<FVector>& GetCenterline() const { return Centerline; }
	const TArray<FTrackSample>& GetSamples() const { return Samples; }
	float GetLengthM() const { return Samples.Num() ? Samples.Last().S : 0.f; }
	int32 SelectedPiece = -1;

protected:
	UPROPERTY(VisibleAnywhere) TObjectPtr<UProceduralMeshComponent> Mesh;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UInstancedStaticMeshComponent> Supports;
	UPROPERTY() TArray<TObjectPtr<UPointLightComponent>> Lights;
	UPROPERTY() TArray<TObjectPtr<UMaterialInterface>> Mats;

private:
	TArray<FTrackSample> Samples;
	TArray<FVector> Centerline;   // world space, decimated, for maps
	float PrevStartDist = 0.f;
	bool bHavePrev = false;

	void BuildSamples();
	void BuildMesh();
	void BuildSupportsAndLights();
};
