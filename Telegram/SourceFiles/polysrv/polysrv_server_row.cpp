/*
This file is part of PolySRV Desktop, a Telegram Desktop fork for the
PolySRV multi-server client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "polysrv/polysrv_server_row.h"

#include "base/weak_ptr.h"
#include "lang/lang_keys.h"
#include "polysrv/polysrv_server_box.h"
#include "styles/style_intro.h"
#include "ui/effects/ripple_animation.h"
#include "ui/painter.h"
#include "ui/widgets/labels.h"

#include <algorithm>
#include <cmath>

namespace PolySrv {

ServerRow::ServerRow(
	QWidget *parent,
	Server server,
	Fn<void()> details,
	Fn<void()> join,
	bool checkStatus)
: RippleButton(parent, st::defaultRippleAnimation)
, _server(std::move(server))
, _details(std::move(details))
, _join(std::move(join))
, _checkStatus(checkStatus)
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
		PaintServerLogo(p, size, _server);
	}, _logo->lifetime());

	_name->setAttribute(Qt::WA_TransparentForMouseEvents);
	_endpoint->setAttribute(Qt::WA_TransparentForMouseEvents);
	_status->setAttribute(Qt::WA_TransparentForMouseEvents);
	_latency->setAttribute(Qt::WA_TransparentForMouseEvents);
	_latency->hide();

	setClickedCallback(_details);

	if (_join) {
		_joinButton->setClickedCallback([=] {
			_join();
		});
	} else {
		_joinButton->hide();
	}
	if (_checkStatus) {
		_checkingAnimation.init([=](crl::time) {
			if (_online.has_value()) {
				return false;
			}
			update();
			return true;
		});
		_checkingAnimation.start();
		CheckServerOnline(_server, crl::guard(this, [=](
				bool online,
				int latencyMs) {
			setStatus(online, latencyMs);
		}));
	} else {
		_status->hide();
	}

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
	const auto joinWidth = _join ? _joinButton->width() : 0;
	const auto joinHeight = _joinButton->height();

	const auto joinLeft = width() - padding.right() - joinWidth;
	_joinButton->moveToLeft(joinLeft, (height() - joinHeight) / 2);

	const auto statusWidth = _checkStatus ? st::introServerRowStatusWidth : 0;
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

	if (_checkStatus) {
		const auto dotSize = st::introServerRowStatusDot;
		const auto dotTop = statusTop + (_status->height() - dotSize) / 2;
		_dotRect = QRect(
			statusLeft - dotSize - st::introServerRowStatusDotSkip,
			dotTop,
			dotSize,
			dotSize);
	} else {
		_dotRect = QRect();
	}

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

} // namespace PolySrv
