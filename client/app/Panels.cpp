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


static const float kPad = 26;


static void
Divider(BView* canvas, BRect bounds, bool left, bool top)
{
	canvas->SetDrawingMode(B_OP_ALPHA);
	canvas->SetPenSize(1);
	canvas->SetHighColor(Alpha(kWashi, 24));
	if (left)
		canvas->StrokeLine(bounds.LeftTop(), bounds.LeftBottom());
	if (top)
		canvas->StrokeLine(bounds.LeftTop(), bounds.RightTop());
}


static void
DrawTruncated(BView* canvas, const BFont& font, const char* text, BPoint where,
	float width)
{
	BString string(text);
	font.TruncateString(&string, B_TRUNCATE_END, width);
	canvas->DrawString(string.String(), where);
}


// #pragma mark - HeaderView


HeaderView::HeaderView(const Model& model)
	:
	InkView("header"),
	fModel(model),
	fPool(*model.demo),
	fHover(false)
{
	SetExplicitMinSize(BSize(600, 76));
	SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, 76));
}


BRect
HeaderView::_ButtonFrame() const
{
	BRect bounds = Bounds();
	return BRect(bounds.right - kPad - 112, bounds.top + 20,
		bounds.right - kPad, bounds.top + 56);
}


void
HeaderView::MouseDown(BPoint where)
{
	if (_ButtonFrame().Contains(where))
		Window()->PostMessage(kMsgTogglePause);
}


void
HeaderView::MouseMoved(BPoint where, uint32 transit, const BMessage* drag)
{
	bool hover = transit != B_EXITED_VIEW && _ButtonFrame().Contains(where);
	if (hover != fHover) {
		fHover = hover;
		Invalidate();
	}
	InkView::MouseMoved(where, transit, drag);
}


const char*
HeaderView::_Status(rgb_color& dot, std::string& storage) const
{
	if (fModel.live) {
		const LiveSnapshot& live = fModel.snapshot;
		switch (live.state) {
			case kLiveBuilding:
				dot = kFire;
				return "Building";
			case kLiveUploading:
			case kLiveReporting:
				dot = kFire2;
				return live.state == kLiveUploading ? "Uploading" : "Reporting";
			case kLivePaused:
				dot = kGold;
				return "Paused";
			case kLiveWaitingIdle:
				dot = kMuted;
				return "Waiting for the computer to be idle";
			case kLiveIdle:
				dot = kWashi2;
				return "Waiting for work";
			case kLiveConnecting:
				dot = kWashi2;
				return "Connecting";
			case kLiveError:
				dot = kGold;
				return "Can't build right now";
			case kLiveOff:
				break;
		}
		dot = kMuted;
		return "Not connected";
	}

	const Pool& pool = fPool.GetPool();
	if (fPool.GetLimits().paused) {
		dot = kGold;
		return "Paused";
	}
	if (fPool.WaitingForIdle()) {
		dot = kMuted;
		return "Waiting for the computer to be idle";
	}
	if (pool.YourPackage() >= 0) {
		dot = kFire;
		return "Building";
	}
	if (pool.LastYouBuilt() >= 0
		&& pool.Packages()[pool.LastYouBuilt()].state != kSealed) {
		dot = kFire2;
		return "Waiting for a second build";
	}
	dot = kWashi2;
	(void)storage;
	return "Looking for work";
}


