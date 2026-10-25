#include "RoadsterUI.h"
#include "Engine/Canvas.h"
#include "Engine/Font.h"
#include "Engine/Engine.h"
#include "CanvasItem.h"
#include "Engine/Texture.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Rendering/SlateRenderer.h"

namespace RoadsterUI
{
	static UFont* LoadFont(const TCHAR* Path)
	{
		return LoadObject<UFont>(nullptr, Path);
	}

	void FContext::Begin(UCanvas* InCanvas, FVector2D Origin, FVector2D Size, FVector2D MouseCanvas, bool bMouseDown, bool bMouseClicked, bool bInteractive)
	{
		Canvas = InCanvas;
		Org = Origin;
		Scale = FVector2D(Size.X / VW, Size.Y / VH);
		Mouse = (MouseCanvas - Origin) / Scale;
		bDown = bMouseDown; bClick = bMouseClicked; bInteract = bInteractive;
		bConsumedClick = false;
		if (!Font) Font = LoadFont(TEXT("/Engine/EngineFonts/Roboto.Roboto"));
		if (!FontBold) FontBold = Font;
	}

	void FContext::End() { Canvas = nullptr; }

	void FContext::Rect(float X, float Y, float W, float H, const FLinearColor& C)
	{
		if (!Canvas) return;
		FCanvasTileItem Item(P(X, Y), FVector2D(W * Scale.X, H * Scale.Y), C);
		Item.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Item);
	}

	void FContext::Outline(float X, float Y, float W, float H, const FLinearColor& C, float T)
	{
		Rect(X, Y, W, T, C); Rect(X, Y + H - T, W, T, C);
		Rect(X, Y, T, H, C); Rect(X + W - T, Y, T, H, C);
	}

	void FContext::Line(float X0, float Y0, float X1, float Y1, const FLinearColor& C, float T)
	{
		if (!Canvas) return;
		FCanvasLineItem L(P(X0, Y0), P(X1, Y1));
		L.SetColor(C);
		L.LineThickness = FMath::Max(1.f, T * Scale.X);
		L.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(L);
	}

	void FContext::Circle(float CX, float CY, float R, const FLinearColor& C, float T, int32 Segs)
	{
		for (int32 i = 0; i < Segs; ++i)
		{
			const float a0 = 2 * PI * i / Segs, a1 = 2 * PI * (i + 1) / Segs;
			Line(CX + R * FMath::Cos(a0), CY + R * FMath::Sin(a0), CX + R * FMath::Cos(a1), CY + R * FMath::Sin(a1), C, T);
		}
	}

	void FContext::Disc(float CX, float CY, float R, const FLinearColor& C)
	{
		if (!Canvas) return;
		const int32 Segs = 28;
		TArray<FCanvasUVTri> Tris;
		const FVector2D Cp = P(CX, CY);
		for (int32 i = 0; i < Segs; ++i)
		{
			const float a0 = 2 * PI * i / Segs, a1 = 2 * PI * (i + 1) / Segs;
			FCanvasUVTri T;
			T.V0_Pos = Cp; T.V1_Pos = P(CX + R * FMath::Cos(a0), CY + R * FMath::Sin(a0)); T.V2_Pos = P(CX + R * FMath::Cos(a1), CY + R * FMath::Sin(a1));
			T.V0_Color = T.V1_Color = T.V2_Color = C;
			Tris.Add(T);
		}
		FCanvasTriangleItem Item(Tris, GWhiteTexture);
		Item.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Item);
	}

	void FContext::Diamond(float CX, float CY, float R, const FLinearColor& C)
	{
		if (!Canvas) return;
		TArray<FCanvasUVTri> Tris;
		auto Tri = [&](FVector2D A, FVector2D B, FVector2D D)
		{
			FCanvasUVTri T; T.V0_Pos = P(A.X, A.Y); T.V1_Pos = P(B.X, B.Y); T.V2_Pos = P(D.X, D.Y);
			T.V0_Color = T.V1_Color = T.V2_Color = C; Tris.Add(T);
		};
		Tri({ CX, CY - R }, { CX + R, CY }, { CX, CY + R });
		Tri({ CX, CY - R }, { CX, CY + R }, { CX - R, CY });
		FCanvasTriangleItem Item(Tris, GWhiteTexture);
		Item.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Item);
	}

	void FContext::Texture(UTexture* Tex, float X, float Y, float W, float H, const FLinearColor& Tint)
	{
		if (!Canvas || !Tex || !Tex->GetResource()) return;
		FCanvasTileItem Item(P(X, Y), Tex->GetResource(), FVector2D(W * Scale.X, H * Scale.Y), Tint);
		Item.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Item);
	}

	void FContext::Text(const FString& S, float X, float Y, float Size, const FLinearColor& C, EAlign Align, bool bBold)
	{
		if (!Canvas || !Font) return;
		const float Px = Size * Scale.Y;
		FSlateFontInfo Info(Font, FMath::Max(1, FMath::RoundToInt(Px * 0.75f)), bBold ? FName("Bold") : FName("Regular"));
		FCanvasTextItem Item(P(X, Y), FText::FromString(S), Info, C);
		Item.BlendMode = SE_BLEND_Translucent;
		if (Align != EAlign::Left)
		{
			const float W = TextWidth(S, Size, bBold) * Scale.X;
			Item.Position.X -= Align == EAlign::Center ? W * 0.5f : W;
		}
		Item.Position.Y -= Px * 0.62f;   // Y is the vertical centre of the line
		Canvas->DrawItem(Item);
	}

	float FContext::TextWidth(const FString& S, float Size, bool bBold)
	{
		if (!Canvas || !Font) return S.Len() * Size * 0.5f;
		const float Px = Size * Scale.Y;
		FSlateFontInfo Info(Font, FMath::Max(1, FMath::RoundToInt(Px * 0.75f)), bBold ? FName("Bold") : FName("Regular"));
		float W = S.Len() * Px * 0.52f;
		if (FSlateApplication::IsInitialized())
		{
			const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
			W = Measure->Measure(S, Info).X;
		}
		return W / Scale.X;
	}

	// ---------------------------------------------------------------- squared numerals
	// Segment layout:  a (top), b (top-right), c (bottom-right), d (bottom), e (bottom-left), f (top-left), g (middle)
	static uint8 SegMask(TCHAR Ch)
	{
		switch (Ch)
		{
		case '0': return 0b0111111; case '1': return 0b0000110; case '2': return 0b1011011; case '3': return 0b1001111;
		case '4': return 0b1100110; case '5': return 0b1101101; case '6': return 0b1111101; case '7': return 0b0000111;
		case '8': return 0b1111111; case '9': return 0b1101111; case '-': return 0b1000000;
		default: return 0;
		}
	}

	void FContext::Seg(float X, float Y, float W, float H, const FLinearColor& C) { Rect(X, Y, W, H, C); }

	float FContext::DigitsWidth(const FString& S, float H) const
	{
		float W = 0;
		for (TCHAR Ch : S)
		{
			if (Ch == '.' || Ch == ':') W += H * 0.28f;
			else if (Ch == ' ') W += H * 0.35f;
			else if (Ch == '1') W += H * 0.42f;
			else W += H * 0.66f;
		}
		return W;
	}

	void FContext::Digits(const FString& S, float X, float Y, float H, const FLinearColor& C, EAlign Align, float Weight)
	{
		const float Total = DigitsWidth(S, H);
		float Cx = Align == EAlign::Left ? X : (Align == EAlign::Center ? X - Total * 0.5f : X - Total);
		const float T = H * Weight;              // stroke
		const float Top = Y - H * 0.5f;
		for (TCHAR Ch : S)
		{
			if (Ch == ' ') { Cx += H * 0.35f; continue; }
			if (Ch == '.') { Rect(Cx + H * 0.06f, Top + H - T, T, T, C); Cx += H * 0.28f; continue; }
			if (Ch == ':') { Rect(Cx + H * 0.06f, Top + H * 0.28f, T, T, C); Rect(Cx + H * 0.06f, Top + H * 0.66f, T, T, C); Cx += H * 0.28f; continue; }
			if (Ch == '+')
			{
				const float W = H * 0.5f;
				Rect(Cx, Top + H * 0.5f - T * 0.5f, W, T, C);
				Rect(Cx + W * 0.5f - T * 0.5f, Top + H * 0.25f, T, H * 0.5f, C);
				Cx += H * 0.66f; continue;
			}
			if (Ch == '%')
			{
				const float W = H * 0.5f;
				Rect(Cx, Top, T, T, C); Rect(Cx + W - T, Top + H - T, T, T, C);
				for (int32 i = 0; i < 8; ++i) Rect(Cx + W * (1 - i / 8.f) - T * 0.5f, Top + H * i / 8.f, T, H / 8.f + 1, C);
				Cx += H * 0.66f; continue;
			}
			const bool bOne = Ch == '1';
			const float W = bOne ? H * 0.28f : H * 0.52f;
			const uint8 M = SegMask(Ch);
			const float Mid = Top + H * 0.5f - T * 0.5f;
			if (bOne)
			{
				// flat-footed "1" of the Roadster UI: stem + small flag + foot
				Rect(Cx + W - T, Top, T, H, C);
				Rect(Cx, Top, W, T, C);
				Rect(Cx, Top + H - T, W + T * 0.6f, T, C);
				Cx += H * 0.42f; continue;
			}
			if (M & 0b0000001) Seg(Cx, Top, W, T, C);                       // a
			if (M & 0b0000010) Seg(Cx + W - T, Top, T, H * 0.5f + T * 0.5f, C);   // b
			if (M & 0b0000100) Seg(Cx + W - T, Mid, T, H * 0.5f + T * 0.5f, C);   // c
			if (M & 0b0001000) Seg(Cx, Top + H - T, W, T, C);               // d
			if (M & 0b0010000) Seg(Cx, Mid, T, H * 0.5f + T * 0.5f, C);     // e
			if (M & 0b0100000) Seg(Cx, Top, T, H * 0.5f + T * 0.5f, C);     // f
			if (M & 0b1000000) Seg(Cx, Mid, W, T, C);                       // g
			Cx += H * 0.66f;
		}
	}

	void FContext::Glow(float X, float Y, float W, float H, const FLinearColor& C, float Strength)
	{
		for (int32 i = 1; i <= 4; ++i)
		{
			FLinearColor G = C; G.A = 0.10f * Strength / i;
			Outline(X - i * 2.f, Y - i * 2.f, W + i * 4.f, H + i * 4.f, G, 2.f);
		}
	}

	void FContext::DotFloor(float X, float Y, float W, float H, float Phase)
	{
		// perspective grid of dots receding to a horizon at the top of the region
		const int32 Rows = 18, Cols = 40;
		for (int32 r = 0; r < Rows; ++r)
		{
			const float t = (r + FMath::Frac(Phase)) / Rows;          // 0 at horizon
			const float depth = t * t;
			const float y = Y + H * (0.05f + 0.95f * depth);
			const float spread = 0.15f + 0.85f * depth;
			const float a = 0.10f + 0.45f * depth;
			for (int32 c = 0; c <= Cols; ++c)
			{
				const float u = (c / float(Cols) - 0.5f) * spread * 1.6f;
				const float x = X + W * (0.5f + u);
				if (x < X || x > X + W) continue;
				const float s = 1.2f + 2.2f * depth;
				FLinearColor Col = RedDim; Col.A = a;
				Rect(x - s * 0.5f, y - s * 0.5f, s, s, Col);
			}
		}
	}

	void FContext::Gradient(float X, float Y, float W, float H, const FLinearColor& Top, const FLinearColor& Bottom)
	{
		if (!Canvas) return;
		TArray<FCanvasUVTri> Tris;
		FCanvasUVTri A, B;
		A.V0_Pos = P(X, Y); A.V1_Pos = P(X + W, Y); A.V2_Pos = P(X, Y + H);
		A.V0_Color = Top; A.V1_Color = Top; A.V2_Color = Bottom;
		B.V0_Pos = P(X + W, Y); B.V1_Pos = P(X + W, Y + H); B.V2_Pos = P(X, Y + H);
		B.V0_Color = Top; B.V1_Color = Bottom; B.V2_Color = Bottom;
		Tris.Add(A); Tris.Add(B);
		FCanvasTriangleItem Item(Tris, GWhiteTexture);
		Item.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Item);
	}

	void FContext::Icon(EIcon Kind, float CX, float CY, float S, const FLinearColor& C)
	{
		const float h = S * 0.5f, T = FMath::Max(2.f, S * 0.09f);
		switch (Kind)
		{
		case EIcon::Prev:
		case EIcon::Next:
		{
			const float d = Kind == EIcon::Next ? 1.f : -1.f;
			Line(CX - d * h * 0.6f, CY - h * 0.6f, CX + d * h * 0.3f, CY, C, T);
			Line(CX + d * h * 0.3f, CY, CX - d * h * 0.6f, CY + h * 0.6f, C, T);
			Rect(CX + d * h * 0.45f - T * 0.5f, CY - h * 0.6f, T, h * 1.2f, C);
			break;
		}
		case EIcon::Shuffle:
			Line(CX - h, CY - h * 0.5f, CX + h, CY + h * 0.5f, C, T);
			Line(CX - h, CY + h * 0.5f, CX + h, CY - h * 0.5f, C, T);
			break;
		case EIcon::Repeat:
			Circle(CX, CY, h * 0.7f, C, T, 20);
			Line(CX + h * 0.7f, CY, CX + h * 0.9f, CY - h * 0.3f, C, T);
			break;
		case EIcon::ChevronL:
		case EIcon::ChevronR:
		{
			const float d = Kind == EIcon::ChevronR ? 1.f : -1.f;
			Line(CX - d * h * 0.3f, CY - h * 0.6f, CX + d * h * 0.3f, CY, C, T);
			Line(CX + d * h * 0.3f, CY, CX - d * h * 0.3f, CY + h * 0.6f, C, T);
			break;
		}
		case EIcon::Speaker:
			Rect(CX - h * 0.8f, CY - h * 0.25f, h * 0.4f, h * 0.5f, C);
			Line(CX - h * 0.4f, CY - h * 0.25f, CX, CY - h * 0.6f, C, T);
			Line(CX - h * 0.4f, CY + h * 0.25f, CX, CY + h * 0.6f, C, T);
			Line(CX, CY - h * 0.6f, CX, CY + h * 0.6f, C, T);
			break;
		case EIcon::Exit:
			Outline(CX - h * 0.6f, CY - h * 0.6f, h * 0.8f, h * 1.2f, C, T);
			Line(CX - h * 0.1f, CY, CX + h * 0.8f, CY, C, T);
			Line(CX + h * 0.5f, CY - h * 0.3f, CX + h * 0.8f, CY, C, T);
			Line(CX + h * 0.5f, CY + h * 0.3f, CX + h * 0.8f, CY, C, T);
			break;
		case EIcon::Minus:
			Rect(CX - h * 0.6f, CY - T * 0.5f, h * 1.2f, T, C);
			break;
		case EIcon::Flag:
			for (int32 r = 0; r < 3; ++r) for (int32 c = 0; c < 4; ++c)
				Rect(CX - h + c * h * 0.5f, CY - h * 0.75f + r * h * 0.5f, h * 0.5f, h * 0.5f, ((r + c) % 2) ? C : FLinearColor(C.R, C.G, C.B, C.A * 0.25f));
			break;
		}
	}

	bool FContext::Hover(float X, float Y, float W, float H) const
	{
		return bInteract && Mouse.X >= X && Mouse.X <= X + W && Mouse.Y >= Y && Mouse.Y <= Y + H;
	}

	bool FContext::Clicked(float X, float Y, float W, float H) const
	{
		return bClick && !bConsumedClick && Hover(X, Y, W, H);
	}

	void FContext::Panel(float X, float Y, float W, float H, bool bActive)
	{
		Rect(X, Y, W, H, bActive ? FLinearColor::FromSRGBColor(FColor(70, 6, 6)) : Tile);
		Outline(X, Y, W, H, bActive ? Red : TileBorder, bActive ? 2.f : 1.f);
		if (bActive) Glow(X, Y, W, H, Red, 1.2f);
	}

	bool FContext::Button(float X, float Y, float W, float H, const FString& Label, bool bActive, float TextSize)
	{
		const bool bHover = Hover(X, Y, W, H);
		if (bActive) { Rect(X, Y, W, H, FLinearColor::FromSRGBColor(FColor(80, 8, 8))); Outline(X, Y, W, H, Red, 2.f); }
		else if (bHover) { Rect(X, Y, W, H, FLinearColor::FromSRGBColor(FColor(45, 5, 5))); Outline(X, Y, W, H, RedDim, 1.f); }
		Text(Label, X + W * 0.5f, Y + H * 0.5f, TextSize, bActive ? Red : (bHover ? Red : RedDim), EAlign::Center);
		const bool bHit = Clicked(X, Y, W, H);
		if (bHit) bConsumedClick = true;
		return bHit;
	}

	int32 FContext::Segmented(float X, float Y, float W, float H, const FString& Label, const TArray<FString>& Options, int32 Current, float LabelW)
	{
		Panel(X, Y, W, H);
		Text(Label, X + 22, Y + H * 0.5f, 25, Red);
		const float AreaX = X + LabelW, AreaW = W - LabelW - 14;
		const float BW = AreaW / FMath::Max(1, Options.Num());
		int32 Out = Current;
		for (int32 i = 0; i < Options.Num(); ++i)
		{
			if (Button(AreaX + i * BW + 4, Y + 10, BW - 8, H - 20, Options[i], i == Current, 23)) Out = i;
		}
		return Out;
	}

	bool FContext::Slider(float X, float Y, float W, float H, const FString& Label, float& Value, float Min, float Max, const FString& ValueText, float LabelW)
	{
		Panel(X, Y, W, H);
		Text(Label, X + 22, Y + H * 0.5f, 25, Red);
		const float VX = X + W - 150;
		Text(ValueText, X + W - 22, Y + H * 0.5f, 26, Red, EAlign::Right, true);
		const float TX = X + LabelW, TW = VX - TX - 20;
		const float Ty = Y + H * 0.5f;
		Rect(TX, Ty - 3, TW, 6, RedFaint);
		const float F = FMath::Clamp((Value - Min) / (Max - Min), 0.f, 1.f);
		Rect(TX, Ty - 3, TW * F, 6, Red);
		Rect(TX + TW * F - 9, Ty - 18, 18, 36, Red);
		const bool bActive = bDown && Hover(TX - 12, Y, TW + 24, H);
		if (bActive)
		{
			Value = FMath::Lerp(Min, Max, FMath::Clamp((Mouse.X - TX) / TW, 0.f, 1.f));
			bConsumedClick = true;
		}
		return bActive;
	}

	void FContext::Bar(float X, float Y, float W, float H, float Frac, const FLinearColor& C, bool bTicks)
	{
		Rect(X, Y, W, H, RedFaint);
		const float F = FMath::Clamp(Frac, 0.f, 1.f);
		// red gradient fill as in the Track Mode bars
		const int32 Steps = 12;
		for (int32 i = 0; i < Steps; ++i)
		{
			const float a = (i + 1) / float(Steps);
			if (a - 1.f / Steps > F) break;
			const float w = FMath::Min(W * F - W * i / Steps, W / Steps);
			FLinearColor G = C; G.A = 0.35f + 0.65f * a;
			Rect(X + W * i / Steps, Y, w, H, G);
		}
		if (bTicks) for (int32 i = 1; i < 4; ++i) Rect(X + W * i / 4.f, Y + H + 3, 1.5f, 8, RedDim);
	}

	bool FContext::Card(float X, float Y, float W, float H, const FString& Name, bool bActive)
	{
		Panel(X, Y, W, H, bActive);
		Text(bActive ? TEXT("ACTIVE") : TEXT("SELECT"), X + 18, Y + 26, 16, bActive ? Red : RedDim, EAlign::Left, true);
		Text(Name, X + 18, Y + H - 34, 34, bActive ? Red : RedDim);
		const bool bHit = Clicked(X, Y, W, H);
		if (bHit) bConsumedClick = true;
		return bHit;
	}
}
