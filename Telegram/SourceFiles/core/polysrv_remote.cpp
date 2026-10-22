/*
This file is part of PolySRV Desktop,
a Telegram Desktop fork for the PolySRV multi-server client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/polysrv_remote.h"

#include "core/branding.h"

namespace PolySrv {

QString RemoteDomain() {
	return Branding::RemoteDomain.utf16();
}

QString ServersUrl() {
	return RemoteDomain() + u"/servers.json"_q;
}

QString UpdateManifestUrl() {
	return RemoteDomain() + u"/update.json"_q;
}

QString ServersFallbackUrl() {
	// The same file from the branch the Pages site serves, so that a broken
	// or not yet built site does not leave the client without a server list.
	return u"https://raw.githubusercontent.com/roon9e/polysrv-desktop/gh-pages/servers.json"_q;
}

} // namespace PolySrv