void
HeaderView::Paint(BView* canvas, BRect bounds)
{
	canvas->SetDrawingMode(B_OP_ALPHA);
	canvas->SetHighColor(kNight2);
	canvas->FillRect(bounds);
	canvas->SetHighColor(Alpha(kWashi, 24));
	canvas->StrokeLine(bounds.LeftBottom(), bounds.RightBottom());

	DrawSeal(canvas, BPoint(bounds.left + kPad + 18, bounds.top + 38), 18);

	BFont name = Display(28);
	canvas->SetFont(&name);
	canvas->SetHighColor(kWashi);
	float x = bounds.left + kPad + 50;
	canvas->DrawString("Awase", BPoint(x, bounds.top + 42));
	x += name.StringWidth("Awase") + 10;
	BFont kana = Cjk(15);
	canvas->SetFont(&kana);
	canvas->SetHighColor(kFire2);
	canvas->DrawString("合わせ", BPoint(x, bounds.top + 41));
	std::string subtitle = "VOLUNTEER BUILDS FOR RENKU PORTS";
	if (fModel.live) {
		std::string host = fModel.snapshot.server;
		size_t scheme = host.find("://");
		if (scheme != std::string::npos)
			host.erase(0, scheme + 3);
		while (!host.empty() && host[host.size() - 1] == '/')
			host.erase(host.size() - 1);
		subtitle = "BUILDING FOR " + host;
		if (!fModel.snapshot.platform.empty())
			subtitle += " \xc2\xb7 " + fModel.snapshot.platform;
		for (size_t i = 0; i < subtitle.size(); i++)
			subtitle[i] = toupper((unsigned char)subtitle[i]);
	}
	DrawLabel(canvas, BPoint(bounds.left + kPad + 51, bounds.top + 60),
		subtitle.c_str(), kMuted);

	// What this machine is doing
	rgb_color dot;
	std::string storage;
	const char* status = _Status(dot, storage);

	BFont body = Body(13, true);
	BString shown(status);
	body.TruncateString(&shown, B_TRUNCATE_END, bounds.Width() * 0.42f);
	status = shown.String();
	float width = body.StringWidth(status) + 44;
	BRect button = _ButtonFrame();
	BRect pill(button.left - 16 - width, button.top + 3, button.left - 16,
		button.bottom - 3);
	canvas->SetHighColor(Alpha(dot, 40));
	canvas->FillRoundRect(pill, pill.Height() / 2, pill.Height() / 2);
	canvas->SetHighColor(Alpha(dot, 110));
	canvas->StrokeRoundRect(pill, pill.Height() / 2, pill.Height() / 2);
	float pulse = dot == kFire
		? 0.5f + 0.5f * sinf((float)system_time() / 300000.0f) : 1;
	canvas->SetHighColor(Alpha(dot, (uint8)(140 + 115 * pulse)));
	canvas->FillEllipse(BPoint(pill.left + 18, pill.top + pill.Height() / 2),
		4.5f, 4.5f);
	canvas->SetFont(&body);
	canvas->SetHighColor(kWashi);
	canvas->DrawString(status, BPoint(pill.left + 32, pill.top + 20));

	// Pause / Resume
	bool paused = fPool.GetLimits().paused;
	if (paused) {
		canvas->SetHighColor(fHover ? kFire2 : kFire);
		canvas->FillRoundRect(button, 5, 5);
	} else {
		canvas->SetHighColor(Alpha(kWashi, fHover ? 30 : 0));
		canvas->FillRoundRect(button, 5, 5);
		canvas->SetHighColor(Alpha(kWashi, fHover ? 200 : 90));
		canvas->StrokeRoundRect(button, 5, 5);
	}
	const char* label = paused ? "Resume" : "Pause";
	BFont bold = Body(13.5f, true);
	canvas->SetFont(&bold);
	canvas->SetHighColor(paused ? make_color(22, 10, 2) : kWashi);
	// The icon: two bars, or a triangle
	float cx = button.left + 24, cy = button.top + button.Height() / 2;
	if (paused) {
		BPoint triangle[3] = { BPoint(cx - 4, cy - 6), BPoint(cx + 6, cy),
			BPoint(cx - 4, cy + 6) };
		canvas->FillPolygon(triangle, 3);
	} else {
		canvas->FillRect(BRect(cx - 5, cy - 6, cx - 2, cy + 6));
		canvas->FillRect(BRect(cx + 2, cy - 6, cx + 5, cy + 6));
	}
	canvas->DrawString(label, BPoint(button.left + 40, cy + 5));
}


// #pragma mark - NowView


NowView::NowView(const Model& model)
	:
	InkView("now"),
	fModel(model),
	fPool(*model.demo)
{
	SetExplicitMinSize(BSize(360, 204));
	SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, 204));
}


void
NowView::Paint(BView* canvas, BRect bounds)
{
	canvas->SetDrawingMode(B_OP_ALPHA);
	canvas->SetHighColor(kNight2);
	canvas->FillRect(bounds);
	Divider(canvas, bounds, true, false);
	if (fModel.live) {
		_PaintLive(canvas, bounds);
		return;
	}

	const Pool& pool = fPool.GetPool();
	if (fPool.GetLimits().paused) {
		_PaintMessage(canvas, bounds, "PAUSED", "Taking a break",
			"Your machine isn't building. Resume to lend it to the pool again.",
			kGold);
		return;
	}
	if (fPool.WaitingForIdle()) {
		_PaintMessage(canvas, bounds, "WAITING",
			"Waiting for you to step away",
			"Awase builds when the computer has been idle for a while.",
			kMuted);
		return;
	}
	if (pool.YourPackage() >= 0) {
		_PaintBuilding(canvas, bounds);
		return;
	}
	int last = pool.LastYouBuilt();
	if (last >= 0) {
		_PaintWaiting(canvas, bounds, last);
		return;
	}
	_PaintMessage(canvas, bounds, "STARTING", "Looking for work",
		"The pool hands your machine a package as soon as one is ready.",
		kWashi2);
}


