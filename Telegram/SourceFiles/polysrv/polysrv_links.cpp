/*
This file is part of PolySRV Desktop,
a Telegram Desktop fork for the PolySRV multi-server client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "polysrv/polysrv_links.h"

#include <QtCore/QUrl>
#include <QtCore/QUrlQuery>

namespace PolySrv {

Link ParseLink(const QString &url) {
	auto result = Link();
	const auto trimmed = url.trimmed();
	const auto protocol = u"srv://"_q;
	if (!trimmed.startsWith(protocol, Qt::CaseInsensitive)) {
		return result;
	}
	const auto body = trimmed.mid(protocol.size());

	// The query of the link, if it has one.
	const auto question = body.indexOf('?');
	const auto path = (question < 0) ? body : body.left(question);
	const auto query = (question < 0)
		? QString()
		: body.mid(question + 1);

	if (path.compare(u"addserver"_q, Qt::CaseInsensitive) == 0) {
		result.type = Link::Type::AddServer;
		const auto parameters = QUrlQuery(query);
		result.host = parameters.queryItemValue(u"host"_q);
		result.port = parameters.queryItemValue(u"port"_q).toInt();
		return result;
	}

	if (path.compare(u"oauth"_q, Qt::CaseInsensitive) == 0
		|| path.compare(u"resolve"_q, Qt::CaseInsensitive) == 0) {
		result.type = Link::Type::Command;
		result.command = query;
		return result;
	}

	// A link of one server: srv://<server host>/<rest>.
	const auto slash = path.indexOf('/');
	if (slash <= 0) {
		return result;
	}
	result.type = Link::Type::ServerHost;
	result.serverHost = path.left(slash);
	result.rest = path.mid(slash + 1);
	return result;
}

} // namespace PolySrv
