/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "export_background/folder_organizer.h"
#include "base/timer.h"

namespace Main {
class Session;
} // namespace Main

class PhotoData;
class DocumentData;
class HistoryItem;

namespace ExportBackground {

class SpoilerMediaSaver {
public:
	explicit SpoilerMediaSaver(not_null<Main::Session*> session);
	~SpoilerMediaSaver();

private:
	struct PendingPhoto {
		not_null<PhotoData*> photo;
		uint64 peerId;
		QString peerName;
	};

	struct PendingDocument {
		not_null<DocumentData*> document;
		uint64 peerId;
		QString peerName;
		MediaFolder folder;
	};

	void onNewItem(not_null<HistoryItem*> item);
	void trySavePhoto(const PendingPhoto &entry);
	void trySaveDocument(const PendingDocument &entry);
	void checkPending();

	[[nodiscard]] static MediaFolder folderForDocument(
		not_null<DocumentData*> doc);
	[[nodiscard]] static QString fileExtension(
		not_null<DocumentData*> doc);

	not_null<Main::Session*> _session;
	FolderOrganizer _folders;

	std::vector<PendingPhoto> _pendingPhotos;
	std::vector<PendingDocument> _pendingDocs;
	base::Timer _checkTimer;

	rpl::lifetime _lifetime;
};

} // namespace ExportBackground
