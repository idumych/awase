/*
 * Copyright 2026, Ian Dumych.
 * SPDX-License-Identifier: MIT
 */


#include "VmWindow.h"

#include <Messenger.h>
#include <String.h>
#include <TextControl.h>
#include <TextView.h>

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include <algorithm>


using namespace awase;
using namespace ink;


static const float kPad = 26;
static const float kHeaderHeight = 118;
static const float kFooterHeight = 62;
static const float kWidth = 724;
static const float kHeight = 584;

enum {
	kIconDone,
	kIconWarn,
	kIconFail,
	kIconWaiting,
	kIconRunning,
	kIconSkipped,
};

enum {
	kMsgDoCancel = 'dcnl',		// close the window
	kMsgDoSetup = 'dset',
	kMsgDoCancelSetup = 'dcst',
	kMsgDoHide = 'dhid',
	kMsgDoCheck = 'dchk',
	kMsgDoStart = 'dsta',
	kMsgDoStop = 'dsto',
	kMsgDoRetry = 'dret',
	kMsgFieldChanged = 'fchg',
};


// Draws text wrapped at word boundaries; returns the baseline after it.
static float
Wrap(BView* canvas, const BFont& font, const std::string& text, float x,
	float y, float width, float lineHeight)
{
	canvas->SetFont(&font);
	BString rest(text.c_str());
	while (rest.Length() > 0) {
		int32 cut = rest.Length();
		while (cut > 0 && font.StringWidth(rest.String(), cut) > width) {
			int32 space = rest.FindLast(' ', cut - 1);
			cut = space > 0 ? space : 0;
		}
		if (cut == 0)
			cut = rest.Length();
		BString line;
		rest.CopyInto(line, 0, cut);
		canvas->DrawString(line.String(), BPoint(x, y));
		rest.Remove(0, cut);
		rest.Trim();
		y += lineHeight;
	}
	return y;
}


static void
DrawIcon(BView* canvas, BPoint center, int icon)
{
	canvas->SetPenSize(1.6f);
	switch (icon) {
		case kIconDone:
			canvas->SetHighColor(kSeal);
			canvas->FillEllipse(center, 8, 8);
			canvas->SetHighColor(255, 244, 234);
			canvas->SetPenSize(2);
			canvas->SetLineMode(B_ROUND_CAP, B_ROUND_JOIN);
			canvas->StrokeLine(BPoint(center.x - 3.6f, center.y + 0.2f),
				BPoint(center.x - 1.1f, center.y + 2.7f));
			canvas->StrokeLine(BPoint(center.x - 1.1f, center.y + 2.7f),
				BPoint(center.x + 3.8f, center.y - 2.6f));
			canvas->SetLineMode(B_BUTT_CAP, B_MITER_JOIN);
			break;
		case kIconWarn:
			canvas->SetHighColor(kGold);
			canvas->StrokeEllipse(center, 7.5f, 7.5f);
			canvas->FillEllipse(center, 2.2f, 2.2f);
			break;
		case kIconFail:
			canvas->SetHighColor(kGold);
			canvas->StrokeEllipse(center, 7.5f, 7.5f);
			canvas->StrokeLine(BPoint(center.x - 2.8f, center.y - 2.8f),
				BPoint(center.x + 2.8f, center.y + 2.8f));
			canvas->StrokeLine(BPoint(center.x + 2.8f, center.y - 2.8f),
				BPoint(center.x - 2.8f, center.y + 2.8f));
			break;
		case kIconRunning:
		{
			canvas->SetHighColor(kIndigo2);
			canvas->SetPenSize(2.4f);
			canvas->StrokeEllipse(center, 7.5f, 7.5f);
			float turn = (float)(system_time() % 1200000) / 1200000.0f * 360;
			canvas->SetHighColor(kFire);
			canvas->StrokeArc(center, 7.5f, 7.5f, 90 - turn, -110);
			break;
		}
		case kIconSkipped:
			canvas->SetHighColor(kMuted);
			canvas->StrokeEllipse(center, 7.5f, 7.5f);
			canvas->StrokeLine(BPoint(center.x - 3.5f, center.y),
				BPoint(center.x + 3.5f, center.y));
			break;
		default:
			canvas->SetHighColor(Alpha(kMuted, 150));
			canvas->StrokeEllipse(center, 7.5f, 7.5f);
			break;
	}
	canvas->SetPenSize(1);
}


