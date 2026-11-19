/*
 * Copyright 2026, Ian Dumych, idumych@gmail.com.
 * SPDX-License-Identifier: MIT
 */


#include "Views.h"

#include "DemoPool.h"

#include <GradientLinear.h>
#include <String.h>
#include <Window.h>

#include <algorithm>
#include <ctype.h>
#include <math.h>
#include <stdio.h>


using namespace awase;
using namespace ink;


static const float kTop = 78;
static const float kBottom = 64;
static const float kSide = 34;
static const float kNode = 6.5f;
static const float kSealHalf = 10;


static float
Jitter(const std::string& name, float range)
{
	uint32 h = 2166136261u;
	for (size_t i = 0; i < name.size(); i++)
		h = (h ^ (uint8)name[i]) * 16777619u;
	return ((h & 0xffff) / 65535.0f - 0.5f) * 2 * range;
}


static BPoint
Bezier(BPoint p0, BPoint p1, BPoint p2, BPoint p3, float t)
{
	float u = 1 - t;
	float a = u * u * u, b = 3 * u * u * t, c = 3 * u * t * t, d = t * t * t;
	return BPoint(a * p0.x + b * p1.x + c * p2.x + d * p3.x,
		a * p0.y + b * p1.y + c * p2.y + d * p3.y);
}


// A build in flight on the live wall: a thread from the machine to the
// package's cell, with a light running along it (nobody knows how far a
// build has got), a glowing head and sparks shed behind it.
static void
DrawLiveThread(BView* canvas, BPoint p0, BPoint p3, rgb_color color,
	rgb_color tail, bool active, bigtime_t clock, float phase)
{
	BPoint p1(p0.x, p0.y - (p0.y - p3.y) * 0.6f);
	BPoint p2(p3.x - 30, p3.y + 60);
	BPoint curve[4] = { p0, p1, p2, p3 };
	canvas->SetPenSize(1);
	canvas->SetHighColor(Alpha(color, 50));
	canvas->StrokeBezier(curve);
	float t = active ? fmodf((float)(clock % 1600000) / 1600000.0f + phase, 1)
		: 0.5f;
	StrokeGradientBezier(canvas, curve, tail, color, 7, 40, t - 0.22f, t);
	canvas->SetPenSize(2.4f);
	for (int s = 0; s < 14; s++) {
		float ta = t - 0.2f * (1 - s / 14.0f);
		float tb = t - 0.2f * (1 - (s + 1) / 14.0f);
		if (tb <= 0)
			continue;
		canvas->SetHighColor(Alpha(active ? color : kGold,
			(uint8)(230 * (s + 1) / 14)));
		canvas->StrokeLine(BezierPoint(curve, std::max(0.0f, ta)),
			BezierPoint(curve, tb));
	}
	canvas->SetPenSize(1);
	BPoint head = BezierPoint(curve, t);
	if (active) {
		DrawGlow(canvas, head, 12, color, 1.2f);
		float seconds = (float)(clock % 1000000000) / 1000000.0f;
		for (int k = 1; k <= 5; k++) {
			float tk = t - 0.022f * k;
			if (tk <= 0)
				break;
			BPoint spark = BezierPoint(curve, tk);
			float drift = sinf(seconds * 5 + k * 2.1f + phase * 7) * 2.2f * k / 5;
			spark.x += drift;
			spark.y -= drift * 0.6f;
			canvas->SetHighColor(Alpha(Mix(color, kWashi, 0.35f),
				(uint8)(190 * (1 - k / 6.0f))));
			float r = 1.6f * (1 - k * 0.12f);
			canvas->FillEllipse(spark, r, r);
		}
		canvas->SetHighColor(Mix(color, kWashi, 0.75f));
		canvas->FillEllipse(head, 3.2f, 3.2f);
	}
}


PoolView::PoolView(const Model& model)
	:
	InkView("pool"),
	fModel(model),
	fPool(*model.demo),
	fMouseInside(false),
	fShownSealed(-1)
{
	SetExplicitMinSize(BSize(420, 360));
}


void
PoolView::MouseMoved(BPoint where, uint32 transit, const BMessage* drag)
{
	fMouse = where;
	fMouseInside = transit != B_EXITED_VIEW && transit != B_OUTSIDE_VIEW;
	InkView::MouseMoved(where, transit, drag);
}


