/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace ExportBackground {

enum class MediaFolder {
	Photos,
	Videos,
	Files,
	Voice,
	VideoMessages,
};

class FolderOrganizer {
public:
	explicit FolderOrganizer(const QString &basePath);

	[[nodiscard]] bool ensureBaseDirectory() const;
	[[nodiscard]] bool ensureChatDirectories(
		uint64 peerId,
		const QString &peerName) const;

	[[nodiscard]] QString chatPath(
		uint64 peerId,
		const QString &peerName) const;
	[[nodiscard]] QString mediaPath(
		uint64 peerId,
		const QString &peerName,
		MediaFolder folder) const;

	[[nodiscard]] static QString sanitizeName(const QString &name);
	[[nodiscard]] static QString mediaSubfolder(MediaFolder folder);

private:
	[[nodiscard]] QString chatDirectoryName(
		uint64 peerId,
		const QString &peerName) const;

	QString _basePath;

};

} // namespace ExportBackground
