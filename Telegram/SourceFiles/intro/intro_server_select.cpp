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
#include "polysrv/polysrv_server_box.h"
#include "polysrv/polysrv_servers.h"
#include "styles/style_intro.h"
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

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

namespace Intro {
namespace details {
namespace {

class ServerRow final : public Ui::RippleButton {
public:
	ServerRow(
		QWidget *parent,
		PolySrv::Server server,
		Fn<void()> details,
		Fn<void()> join);

private:
	void setStatus(bool online, int latencyMs);
	void updateLayout();
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	QImage prepareRippleMask() const override;

	const PolySrv::Server _server;
	const Fn<void()> _details;
	const Fn<void()> _join;

	object_ptr<Ui::RpWidget> _logo;
	object_ptr<Ui::FlatLabel> _name;
	object_ptr<Ui::FlatLabel> _endpoint;
	object_ptr<Ui::FlatLabel> _status;
	object_ptr<Ui::FlatLabel> _latency;
	object_ptr<Ui::RoundButton> _joinButton;

	std::optional<bool> _online;
	Ui::Animations::Basic _checkingAnimation;
	QRect _dotRect;
};

ServerRow::ServerRow(
	QWidget *parent,
	PolySrv::Server server,
	Fn<void()> details,
	Fn<void()> join)
: RippleButton(parent, st::defaultRippleAnimation)
, _server(std::move(server))
, _details(std::move(details))
, _join(std::move(join))
, _logo(this)
, _name(this, _server.name, st::introServerRowName)
, _endpoint(
	this,
	tr::lng_polysrv_server_endpoint(
		tr::now,
		lt_host,
		_server.primaryHost(),
		lt_port,
		QString::number(_server.primaryPort())),
	st::introServerRowEndpoint)
, _status(
	this,
	tr::lng_polysrv_server_checking(tr::now),
	st::introServerRowStatus)
, _latency(this, QString(), st::introServerRowLatency)
, _joinButton(
	this,
	rpl::single(tr::lng_polysrv_server_join(tr::now)),
	st::introServerRowJoinButton) {
	const auto size = st::introServerRowAvatarSize;
	_logo->resize(size, size);
	_logo->setAttribute(Qt::WA_TransparentForMouseEvents);
	_logo->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(_logo.data());
		PolySrv::PaintServerLogo(p, size, _server);
	}, _logo->lifetime());

	_name->setAttribute(Qt::WA_TransparentForMouseEvents);
	_endpoint->setAttribute(Qt::WA_TransparentForMouseEvents);
	_status->setAttribute(Qt::WA_TransparentForMouseEvents);
	_latency->setAttribute(Qt::WA_TransparentForMouseEvents);
	_latency->hide();

	setClickedCallback(_details);
	_joinButton->setClickedCallback([=] {
		_join();
	});

	_checkingAnimation.init([=](crl::time) {
		if (_online.has_value()) {
			return false;
		}
		update();
		return true;
	});
	_checkingAnimation.start();

	PolySrv::CheckServerOnline(_server, crl::guard(this, [=](
			bool online,
			int latencyMs) {
		setStatus(online, latencyMs);
	}));

