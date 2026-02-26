/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "export_background/folder_organizer.h"
#include "base/timer.h"

#include <memory>

namespace Main {
class Session;
} // namespace Main

namespace Data {
class PhotoMedia;
class DocumentMedia;
} // namespace Data

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
		std::shared_ptr<Data::PhotoMedia> mediaView;
		uint64 peerId;
		QString peerName;
	};

	struct PendingDocument {
		not_null<DocumentData*> document;
		std::shared_ptr<Data::DocumentMedia> mediaView;
		uint64 peerId;
		QString peerName;
		MediaFolder folder;
	};

	void onNewItem(not_null<HistoryItem*> item);
	[[nodiscard]] bool trySavePhoto(PendingPhoto &entry);
	[[nodiscard]] bool trySaveDocument(PendingDocument &entry);
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
