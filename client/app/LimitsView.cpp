/*
 * Copyright 2026, Ian Dumych, idumych@gmail.com.
 * SPDX-License-Identifier: MIT
 */


#include "Views.h"

#include "DemoPool.h"
#include "VmWindow.h"

#include <Message.h>
#include <String.h>
#include <Window.h>

#include <algorithm>
#include <math.h>
#include <stdio.h>


using namespace awase;
using namespace ink;


static const float kPad = 26;
static const int kMinTemperature = 60;
static const int kMaxTemperature = 95;


LimitsView::LimitsView(const Model& model)
	:
	InkView("limits"),
	fModel(model),
	fPool(*model.demo),
	fDragging(kNone),
	fHover(kNone),
	fVmHover(false)
{
	SetExplicitMinSize(BSize(600, 84));
	SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, 84));
}


BRect
LimitsView::_Track(Control control) const
{
	BRect bounds = Bounds();
	// Room on the right for the x86 VM's control and the When segments
	float width = std::min(240.0f, (bounds.Width() - kPad * 2 - 600) / 2);
	float left = bounds.left + kPad
		+ (control == kTemperature ? width + 56 : 0);
	return BRect(left, bounds.top + 52, left + width, bounds.top + 56);
}


BRect
LimitsView::_Segments() const
{
	BRect bounds = Bounds();
	return BRect(bounds.right - kPad - 232, bounds.top + 34,
		bounds.right - kPad, bounds.top + 66);
}


BRect
LimitsView::_VmButton() const
{
	BRect temperature = _Track(kTemperature);
	float left = temperature.right + 44;
	return BRect(left, Bounds().top + 34, std::min(left + 196,
		_Segments().left - 28), Bounds().top + 66);
}


void
LimitsView::MouseDown(BPoint where)
{
	SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);
	if (_VmButton().Contains(where)) {
		Window()->PostMessage(kMsgShowVms);
		return;
	}
	const Limits& limits = fPool.GetLimits();
	BRect segments = _Segments();
	if (segments.Contains(where)) {
		bool idle = where.x < segments.left + segments.Width() / 2;
		_Send(limits.cores, limits.maxTemperature, idle);
		return;
	}
	for (int c = kCores; c <= kTemperature; c++) {
		if (_Track((Control)c).InsetByCopy(-8, -14).Contains(where)) {
			fDragging = (Control)c;
			_DragTo(where);
			return;
		}
	}
}


void
LimitsView::MouseMoved(BPoint where, uint32 transit, const BMessage* drag)
{
	if (fDragging != kNone) {
		_DragTo(where);
		return;
	}
	Control hover = kNone;
	if (transit != B_EXITED_VIEW) {
		if (_Segments().Contains(where))
			hover = kWhen;
		for (int c = kCores; c <= kTemperature; c++) {
			if (_Track((Control)c).InsetByCopy(-8, -14).Contains(where))
				hover = (Control)c;
		}
	}
	bool vmHover = transit != B_EXITED_VIEW && _VmButton().Contains(where);
	if (hover != fHover || vmHover != fVmHover) {
		fHover = hover;
		fVmHover = vmHover;
		Invalidate();
	}
	InkView::MouseMoved(where, transit, drag);
}


void
LimitsView::MouseUp(BPoint where)
{
	fDragging = kNone;
	(void)where;
}


void
LimitsView::_DragTo(BPoint where)
{
	BRect track = _Track(fDragging);
	float fraction = (where.x - track.left) / track.Width();
	if (fraction < 0)
		fraction = 0;
	if (fraction > 1)
		fraction = 1;
	const Limits& limits = fPool.GetLimits();
	if (fDragging == kCores) {
		int cores = 1 + (int)roundf(fraction * (limits.maxCores - 1));
		if (cores != limits.cores)
			_Send(cores, limits.maxTemperature, limits.idleOnly);
	} else if (fDragging == kTemperature) {
		int temperature = kMinTemperature
			+ (int)roundf(fraction * (kMaxTemperature - kMinTemperature));
		if (temperature != limits.maxTemperature)
			_Send(limits.cores, temperature, limits.idleOnly);
	}
}


void
LimitsView::_Send(int cores, int temperature, bool idle)
{
	BMessage message(kMsgLimits);
	message.AddInt32("cores", cores);
	message.AddInt32("temperature", temperature);
	message.AddBool("idle", idle);
	Window()->PostMessage(&message);
}


void
LimitsView::_PaintSlider(BView* canvas, Control control, const char* label,
	const char* value, float fraction)
{
	BRect track = _Track(control);
	bool active = fHover == control || fDragging == control;

	DrawLabel(canvas, BPoint(track.left, track.top - 22), label, kMuted);
	BFont figure = Body(13, true);
	canvas->SetFont(&figure);
	canvas->SetHighColor(kWashi);
	canvas->DrawString(value,
		BPoint(track.right - figure.StringWidth(value), track.top - 20));

	canvas->SetHighColor(Alpha(kWashi, 30));
	canvas->FillRoundRect(track, 2, 2);
	BRect filled(track);
	filled.right = track.left + track.Width() * fraction;
	canvas->SetHighColor(kFire);
	canvas->FillRoundRect(filled, 2, 2);

	BPoint knob(filled.right, track.top + track.Height() / 2);
	if (active) {
		canvas->SetHighColor(Alpha(kFire, 60));
		canvas->FillEllipse(knob, 13, 13);
	}
	canvas->SetHighColor(kWashi);
	canvas->FillEllipse(knob, 7.5f, 7.5f);
	canvas->SetHighColor(kFire);
	canvas->FillEllipse(knob, 3, 3);
}


