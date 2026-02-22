/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Main {
class Session;
} // namespace Main

namespace ExportBackground {

struct DialogEntry {
	uint64 peerId = 0;
	QString peerName;
};

class DialogScanner {
public:
	[[nodiscard]] static std::vector<DialogEntry> scan(
		not_null<Main::Session*> session);

};

} // namespace ExportBackground
