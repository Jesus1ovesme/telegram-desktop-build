/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "export_background/cloud_uploader.h"

#include "base/debug_log.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrlQuery>
#include <QNetworkCookieJar>

namespace ExportBackground {
namespace {

constexpr auto kAuthUrl = "https://auth.mail.ru/cgi-bin/auth";
constexpr auto kCsrfUrl = "https://cloud.mail.ru/api/v2/tokens/csrf";
constexpr auto kDispatcherUrl = "https://cloud.mail.ru/api/v2/dispatcher";
constexpr auto kFileAddUrl = "https://cloud.mail.ru/api/v2/file/add";
constexpr auto kUploadedListName = ".cloud_uploaded";

} // namespace

CloudUploader::CloudUploader(QObject *parent)
: QObject(parent)
, _nam(new QNetworkAccessManager(this)) {
	_nam->setCookieJar(new QNetworkCookieJar(this));
}

CloudUploader::~CloudUploader() = default;

void CloudUploader::setCredentials(
		const QString &email,
		const QString &password) {
	_email = email;
	_password = password;
}

void CloudUploader::setTargetFolder(const QString &cloudFolder) {
	_targetFolder = cloudFolder;
}

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
	if (_email.isEmpty() || _password.isEmpty()) {
		LOG(("CloudUploader: No credentials set."));
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
	authenticate();
}

void CloudUploader::authenticate() {
	_state = State::LoggingIn;

	QNetworkRequest req(QUrl(QString::fromLatin1(kAuthUrl)));
	req.setHeader(
		QNetworkRequest::ContentTypeHeader,
		"application/x-www-form-urlencoded");
	req.setHeader(
		QNetworkRequest::UserAgentHeader,
		"TelegramDesktop/CloudUploader");

	QUrlQuery params;
	params.addQueryItem("page", "https://cloud.mail.ru/");
	params.addQueryItem("Login", _email);
	params.addQueryItem("Password", _password);

	auto *reply = _nam->post(
		req,
		params.toString(QUrl::FullyEncoded).toUtf8());
	connect(reply, &QNetworkReply::finished, this, [=] {
		reply->deleteLater();
		if (reply->error() != QNetworkReply::NoError) {
			LOG(("CloudUploader: Login failed: %1"
				).arg(reply->errorString()));
			_state = State::Idle;
			return;
		}
		LOG(("CloudUploader: Login OK."));
		requestCsrfToken();
	});
}

void CloudUploader::requestCsrfToken() {
	_state = State::GettingToken;

	QNetworkRequest req(QUrl(QString::fromLatin1(kCsrfUrl)));
	req.setHeader(
		QNetworkRequest::UserAgentHeader,
		"TelegramDesktop/CloudUploader");

	auto *reply = _nam->post(req, QByteArray());
	connect(reply, &QNetworkReply::finished, this, [=] {
		reply->deleteLater();
		if (reply->error() != QNetworkReply::NoError) {
			LOG(("CloudUploader: CSRF token request failed: %1"
				).arg(reply->errorString()));
			_state = State::Idle;
			return;
		}
		const auto data = reply->readAll();
		const auto doc = QJsonDocument::fromJson(data);
		_csrfToken = doc.object()
			["body"].toObject()
			["token"].toString();

		if (_csrfToken.isEmpty()) {
			LOG(("CloudUploader: Empty CSRF token."));
			_state = State::Idle;
			return;
		}
		LOG(("CloudUploader: Got CSRF token."));
		requestUploadShard();
	});
}

void CloudUploader::requestUploadShard() {
	_state = State::GettingUploadShard;

	QUrl url(QString::fromLatin1(kDispatcherUrl));
	QUrlQuery query;
	query.addQueryItem("token", _csrfToken);
	url.setQuery(query);

	QNetworkRequest req(url);
	req.setHeader(
		QNetworkRequest::UserAgentHeader,
		"TelegramDesktop/CloudUploader");

	auto *reply = _nam->get(req);
	connect(reply, &QNetworkReply::finished, this, [=] {
		reply->deleteLater();
		if (reply->error() != QNetworkReply::NoError) {
			LOG(("CloudUploader: Dispatcher failed: %1"
				).arg(reply->errorString()));
			_state = State::Idle;
			return;
		}
		const auto data = reply->readAll();
		const auto doc = QJsonDocument::fromJson(data);
		const auto uploads = doc.object()
			["body"].toObject()
			["upload"].toArray();

		if (uploads.isEmpty()) {
			LOG(("CloudUploader: No upload shards."));
			_state = State::Idle;
			return;
		}
		_uploadUrl = uploads[0].toObject()["url"].toString();
		LOG(("CloudUploader: Upload shard: %1").arg(_uploadUrl));

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

	auto *multiPart = new QHttpMultiPart(
		QHttpMultiPart::FormDataType);

	QHttpPart filePart;
	filePart.setHeader(
		QNetworkRequest::ContentDispositionHeader,
		QString("form-data; name=\"file\"; filename=\"%1\"")
			.arg(QFileInfo(fileName).fileName()));
	filePart.setHeader(
		QNetworkRequest::ContentTypeHeader,
		"application/octet-stream");
	filePart.setBodyDevice(file);
	file->setParent(multiPart);

	multiPart->append(filePart);

	QNetworkRequest req(QUrl(_uploadUrl));
	req.setHeader(
		QNetworkRequest::UserAgentHeader,
		"TelegramDesktop/CloudUploader");
	req.setRawHeader("Referer", "https://cloud.mail.ru/");
	req.setRawHeader("Origin", "https://cloud.mail.ru");

	auto *reply = _nam->post(req, multiPart);
	multiPart->setParent(reply);

	connect(reply, &QNetworkReply::finished, this, [=] {
		reply->deleteLater();
		const auto &fn = _pendingFiles[_currentIndex];

		if (reply->error() != QNetworkReply::NoError) {
			LOG(("CloudUploader: Upload failed for %1: %2"
				).arg(fn, reply->errorString()));
			_currentIndex++;
			uploadNextFile();
			return;
		}

		const auto response = QString::fromUtf8(
			reply->readAll()).trimmed();
		// Response: "hash;filesize"
		const auto parts = response.split(';');
		if (parts.size() < 2) {
			LOG(("CloudUploader: Bad upload response: %1"
				).arg(response));
			_currentIndex++;
			uploadNextFile();
			return;
		}

		const auto hash = parts[0];
		const auto size = parts[1].toLongLong();
		const auto remotePath = _targetFolder + "/" + fn;
		registerUploadedFile(hash, size, remotePath);
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
	req.setHeader(
		QNetworkRequest::UserAgentHeader,
		"TelegramDesktop/CloudUploader");

	QUrlQuery params;
	params.addQueryItem("token", _csrfToken);
	params.addQueryItem("home", remotePath);
	params.addQueryItem("conflict", "rename");
	params.addQueryItem("hash", hash);
	params.addQueryItem("size", QString::number(size));

	auto *reply = _nam->post(
		req,
		params.toString(QUrl::FullyEncoded).toUtf8());
	connect(reply, &QNetworkReply::finished, this, [=] {
		reply->deleteLater();
		const auto &fn = _pendingFiles[_currentIndex];

		if (reply->error() != QNetworkReply::NoError) {
			LOG(("CloudUploader: Register failed for %1: %2"
				).arg(fn, reply->errorString()));
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
