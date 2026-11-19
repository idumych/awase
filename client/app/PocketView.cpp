/*
 * Copyright 2026, Ian Dumych, idumych@gmail.com.
 * SPDX-License-Identifier: MIT
 */


#include "Views.h"

#include "DemoPool.h"

#include <String.h>
#include <Window.h>

#include <algorithm>
#include <math.h>
#include <stdio.h>


using namespace awase;
using namespace ink;


static const float kPad = 22;
static const float kWidth = 420;
static const float kHeight = 434;
static const float kRingRadius = 84;
static const float kRingTop = 72;		// the ring's square, 210 tall
static const float kFiguresTop = 294;
static const float kButtonsTop = 372;


// What the ring shows: how far along a build is, or that something is
// going on when nobody knows how far.
struct Ring {
	std::string	label;		// small capitals above the name
	rgb_color	labelColor;
	std::string	title;
	std::string	line;
	float		progress;	// 0..1, or < 0 for none
	bool		turning;	// a short arc that goes round
	bool		paused;
	std::string	vm;			// the x86 VM's build, under the line
};


static std::string
Upper(const char* text)
{
	std::string upper(text);
	for (size_t i = 0; i < upper.size(); i++)
		upper[i] = toupper((unsigned char)upper[i]);
	return upper;
}


static Ring
DemoRing(const DemoPool& demo)
{
	static const char* kSteps[] = { "Fetch", "Configure", "Compile",
		"Package", "Sign" };
	Ring ring = { "", kFire2, "", "", -1, false, false, "" };
	const Pool& pool = demo.GetPool();
	if (demo.GetLimits().paused) {
		ring.label = "PAUSED";
		ring.labelColor = kGold;
		ring.title = "Taking a break";
		ring.paused = true;
		return ring;
	}
	if (demo.WaitingForIdle()) {
		ring.label = "WAITING";
		ring.labelColor = kMuted;
		ring.title = "Step away";
		ring.line = "builds when idle";
		return ring;
	}
	int package = pool.YourPackage();
	if (package >= 0) {
		ring.label = Upper(kSteps[std::min((int)pool.YourPhase(), 4)]);
		ring.title = pool.Packages()[package].name;
		ring.progress = (float)pool.YourProgress();
		char line[64];
		snprintf(line, sizeof(line), "%d%% \xc2\xb7 %s left",
			(int)(ring.progress * 100), FormatDuration(pool.YourSecondsLeft()
				* DemoPool::kTimeScale).c_str());
		ring.line = line;
		return ring;
	}
	int last = pool.LastYouBuilt();
	if (last >= 0) {
		const Package& built = pool.Packages()[last];
		bool sealed = built.state == kSealed;
		ring.label = sealed ? "ACCEPTED" : "WAITING FOR A MATCH";
		ring.labelColor = sealed ? kSeal : kFire2;
		ring.title = built.name;
		ring.line = sealed ? "both builds match" : "built, to be checked";
		ring.progress = sealed ? 1 : -1;
		return ring;
	}
	ring.label = "STARTING";
	ring.labelColor = kWashi2;
	ring.title = "Looking for work";
	return ring;
}


