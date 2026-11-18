/*
 * Copyright 2026, Ian Dumych, idumych@gmail.com.
 * SPDX-License-Identifier: MIT
 */


#include "Pool.h"

#include <algorithm>


namespace awase {


static const size_t kMaxEvents = 50;
static const size_t kHistoryBuckets = 24;
static const size_t kQuorum = 2;


Pool::Pool()
	:
	fNow(0),
	fBucketStart(0),
	fYou(-1),
	fLastYours(-1)
{
	fStats.builds = 0;
	fStats.accepted = 0;
	fStats.mismatched = 0;
	fStats.buildSeconds = 0;
	fStats.history.push_back(0);
}


int
Pool::AddVolunteer(const std::string& name, double speed, bool you)
{
	Volunteer volunteer;
	volunteer.name = name;
	volunteer.speed = speed;
	volunteer.you = you;
	volunteer.package = -1;
	fVolunteers.push_back(volunteer);
	int index = (int)fVolunteers.size() - 1;
	if (you)
		fYou = index;
	return index;
}


int
Pool::AddPackage(const std::string& name, const std::string& version,
	const std::string& arch, const std::vector<int>& deps, double cost)
{
	Package package;
	package.name = name;
	package.version = version;
	package.arch = arch;
	package.deps = deps;
	package.layer = 0;
	for (size_t i = 0; i < deps.size(); i++)
		package.layer = std::max(package.layer, fPackages[deps[i]].layer + 1);
	package.state = kWaiting;
	package.cost = cost;
	package.sealedAt = -1;
	package.mismatchAt = -1;
	package.mismatches = 0;
	fPackages.push_back(package);
	_UpdateReadiness();
	return (int)fPackages.size() - 1;
}


void
Pool::Clear()
{
	fPackages.clear();
	for (size_t i = 0; i < fVolunteers.size(); i++)
		fVolunteers[i].package = -1;
	fLastYours = -1;
}


bool
Pool::CanBuild(int index, int volunteer) const
{
	const Package& package = fPackages[index];
	if (package.state != kReady && package.state != kBuilding)
		return false;
	if (fVolunteers[volunteer].package >= 0)
		return false;
	if (package.copies.size() + package.results.size() >= kQuorum)
		return false;
	// Never two copies of one package on the same machine.
	if (_CopyIndex(package, volunteer) >= 0)
		return false;
	for (size_t i = 0; i < package.results.size(); i++) {
		if (package.results[i].volunteer == volunteer)
			return false;
	}
	return true;
}


void
Pool::Assign(int index, int volunteer, double needed)
{
	if (!CanBuild(index, volunteer))
		return;

	Package& package = fPackages[index];
	Copy copy;
	copy.volunteer = volunteer;
	copy.done = 0;
	copy.needed = needed;
	package.copies.push_back(copy);
	package.state = kBuilding;
	fVolunteers[volunteer].package = index;

	if (volunteer == fYou) {
		fLastYours = index;
		_Event(kEventYouStarted, index,
			"You started " + package.name + " " + package.version);
	}
}


void
Pool::Work(int volunteer, double seconds)
{
	int index = fVolunteers[volunteer].package;
	if (index < 0)
		return;
	Package& package = fPackages[index];
	int copy = _CopyIndex(package, volunteer);
	if (copy < 0)
		return;
	Copy& c = package.copies[copy];
	c.done = std::min(c.needed, c.done + seconds);
}


void
Pool::Finish(int volunteer, const std::string& hash)
{
	int index = fVolunteers[volunteer].package;
	if (index < 0)
		return;
	Package& package = fPackages[index];
	int copy = _CopyIndex(package, volunteer);
	if (copy < 0)
		return;

	package.copies.erase(package.copies.begin() + copy);
	fVolunteers[volunteer].package = -1;
	Result result;
	result.volunteer = volunteer;
	result.hash = hash;
	package.results.push_back(result);

	bool yours = volunteer == fYou;
	if (yours) {
		fStats.builds++;
		_Event(kEventYouFinished, index,
			"You finished " + package.name + "; waiting for a second build");
	}

	if (package.results.size() < kQuorum)
		return;

	const Result& a = package.results[0];
	const Result& b = package.results[1];
	bool youTookPart = a.volunteer == fYou || b.volunteer == fYou;
	std::string who = fVolunteers[a.volunteer].name + " and "
		+ fVolunteers[b.volunteer].name;

	if (a.hash == b.hash) {
		package.state = kSealed;
		package.sealedAt = fNow;
		if (youTookPart)
			fStats.accepted++;
		_Event(kEventSealed, index,
			package.name + " " + package.version + " sealed: " + who
				+ " matched");
	} else {
		package.mismatches++;
		package.mismatchAt = fNow;
		if (youTookPart)
			fStats.mismatched++;
		_Event(kEventMismatch, index,
			package.name + ": " + who + " disagree; sent to a maintainer");
		// The maintainer's look is out of scope here: build it again.
		package.results.clear();
		package.state = kReady;
	}
	_UpdateReadiness();
}


void
Pool::Revoke(int volunteer)
{
	int index = fVolunteers[volunteer].package;
	if (index < 0)
		return;
	Package& package = fPackages[index];
	int copy = _CopyIndex(package, volunteer);
	if (copy >= 0)
		package.copies.erase(package.copies.begin() + copy);
	fVolunteers[volunteer].package = -1;
	if (package.copies.empty() && package.results.empty())
		package.state = kReady;
}


bool
Pool::AllSealed() const
{
	for (size_t i = 0; i < fPackages.size(); i++) {
		if (fPackages[i].state != kSealed)
			return false;
	}
	return !fPackages.empty();
}


int
Pool::YourPackage() const
{
	return fYou >= 0 ? fVolunteers[fYou].package : -1;
}


double
Pool::YourProgress() const
{
	int index = YourPackage();
	if (index < 0)
		return fLastYours >= 0 ? 1.0 : 0.0;
	const Package& package = fPackages[index];
	int copy = _CopyIndex(package, fYou);
	if (copy < 0 || package.copies[copy].needed <= 0)
		return 0;
	return package.copies[copy].done / package.copies[copy].needed;
}


BuildPhase
Pool::YourPhase() const
{
	if (YourPackage() >= 0)
		return PhaseFor(YourProgress());
	return kAwaitingMatch;
}


double
Pool::YourSecondsLeft() const
{
	int index = YourPackage();
	if (index < 0)
		return 0;
	const Package& package = fPackages[index];
	int copy = _CopyIndex(package, fYou);
	double speed = fVolunteers[fYou].speed;
	if (copy < 0 || speed <= 0)
		return 0;
	return (package.copies[copy].needed - package.copies[copy].done) / speed;
}


void
Pool::AddWorkingTime(double seconds, double bucketLength)
{
	fStats.buildSeconds += seconds;
	while (fNow - fBucketStart >= bucketLength) {
		fBucketStart += bucketLength;
		fStats.history.push_back(0);
		if (fStats.history.size() > kHistoryBuckets)
			fStats.history.erase(fStats.history.begin());
	}
	fStats.history.back() += seconds;
}


/*static*/ BuildPhase
Pool::PhaseFor(double progress)
{
	if (progress < 0.08)
		return kFetching;
	if (progress < 0.18)
		return kConfiguring;
	if (progress < 0.88)
		return kCompiling;
	if (progress < 0.97)
		return kPackaging;
	if (progress < 1.0)
		return kSigning;
	return kAwaitingMatch;
}


void
Pool::_UpdateReadiness()
{
	for (size_t i = 0; i < fPackages.size(); i++) {
		Package& package = fPackages[i];
		if (package.state != kWaiting)
			continue;
		bool ready = true;
		for (size_t d = 0; d < package.deps.size(); d++) {
			if (fPackages[package.deps[d]].state != kSealed) {
				ready = false;
				break;
			}
		}
		if (ready)
			package.state = kReady;
	}
}


void
Pool::_Event(EventKind kind, int package, const std::string& text)
{
	Event event;
	event.time = fNow;
	event.kind = kind;
	event.package = package;
	event.text = text;
	fEvents.push_back(event);
	if (fEvents.size() > kMaxEvents)
		fEvents.erase(fEvents.begin());
}


int
Pool::_CopyIndex(const Package& package, int volunteer) const
{
	for (size_t i = 0; i < package.copies.size(); i++) {
		if (package.copies[i].volunteer == volunteer)
			return (int)i;
	}
	return -1;
}


}	// namespace awase