void
PoolView::_Layout(BRect bounds)
{
	const std::vector<Package>& packages = fPool.GetPool().Packages();
	int layers = 1;
	for (size_t i = 0; i < packages.size(); i++) {
		if (packages[i].layer + 1 > layers)
			layers = packages[i].layer + 1;
	}
	std::vector<int> counts(layers, 0), seen(layers, 0);
	for (size_t i = 0; i < packages.size(); i++)
		counts[packages[i].layer]++;

	float left = bounds.left + kSide + 30, right = bounds.right - kSide - 30;
	float top = bounds.top + kTop - 4, bottom = bounds.bottom - kBottom - 14;
	// The nodes are nudged off their grid a little, less when a crowded
	// layer leaves no room between a seal and the name above it.
	int crowded = *std::max_element(counts.begin(), counts.end());
	float jitter = std::max(0.0f,
		std::min(10.0f, ((bottom - top) / crowded - 34) / 2));
	fNodes.resize(packages.size());
	for (size_t i = 0; i < packages.size(); i++) {
		int layer = packages[i].layer;
		float x = layers > 1
			? left + (right - left) * layer / (layers - 1) : (left + right) / 2;
		float y = top + (bottom - top) * (seen[layer] + 0.5f) / counts[layer];
		seen[layer]++;
		fNodes[i] = BPoint(x + Jitter(packages[i].name, 14),
			y + Jitter(packages[i].name + "y", jitter));
	}

	const std::vector<Volunteer>& volunteers = fPool.GetPool().Volunteers();
	fAnchors.resize(volunteers.size());
	float y = bounds.bottom - 30;
	float x0 = bounds.left + kSide + 40, x1 = bounds.right - kSide;
	int others = (int)volunteers.size() - 1;
	int n = 0;
	for (size_t i = 0; i < volunteers.size(); i++) {
		if (volunteers[i].you) {
			fAnchors[i] = BPoint(bounds.left + kSide, y);
			continue;
		}
		fAnchors[i] = BPoint(x0 + (x1 - x0) * (n + 0.5f) / others, y + 6);
		n++;
	}
}


BPoint
PoolView::_Anchor(int volunteer) const
{
	return fAnchors[volunteer];
}


