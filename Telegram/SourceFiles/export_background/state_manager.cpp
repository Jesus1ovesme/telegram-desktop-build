/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "export_background/state_manager.h"

#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

namespace ExportBackground {
namespace {

constexpr auto kStateVersion = 2;

} // namespace

StateManager::StateManager(const QString &path)
: _path(path) {
}

void StateManager::load() {
	_chats.clear();
	auto f = QFile(_path);
	if (!f.open(QIODevice::ReadOnly)) {
		return;
	}
	const auto data = f.readAll();
	const auto json = QJsonDocument::fromJson(data);
	if (!json.isObject()) {
		return;
	}
	const auto object = json.object();
	const auto version = object.value(u"version"_q).toInt();
	if (version != kStateVersion) {
		// Incompatible version, start fresh.
		return;
	}
	deserialize(object);
}

void StateManager::save() const {
	const auto tempPath = _path + u".tmp"_q;
	auto f = QFile(tempPath);
	if (!f.open(QIODevice::WriteOnly)) {
		return;
	}
	f.write(QJsonDocument(serialize()).toJson(QJsonDocument::Indented));
	f.close();
	QFile::remove(_path);
	QFile::rename(tempPath, _path);
}

bool StateManager::isChatCompleted(uint64 peerId) const {
	const auto i = _chats.find(peerId);
	return (i != end(_chats)) && i->second.completed;
}

bool StateManager::isFilterCompleted(
		uint64 peerId,
		int filterIndex) const {
	if (filterIndex < 0 || filterIndex >= kFilterCount) {
		return true;
	}
	const auto i = _chats.find(peerId);
	if (i == end(_chats)) {
		return false;
	}
	return i->second.filters[filterIndex].completed;
}

int64 StateManager::lastMessageId(
		uint64 peerId,
		int filterIndex) const {
	if (filterIndex < 0 || filterIndex >= kFilterCount) {
		return 0;
	}
	const auto i = _chats.find(peerId);
	return (i != end(_chats))
		? i->second.filters[filterIndex].lastMessageId
		: 0;
}

void StateManager::updateFilterProgress(
		uint64 peerId,
		int filterIndex,
		int64 lastMessageId) {
	if (filterIndex < 0 || filterIndex >= kFilterCount) {
		return;
	}
	auto &state = _chats[peerId];
	state.filters[filterIndex].lastMessageId = lastMessageId;
}

void StateManager::markFilterCompleted(
		uint64 peerId,
		int filterIndex) {
	if (filterIndex < 0 || filterIndex >= kFilterCount) {
		return;
	}
	auto &state = _chats[peerId];
	state.filters[filterIndex].completed = true;

	// Check if all filters are completed.
	bool allDone = true;
	for (int i = 0; i < kFilterCount; ++i) {
		if (!state.filters[i].completed) {
			allDone = false;
			break;
		}
	}
	if (allDone) {
		state.completed = true;
	}
}

void StateManager::markChatCompleted(uint64 peerId) {
	auto &state = _chats[peerId];
	state.completed = true;
	for (int i = 0; i < kFilterCount; ++i) {
		state.filters[i].completed = true;
	}
}

void StateManager::reset() {
	_chats.clear();
	QFile::remove(_path);
}

QJsonObject StateManager::serialize() const {
	auto chats = QJsonObject();
	for (const auto &[peerId, state] : _chats) {
		auto chat = QJsonObject();
		chat.insert(u"completed"_q, state.completed);

		auto filtersArr = QJsonArray();
		for (int i = 0; i < kFilterCount; ++i) {
			auto filterObj = QJsonObject();
			filterObj.insert(
				u"last_message_id"_q,
				qint64(state.filters[i].lastMessageId));
			filterObj.insert(
				u"completed"_q,
				state.filters[i].completed);
			filtersArr.append(filterObj);
		}
		chat.insert(u"filters"_q, filtersArr);

		chats.insert(QString::number(peerId), chat);
	}
	auto result = QJsonObject();
	result.insert(u"version"_q, kStateVersion);
	result.insert(u"chats"_q, chats);
	return result;
}

void StateManager::deserialize(const QJsonObject &object) {
	const auto chats = object.value(u"chats"_q).toObject();
	for (auto i = chats.begin(); i != chats.end(); ++i) {
		const auto peerId = i.key().toULongLong();
		if (!peerId) {
			continue;
		}
		const auto chat = i.value().toObject();
		auto state = ChatState();
		state.completed = chat.value(u"completed"_q).toBool();

		const auto filtersArr = chat.value(u"filters"_q).toArray();
		for (int f = 0; f < kFilterCount && f < filtersArr.size(); ++f) {
			const auto filterObj = filtersArr[f].toObject();
			state.filters[f].lastMessageId = int64(
				filterObj.value(u"last_message_id"_q).toDouble());
			state.filters[f].completed = filterObj.value(
				u"completed"_q).toBool();
		}

		_chats.emplace(peerId, state);
	}
}

} // namespace ExportBackground
