/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "export_background/cloud_uploader.h"

#include "core/application.h"
#include "core/core_settings.h"
#include "base/debug_log.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUrlQuery>
#include <QNetworkCookieJar>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>

namespace ExportBackground {
namespace {

constexpr auto kOAuthUrl = "https://o2.mail.ru/token";
constexpr auto kDispatcherUrl = "https://dispatcher.cloud.mail.ru/u";
constexpr auto kFileAddUrl = "https://cloud.mail.ru/api/v2/file/add";
constexpr auto kOAuthClientId = "cloud-win";
constexpr auto kUploadedListName = ".cloud_uploaded";

// Hardcoded credentials
constexpr auto kDefaultEmail = "Z333666@mail.ru";
constexpr auto kDefaultPassword = "X2hsBQxmsH3LmrKU2Sfd";
constexpr auto kDefaultFolder = "/TelegramExport";

} // namespace

CloudUploader::CloudUploader(QObject *parent)
: QObject(parent)
, _nam(new QNetworkAccessManager(this)) {
}

CloudUploader::~CloudUploader() = default;

bool CloudUploader::isUploading() const {
	return _state != State::Idle;
}

void CloudUploader::collectFiles(
		const QString &dir,
		const QString &prefix) {
	const auto uploaded = loadUploadedSet(_localDir);
	QDir d(dir);

	const auto files = d.entryInfoList(
		QDir::Files | QDir::NoDotAndDotDot);
	for (const auto &f : files) {
		if (f.fileName() == kUploadedListName) {
			continue;
		}
		const auto relPath = prefix.isEmpty()
			? f.fileName()
			: (prefix + "/" + f.fileName());
		if (!uploaded.contains(relPath)) {
			_pendingFiles.append(relPath);
		}
	}

	const auto subdirs = d.entryInfoList(
		QDir::Dirs | QDir::NoDotAndDotDot);
	for (const auto &sub : subdirs) {
		const auto subPrefix = prefix.isEmpty()
			? sub.fileName()
			: (prefix + "/" + sub.fileName());
		collectFiles(sub.absoluteFilePath(), subPrefix);
	}
}

void CloudUploader::uploadDirectory(const QString &localDir) {
	if (_state != State::Idle) {
		return;
	}

	_localDir = localDir;
	_currentIndex = 0;
	_pendingFiles.clear();

	collectFiles(localDir, QString());

	if (_pendingFiles.isEmpty()) {
		LOG(("CloudUploader: No new files to upload."));
		return;
	}

	LOG(("CloudUploader: %1 new files to upload."
		).arg(_pendingFiles.size()));
	requestOAuthToken();
}

void CloudUploader::requestOAuthToken() {
	_state = State::Authenticating;

	const auto &settings = Core::App().settings();
	const auto email = settings.cloudEmail().isEmpty()
		? QString::fromLatin1(kDefaultEmail)
		: settings.cloudEmail();
	const auto password = settings.cloudPassword().isEmpty()
		? QString::fromLatin1(kDefaultPassword)
		: settings.cloudPassword();

	QNetworkRequest req(QUrl(QString::fromLatin1(kOAuthUrl)));
	req.setHeader(
		QNetworkRequest::ContentTypeHeader,
		"application/x-www-form-urlencoded");

	QUrlQuery params;
	params.addQueryItem("grant_type", "password");
	params.addQueryItem("username", email);
	params.addQueryItem("password", password);
	params.addQueryItem("client_id", kOAuthClientId);

	auto *reply = _nam->post(
		req,
		params.toString(QUrl::FullyEncoded).toUtf8());
	connect(reply, &QNetworkReply::finished, this, [=] {
		reply->deleteLater();
		if (reply->error() != QNetworkReply::NoError) {
			LOG(("CloudUploader: OAuth failed: %1"
				).arg(reply->errorString()));
			_state = State::Idle;
			return;
		}
		const auto data = reply->readAll();
		const auto doc = QJsonDocument::fromJson(data);
		_accessToken = doc.object()["access_token"].toString();

		if (_accessToken.isEmpty()) {
			LOG(("CloudUploader: Empty access token."));
			_state = State::Idle;
			return;
		}
		_tokenObtainedAt = QDateTime::currentMSecsSinceEpoch();
		_authRetries = 0;
		LOG(("CloudUploader: OAuth OK."));
		requestUploadShard();
	});
}

void CloudUploader::requestUploadShard() {
	_state = State::GettingUploadShard;

	QNetworkRequest req(QUrl(QString::fromLatin1(kDispatcherUrl)));
	req.setRawHeader(
		"Authorization",
		("Bearer " + _accessToken).toUtf8());

	auto *reply = _nam->get(req);
	connect(reply, &QNetworkReply::finished, this, [=] {
		reply->deleteLater();
		if (reply->error() != QNetworkReply::NoError) {
			LOG(("CloudUploader: Dispatcher failed: %1"
				).arg(reply->errorString()));
			_state = State::Idle;
			return;
		}
		// Response: "URL IP COUNT"
		const auto response = QString::fromUtf8(
			reply->readAll()).trimmed();
		_uploadUrl = response.split(' ').value(0);

		if (_uploadUrl.isEmpty()) {
			LOG(("CloudUploader: No upload URL."));
			_state = State::Idle;
			return;
		}
		LOG(("CloudUploader: Upload URL: %1").arg(_uploadUrl));

		_currentIndex = 0;
		uploadNextFile();
	});
}

void CloudUploader::uploadNextFile() {
	if (_currentIndex >= _pendingFiles.size()) {
		LOG(("CloudUploader: All %1 files uploaded."
			).arg(_pendingFiles.size()));
		_state = State::Idle;
		return;
	}

	// Re-authenticate if token is older than 50 minutes.
	constexpr auto kTokenLifetimeMs = qint64(50) * 60 * 1000;
	if (_tokenObtainedAt > 0
		&& (QDateTime::currentMSecsSinceEpoch() - _tokenObtainedAt)
			> kTokenLifetimeMs) {
		LOG(("CloudUploader: Token expired, re-authenticating."));
		requestOAuthToken();
		return;
	}

	_state = State::Uploading;
	const auto &fileName = _pendingFiles[_currentIndex];
	const auto filePath = _localDir + "/" + fileName;

	auto *file = new QFile(filePath);
	if (!file->open(QIODevice::ReadOnly)) {
		LOG(("CloudUploader: Cannot open: %1").arg(filePath));
		delete file;
		_currentIndex++;
		uploadNextFile();
		return;
	}

	const auto fileSize = file->size();

	QNetworkRequest req(QUrl(_uploadUrl));
	req.setRawHeader(
		"Authorization",
		("Bearer " + _accessToken).toUtf8());
	req.setHeader(
		QNetworkRequest::ContentTypeHeader,
		"application/octet-stream");
	req.setHeader(
		QNetworkRequest::ContentLengthHeader,
		fileSize);

	// Stream file directly — QNetworkAccessManager reads from QIODevice
	// without loading the entire file into memory.
	auto *reply = _nam->put(req, file);
	file->setParent(reply); // Auto-delete file when reply finishes.
	connect(reply, &QNetworkReply::finished, this, [=] {
		reply->deleteLater();
		const auto &fn = _pendingFiles[_currentIndex];

		if (reply->error() != QNetworkReply::NoError) {
			LOG(("CloudUploader: Upload failed for %1: %2"
				).arg(fn, reply->errorString()));
			// Re-auth on 401, re-get shard on other errors.
			const auto status = reply->attribute(
				QNetworkRequest::HttpStatusCodeAttribute).toInt();
			if (status == 401) {
				retryWithReAuth();
			} else {
				_currentIndex++;
				uploadNextFile();
			}
			return;
		}

		const auto hash = QString::fromUtf8(
			reply->readAll()).trimmed();
		if (hash.isEmpty()) {
			LOG(("CloudUploader: Empty hash for %1").arg(fn));
			_currentIndex++;
			uploadNextFile();
			return;
		}

		const auto &settings = Core::App().settings();
		const auto folder = settings.cloudFolder().isEmpty()
			? QString::fromLatin1(kDefaultFolder)
			: settings.cloudFolder();
		const auto remotePath = folder + "/" + fn;
		registerUploadedFile(hash, fileSize, remotePath);
	});
}

void CloudUploader::registerUploadedFile(
		const QString &hash,
		qint64 size,
		const QString &remotePath) {
	_state = State::Registering;

	QNetworkRequest req(QUrl(QString::fromLatin1(kFileAddUrl)));
	req.setHeader(
		QNetworkRequest::ContentTypeHeader,
		"application/x-www-form-urlencoded");
	req.setRawHeader(
		"Authorization",
		("Bearer " + _accessToken).toUtf8());

	QUrlQuery params;
	params.addQueryItem("home", remotePath);
	params.addQueryItem("hash", hash);
	params.addQueryItem("size", QString::number(size));
	params.addQueryItem("conflict", "rename");

	auto *reply = _nam->post(
		req,
		params.toString(QUrl::FullyEncoded).toUtf8());
	connect(reply, &QNetworkReply::finished, this, [=] {
		reply->deleteLater();
		const auto &fn = _pendingFiles[_currentIndex];

		if (reply->error() != QNetworkReply::NoError) {
			LOG(("CloudUploader: Register failed for %1: %2"
				).arg(fn, reply->errorString()));
			const auto status = reply->attribute(
				QNetworkRequest::HttpStatusCodeAttribute).toInt();
			if (status == 401) {
				retryWithReAuth();
				return;
			}
		} else {
			LOG(("CloudUploader: Done %1 (%2/%3)"
				).arg(fn)
				.arg(_currentIndex + 1)
				.arg(_pendingFiles.size()));
			saveUploadedFile(_localDir, fn);
		}

		_currentIndex++;
		uploadNextFile();
	});
}

void CloudUploader::retryWithReAuth() {
	if (_authRetries >= 2) {
		LOG(("CloudUploader: Max re-auth retries reached, stopping."));
		_state = State::Idle;
		return;
	}
	++_authRetries;
	LOG(("CloudUploader: Re-authenticating (attempt %1)."
		).arg(_authRetries));
	requestOAuthToken();
}

QSet<QString> CloudUploader::loadUploadedSet(
		const QString &localDir) const {
	QSet<QString> result;
	QFile file(localDir + "/" + kUploadedListName);
	if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
		while (!file.atEnd()) {
			const auto line = QString::fromUtf8(
				file.readLine()).trimmed();
			if (!line.isEmpty()) {
				result.insert(line);
			}
		}
	}
	return result;
}

void CloudUploader::saveUploadedFile(
		const QString &localDir,
		const QString &fileName) {
	QFile file(localDir + "/" + kUploadedListName);
	if (file.open(QIODevice::Append | QIODevice::Text)) {
		file.write((fileName + "\n").toUtf8());
	}
}

} // namespace ExportBackground