static std::string
Seconds(double seconds)
{
	char buffer[32];
	if (seconds < 60)
		snprintf(buffer, sizeof(buffer), "%d s", (int)(seconds + 0.5));
	else if (seconds < 3600)
		snprintf(buffer, sizeof(buffer), "%d min", (int)(seconds / 60 + 0.5));
	else
		snprintf(buffer, sizeof(buffer), "%d h %02d min", (int)(seconds / 3600),
			((int)seconds % 3600) / 60);
	return buffer;
}


// #pragma mark - VmView


VmView::VmView(VmSetup& setup, VmService& service, const std::string& vmDir,
	const std::string& client, const std::string& secret, bool enabled)
	:
	InkView("vms"),
	fSetup(setup),
	fService(service),
	fDir(vmDir),
	fReady(false),
	fEnabled(enabled),
	fShowResult(false),
	fX86(true),
	fHover(-1)
{
	SetFlags(Flags() | B_PULSE_NEEDED);
	for (int i = 0; i < kStepCount; i++)
		fDoneAt[i] = 0;
	SetExplicitMinSize(BSize(kWidth, kHeight));
	SetExplicitMaxSize(BSize(kWidth, kHeight));

	fClient = new BTextControl("client", NULL, client.c_str(),
		new BMessage(kMsgFieldChanged));
	fSecret = new BTextControl("secret", NULL, "",
		new BMessage(kMsgFieldChanged));
	// HideTyping() erases what's typed, so the secret goes in after it
	fSecret->TextView()->HideTyping(true);
	fSecret->SetText(secret.c_str());
	BTextControl* fields[2] = { fClient, fSecret };
	for (int i = 0; i < 2; i++) {
		fields[i]->SetModificationMessage(new BMessage(kMsgFieldChanged));
		AddChild(fields[i]);
		// In the x86 card of the first page (_PaintChoose)
		fields[i]->ResizeTo(238, 26);
		fields[i]->MoveTo(446, 304 + i * 32);
	}
	_Check();
}


void
VmView::AttachedToWindow()
{
	InkView::AttachedToWindow();
	fClient->SetTarget(this);
	fSecret->SetTarget(this);
	fSnapshot = fSetup.Snapshot();
	_ShowFields(_Page() == kChoose);
}


void
VmView::AllAttached()
{
	// After the fields' own AttachedToWindow(), which sets their colours
	InkView::AllAttached();
	BTextControl* fields[2] = { fClient, fSecret };
	for (int i = 0; i < 2; i++) {
		BTextView* text = fields[i]->TextView();
		text->SetViewColor(kNight);
		text->SetLowColor(kNight);
		BFont font = Mono(12);
		text->SetFontAndColor(&font, B_FONT_ALL, &kWashi);
		fields[i]->SetViewColor(kIndigo);
		fields[i]->SetLowColor(kIndigo);
		text->Invalidate();
	}
}


void
VmView::_Check()
{
	fHost = CheckHost(fDir);
	std::vector<std::string> argv;
	if (QemuInstallCommand(argv, fQemuHow).empty())
		fQemuHow = "Install it first: " + fQemuHow;
	else
		fQemuHow = "Awase installs it: " + fQemuHow;
	struct stat st;
	// A disk from an earlier setup is replaced, so its space counts as free
	if (stat((fDir + "/disk.img").c_str(), &st) == 0)
		fHost.diskFree += st.st_size;
	fReady = stat((fDir + "/ready").c_str(), &st) == 0
		&& stat((fDir + "/disk.img").c_str(), &st) == 0;
}


void
VmView::SetEnabled(bool enabled)
{
	fEnabled = enabled;
	Invalidate();
}


VmView::Page
VmView::_Page()
{
	if (fSnapshot.running || (fSnapshot.finished && fShowResult))
		return kProgress;
	if (!fHost.Refusal().empty())
		return kDeclined;
	if (fReady)
		return kProgress;
	return kChoose;
}


void
VmView::_ShowFields(bool show)
{
	BTextControl* fields[2] = { fClient, fSecret };
	for (int i = 0; i < 2; i++) {
		if (show && fields[i]->IsHidden())
			fields[i]->Show();
		else if (!show && !fields[i]->IsHidden())
			fields[i]->Hide();
	}
}


void
VmView::Pulse()
{
	VmSetupSnapshot snapshot = fSetup.Snapshot();
	bool finishedNow = fSnapshot.running && !snapshot.running;
	// A step that just finished lands like a seal
	for (int i = 0; i < kStepCount && i < (int)fSnapshot.steps.size(); i++) {
		if (snapshot.steps[i].state == kStepDone
			&& fSnapshot.steps[i].state != kStepDone && snapshot.running)
			fDoneAt[i] = system_time();
		else if (snapshot.steps[i].state != kStepDone)
			fDoneAt[i] = 0;
	}
	fSnapshot = snapshot;
	if (finishedNow)
		_Check();
	_ShowFields(_Page() == kChoose);
	Invalidate();
}