void
NowView::_PaintBuilding(BView* canvas, BRect bounds)
{
	const Pool& pool = fPool.GetPool();
	const Package& package = pool.Packages()[pool.YourPackage()];
	double progress = pool.YourProgress();
	BuildPhase phase = pool.YourPhase();

	float left = bounds.left + kPad;
	DrawLabel(canvas, BPoint(left, bounds.top + 34), "NOW BUILDING", kFire2);

	BFont name = Display(27);
	canvas->SetFont(&name);
	canvas->SetHighColor(kWashi);
	float ringSpace = 132;
	DrawTruncated(canvas, name, package.name.c_str(),
		BPoint(left, bounds.top + 70), bounds.Width() - kPad * 2 - ringSpace);

	// Version and architecture chips
	BFont mono = Mono(11);
	canvas->SetFont(&mono);
	float x = left;
	const std::string chips[2] = { package.version, package.arch };
	for (int i = 0; i < 2; i++) {
		float w = mono.StringWidth(chips[i].c_str()) + 16;
		BRect chip(x, bounds.top + 84, x + w, bounds.top + 104);
		canvas->SetHighColor(Alpha(i == 1 ? kFire : kWashi, 26));
		canvas->FillRoundRect(chip, 4, 4);
		canvas->SetHighColor(i == 1 ? kFire2 : kWashi2);
		canvas->DrawString(chips[i].c_str(), BPoint(x + 8, chip.top + 14));
		x += w + 8;
	}

	// The five steps of a build
	static const char* kSteps[] = { "Fetch", "Configure", "Compile",
		"Package", "Sign" };
	float stepsTop = bounds.top + 126;
	float stepsWidth = bounds.Width() - kPad * 2 - ringSpace;
	float stepWidth = stepsWidth / 5;
	BFont stepFont = Body(11);
	for (int i = 0; i < 5; i++) {
		BRect bar(left + i * stepWidth, stepsTop,
			left + (i + 1) * stepWidth - 4, stepsTop + 4);
		rgb_color color = i < (int)phase ? kFire
			: i == (int)phase ? kFire2 : Alpha(kWashi, 30);
		canvas->SetHighColor(color);
		canvas->FillRoundRect(bar, 2, 2);
		if (i == (int)phase) {
			float pulse = 0.5f + 0.5f * sinf((float)system_time() / 160000.0f);
			canvas->SetHighColor(Alpha(kFire2, (uint8)(60 * pulse)));
			canvas->FillRoundRect(bar.InsetByCopy(-2, -3), 3, 3);
		}
		canvas->SetFont(&stepFont);
		canvas->SetHighColor(i <= (int)phase ? kWashi2 : kMuted);
		canvas->DrawString(kSteps[i], BPoint(bar.left, stepsTop + 22));
	}

	char left_[64];
	snprintf(left_, sizeof(left_), "About %s left",
		FormatDuration(pool.YourSecondsLeft() * DemoPool::kTimeScale).c_str());
	BFont body = Body(13);
	canvas->SetFont(&body);
	canvas->SetHighColor(kWashi2);
	canvas->DrawString(left_, BPoint(left, bounds.top + 182));
	char cores[64];
	snprintf(cores, sizeof(cores), "on %d of %d cores",
		fPool.GetLimits().cores, fPool.GetLimits().maxCores);
	canvas->SetHighColor(kMuted);
	canvas->DrawString(cores, BPoint(left + body.StringWidth(left_) + 10,
		bounds.top + 182));

	// The ring
	BPoint center(bounds.right - kPad - 56, bounds.top + 104);
	float radius = 50;
	canvas->SetPenSize(7);
	canvas->SetLineMode(B_ROUND_CAP, B_ROUND_JOIN);
	canvas->SetHighColor(Alpha(kWashi, 24));
	canvas->StrokeEllipse(center, radius, radius);
	canvas->SetHighColor(kFire);
	canvas->StrokeArc(center, radius, radius, 90, -360 * (float)progress);
	canvas->SetLineMode(B_BUTT_CAP, B_MITER_JOIN);
	float angle = (90 - 360 * (float)progress) * (float)M_PI / 180;
	BPoint tip(center.x + radius * cosf(angle), center.y - radius * sinf(angle));
	canvas->SetHighColor(Alpha(kFire2, 90));
	canvas->FillEllipse(tip, 8, 8);

	char percent[16];
	snprintf(percent, sizeof(percent), "%d", (int)(progress * 100));
	BFont figure = Display(30);
	canvas->SetFont(&figure);
	canvas->SetHighColor(kWashi);
	float w = figure.StringWidth(percent);
	canvas->DrawString(percent, BPoint(center.x - w / 2 - 5, center.y + 10));
	canvas->SetFont(&mono);
	canvas->SetHighColor(kMuted);
	canvas->DrawString("%", BPoint(center.x + w / 2 - 3, center.y + 10));
	canvas->SetPenSize(1);
}


