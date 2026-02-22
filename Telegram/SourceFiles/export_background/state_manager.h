/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/flat_map.h"

namespace ExportBackground {

class StateManager {
public:
	explicit StateManager(const QString &path);

	void load();
	void save() const;

	[[nodiscard]] bool isChatCompleted(uint64 peerId) const;
	[[nodiscard]] int64 lastMessageId(uint64 peerId) const;

	void updateChatProgress(uint64 peerId, int64 lastMessageId);
	void markChatCompleted(uint64 peerId);
	void reset();

private:
	struct ChatState {
		int64 lastMessageId = 0;
		bool completed = false;
	};

	[[nodiscard]] QJsonObject serialize() const;
	void deserialize(const QJsonObject &object);

	QString _path;
	base::flat_map<uint64, ChatState> _chats;

};

} // namespace ExportBackground
