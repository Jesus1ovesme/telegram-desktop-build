/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "export_background/rate_limiter.h"

namespace ExportBackground {

RateLimiter::RateLimiter(crl::time baseDelay)
: _baseDelay(baseDelay) {
}

void RateLimiter::schedule(Fn<void()> callback) {
	const auto now = crl::now();
	const auto requestTime = std::max(now, _nextAllowed);
	const auto delay = requestTime - now;
	_nextAllowed = requestTime + _baseDelay;
	if (delay > 0) {
		_timer.setCallback(std::move(callback));
		_timer.callOnce(delay);
	} else {
		callback();
	}
}

void RateLimiter::handleFloodWait(int seconds) {
	_nextAllowed = crl::now() + crl::time(seconds) * 1000 + 10;
}

void RateLimiter::cancel() {
	_timer.cancel();
}

} // namespace ExportBackground