void
PoolView::Paint(BView* canvas, BRect bounds)
{
	const Pool& pool = fPool.GetPool();
	const std::vector<Package>& packages = pool.Packages();
	const std::vector<Volunteer>& volunteers = pool.Volunteers();
	double now = pool.Now();
	if (fModel.live) {
		_PaintLive(canvas, bounds);
		return;
	}
	_Layout(bounds);
	float clock = (float)(system_time() % 1000000000) / 1000000.0f;

	canvas->SetDrawingMode(B_OP_ALPHA);

	// A glow behind the graph, as on the website.
	BPoint glow(bounds.right * 0.7f, bounds.top + bounds.Height() * 0.35f);
	for (int i = 8; i > 0; i--) {
		float r = bounds.Width() * 0.07f * i;
		canvas->SetHighColor(Alpha(kIndigo2, 9));
		canvas->FillEllipse(glow, r, r * 0.6f);
	}
	_DrawSky(canvas, bounds);

	// Title
	int sealed = 0;
	for (size_t i = 0; i < packages.size(); i++) {
		if (packages[i].state == kSealed)
			sealed++;
	}
	DrawLabel(canvas, BPoint(bounds.left + kSide, bounds.top + 30),
		"THE POOL \xc2\xb7 DEMO", kFire2);
	BFont display = Display(22);
	canvas->SetFont(&display);
	canvas->SetHighColor(kWashi);
	char title[64];
	snprintf(title, sizeof(title), "Round %d", fPool.Round());
	canvas->DrawString(title, BPoint(bounds.left + kSide, bounds.top + 58));
	float titleWidth = display.StringWidth(title);
	BFont mono = Mono(11);
	canvas->SetFont(&mono);
	canvas->SetHighColor(kMuted);
	char count[64];
	snprintf(count, sizeof(count), "%d of %d sealed", sealed,
		(int)packages.size());
	canvas->DrawString(count,
		BPoint(bounds.left + kSide + titleWidth + 14, bounds.top + 57));

	// How much of the round is sealed, the bar easing toward the count
	if (fShownSealed < 0)
		fShownSealed = sealed;
	fShownSealed += (sealed - fShownSealed) * 0.12f;
	if (fabsf(sealed - fShownSealed) < 0.01f)
		fShownSealed = sealed;
	BRect track(bounds.left + kSide, bounds.top + 67,
		bounds.left + kSide + 180, bounds.top + 69);
	canvas->SetHighColor(kIndigo2);
	canvas->FillRoundRect(track, 1.5f, 1.5f);
	if (!packages.empty() && fShownSealed > 0.01f) {
		BRect fill = track;
		fill.right = track.left + track.Width() * fShownSealed
			/ packages.size();
		BGradientLinear gradient(track.LeftTop(), track.RightTop());
		gradient.AddColor(kSeal, 0);
		gradient.AddColor(kGold, 255);
		canvas->FillRoundRect(fill, 1.5f, 1.5f, gradient);
	}

	// Legend, top right: in a row beside the title, or in a column above
	// the graph when the title leaves no room for the row
	float titleRight = bounds.left + kSide + titleWidth + 14
		+ mono.StringWidth(count);
	float columnWidth = 28 + mono.StringWidth("two matched");
	bool row = bounds.right - kSide - 300 >= titleRight + 24;
	float lx = row ? bounds.right - kSide - 300
		: bounds.right - kSide - columnWidth;
	float ly = bounds.top + (row ? 52 : 26);
	if (row || lx >= titleRight + 16) {
		canvas->SetHighColor(Alpha(kWashi2, 200));
		canvas->SetPenSize(1.4f);
		canvas->StrokeEllipse(BPoint(lx + 5, ly - 4), 4.5f, 4.5f);
		canvas->SetFont(&mono);
		canvas->SetHighColor(kMuted);
		canvas->DrawString("package", BPoint(lx + (row ? 16 : 28), ly));
		if (row)
			lx += 88;
		else
			ly += 16;
		canvas->SetHighColor(kFire2);
		canvas->SetPenSize(2);
		canvas->StrokeLine(BPoint(lx, ly - 4), BPoint(lx + 20, ly - 4));
		canvas->SetHighColor(kMuted);
		canvas->DrawString("a build", BPoint(lx + 28, ly));
		if (row)
			lx += 92;
		else
			ly += 16;
		DrawSeal(canvas, BPoint(lx + 7, ly - 4), 7);
		canvas->SetFont(&mono);
		canvas->SetHighColor(kMuted);
		canvas->DrawString("two matched", BPoint(lx + (row ? 20 : 28), ly));
	}

	// Dependencies
	canvas->SetPenSize(1);
	for (size_t i = 0; i < packages.size(); i++) {
		for (size_t d = 0; d < packages[i].deps.size(); d++) {
			int dep = packages[i].deps[d];
			BPoint a = fNodes[dep], b = fNodes[i];
			bool lit = packages[dep].state == kSealed
				&& packages[i].state == kSealed;
			float mid = (a.x + b.x) / 2;
			BPoint curve[4] = { a, BPoint(mid, a.y), BPoint(mid, b.y), b };
			if (lit) {
				// Both ends sealed: the thread glows, vermilion to gold
				canvas->SetPenSize(5);
				canvas->SetHighColor(Alpha(Mix(kSeal, kGold, 0.5f), 30));
				canvas->StrokeBezier(curve);
				canvas->SetPenSize(1);
				StrokeGradientBezier(canvas, curve, kSeal, kGold, 1.4f, 200);
			} else {
				canvas->SetHighColor(Alpha(kWashi, 22));
				canvas->StrokeBezier(curve);
			}
		}
	}

	// Builds in flight: each travels from the volunteer's machine to the
	// package as the build progresses.
	for (size_t i = 0; i < packages.size(); i++) {
		const Package& package = packages[i];
		for (size_t c = 0; c < package.copies.size(); c++) {
			const Copy& copy = package.copies[c];
			bool you = volunteers[copy.volunteer].you;
			BPoint p0 = _Anchor(copy.volunteer), p3 = fNodes[i];
			BPoint p1(p0.x, p0.y - (p0.y - p3.y) * 0.55f);
			BPoint p2(p3.x - (p3.x - p0.x) * 0.25f, p3.y + 40);
			float t = copy.needed > 0 ? copy.done / copy.needed : 0;

			// The whole path, faint
			canvas->SetPenSize(1);
			canvas->SetHighColor(Alpha(you ? kFire : kWashi, you ? 46 : 16));
			BPoint curve[4] = { p0, p1, p2, p3 };
			canvas->StrokeBezier(curve);

			// A soft glow along the tail
			StrokeGradientBezier(canvas, curve, you ? kSeal : kFire2,
				you ? kFire : kGold, you ? 7 : 4, you ? 40 : 22, t - 0.22f, t);

			// The bright part behind the head
			const int steps = 14;
			float tail = 0.18f;
			canvas->SetPenSize(you ? 2.4f : 1.6f);
			for (int s = 0; s < steps; s++) {
				float ta = t - tail * (1 - (float)s / steps);
				float tb = t - tail * (1 - (float)(s + 1) / steps);
				if (tb <= 0)
					continue;
				if (ta < 0)
					ta = 0;
				uint8 a = (uint8)((you ? 230 : 140) * (s + 1) / steps);
				canvas->SetHighColor(Alpha(you ? kFire : kFire2, a));
				canvas->StrokeLine(Bezier(p0, p1, p2, p3, ta),
					Bezier(p0, p1, p2, p3, tb));
			}
			BPoint head = Bezier(p0, p1, p2, p3, t);
			float pulse = 0.5f + 0.5f * sinf((float)now * 6 + i);
			DrawGlow(canvas, head, you ? 15 : 8, kFire,
				you ? 1.0f + 0.6f * pulse : 0.6f);

			// Sparks shed behind the head, drifting off the thread
			for (int k = 1; k <= 5; k++) {
				float tk = t - 0.022f * k;
				if (tk <= 0)
					break;
				BPoint spark = BezierPoint(curve, tk);
				float drift = sinf(clock * 5 + k * 2.1f + i) * 2.2f * k / 5;
				spark.x += drift;
				spark.y -= drift * 0.6f;
				canvas->SetHighColor(Alpha(Mix(kFire2, kWashi, 0.35f),
					(uint8)((you ? 190 : 110) * (1 - k / 6.0f))));
				float r = (you ? 1.6f : 1.1f) * (1 - k * 0.12f);
				canvas->FillEllipse(spark, r, r);
			}
			canvas->SetHighColor(you ? Mix(kFire, kWashi, 0.75f)
				: Alpha(kFire2, 240));
			canvas->FillEllipse(head, you ? 3.4f : 2.4f, you ? 3.4f : 2.4f);
		}
	}

	// Packages
	int newest = -1;
	for (size_t i = 0; i < packages.size(); i++) {
		if (packages[i].state == kSealed && (newest < 0
				|| packages[i].sealedAt > packages[newest].sealedAt))
			newest = (int)i;
	}
	BFont small = Mono(10);
	for (size_t i = 0; i < packages.size(); i++) {
		const Package& package = packages[i];
		BPoint at = fNodes[i];
		bool yours = false;
		for (size_t c = 0; c < package.copies.size(); c++) {
			if (volunteers[package.copies[c].volunteer].you)
				yours = true;
		}

		if (package.state == kSealed) {
			float age = (float)(now - package.sealedAt);
			float pop = age < 0.35f ? 1.0f + 0.6f * (1 - age / 0.35f) : 1.0f;
			DrawGlow(canvas, at, kSealHalf * 1.9f, kSeal, 0.7f);
			if (age < 1.8f) {
				// It just landed: rings spread and sparks fly
				DrawRipples(canvas, at, age, kSealHalf + 2, kFire2);
				DrawSparks(canvas, at, age, kSealHalf,
					(uint32)i * 7919u + (uint32)(package.sealedAt * 10),
					Mix(kFire2, kWashi, 0.4f));
			} else if ((int)i == newest) {
				// The newest seal keeps a gentle ripple
				DrawRipples(canvas, at, fmodf(age - 1.8f, 3.0f),
					kSealHalf + 2, kFire2, 0.35f);
			}
			DrawSeal(canvas, at, kSealHalf, pop);
		} else {
			uint8 a = package.state == kWaiting ? 60
				: package.state == kReady ? 150 : 225;
			canvas->SetPenSize(yours ? 2.0f : 1.4f);
			canvas->SetHighColor(yours ? kFire : Alpha(kWashi, a));
			canvas->StrokeEllipse(at, kNode, kNode);
			if (!package.results.empty()) {
				canvas->SetHighColor(Alpha(kFire2, 220));
				canvas->FillEllipse(at, 2.8f, 2.8f);
			}
		}

		if (package.mismatches > 0 && package.state != kSealed) {
			// Builds disagreed: gold rings breathe until a third settles it
			float breathe = 0.6f + 0.4f * sinf(clock * 2.4f + i);
			canvas->SetPenSize(1.6f);
			canvas->SetHighColor(Alpha(kGold, (uint8)(150 * breathe)));
			canvas->StrokeEllipse(at, kNode + 5, kNode + 5);
			canvas->SetPenSize(1);
			canvas->SetHighColor(Alpha(kGold, (uint8)(60 * breathe)));
			canvas->StrokeEllipse(at, kNode + 11, kNode + 11);
		}

		float since = (float)(now - package.mismatchAt);
		if (package.mismatchAt >= 0 && since < 1.6f) {
			float k = since / 1.6f;
			canvas->SetPenSize(2);
			canvas->SetHighColor(Alpha(kGold, (uint8)(255 * (1 - k))));
			canvas->StrokeEllipse(at, kNode + 4 + 16 * k, kNode + 4 + 16 * k);
		}

		canvas->SetFont(&small);
		uint8 la = package.state == kSealed ? 170
			: package.state == kWaiting ? 70 : 120;
		canvas->SetHighColor(Alpha(yours ? kFire2 : kWashi, yours ? 240 : la));
		float w = small.StringWidth(package.name.c_str());
		canvas->DrawString(package.name.c_str(),
			BPoint(at.x - w / 2, at.y + kSealHalf + 13));
	}

	// The volunteers' machines along the bottom, you first.
	canvas->SetPenSize(1);
	canvas->SetHighColor(Alpha(kWashi, 18));
	canvas->StrokeLine(BPoint(bounds.left + kSide, bounds.bottom - 52),
		BPoint(bounds.right - kSide, bounds.bottom - 52));
	for (size_t v = 0; v < volunteers.size(); v++) {
		BPoint at = fAnchors[v];
		bool busy = volunteers[v].package >= 0;
		if (volunteers[v].you) {
			bool active = fPool.YouActive();
			if (active)
				DrawGlow(canvas, at, 24, kFire, 1.2f);
			canvas->SetHighColor(Alpha(kFire, active ? 60 : 20));
			canvas->FillEllipse(at, 11, 11);
			canvas->SetHighColor(active ? kFire : kMuted);
			canvas->FillEllipse(at, 5.5f, 5.5f);
			BFont label = Body(12, true);
			canvas->SetFont(&label);
			canvas->SetHighColor(active ? kWashi : kMuted);
			canvas->DrawString("you", BPoint(at.x + 16, at.y + 4));
		} else {
			canvas->SetHighColor(busy ? Alpha(kFire2, 200) : Alpha(kWashi, 70));
			canvas->FillEllipse(at, 3, 3);
		}
	}

	_DrawCard(canvas, bounds);
}


