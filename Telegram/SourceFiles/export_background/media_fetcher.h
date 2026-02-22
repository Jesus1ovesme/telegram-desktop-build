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

class MediaFetcher {
public:
	static constexpr auto kChunkSize = 128 * 1024;

	explicit MediaFetcher(not_null<Main::Session*> session);
	~MediaFetcher();

	void download(
		const MTPInputFileLocation &location,
		int dcId,
		int64 size,
		const QString &path,
		FnMut<void(bool success)> done);

	void cancel();
	[[nodiscard]] bool active() const;

private:
	struct Active;

	void requestPart();
	void finish(bool success);

	MTP::Sender _api;
	std::unique_ptr<Active> _active;

};

} // namespace ExportBackground
