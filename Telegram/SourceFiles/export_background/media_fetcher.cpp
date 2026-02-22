/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "export_background/media_fetcher.h"

#include "main/main_session.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>

namespace ExportBackground {

struct MediaFetcher::Active {
	MTPInputFileLocation location;
	int dcId = 0;
	int64 size = 0;
	int64 offset = 0;
	QString path;
	QFile file;
	FnMut<void(bool)> done;
};

MediaFetcher::MediaFetcher(not_null<Main::Session*> session)
: _api(&session->mtp()) {
}

MediaFetcher::~MediaFetcher() {
	cancel();
}

void MediaFetcher::download(
		const MTPInputFileLocation &location,
		int dcId,
		int64 size,
		const QString &path,
		FnMut<void(bool success)> done) {
	cancel();

	_active = std::make_unique<Active>();
	_active->location = location;
	_active->dcId = dcId;
	_active->size = size;
	_active->path = path;
	_active->done = std::move(done);

	const auto dir = QFileInfo(path).absoluteDir();
	if (!dir.exists()) {
		dir.mkpath(u"."_q);
	}

	_active->file.setFileName(path);
	if (!_active->file.open(QIODevice::WriteOnly)) {
		finish(false);
		return;
	}

	requestPart();
}

void MediaFetcher::cancel() {
	if (_active) {
		_active->file.close();
		_active.reset();
	}
}

bool MediaFetcher::active() const {
	return _active != nullptr;
}

void MediaFetcher::requestPart() {
	if (!_active) {
		return;
	}
	_api.request(MTPupload_GetFile(
		MTP_flags(0),
		_active->location,
		MTP_long(_active->offset),
		MTP_int(kChunkSize)
	)).done([this](const MTPupload_File &result) {
		if (!_active) {
			return;
		}
		result.match([&](const MTPDupload_file &data) {
			const auto &bytes = data.vbytes().v;
			if (bytes.isEmpty()) {
				_active->file.close();
				finish(true);
				return;
			}
			_active->file.write(bytes);
			_active->offset += bytes.size();
			if (_active->offset >= _active->size) {
				_active->file.close();
				finish(true);
			} else {
				requestPart();
			}
		}, [&](const MTPDupload_fileCdnRedirect &) {
			finish(false);
		});
	}).fail([this](const MTP::Error &) {
		finish(false);
	}).toDC(MTP::ShiftDcId(_active->dcId, 0)).send();
}

void MediaFetcher::finish(bool success) {
	if (!_active) {
		return;
	}
	if (!success) {
		_active->file.close();
		QFile::remove(_active->path);
	}
	auto done = std::move(_active->done);
	_active.reset();
	if (done) {
		done(success);
	}
}

} // namespace ExportBackground