static Ring
LiveRing(const Model& model)
{
	static const char* kPhases[] = { "Fetch", "Configure", "Compile",
		"Package" };
	const LiveSnapshot& live = model.snapshot;
	Ring ring = { "", kFire2, "", "", -1, false, false, "" };
	if (model.TwoMachines() && model.vmSnapshot.hasJob)
		ring.vm = "x86 VM \xc2\xb7 " + model.vmSnapshot.job.port;
	if (!live.hasJob) {
		ring.labelColor = kWashi2;
		switch (live.state) {
			case kLiveIdle:
				ring.label = "WAITING FOR WORK";
				ring.title = "Nothing queued";
				break;
			case kLiveConnecting:
				ring.label = "CONNECTING";
				ring.title = "Saying hello";
				break;
			case kLiveError:
				ring.label = "CAN'T BUILD RIGHT NOW";
				ring.labelColor = kGold;
				ring.title = "Trying again soon";
				break;
			case kLivePaused:
				ring.label = "PAUSED";
				ring.labelColor = kGold;
				ring.title = "Taking a break";
				ring.paused = true;
				break;
			case kLiveWaitingIdle:
				ring.label = "WAITING";
				ring.labelColor = kMuted;
				ring.title = "Step away";
				ring.line = "builds when idle";
				break;
			default:
				ring.label = "NOT CONNECTED";
				ring.labelColor = kMuted;
				ring.title = "No coordinator";
				ring.line = "Full view \xe2\x80\xba Pool \xe2\x80\xba Coordinator";
				break;
		}
		return ring;
	}

	bool uploading = live.state == kLiveUploading
		|| live.state == kLiveReporting;
	ring.paused = live.state == kLivePaused;
	ring.title = live.job.port;
	if (ring.paused) {
		ring.label = "BUILD PAUSED";
		ring.labelColor = kGold;
	} else if (uploading)
		ring.label = "UPLOADING";
	else
		ring.label = Upper(kPhases[std::min((int)live.phase, 3)]);

	char line[64];
	if (uploading && live.toUpload > 0) {
		ring.progress = (float)live.uploaded / live.toUpload;
		snprintf(line, sizeof(line), "%d of %d packages", live.uploaded,
			live.toUpload);
	} else {
		ring.turning = true;
		int seconds = (int)live.elapsed;
		if (seconds < 3600)
			snprintf(line, sizeof(line), "%d:%02d elapsed", seconds / 60,
				seconds % 60);
		else
			snprintf(line, sizeof(line), "%dh%02d elapsed", seconds / 3600,
				seconds / 60 % 60);
	}
	ring.line = line;
	return ring;
}


PocketView::PocketView(const Model& model)
	:
	InkView("pocket"),
	fModel(model),
	fPool(*model.demo),
	fHover(kNoButton)
{
	SetExplicitMinSize(BSize(kWidth, kHeight));
	SetExplicitMaxSize(BSize(kWidth, kHeight));
}


BRect
PocketView::_ButtonFrame(Button button) const
{
	BRect bounds = Bounds();
	float half = (bounds.Width() - kPad * 2 - 8) / 2;
	float left = bounds.left + kPad + (button == kPause ? half + 8 : 0);
	return BRect(left, bounds.top + kButtonsTop, left + half,
		bounds.top + kButtonsTop + 44);
}


void
PocketView::MouseDown(BPoint where)
{
	if (_ButtonFrame(kFullView).Contains(where))
		Window()->PostMessage(kMsgToggleMini);
	else if (_ButtonFrame(kPause).Contains(where))
		Window()->PostMessage(kMsgTogglePause);
}


void
PocketView::MouseMoved(BPoint where, uint32 transit, const BMessage* drag)
{
	Button hover = kNoButton;
	if (transit != B_EXITED_VIEW && transit != B_OUTSIDE_VIEW) {
		if (_ButtonFrame(kFullView).Contains(where))
			hover = kFullView;
		else if (_ButtonFrame(kPause).Contains(where))
			hover = kPause;
	}
	if (hover != fHover) {
		fHover = hover;
		Invalidate();
	}
	InkView::MouseMoved(where, transit, drag);
}


