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

void RateLimiter::enqueue(Fn<void()> callback) {
	_queue.push_back(std::move(callback));
	if (!_processing) {
		processQueue();
	}
}

void RateLimiter::processQueue() {
	if (_queue.empty()) {
		_processing = false;
		return;
	}
	_processing = true;

	const auto now = crl::now();
	const auto requestTime = std::max(now, _nextAllowed);
	const auto delay = requestTime - now;
	_nextAllowed = requestTime + _baseDelay;

	auto callback = std::move(_queue.front());
	_queue.pop_front();

	if (delay > 0) {
		_timer.setCallback([this, cb = std::move(callback)]() mutable {
			cb();
			processQueue();
		});
		_timer.callOnce(delay);
	} else {
		// Use zero-delay timer to avoid deep recursion.
		_timer.setCallback([this, cb = std::move(callback)]() mutable {
			cb();
			processQueue();
		});
		_timer.callOnce(1);
	}
}

void RateLimiter::handleFloodWait(int seconds) {
	_nextAllowed = crl::now() + crl::time(seconds) * 1000 + 10;
}

void RateLimiter::cancel() {
	_queue.clear();
	_timer.cancel();
	_processing = false;
}

} // namespace ExportBackground