void
VmView::MessageReceived(BMessage* message)
{
	if (message->what == kMsgFieldChanged) {
		Invalidate();
		return;
	}
	InkView::MessageReceived(message);
}


void
VmView::MouseDown(BPoint where)
{
	if (_Page() == kChoose && fToggle.Contains(where)) {
		fX86 = !fX86;
		Invalidate();
		return;
	}
	for (size_t i = 0; i < fButtons.size(); i++) {
		if (fButtons[i].enabled && fButtons[i].frame.Contains(where)) {
			_Do(fButtons[i].what);
			return;
		}
	}
}


void
VmView::MouseMoved(BPoint where, uint32 transit, const BMessage* drag)
{
	int hover = -1;
	for (size_t i = 0; transit != B_EXITED_VIEW && i < fButtons.size(); i++) {
		if (fButtons[i].enabled && fButtons[i].frame.Contains(where))
			hover = (int)i;
	}
	if (hover != fHover) {
		fHover = hover;
		Invalidate();
	}
	InkView::MouseMoved(where, transit, drag);
}


void
VmView::_Do(uint32 what)
{
	BWindow* window = Window();
	switch (what) {
		case kMsgDoCancel:
		case kMsgDoHide:
			fShowResult = fSnapshot.running;
			window->PostMessage(B_QUIT_REQUESTED);
			break;
		case kMsgDoCheck:
			_Check();
			break;
		case kMsgDoSetup:
		case kMsgDoRetry:
		{
			BMessage message(kMsgVmSetup);
			message.AddString("client", fClient->Text());
			message.AddString("secret", fSecret->Text());
			window->PostMessage(&message);
			fShowResult = true;
			fSnapshot.running = true;	// until the next pulse says so
			break;
		}
		case kMsgDoCancelSetup:
			window->PostMessage(kMsgVmCancelSetup);
			break;
		case kMsgDoStart:
		case kMsgDoStop:
		{
			BMessage message(kMsgVmBuild);
			message.AddBool("on", what == kMsgDoStart);
			window->PostMessage(&message);
			fEnabled = what == kMsgDoStart;
			break;
		}
	}
	Invalidate();
}


// #pragma mark - drawing


void
VmView::_Header(BView* canvas, BRect bounds, const char* label,
	rgb_color labelColor, const char* title, const char* text)
{
	BRect band(bounds.left, bounds.top, bounds.right,
		bounds.top + kHeaderHeight);
	canvas->SetHighColor(kNight2);
	canvas->FillRect(band);
	DrawGlow(canvas, BPoint(bounds.right - 20, bounds.top - 30), 200, kFire,
		0.35f);
	canvas->SetHighColor(Alpha(kWashi, 22));
	canvas->StrokeLine(band.LeftBottom(), band.RightBottom());
	DrawLabel(canvas, BPoint(kPad, bounds.top + 32), label, labelColor);
	BFont display = Display(24);
	canvas->SetFont(&display);
	canvas->SetHighColor(kWashi);
	canvas->DrawString(title, BPoint(kPad, bounds.top + 63));
	if (text != NULL) {
		canvas->SetHighColor(kWashi2);
		Wrap(canvas, Body(13), text, kPad, bounds.top + 86,
			bounds.Width() - kPad * 2, 19);
	}
}


float
VmView::_Item(BView* canvas, float x, float y, float width, int icon,
	const char* title, const std::string& detail)
{
	DrawIcon(canvas, BPoint(x + 8, y + 1), icon);
	BFont bold = Body(13, true);
	canvas->SetFont(&bold);
	canvas->SetHighColor(icon == kIconWarn || icon == kIconFail ? kGold
		: kWashi);
	canvas->DrawString(title, BPoint(x + 26, y + 5));
	canvas->SetHighColor(kMuted);
	return Wrap(canvas, Body(11.5f), detail, x + 26, y + 22, width - 26, 16);
}


void
VmView::_Footer(BView* canvas, BRect bounds, const char* note)
{
	BRect band(bounds.left, bounds.bottom - kFooterHeight, bounds.right,
		bounds.bottom);
	canvas->SetHighColor(kNight2);
	canvas->FillRect(band);
	canvas->SetHighColor(Alpha(kWashi, 22));
	canvas->StrokeLine(band.LeftTop(), band.RightTop());
	if (note != NULL) {
		BFont small = Body(12);
		canvas->SetFont(&small);
		canvas->SetHighColor(kMuted);
		canvas->DrawString(note, BPoint(kPad, band.top + 36));
	}
}


