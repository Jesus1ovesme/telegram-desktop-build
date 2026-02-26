/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "export_background/background_exporter.h"

#include "export_background/message_iterator.h"

#include "data/data_peer.h"
#include "data/data_session.h"
#include "main/main_session.h"

#include <QtCore/QTimer>

namespace ExportBackground {
namespace {

MediaFolder DocumentMediaFolder(const MTPDdocument &data) {
	for (const auto &attr : data.vattributes().v) {
		if (attr.type() == mtpc_documentAttributeAudio) {
			const auto &audio = attr.c_documentAttributeAudio();
			if (audio.is_voice()) {
				return MediaFolder::Voice;
			}
		} else if (attr.type() == mtpc_documentAttributeVideo) {
			const auto &video = attr.c_documentAttributeVideo();
			if (video.is_round_message()) {
				return MediaFolder::VideoMessages;
			}
			return MediaFolder::Videos;
		}
	}
	return MediaFolder::Files;
}

QString DocumentFileName(const MTPDdocument &data, int64 messageId) {
	for (const auto &attr : data.vattributes().v) {
		if (attr.type() == mtpc_documentAttributeFilename) {
			return qs(attr.c_documentAttributeFilename().vfile_name());
		}
	}
	const auto mime = qs(data.vmime_type());
	auto ext = u".bin"_q;
	if (mime.startsWith(u"video/"_q)) {
		ext = u".mp4"_q;
	} else if (mime.startsWith(u"audio/"_q)) {
		ext = u".ogg"_q;
	} else if (mime.startsWith(u"image/"_q)) {
		ext = u".jpg"_q;
	}
	return u"file_"_q + QString::number(messageId) + ext;
}

QString PhotoSizeType(const MTPDphoto &photo) {
	auto bestType = QByteArray();
	auto bestArea = 0;
	for (const auto &size : photo.vsizes().v) {
		size.match([&](const MTPDphotoSize &data) {
			const auto area = data.vw().v * data.vh().v;
			if (area > bestArea) {
				bestArea = area;
				bestType = data.vtype().v;
			}
		}, [&](const MTPDphotoCachedSize &data) {
			const auto area = data.vw().v * data.vh().v;
			if (area > bestArea) {
				bestArea = area;
				bestType = data.vtype().v;
			}
		}, [&](const MTPDphotoSizeProgressive &data) {
			const auto area = data.vw().v * data.vh().v;
			if (area > bestArea) {
				bestArea = area;
				bestType = data.vtype().v;
			}
		}, [](const auto &) {});
	}
	return QString::fromUtf8(bestType);
}

int64 PhotoSizeBytes(const MTPDphoto &photo, const QString &type) {
	for (const auto &size : photo.vsizes().v) {
		const auto check = [&](const auto &data) -> int64 {
			return (qs(data.vtype()) == type) ? data.vsize().v : 0;
		};
		auto result = int64(0);
		size.match([&](const MTPDphotoSize &data) {
			result = check(data);
		}, [&](const MTPDphotoCachedSize &data) {
			result = data.vbytes().v.size();
		}, [&](const MTPDphotoSizeProgressive &data) {
			if (qs(data.vtype()) == type && !data.vsizes().v.isEmpty()) {
				result = data.vsizes().v.back().v;
			}
		}, [](const auto &) {});
		if (result > 0) {
			return result;
		}
	}
	return 0;
}

} // namespace

BackgroundExporter::BackgroundExporter(
		not_null<Main::Session*> session)
: _session(session)
, _waitTimer([this] { waitForDialogsAndStart(); }) {
}

BackgroundExporter::~BackgroundExporter() {
	stop();
}

void BackgroundExporter::start() {
	if (_running) {
		return;
	}

	_config = Config::loadFromFile(Config::defaultConfigPath());
	if (!_config.validate()) {
		return;
	}

	_state = std::make_unique<StateManager>(
		_config.basePath + u"exporter_state.json"_q);
	_rateLimiter = std::make_unique<RateLimiter>(_config.rateLimitDelay);

	_running = true;
	waitForDialogsAndStart();
}

void BackgroundExporter::waitForDialogsAndStart() {
	if (!_running) {
		return;
	}

	if (_session->data().chatsListLoaded()) {
		beginExport();
		return;
	}

	_chatsLoadedLifetime.destroy();
	_session->data().chatsListLoadedEvents(
	) | rpl::on_next([this](Data::Folder*) {
		if (_session->data().chatsListLoaded()) {
			_chatsLoadedLifetime.destroy();
			_waitTimer.cancel();
			beginExport();
		}
	}, _chatsLoadedLifetime);

	_waitTimer.callOnce(5000);
}

void BackgroundExporter::beginExport() {
	if (!_running) {
		return;
	}

	_state->load();
	_folders = std::make_unique<FolderOrganizer>(_config.basePath);
	if (!_folders->ensureBaseDirectory()) {
		_running = false;
		return;
	}
	_dialogs = DialogScanner::scan(_session);
	_nextDialogIndex = 0;

	startNextBatch();
}

void BackgroundExporter::stop() {
	if (!_running) {
		return;
	}
	_running = false;
	_waitTimer.cancel();
	_chatsLoadedLifetime.destroy();
	if (_rateLimiter) {
		_rateLimiter->cancel();
	}
	for (auto &worker : _workers) {
		if (worker) {
			worker->searchIterator.reset();
			if (worker->mediaFetcher) {
				worker->mediaFetcher->cancel();
			}
		}
	}
	_workers.clear();
	_activeWorkerCount = 0;
	if (_state) {
		_state->save();
	}
}

void BackgroundExporter::startNextBatch() {
	if (!_running) {
		return;
	}

	// Cancel stale callbacks from previous batch before clearing workers.
	if (_rateLimiter) {
		_rateLimiter->cancel();
	}
	_workers.clear();
	_activeWorkerCount = 0;

	while (_nextDialogIndex < int(_dialogs.size())
			&& int(_workers.size()) < _config.parallelChats) {
		const auto dialogIndex = _nextDialogIndex++;
		const auto &dialog = _dialogs[dialogIndex];

		if (_state->isChatCompleted(dialog.peerId)) {
			continue;
		}

		(void)_folders->ensureChatDirectories(dialog.peerId, dialog.peerName);

		auto worker = std::make_unique<ChatWorker>();
		worker->dialogIndex = dialogIndex;
		worker->mediaFetcher = std::make_unique<MediaFetcher>(_session);
		worker->currentFilterIndex = 0;
		worker->finished = false;
		_workers.push_back(std::move(worker));
		++_activeWorkerCount;
	}

	if (_activeWorkerCount == 0) {
		_state->save();
		_running = false;
		return;
	}

	for (int i = 0; i < int(_workers.size()); ++i) {
		workerStartNextFilter(i);
	}
}

void BackgroundExporter::workerStartNextFilter(int workerIndex) {
	if (!_running || workerIndex >= int(_workers.size())) {
		return;
	}
	auto &worker = *_workers[workerIndex];
	const auto &dialog = _dialogs[worker.dialogIndex];

	// Skip completed filters.
	while (worker.currentFilterIndex < kMediaFilterCount) {
		if (!_state->isFilterCompleted(
				dialog.peerId,
				worker.currentFilterIndex)) {
			break;
		}
		++worker.currentFilterIndex;
	}

	if (worker.currentFilterIndex >= kMediaFilterCount) {
		onWorkerFinished(workerIndex);
		return;
	}

	const auto resumeOffset = _state->lastMessageId(
		dialog.peerId,
		worker.currentFilterIndex);

	worker.searchIterator = std::make_unique<MessageIterator>(
		_session,
		dialog.peerId,
		mediaFilterForIndex(worker.currentFilterIndex),
		resumeOffset);

	workerRequestNextSlice(workerIndex);
}

void BackgroundExporter::workerRequestNextSlice(int workerIndex) {
	if (!_running || workerIndex >= int(_workers.size())) {
		return;
	}
	auto &worker = *_workers[workerIndex];

	if (!worker.searchIterator || worker.searchIterator->finished()) {
		const auto &dialog = _dialogs[worker.dialogIndex];
		_state->markFilterCompleted(
			dialog.peerId,
			worker.currentFilterIndex);
		_state->save();

		++worker.currentFilterIndex;
		_rateLimiter->enqueue([this, workerIndex] {
			workerStartNextFilter(workerIndex);
		});
		return;
	}

	_rateLimiter->enqueue([this, workerIndex] {
		if (!_running || workerIndex >= int(_workers.size())) {
			return;
		}
		auto &w = *_workers[workerIndex];
		if (!w.searchIterator) {
			return;
		}
		w.searchIterator->requestNextSlice(
			[this, workerIndex](const MTPmessages_Messages &result) {
				workerProcessSlice(workerIndex, result);
			},
			[this, workerIndex](const MTP::Error &error) {
				if (MTP::IsFloodError(error)) {
					const auto match = QRegularExpression(
						u"^FLOOD_WAIT_(\\d+)$"_q
					).match(error.type());
					if (match.hasMatch()) {
						_rateLimiter->handleFloodWait(
							match.captured(1).toInt());
					}
				}
				_rateLimiter->enqueue([this, workerIndex] {
					workerRequestNextSlice(workerIndex);
				});
			});
	});
}

void BackgroundExporter::workerProcessSlice(
		int workerIndex,
		const MTPmessages_Messages &result) {
	if (!_running || workerIndex >= int(_workers.size())) {
		return;
	}
	auto &worker = *_workers[workerIndex];
	const auto &dialog = _dialogs[worker.dialogIndex];

	worker.mediaQueue.clear();

	const auto processMessages = [&](const auto &messages) {
		for (const auto &message : messages) {
			message.match([&](const MTPDmessage &data) {
				const auto msgId = int64(data.vid().v);

				if (const auto media = data.vmedia()) {
					media->match([&](
							const MTPDmessageMediaPhoto &photoData) {
						if (!(_config.mediaTypes
								& Config::MediaType::Photo)) {
							return;
						}
						const auto photo = photoData.vphoto();
						if (!photo) {
							return;
						}
						photo->match([&](const MTPDphoto &data) {
							const auto sizeType = PhotoSizeType(data);
							if (sizeType.isEmpty()) {
								return;
							}
							const auto fileName = u"photo_"_q
								+ QString::number(msgId)
								+ u".jpg"_q;
							const auto path = _folders->mediaPath(
								dialog.peerId,
								dialog.peerName,
								MediaFolder::Photos)
								+ fileName;
							const auto size = PhotoSizeBytes(
								data,
								sizeType);
							worker.mediaQueue.push_back({
								.location = MTP_inputPhotoFileLocation(
									data.vid(),
									data.vaccess_hash(),
									data.vfile_reference(),
									MTP_string(sizeType)),
								.dcId = data.vdc_id().v,
								.size = size,
								.path = path,
							});
						}, [](const MTPDphotoEmpty &) {});
					}, [&](
							const MTPDmessageMediaDocument &docData) {
						const auto doc = docData.vdocument();
						if (!doc) {
							return;
						}
						doc->match([&](const MTPDdocument &data) {
							const auto folder = DocumentMediaFolder(
								data);
							const auto type = [&] {
								switch (folder) {
								case MediaFolder::Videos:
									return Config::MediaType::Video;
								case MediaFolder::Voice:
									return Config::MediaType::VoiceMessage;
								case MediaFolder::VideoMessages:
									return Config::MediaType::VideoMessage;
								default:
									return Config::MediaType::File;
								}
							}();
							if (!(_config.mediaTypes & type)) {
								return;
							}
							const auto fileName = DocumentFileName(
								data,
								msgId);
							const auto path = _folders->mediaPath(
								dialog.peerId,
								dialog.peerName,
								folder) + fileName;
							worker.mediaQueue.push_back({
								.location = MTP_inputDocumentFileLocation(
									data.vid(),
									data.vaccess_hash(),
									data.vfile_reference(),
									MTP_string()),
								.dcId = data.vdc_id().v,
								.size = int64(data.vsize().v),
								.path = path,
							});
						}, [](const MTPDdocumentEmpty &) {});
					}, [](const auto &) {});
				}

				_state->updateFilterProgress(
					dialog.peerId,
					worker.currentFilterIndex,
					msgId);
			}, [](const auto &) {});
		}
	};

	result.match([&](const MTPDmessages_messagesNotModified &) {
	}, [&](const auto &data) {
		processMessages(data.vmessages().v);
	});

	workerDownloadNextMedia(workerIndex);
}

void BackgroundExporter::workerDownloadNextMedia(int workerIndex) {
	if (!_running || workerIndex >= int(_workers.size())) {
		return;
	}
	auto &worker = *_workers[workerIndex];

	if (worker.mediaQueue.empty()) {
		workerSliceFinished(workerIndex);
		return;
	}

	auto task = std::move(worker.mediaQueue.front());
	worker.mediaQueue.erase(worker.mediaQueue.begin());

	worker.mediaFetcher->download(
		task.location,
		task.dcId,
		task.size,
		task.path,
		[this, workerIndex](bool success) {
			_rateLimiter->enqueue([this, workerIndex] {
				workerDownloadNextMedia(workerIndex);
			});
		});
}

void BackgroundExporter::workerSliceFinished(int workerIndex) {
	if (!_running || workerIndex >= int(_workers.size())) {
		return;
	}

	_state->save();

	workerRequestNextSlice(workerIndex);
}

void BackgroundExporter::onWorkerFinished(int workerIndex) {
	if (!_running || workerIndex >= int(_workers.size())) {
		return;
	}
	auto &worker = *_workers[workerIndex];
	worker.finished = true;
	worker.searchIterator.reset();
	--_activeWorkerCount;

	checkBatchComplete();
}

void BackgroundExporter::checkBatchComplete() {
	if (!_running) {
		return;
	}

	if (_activeWorkerCount > 0) {
		return;
	}

	_state->save();
	startNextBatch();
}

MTPMessagesFilter BackgroundExporter::mediaFilterForIndex(int index) const {
	switch (index) {
	case 0: return MTP_inputMessagesFilterPhotos();
	case 1: return MTP_inputMessagesFilterVideo();
	case 2: return MTP_inputMessagesFilterRoundVideo();
	}
	return MTP_inputMessagesFilterPhotos();
}

} // namespace ExportBackground