void
PocketView::Paint(BView* canvas, BRect bounds)
{
	canvas->SetDrawingMode(B_OP_ALPHA);
	float left = bounds.left + kPad, right = bounds.right - kPad;

	// The seal, the name, and where it builds
	BPoint sealAt(left + 17, bounds.top + kPad + 17);
	DrawGlow(canvas, sealAt, 30, kFire, 0.9f);
	DrawSeal(canvas, sealAt, 17);
	BFont name = Display(20);
	canvas->SetFont(&name);
	canvas->SetHighColor(kWashi);
	float x = left + 46;
	canvas->DrawString("Awase", BPoint(x, bounds.top + 38));
	std::string where = "demo pool";
	if (fModel.live) {
		std::string host = fModel.snapshot.server;
		size_t scheme = host.find("://");
		if (scheme != std::string::npos)
			host.erase(0, scheme + 3);
		while (!host.empty() && host[host.size() - 1] == '/')
			host.erase(host.size() - 1);
		where = host.empty() ? "not connected" : host;
		if (!fModel.snapshot.platform.empty())
			where = fModel.snapshot.platform + " \xc2\xb7 " + where;
	}

	// What it's doing, as a pill at the right
	rgb_color dot;
	BString status(StatusText(fModel, dot));
	BFont bold = Body(12, true);
	float nameRight = x + name.StringWidth("Awase");
	bold.TruncateString(&status, B_TRUNCATE_END,
		std::max(40.0f, right - nameRight - 16 - 32));
	BRect pill(right - bold.StringWidth(status.String()) - 32,
		bounds.top + 26, right, bounds.top + 52);
	canvas->SetHighColor(Alpha(dot, 30));
	canvas->FillRoundRect(pill, 13, 13);
	canvas->SetHighColor(Alpha(dot, 120));
	canvas->StrokeRoundRect(pill, 13, 13);
	BPoint dotAt(pill.left + 14, pill.top + 13);
	if (dot == kFire)
		DrawGlow(canvas, dotAt, 9, dot, 1.0f);
	canvas->SetHighColor(dot);
	canvas->FillEllipse(dotAt, 3.5f, 3.5f);
	canvas->SetFont(&bold);
	canvas->SetHighColor(dot == kMuted ? kWashi2 : Mix(dot, kWashi, 0.3f));
	canvas->DrawString(status.String(), BPoint(pill.left + 24,
		pill.top + 17));

	BFont mono = Mono(11);
	BString whereText(where.c_str());
	mono.TruncateString(&whereText, B_TRUNCATE_MIDDLE,
		std::max(40.0f, pill.left - 12 - x));
	canvas->SetFont(&mono);
	canvas->SetHighColor(kMuted);
	canvas->DrawString(whereText.String(), BPoint(x, bounds.top + 54));

	_PaintRing(canvas, BPoint((bounds.left + bounds.right) / 2,
		bounds.top + kRingTop + 105));

	// Three figures between two rules
	float top = bounds.top + kFiguresTop;
	canvas->SetHighColor(Alpha(kWashi, 24));
	canvas->StrokeLine(BPoint(left, top), BPoint(right, top));
	canvas->StrokeLine(BPoint(left, top + 62), BPoint(right, top + 62));
	char figures[3][32];
	const char* names[3];
	const Limits& limits = fPool.GetLimits();
	if (fModel.live) {
		const LiveSnapshot& live = fModel.snapshot;
		snprintf(figures[0], 32, "%d", live.succeeded);
		names[0] = "built";
		if (live.builds > 0)
			snprintf(figures[1], 32, "%d%%", live.succeeded * 100 / live.builds);
		else
			snprintf(figures[1], 32, "\xe2\x80\x93");
		names[1] = "succeeded";
	} else {
		const Stats& stats = fPool.GetPool().YourStats();
		snprintf(figures[0], 32, "%d", stats.accepted);
		names[0] = "sealed";
		int finished = stats.accepted + stats.mismatched;
		if (finished > 0)
			snprintf(figures[1], 32, "%d%%", stats.accepted * 100 / finished);
		else
			snprintf(figures[1], 32, "\xe2\x80\x93");
		names[1] = "matched";
	}
	snprintf(figures[2], 32, "%d/%d", limits.cores, limits.maxCores);
	names[2] = "cores";
	BFont figure = Display(22);
	BFont label = Body(11);
	float column = (right - left) / 3;
	for (int i = 0; i < 3; i++) {
		float center = left + column * (i + 0.5f);
		canvas->SetFont(&figure);
		canvas->SetHighColor(kWashi);
		canvas->DrawString(figures[i], BPoint(center
			- figure.StringWidth(figures[i]) / 2, top + 33));
		canvas->SetFont(&label);
		canvas->SetHighColor(kMuted);
		canvas->DrawString(names[i], BPoint(center
			- label.StringWidth(names[i]) / 2, top + 50));
	}

	_PaintButton(canvas, kFullView, "Full view", false);
	_PaintButton(canvas, kPause, limits.paused ? "Resume" : "Pause", true);
}


