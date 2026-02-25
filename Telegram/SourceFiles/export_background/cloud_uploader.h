/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSet>

namespace ExportBackground {

class CloudUploader final : public QObject {
public:
	explicit CloudUploader(QObject *parent = nullptr);
	~CloudUploader();

	void uploadDirectory(const QString &localDir);
	[[nodiscard]] bool isUploading() const;

private:
	enum class State {
		Idle,
		Authenticating,
		GettingUploadShard,
		Uploading,
		Registering,
	};

	void requestOAuthToken();
	void requestUploadShard();
	void uploadNextFile();
	void registerUploadedFile(
		const QString &hash,
		qint64 size,
		const QString &remotePath);

	[[nodiscard]] QSet<QString> loadUploadedSet(
		const QString &localDir) const;
	void saveUploadedFile(
		const QString &localDir,
		const QString &fileName);
	void collectFiles(const QString &dir, const QString &prefix);

	QNetworkAccessManager *_nam = nullptr;
	State _state = State::Idle;
	QString _accessToken;
	QString _uploadUrl;
	QString _localDir;
	QStringList _pendingFiles;
	int _currentIndex = 0;
};

} // namespace ExportBackground
