/*
This file is part of PolySRV Desktop,
a Telegram Desktop fork for the PolySRV multi-server client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "polysrv/polysrv_servers.h"

#include "base/debug_log.h"
#include "core/polysrv_remote.h"
#include "rpl/event_stream.h"
#include "settings.h"

#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSaveFile>
#include <QtCore/QUrl>
#include <QtCore/QUuid>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>

#include <algorithm>
#include <memory>

namespace PolySrv {

namespace {

constexpr auto kMaxRemoteResponseSize = 256 * 1024;

[[nodiscard]] QString CustomServersFile() {
	return cWorkingDir() + u"tdata/polysrv_servers.json"_q;
}

[[nodiscard]] Server TelegramServer() {
	return Server{
		.id = u"telegram"_q,
		.name = u"Telegram"_q,
		.host = u"149.154.167.51"_q,
		.port = 443,
		.description = u"Official Telegram cloud."_q,
		.isTelegram = true,
		.multiDc = true,
		.mainDcId = 2,
	};
}

[[nodiscard]] QJsonObject ServerToJson(const Server &server) {
	auto object = QJsonObject();
	object["id"] = server.id;
	object["name"] = server.name;
	object["host"] = server.host;
	object["port"] = server.port;
	object["description"] = server.description;
	object["rsaPublicKey"] = server.rsaPublicKey;
	object["logoPath"] = server.logoPath;
	object["multiDc"] = server.multiDc;
	object["mainDcId"] = server.mainDcId;
	return object;
}

[[nodiscard]] Server ServerFromJson(const QJsonObject &object) {
	auto server = Server();
	server.id = object.value("id").toString();
	server.name = object.value("name").toString();
	server.host = object.value("host").toString();
	server.port = object.value("port").toInt();
	server.description = object.value("description").toString();
	server.rsaPublicKey = object.value("rsaPublicKey").toString();
	server.logoPath = object.value("logoPath").toString();
	server.multiDc = object.value("multiDc").toBool();
	server.mainDcId = object.value("mainDcId").toInt();
	return server;
}

[[nodiscard]] std::vector<Server> ReadCustomServers() {
	auto result = std::vector<Server>();
	QFile file(CustomServersFile());
	if (!file.open(QIODevice::ReadOnly)) {
		return result;
	}
	const auto document = QJsonDocument::fromJson(file.readAll());
	if (!document.isArray()) {
		return result;
	}
	for (const auto &entry : document.array()) {
		if (!entry.isObject()) {
			continue;
		}
		const auto server = ServerFromJson(entry.toObject());
		if (server.valid()) {
			result.push_back(server);
		}
	}
	return result;
}

void WriteCustomServers(const std::vector<Server> &servers) {
	auto array = QJsonArray();
	for (const auto &server : servers) {
		array.append(ServerToJson(server));
	}
	QSaveFile file(CustomServersFile());
	if (!file.open(QIODevice::WriteOnly)) {
		LOG(("PolySRV Error: could not write the server list."));
		return;
	}
	file.write(QJsonDocument(array).toJson(QJsonDocument::Indented));
	if (!file.commit()) {
		LOG(("PolySRV Error: could not write the server list."));
	}
}

[[nodiscard]] std::vector<Server> ParseRemoteServers(const QByteArray &data) {
	auto result = std::vector<Server>();
	const auto document = QJsonDocument::fromJson(data);
	if (!document.isObject()) {
		return result;
	}
	const auto list = document.object().value("servers").toArray();
	for (const auto &entry : list) {
		if (!entry.isObject()) {
			continue;
		}
		const auto object = entry.toObject();
		auto server = ServerFromJson(object);
		server.isTelegram = object.value("isTelegram").toBool();
		if (server.valid()) {
			result.push_back(server);
		}
	}
	return result;
}

struct State {
	std::vector<Server> custom;
	std::vector<Server> remote;
	rpl::event_stream<> changed;
	std::unique_ptr<QNetworkAccessManager> manager;
	bool loaded = false;
};

State &StateInstance() {
	static auto result = State();
	if (!result.loaded) {
		result.custom = ReadCustomServers();
		result.loaded = true;
	}
	return result;
}

[[nodiscard]] std::vector<Server> MergeServers() {
	auto result = std::vector<Server>();
	result.push_back(TelegramServer());
	const auto &state = StateInstance();
	result.insert(result.end(), state.remote.begin(), state.remote.end());
	for (const auto &server : state.custom) {
		const auto exists = std::any_of(result.begin(), result.end(),
		[&](const Server &known) {
			return known.id == server.id;
		});
		if (!exists) {
			result.push_back(server);
		}
	}
	return result;
}

} // namespace

bool Server::valid() const {
	return !id.isEmpty() && !host.isEmpty() && port > 0;
}

QString Server::endpoint() const {
	return host + u":"_q + QString::number(port);
}

std::vector<Server> Servers() {
	return MergeServers();
}

std::optional<Server> FindServer(const QString &id) {
	const auto servers = Servers();
	const auto found = std::find_if(servers.begin(), servers.end(),
	[&](const Server &server) {
		return server.id == id;
	});
	if (found == servers.end()) {
		return std::nullopt;
	}
	return *found;
}

std::optional<Server> AddCustomServer(
	const QString &name,
	const QString &host,
	int port,
	const QString &description,
	const QString &rsaPublicKey) {
	auto server = Server();
	server.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
	server.name = name;
	server.host = host;
	server.port = port;
	server.description = description;
	server.rsaPublicKey = rsaPublicKey;
	if (!server.valid()) {
		return std::nullopt;
	}
	auto &state = StateInstance();
	state.custom.push_back(server);
	WriteCustomServers(state.custom);
	state.changed.fire({});
	return server;
}

std::optional<Server> UpdateCustomServer(
	const QString &id,
	const QString &name,
	const QString &host,
	int port,
	const QString &description,
	const QString &rsaPublicKey) {
	auto &state = StateInstance();
	const auto found = std::find_if(state.custom.begin(), state.custom.end(),
	[&](const Server &server) {
		return server.id == id;
	});
	if (found == state.custom.end()) {
		return std::nullopt;
	}
	found->name = name;
	found->host = host;
	found->port = port;
	found->description = description;
	found->rsaPublicKey = rsaPublicKey;
	if (!found->valid()) {
		return std::nullopt;
	}
	WriteCustomServers(state.custom);
	state.changed.fire({});
	return *found;
}

bool RemoveCustomServer(const QString &id) {
	auto &state = StateInstance();
	const auto removed = std::remove_if(state.custom.begin(),
		state.custom.end(),
	[&](const Server &server) {
			return server.id == id;
		});
	if (removed == state.custom.end()) {
		return false;
	}
	state.custom.erase(removed, state.custom.end());
	WriteCustomServers(state.custom);
	state.changed.fire({});
	return true;
}

bool IsRemovableServer(const Server &server) {
	return server.id != u"telegram"_q;
}

void RefreshRemoteServers() {
	auto &state = StateInstance();
	if (!state.manager) {
		state.manager = std::make_unique<QNetworkAccessManager>();
	}
	const auto request = QNetworkRequest(QUrl(ServersUrl()));
	const auto reply = state.manager->get(request);
	reply->connect(reply, &QNetworkReply::finished, [=] {
		const auto data = reply->readAll();
		const auto error = reply->error();
		reply->deleteLater();
		if (error != QNetworkReply::NoError
			|| data.size() > kMaxRemoteResponseSize) {
			LOG(("PolySRV Error: could not read the server list."));
			return;
		}
		const auto servers = ParseRemoteServers(data);
		if (servers.empty()) {
			return;
		}
		auto &state = StateInstance();
		state.remote = servers;
		state.changed.fire({});
	});
}

rpl::producer<> ServersChanged() {
	return StateInstance().changed.events();
}

} // namespace PolySrv
