/*
This file is part of PolySRV Desktop,
a Telegram Desktop fork for the PolySRV multi-server client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

#include <rpl/producer.h>

#include <QtCore/QString>

#include <optional>
#include <vector>

namespace MTP {
class DcOptions;
} // namespace MTP

namespace PolySrv {

// One data center address of a published server.
struct ServerDataCenter {
	int id = 0;
	QString host;
	int port = 0;
};

// A server the client can sign in to.
struct Server {
	QString id;
	QString name;
	QString host;
	int port = 0;
	QString description;
	QString rsaPublicKey;
	QString logoPath;
	// The official Telegram network, whose data centers are discovered.
	bool isTelegram = false;
	// A server with more than one data center, whose addresses are
	// discovered; a single machine server is mapped onto every data center.
	bool multiDc = false;
	// The home data center. Zero means automatic.
	int mainDcId = 0;
	// The addresses a published server names, one per data center.
	std::vector<ServerDataCenter> dataCenters;

	[[nodiscard]] bool valid() const;
	[[nodiscard]] QString primaryHost() const;
	[[nodiscard]] int primaryPort() const;
	[[nodiscard]] QString endpoint() const;
};

// The state of the remote list of servers.
enum class RemoteStatus {
	Idle,
	Loading,
	Succeeded,
	Failed,
};

struct RemoteState {
	RemoteStatus status = RemoteStatus::Idle;
	QString error;
	int count = 0;
};

// The scope of a server, used to keep the identity of the same user id
// on two servers apart.
[[nodiscard]] uint64 ServerScopeHash(const QString &host);

// The session id of an account on a server. The server scope is mixed
// into the id, so that the same user id on two servers is two accounts.
[[nodiscard]] uint64 ScopedSessionId(uint64 sessionId, const QString &scope);

// The home data center of a server. Zero means automatic.
[[nodiscard]] int MainDcIdForServer(const Server &server);

// Applies a server to the data center options of a session.
void ApplyServerToDcOptions(MTP::DcOptions &options, const Server &server);

// The servers the client knows about: the official Telegram network first,
// then the ones the remote list names, then the ones the user added.
[[nodiscard]] std::vector<Server> Servers();
[[nodiscard]] std::optional<Server> FindServer(const QString &id);

// The user added servers, stored in the working directory.
[[nodiscard]] std::optional<Server> AddCustomServer(
	const QString &name,
	const QString &host,
	int port,
	const QString &description = QString(),
	const QString &rsaPublicKey = QString(),
	bool multiDc = false,
	int mainDcId = 0);
[[nodiscard]] std::optional<Server> UpdateCustomServer(
	const QString &id,
	const QString &name,
	const QString &host,
	int port,
	const QString &description = QString(),
	const QString &rsaPublicKey = QString(),
	bool multiDc = false,
	int mainDcId = 0);
[[nodiscard]] bool RemoveCustomServer(const QString &id);
[[nodiscard]] bool IsRemovableServer(const Server &server);
[[nodiscard]] bool IsCustomServer(const QString &id);

// Fetches the server list from the remote domain and merges it in.
void RefreshRemoteServers();

// The state of the last remote list fetch, for the sign-in screen.
[[nodiscard]] RemoteState Remote();

// Fires when the list of servers changes.
[[nodiscard]] rpl::producer<> ServersChanged();

} // namespace PolySrv
