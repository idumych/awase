/*
 * Copyright 2026, Ian Dumych, idumych@gmail.com.
 * SPDX-License-Identifier: MIT
 */
#ifndef AWASE_VIEWS_H
#define AWASE_VIEWS_H

#include "Ink.h"

#include <Point.h>

#include <map>
#include <string>
#include <vector>

#include "LivePool.h"
#include "VmHost.h"
#include "VmService.h"


namespace awase {
	class DemoPool;
}


// What the views draw: the demo pool, or a copy of the coordinator client's
// state taken once a frame.
struct Model {
	const awase::DemoPool*	demo;
	bool					live;
	awase::LiveSnapshot		snapshot;

	// The x86 build VM
	bool					vmReady;		// set up
	bool					vmOn;			// building in it is on
	awase::VmServiceState	vmState;
	bool					vmFrozen;
	std::string				vmMessage;
	bool					vmSettingUp;
	double					vmSetupFraction;
	awase::LiveSnapshot		vmSnapshot;

	// The VM is up and its client running, so there are two machines
	bool					TwoMachines() const
								{ return live && vmOn
									&& vmState == awase::kVmRunning; }
};


// The x86 build VM needs an x86 machine with NVMM (Haiku) or KVM (Linux):
// the lite builds for arm64 (make defines AWASE_NO_VM) offer no way to it.
#ifdef AWASE_NO_VM
static const bool kHasVm = false;
#else
static const bool kHasVm = true;
#endif


enum {
	kMsgTick = 'tick',
	kMsgTogglePause = 'paus',
	kMsgLimits = 'limt',		// "cores", "temperature" (int32), "idle" (bool)
	kMsgWebsite = 'webs',
	kMsgToggleMini = 'mini',
	kMsgToggleDeskbar = 'dbtg',
};


// What the machine is doing, in a few words, and the colour of its dot.
const char*	StatusText(const Model& model, rgb_color& dot);


// The top bar: the seal and name, what the machine is doing, Pause.
class HeaderView : public ink::InkView {
public:
								HeaderView(const Model& model);

	virtual	void				MouseDown(BPoint where);
	virtual	void				MouseMoved(BPoint where, uint32 transit,
									const BMessage* dragMessage);

protected:
	virtual	void				Paint(BView* canvas, BRect bounds);

private:
			BRect				_ButtonFrame() const;

			const Model&		fModel;
			const awase::DemoPool& fPool;
			bool				fHover;
};


// The pool: packages in dependency order, builds travelling to them, seals.
class PoolView : public ink::InkView {
public:
								PoolView(const Model& model);

	virtual	void				MouseMoved(BPoint where, uint32 transit,
									const BMessage* dragMessage);

protected:
	virtual	void				Paint(BView* canvas, BRect bounds);

private:
			void				_Layout(BRect bounds);
			BPoint				_Anchor(int volunteer) const;
			void				_DrawCard(BView* canvas, BRect bounds);
			void				_DrawSky(BView* canvas, BRect bounds);
			void				_PaintLive(BView* canvas, BRect bounds);

			const Model&		fModel;
			const awase::DemoPool& fPool;
			std::vector<BPoint>	fNodes;
			std::vector<BPoint>	fAnchors;
			BPoint				fMouse;
			bool				fMouseInside;
			float				fShownSealed;	// eases toward the count
			std::map<std::string, bigtime_t> fFirstSeen;	// live packages
};


// What your machine is building now.
class NowView : public ink::InkView {
public:
								NowView(const Model& model);

protected:
	virtual	void				Paint(BView* canvas, BRect bounds);

private:
			void				_PaintBuilding(BView* canvas, BRect bounds);
			void				_PaintLive(BView* canvas, BRect bounds);
			void				_PaintTwo(BView* canvas, BRect bounds);
			void				_PaintMachine(BView* canvas, BRect frame,
									const awase::LiveSnapshot& live,
									const char* who, rgb_color color,
									const char* extra);
			void				_PaintWaiting(BView* canvas, BRect bounds,
									int package);
			void				_PaintMessage(BView* canvas, BRect bounds,
									const char* label, const char* title,
									const char* text, rgb_color color);

			const Model&		fModel;
			const awase::DemoPool& fPool;
};


// Your figures, and a line of your recent build time.
class StatsView : public ink::InkView {
public:
								StatsView(const Model& model);

protected:
	virtual	void				Paint(BView* canvas, BRect bounds);

private:
			void				_PaintLive(BView* canvas, BRect bounds);
			const Model&		fModel;
			const awase::DemoPool& fPool;
};


// What happened lately in the pool.
class ActivityView : public ink::InkView {
public:
								ActivityView(const Model& model);

protected:
	virtual	void				Paint(BView* canvas, BRect bounds);

private:
			void				_PaintLive(BView* canvas, BRect bounds);
			const Model&		fModel;
			const awase::DemoPool& fPool;
};


// The column right of the pool: what's building now, your figures, and the
// pool's activity. On a short window the activity goes first, then the
// figures, so what's building now always shows.
class PanelColumn : public BView {
public:
								PanelColumn(NowView* now, StatsView* stats,
									ActivityView* activity);

	virtual	void				AllAttached();
	virtual	void				FrameResized(float width, float height);
	virtual	BSize				MinSize();
	virtual	BSize				MaxSize();
	virtual	BSize				PreferredSize();

protected:
	virtual	void				DoLayout();

private:
			void				_Arrange();
			void				_Place(int index, float top, float height);

			BView*				fViews[3];
			bool				fShown[3];
};


// Mini mode: the window shrunk to what's building, a few figures, Full
// view and Pause, as in the Pocket mockup.
class PocketView : public ink::InkView {
public:
								PocketView(const Model& model);

	virtual	void				MouseDown(BPoint where);
	virtual	void				MouseMoved(BPoint where, uint32 transit,
									const BMessage* dragMessage);

protected:
	virtual	void				Paint(BView* canvas, BRect bounds);

private:
			enum Button { kNoButton, kFullView, kPause };

			BRect				_ButtonFrame(Button button) const;
			void				_PaintRing(BView* canvas, BPoint center);
			void				_PaintButton(BView* canvas, Button button,
									const char* label, bool filled);

			const Model&		fModel;
			const awase::DemoPool& fPool;
			Button				fHover;
};


// The limits: cores, temperature, and when to build.
class LimitsView : public ink::InkView {
public:
								LimitsView(const Model& model);

	virtual	void				MouseDown(BPoint where);
	virtual	void				MouseMoved(BPoint where, uint32 transit,
									const BMessage* dragMessage);
	virtual	void				MouseUp(BPoint where);

protected:
	virtual	void				Paint(BView* canvas, BRect bounds);

private:
			enum Control { kNone, kCores, kTemperature, kWhen };

			BRect				_Track(Control control) const;
			BRect				_Segments() const;
			BRect				_VmButton() const;
			void				_DragTo(BPoint where);
			void				_Send(int cores, int temperature, bool idle);
			void				_PaintSlider(BView* canvas, Control control,
									const char* label, const char* value,
									float fraction);

			const Model&		fModel;
			const awase::DemoPool& fPool;
			Control				fDragging;
			Control				fHover;
			bool				fVmHover;
};


#endif	// AWASE_VIEWS_H