void
VmView::_AddButton(BView* canvas, float right, float top, uint32 what,
	const char* label, bool primary, bool enabled, float* left)
{
	BFont bold = Body(13, true);
	float width = bold.StringWidth(label) + 36;
	BRect frame(right - width, top, right, top + 34);
	Button button;
	button.frame = frame;
	button.what = what;
	button.label = label;
	button.primary = primary;
	button.enabled = enabled;
	bool hover = fHover == (int)fButtons.size();
	fButtons.push_back(button);

	if (primary && enabled) {
		DrawGlow(canvas, BPoint(frame.left + frame.Width() / 2,
			frame.top + 17), frame.Width() * 0.6f, kFire, hover ? 1.0f : 0.6f);
		canvas->SetHighColor(hover ? kFire2 : kFire);
		canvas->FillRoundRect(frame, 6, 6);
		canvas->SetHighColor(Alpha(kWashi, 90));
		canvas->StrokeLine(BPoint(frame.left + 5, frame.top + 1),
			BPoint(frame.right - 5, frame.top + 1));
	} else {
		canvas->SetHighColor(Alpha(kWashi, hover ? 26 : 0));
		canvas->FillRoundRect(frame, 6, 6);
		canvas->SetHighColor(Alpha(kWashi, enabled ? (hover ? 170 : 90) : 30));
		canvas->StrokeRoundRect(frame, 6, 6);
	}
	canvas->SetFont(&bold);
	canvas->SetHighColor(primary && enabled ? make_color(22, 10, 2)
		: enabled ? kWashi : Alpha(kMuted, 120));
	canvas->DrawString(label, BPoint(frame.left + 18, frame.top + 22));
	if (left != NULL)
		*left = frame.left - 12;
}


void
VmView::Paint(BView* canvas, BRect bounds)
{
	canvas->SetDrawingMode(B_OP_ALPHA);
	canvas->SetHighColor(kNight);
	canvas->FillRect(bounds);
	fButtons.clear();
	switch (_Page()) {
		case kChoose:
			_PaintChoose(canvas, bounds);
			break;
		case kDeclined:
			_PaintDeclined(canvas, bounds);
			break;
		case kProgress:
			_PaintProgress(canvas, bounds);
			break;
	}
}


