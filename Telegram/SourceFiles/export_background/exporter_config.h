/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
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
	MediaTypes mediaTypes = MediaType::AllMask;
	crl::time rateLimitDelay = 500;
	crl::time chatSwitchDelay = 3000;
	bool exportJson = true;
	bool exportHtml = true;

	[[nodiscard]] bool validate() const;
	[[nodiscard]] static Config loadFromFile(const QString &path);
	void saveToFile(const QString &path) const;
	[[nodiscard]] static QString defaultConfigPath();
};

} // namespace ExportBackground
