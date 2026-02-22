/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/sender.h"

namespace Main {
class Session;
} // namespace Main

namespace ExportBackground {

class MessageIterator {
public:
	static constexpr auto kSliceLimit = 100;

	MessageIterator(
		not_null<Main::Session*> session,
		uint64 peerId,
		MTPMessagesFilter filter,
		int64 resumeOffsetId);

	void requestNextSlice(
		FnMut<void(const MTPmessages_Messages&)> done,
		Fn<void(const MTP::Error&)> fail);

	[[nodiscard]] bool finished() const;
	[[nodiscard]] int64 lastOffsetId() const;

private:
	void updatePagination(const MTPmessages_Messages &result);

	MTP::Sender _api;
	not_null<Main::Session*> _session;
	uint64 _peerId = 0;
	MTPMessagesFilter _filter;
	int64 _offsetId = 0;
	bool _finished = false;

};

} // namespace ExportBackground
