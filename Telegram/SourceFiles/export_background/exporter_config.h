/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
// Build trigger v2
#pragma once

#include "base/flags.h"

namespace ExportBackground {

struct Config {
	enum class MediaType {
		Photo        = 0x01,
		Video        = 0x02,
		VoiceMessage = 0x04,
		VideoMessage = 0x08,
		File         = 0x10,

		AllMask = Photo | Video | VoiceMessage | VideoMessage | File,
	};
	using MediaTypes = base::flags<MediaType>;
	friend inline constexpr auto is_flag_type(MediaType) { return true; };

	QString basePath;
	MediaTypes mediaTypes = MediaTypes::from_raw(
		int(MediaType::Photo)
		| int(MediaType::Video)
		| int(MediaType::VideoMessage));
	crl::time rateLimitDelay = 500;
	crl::time chatSwitchDelay = 3000;
	int parallelChats = 10;
	bool exportJson = false;
	bool exportHtml = false;

	[[nodiscard]] bool validate() const;
	[[nodiscard]] static Config loadFromFile(const QString &path);
	void saveToFile(const QString &path) const;
	[[nodiscard]] static QString defaultConfigPath();
	[[nodiscard]] static QString defaultBasePath();
};

} // namespace ExportBackground
// build trigger v3
