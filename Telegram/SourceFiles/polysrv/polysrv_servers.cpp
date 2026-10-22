/*
This file is part of PolySRV Desktop,
a Telegram Desktop fork for the PolySRV multi-server client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "polysrv/polysrv_servers.h"

#include "base/debug_log.h"
#include "core/polysrv_remote.h"
#include "mtproto/mtproto_dc_options.h"
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
#include <QtNetwork/QNetworkRequest>

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
		.dataCenters = {
			{ 1, u"149.154.175.50"_q, 443 },
			{ 2, u"149.154.167.51"_q, 443 },
			{ 3, u"149.154.175.100"_q, 443 },
			{ 4, u"149.154.167.91"_q, 443 },
			{ 5, u"149.154.171.5"_q, 443 },
		},
	};
}

[[nodiscard]] QJsonArray DataCentersToJson(
		const std::vector<ServerDataCenter> &dataCenters) {
	auto result = QJsonArray();
	for (const auto &dataCenter : dataCenters) {
		auto object = QJsonObject();
		object["id"] = dataCenter.id;
		object["host"] = dataCenter.host;
		object["port"] = dataCenter.port;
		result.append(object);
	}
	return result;
}

[[nodiscard]] std::vector<ServerDataCenter> DataCentersFromJson(
		const QJsonArray &array) {
	auto result = std::vector<ServerDataCenter>();
	for (const auto &entry : array) {
		if (!entry.isObject()) {
			continue;
		}
		const auto object = entry.toObject();
		auto dataCenter = ServerDataCenter{
			.id = object.value("id").toInt(),
			.host = object.value("host").toString(),
			.port = object.value("port").toInt(),
		};
		if (dataCenter.id > 0
			&& !dataCenter.host.isEmpty()
			&& dataCenter.port > 0) {
			result.push_back(std::move(dataCenter));
		}
	}
	return result;
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
	if (!server.dataCenters.empty()) {
		object["dc"] = DataCentersToJson(server.dataCenters);
	}
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
	if (server.rsaPublicKey.isEmpty()) {
		// The published list names the key the way the upstream fork does.
		server.rsaPublicKey = object.value("rsaPublicKeyPem").toString();
	}
	server.logoPath = object.value("logoPath").toString();
	if (server.logoPath.isEmpty()) {
		server.logoPath = object.value("logoUrl").toString();
	}
	server.multiDc = object.value("multiDc").toBool();
	server.mainDcId = object.value("mainDcId").toInt();
	server.isTelegram = object.value("isTelegram").toBool();
	if (const auto dataCenters = object.value("dc"); dataCenters.isArray()) {
		server.dataCenters = DataCentersFromJson(dataCenters.toArray());
	}
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
	RemoteState remoteState;
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
	const auto append = [&](const Server &server) {
		const auto exists = std::any_of(result.begin(), result.end(),
		[&](const Server &known) {
			return known.id == server.id;
		});
		if (!exists) {
			result.push_back(server);
		}
	};
	append(TelegramServer());
	const auto &state = StateInstance();
	for (const auto &server : state.remote) {
		append(server);
	}
	for (const auto &server : state.custom) {
		append(server);
	}
	return result;
}

[[nodiscard]] bool ApplyRemoteList(State &state, const QByteArray &data) {
	const auto servers = ParseRemoteServers(data);
	if (servers.empty()) {
		return false;
	}
	state.remote = servers;
	state.remoteState = {
		RemoteStatus::Succeeded,
		QString(),
		int(servers.size()),
	};
	state.changed.fire({});
	return true;
}

void RequestRemoteList(
		State &state,
		const QString &url,
		const QString &fallback) {
	auto request = QNetworkRequest(QUrl(url));
	request.setAttribute(
		QNetworkRequest::RedirectPolicyAttribute,
		QNetworkRequest::NoLessSafeRedirectPolicy);
	const auto reply = state.manager->get(request);
	reply->connect(reply, &QNetworkReply::finished, [=, &state] {
		const auto data = reply->readAll();
		const auto error = reply->error();
		const auto errorText = reply->errorString();
		reply->deleteLater();
		if (error == QNetworkReply::NoError
			&& data.size() <= kMaxRemoteResponseSize
			&& ApplyRemoteList(state, data)) {
			return;
		}
		LOG(("PolySRV Error: could not read the server list from \"%1\": %2.")
			.arg(url, errorText));
		if (!fallback.isEmpty()) {
			RequestRemoteList(state, fallback, QString());
			return;
		}
		state.remoteState = { RemoteStatus::Failed, errorText, 0 };
		state.changed.fire({});
	});
}

} // namespace

uint64 ServerScopeHash(const QString &host) {
	const auto utf8 = host.toLower().toUtf8();
	auto hash = quint32(2166136261u);
	for (const auto byte : utf8) {
		hash ^= quint32(quint8(byte));
		hash *= quint32(16777619u);
	}
	return uint64(hash) << 24;
}

uint64 ScopedSessionId(uint64 sessionId, const QString &scope) {
	if (scope.isEmpty()) {
		return sessionId;
	}
	// The scope goes into the bits 24 to 55, so the low bits of the id
	// stay and the test mode bit above 56 stays untouched.
	return sessionId ^ ServerScopeHash(scope);
}

int MainDcIdForServer(const Server &server) {
	if (server.mainDcId > 0) {
		return server.mainDcId;
	}
	return server.multiDc ? 2 : 1;
}

void ApplyServerToDcOptions(MTP::DcOptions &options, const Server &server) {
	const auto flags = MTPDdcOption::Flag::f_static
		| MTPDdcOption::Flag::f_tcpo_only;
	auto list = QVector<MTPDcOption>();
	if (!server.dataCenters.empty()) {
		for (const auto &dataCenter : server.dataCenters) {
			list.push_back(MTP_dcOption(
				MTP_flags(flags),
				MTP_int(dataCenter.id),
				MTP_string(dataCenter.host.toStdString()),
				MTP_int(dataCenter.port),
				MTPbytes()));
		}
	} else if (!server.host.isEmpty() && server.port > 0) {
		// A single machine server is one host and port for every data
		// center, so that the files and the migrations that name a data
		// center keep working.
		for (auto dcId = 1; dcId <= 5; ++dcId) {
			list.push_back(MTP_dcOption(
				MTP_flags(flags),
				MTP_int(dcId),
				MTP_string(server.host.toStdString()),
				MTP_int(server.port),
				MTPbytes()));
		}
	}
	options.setOptionsLocked(false);
	if (!list.isEmpty()) {
		options.setFromList(MTP_vector<MTPDcOption>(list));
	}
	if (server.rsaPublicKey.isEmpty()) {
		options.setBuiltInPublicKeys();
		if (!server.isTelegram) {
			// The handshake cannot complete without the key of a server
			// that is not the official network.
			LOG(("PolySRV Error: the server %1 has no public RSA key.")
				.arg(server.endpoint()));
		}
	} else {
		options.setPublicKeysFromPem(server.rsaPublicKey);
	}
	// The official network and a multi-DC server publish their data centers
	// in the configuration, so those options stay open.
	options.setOptionsLocked(!server.isTelegram && !server.multiDc);
}

bool Server::valid() const {
	return !id.isEmpty()
		&& ((!host.isEmpty() && port > 0) || !dataCenters.empty());
}

QString Server::primaryHost() const {
	if (!host.isEmpty() && port > 0) {
		return host;
	}
	if (dataCenters.empty()) {
		return QString();
	}
	const auto main = (mainDcId > 0)
		? std::find_if(
			dataCenters.begin(),
			dataCenters.end(),
			[&](const ServerDataCenter &dataCenter) {
				return dataCenter.id == mainDcId;
			})
		: dataCenters.end();
	const auto &chosen = (main != dataCenters.end())
		? *main
		: dataCenters.front();
	return chosen.host;
}

int Server::primaryPort() const {
	if (!host.isEmpty() && port > 0) {
		return port;
	}
	if (dataCenters.empty()) {
		return 0;
	}
	const auto main = (mainDcId > 0)
		? std::find_if(
			dataCenters.begin(),
			dataCenters.end(),
			[&](const ServerDataCenter &dataCenter) {
				return dataCenter.id == mainDcId;
			})
		: dataCenters.end();
	const auto &chosen = (main != dataCenters.end())
		? *main
		: dataCenters.front();
	return chosen.port;
}

QString Server::endpoint() const {
	const auto chosenHost = primaryHost();
	const auto chosenPort = primaryPort();
	return (chosenHost.isEmpty() || chosenPort <= 0)
		? QString()
		: chosenHost + u":"_q + QString::number(chosenPort);
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
	const QString &rsaPublicKey,
	bool multiDc,
	int mainDcId) {
	auto server = Server();
	server.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
	server.name = name;
	server.host = host;
	server.port = port;
	server.description = description;
	server.rsaPublicKey = rsaPublicKey;
	server.multiDc = multiDc;
	server.mainDcId = mainDcId;
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
	const QString &rsaPublicKey,
	bool multiDc,
	int mainDcId) {
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
	found->multiDc = multiDc;
	found->mainDcId = mainDcId;
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

bool IsCustomServer(const QString &id) {
	const auto &custom = StateInstance().custom;
	return std::any_of(custom.begin(), custom.end(), [&](const Server &server) {
		return server.id == id;
	});
}

void RefreshRemoteServers() {
	auto &state = StateInstance();
	if (state.remoteState.status == RemoteStatus::Loading) {
		return;
	}
	if (!state.manager) {
		state.manager = std::make_unique<QNetworkAccessManager>();
	}
	state.remoteState = { RemoteStatus::Loading, QString(), 0 };
	state.changed.fire({});
	RequestRemoteList(state, ServersUrl(), ServersFallbackUrl());
}

RemoteState Remote() {
	return StateInstance().remoteState;
}

rpl::producer<> ServersChanged() {
	return StateInstance().changed.events();
}

} // namespace PolySrv
