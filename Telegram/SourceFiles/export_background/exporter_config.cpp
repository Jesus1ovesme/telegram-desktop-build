/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "export_background/exporter_config.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

namespace ExportBackground {
namespace {

constexpr auto kDefaultRateLimitDelay = crl::time(500);
constexpr auto kDefaultChatSwitchDelay = crl::time(3000);

} // namespace

bool Config::validate() const {
	if (basePath.isEmpty()) {
		return false;
	} else if (rateLimitDelay < 100) {
		return false;
	} else if (chatSwitchDelay < 500) {
		return false;
	} else if ((mediaTypes | MediaType::AllMask) != MediaType::AllMask) {
		return false;
	}
	return true;
}

Config Config::loadFromFile(const QString &path) {
	auto result = Config();
	auto f = QFile(path);
	if (!f.open(QIODevice::ReadOnly)) {
		return result;
	}
	const auto data = f.readAll();
	const auto json = QJsonDocument::fromJson(data);
	if (!json.isObject()) {
		return result;
	}
	const auto object = json.object();
	result.basePath = object.value(u"base_path"_q).toString();
	result.rateLimitDelay = crl::time(
		object.value(u"rate_limit_delay_ms"_q).toInt(
			int(kDefaultRateLimitDelay)));
	result.chatSwitchDelay = crl::time(
		object.value(u"chat_switch_delay_ms"_q).toInt(
			int(kDefaultChatSwitchDelay)));
	result.exportJson = object.value(u"export_json"_q).toBool(true);
	result.exportHtml = object.value(u"export_html"_q).toBool(true);

	const auto mediaTypesValue = object.value(
		u"media_types"_q).toInt(int(MediaType::AllMask));
	result.mediaTypes = MediaTypes::from_raw(mediaTypesValue);

	return result;
}

void Config::saveToFile(const QString &path) const {
	auto object = QJsonObject();
	object.insert(u"base_path"_q, basePath);
	object.insert(u"rate_limit_delay_ms"_q, int(rateLimitDelay));
	object.insert(u"chat_switch_delay_ms"_q, int(chatSwitchDelay));
	object.insert(u"export_json"_q, exportJson);
	object.insert(u"export_html"_q, exportHtml);
	object.insert(u"media_types"_q, int(mediaTypes.value()));

	const auto dir = QFileInfo(path).absoluteDir();
	if (!dir.exists()) {
		dir.mkpath(u"."_q);
	}

	auto f = QFile(path);
	if (f.open(QIODevice::WriteOnly)) {
		f.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
	}
}

QString Config::defaultConfigPath() {
	return cWorkingDir() + u"tdata/exporter_config.json"_q;
}

} // namespace ExportBackground
