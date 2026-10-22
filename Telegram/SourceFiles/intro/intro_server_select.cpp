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
#include "polysrv/polysrv_server_box.h"
#include "polysrv/polysrv_servers.h"
#include "styles/style_intro.h"
#include "ui/qt_object_factory.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"

#include <rpl/producer.h>

#include <QtWidgets/QWidget>

namespace Intro {
namespace details {

namespace {

[[nodiscard]] QString ServerLabel(const PolySrv::Server &server) {
	return server.name + u" \u00b7 "_q + server.endpoint();
}

[[nodiscard]] QString StatusText() {
	const auto state = PolySrv::Remote();
	switch (state.status) {
	case PolySrv::RemoteStatus::Idle:
		return QString();
	case PolySrv::RemoteStatus::Loading:
		return tr::lng_polysrv_servers_loading(tr::now);
	case PolySrv::RemoteStatus::Succeeded:
		return tr::lng_polysrv_servers_loaded(
			tr::now,
			lt_count,
			state.count);
	case PolySrv::RemoteStatus::Failed:
		return tr::lng_polysrv_servers_failed(
			tr::now,
			lt_error,
			state.error);
	}
	return QString();
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
	_addServer = Ui::CreateChild<Ui::LinkButton>(
		_container.get(),
		tr::lng_polysrv_add_server(tr::now),
		st::introServerAdd);
	_addServer->setClickedCallback([] {
		PolySrv::ShowServerListBox();
	});
	_status = Ui::CreateChild<Ui::FlatLabel>(
		_container.get(),
		StatusText(),
		st::introServerStatus);

	// A step is already visible when the rows are rebuilt after the remote
	// list arrives, so a child created now is not shown by its parent.
	_container->show();

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
	if (_addServer) {
		_addServer->resizeToWidth(width);
		_addServer->moveToLeft(0, y);
		y += _addServer->height() + st::introServerRowSpacing;
	}
	if (_status) {
		_status->resizeToWidth(width);
		_status->moveToLeft(0, y);
		y += _status->height() + st::introServerRowSpacing;
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
		account().applyServer(
			server.id,
			server.primaryHost(),
			server.primaryPort());
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