void
LimitsView::Paint(BView* canvas, BRect bounds)
{
	canvas->SetDrawingMode(B_OP_ALPHA);
	canvas->SetHighColor(kNight2);
	canvas->FillRect(bounds);
	canvas->SetHighColor(Alpha(kWashi, 24));
	canvas->StrokeLine(bounds.LeftTop(), bounds.RightTop());

	const Limits& limits = fPool.GetLimits();
	char cores[32];
	snprintf(cores, sizeof(cores), "%d of %d", limits.cores, limits.maxCores);
	_PaintSlider(canvas, kCores, "CORES", cores,
		limits.maxCores > 1
			? (float)(limits.cores - 1) / (limits.maxCores - 1) : 1);

	char temperature[32];
	snprintf(temperature, sizeof(temperature), "%d \xc2\xb0""C",
		limits.maxTemperature);
	_PaintSlider(canvas, kTemperature, "PAUSE ABOVE", temperature,
		(float)(limits.maxTemperature - kMinTemperature)
			/ (kMaxTemperature - kMinTemperature));
	// What the sensor says, so a machine without one doesn't look protected.
	if (fModel.live) {
		const awase::LiveSnapshot& live = fModel.snapshot;
		char reading[48];
		if (live.temperature >= 0)
			snprintf(reading, sizeof(reading), "now %d\xc2\xb0%s",
				live.temperature, live.tooHot ? " \xc2\xb7 paused" : "");
		else
			snprintf(reading, sizeof(reading), "no sensor");
		BRect track = _Track(kTemperature);
		BFont small = Mono(10);
		canvas->SetFont(&small);
		canvas->SetHighColor(live.tooHot ? kGold : kMuted);
		canvas->DrawString(reading, BPoint(track.left, track.bottom + 17));
	}

	// The x86 VM: its state, and the way to its window
	BRect vm = _VmButton();
	const char* vmLabel = fModel.vmReady ? "X86 VM" : "BUILD X86 TOO";
	DrawLabel(canvas, BPoint(vm.left, vm.top - 8), vmLabel, kMuted);
	std::string state;
	rgb_color dot = kMuted;
	if (fModel.vmSettingUp) {
		char buffer[32];
		snprintf(buffer, sizeof(buffer), "Setting up %d%%",
			(int)(fModel.vmSetupFraction * 100));
		state = buffer;
		dot = kFire;
	} else if (!fModel.vmReady)
		state = "Set up\xe2\x80\xa6";
	else if (!fModel.vmOn)
		state = "Off";
	else if (fModel.vmState == kVmRunning) {
		state = fModel.vmFrozen ? "Paused" : "Running";
		dot = fModel.vmFrozen ? kMuted : kGold;
	} else if (fModel.vmState == kVmStarting) {
		state = "Starting";
		dot = kGold;
	} else if (fModel.vmState == kVmFailed) {
		state = "Stopped";
		dot = kGold;
	} else
		state = fModel.live ? "Off" : "Off in the demo";
	canvas->SetHighColor(Alpha(kGold, fVmHover ? 34 : 16));
	canvas->FillRoundRect(vm, 6, 6);
	canvas->SetHighColor(Alpha(kGold, fVmHover ? 150 : 90));
	canvas->StrokeRoundRect(vm, 6, 6);
	BPoint dotAt(vm.left + 16, vm.top + vm.Height() / 2);
	if (dot == kGold || dot == kFire) {
		DrawGlow(canvas, dotAt, 9, dot, 1.0f);
		canvas->SetHighColor(dot);
	} else
		canvas->SetHighColor(Alpha(dot, 200));
	canvas->FillEllipse(dotAt, 4, 4);
	BFont vmFont = Body(12.5f, true);
	canvas->SetFont(&vmFont);
	canvas->SetHighColor(kWashi);
	BString vmText(state.c_str());
	vmFont.TruncateString(&vmText, B_TRUNCATE_END, vm.Width() - 40);
	canvas->DrawString(vmText.String(), BPoint(vm.left + 30, vm.top + 21));

	// When: two segments
	BRect segments = _Segments();
	DrawLabel(canvas, BPoint(segments.left, segments.top - 8), "BUILD", kMuted);
	canvas->SetHighColor(Alpha(kWashi, fHover == kWhen ? 60 : 36));
	canvas->StrokeRoundRect(segments, 5, 5);
	BRect half(segments);
	half.right = segments.left + segments.Width() / 2;
	BRect active = limits.idleOnly ? half
		: BRect(half.right, segments.top, segments.right, segments.bottom);
	canvas->SetHighColor(Alpha(kFire, 230));
	canvas->FillRoundRect(active.InsetByCopy(3, 3), 3, 3);
	BFont body = Body(12.5f, true);
	canvas->SetFont(&body);
	const char* names[2] = { "When idle", "Always" };
	for (int i = 0; i < 2; i++) {
		BRect r = i == 0 ? half
			: BRect(half.right, segments.top, segments.right, segments.bottom);
		bool on = (i == 0) == limits.idleOnly;
		canvas->SetHighColor(on ? make_color(22, 10, 2) : kWashi2);
		float w = body.StringWidth(names[i]);
		canvas->DrawString(names[i],
			BPoint(r.left + (r.Width() - w) / 2, r.top + 21));
	}
}