	resize(parent->width(), st::introServerRowHeight);
	updateLayout();
}

void ServerRow::setStatus(bool online, int latencyMs) {
	_online = online;
	_checkingAnimation.stop();
	_status->setText(online
		? tr::lng_polysrv_server_online(tr::now)
		: tr::lng_polysrv_server_offline(tr::now));
	_status->setTextColorOverride(online
		? st::windowActiveTextFg->c
		: st::attentionButtonFg->c);
	if (online && latencyMs >= 0) {
		_latency->setText(tr::lng_polysrv_server_latency(
			tr::now,
			lt_latency,
			QString::number(latencyMs)));
		_latency->show();
	} else {
		_latency->setText(QString());
		_latency->hide();
	}
	updateLayout();
	update();
}

void ServerRow::updateLayout() {
	const auto padding = st::introServerRowPadding;
	const auto avatarSize = st::introServerRowAvatarSize;
	const auto joinWidth = _joinButton->width();
	const auto joinHeight = _joinButton->height();
	const auto statusWidth = st::introServerRowStatusWidth;

	const auto joinLeft = width() - padding.right() - joinWidth;
	const auto joinTop = (height() - joinHeight) / 2;
	_joinButton->moveToLeft(joinLeft, joinTop);

	const auto statusRight = joinLeft - st::introServerRowJoinSkip;
	const auto statusLeft = statusRight - statusWidth;

	_status->resizeToWidth(statusWidth);
	_latency->resizeToWidth(statusWidth);
	const auto statusBlockHeight = _status->height()
		+ (_latency->isHidden()
			? 0
			: (_latency->height() + st::introServerRowStatusSkip));
	const auto statusTop = (height() - statusBlockHeight) / 2;
	_status->moveToLeft(statusLeft, statusTop);
	_latency->moveToLeft(
		statusLeft,
		statusTop + _status->height() + st::introServerRowStatusSkip);

	const auto dotSize = st::introServerRowStatusDot;
	const auto dotTop = statusTop + (_status->height() - dotSize) / 2;
	_dotRect = QRect(
		statusLeft - dotSize - st::introServerRowStatusDotSkip,
		dotTop,
		dotSize,
		dotSize);

	const auto infoLeft = padding.left()
		+ avatarSize
		+ st::introServerRowAvatarSkip;
	const auto infoRight = statusLeft - st::introServerRowJoinSkip;
	const auto infoWidth = std::max(infoRight - infoLeft, 1);

	_name->resizeToWidth(infoWidth);
	_endpoint->resizeToWidth(infoWidth);

	const auto textBlockHeight = _name->height()
		+ st::introServerRowStatusSkip
		+ _endpoint->height();
	const auto textTop = (height() - textBlockHeight) / 2;
	_name->moveToLeft(infoLeft, textTop);
	_endpoint->moveToLeft(
		infoLeft,
		textTop + _name->height() + st::introServerRowStatusSkip);

	_logo->moveToLeft(padding.left(), (height() - avatarSize) / 2);
}

void ServerRow::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	p.setClipRect(e->rect());
	PainterHighQualityEnabler hq(p);

	const auto radius = st::introServerRowRadius;
	p.setPen(Qt::NoPen);
	p.setBrush(isOver() ? st::windowBgOver : st::boxBg);
	p.drawRoundedRect(rect(), radius, radius);

	if (_dotRect.isValid()) {
		p.setPen(Qt::NoPen);
		if (!_online.has_value()) {
			const auto phase = (crl::now() - _checkingAnimation.started()) % 1200;
			const auto wave = 0.5 + 0.5 * std::sin(phase / 1200. * 6.283185307);
			auto color = st::windowSubTextFg->c;
			color.setAlpha(int(80 + 175 * wave));
			p.setBrush(color);
		} else {
			p.setBrush(*_online
				? st::windowActiveTextFg
				: st::attentionButtonFg);
		}
		p.drawEllipse(_dotRect);
	}

	paintRipple(p, 0, 0);
}

void ServerRow::resizeEvent(QResizeEvent *e) {
	RippleButton::resizeEvent(e);
	updateLayout();
}

QImage ServerRow::prepareRippleMask() const {
	return Ui::RippleAnimation::RoundRectMask(
		size(),
		st::introServerRowRadius);
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
	if (const auto remote = PolySrv::Remote();
		remote.status != PolySrv::RemoteStatus::Succeeded
		&& remote.status != PolySrv::RemoteStatus::Loading) {
		PolySrv::RefreshRemoteServers();
	}
	updateStatus();
}

void ServerSelectWidget::submit() {
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
	const auto nextTop = contentTop() + st::introNextTop;
	const auto maxHeight = nextTop
		- panelTop
		- st::introServerListBottom
		- reserved;
	return std::clamp(
		natural,
		st::introServerListMinHeight,
		std::max(maxHeight, st::introServerListMinHeight));
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
		const auto row = Ui::CreateChild<ServerRow>(
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
	goNext<QrWidget>();
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
		const auto row = static_cast<ServerRow*>(child);
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