void
NowView::_PaintWaiting(BView* canvas, BRect bounds, int index)
{
	const Pool& pool = fPool.GetPool();
	const Package& package = pool.Packages()[index];
	const std::vector<Volunteer>& volunteers = pool.Volunteers();
	float left = bounds.left + kPad;
	bool sealed = package.state == kSealed;

	DrawLabel(canvas, BPoint(left, bounds.top + 34),
		sealed ? "ACCEPTED" : "WAITING FOR A MATCH", sealed ? kSeal : kFire2);
	BFont name = Display(27);
	canvas->SetFont(&name);
	canvas->SetHighColor(kWashi);
	DrawTruncated(canvas, name, package.name.c_str(),
		BPoint(left, bounds.top + 70), bounds.Width() - kPad * 2 - 80);

	// Your result and the other one, side by side
	std::string yours = FakeHash(package.name + package.version);
	std::string other;
	std::string otherName = "Second build";
	for (size_t r = 0; r < package.results.size(); r++) {
		if (!volunteers[package.results[r].volunteer].you) {
			other = yours;
			otherName = volunteers[package.results[r].volunteer].name;
		}
	}
	for (size_t c = 0; c < package.copies.size() && other.empty(); c++)
		otherName = volunteers[package.copies[c].volunteer].name;

	BFont mono = Mono(12);
	BFont label = Body(11);
	float top = bounds.top + 92;
	for (int row = 0; row < 2; row++) {
		float y = top + row * 34;
		BRect box(left, y, bounds.right - kPad, y + 28);
		canvas->SetHighColor(Alpha(kWashi, 10));
		canvas->FillRoundRect(box, 4, 4);
		canvas->SetHighColor(row == 0 || !other.empty()
			? Alpha(kSeal, sealed ? 150 : 60) : Alpha(kWashi, 30));
		canvas->StrokeRoundRect(box, 4, 4);
		canvas->SetFont(&label);
		canvas->SetHighColor(kMuted);
		canvas->DrawString(row == 0 ? "You" : otherName.c_str(),
			BPoint(box.left + 10, box.top + 18));
		canvas->SetFont(&mono);
		if (row == 0 || !other.empty()) {
			canvas->SetHighColor(kWashi2);
			canvas->DrawString(yours.c_str(),
				BPoint(box.left + 120, box.top + 19));
		} else {
			// Dots that breathe while the second build runs
			for (int d = 0; d < 3; d++) {
				float k = 0.5f + 0.5f * sinf((float)system_time() / 200000.0f
					- d * 0.8f);
				canvas->SetHighColor(Alpha(kWashi2, (uint8)(60 + 160 * k)));
				canvas->FillEllipse(BPoint(box.left + 124 + d * 12,
					box.top + 14), 2.5f, 2.5f);
			}
		}
	}

	BFont body = Body(13);
	canvas->SetFont(&body);
	canvas->SetHighColor(sealed ? kWashi : kWashi2);
	canvas->DrawString(sealed ? "Both builds match. The package is accepted."
		: "Another volunteer builds it too. It ships if both match.",
		BPoint(left, bounds.top + 186));

	if (sealed) {
		float age = (float)(pool.Now() - package.sealedAt);
		float pop = age < 0.35f ? 1.0f + 0.6f * (1 - age / 0.35f) : 1.0f;
		DrawSeal(canvas, BPoint(bounds.right - kPad - 30, bounds.top + 52), 22,
			pop);
	}
}


void
NowView::_PaintMessage(BView* canvas, BRect bounds, const char* label,
	const char* title, const char* text, rgb_color color)
{
	float left = bounds.left + kPad;
	DrawLabel(canvas, BPoint(left, bounds.top + 34), label, color);
	BFont name = Display(25);
	canvas->SetFont(&name);
	canvas->SetHighColor(kWashi);
	canvas->DrawString(title, BPoint(left, bounds.top + 72));
	BFont body = Body(13);
	canvas->SetFont(&body);
	canvas->SetHighColor(kWashi2);

	// Wrap the text at word boundaries
	BString rest(text);
	float width = bounds.Width() - kPad * 2;
	float y = bounds.top + 104;
	while (rest.Length() > 0) {
		int32 cut = rest.Length();
		while (cut > 0 && body.StringWidth(rest.String(), cut) > width) {
			int32 space = rest.FindLast(' ', cut - 1);
			cut = space > 0 ? space : 0;
		}
		if (cut == 0)
			cut = rest.Length();
		BString line;
		rest.CopyInto(line, 0, cut);
		canvas->DrawString(line.String(), BPoint(left, y));
		rest.Remove(0, cut);
		rest.Trim();
		y += 19;
	}
}


// #pragma mark - StatsView


StatsView::StatsView(const Model& model)
	:
	InkView("stats"),
	fModel(model),
	fPool(*model.demo)
{
	SetExplicitMinSize(BSize(360, 186));
	SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, 186));
}


