/*
This file is part of PolySRV Desktop,
a Telegram Desktop fork for the PolySRV multi-server client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

namespace PolySrv {

// The domain the client fetches everything remote from: the server list and
// the update manifest. There is one domain, so there is one place to change
// it and one origin to trust.
[[nodiscard]] QString RemoteDomain();

// The files published on that domain.
[[nodiscard]] QString ServersUrl();
[[nodiscard]] QString UpdateManifestUrl();

} // namespace PolySrv
