/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace ExportBackground {

struct MessageInfo {
	int64 id = 0;
	int32 date = 0;
	QString fromName;
	QString text;
	QString mediaPath;
};

class TextExporter {
public:
	TextExporter(
		const QString &chatPath,
		bool exportJson,
		bool exportHtml);
	~TextExporter();

	void appendMessage(const MessageInfo &message);
	void finalize();

private:
	void openFiles();
	void appendJson(const MessageInfo &message);
	void appendHtml(const MessageInfo &message);

	[[nodiscard]] static QByteArray escapeJson(const QString &text);
	[[nodiscard]] static QByteArray escapeHtml(const QString &text);
	[[nodiscard]] static QByteArray formatDateTime(int32 date);

	QString _chatPath;
	bool _exportJson = false;
	bool _exportHtml = false;

	struct Files;
	std::unique_ptr<Files> _files;
	int _messageCount = 0;

};

} // namespace ExportBackground
