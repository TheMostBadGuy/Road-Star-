#pragma once

#include "CoreMinimal.h"

class UCanvas;
class UFont;
class UTexture;

/**
 * Immediate-mode UI drawn on a UCanvas, styled after the 2026 Roadster's red-on-black "pilot" interface.
 * All coordinates are in a virtual 1920 x 1200 (16:10) space that Begin() maps onto any canvas region,
 * so the same code paints the in-car screen render target and the full-screen touchscreen overlay.
 */
namespace RoadsterUI
{
	// Palette measured from Tesla's UI video frames (Research/roadster_specs.md section 6.1)
	inline const FLinearColor Black(0.f, 0.f, 0.f, 1.f);
	inline const FLinearColor Red = FLinearColor::FromSRGBColor(FColor(255, 38, 38));
	inline const FLinearColor RedDim = FLinearColor::FromSRGBColor(FColor(130, 16, 16));
	inline const FLinearColor RedFaint = FLinearColor::FromSRGBColor(FColor(70, 8, 8));
	inline const FLinearColor Tile = FLinearColor::FromSRGBColor(FColor(30, 3, 3));
	inline const FLinearColor TileBorder = FLinearColor::FromSRGBColor(FColor(150, 10, 10));
	inline const FLinearColor HeaderFill = FLinearColor::FromSRGBColor(FColor(235, 30, 30));
	inline const FLinearColor White = FLinearColor::FromSRGBColor(FColor(235, 235, 240));
	inline const FLinearColor Grey = FLinearColor::FromSRGBColor(FColor(120, 120, 125));
	inline const FLinearColor Green = FLinearColor::FromSRGBColor(FColor(60, 230, 90));
	inline const FLinearColor Amber = FLinearColor::FromSRGBColor(FColor(255, 160, 30));

	enum class EAlign : uint8 { Left, Center, Right };

	class FContext
	{
	public:
		/** Map the virtual 1920x1200 space into Canvas rect (Origin, Size). Mouse is in canvas pixels. */
		void Begin(UCanvas* InCanvas, FVector2D Origin, FVector2D Size, FVector2D MouseCanvas, bool bMouseDown, bool bMouseClicked, bool bInteractive);
		void End();

		static constexpr float VW = 1920.f, VH = 1200.f;

		// ---- primitives (virtual coordinates)
		void Rect(float X, float Y, float W, float H, const FLinearColor& C);
		void Outline(float X, float Y, float W, float H, const FLinearColor& C, float T = 1.5f);
		void Line(float X0, float Y0, float X1, float Y1, const FLinearColor& C, float T = 1.5f);
		void Circle(float CX, float CY, float R, const FLinearColor& C, float T = 1.5f, int32 Segs = 48);
		void Disc(float CX, float CY, float R, const FLinearColor& C);
		void Diamond(float CX, float CY, float R, const FLinearColor& C);
		void Texture(UTexture* Tex, float X, float Y, float W, float H, const FLinearColor& Tint = FLinearColor::White);
		/** Text in Roboto; Size is cap-height-ish in virtual px. */
		void Text(const FString& S, float X, float Y, float Size, const FLinearColor& C, EAlign Align = EAlign::Left, bool bBold = false);
		float TextWidth(const FString& S, float Size, bool bBold = false);
		/** Squared "HUD" numerals (seven-segment derived) for big values: digits, + - . : % and space. */
		void Digits(const FString& S, float X, float Y, float H, const FLinearColor& C, EAlign Align = EAlign::Left, float Weight = 0.16f);
		float DigitsWidth(const FString& S, float H) const;
		/** Glow: a few expanding translucent outlines. */
		void Glow(float X, float Y, float W, float H, const FLinearColor& C, float Strength = 1.f);
		/** Perspective dot-grid "floor" behind the 3D car. */
		void DotFloor(float X, float Y, float W, float H, float Phase = 0.f);
		/** Vertical gradient quad (one draw, no banding). */
		void Gradient(float X, float Y, float W, float H, const FLinearColor& Top, const FLinearColor& Bottom, bool bHorizontal = false);
		/** Line-drawn icons (Roboto has no media glyphs; missing glyphs hit a slow font fallback). */
		enum class EIcon : uint8 { Prev, Next, Shuffle, Repeat, ChevronL, ChevronR, Speaker, Exit, Minus, Flag };
		void Icon(EIcon Kind, float CX, float CY, float S, const FLinearColor& C);

		// ---- widgets
		bool Hover(float X, float Y, float W, float H) const;
		bool Clicked(float X, float Y, float W, float H) const;
		/** Maroon tile with a thin red border (the dashboard tiles). */
		void Panel(float X, float Y, float W, float H, bool bActive = false);
		bool Button(float X, float Y, float W, float H, const FString& Label, bool bActive, float TextSize = 26.f);
		/** Labelled segmented selector; returns the chosen index (Current if unchanged). */
		int32 Segmented(float X, float Y, float W, float H, const FString& Label, const TArray<FString>& Options, int32 Current, float LabelW = 260.f);
		/** Labelled horizontal slider; returns true while changing. */
		bool Slider(float X, float Y, float W, float H, const FString& Label, float& Value, float Min, float Max, const FString& ValueText, float LabelW = 260.f);
		void Bar(float X, float Y, float W, float H, float Frac, const FLinearColor& C, bool bTicks = true);
		/** Mode card (SELECT / ACTIVE micro-label + big name). */
		bool Card(float X, float Y, float W, float H, const FString& Name, bool bActive);

		bool bConsumedClick = false;
		FVector2D Mouse;          // virtual
		bool bDown = false, bClick = false, bInteract = false;

	private:
		UCanvas* Canvas = nullptr;
		FVector2D Org, Scale;
		UFont* Font = nullptr;
		UFont* FontBold = nullptr;
		FVector2D P(float X, float Y) const { return Org + FVector2D(X * Scale.X, Y * Scale.Y); }
		void Seg(float X, float Y, float W, float H, const FLinearColor& C);
	};
}
