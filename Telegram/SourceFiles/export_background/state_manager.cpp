/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "export_background/state_manager.h"

#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

namespace ExportBackground {
namespace {

constexpr auto kStateVersion = 1;

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
	if (object.value(u"version"_q).toInt() != kStateVersion) {
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

int64 StateManager::lastMessageId(uint64 peerId) const {
	const auto i = _chats.find(peerId);
	return (i != end(_chats)) ? i->second.lastMessageId : 0;
}

void StateManager::updateChatProgress(
		uint64 peerId,
		int64 lastMessageId) {
	auto &state = _chats[peerId];
	state.lastMessageId = lastMessageId;
}

void StateManager::markChatCompleted(uint64 peerId) {
	auto &state = _chats[peerId];
	state.completed = true;
}

void StateManager::reset() {
	_chats.clear();
	QFile::remove(_path);
}

QJsonObject StateManager::serialize() const {
	auto chats = QJsonObject();
	for (const auto &[peerId, state] : _chats) {
		auto chat = QJsonObject();
		chat.insert(u"last_message_id"_q, qint64(state.lastMessageId));
		chat.insert(u"completed"_q, state.completed);
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
		state.lastMessageId = int64(
			chat.value(u"last_message_id"_q).toDouble());
		state.completed = chat.value(u"completed"_q).toBool();
		_chats.emplace(peerId, state);
	}
}

} // namespace ExportBackground