// Stars that twinkle, and a faint 合 behind the graph.
void
PoolView::_DrawSky(BView* canvas, BRect bounds)
{
	float clock = (float)(system_time() % 1000000000) / 1000000.0f;
	for (int i = 0; i < 28; i++) {
		uint32 h = (uint32)(i + 1) * 2654435761u;
		h = (h ^ (h >> 15)) * 2246822519u;
		h ^= h >> 13;
		float x = bounds.left + (h & 0xffff) / 65535.0f * bounds.Width();
		float y = bounds.top + ((h >> 16) & 0xffff) / 65535.0f
			* (bounds.Height() - 60);
		float twinkle = 0.5f + 0.5f
			* sinf(clock * (0.6f + (h & 7) * 0.15f) + i * 1.7f);
		float r = 0.5f + ((h >> 8) & 3) * 0.2f;
		canvas->SetHighColor(Alpha(kWashi, (uint8)(20 + 70 * twinkle)));
		canvas->FillEllipse(BPoint(x, y), r, r);
	}
	BFont big = Cjk(std::min(300.0f, bounds.Height() * 0.6f));
	canvas->SetFont(&big);
	canvas->SetHighColor(Alpha(kWashi, 4));
	canvas->DrawString("\xe5\x90\x88", BPoint(bounds.right
		- big.StringWidth("\xe5\x90\x88") - 20, bounds.bottom - 70));
}


