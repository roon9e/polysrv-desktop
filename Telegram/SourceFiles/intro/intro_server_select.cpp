/*
This file is part of PolySRV Desktop,
a Telegram Desktop fork for the PolySRV multi-server client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "intro/intro_server_select.h"

#include "base/timer.h"
#include "boxes/abstract_box.h"
#include "intro/intro_qr.h"
#include "lang/lang_keys.h"
#include "main/main_account.h"
#include "mtproto/facade.h"
#include "polysrv/polysrv_server_box.h"
#include "polysrv/polysrv_server_row.h"
#include "polysrv/polysrv_servers.h"
#include "styles/style_intro.h"
#include "styles/style_layers.h"
#include "ui/boxes/confirm_box.h"
#include "ui/effects/animations.h"
#include "ui/effects/ripple_animation.h"
#include "ui/layers/box_content.h"
#include "ui/painter.h"
#include "ui/qt_object_factory.h"
#include "ui/toast/toast.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/scroll_area.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/vertical_layout.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

namespace Intro {
namespace details {
namespace {

constexpr auto kConnectPollMs = crl::time(250);
constexpr auto kConnectTimeout = crl::time(20000);
constexpr auto kConnectDotsMs = crl::time(400);
constexpr auto kConnectMaxDots = 3;

// The screen shown while the client connects to the chosen server, before
// the QR code is drawn. Escape and outside click stay off so a stray click
// cannot drop an in-flight attempt, but Cancel is always present.
class ConnectingBox final : public Ui::BoxContent {
public:
	ConnectingBox(
		QWidget *,
		not_null<Main::Account*> account,
		const QString &serverName,
		Fn<void()> ready)
	: _account(account)
	, _serverName(serverName)
	, _ready(std::move(ready)) {
	}

	void showFailed();

protected:
	void prepare() override;

private:
	void poll();
	void updateText();

	const not_null<Main::Account*> _account;
	const QString _serverName;
	Fn<void()> _ready;
	QPointer<Ui::FlatLabel> _label;
	QPointer<Ui::RoundButton> _button;
	base::Timer _poll;
	base::Timer _dots;
	crl::time _started = 0;
	int _dotsCount = 0;
	bool _failed = false;
	bool _finished = false;
};

void ConnectingBox::prepare() {
	setCloseByEscape(false);
	setCloseByOutsideClick(false);

	const auto content = setInnerWidget(
		object_ptr<Ui::VerticalLayout>(this));
	content->add(
		object_ptr<Ui::FixedHeightWidget>(content, st::boxLittleSkip));
	_label = content->add(
		object_ptr<Ui::FlatLabel>(
			content,
			QString(),
			st::introErrorCentered),
		st::boxRowPadding);
	content->add(
		object_ptr<Ui::FixedHeightWidget>(content, st::boxLittleSkip));
	updateText();

	_dots.setCallback([=] {
		_dotsCount = (_dotsCount + 1) % (kConnectMaxDots + 1);
		updateText();
	});
	_dots.callEach(kConnectDotsMs);

	_button = addButton(tr::lng_cancel(), [=] {
		_finished = true;
		_poll.cancel();
		_dots.cancel();
		closeBox();
	});

	_poll.setCallback([=] { poll(); });
	_started = crl::now();
	_poll.callEach(kConnectPollMs);

	setDimensionsToContent(st::boxWidth, content);
}

void ConnectingBox::poll() {
	if (_finished) {
		return;
	}
	if (_account->mtp().dcstate() == MTP::ConnectedState) {
		_finished = true;
		_poll.cancel();
		_dots.cancel();
		if (_ready) {
			_ready();
		}
		closeBox();
		return;
	}
	if (crl::now() - _started >= kConnectTimeout) {
		showFailed();
	}
}

void ConnectingBox::showFailed() {
	if (_failed || _finished) {
		return;
	}
	_failed = true;
	_dots.cancel();
	updateText();
	if (_button) {
		_button->setText(tr::lng_close());
	}
}

void ConnectingBox::updateText() {
	if (!_label) {
		return;
	}
	if (_failed) {
		_label->setText(tr::lng_polysrv_connection_failed(tr::now));
	} else {
		_label->setText(tr::lng_polysrv_connecting(
			tr::now,
			lt_server,
			_serverName) + QString(_dotsCount, '.'));
	}
}

} // namespace

ServerSelectWidget::ServerSelectWidget(
	QWidget *parent,
	not_null<Main::Account*> account,
	not_null<Data*> data)
: Step(parent, account, data, false)
, _panel(Ui::CreateChild<Ui::RpWidget>(this))
, _scroll(Ui::CreateChild<Ui::ScrollArea>(_panel))
, _rowsContainer(_scroll->setOwnedWidget(object_ptr<Ui::RpWidget>(_scroll.data())))
, _ownTitle(Ui::CreateChild<Ui::FlatLabel>(
	this,
	tr::lng_polysrv_server_title(tr::now),
	st::introTitle))
, _ownDescription(Ui::CreateChild<Ui::FlatLabel>(
	this,
	tr::lng_polysrv_server_description(tr::now),
	st::introServerDescription))
, _status(Ui::CreateChild<Ui::FlatLabel>(
	this,
	QString(),
	st::introServerStatus))
, _addServer(
	this,
	rpl::single(tr::lng_polysrv_add_server(tr::now)),
	st::introServerAddButton) {
	setTitleText(rpl::single(QString()));
	setDescriptionText(TextWithEntities());

	_ownTitle->setAttribute(Qt::WA_TransparentForMouseEvents);
	_ownDescription->setAttribute(Qt::WA_TransparentForMouseEvents);
	_status->setAttribute(Qt::WA_TransparentForMouseEvents);
	_status->hide();

	_panel->setAttribute(Qt::WA_OpaquePaintEvent, false);
	_panel->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(_panel.data());
		PainterHighQualityEnabler hq(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBgOver);
		const auto radius = st::introServerListPanelRadius;
		p.drawRoundedRect(_panel->rect(), radius, radius);
	}, _panel->lifetime());

	_scroll->setAttribute(Qt::WA_OpaquePaintEvent, false);

	_addServer->setClickedCallback([=] {
		PolySrv::ShowServerEditBox(std::nullopt, crl::guard(this, [=] {
			rebuildList();
		}));
	});

	PolySrv::ServersChanged() | rpl::on_next([=] {
		rebuildList();
		updateStatus();
	}, lifetime());
}

void ServerSelectWidget::finishInit() {
	updateListGeometry();
	rebuildList();
}

void ServerSelectWidget::activate() {
	Step::activate();
	updateListGeometry();
	rebuildList();
	showChildren();
	_panel->show();
	_scroll->show();
	_rowsContainer->show();
	_addServer->show();
	PolySrv::RefreshServerLogos();
	if (const auto remote = PolySrv::Remote();
		remote.status != PolySrv::RemoteStatus::Succeeded
		&& remote.status != PolySrv::RemoteStatus::Loading) {
		PolySrv::RefreshRemoteServers();
	}
	updateStatus();
}

void ServerSelectWidget::submit() {
}

bool ServerSelectWidget::hasBack() const {
	return true;
}

rpl::producer<QString> ServerSelectWidget::nextButtonText() const {
	return rpl::single(QString());
}

int ServerSelectWidget::listWidth() const {
	const auto available = std::max(
		width() - 2 * st::introSettingsSkip,
		st::introServerListMinWidth);
	return std::min(available, st::introServerListMaxWidth);
}

int ServerSelectWidget::listInnerWidth() const {
	const auto padding = st::introServerListPanelPadding;
	return listWidth() - padding.left() - padding.right();
}

int ServerSelectWidget::listPanelHeight(int panelTop) const {
	const auto padding = st::introServerListPanelPadding;
	const auto natural = _rowsContainer->height()
		+ padding.top()
		+ padding.bottom();
	const auto reserved = !_status->isHidden()
		? (_status->height() + st::introServerListTop)
		: 0;
	// The whole step below the description is free, there is no next button.
	const auto available = height()
		- panelTop
		- st::introServerListBottom
		- reserved;
	return std::clamp(
		natural,
		st::introServerListMinHeight,
		std::max(available, st::introServerListMinHeight));
}

void ServerSelectWidget::rebuildList() {
	const auto children = _rowsContainer->children();
	for (auto i = children.size() - 1; i >= 0; --i) {
		delete children[i];
	}

	auto servers = std::vector<PolySrv::Server>();
	for (const auto &server : PolySrv::Servers()) {
		if (server.valid()) {
			servers.push_back(server);
		}
	}

	const auto width = listInnerWidth();
	const auto rowHeight = st::introServerRowHeight;
	const auto spacing = st::introServerRowSpacing;
	auto y = 0;
	for (const auto &server : servers) {
		const auto row = Ui::CreateChild<PolySrv::ServerRow>(
			_rowsContainer,
			server,
			[=] { showDetails(server); },
			[=] { joinServer(server); });
		row->setGeometry(0, y, width, rowHeight);
		row->show();
		y += rowHeight + spacing;
	}
	_rowsContainer->resize(width, servers.empty() ? 0 : (y - spacing));
	updateListGeometry();
}

void ServerSelectWidget::showDetails(const PolySrv::Server &server) {
	PolySrv::ShowServerDetailsBox(
		server,
		crl::guard(this, [=](const PolySrv::Server &chosen) {
			joinServer(chosen);
		}),
		crl::guard(this, [=] {
			rebuildList();
		}));
}

void ServerSelectWidget::joinServer(const PolySrv::Server &server) {
	if (!server.valid()) {
		Ui::Toast::Show(tr::lng_polysrv_server_invalid(tr::now));
		return;
	}
	PolySrv::CheckServerOnline(server, crl::guard(this, [=](
			bool online,
			int) {
		if (!online) {
			Ui::show(Ui::MakeConfirmBox({
				.text = tr::lng_polysrv_server_offline_confirm(),
				.confirmed = crl::guard(this, [=] {
					proceedJoin(server);
				}),
				.confirmText = tr::lng_polysrv_server_join(),
			}));
			return;
		}
		proceedJoin(server);
	}));
}

void ServerSelectWidget::proceedJoin(const PolySrv::Server &server) {
	account().applyServer(
		server.id,
		server.primaryHost(),
		server.primaryPort());
	Ui::show(
		Box<ConnectingBox>(
			&account(),
			server.name,
			crl::guard(this, [=] { goNext<QrWidget>(); })),
		Ui::LayerOption::KeepOther);
}

void ServerSelectWidget::updateStatus() {
	const auto state = PolySrv::Remote();
	auto text = QString();
	switch (state.status) {
	case PolySrv::RemoteStatus::Loading:
		text = tr::lng_polysrv_servers_loading(tr::now);
		break;
	case PolySrv::RemoteStatus::Failed:
		text = tr::lng_polysrv_servers_failed(tr::now, lt_error, state.error);
		break;
	case PolySrv::RemoteStatus::Idle:
	case PolySrv::RemoteStatus::Succeeded:
		break;
	}
	_status->setText(text);
	_status->setVisible(!text.isEmpty());
	updateListGeometry();
}

void ServerSelectWidget::updateRowsGeometry() {
	const auto width = listInnerWidth();
	const auto rowHeight = st::introServerRowHeight;
	const auto spacing = st::introServerRowSpacing;
	auto y = 0;
	for (const auto child : _rowsContainer->children()) {
		const auto row = static_cast<PolySrv::ServerRow*>(child);
		row->setGeometry(0, y, width, rowHeight);
		y += rowHeight + spacing;
	}
	if (y > 0) {
		_rowsContainer->resize(width, y - spacing);
	}
}

void ServerSelectWidget::updateListGeometry() {
	const auto panelWidth = listWidth();
	const auto left = (width() - panelWidth) / 2;

	const auto titleTop = contentTop() + st::introTitleTop;
	_ownTitle->resizeToWidth(panelWidth);
	_ownTitle->moveToLeft(left, titleTop);

	const auto descTop = _ownTitle->y()
		+ _ownTitle->height()
		+ st::introServerTitleSkip;
	_ownDescription->resizeToWidth(std::max(
		panelWidth - _addServer->width() - st::introServerAddSkip,
		1));
	_ownDescription->moveToLeft(left, descTop);

	_addServer->moveToLeft(
		left + panelWidth - _addServer->width(),
		descTop + (_ownDescription->height() - _addServer->height()) / 2);

	const auto top = _ownDescription->y()
		+ _ownDescription->height()
		+ st::introServerListTop;
	_status->resizeToWidth(panelWidth);
	const auto panelHeight = listPanelHeight(top);
	_panel->setGeometry(left, top, panelWidth, panelHeight);

	const auto padding = st::introServerListPanelPadding;
	_scroll->setGeometry(
		padding.left(),
		padding.top(),
		panelWidth - padding.left() - padding.right(),
		panelHeight - padding.top() - padding.bottom());

	_status->moveToLeft(
		left,
		top + panelHeight + st::introServerListTop);

	updateRowsGeometry();
}

void ServerSelectWidget::resizeEvent(QResizeEvent *e) {
	Step::resizeEvent(e);
	updateListGeometry();
}

} // namespace details
} // namespace Intro
