/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"

#include <deque>

namespace ExportBackground {

class RateLimiter {
public:
	explicit RateLimiter(crl::time baseDelay);

	void enqueue(Fn<void()> callback);
	void handleFloodWait(int seconds);
	void cancel();

private:
	void processQueue();

	base::Timer _timer;
	std::deque<Fn<void()>> _queue;
	crl::time _baseDelay = 0;
	crl::time _nextAllowed = 0;
	bool _processing = false;

};

} // namespace ExportBackground