void
PocketView::_PaintRing(BView* canvas, BPoint center)
{
	Ring ring = fModel.live ? LiveRing(fModel) : DemoRing(fPool);
	float radius = kRingRadius;

	DrawGlow(canvas, center, 150, kFire, 0.25f);
	canvas->SetPenSize(12);
	canvas->SetHighColor(kIndigo);
	canvas->StrokeEllipse(center, radius, radius);
	canvas->SetPenSize(1);
	canvas->SetHighColor(Alpha(kWashi, 16));
	canvas->StrokeEllipse(center, radius - 20, radius - 20);

	rgb_color tipColor = kGold;
	float tipAngle = 0;
	if (ring.progress > 0.002f) {
		StrokeGradientArc(canvas, center, radius, 90,
			-360 * std::min(1.0f, ring.progress), kSeal, kGold, 12);
		tipAngle = 90 - 360 * std::min(1.0f, ring.progress);
	} else if (ring.turning) {
		float turn = ring.paused ? 0
			: (float)(system_time() % 2400000) / 2400000.0f;
		float start = 90 - 360 * turn;
		StrokeGradientArc(canvas, center, radius, start, -100,
			ring.paused ? kMuted : kSeal, ring.paused ? kGold : kFire, 12);
		tipAngle = start - 100;
		tipColor = ring.paused ? kGold : kFire2;
	}
	if ((ring.progress > 0.002f || ring.turning) && ring.progress < 0.995f) {
		float a = tipAngle * (float)M_PI / 180;
		BPoint tip(center.x + radius * cosf(a), center.y - radius * sinf(a));
		float flicker = 0.85f + 0.15f * sinf((float)system_time() / 90000.0f);
		DrawGlow(canvas, tip, 14, tipColor, 1.5f * flicker);
		canvas->SetHighColor(255, 246, 220);
		canvas->FillEllipse(tip, 3.5f, 3.5f);
	}

	// The words inside the ring: the name as large as fits, smaller before
	// it's cut short
	float inner = (radius - 24) * 2;
	float labelWidth = LabelWidth(canvas, ring.label.c_str());
	DrawLabel(canvas, BPoint(center.x - labelWidth / 2, center.y - 24),
		ring.label.c_str(), ring.labelColor);
	float titleRoom = (radius - 10) * 2;
	BFont title = Display(26);
	for (float size = 26; size > 17
		&& title.StringWidth(ring.title.c_str()) > titleRoom; size -= 2)
		title.SetSize(size - 2);
	BString titleText(ring.title.c_str());
	title.TruncateString(&titleText, B_TRUNCATE_END, titleRoom);
	canvas->SetFont(&title);
	canvas->SetHighColor(kWashi);
	canvas->DrawString(titleText.String(), BPoint(center.x
		- title.StringWidth(titleText.String()) / 2, center.y + 8));
	BFont mono = Mono(12);
	canvas->SetFont(&mono);
	if (!ring.line.empty()) {
		BString line(ring.line.c_str());
		mono.TruncateString(&line, B_TRUNCATE_END, inner);
		canvas->SetHighColor(kWashi2);
		canvas->DrawString(line.String(), BPoint(center.x
			- mono.StringWidth(line.String()) / 2, center.y + 30));
	}
	if (!ring.vm.empty()) {
		BFont small = Mono(10.5f);
		BString vm(ring.vm.c_str());
		small.TruncateString(&vm, B_TRUNCATE_END, inner - 16);
		canvas->SetFont(&small);
		canvas->SetHighColor(kGold);
		canvas->DrawString(vm.String(), BPoint(center.x
			- small.StringWidth(vm.String()) / 2, center.y + 50));
	}
}


void
PocketView::_PaintButton(BView* canvas, Button button, const char* label,
	bool filled)
{
	BRect frame = _ButtonFrame(button);
	bool hover = fHover == button;
	if (filled) {
		canvas->SetHighColor(hover ? kFire2 : kFire);
		canvas->FillRoundRect(frame, 6, 6);
	} else {
		canvas->SetHighColor(Alpha(kWashi, hover ? 26 : 0));
		canvas->FillRoundRect(frame, 6, 6);
		canvas->SetHighColor(Alpha(kWashi, hover ? 170 : 80));
		canvas->StrokeRoundRect(frame, 6, 6);
	}
	BFont font = Body(14, true);
	canvas->SetFont(&font);
	canvas->SetHighColor(filled ? make_color(22, 10, 2) : kWashi);
	canvas->DrawString(label, BPoint(frame.left + (frame.Width()
		- font.StringWidth(label)) / 2, frame.top + 27));
}
