/*
This file is part of PolySRV Desktop,
a Telegram Desktop fork for the PolySRV multi-server client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

#include <QtCore/QString>

#include <optional>
#include <vector>

namespace rpl {
template <typename Value>
class producer;
} // namespace rpl

namespace PolySrv {

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

	[[nodiscard]] bool valid() const;
	[[nodiscard]] QString endpoint() const;
};

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
	const QString &rsaPublicKey = QString());
[[nodiscard]] std::optional<Server> UpdateCustomServer(
	const QString &id,
	const QString &name,
	const QString &host,
	int port,
	const QString &description = QString(),
	const QString &rsaPublicKey = QString());
[[nodiscard]] bool RemoveCustomServer(const QString &id);
[[nodiscard]] bool IsRemovableServer(const Server &server);

// Fetches the server list from the remote domain and merges it in.
void RefreshRemoteServers();

// Fires when the list of servers changes.
[[nodiscard]] rpl::producer<> ServersChanged();

} // namespace PolySrv
