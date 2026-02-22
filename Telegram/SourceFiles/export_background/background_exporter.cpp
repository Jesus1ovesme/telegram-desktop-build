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

namespace ExportBackground {
namespace {

QString PeerName(
		not_null<Main::Session*> session,
		PeerId peerId) {
	const auto peer = session->data().peerLoaded(peerId);
	return peer ? peer->name() : u"Unknown"_q;
}

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
, _config(Config::loadFromFile(Config::defaultConfigPath()))
, _state(_config.basePath + u"exporter_state.json"_q)
, _rateLimiter(_config.rateLimitDelay)
, _mediaFetcher(session) {
}

BackgroundExporter::~BackgroundExporter() {
	stop();
}

void BackgroundExporter::start() {
	if (_running) {
		return;
	}
	if (!_config.validate()) {
		return;
	}

	_running = true;
	_state.load();
	_folders = std::make_unique<FolderOrganizer>(_config.basePath);
	if (!_folders->ensureBaseDirectory()) {
		_running = false;
		return;
	}
	_dialogs = DialogScanner::scan(_session);
	_dialogIndex = -1;

	processNextChat();
}

void BackgroundExporter::stop() {
	if (!_running) {
		return;
	}
	_running = false;
	_mediaFetcher.cancel();
	_rateLimiter.cancel();
	_messageIterator.reset();
	if (_textExporter) {
		_textExporter->finalize();
		_textExporter.reset();
	}
	_state.save();
}

void BackgroundExporter::processNextChat() {
	if (!_running) {
		return;
	}

	_messageIterator.reset();
	if (_textExporter) {
		_textExporter->finalize();
		_textExporter.reset();
	}

	while (++_dialogIndex < int(_dialogs.size())) {
		const auto &dialog = _dialogs[_dialogIndex];
		if (_state.isChatCompleted(dialog.peerId)) {
			continue;
		}

		_folders->ensureChatDirectories(dialog.peerId, dialog.peerName);

		const auto chatPath = _folders->chatPath(
			dialog.peerId,
			dialog.peerName);
		const auto resumeOffset = _state.lastMessageId(dialog.peerId);

		_messageIterator = std::make_unique<MessageIterator>(
			_session,
			dialog.peerId,
			resumeOffset);
		_textExporter = std::make_unique<TextExporter>(
			chatPath,
			_config.exportJson,
			_config.exportHtml);

		processNextSlice();
		return;
	}

	_state.save();
	_running = false;
}

void BackgroundExporter::processNextSlice() {
	if (!_running || !_messageIterator) {
		return;
	}

	if (_messageIterator->finished()) {
		const auto &dialog = _dialogs[_dialogIndex];
		_state.markChatCompleted(dialog.peerId);
		_state.save();

		_rateLimiter.schedule([this] {
			processNextChat();
		});
		return;
	}

	_messageIterator->requestNextSlice(
		[this](const MTPmessages_Messages &result) {
			processSlice(result);
		},
		[this](const MTP::Error &error) {
			if (MTP::IsFloodError(error)) {
				const auto match = QRegularExpression(
					u"^FLOOD_WAIT_(\\d+)$"_q
				).match(error.type());
				if (match.hasMatch()) {
					_rateLimiter.handleFloodWait(
						match.captured(1).toInt());
				}
			}
			_rateLimiter.schedule([this] {
				processNextSlice();
			});
		});
}

void BackgroundExporter::processSlice(
		const MTPmessages_Messages &result) {
	if (!_running) {
		return;
	}

	_mediaQueue.clear();

	const auto processMessages = [&](const auto &messages) {
		for (const auto &message : messages) {
			message.match([&](const MTPDmessage &data) {
				const auto msgId = int64(data.vid().v);
				const auto date = data.vdate().v;
				const auto fromPeerId = data.vfrom_id()
					? peerFromMTP(*data.vfrom_id())
					: PeerId(0);
				const auto fromName = fromPeerId
					? PeerName(_session, fromPeerId)
					: QString();

				_textExporter->appendMessage({
					.id = msgId,
					.date = date,
					.fromName = fromName,
					.text = qs(data.vmessage()),
				});

				if (const auto media = data.vmedia()) {
					const auto &dialog = _dialogs[_dialogIndex];
					media->match([&](
							const MTPDmessageMediaPhoto &photoData) {
						if (!((_config.mediaTypes
								& Config::MediaType::Photo))) {
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
							_mediaQueue.push_back({
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
							if (!((_config.mediaTypes & type))) {
								return;
							}
							const auto fileName = DocumentFileName(
								data,
								msgId);
							const auto path = _folders->mediaPath(
								dialog.peerId,
								dialog.peerName,
								folder) + fileName;
							_mediaQueue.push_back({
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

				const auto &dialog = _dialogs[_dialogIndex];
				_state.updateChatProgress(dialog.peerId, msgId);
			}, [](const auto &) {});
		}
	};

	result.match([&](const MTPDmessages_messagesNotModified &) {
	}, [&](const auto &data) {
		processMessages(data.vmessages().v);
	});

	downloadNextMedia();
}

void BackgroundExporter::downloadNextMedia() {
	if (!_running) {
		return;
	}

	if (_mediaQueue.empty()) {
		sliceFinished();
		return;
	}

	auto task = std::move(_mediaQueue.back());
	_mediaQueue.pop_back();

	_mediaFetcher.download(
		task.location,
		task.dcId,
		task.size,
		task.path,
		[this](bool success) {
			_rateLimiter.schedule([this] {
				downloadNextMedia();
			});
		});
}

void BackgroundExporter::sliceFinished() {
	if (!_running) {
		return;
	}

	_state.save();

	_rateLimiter.schedule([this] {
		processNextSlice();
	});
}

} // namespace ExportBackground
