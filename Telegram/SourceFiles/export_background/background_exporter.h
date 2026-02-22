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

	struct ChatWorker {
		int dialogIndex = -1;
		std::unique_ptr<MessageIterator> searchIterator;
		std::unique_ptr<MediaFetcher> mediaFetcher;
		std::vector<MediaTask> mediaQueue;
		int currentFilterIndex = 0;
		bool finished = false;
	};

	static constexpr auto kMediaFilterCount = 3;

	void waitForDialogsAndStart();
	void beginExport();

	void startNextBatch();
	void workerStartNextFilter(int workerIndex);
	void workerRequestNextSlice(int workerIndex);
	void workerProcessSlice(
		int workerIndex,
		const MTPmessages_Messages &result);
	void workerDownloadNextMedia(int workerIndex);
	void workerSliceFinished(int workerIndex);
	void onWorkerFinished(int workerIndex);
	void checkBatchComplete();

	[[nodiscard]] MTPMessagesFilter mediaFilterForIndex(int index) const;

	not_null<Main::Session*> _session;
	Config _config;
	std::unique_ptr<StateManager> _state;
	std::unique_ptr<FolderOrganizer> _folders;
	std::unique_ptr<RateLimiter> _rateLimiter;

	std::vector<DialogEntry> _dialogs;
	std::vector<std::unique_ptr<ChatWorker>> _workers;
	int _nextDialogIndex = 0;
	int _activeWorkerCount = 0;

	bool _running = false;
	base::Timer _waitTimer;
	rpl::lifetime _chatsLoadedLifetime;

};

} // namespace ExportBackground