void
StatsView::Paint(BView* canvas, BRect bounds)
{
	canvas->SetDrawingMode(B_OP_ALPHA);
	canvas->SetHighColor(kNight);
	canvas->FillRect(bounds);
	Divider(canvas, bounds, true, true);
	if (fModel.live) {
		_PaintLive(canvas, bounds);
		return;
	}

	const Stats& stats = fPool.GetPool().YourStats();
	float left = bounds.left + kPad;
	DrawLabel(canvas, BPoint(left, bounds.top + 32), "YOUR CONTRIBUTION",
		kFire2);
	BFont mono = Mono(10);
	canvas->SetFont(&mono);
	canvas->SetHighColor(kMuted);
	const char* demo = "demo figures";
	canvas->DrawString(demo,
		BPoint(bounds.right - kPad - mono.StringWidth(demo), bounds.top + 32));

	char figures[4][32];
	snprintf(figures[0], 32, "%d", stats.builds);
	snprintf(figures[1], 32, "%d", stats.accepted);
	snprintf(figures[2], 32, "%.1f", stats.buildSeconds / 3600.0);
	int finished = stats.accepted + stats.mismatched;
	if (finished > 0)
		snprintf(figures[3], 32, "%d%%", stats.accepted * 100 / finished);
	else
		snprintf(figures[3], 32, "\xe2\x80\x93");
	static const char* kNames[] = { "builds", "accepted", "build-hours",
		"matched" };

	float column = (bounds.Width() - kPad * 2) / 4;
	BFont figure = Display(30);
	BFont label = Body(11.5f);
	for (int i = 0; i < 4; i++) {
		float x = left + i * column;
		canvas->SetFont(&figure);
		canvas->SetHighColor(i == 1 && stats.accepted > 0 ? kWashi : kWashi);
		canvas->DrawString(figures[i], BPoint(x, bounds.top + 70));
		canvas->SetFont(&label);
		canvas->SetHighColor(kMuted);
		canvas->DrawString(kNames[i], BPoint(x, bounds.top + 88));
	}

	// Recent build time as an area
	const std::vector<double>& history = stats.history;
	BRect chart(left, bounds.top + 106, bounds.right - kPad,
		bounds.bottom - 26);
	double peak = 1;
	for (size_t i = 0; i < history.size(); i++)
		peak = std::max(peak, history[i]);
	canvas->SetHighColor(Alpha(kWashi, 14));
	for (int g = 0; g <= 2; g++) {
		float y = chart.bottom - chart.Height() * g / 2;
		canvas->StrokeLine(BPoint(chart.left, y), BPoint(chart.right, y));
	}
	const size_t slots = 24;
	if (!history.empty()) {
		std::vector<BPoint> points;
		size_t offset = slots - std::min(slots, history.size());
		for (size_t i = 0; i < history.size() && i < slots; i++) {
			float x = chart.left + chart.Width() * (offset + i) / (slots - 1);
			float y = chart.bottom - chart.Height()
				* (float)(history[i] / peak) * 0.92f;
			points.push_back(BPoint(x, y));
		}
		if (points.size() == 1)
			points.insert(points.begin(), BPoint(chart.left, points[0].y));
		std::vector<BPoint> area(points);
		area.push_back(BPoint(points.back().x, chart.bottom));
		area.push_back(BPoint(points.front().x, chart.bottom));
		canvas->SetHighColor(Alpha(kFire, 46));
		canvas->FillPolygon(&area[0], (int32)area.size());
		canvas->SetPenSize(1.8f);
		canvas->SetHighColor(kFire2);
		for (size_t i = 1; i < points.size(); i++)
			canvas->StrokeLine(points[i - 1], points[i]);
		canvas->SetPenSize(1);
		canvas->SetHighColor(Alpha(kFire, 70));
		canvas->FillEllipse(points.back(), 7, 7);
		canvas->SetHighColor(kFire);
		canvas->FillEllipse(points.back(), 3.5f, 3.5f);
	}
	canvas->SetFont(&mono);
	canvas->SetHighColor(kMuted);
	canvas->DrawString("recent build time",
		BPoint(chart.left, bounds.bottom - 9));
	const char* nowLabel = "now";
	canvas->DrawString(nowLabel,
		BPoint(chart.right - mono.StringWidth(nowLabel), bounds.bottom - 9));
}


// #pragma mark - ActivityView


ActivityView::ActivityView(const Model& model)
	:
	InkView("activity"),
	fModel(model),
	fPool(*model.demo)
{
	SetExplicitMinSize(BSize(360, 112));
}