void
PoolView::_DrawCard(BView* canvas, BRect bounds)
{
	if (!fMouseInside)
		return;

	const Pool& pool = fPool.GetPool();
	const std::vector<Package>& packages = pool.Packages();
	const std::vector<Volunteer>& volunteers = pool.Volunteers();

	std::string title, line1, line2;
	BPoint at;
	float best = 18 * 18;
	for (size_t i = 0; i < fNodes.size(); i++) {
		float dx = fNodes[i].x - fMouse.x, dy = fNodes[i].y - fMouse.y;
		if (dx * dx + dy * dy >= best)
			continue;
		best = dx * dx + dy * dy;
		at = fNodes[i];
		const Package& package = packages[i];
		title = package.name + " " + package.version;
		line1 = package.arch;
		switch (package.state) {
			case kSealed:
				line2 = "Sealed: " + volunteers[package.results[0].volunteer].name
					+ " and " + volunteers[package.results[1].volunteer].name
					+ " matched";
				break;
			case kBuilding:
			{
				line2 = "Building on ";
				for (size_t c = 0; c < package.copies.size(); c++) {
					if (c > 0)
						line2 += " and ";
					line2 += volunteers[package.copies[c].volunteer].name;
				}
				if (!package.results.empty()) {
					line2 += package.copies.empty() ? "" : "; ";
					line2 += volunteers[package.results[0].volunteer].name
						+ " finished";
				}
				break;
			}
			case kReady:
				line2 = "Ready to hand out";
				break;
			case kWaiting:
			{
				line2 = "Waiting for ";
				int n = 0;
				for (size_t d = 0; d < package.deps.size(); d++) {
					const Package& dep = packages[package.deps[d]];
					if (dep.state == kSealed)
						continue;
					line2 += (n++ > 0 ? ", " : "") + dep.name;
				}
				break;
			}
		}
		if (package.mismatches > 0) {
			char buffer[48];
			snprintf(buffer, sizeof(buffer), " \xc2\xb7 %d mismatch%s",
				package.mismatches, package.mismatches > 1 ? "es" : "");
			line1 += buffer;
		}
	}
	for (size_t v = 0; title.empty() && v < fAnchors.size(); v++) {
		float dx = fAnchors[v].x - fMouse.x, dy = fAnchors[v].y - fMouse.y;
		if (dx * dx + dy * dy >= 14 * 14)
			continue;
		at = fAnchors[v];
		title = volunteers[v].you ? "This machine" : volunteers[v].name;
		line1 = volunteers[v].you ? "you" : "a volunteer";
		int p = volunteers[v].package;
		line2 = p >= 0 ? "Building " + packages[p].name : "Idle";
	}
	if (title.empty())
		return;

	BFont titleFont = Display(15);
	BFont mono = Mono(10.5f);
	BFont body = Body(11.5f);
	float width = std::max(titleFont.StringWidth(title.c_str()),
		std::max(mono.StringWidth(line1.c_str()),
			body.StringWidth(line2.c_str()))) + 28;
	BRect card(0, 0, width, 70);
	card.OffsetTo(at.x + 16, at.y - 35);
	if (card.right > bounds.right - 8)
		card.OffsetTo(at.x - 16 - card.Width(), card.top);
	if (card.bottom > bounds.bottom - 8)
		card.OffsetBy(0, bounds.bottom - 8 - card.bottom);
	if (card.top < bounds.top + 8)
		card.OffsetTo(card.left, bounds.top + 8);

	canvas->SetDrawingMode(B_OP_ALPHA);
	canvas->SetHighColor(Alpha(kNight, 120));
	canvas->FillRoundRect(card.OffsetByCopy(3, 4), 6, 6);
	canvas->SetHighColor(Alpha(kIndigo, 245));
	canvas->FillRoundRect(card, 6, 6);
	canvas->SetHighColor(Alpha(kWashi, 40));
	canvas->SetPenSize(1);
	canvas->StrokeRoundRect(card, 6, 6);

	canvas->SetFont(&titleFont);
	canvas->SetHighColor(kWashi);
	canvas->DrawString(title.c_str(), BPoint(card.left + 14, card.top + 22));
	canvas->SetFont(&mono);
	canvas->SetHighColor(kFire2);
	canvas->DrawString(line1.c_str(), BPoint(card.left + 14, card.top + 39));
	canvas->SetFont(&body);
	canvas->SetHighColor(kWashi2);
	canvas->DrawString(line2.c_str(), BPoint(card.left + 14, card.top + 57));
}


