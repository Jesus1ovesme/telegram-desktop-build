/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"

namespace ExportBackground {

class RateLimiter {
public:
	explicit RateLimiter(crl::time baseDelay);

	void schedule(Fn<void()> callback);
	void handleFloodWait(int seconds);
	void cancel();

private:
	base::Timer _timer;
	crl::time _baseDelay = 0;
	crl::time _nextAllowed = 0;

};

} // namespace ExportBackground