void
VmView::_PaintChoose(BView* canvas, BRect bounds)
{
	const Accelerator& accel = HostAccelerator();
	std::string intro = std::string("Awase runs a 32-bit Renku in a virtual "
		"machine, in QEMU with ") + accel.name + ", and hands it the pool's x86 "
		"jobs. It builds only within your limits, like everything else.";
	_Header(canvas, bounds, "BUILD FOR MORE ARCHITECTURES", kFire2,
		"Let this machine build x86 too", intro.c_str());

	// This machine
	float left = kPad, column = 268;
	float y = kHeaderHeight + 30;
	DrawLabel(canvas, BPoint(left, y), "THIS MACHINE", kMuted);
	y += 26;
	y = _Item(canvas, left, y, column, kIconDone, "64-bit x86",
		fHost.cpu.empty() ? std::string("x86_64") : fHost.cpu) + 6;
	y = _Item(canvas, left, y, column, kIconDone, accel.name,
		std::string(accel.what) + ", using "
			+ (fHost.virt.empty() ? std::string("the processor") : fHost.virt))
		+ 6;
	std::string memory = FormatBytes(fHost.memoryTotal) + " memory";
	y = _Item(canvas, left, y, column,
		fHost.memoryFree >= (int64_t)3 << 30 ? kIconDone : kIconWarn,
		memory.c_str(), FormatBytes(fHost.memoryFree) + " free now") + 6;
	std::string disk = FormatBytes(fHost.diskFree) + " free";
	bool roomy = fHost.diskFree >= (int64_t)22 << 30;
	y = _Item(canvas, left, y, column, roomy ? kIconDone : kIconWarn,
		disk.c_str(), roomy ? "The VM takes up to 20 GB"
			: "The VM needs about 22 GB free") + 6;
	if (fHost.qemu.empty())
		_Item(canvas, left, y, column, kIconWarn, "QEMU isn't installed",
			fQemuHow);
	else
		_Item(canvas, left, y, column, kIconDone, "QEMU is installed",
			fHost.qemu);
	canvas->SetHighColor(kMuted);
	Wrap(canvas, Body(11.5f), std::string(accel.name) + " runs the VM on the "
		"CPU itself, so it builds close to native speed.", left,
		bounds.bottom - kFooterHeight - 38, column, 16);

	canvas->SetHighColor(Alpha(kWashi, 22));
	canvas->StrokeLine(BPoint(left + column + 18, kHeaderHeight),
		BPoint(left + column + 18, bounds.bottom - kFooterHeight));

	// Architectures
	float x = left + column + 40, right = bounds.right - kPad;
	y = kHeaderHeight + 30;
	DrawLabel(canvas, BPoint(x, y), "ARCHITECTURES", kMuted);
	BRect card(x, y + 14, right, y + 240);
	canvas->SetHighColor(Alpha(kFire, fX86 ? 22 : 8));
	canvas->FillRoundRect(card, 8, 8);
	canvas->SetHighColor(Alpha(kFire, fX86 ? 115 : 50));
	canvas->StrokeRoundRect(card, 8, 8);

	fToggle = BRect(card.left + 14, card.top + 14, card.left + 30,
		card.top + 30);
	canvas->SetHighColor(fX86 ? kFire : Alpha(kWashi, 40));
	canvas->FillRoundRect(fToggle, 3, 3);
	if (fX86) {
		canvas->SetHighColor(make_color(22, 10, 2));
		canvas->SetPenSize(2);
		canvas->StrokeLine(BPoint(fToggle.left + 4, fToggle.top + 8),
			BPoint(fToggle.left + 7, fToggle.top + 11));
		canvas->StrokeLine(BPoint(fToggle.left + 7, fToggle.top + 11),
			BPoint(fToggle.left + 12, fToggle.top + 5));
		canvas->SetPenSize(1);
	}
	float tx = card.left + 42;
	BFont display = Display(15);
	canvas->SetFont(&display);
	canvas->SetHighColor(kWashi);
	canvas->DrawString("x86", BPoint(tx, card.top + 27));
	BFont chipFont = Mono(10.5f);
	BRect chip(tx + 36, card.top + 14, tx + 36
		+ chipFont.StringWidth(accel.name) + 14, card.top + 31);
	canvas->SetHighColor(Alpha(kFire, 40));
	canvas->FillRoundRect(chip, 4, 4);
	canvas->SetFont(&chipFont);
	canvas->SetHighColor(kFire2);
	canvas->DrawString(accel.name, BPoint(chip.left + 7, chip.top + 12));
	canvas->SetHighColor(kWashi2);
	Wrap(canvas, Body(12), "Renku x86_gcc2h nightly, the pool's x86 "
		"platform. Accelerated: close to native speed.", tx, card.top + 50,
		card.right - tx - 14, 17);
	BFont mono = Mono(11);
	canvas->SetFont(&mono);
	canvas->SetHighColor(kMuted);
	canvas->DrawString("cores 4   memory 3 GB   disk 20 GB",
		BPoint(tx, card.top + 94));

	// The x86 build client: the portal gives each client one platform. The
	// text fields sit at fixed places to match (see the constructor).
	float ty = card.top + 126;
	canvas->SetHighColor(Alpha(kWashi, 22));
	canvas->StrokeLine(BPoint(tx, ty - 10), BPoint(card.right - 14, ty - 10));
	DrawLabel(canvas, BPoint(tx, ty + 6), "X86 BUILD CLIENT", kFire2);
	BFont small = Body(11.5f);
	canvas->SetFont(&small);
	canvas->SetHighColor(kWashi2);
	canvas->DrawString("Client ID", BPoint(tx, ty + 34));
	canvas->DrawString("Secret", BPoint(tx, ty + 66));

	// What isn't offered, and why
	BRect other(x, card.bottom + 12, right, card.bottom + 104);
	canvas->SetHighColor(Alpha(kWashi, 40));
	canvas->StrokeRoundRect(other, 8, 8, B_MIXED_COLORS);
	BFont otherTitle = Display(14);
	canvas->SetFont(&otherTitle);
	canvas->SetHighColor(kMuted);
	canvas->DrawString("arm64, riscv64", BPoint(other.left + 14,
		other.top + 22));
	canvas->SetFont(&chipFont);
	canvas->DrawString("NOT OFFERED", BPoint(other.left + 24
		+ otherTitle.StringWidth("arm64, riscv64"), other.top + 21));
	Wrap(canvas, Body(11.5f), std::string("On an x86 machine, ") + accel.name
		+ " runs only x86 code. Other architectures would be emulated, far too "
		"slowly to build for the pool, so Awase doesn't set them up.",
		other.left + 14, other.top + 42, other.Width() - 28, 16);

	_Footer(canvas, bounds, "Downloads about 700 MB once.");
	bool filled = strlen(fClient->Text()) > 0 && strlen(fSecret->Text()) > 0;
	float at = bounds.right - kPad;
	_AddButton(canvas, at, bounds.bottom - 48, kMsgDoSetup,
		fHost.qemu.empty() ? "Install QEMU and set up" : "Set up", true,
		fX86 && filled && roomy, &at);
	_AddButton(canvas, at, bounds.bottom - 48, kMsgDoCancel, "Cancel", false,
		true, NULL);
}


