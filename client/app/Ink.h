/*
 * Copyright 2026, Ian Dumych, idumych@gmail.com.
 * SPDX-License-Identifier: MIT
 */
#ifndef AWASE_INK_H
#define AWASE_INK_H

// The Awase look: night ink, washi paper and a vermilion seal, as on the
// website. Every view is drawn by hand, off screen, so the look is the same
// on Renku and on Linux.

#include <Font.h>
#include <GraphicsDefs.h>
#include <View.h>

#include <string>


class BBitmap;


namespace ink {


extern const rgb_color kNight;
extern const rgb_color kNight2;
extern const rgb_color kIndigo;
extern const rgb_color kIndigo2;
extern const rgb_color kWashi;
extern const rgb_color kWashi2;
extern const rgb_color kMuted;
extern const rgb_color kFire;
extern const rgb_color kFire2;
extern const rgb_color kSeal;
extern const rgb_color kGold;

rgb_color		Alpha(rgb_color color, uint8 alpha);
rgb_color		Mix(rgb_color a, rgb_color b, float t);

void			InitFonts();
BFont			Display(float size);	// the serif, for names and figures
BFont			Body(float size, bool bold = false);
BFont			Mono(float size);
BFont			Cjk(float size);		// has 合

// A small uppercase label with its letters spaced out, as on the website.
void			DrawLabel(BView* view, BPoint where, const char* text,
					rgb_color color);
float			LabelWidth(BView* view, const char* text);

// The seal: a vermilion square with 合 on it. pop > 1 draws it larger, for
// the moment it lands.
void			DrawSeal(BView* view, BPoint center, float half,
					float pop = 1.0f);

// Light and motion, as in the eye-candy mockup. There's no blur, so a glow
// is stacked translucent circles, and a gradient stroke is short segments.
float			EaseOut(float t);	// 0..1, fast then slow
BPoint			BezierPoint(const BPoint curve[4], float t);
void			DrawGlow(BView* view, BPoint center, float radius,
					rgb_color color, float strength = 1.0f);
void			StrokeGradientBezier(BView* view, const BPoint curve[4],
					rgb_color from, rgb_color to, float width, uint8 alpha,
					float t0 = 0, float t1 = 1);
// An arc from startDegrees (0 is east, counterclockwise) through
// spanDegrees (negative is clockwise), shading from one colour to the other.
void			StrokeGradientArc(BView* view, BPoint center, float radius,
					float startDegrees, float spanDegrees, rgb_color from,
					rgb_color to, float width);
// Three rings spreading from a point, age seconds after something landed.
void			DrawRipples(BView* view, BPoint center, float age,
					float inner, rgb_color color, float strength = 1.0f);
// Sparks thrown out from a point, age seconds after something landed.
void			DrawSparks(BView* view, BPoint center, float age, float inner,
					uint32 seed, rgb_color color);

std::string		FormatDuration(double seconds);
std::string		FakeHash(const std::string& seed);	// "c01f 9a3e … 4e10"


// A view drawn into an off-screen bitmap and copied to the screen in one go.
class InkView : public BView {
public:
								InkView(const char* name);
	virtual						~InkView();

	virtual	void				AttachedToWindow();
	virtual	void				Draw(BRect updateRect);
	virtual	void				FrameResized(float width, float height);

protected:
	virtual	void				Paint(BView* canvas, BRect bounds) = 0;

private:
			void				_Allocate();

			BBitmap*			fBitmap;
			BView*				fCanvas;
};


}	// namespace ink


#endif	// AWASE_INK_H
