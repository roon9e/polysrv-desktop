/*
This file is part of PolySRV Desktop,
a Telegram Desktop fork for the PolySRV multi-server client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "polysrv/polysrv_servers.h"

#include <optional>

namespace PolySrv {

// The servers the client knows about, with the ones the user added editable.
void ShowServerListBox();

// Adds a server, or edits the one that is passed.
void ShowServerEditBox(std::optional<Server> server);

} // namespace PolySrv
