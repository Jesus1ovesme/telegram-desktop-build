/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "export_background/text_exporter.h"

#include <QtCore/QDateTime>
#include <QtCore/QFile>
#include <QtCore/QTimeZone>

namespace ExportBackground {

struct TextExporter::Files {
	QFile json;
	QFile html;
	bool jsonStarted = false;
};

TextExporter::TextExporter(
		const QString &chatPath,
		bool exportJson,
		bool exportHtml)
: _chatPath(chatPath)
, _exportJson(exportJson)
, _exportHtml(exportHtml) {
}

TextExporter::~TextExporter() {
	finalize();
}

void TextExporter::openFiles() {
	if (_files) {
		return;
	}
	_files = std::make_unique<Files>();
	if (_exportJson) {
		_files->json.setFileName(_chatPath + u"messages.json"_q);
		if (_files->json.open(QIODevice::WriteOnly)) {
			_files->json.write("{\"messages\":[\n");
		}
	}
	if (_exportHtml) {
		_files->html.setFileName(_chatPath + u"messages.html"_q);
		if (_files->html.open(QIODevice::WriteOnly)) {
			_files->html.write(
				"<!DOCTYPE html>\n"
				"<html>\n"
				"<head>\n"
				"<meta charset=\"utf-8\">\n"
				"<title>Messages</title>\n"
				"<style>\n"
				"body { font-family: sans-serif; margin: 20px; }\n"
				".message { margin: 8px 0; padding: 8px; "
				"border-left: 3px solid #ccc; }\n"
				".from { font-weight: bold; }\n"
				".date { color: #888; font-size: 0.85em; "
				"margin-left: 8px; }\n"
				".text { margin-top: 4px; white-space: pre-wrap; }\n"
				".media { margin-top: 4px; color: #069; }\n"
				"</style>\n"
				"</head>\n"
				"<body>\n");
		}
	}
}

void TextExporter::appendMessage(const MessageInfo &message) {
	openFiles();
	if (_exportJson) {
		appendJson(message);
	}
	if (_exportHtml) {
		appendHtml(message);
	}
	++_messageCount;
}

void TextExporter::finalize() {
	if (!_files) {
		return;
	}
	if (_files->json.isOpen()) {
		_files->json.write("\n]}");
		_files->json.close();
	}
	if (_files->html.isOpen()) {
		_files->html.write("</body>\n</html>\n");
		_files->html.close();
	}
	_files.reset();
}

void TextExporter::appendJson(const MessageInfo &message) {
	if (!_files->json.isOpen()) {
		return;
	}
	auto entry = QByteArray();
	if (_files->jsonStarted) {
		entry.append(",\n");
	}
	_files->jsonStarted = true;

	const auto dateStr = formatDateTime(message.date);
	entry.append("{\"id\":");
	entry.append(QByteArray::number(qlonglong(message.id)));
	entry.append(",\"type\":\"message\"");
	entry.append(",\"date\":\"");
	entry.append(dateStr);
	entry.append("\",\"date_unixtime\":\"");
	entry.append(QByteArray::number(message.date));
	entry.append("\",\"from\":\"");
	entry.append(escapeJson(message.fromName));
	entry.append("\",\"text\":\"");
	entry.append(escapeJson(message.text));
	entry.append("\"");
	if (!message.mediaPath.isEmpty()) {
		entry.append(",\"media_path\":\"");
		entry.append(escapeJson(message.mediaPath));
		entry.append("\"");
	}
	entry.append("}");
	_files->json.write(entry);
}

void TextExporter::appendHtml(const MessageInfo &message) {
	if (!_files->html.isOpen()) {
		return;
	}
	auto entry = QByteArray();
	entry.append("<div class=\"message\">\n");
	entry.append(" <div class=\"from\">");
	entry.append(escapeHtml(message.fromName));
	entry.append("<span class=\"date\">");
	entry.append(formatDateTime(message.date));
	entry.append("</span></div>\n");
	if (!message.text.isEmpty()) {
		entry.append(" <div class=\"text\">");
		entry.append(escapeHtml(message.text));
		entry.append("</div>\n");
	}
	if (!message.mediaPath.isEmpty()) {
		entry.append(" <div class=\"media\">[");
		entry.append(escapeHtml(message.mediaPath));
		entry.append("]</div>\n");
	}
	entry.append("</div>\n");
	_files->html.write(entry);
}

QByteArray TextExporter::escapeJson(const QString &text) {
	auto result = text.toUtf8();
	result.replace('\\', "\\\\");
	result.replace('"', "\\\"");
	result.replace('\n', "\\n");
	result.replace('\r', "\\r");
	result.replace('\t', "\\t");
	return result;
}

QByteArray TextExporter::escapeHtml(const QString &text) {
	auto result = text.toUtf8();
	result.replace('&', "&amp;");
	result.replace('<', "&lt;");
	result.replace('>', "&gt;");
	result.replace('"', "&quot;");
	result.replace('\n', "<br>");
	return result;
}

QByteArray TextExporter::formatDateTime(int32 date) {
	return QDateTime::fromSecsSinceEpoch(
		date,
		QTimeZone::utc()
	).toString(Qt::ISODate).toUtf8();
}

} // namespace ExportBackground
