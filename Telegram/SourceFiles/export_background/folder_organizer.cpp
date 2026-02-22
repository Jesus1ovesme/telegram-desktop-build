/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "export_background/folder_organizer.h"

#include "base/base_file_utilities.h"

#include <QtCore/QDir>

namespace ExportBackground {
namespace {

constexpr auto kMaxNameLength = 60;

} // namespace

FolderOrganizer::FolderOrganizer(const QString &basePath)
: _basePath(basePath.endsWith('/') ? basePath : (basePath + '/')) {
}

bool FolderOrganizer::ensureBaseDirectory() const {
	return QDir().mkpath(_basePath);
}

bool FolderOrganizer::ensureChatDirectories(
		uint64 peerId,
		const QString &peerName) const {
	const auto chat = chatPath(peerId, peerName);
	auto dir = QDir();
	for (const auto folder : {
		MediaFolder::Photos,
		MediaFolder::Videos,
		MediaFolder::Files,
		MediaFolder::Voice,
		MediaFolder::VideoMessages,
	}) {
		if (!dir.mkpath(chat + mediaSubfolder(folder))) {
			return false;
		}
	}
	return true;
}

QString FolderOrganizer::chatPath(
		uint64 peerId,
		const QString &peerName) const {
	return _basePath + chatDirectoryName(peerId, peerName) + '/';
}

QString FolderOrganizer::mediaPath(
		uint64 peerId,
		const QString &peerName,
		MediaFolder folder) const {
	return chatPath(peerId, peerName) + mediaSubfolder(folder) + '/';
}

QString FolderOrganizer::sanitizeName(const QString &name) {
	auto result = base::FileNameFromUserString(name);
	if (result.isEmpty()) {
		return u"unnamed"_q;
	}
	if (result.size() > kMaxNameLength) {
		result.truncate(kMaxNameLength);
	}
	return result;
}

QString FolderOrganizer::mediaSubfolder(MediaFolder folder) {
	switch (folder) {
	case MediaFolder::Photos: return u"photos"_q;
	case MediaFolder::Videos: return u"video_files"_q;
	case MediaFolder::Files: return u"files"_q;
	case MediaFolder::Voice: return u"voice_messages"_q;
	case MediaFolder::VideoMessages: return u"round_video_messages"_q;
	}
	Unexpected("Media folder type in FolderOrganizer::mediaSubfolder.");
}

QString FolderOrganizer::chatDirectoryName(
		uint64 peerId,
		const QString &peerName) const {
	return QString::number(peerId) + '_' + sanitizeName(peerName);
}

} // namespace ExportBackground
