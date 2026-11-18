/*
 * Copyright 2026, Ian Dumych, idumych@gmail.com.
 * SPDX-License-Identifier: MIT
 */


#include "Ink.h"

#include <Bitmap.h>

#include <stdio.h>
#include <string.h>


namespace ink {


const rgb_color kNight = { 12, 13, 18, 255 };
const rgb_color kNight2 = { 18, 20, 28, 255 };
const rgb_color kIndigo = { 25, 29, 46, 255 };
const rgb_color kIndigo2 = { 35, 40, 66, 255 };
const rgb_color kWashi = { 239, 233, 221, 255 };
const rgb_color kWashi2 = { 201, 194, 180, 255 };
const rgb_color kMuted = { 141, 138, 153, 255 };
const rgb_color kFire = { 255, 106, 19, 255 };
const rgb_color kFire2 = { 255, 138, 61, 255 };
const rgb_color kSeal = { 216, 67, 42, 255 };
const rgb_color kGold = { 242, 193, 78, 255 };


static BFont sDisplay;
static BFont sCjk;
static bool sFontsReady = false;


static bool
TryFont(BFont& font, const char* family, const char* style)
{
	int32 families = count_font_families();
	for (int32 i = 0; i < families; i++) {
		font_family name;
		uint32 flags;
		if (get_font_family(i, &name, &flags) != B_OK
			|| strcmp(name, family) != 0)
			continue;
		int32 styles = count_font_styles(name);
		for (int32 s = 0; s < styles; s++) {
			font_style styleName;
			if (get_font_style(name, s, &styleName, &flags) == B_OK
				&& strcmp(styleName, style) == 0) {
				font.SetFamilyAndStyle(family, style);
				return true;
			}
		}
	}
	return false;
}


void
InitFonts()
{
	if (sFontsReady)
		return;
	sFontsReady = true;

	sDisplay = *be_bold_font;
	if (!TryFont(sDisplay, "Noto Serif", "Bold")
		&& !TryFont(sDisplay, "Noto Serif Display", "Bold")
		&& !TryFont(sDisplay, "DejaVu Serif", "Bold"))
		sDisplay = *be_bold_font;

	sCjk = *be_bold_font;
	if (!TryFont(sCjk, "Noto Sans CJK JP", "Bold")
		&& !TryFont(sCjk, "Noto Sans CJK JP", "Regular")
		&& !TryFont(sCjk, "Noto Sans CJK SC", "Bold"))
		sCjk = *be_bold_font;
}


BFont
Display(float size)
{
	InitFonts();
	BFont font(sDisplay);
	font.SetSize(size);
	return font;
}


BFont
Body(float size, bool bold)
{
	BFont font(bold ? be_bold_font : be_plain_font);
	font.SetSize(size);
	return font;
}


BFont
Mono(float size)
{
	BFont font(be_fixed_font);
	font.SetSize(size);
	return font;
}


BFont
Cjk(float size)
{
	InitFonts();
	BFont font(sCjk);
	font.SetSize(size);
	return font;
}


rgb_color
Alpha(rgb_color color, uint8 alpha)
{
	color.alpha = alpha;
	return color;
}


rgb_color
Mix(rgb_color a, rgb_color b, float t)
{
	if (t < 0)
		t = 0;
	if (t > 1)
		t = 1;
	rgb_color c;
	c.red = (uint8)(a.red + (b.red - a.red) * t);
	c.green = (uint8)(a.green + (b.green - a.green) * t);
	c.blue = (uint8)(a.blue + (b.blue - a.blue) * t);
	c.alpha = (uint8)(a.alpha + (b.alpha - a.alpha) * t);
	return c;
}


static const float kLabelSpacing = 1.6f;


// The length in bytes of the UTF-8 character at text.
static int32
UTF8Length(const char* text)
{
	uint8 c = (uint8)*text;
	int32 length = c < 0x80 ? 1 : c < 0xe0 ? 2 : c < 0xf0 ? 3 : 4;
	for (int32 i = 1; i < length; i++) {
		if (text[i] == '\0')
			return i;
	}
	return length;
}


float
LabelWidth(BView* view, const char* text)
{
	BFont font = Mono(10);
	float width = 0;
	for (const char* c = text; *c != '\0';) {
		int32 length = UTF8Length(c);
		width += font.StringWidth(c, length) + kLabelSpacing;
		c += length;
	}
	(void)view;
	return width;
}


void
DrawLabel(BView* view, BPoint where, const char* text, rgb_color color)
{
	BFont font = Mono(10);
	view->SetFont(&font);
	view->SetHighColor(color);
	for (const char* c = text; *c != '\0';) {
		int32 length = UTF8Length(c);
		view->DrawString(c, length, where);
		where.x += font.StringWidth(c, length) + kLabelSpacing;
		c += length;
	}
}


void
DrawSeal(BView* view, BPoint center, float half, float pop)
{
	half *= pop;
	BRect square(center.x - half, center.y - half, center.x + half,
		center.y + half);
	drawing_mode mode = view->DrawingMode();
	view->SetDrawingMode(B_OP_ALPHA);
	if (pop > 1.0f) {
		view->SetHighColor(Alpha(kSeal, (uint8)(90 * (pop - 1.0f) * 2)));
		view->FillRoundRect(square.InsetByCopy(-half * 0.5f, -half * 0.5f),
			half * 0.5f, half * 0.5f);
	}
	view->SetHighColor(kSeal);
	view->FillRoundRect(square, half * 0.36f, half * 0.36f);

	BFont font = Cjk(half * 1.25f);
	view->SetFont(&font);
	font_height height;
	font.GetHeight(&height);
	const char* glyph = "合";
	float width = font.StringWidth(glyph);
	view->SetHighColor(255, 244, 234);
	view->DrawString(glyph, BPoint(center.x - width / 2,
		center.y + (height.ascent - height.descent) / 2));
	view->SetDrawingMode(mode);
}


std::string
FormatDuration(double seconds)
{
	char buffer[32];
	int minutes = (int)(seconds / 60 + 0.5);
	if (minutes < 1)
		snprintf(buffer, sizeof(buffer), "under a minute");
	else if (minutes < 60)
		snprintf(buffer, sizeof(buffer), "%d min", minutes);
	else
		snprintf(buffer, sizeof(buffer), "%d h %02d min", minutes / 60,
			minutes % 60);
	return buffer;
}


std::string
FakeHash(const std::string& seed)
{
	// Only for showing: the demo pool's results aren't real files.
	uint32 h = 2166136261u;
	for (size_t i = 0; i < seed.size(); i++)
		h = (h ^ (uint8)seed[i]) * 16777619u;
	uint32 g = h * 2654435761u;
	char buffer[40];
	snprintf(buffer, sizeof(buffer), "%04x %04x \xe2\x80\xa6 %04x",
		(unsigned)(h >> 16), (unsigned)(g & 0xffff), (unsigned)(g >> 16));
	return buffer;
}


// #pragma mark - InkView


InkView::InkView(const char* name)
	:
	BView(name, B_WILL_DRAW | B_FRAME_EVENTS | B_FULL_UPDATE_ON_RESIZE),
	fBitmap(NULL),
	fCanvas(NULL)
{
	SetViewColor(B_TRANSPARENT_COLOR);
}


InkView::~InkView()
{
	delete fBitmap;
}


void
InkView::AttachedToWindow()
{
	BView::AttachedToWindow();
	_Allocate();
}


void
InkView::FrameResized(float width, float height)
{
	BView::FrameResized(width, height);
	_Allocate();
	Invalidate();
}


void
InkView::Draw(BRect updateRect)
{
	if (fBitmap == NULL || fBitmap->Bounds() != Bounds().OffsetToCopy(0, 0))
		_Allocate();
	if (fBitmap == NULL || !fBitmap->Lock())
		return;
	BRect bounds = fBitmap->Bounds();
	fCanvas->SetDrawingMode(B_OP_COPY);
	fCanvas->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
	fCanvas->SetHighColor(kNight);
	fCanvas->FillRect(bounds);
	Paint(fCanvas, bounds);
	fCanvas->Sync();
	fBitmap->Unlock();
	DrawBitmap(fBitmap, Bounds().LeftTop());
	(void)updateRect;
}


void
InkView::_Allocate()
{
	BRect bounds = Bounds().OffsetToCopy(0, 0);
	if (bounds.Width() < 1 || bounds.Height() < 1)
		return;
	if (fBitmap != NULL && fBitmap->Bounds() == bounds)
		return;
	delete fBitmap;
	fBitmap = new BBitmap(bounds, B_BITMAP_ACCEPTS_VIEWS, B_RGB32);
	fCanvas = new BView(bounds, "canvas", B_FOLLOW_NONE, B_WILL_DRAW);
	fBitmap->Lock();
	fBitmap->AddChild(fCanvas);
	fBitmap->Unlock();
}


}	// namespace ink
