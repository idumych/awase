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


namespace awase {
	class DemoPool;
}


// What the views draw: the demo pool, or a copy of the coordinator client's
// state taken once a frame.
struct Model {
	const awase::DemoPool*	demo;
	bool					live;
	awase::LiveSnapshot		snapshot;
};


enum {
	kMsgTick = 'tick',
	kMsgTogglePause = 'paus',
	kMsgLimits = 'limt',		// "cores", "temperature" (int32), "idle" (bool)
	kMsgWebsite = 'webs',
};


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
			const char*			_Status(rgb_color& dot,
									std::string& storage) const;

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
			void				_DragTo(BPoint where);
			void				_Send(int cores, int temperature, bool idle);
			void				_PaintSlider(BView* canvas, Control control,
									const char* label, const char* value,
									float fraction);

			const Model&		fModel;
			const awase::DemoPool& fPool;
			Control				fDragging;
			Control				fHover;
};


#endif	// AWASE_VIEWS_H
