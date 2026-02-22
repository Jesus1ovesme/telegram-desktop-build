/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "export_background/dialog_scanner.h"
#include "export_background/exporter_config.h"
#include "export_background/folder_organizer.h"
#include "export_background/media_fetcher.h"
#include "export_background/rate_limiter.h"
#include "export_background/state_manager.h"
#include "export_background/text_exporter.h"

#include "base/timer.h"

namespace Main {
class Session;
} // namespace Main

namespace ExportBackground {

class MessageIterator;

class BackgroundExporter {
public:
	explicit BackgroundExporter(not_null<Main::Session*> session);
	~BackgroundExporter();

	void start();
	void stop();

private:
	struct MediaTask {
		MTPInputFileLocation location;
		int dcId = 0;
		int64 size = 0;
		QString path;
	};

	void waitForDialogsAndStart();
	void beginExport();
	void processNextChat();
	void processNextSlice();
	void processSlice(const MTPmessages_Messages &result);
	void downloadNextMedia();
	void sliceFinished();

	not_null<Main::Session*> _session;
	Config _config;
	std::unique_ptr<StateManager> _state;
	std::unique_ptr<FolderOrganizer> _folders;
	std::unique_ptr<RateLimiter> _rateLimiter;
	MediaFetcher _mediaFetcher;

	std::vector<DialogEntry> _dialogs;
	int _dialogIndex = -1;

	std::unique_ptr<MessageIterator> _messageIterator;
	std::unique_ptr<TextExporter> _textExporter;
	std::vector<MediaTask> _mediaQueue;

	bool _running = false;
	base::Timer _waitTimer;
	rpl::lifetime _chatsLoadedLifetime;

};

} // namespace ExportBackground
