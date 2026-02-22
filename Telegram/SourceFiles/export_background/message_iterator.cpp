/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "export_background/message_iterator.h"

#include "data/data_peer.h"
#include "data/data_session.h"
#include "main/main_session.h"

namespace ExportBackground {

MessageIterator::MessageIterator(
		not_null<Main::Session*> session,
		uint64 peerId,
		MTPMessagesFilter filter,
		int64 resumeOffsetId)
: _api(&session->mtp())
, _session(session)
, _peerId(peerId)
, _filter(filter)
, _offsetId(resumeOffsetId) {
}

void MessageIterator::requestNextSlice(
		FnMut<void(const MTPmessages_Messages&)> done,
		Fn<void(const MTP::Error&)> fail) {
	if (_finished) {
		return;
	}
	const auto peerId = PeerId(PeerIdHelper(_peerId));
	const auto peer = _session->data().peer(peerId);
	using Flag = MTPmessages_Search::Flag;
	_api.request(MTPmessages_Search(
		MTP_flags(Flag(0)),
		peer->input(),
		MTP_string(),
		MTP_inputPeerEmpty(),
		MTPInputPeer(),
		MTPVector<MTPReaction>(),
		MTP_int(0),
		_filter,
		MTP_int(0),
		MTP_int(0),
		MTP_int(int32(_offsetId)),
		MTP_int(0),
		MTP_int(kSliceLimit),
		MTP_int(0),
		MTP_int(0),
		MTP_long(0)
	)).done([this, callback = std::move(done)](
			const MTPmessages_Messages &result) mutable {
		updatePagination(result);
		callback(result);
	}).fail(std::move(fail)).send();
}

bool MessageIterator::finished() const {
	return _finished;
}

int64 MessageIterator::lastOffsetId() const {
	return _offsetId;
}

void MessageIterator::updatePagination(
		const MTPmessages_Messages &result) {
	auto minId = int64(0);
	auto count = 0;
	const auto extractMinId = [&](const auto &messages) {
		count = int(messages.size());
		for (const auto &message : messages) {
			message.match([&](const auto &data) {
				const auto id = int64(data.vid().v);
				if (!minId || id < minId) {
					minId = id;
				}
			});
		}
	};
	result.match([&](const MTPDmessages_messagesNotModified &) {
		_finished = true;
	}, [&](const MTPDmessages_messages &data) {
		_finished = true;
		extractMinId(data.vmessages().v);
	}, [&](const auto &data) {
		extractMinId(data.vmessages().v);
	});
	if (count > 0 && count < kSliceLimit) {
		_finished = true;
	}
	if (count == 0) {
		_finished = true;
	}
	if (minId) {
		_offsetId = minId;
	}
}

} // namespace ExportBackground