void
VmView::_PaintDeclined(BView* canvas, BRect bounds)
{
	const Accelerator& accel = HostAccelerator();
	std::string needs = std::string("NEEDS ") + accel.name;
	std::string intro = std::string("Awase builds in a VM only with ")
		+ accel.name + ", " + accel.what + ". Without it the VM would be "
		"emulated, far too slowly to build for the pool, so Awase won't set "
		"one up.";
	_Header(canvas, bounds, needs.c_str(), kGold,
		"This machine can't run the x86 VM", intro.c_str());

	float left = kPad, width = bounds.Width() - kPad * 2;
	float y = kHeaderHeight + 30;
	DrawLabel(canvas, BPoint(left, y), "WHAT AWASE FOUND", kMuted);
	y += 26;
	y = _Item(canvas, left, y, width, fHost.x86_64 ? kIconDone : kIconFail,
		fHost.x86_64 ? "64-bit x86" : "Not 64-bit x86",
		fHost.cpu.empty() ? std::string("this processor") : fHost.cpu) + 10;

	if (fHost.x86_64) {
		BRect box(left - 12, y - 14, bounds.right - kPad + 12, y + 76);
		canvas->SetHighColor(Alpha(kGold, 18));
		canvas->FillRoundRect(box, 8, 8);
		canvas->SetHighColor(Alpha(kGold, 90));
		canvas->StrokeRoundRect(box, 8, 8);
		std::string why = fHost.Refusal();
		if (!fHost.accelDenied)
			why += " Inside a virtual machine, the host has to pass "
				"virtualization through (\"nested virtualization\").";
		std::string title = std::string(accel.name) + (fHost.accelDenied
			? " isn't yours to use" : " isn't available");
		y = _Item(canvas, left, y, width, kIconFail, title.c_str(), why) + 22;
	}
	_Item(canvas, left, y, width, kIconSkipped, "Memory, disk, QEMU",
		std::string("Not checked: nothing is installed or downloaded without ")
			+ accel.name);

	canvas->SetHighColor(kWashi2);
	Wrap(canvas, Body(12.5f), "Awase keeps building for this machine's own "
		"platform, as before.", left, bounds.bottom - kFooterHeight - 24,
		width, 18);

	_Footer(canvas, bounds, NULL);
	float at = bounds.right - kPad;
	_AddButton(canvas, at, bounds.bottom - 48, kMsgDoCheck, "Check again", true,
		true, &at);
	_AddButton(canvas, at, bounds.bottom - 48, kMsgDoCancel, "Close", false,
		true, NULL);
	float unused;
	_AddButton(canvas, kPad + Body(13, true).StringWidth(
		"Install QEMU and set up") + 36, bounds.bottom - 48, kMsgDoSetup,
		"Install QEMU and set up", false, false, &unused);
}


