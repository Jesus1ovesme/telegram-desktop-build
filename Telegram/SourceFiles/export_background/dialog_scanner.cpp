/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "export_background/dialog_scanner.h"

#include "data/data_peer.h"
#include "data/data_session.h"
#include "dialogs/dialogs_indexed_list.h"
#include "dialogs/dialogs_key.h"
#include "dialogs/dialogs_main_list.h"
#include "dialogs/dialogs_row.h"
#include "main/main_session.h"

namespace ExportBackground {

std::vector<DialogEntry> DialogScanner::scan(
		not_null<Main::Session*> session) {
	auto result = std::vector<DialogEntry>();
	const auto list = session->data().chatsList()->indexed();
	for (const auto &row : list->all()) {
		const auto peer = row->key().peer();
		if (!peer) {
			continue;
		}
		result.push_back({
			.peerId = peer->id.value,
			.peerName = peer->name(),
		});
	}
	return result;
}

} // namespace ExportBackground