void
ActivityView::Paint(BView* canvas, BRect bounds)
{
	canvas->SetDrawingMode(B_OP_ALPHA);
	canvas->SetHighColor(kNight);
	canvas->FillRect(bounds);
	Divider(canvas, bounds, true, true);

	if (fModel.live) {
		_PaintLive(canvas, bounds);
		return;
	}
	float left = bounds.left + kPad;
	DrawLabel(canvas, BPoint(left, bounds.top + 32), "ACTIVITY", kFire2);

	const Pool& pool = fPool.GetPool();
	const std::vector<Event>& events = pool.Events();
	BFont body = Body(12.5f);
	BFont mono = Mono(10.5f);
	float y = bounds.top + 62;
	float row = 27;
	for (int i = (int)events.size() - 1; i >= 0 && y < bounds.bottom - 8;
		i--) {
		const Event& event = events[i];
		if (event.kind == kEventRound)
			continue;
		BPoint mark(left + 8, y - 4);
		switch (event.kind) {
			case kEventSealed:
				DrawSeal(canvas, mark, 7);
				break;
			case kEventMismatch:
				canvas->SetPenSize(1.6f);
				canvas->SetHighColor(kGold);
				canvas->StrokeEllipse(mark, 6, 6);
				canvas->SetPenSize(1);
				break;
			case kEventYouStarted:
				canvas->SetHighColor(Alpha(kFire, 60));
				canvas->FillEllipse(mark, 6, 6);
				canvas->SetHighColor(kFire);
				canvas->FillEllipse(mark, 3, 3);
				break;
			default:
				canvas->SetHighColor(kWashi2);
				canvas->FillEllipse(mark, 3, 3);
				break;
		}

		char ago[24];
		int seconds = (int)((pool.Now() - event.time) * DemoPool::kTimeScale);
		if (seconds < 60)
			snprintf(ago, sizeof(ago), "now");
		else if (seconds < 3600)
			snprintf(ago, sizeof(ago), "%d min", seconds / 60);
		else
			snprintf(ago, sizeof(ago), "%d h", seconds / 3600);
		float agoWidth = mono.StringWidth(ago);

		float fade = std::max(0.35f, 1.0f - (bounds.top + 62 - y) / -320.0f);
		canvas->SetFont(&body);
		canvas->SetHighColor(Alpha(event.kind == kEventMismatch ? kGold : kWashi,
			(uint8)(235 * fade)));
		DrawTruncated(canvas, body, event.text.c_str(), BPoint(left + 26, y),
			bounds.right - kPad - agoWidth - 14 - (left + 26));
		canvas->SetFont(&mono);
		canvas->SetHighColor(kMuted);
		canvas->DrawString(ago, BPoint(bounds.right - kPad - agoWidth, y));
		y += row;
	}
	if (events.empty()) {
		canvas->SetFont(&body);
		canvas->SetHighColor(kMuted);
		canvas->DrawString("Nothing yet.", BPoint(left, y));
	}
}


// #pragma mark - Live: building for a coordinator


static const char*
AgoText(double seconds, char* buffer, size_t size)
{
	if (seconds < 60)
		snprintf(buffer, size, "now");
	else if (seconds < 3600)
		snprintf(buffer, size, "%d min", (int)(seconds / 60));
	else
		snprintf(buffer, size, "%d h", (int)(seconds / 3600));
	return buffer;
}