void
VmView::_PaintProgress(BView* canvas, BRect bounds)
{
	const VmSetupSnapshot& s = fSnapshot;
	bool running = s.running;
	bool failed = s.finished && !s.succeeded && fShowResult;
	bool ready = !running && !failed;

	int step = s.current >= 0 ? s.current + 1 : kStepCount;
	char label[64];
	if (running)
		snprintf(label, sizeof(label), "SETTING UP \xc2\xb7 STEP %d OF %d", step,
			(int)kStepCount);
	else
		snprintf(label, sizeof(label), failed ? "SETUP STOPPED"
			: "SET UP \xc2\xb7 X86 VM");
	const char* title = running ? "Setting up the x86 VM"
		: failed ? "Setup stopped" : "The x86 VM is ready";
	_Header(canvas, bounds, label, failed ? kGold : kFire2, title, NULL);

	// The whole setup, as a figure and a bar
	double fraction = ready ? 1.0 : s.fraction;
	char percent[16];
	snprintf(percent, sizeof(percent), "%d", (int)(fraction * 100 + 0.5));
	BFont figure = Display(28);
	BFont mono = Mono(12);
	float pw = figure.StringWidth(percent);
	float right = bounds.right - kPad;
	canvas->SetFont(&figure);
	canvas->SetHighColor(kWashi);
	canvas->DrawString(percent, BPoint(right - pw - 14, bounds.top + 58));
	canvas->SetFont(&mono);
	canvas->SetHighColor(kMuted);
	canvas->DrawString("%", BPoint(right - 11, bounds.top + 58));
	std::string sub;
	if (running && s.secondsLeft > 0)
		sub = "about " + Seconds(s.secondsLeft) + " left";
	else if (ready) {
		VmServiceState state = fService.State();
		sub = !fEnabled ? "not building" : state == kVmRunning
			? (fService.Frozen() ? "paused" : "running") : state == kVmStarting
				? "starting" : state == kVmFailed ? "stopped" : "off";
	}
	if (!sub.empty()) {
		BFont small = Body(11.5f);
		canvas->SetFont(&small);
		canvas->DrawString(sub.c_str(), BPoint(right - small.StringWidth(
			sub.c_str()), bounds.top + 78));
	}
	BRect track(kPad, bounds.top + 92, right, bounds.top + 97);
	canvas->SetHighColor(kIndigo2);
	canvas->FillRoundRect(track, 2.5f, 2.5f);
	if (fraction > 0.005) {
		BRect fill(track);
		fill.right = track.left + track.Width() * fraction;
		for (float px = fill.left; px < fill.right; px += 2) {
			canvas->SetHighColor(Mix(kSeal, kGold,
				(px - track.left) / track.Width()));
			canvas->FillRect(BRect(px, fill.top, std::min(px + 2, fill.right),
				fill.bottom));
		}
		BPoint tip(fill.right, track.top + 2.5f);
		DrawGlow(canvas, tip, 12, failed ? kGold : kFire, 1.3f);
		canvas->SetHighColor(255, 246, 220);
		canvas->FillEllipse(tip, 4, 4);
	}

	// The steps
	float y = kHeaderHeight + 22;
	for (int i = 0; i < kStepCount; i++) {
		const VmStep& st = s.steps[i];
		VmStepState state = ready ? kStepDone : st.state;
		bool current = state == kStepRunning;
		float rowHeight = current ? 62 : 44;
		if (current) {
			BRect box(kPad - 14, y - 10, right + 14, y + rowHeight - 12);
			canvas->SetHighColor(Alpha(kFire, 20));
			canvas->FillRoundRect(box, 8, 8);
			canvas->SetHighColor(Alpha(kFire, 75));
			canvas->StrokeRoundRect(box, 8, 8);
		}
		int icon = state == kStepDone ? kIconDone : state == kStepFailed
			? kIconFail : current ? kIconRunning : kIconWaiting;
		uint8 dim = state == kStepWaiting ? 130 : 255;
		BPoint iconAt(kPad + 9, y + 8);
		float age = fDoneAt[i] > 0
			? (system_time() - fDoneAt[i]) / 1000000.0f : 99;
		if (age < 1.8f) {
			DrawGlow(canvas, iconAt, 18, kSeal, 1.2f * (1 - age / 1.8f));
			DrawRipples(canvas, iconAt, age, 9, kFire2);
			DrawSparks(canvas, iconAt, age, 8, (uint32)(i * 7919 + 13),
				Mix(kFire2, kWashi, 0.4f));
		}
		DrawIcon(canvas, iconAt, icon);
		BFont bold = Body(13.5f, true);
		canvas->SetFont(&bold);
		canvas->SetHighColor(Alpha(state == kStepFailed ? kGold : kWashi, dim));
		canvas->DrawString(st.title.c_str(), BPoint(kPad + 34, y + 12));
		BFont small = Body(11.5f);
		canvas->SetFont(&small);
		canvas->SetHighColor(Alpha(current ? kWashi2 : kMuted, dim));
		BString detail(st.detail.c_str());
		small.TruncateString(&detail, B_TRUNCATE_END, right - kPad - 120);
		canvas->DrawString(detail.String(), BPoint(kPad + 34, y + 29));

		std::string time;
		if (current && st.secondsLeft > 0)
			time = Seconds(st.secondsLeft) + " left";
		else if (current || (state != kStepWaiting && st.seconds > 0.5))
			time = Seconds(st.seconds);
		if (!time.empty()) {
			BFont m = Mono(11);
			canvas->SetFont(&m);
			canvas->SetHighColor(current ? kFire2 : kMuted);
			canvas->DrawString(time.c_str(), BPoint(right - m.StringWidth(
				time.c_str()), y + 12));
		}
		if (current) {
			BRect bar(kPad + 34, y + 38, right - 80, y + 42);
			canvas->SetHighColor(kIndigo2);
			canvas->FillRoundRect(bar, 2, 2);
			if (st.fraction >= 0) {
				BRect fill(bar);
				fill.right = bar.left + bar.Width() * st.fraction;
				canvas->SetHighColor(kFire);
				canvas->FillRoundRect(fill, 2, 2);
				DrawGlow(canvas, BPoint(fill.right, bar.top + 2), 8, kFire, 1);
			} else {
				// Can't tell how far: a light sweeps along the bar
				float t = (float)(system_time() % 1600000) / 1600000.0f;
				float w = bar.Width() * 0.25f;
				float x0 = bar.left - w + (bar.Width() + w) * t;
				BRect glow(std::max(bar.left, x0), bar.top,
					std::min(bar.right, x0 + w), bar.bottom);
				if (glow.IsValid()) {
					canvas->SetHighColor(kFire);
					canvas->FillRoundRect(glow, 2, 2);
				}
			}
		}
		y += rowHeight;
	}

	// The last thing it said, or why it stopped
	std::string line = failed ? s.error : running && s.current >= 0
		? s.steps[s.current].detail : ready && fService.State() == kVmFailed
			? fService.Message() : "";
	if (!line.empty()) {
		BRect strip(kPad, bounds.bottom - kFooterHeight - 42, right,
			bounds.bottom - kFooterHeight - 14);
		canvas->SetHighColor(kNight2);
		canvas->FillRoundRect(strip, 5, 5);
		canvas->SetHighColor(Alpha(kWashi, 20));
		canvas->StrokeRoundRect(strip, 5, 5);
		BFont m = Mono(11);
		canvas->SetFont(&m);
		canvas->SetHighColor(failed ? kGold : kMuted);
		BString text(("Log \xc2\xb7 " + line).c_str());
		m.TruncateString(&text, B_TRUNCATE_END, strip.Width() - 20);
		canvas->DrawString(text.String(), BPoint(strip.left + 10,
			strip.top + 18));
	}

	float at = right;
	if (running) {
		_Footer(canvas, bounds, "You can close this window; setup carries on.");
		_AddButton(canvas, at, bounds.bottom - 48, kMsgDoHide, "Hide", false,
			true, &at);
		_AddButton(canvas, at, bounds.bottom - 48, kMsgDoCancelSetup,
			"Cancel setup", false, true, NULL);
	} else if (failed) {
		_Footer(canvas, bounds, "Its logs are in the VM folder.");
		_AddButton(canvas, at, bounds.bottom - 48, kMsgDoRetry, "Try again",
			true, true, &at);
		_AddButton(canvas, at, bounds.bottom - 48, kMsgDoCancel, "Close", false,
			true, NULL);
	} else {
		_Footer(canvas, bounds, fEnabled
			? "Awase builds the pool's x86 jobs in it."
			: "It builds only when you turn it on.");
		_AddButton(canvas, at, bounds.bottom - 48,
			fEnabled ? kMsgDoStop : kMsgDoStart,
			fEnabled ? "Stop building in the VM" : "Build in the VM", true, true,
			&at);
		_AddButton(canvas, at, bounds.bottom - 48, kMsgDoCancel, "Close", false,
			true, NULL);
	}
}


// #pragma mark - VmWindow


VmWindow::VmWindow(BWindow* owner, VmSetup& setup, VmService& service,
	const std::string& vmDir, const std::string& client,
	const std::string& secret, bool enabled)
	:
	BWindow(BRect(0, 0, kWidth - 1, kHeight - 1), "Virtual machines",
		B_TITLED_WINDOW, B_NOT_RESIZABLE | B_NOT_ZOOMABLE
			| B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS),
	fOwner(owner)
{
	fView = new VmView(setup, service, vmDir, client, secret, enabled);
	fView->ResizeTo(kWidth - 1, kHeight - 1);
	AddChild(fView);
	SetPulseRate(100000);
	if (owner != NULL)
		CenterIn(owner->Frame());
}


void
VmWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgVmSetup:
		case kMsgVmCancelSetup:
		case kMsgVmBuild:
			BMessenger(fOwner).SendMessage(message);
			break;
		case kMsgVmEnabled:
		{
			bool on;
			if (message->FindBool("on", &on) == B_OK)
				fView->SetEnabled(on);
			break;
		}
		default:
			BWindow::MessageReceived(message);
	}
}


bool
VmWindow::QuitRequested()
{
	return true;
}
