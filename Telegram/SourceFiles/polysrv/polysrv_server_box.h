/*
This file is part of PolySRV Desktop,
a Telegram Desktop fork for the PolySRV multi-server client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "polysrv/polysrv_servers.h"

#include <optional>

class QPainter;

namespace PolySrv {

// Paints the logo of a server, or its initial on a colour, into a square.
void PaintServerLogo(
	QPainter &p,
	int size,
	const Server &server);

// Shows the details of a server with the actions that apply to it. The join
// action is empty when the client is already signed in to a server.
void ShowServerDetailsBox(
	Server server,
	Fn<void(const Server &)> join,
	Fn<void()> changed = nullptr);

// The servers the client knows about.
void ShowServerListBox();

// Adds a server, or edits the one that is passed.
void ShowServerEditBox(
	std::optional<Server> server,
	Fn<void()> changed = nullptr);

} // namespace PolySrv
