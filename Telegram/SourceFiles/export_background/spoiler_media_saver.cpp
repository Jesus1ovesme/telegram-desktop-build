/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "export_background/spoiler_media_saver.h"

#include "main/main_session.h"
#include "data/data_session.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_media_types.h"
#include "history/history_item.h"
#include "history/history.h"
#include "core/application.h"
#include "base/debug_log.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QDateTime>

namespace ExportBackground {

SpoilerMediaSaver::SpoilerMediaSaver(not_null<Main::Session*> session)
: _session(session)
, _folders(cWorkingDir() + u"tdata/exports/"_q)
, _checkTimer([=] { checkPending(); }) {
	_folders.ensureBaseDirectory();

	_session->data().newItemAdded(
	) | rpl::on_next([=](not_null<HistoryItem*> item) {
		onNewItem(item);
	}, _lifetime);
}

SpoilerMediaSaver::~SpoilerMediaSaver() = default;

void SpoilerMediaSaver::onNewItem(not_null<HistoryItem*> item) {
	const auto media = item->media();
	if (!media) {
		return;
	}

	const auto isSpoiler = media->hasSpoiler();
	const auto isTtl = (media->ttlSeconds() > 0);

	if (!isSpoiler && !isTtl) {
		return;
	}

	const auto peer = item->history()->peer;
	const auto peerId = peer->id.value;
	const auto peerName = peer->name();

	if (const auto photo = media->photo()) {
		_folders.ensureChatDirectories(peerId, peerName);

		auto entry = PendingPhoto{
			.photo = photo,
			.mediaView = photo->createMediaView(),
			.peerId = peerId,
			.peerName = peerName,
		};

		if (!trySavePhoto(entry)) {
			entry.photo->load(
				Data::FileOrigin(),
				LoadFromCloudOrLocal,
				true);
			_pendingPhotos.push_back(std::move(entry));
			if (!_checkTimer.isActive()) {
				_checkTimer.callEach(500);
			}
		}
		return;
	}

	if (const auto document = media->document()) {
		const auto folder = folderForDocument(document);
		_folders.ensureChatDirectories(peerId, peerName);

		auto entry = PendingDocument{
			.document = document,
			.mediaView = document->createMediaView(),
			.peerId = peerId,
			.peerName = peerName,
			.folder = folder,
		};

		if (!trySaveDocument(entry)) {
			document->save(Data::FileOrigin(), QString());
			_pendingDocs.push_back(std::move(entry));
			if (!_checkTimer.isActive()) {
				_checkTimer.callEach(500);
			}
		}
	}
}

bool SpoilerMediaSaver::trySavePhoto(PendingPhoto &entry) {
	if (!entry.mediaView->loaded()) {
		return false;
	}

	const auto path = _folders.mediaPath(
		entry.peerId,
		entry.peerName,
		MediaFolder::Photos);
	const auto timestamp = QDateTime::currentDateTime().toString(
		u"yyyyMMdd_HHmmss"_q);
	const auto destPath = path + timestamp
		+ u"_"_q + QString::number(entry.photo->id)
		+ u".jpg"_q;

	if (!QFileInfo::exists(destPath)) {
		if (entry.mediaView->saveToFile(destPath)) {
			LOG(("SpoilerSaver: Saved photo to %1").arg(destPath));
		}
	}
	return true;
}

bool SpoilerMediaSaver::trySaveDocument(PendingDocument &entry) {
	const auto doc = entry.document;
	const auto filePath = doc->filepath(true);
	const auto bytes = entry.mediaView->bytes();

	if (filePath.isEmpty() && bytes.isEmpty()) {
		return false;
	}

	const auto path = _folders.mediaPath(
		entry.peerId,
		entry.peerName,
		entry.folder);
	const auto timestamp = QDateTime::currentDateTime().toString(
		u"yyyyMMdd_HHmmss"_q);
	const auto ext = fileExtension(doc);
	const auto destPath = path + timestamp
		+ u"_"_q + QString::number(doc->id)
		+ (ext.isEmpty() ? QString() : (u"."_q + ext));

	if (QFileInfo::exists(destPath)) {
		return true;
	}

	if (!filePath.isEmpty()) {
		QFile::copy(filePath, destPath);
		LOG(("SpoilerSaver: Copied document to %1").arg(destPath));
	} else {
		auto file = QFile(destPath);
		if (file.open(QIODevice::WriteOnly)) {
			file.write(bytes);
			LOG(("SpoilerSaver: Wrote document to %1").arg(destPath));
		}
	}
	return true;
}

void SpoilerMediaSaver::checkPending() {
	for (auto i = _pendingPhotos.begin(); i != _pendingPhotos.end();) {
		if (trySavePhoto(*i)) {
			i = _pendingPhotos.erase(i);
		} else {
			++i;
		}
	}

	for (auto i = _pendingDocs.begin(); i != _pendingDocs.end();) {
		if (trySaveDocument(*i)) {
			i = _pendingDocs.erase(i);
		} else {
			++i;
		}
	}

	if (_pendingPhotos.empty() && _pendingDocs.empty()) {
		_checkTimer.cancel();
	}
}

MediaFolder SpoilerMediaSaver::folderForDocument(
		not_null<DocumentData*> doc) {
	if (doc->isVideoMessage()) {
		return MediaFolder::VideoMessages;
	} else if (doc->isVoiceMessage()) {
		return MediaFolder::Voice;
	} else if (doc->isVideoFile() || doc->isAnimation()) {
		return MediaFolder::Videos;
	}
	return MediaFolder::Files;
}

QString SpoilerMediaSaver::fileExtension(not_null<DocumentData*> doc) {
	const auto name = doc->filename();
	if (!name.isEmpty()) {
		const auto ext = QFileInfo(name).suffix();
		if (!ext.isEmpty()) {
			return ext;
		}
	}
	if (doc->isVideoMessage() || doc->isVideoFile() || doc->isAnimation()) {
		return u"mp4"_q;
	} else if (doc->isVoiceMessage()) {
		return u"ogg"_q;
	}
	return QString();
}

} // namespace ExportBackground
