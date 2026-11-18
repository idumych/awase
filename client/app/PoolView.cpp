/*
 * Copyright 2026, Ian Dumych, idumych@gmail.com.
 * SPDX-License-Identifier: MIT
 */


#include "Views.h"

#include "DemoPool.h"

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


PoolView::PoolView(const Model& model)
	:
	InkView("pool"),
	fModel(model),
	fPool(*model.demo),
	fMouseInside(false)
{
	SetExplicitMinSize(BSize(480, 380));
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
	float top = bounds.top + kTop, bottom = bounds.bottom - kBottom - 20;
	fNodes.resize(packages.size());
	for (size_t i = 0; i < packages.size(); i++) {
		int layer = packages[i].layer;
		float x = layers > 1
			? left + (right - left) * layer / (layers - 1) : (left + right) / 2;
		float y = top + (bottom - top) * (seen[layer] + 0.5f) / counts[layer];
		seen[layer]++;
		fNodes[i] = BPoint(x + Jitter(packages[i].name, 14),
			y + Jitter(packages[i].name + "y", 10));
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

	canvas->SetDrawingMode(B_OP_ALPHA);

	// A glow behind the graph, as on the website.
	BPoint glow(bounds.right * 0.7f, bounds.top + bounds.Height() * 0.35f);
	for (int i = 8; i > 0; i--) {
		float r = bounds.Width() * 0.07f * i;
		canvas->SetHighColor(Alpha(kIndigo2, 9));
		canvas->FillEllipse(glow, r, r * 0.6f);
	}

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

	// Legend, top right
	float lx = bounds.right - kSide - 300, ly = bounds.top + 52;
	canvas->SetHighColor(Alpha(kWashi2, 200));
	canvas->SetPenSize(1.4f);
	canvas->StrokeEllipse(BPoint(lx + 5, ly - 4), 4.5f, 4.5f);
	canvas->SetFont(&mono);
	canvas->SetHighColor(kMuted);
	canvas->DrawString("package", BPoint(lx + 16, ly));
	lx += 88;
	canvas->SetHighColor(kFire2);
	canvas->SetPenSize(2);
	canvas->StrokeLine(BPoint(lx, ly - 4), BPoint(lx + 20, ly - 4));
	canvas->SetHighColor(kMuted);
	canvas->DrawString("a build", BPoint(lx + 28, ly));
	lx += 92;
	DrawSeal(canvas, BPoint(lx + 7, ly - 4), 7);
	canvas->SetFont(&mono);
	canvas->SetHighColor(kMuted);
	canvas->DrawString("two matched", BPoint(lx + 20, ly));

	// Dependencies
	canvas->SetPenSize(1);
	for (size_t i = 0; i < packages.size(); i++) {
		for (size_t d = 0; d < packages[i].deps.size(); d++) {
			int dep = packages[i].deps[d];
			BPoint a = fNodes[dep], b = fNodes[i];
			bool lit = packages[dep].state == kSealed
				&& packages[i].state == kSealed;
			canvas->SetHighColor(lit ? Alpha(kFire2, 70) : Alpha(kWashi, 22));
			float mid = (a.x + b.x) / 2;
			BPoint curve[4] = { a, BPoint(mid, a.y), BPoint(mid, b.y), b };
			canvas->StrokeBezier(curve);
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
			if (you) {
				canvas->SetHighColor(Alpha(kFire, (uint8)(50 + 50 * pulse)));
				canvas->FillEllipse(head, 7, 7);
			}
			canvas->SetHighColor(Alpha(you ? kFire : kFire2, 240));
			canvas->FillEllipse(head, you ? 3.2f : 2.4f, you ? 3.2f : 2.4f);
		}
	}

	// Packages
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
	canvas->SetFont(&mono);
	canvas->SetHighColor(kMuted);
	canvas->DrawString("published, newest first",
		BPoint(bounds.left + kSide + display.StringWidth(title) + 14,
			bounds.top + 57));

	// Legend
	float lx = bounds.right - kSide - 230, ly = bounds.top + 52;
	DrawSeal(canvas, BPoint(lx + 7, ly - 4), 7);
	canvas->SetFont(&mono);
	canvas->SetHighColor(kMuted);
	canvas->DrawString("published", BPoint(lx + 20, ly));
	lx += 110;
	canvas->SetPenSize(1.6f);
	canvas->SetHighColor(kFire);
	canvas->StrokeRoundRect(BRect(lx, ly - 11, lx + 14, ly + 3), 4, 4);
	canvas->SetPenSize(1);
	canvas->SetHighColor(kMuted);
	canvas->DrawString("by you", BPoint(lx + 22, ly));

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
	int shown = std::min((int)live.recent.size(), capacity - (live.hasJob ? 1 : 0));
	int first = live.hasJob ? 1 : 0;	// the build in progress takes the first cell
	for (int i = 0; i < shown; i++) {
		const PortalPackage& package = live.recent[i];
		int cell = i + first;
		BPoint at(area.left + (cell % columns + 0.5f) * cellWidth,
			area.top + (cell / columns + 0.5f) * cellHeight - 6);
		bool mine = std::find(live.published.begin(), live.published.end(),
			package.portName) != live.published.end();
		if (mine) {
			canvas->SetPenSize(1.6f);
			canvas->SetHighColor(Alpha(kFire, 220));
			canvas->StrokeRoundRect(BRect(at.x - 15, at.y - 15, at.x + 15,
				at.y + 15), 6, 6);
			canvas->SetPenSize(1);
		}
		DrawSeal(canvas, at, kSealHalf);
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
	if (live.hasJob) {
		BPoint p0 = you, p3 = next;
		BPoint p1(p0.x, p0.y - (p0.y - p3.y) * 0.6f);
		BPoint p2(p3.x - 30, p3.y + 60);
		BPoint curve[4] = { p0, p1, p2, p3 };
		canvas->SetPenSize(1);
		canvas->SetHighColor(Alpha(kFire, 50));
		canvas->StrokeBezier(curve);
		// Nobody knows how far a build has got, so the light runs along the
		// thread instead of creeping up it.
		float t = active ? (float)(clock % 1600000) / 1600000.0f : 0.5f;
		canvas->SetPenSize(2.4f);
		for (int s = 0; s < 14; s++) {
			float ta = t - 0.2f * (1 - s / 14.0f);
			float tb = t - 0.2f * (1 - (s + 1) / 14.0f);
			if (tb <= 0)
				continue;
			canvas->SetHighColor(Alpha(active ? kFire : kGold,
				(uint8)(230 * (s + 1) / 14)));
			canvas->StrokeLine(Bezier(p0, p1, p2, p3, std::max(0.0f, ta)),
				Bezier(p0, p1, p2, p3, tb));
		}
		canvas->SetPenSize(1);
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
	canvas->DrawString(live.message.c_str(), BPoint(you.x + 52, you.y + 4));
}