// #pragma mark - Live: the coordinator's repository


void
PoolView::_PaintLive(BView* canvas, BRect bounds)
{
	const LiveSnapshot& live = fModel.snapshot;
	canvas->SetDrawingMode(B_OP_ALPHA);

	BPoint glow(bounds.right * 0.7f, bounds.top + bounds.Height() * 0.35f);
	for (int i = 8; i > 0; i--) {
		float r = bounds.Width() * 0.07f * i;
		canvas->SetHighColor(Alpha(kIndigo2, 9));
		canvas->FillEllipse(glow, r, r * 0.6f);
	}

	std::string platform = live.platform.empty() ? "\xe2\x80\xa6" : live.platform;
	std::string label = "THE REPOSITORY \xc2\xb7 ";
	for (size_t i = 0; i < platform.size(); i++)
		label += (char)toupper((unsigned char)platform[i]);
	DrawLabel(canvas, BPoint(bounds.left + kSide, bounds.top + 30),
		label.c_str(), kFire2);
	BFont display = Display(22);
	canvas->SetFont(&display);
	canvas->SetHighColor(kWashi);
	char title[64];
	snprintf(title, sizeof(title), "%d package%s", (int)live.recent.size(),
		live.recent.size() == 1 ? "" : "s");
	canvas->DrawString(title, BPoint(bounds.left + kSide, bounds.top + 58));
	BFont mono = Mono(11);
	const char* subtitle = "published, newest first";
	float subtitleLeft = bounds.left + kSide + display.StringWidth(title) + 14;
	float subtitleRight = subtitleLeft + mono.StringWidth(subtitle);

	// Legend: in a row beside the title, or else in a column at the top
	// right, with the title's note left out if it would run into it
	float columnWidth = 28 + mono.StringWidth("2 builds match");
	bool row = bounds.right - kSide - 250 >= subtitleRight + 24;
	float lx = row ? bounds.right - kSide - 250
		: bounds.right - kSide - columnWidth;
	float ly = bounds.top + (row ? 52 : 26);
	bool legend = row || lx >= bounds.left + kSide
		+ LabelWidth(canvas, label.c_str()) + 16;
	if (row || !legend || subtitleRight + 16 <= lx) {
		canvas->SetFont(&mono);
		canvas->SetHighColor(kMuted);
		canvas->DrawString(subtitle, BPoint(subtitleLeft, bounds.top + 57));
	}
	if (legend) {
		DrawSeal(canvas, BPoint(lx + 7, ly - 4), 7);
		canvas->SetFont(&mono);
		canvas->SetHighColor(kMuted);
		canvas->DrawString("2 builds match", BPoint(lx + (row ? 20 : 28), ly));
		if (row)
			lx += 130;
		else
			ly += 16;
		canvas->SetPenSize(1.6f);
		canvas->SetHighColor(kFire);
		canvas->StrokeRoundRect(BRect(lx, ly - 11, lx + 14, ly + 3), 4, 4);
		canvas->SetPenSize(1);
		canvas->SetHighColor(kMuted);
		canvas->DrawString("built here", BPoint(lx + (row ? 22 : 28), ly));
	}

	// The packages as a wall of seals
	BRect area(bounds.left + kSide, bounds.top + kTop + 4,
		bounds.right - kSide, bounds.bottom - kBottom - 16);
	const float cellWidth = 92, cellHeight = 52;
	int columns = std::max(1, (int)(area.Width() / cellWidth));
	int rows = std::max(1, (int)(area.Height() / cellHeight));
	int capacity = columns * rows;
	BFont small = Mono(10);
	bigtime_t clock = system_time();
	BPoint next = area.LeftTop() + BPoint(cellWidth / 2, cellHeight / 2 - 6);
	// The builds in progress take the first cells: this machine's, then the
	// x86 VM's
	bool vmJob = fModel.TwoMachines() && fModel.vmSnapshot.hasJob;
	int first = (live.hasJob ? 1 : 0) + (vmJob ? 1 : 0);
	int shown = std::min((int)live.recent.size(), capacity - first);
	BPoint vmNext = area.LeftTop() + BPoint(cellWidth * ((live.hasJob ? 1 : 0)
		% columns + 0.5f), cellHeight / 2 - 6);
	// Packages already there when the list first arrives don't land again.
	bool initial = fFirstSeen.empty();
	for (int i = 0; i < shown; i++) {
		const PortalPackage& package = live.recent[i];
		int cell = i + first;
		BPoint at(area.left + (cell % columns + 0.5f) * cellWidth,
			area.top + (cell / columns + 0.5f) * cellHeight - 6);
		bool mine = std::find(live.built.begin(), live.built.end(),
			package.portName) != live.built.end();
		if (mine) {
			canvas->SetPenSize(1.6f);
			canvas->SetHighColor(Alpha(kFire, 220));
			canvas->StrokeRoundRect(BRect(at.x - 15, at.y - 15, at.x + 15,
				at.y + 15), 6, 6);
			canvas->SetPenSize(1);
		}
		bigtime_t& seen = fFirstSeen[package.fileName];
		if (seen == 0 && !initial)
			seen = clock;
		else if (seen == 0)
			seen = 1;
		float age = seen > 1 ? (clock - seen) / 1000000.0f : 99;
		if (package.Matched()) {
			float pop = age < 0.35f ? 1.0f + 0.6f * (1 - age / 0.35f) : 1.0f;
			DrawGlow(canvas, at, kSealHalf * 1.9f, kSeal, 0.7f);
			if (age < 1.8f) {
				DrawRipples(canvas, at, age, kSealHalf + 2, kFire2);
				DrawSparks(canvas, at, age, kSealHalf, (uint32)seen,
					Mix(kFire2, kWashi, 0.4f));
			}
			DrawSeal(canvas, at, kSealHalf, pop);
		} else {
			// Published without two matching builds (imported, older, or
			// approved by an admin): an empty frame where the seal would be.
			canvas->SetHighColor(Alpha(kWashi, 90));
			canvas->StrokeRoundRect(BRect(at.x - kSealHalf, at.y - kSealHalf,
				at.x + kSealHalf, at.y + kSealHalf), 3, 3);
		}
		std::string name = package.portName.empty() ? package.fileName
			: package.portName;
		BString text(name.c_str());
		small.TruncateString(&text, B_TRUNCATE_END, cellWidth - 8);
		canvas->SetFont(&small);
		canvas->SetHighColor(Alpha(mine ? kFire2 : kWashi, mine ? 240 : 150));
		float w = small.StringWidth(text.String());
		canvas->DrawString(text.String(), BPoint(at.x - w / 2,
			at.y + kSealHalf + 14));
	}
	if (live.recent.empty() && !live.hasJob) {
		BFont body = Body(13);
		canvas->SetFont(&body);
		canvas->SetHighColor(kMuted);
		const char* text = live.state == kLiveOff
			? "Connect to a coordinator to see its packages."
			: "Nothing published for this platform yet.";
		canvas->DrawString(text, BPoint(area.left + 4, area.top + 30));
	}

	// You, and the build in progress
	BPoint you(bounds.left + kSide, bounds.bottom - 30);
	canvas->SetHighColor(Alpha(kWashi, 18));
	canvas->StrokeLine(BPoint(bounds.left + kSide, bounds.bottom - 52),
		BPoint(bounds.right - kSide, bounds.bottom - 52));
	bool active = live.state == kLiveBuilding || live.state == kLiveUploading
		|| live.state == kLiveReporting;
	// The x86 VM, beside you, with its build in flight
	BPoint vmAt(bounds.left + kSide + 130, bounds.bottom - 30);
	if (fModel.TwoMachines()) {
		const LiveSnapshot& vm = fModel.vmSnapshot;
		bool vmActive = !fModel.vmFrozen && (vm.state == kLiveBuilding
			|| vm.state == kLiveUploading || vm.state == kLiveReporting);
		if (vmJob) {
			DrawLiveThread(canvas, vmAt, vmNext, kGold, kFire2, vmActive, clock,
				0.45f);
			float pulse = 0.5f + 0.5f * sinf((float)clock / 300000.0f);
			DrawGlow(canvas, vmNext, 16, kGold, 0.6f + 0.6f * pulse);
			canvas->SetPenSize(2);
			canvas->SetHighColor(kGold);
			canvas->StrokeEllipse(vmNext, kNode + 1, kNode + 1);
			canvas->SetPenSize(1);
			canvas->SetFont(&small);
			canvas->SetHighColor(kGold);
			BString text(vm.job.port.c_str());
			small.TruncateString(&text, B_TRUNCATE_END, cellWidth - 8);
			canvas->DrawString(text.String(), BPoint(vmNext.x
				- small.StringWidth(text.String()) / 2, vmNext.y + kSealHalf + 14));
		}
		if (vmActive)
			DrawGlow(canvas, vmAt, 20, kGold, 1.0f);
		BRect box(vmAt.x - 9, vmAt.y - 9, vmAt.x + 9, vmAt.y + 9);
		canvas->SetPenSize(1.6f);
		canvas->SetHighColor(kGold);
		canvas->StrokeRoundRect(box, 4, 4);
		canvas->FillEllipse(vmAt, 3.5f, 3.5f);
		canvas->SetPenSize(1);
		BFont vmBold = Body(12, true);
		canvas->SetFont(&vmBold);
		canvas->SetHighColor(vmActive ? kWashi : kMuted);
		canvas->DrawString("your VM", BPoint(vmAt.x + 16, vmAt.y - 1));
		BFont vmMono = Mono(9.5f);
		canvas->SetFont(&vmMono);
		canvas->SetHighColor(kMuted);
		std::string vmWhat = std::string("x86 \xc2\xb7 ") + HostAccelerator().name;
		canvas->DrawString(vmWhat.c_str(), BPoint(vmAt.x + 16, vmAt.y + 12));
	}

	if (live.hasJob) {
		BPoint p0 = you, p3 = next;
		DrawLiveThread(canvas, p0, p3, kFire, kSeal, active, clock, 0);
		float pulse = 0.5f + 0.5f * sinf((float)clock / 250000.0f);
		canvas->SetHighColor(Alpha(active ? kFire : kGold,
			(uint8)(60 + 80 * pulse)));
		canvas->FillEllipse(next, 13, 13);
		canvas->SetPenSize(2);
		canvas->SetHighColor(active ? kFire : kGold);
		canvas->StrokeEllipse(next, kNode + 1, kNode + 1);
		canvas->SetPenSize(1);
		canvas->SetFont(&small);
		canvas->SetHighColor(kFire2);
		BString text(live.job.port.c_str());
		small.TruncateString(&text, B_TRUNCATE_END, cellWidth - 8);
		float w = small.StringWidth(text.String());
		canvas->DrawString(text.String(), BPoint(next.x - w / 2,
			next.y + kSealHalf + 14));
	}
	canvas->SetHighColor(Alpha(kFire, active ? 60 : 20));
	canvas->FillEllipse(you, 11, 11);
	canvas->SetHighColor(active ? kFire : kMuted);
	canvas->FillEllipse(you, 5.5f, 5.5f);
	BFont bold = Body(12, true);
	canvas->SetFont(&bold);
	canvas->SetHighColor(active ? kWashi : kMuted);
	canvas->DrawString("you", BPoint(you.x + 16, you.y + 4));
	canvas->SetFont(&mono);
	canvas->SetHighColor(kMuted);
	float messageLeft = fModel.TwoMachines() ? vmAt.x + 120 : you.x + 52;
	BString message(live.message.c_str());
	mono.TruncateString(&message, B_TRUNCATE_END,
		bounds.right - kSide - messageLeft);
	canvas->DrawString(message.String(), BPoint(messageLeft, you.y + 4));
}
