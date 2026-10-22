/*
This file is part of PolySRV Desktop,
a Telegram Desktop fork for the PolySRV multi-server client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "intro/intro_server_select.h"

#include "intro/intro_qr.h"
#include "lang/lang_keys.h"
#include "main/main_account.h"
#include "polysrv/polysrv_servers.h"
#include "styles/style_intro.h"
#include "ui/qt_object_factory.h"
#include "ui/widgets/buttons.h"

#include <rpl/producer.h>

#include <QtWidgets/QWidget>

namespace Intro {
namespace details {

namespace {

[[nodiscard]] QString ServerLabel(const PolySrv::Server &server) {
	return server.name + u"  \u00b7  "_q + server.endpoint();
}

} // namespace

ServerSelectWidget::ServerSelectWidget(
	QWidget *parent,
	not_null<Main::Account*> account,
	not_null<Data*> data)
: Step(parent, account, data, true) {
	setTitleText(tr::lng_polysrv_server_title());
	setDescriptionText(tr::lng_polysrv_server_description());

	fillRows();

	// The list of the servers the remote domain publishes arrives over
	// the network, so the rows are filled again when it does.
	PolySrv::RefreshRemoteServers();
	PolySrv::ServersChanged() | rpl::on_next([=] {
		fillRows();
	}, lifetime());
}

void ServerSelectWidget::fillRows() {
	_rows.clear();
	_container.destroyDelayed();
	_container = object_ptr<QWidget>::fromRaw(
		Ui::CreateChild<QWidget>(this));

	const auto servers = PolySrv::Servers();
	_rows.reserve(servers.size());
	for (const auto &server : servers) {
		auto button = Ui::CreateChild<Ui::LinkButton>(
			_container.get(),
			ServerLabel(server));
		_rows.push_back(button);
		const auto id = server.id;
		button->setClickedCallback([=] {
			chooseServer(id);
		});
	}
	updateRowsGeometry();
}

void ServerSelectWidget::updateRowsGeometry() {
	if (!_container) {
		return;
	}
	const auto width = st::introNextButton.width;
	auto y = 0;
	for (const auto &button : _rows) {
		button->resizeToWidth(width);
		button->moveToLeft(0, y);
		y += button->height() + st::introServerRowSpacing;
	}
	_container->resize(width, y);
	_container->move(
		contentLeft(),
		contentTop() + st::introStepFieldTop);
}

void ServerSelectWidget::chooseServer(const QString &id) {
	const auto servers = PolySrv::Servers();
	for (const auto &server : servers) {
		if (server.id != id) {
			continue;
		}
		account().applyServer(server.id, server.host, server.port);
		_selectedId = server.id;
		goNext<QrWidget>();
		return;
	}
}

PolySrv::Server ServerSelectWidget::currentServer() const {
	const auto servers = PolySrv::Servers();
	for (const auto &server : servers) {
		if (server.id == _selectedId) {
			return server;
		}
	}
	return servers.empty() ? PolySrv::Server() : servers.front();
}

void ServerSelectWidget::submit() {
	const auto server = currentServer();
	if (server.valid()) {
		chooseServer(server.id);
	}
}

rpl::producer<QString> ServerSelectWidget::nextButtonText() const {
	return tr::lng_polysrv_server_continue();
}

void ServerSelectWidget::activate() {
	Step::activate();
	setInnerFocus();
}

void ServerSelectWidget::setInnerFocus() {
	if (!_rows.empty()) {
		_rows.front()->setFocus();
	}
}

void ServerSelectWidget::resizeEvent(QResizeEvent *e) {
	Step::resizeEvent(e);
	updateRowsGeometry();
}

} // namespace details
} // namespace Intro