void
NowView::_PaintLive(BView* canvas, BRect bounds)
{
	const LiveSnapshot& live = fModel.snapshot;
	float left = bounds.left + kPad;

	if (!live.hasJob) {
		char buffer[160];
		switch (live.state) {
			case kLiveIdle:
				snprintf(buffer, sizeof(buffer), "The next check is in %d s. "
					"Jobs are queued on the coordinator.",
					(int)(live.nextPoll + 0.5));
				_PaintMessage(canvas, bounds, "WAITING FOR WORK",
					"Nothing queued", buffer, kWashi2);
				return;
			case kLiveConnecting:
				_PaintMessage(canvas, bounds, "CONNECTING", "Saying hello",
					live.message.c_str(), kWashi2);
				return;
			case kLiveError:
				_PaintMessage(canvas, bounds, "CAN'T BUILD RIGHT NOW",
					"Trying again soon", live.message.c_str(), kGold);
				return;
			case kLivePaused:
				_PaintMessage(canvas, bounds, "PAUSED", "Taking a break",
					"Your machine isn't building. Resume to lend it to the "
					"pool again.", kGold);
				return;
			case kLiveWaitingIdle:
				_PaintMessage(canvas, bounds, "WAITING",
					"Waiting for you to step away",
					"Awase builds when the computer has been idle for a "
					"while.", kMuted);
				return;
			default:
				_PaintMessage(canvas, bounds, "NOT CONNECTED",
					"No coordinator yet", "Choose Pool \xe2\x80\xba "
					"Coordinator\xe2\x80\xa6 and enter its address, this "
					"machine's client ID and secret.", kMuted);
				return;
		}
	}

	bool paused = live.state == kLivePaused;
	bool uploading = live.state == kLiveUploading
		|| live.state == kLiveReporting;
	DrawLabel(canvas, BPoint(left, bounds.top + 34),
		paused ? "BUILD PAUSED" : uploading ? "UPLOADING" : "NOW BUILDING",
		paused ? kGold : kFire2);

	float ringSpace = 132;
	BFont name = Display(27);
	canvas->SetFont(&name);
	canvas->SetHighColor(kWashi);
	DrawTruncated(canvas, name, live.job.port.c_str(),
		BPoint(left, bounds.top + 70), bounds.Width() - kPad * 2 - ringSpace);

	BFont mono = Mono(11);
	canvas->SetFont(&mono);
	char jobText[32];
	snprintf(jobText, sizeof(jobText), "job %lld", (long long)live.job.id);
	const std::string chips[3] = { live.job.version, live.job.platform,
		jobText };
	float x = left;
	for (int i = 0; i < 3; i++) {
		float w = mono.StringWidth(chips[i].c_str()) + 16;
		if (x + w > bounds.right - kPad - ringSpace)
			break;
		BRect chip(x, bounds.top + 84, x + w, bounds.top + 104);
		canvas->SetHighColor(Alpha(i == 1 ? kFire : kWashi, 26));
		canvas->FillRoundRect(chip, 4, 4);
		canvas->SetHighColor(i == 1 ? kFire2 : kWashi2);
		canvas->DrawString(chips[i].c_str(), BPoint(x + 8, chip.top + 14));
		x += w + 8;
	}

	// The steps; uploading replaces signing, which the coordinator does
	static const char* kSteps[] = { "Fetch", "Configure", "Compile",
		"Package", "Upload" };
	int phase = uploading ? 4 : std::min((int)live.phase, 3);
	float stepsTop = bounds.top + 126;
	float stepWidth = (bounds.Width() - kPad * 2 - ringSpace) / 5;
	BFont stepFont = Body(11);
	for (int i = 0; i < 5; i++) {
		BRect bar(left + i * stepWidth, stepsTop,
			left + (i + 1) * stepWidth - 4, stepsTop + 4);
		canvas->SetHighColor(i < phase ? kFire
			: i == phase ? (paused ? kGold : kFire2) : Alpha(kWashi, 30));
		canvas->FillRoundRect(bar, 2, 2);
		canvas->SetFont(&stepFont);
		canvas->SetHighColor(i <= phase ? kWashi2 : kMuted);
		canvas->DrawString(kSteps[i], BPoint(bar.left, stepsTop + 22));
	}

	// The last thing the build said
	BFont logFont = Mono(10.5f);
	canvas->SetFont(&logFont);
	canvas->SetHighColor(kMuted);
	std::string line = uploading ? live.message : live.lastLine;
	DrawTruncated(canvas, logFont, line.empty() ? "\xe2\x80\xa6" : line.c_str(),
		BPoint(left, bounds.top + 182), bounds.Width() - kPad * 2);

	// The ring: real progress while uploading; while building nobody knows
	// how far along it is, so it turns, and shows the time so far.
	BPoint center(bounds.right - kPad - 56, bounds.top + 96);
	float radius = 46;
	canvas->SetPenSize(7);
	canvas->SetLineMode(B_ROUND_CAP, B_ROUND_JOIN);
	canvas->SetHighColor(Alpha(kWashi, 24));
	canvas->StrokeEllipse(center, radius, radius);
	if (uploading && live.toUpload > 0) {
		canvas->SetHighColor(kFire);
		canvas->StrokeArc(center, radius, radius, 90,
			-360.0f * live.uploaded / live.toUpload);
	} else {
		float turn = paused ? 0 : (float)(system_time() % 2000000) / 2000000.0f;
		canvas->SetHighColor(paused ? kGold : kFire);
		canvas->StrokeArc(center, radius, radius, 90 - 360 * turn, -70);
	}
	canvas->SetLineMode(B_BUTT_CAP, B_MITER_JOIN);
	canvas->SetPenSize(1);

	char figure[24];
	if (uploading && live.toUpload > 0)
		snprintf(figure, sizeof(figure), "%d/%d", live.uploaded, live.toUpload);
	else {
		int seconds = (int)live.elapsed;
		if (seconds < 3600)
			snprintf(figure, sizeof(figure), "%d:%02d", seconds / 60,
				seconds % 60);
		else
			snprintf(figure, sizeof(figure), "%dh%02d", seconds / 3600,
				seconds / 60 % 60);
	}
	BFont big = Display(22);
	canvas->SetFont(&big);
	canvas->SetHighColor(kWashi);
	float w = big.StringWidth(figure);
	canvas->DrawString(figure, BPoint(center.x - w / 2, center.y + 8));
	canvas->SetFont(&logFont);
	canvas->SetHighColor(kMuted);
	const char* unit = uploading ? "packages" : "elapsed";
	w = logFont.StringWidth(unit);
	canvas->DrawString(unit, BPoint(center.x - w / 2, center.y + 24));
}


void
StatsView::_PaintLive(BView* canvas, BRect bounds)
{
	const LiveSnapshot& live = fModel.snapshot;
	float left = bounds.left + kPad;
	DrawLabel(canvas, BPoint(left, bounds.top + 32), "THIS MACHINE", kFire2);

	char figures[4][32];
	snprintf(figures[0], 32, "%d", live.builds);
	snprintf(figures[1], 32, "%d", live.succeeded);
	snprintf(figures[2], 32, "%.1f", live.buildSeconds / 3600.0);
	if (live.builds > 0)
		snprintf(figures[3], 32, "%d%%", live.succeeded * 100 / live.builds);
	else
		snprintf(figures[3], 32, "\xe2\x80\x93");
	static const char* kNames[] = { "builds", "built", "build-hours",
		"succeeded" };

	float column = (bounds.Width() - kPad * 2) / 4;
	BFont figure = Display(30);
	BFont label = Body(11.5f);
	for (int i = 0; i < 4; i++) {
		float x = left + i * column;
		canvas->SetFont(&figure);
		canvas->SetHighColor(kWashi);
		canvas->DrawString(figures[i], BPoint(x, bounds.top + 70));
		canvas->SetFont(&label);
		canvas->SetHighColor(kMuted);
		canvas->DrawString(kNames[i], BPoint(x, bounds.top + 88));
	}

	const std::vector<double>& history = live.history;
	BRect chart(left, bounds.top + 106, bounds.right - kPad, bounds.bottom - 26);
	double peak = 1;
	for (size_t i = 0; i < history.size(); i++)
		peak = std::max(peak, history[i]);
	canvas->SetHighColor(Alpha(kWashi, 14));
	for (int g = 0; g <= 2; g++) {
		float y = chart.bottom - chart.Height() * g / 2;
		canvas->StrokeLine(BPoint(chart.left, y), BPoint(chart.right, y));
	}
	const size_t slots = 24;
	std::vector<BPoint> points;
	size_t offset = slots - std::min(slots, history.size());
	for (size_t i = 0; i < history.size() && i < slots; i++) {
		points.push_back(BPoint(
			chart.left + chart.Width() * (offset + i) / (slots - 1),
			chart.bottom - chart.Height() * (float)(history[i] / peak) * 0.92f));
	}
	if (points.size() == 1)
		points.insert(points.begin(), BPoint(chart.left, points[0].y));
	if (points.size() >= 2) {
		std::vector<BPoint> area(points);
		area.push_back(BPoint(points.back().x, chart.bottom));
		area.push_back(BPoint(points.front().x, chart.bottom));
		canvas->SetHighColor(Alpha(kFire, 46));
		canvas->FillPolygon(&area[0], (int32)area.size());
		canvas->SetPenSize(1.8f);
		canvas->SetHighColor(kFire2);
		for (size_t i = 1; i < points.size(); i++)
			canvas->StrokeLine(points[i - 1], points[i]);
		canvas->SetPenSize(1);
		canvas->SetHighColor(kFire);
		canvas->FillEllipse(points.back(), 3.5f, 3.5f);
	}
	BFont mono = Mono(10);
	canvas->SetFont(&mono);
	canvas->SetHighColor(kMuted);
	canvas->DrawString("build time, last two hours",
		BPoint(chart.left, bounds.bottom - 9));
	const char* nowLabel = "now";
	canvas->DrawString(nowLabel,
		BPoint(chart.right - mono.StringWidth(nowLabel), bounds.bottom - 9));
}


void
ActivityView::_PaintLive(BView* canvas, BRect bounds)
{
	const LiveSnapshot& live = fModel.snapshot;
	float left = bounds.left + kPad;
	DrawLabel(canvas, BPoint(left, bounds.top + 32), "ACTIVITY", kFire2);

	BFont body = Body(12.5f);
	BFont mono = Mono(10.5f);
	float y = bounds.top + 62;
	for (int i = (int)live.events.size() - 1; i >= 0 && y < bounds.bottom - 8;
		i--) {
		const LiveEvent& event = live.events[i];
		BPoint mark(left + 8, y - 4);
		rgb_color text = kWashi;
		switch (event.kind) {
			case kLiveSucceeded:
				DrawSeal(canvas, mark, 7);
				break;
			case kLiveFailed:
			case kLiveProblem:
				canvas->SetPenSize(1.6f);
				canvas->SetHighColor(kGold);
				canvas->StrokeEllipse(mark, 6, 6);
				canvas->SetPenSize(1);
				text = kGold;
				break;
			case kLiveStarted:
				canvas->SetHighColor(Alpha(kFire, 60));
				canvas->FillEllipse(mark, 6, 6);
				canvas->SetHighColor(kFire);
				canvas->FillEllipse(mark, 3, 3);
				break;
			default:
				canvas->SetHighColor(kWashi2);
				canvas->FillEllipse(mark, 3, 3);
				break;
		}
		char ago[24];
		AgoText(live.now - event.time, ago, sizeof(ago));
		float agoWidth = mono.StringWidth(ago);
		canvas->SetFont(&body);
		canvas->SetHighColor(text);
		DrawTruncated(canvas, body, event.text.c_str(), BPoint(left + 26, y),
			bounds.right - kPad - agoWidth - 14 - (left + 26));
		canvas->SetFont(&mono);
		canvas->SetHighColor(kMuted);
		canvas->DrawString(ago, BPoint(bounds.right - kPad - agoWidth, y));
		y += 27;
	}
	if (live.events.empty()) {
		canvas->SetFont(&body);
		canvas->SetHighColor(kMuted);
		canvas->DrawString("Nothing yet.", BPoint(left, y));
	}
}
