/*
This file is part of PolySRV Desktop,
a Telegram Desktop fork for the PolySRV multi-server client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "polysrv/polysrv_server_box.h"
#include "polysrv/polysrv_server_row.h"

#include "boxes/abstract_box.h"
#include "core/application.h"
#include "core/file_utilities.h"
#include "lang/lang_keys.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "styles/style_intro.h"
#include "styles/style_layers.h"
#include "styles/style_widgets.h"
#include "ui/boxes/confirm_box.h"
#include "ui/effects/animations.h"
#include "ui/effects/ripple_animation.h"
#include "ui/layers/box_content.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"

#include <QtCore/QFile>
#include <QtCore/QPointer>
#include <QtGui/QFontMetrics>
#include <QtGui/QImage>
#include <QtGui/QPainterPath>
#include <QtGui/QPixmap>

namespace PolySrv {
namespace {

constexpr auto kDefaultPort = 443;
constexpr auto kDefaultMainDcId = 2;
constexpr auto kMaxMainDcId = 5;

[[nodiscard]] QString LogoPathFor(const Server &server) {
	return PolySrv::LocalLogoPath(server.logoPath);
}

[[nodiscard]] std::vector<not_null<Main::Account*>> AccountsUsingServer(
		const Server &server) {
	auto result = std::vector<not_null<Main::Account*>>();
	const auto scope = PolySrv::ServerScopeFor(server);
	if (scope.isEmpty()) {
		return result;
	}
	for (const auto &entry : Core::App().domain().accounts()) {
		if (entry.account->serverScope() == scope) {
			result.push_back(entry.account.get());
		}
	}
	return result;
}

// The scope of an account is the host it joined with, so the host, the
// port and the key cannot change under an account that already uses it.
[[nodiscard]] bool RebindsAccounts(
		const Server &server,
		const QString &host,
		int port,
		const QString &rsaPublicKey) {
	return server.primaryHost().compare(host, Qt::CaseInsensitive) != 0
		|| server.primaryPort() != port
		|| server.rsaPublicKey != rsaPublicKey;
}

void MoveAccountsToServer(
		const std::vector<not_null<Main::Account*>> &accounts,
		const Server &server) {
	for (const auto &account : accounts) {
		account->applyServer(
			server.id,
			server.primaryHost(),
			server.primaryPort());
	}
}

void DeleteServer(const Server &server, Fn<void()> changed) {
	const auto affected = AccountsUsingServer(server);
	const auto text = affected.empty()
		? tr::lng_polysrv_server_delete_confirm(
			tr::now,
			lt_name,
			server.name)
		: tr::lng_polysrv_server_delete_confirm_accounts(
			tr::now,
			lt_count,
			float64(affected.size()),
			lt_name,
			server.name);
	Ui::show(Ui::MakeConfirmBox({
		.text = text,
		.confirmed = [=](Fn<void()> close) {
			for (const auto &account : affected) {
				account->logOut();
			}
			if (RemoveCustomServer(server.id) && changed) {
				changed();
			}
			close();
		},
		.confirmText = tr::lng_polysrv_server_delete(),
		.confirmStyle = &st::attentionBoxButton,
	}), Ui::LayerOption::KeepOther);
}

class DetailRow final : public Ui::RpWidget {
public:
	DetailRow(
		QWidget *parent,
		const QString &label,
		const QString &value);

	[[nodiscard]] int resizeGetHeight(int newWidth) override;

private:
	object_ptr<Ui::FlatLabel> _label;
	object_ptr<Ui::FlatLabel> _value;
};

DetailRow::DetailRow(
	QWidget *parent,
	const QString &label,
	const QString &value)
: RpWidget(parent)
, _label(this, label, st::introServerDetailsLabel)
, _value(this, value, st::introServerDetailsValue) {
}

int DetailRow::resizeGetHeight(int newWidth) {
	const auto labelWidth = st::introServerDetailsLabelWidth;
	_label->resizeToWidth(labelWidth);
	const auto valueWidth = std::max(
		newWidth - labelWidth - st::introServerDetailsRowSkip,
		1);
	_value->resizeToWidth(valueWidth);
	const auto rowHeight = std::max(_label->height(), _value->height());
	_label->moveToLeft(0, (rowHeight - _label->height()) / 2);
	_value->moveToLeft(
		labelWidth + st::introServerDetailsRowSkip,
		(rowHeight - _value->height()) / 2);
	return rowHeight;
}

class DescriptionText final : public Ui::RpWidget {
public:
	DescriptionText(QWidget *parent, const QString &text);

	[[nodiscard]] int resizeGetHeight(int newWidth) override;

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	QString _text;
};

DescriptionText::DescriptionText(QWidget *parent, const QString &text)
: RpWidget(parent)
, _text(text) {
}

int DescriptionText::resizeGetHeight(int newWidth) {
	if (_text.isEmpty() || newWidth <= 0) {
		return st::introServerDetailsDescription.style.font->height;
	}
	const auto metrics = QFontMetrics(st::introServerDetailsDescription.style.font->f);
	const auto bounds = metrics.boundingRect(
		QRect(0, 0, newWidth, 99999),
		Qt::TextWordWrap | Qt::TextWrapAnywhere,
		_text);
	return std::max(bounds.height(), 1);
}

void DescriptionText::paintEvent(QPaintEvent *e) {
	if (_text.isEmpty()) {
		return;
	}
	const auto &style = st::introServerDetailsDescription;
	auto p = QPainter(this);
	p.setFont(style.style.font->f);
	p.setPen(style.textFg->c);
	p.drawText(
		rect(),
		Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap | Qt::TextWrapAnywhere,
		_text);
}

class DescriptionSection final : public Ui::RpWidget {
public:
	DescriptionSection(QWidget *parent, const QString &text);

	[[nodiscard]] int resizeGetHeight(int newWidth) override;

private:
	object_ptr<Ui::FlatLabel> _title;
	object_ptr<Ui::RpWidget> _box;
	DescriptionText *_text = nullptr;
};

DescriptionSection::DescriptionSection(QWidget *parent, const QString &text)
: RpWidget(parent)
, _title(
	this,
	tr::lng_polysrv_server_details_description(tr::now),
	st::introServerDetailsSectionTitle)
, _box(this) {
	_text = Ui::CreateChild<DescriptionText>(_box.data(), text);
	_box->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(_box.data());
		PainterHighQualityEnabler hq(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBgOver);
		const auto radius = st::introServerDetailsDescriptionRadius;
		p.drawRoundedRect(_box->rect(), radius, radius);
	}, _box->lifetime());
	setNaturalWidth(-1);
}

int DescriptionSection::resizeGetHeight(int newWidth) {
	const auto titleSkip = st::introServerDetailsRowSkip;
	const auto padding = st::introServerDetailsDescriptionPadding;
	const auto innerWidth = std::max(
		newWidth - padding.left() - padding.right(),
		1);

	_title->resizeToWidth(newWidth);
	_title->moveToLeft(0, 0);

	_text->resizeToWidth(innerWidth);
	_text->moveToLeft(padding.left(), padding.top());

	const auto boxTop = _title->height() + titleSkip;
	const auto naturalBoxHeight = _text->height()
		+ padding.top()
		+ padding.bottom();
	const auto boxHeight = std::max(
		naturalBoxHeight,
		st::introServerDetailsDescriptionMinHeight);

	_box->setGeometry(0, boxTop, newWidth, boxHeight);

	return boxTop + boxHeight;
}

class HeaderRow final : public Ui::RippleButton {
public:
	HeaderRow(QWidget *parent, Server server, bool editable = false);

	void setName(const QString &text);
	[[nodiscard]] QString logoPath() const;
	void setLogoPath(const QString &path);

protected:
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	[[nodiscard]] QImage prepareRippleMask() const override;

private:
	[[nodiscard]] Server LogoServer() const;

	const bool _editable = false;
	QString _nameText;
	QString _logoPath;
	object_ptr<Ui::RpWidget> _logo;
	object_ptr<Ui::FlatLabel> _name;
	object_ptr<Ui::FlatLabel> _hint;
};

HeaderRow::HeaderRow(QWidget *parent, Server server, bool editable)
: RippleButton(parent, st::defaultRippleAnimation)
, _editable(editable)
, _nameText(std::move(server.name))
, _logoPath(std::move(server.logoPath))
, _logo(this)
, _name(this, _nameText, st::introServerCardName)
, _hint(
	this,
	tr::lng_polysrv_server_logo_choose(tr::now),
	st::introServerRowEndpoint) {
	const auto logoSize = st::introServerCardLogo;
	_logo->resize(logoSize, logoSize);
	_logo->setAttribute(Qt::WA_TransparentForMouseEvents);
	_name->setAttribute(Qt::WA_TransparentForMouseEvents);
	_hint->setAttribute(Qt::WA_TransparentForMouseEvents);
	_logo->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(_logo.data());
		PaintServerLogo(p, logoSize, LogoServer());
	}, _logo->lifetime());
	if (_editable) {
		_hint->show();
		resize(parent->width(), st::introServerLogoRowHeight);
	} else {
		_hint->hide();
		setAttribute(Qt::WA_TransparentForMouseEvents);
		resize(parent->width(), logoSize);
	}
}

void HeaderRow::setName(const QString &text) {
	if (_nameText == text) {
		return;
	}
	_nameText = text;
	_name->setText(text);
	_logo->update();
}

QString HeaderRow::logoPath() const {
	return _logoPath;
}

void HeaderRow::setLogoPath(const QString &path) {
	if (_logoPath == path) {
		return;
	}
	_logoPath = path;
	_logo->update();
}

Server HeaderRow::LogoServer() const {
	auto server = Server();
	server.name = _nameText;
	server.logoPath = _logoPath;
	return server;
}

void HeaderRow::resizeEvent(QResizeEvent *e) {
	RippleButton::resizeEvent(e);
	const auto logoSize = st::introServerCardLogo;
	_logo->moveToLeft(0, (height() - logoSize) / 2);
	const auto nameLeft = logoSize + st::introServerDetailsHeaderSkip;
	const auto nameWidth = std::max(width() - nameLeft, 1);
	_name->resizeToWidth(nameWidth);
	_hint->resizeToWidth(nameWidth);
	const auto block = _name->height()
		+ (_hint->isHidden() ? 0 : _hint->height());
	const auto top = (height() - block) / 2;
	_name->moveToLeft(nameLeft, top);
	_hint->moveToLeft(nameLeft, top + _name->height());
}

void HeaderRow::paintEvent(QPaintEvent *e) {
	if (!_editable) {
		return;
	}
	auto p = Painter(this);
	p.setClipRect(e->rect());
	PainterHighQualityEnabler hq(p);
	const auto radius = st::introServerRowRadius;
	p.setPen(Qt::NoPen);
	p.setBrush(isOver() ? st::windowBgOver : st::boxBg);
	p.drawRoundedRect(rect(), radius, radius);
	paintRipple(p, 0, 0);
}

QImage HeaderRow::prepareRippleMask() const {
	return Ui::RippleAnimation::RoundRectMask(
		size(),
		st::introServerRowRadius);
}

class StatusBlock final : public Ui::RpWidget {
public:
	StatusBlock(QWidget *parent);

	void setOnline(bool online, int latencyMs);

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private:
	object_ptr<Ui::FlatLabel> _status;
	object_ptr<Ui::FlatLabel> _latency;
	std::optional<bool> _online;
};

StatusBlock::StatusBlock(QWidget *parent)
: RpWidget(parent)
, _status(
	this,
	tr::lng_polysrv_server_checking(tr::now),
	st::introServerCardStatusOnline)
, _latency(this, QString(), st::introServerDetailsStatusLatency) {
	const auto padding = st::introServerDetailsStatusBlock;
	resize(parent->width(), padding.top() + _status->height() + padding.bottom());
}

void StatusBlock::setOnline(bool online, int latencyMs) {
	_online = online;
	_status->setText(online
		? tr::lng_polysrv_server_online(tr::now)
		: tr::lng_polysrv_server_offline(tr::now));
	_status->setTextColorOverride(online
		? st::windowActiveTextFg->c
		: st::attentionButtonFg->c);
	_latency->setText((online && latencyMs >= 0)
		? tr::lng_polysrv_server_latency(
			tr::now,
			lt_latency,
			QString::number(latencyMs))
		: QString());
	const auto padding = st::introServerDetailsStatusBlock;
	resize(
		width(),
		padding.top() + _status->height() + padding.bottom());
	update();
}

void StatusBlock::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	p.setClipRect(e->rect());
	PainterHighQualityEnabler hq(p);

	p.setPen(Qt::NoPen);
	p.setBrush(st::windowBgOver);
	const auto radius = st::introServerDetailsStatusRadius;
	p.drawRoundedRect(rect(), radius, radius);

	if (_online.has_value()) {
		const auto padding = st::introServerDetailsStatusBlock;
		const auto dotSize = st::introServerCardStatusDot;
		const auto dotLeft = padding.left();
		const auto dotTop = padding.top()
			+ (_status->height() - dotSize) / 2;
		p.setPen(Qt::NoPen);
		p.setBrush(*_online ? st::windowActiveTextFg : st::attentionButtonFg);
		p.drawEllipse(QRectF(dotLeft, dotTop, dotSize, dotSize));
	}
}

void StatusBlock::resizeEvent(QResizeEvent *e) {
	RpWidget::resizeEvent(e);
	const auto padding = st::introServerDetailsStatusBlock;
	const auto innerLeft = padding.left();
	const auto innerWidth = width() - padding.left() - padding.right();
	const auto statusLeft = innerLeft
		+ st::introServerCardStatusDot
		+ st::introServerDetailsStatusDotSkip;
	_status->resizeToWidth(std::max(innerWidth / 2, 1));
	_status->moveToLeft(statusLeft, padding.top());
	_latency->resizeToWidth(innerWidth);
	_latency->moveToLeft(innerLeft, padding.top());
	const auto blockHeight = padding.top()
		+ _status->height()
		+ padding.bottom();
	if (height() != blockHeight) {
		resize(width(), blockHeight);
	}
}

class ServerDetailsBox final : public Ui::BoxContent {
public:
	ServerDetailsBox(
		QWidget*,
		Server server,
		Fn<void(const Server &)> join,
		Fn<void()> changed);

protected:
	void prepare() override;

private:
	Server _server;
	Fn<void(const Server &)> _join;
	Fn<void()> _changed;
	object_ptr<Ui::VerticalLayout> _content;
	StatusBlock *_statusBlock = nullptr;
};

ServerDetailsBox::ServerDetailsBox(
	QWidget*,
	Server server,
	Fn<void(const Server &)> join,
	Fn<void()> changed)
: _server(std::move(server))
, _join(std::move(join))
, _changed(std::move(changed))
, _content(this) {
	const auto rowMargin = st::boxRowPadding + QMargins(
		0,
		st::introServerDetailsRowSkip,
		0,
		0);
	const auto sectionMargin = st::boxRowPadding + QMargins(
		0,
		st::introServerDetailsSectionSkip,
		0,
		0);

	_content->add(
		object_ptr<HeaderRow>(_content.data(), _server),
		st::boxRowPadding);
	_content->add(
		object_ptr<DetailRow>(
			_content.data(),
			tr::lng_polysrv_server_details_host(tr::now),
			_server.host),
		rowMargin);
	_content->add(
		object_ptr<DetailRow>(
			_content.data(),
			tr::lng_polysrv_server_details_port(tr::now),
			_server.primaryPort() > 0
				? QString::number(_server.primaryPort())
				: u"\u2014"_q),
		rowMargin);
	_content->add(
		object_ptr<DescriptionSection>(
			_content.data(),
			_server.description.isEmpty()
				? u"\u2014"_q
				: _server.description),
		sectionMargin);
	_content->add(
		object_ptr<Ui::FlatLabel>(
			_content.data(),
			tr::lng_polysrv_server_details_status(tr::now),
			st::introServerDetailsSectionTitle),
		sectionMargin);
	_statusBlock = _content->add(
		object_ptr<StatusBlock>(_content.data()),
		st::boxRowPadding + QMargins(0, st::introServerDetailsRowSkip, 0, 0));
}

void ServerDetailsBox::prepare() {
	setTitle(tr::lng_polysrv_server_details_title());
	if (_join) {
		if (IsRemovableServer(_server)) {
			addLeftButton(tr::lng_polysrv_server_delete(), [=] {
				DeleteServer(_server, crl::guard(this, [=] {
					if (_changed) {
						_changed();
					}
					closeBox();
				}));
			}, st::attentionBoxButton);
		}
		addButton(tr::lng_polysrv_server_join(), [=] {
			_join(_server);
			closeBox();
		});
		addButton(tr::lng_cancel(), [=] { closeBox(); });
		if (IsRemovableServer(_server)) {
			addButton(tr::lng_proxy_menu_edit(), [=] {
				const auto changed = _changed;
				closeBox();
				ShowServerEditBox(_server, changed);
			});
		}
	} else {
		if (IsRemovableServer(_server)) {
			addLeftButton(tr::lng_polysrv_server_delete(), [=] {
				DeleteServer(_server, crl::guard(this, [=] {
					if (_changed) {
						_changed();
					}
					closeBox();
				}));
			}, st::attentionBoxButton);
			addButton(tr::lng_proxy_menu_edit(), [=] {
				const auto changed = _changed;
				closeBox();
				ShowServerEditBox(_server, changed);
			});
		}
		addButton(tr::lng_close(), [=] { closeBox(); });
	}
	setDimensionsToContent(st::boxWidth, _content.data());
	CheckServerOnline(_server, crl::guard(this, [=](bool online, int latency) {
		if (_statusBlock) {
			_statusBlock->setOnline(online, latency);
		}
	}));
}

[[nodiscard]] not_null<Ui::InputField*> AddField(
		not_null<Ui::GenericBox*> box,
		rpl::producer<QString> placeholder,
		const QString &value,
		Qt::InputMethodHints hints = Qt::ImhNone) {
	const auto field = box->addRow(object_ptr<Ui::InputField>(
		box,
		st::defaultInputField,
		Ui::InputField::Mode::NoNewlines,
		std::move(placeholder),
		value));
	field->setInputMethodHints(hints);
	return field;
}

[[nodiscard]] bool ValidMainDcId(const QString &text) {
	const auto value = text.toInt();
	return value >= 1 && value <= kMaxMainDcId;
}

// The host and the port sit on one row, each with its own field, so the two
// stay separate but read as one address.
class PairFieldsRow final : public Ui::RpWidget {
public:
	PairFieldsRow(
		QWidget *parent,
		rpl::producer<QString> leftPlaceholder,
		const QString &leftValue,
		Qt::InputMethodHints leftHints,
		rpl::producer<QString> rightPlaceholder,
		const QString &rightValue,
		Qt::InputMethodHints rightHints)
	: RpWidget(parent)
	, _left(
		this,
		st::defaultInputField,
		Ui::InputField::Mode::NoNewlines,
		std::move(leftPlaceholder),
		leftValue)
	, _right(
		this,
		st::defaultInputField,
		Ui::InputField::Mode::NoNewlines,
		std::move(rightPlaceholder),
		rightValue) {
		_left->setInputMethodHints(leftHints);
		_right->setInputMethodHints(rightHints);
	}

	[[nodiscard]] not_null<Ui::InputField*> left() const {
		return _left.data();
	}
	[[nodiscard]] not_null<Ui::InputField*> right() const {
		return _right.data();
	}

protected:
	int resizeGetHeight(int newWidth) override {
		const auto rightWidth = st::introServerPortWidth;
		const auto gap = st::introServerFieldGap;
		_right->resizeToWidth(rightWidth);
		_left->resizeToWidth(std::max(newWidth - rightWidth - gap, 1));
		_left->moveToLeft(0, 0);
		_right->moveToLeft(newWidth - rightWidth, 0);
		return std::max(_left->height(), _right->height());
	}

private:
	object_ptr<Ui::InputField> _left;
	object_ptr<Ui::InputField> _right;
};

// The chosen logo as a thumbnail and the picker itself: the picture is the
// button that adds or replaces the image, never a text field.
} // namespace

void PaintServerLogo(QPainter &p, int size, const Server &server) {
	PainterHighQualityEnabler hq(p);
	const auto path = LogoPathFor(server);
	if (path.isEmpty()) {
		static const QColor kColors[7] = {
			QColor(0xE5, 0x39, 0x35),
			QColor(0xE6, 0x4A, 0x19),
			QColor(0x43, 0xA0, 0x47),
			QColor(0x00, 0x88, 0x8D),
			QColor(0x19, 0x76, 0xD2),
			QColor(0x51, 0x2D, 0xA8),
			QColor(0xF5, 0x7F, 0x17),
		};
		p.setPen(Qt::NoPen);
		p.setBrush(kColors[int(qHash(server.name) % 7u)]);
		p.drawEllipse(0, 0, size, size);
		const auto ch = server.name.isEmpty()
			? QChar('?')
			: server.name.at(0).toUpper();
		p.setPen(Qt::white);
		auto font = p.font();
		font.setPixelSize(size / 2);
		font.setBold(true);
		p.setFont(font);
		p.drawText(QRect(0, 0, size, size), Qt::AlignCenter, ch);
		return;
	}
	const auto circleMask = !path.startsWith(u":/"_q);
	const auto image = QPixmap(path).scaled(
		size,
		size,
		circleMask
			? Qt::KeepAspectRatioByExpanding
			: Qt::KeepAspectRatio,
		Qt::SmoothTransformation);
	const auto left = (size - image.width()) / 2;
	const auto top = (size - image.height()) / 2;
	if (circleMask) {
		p.setPen(Qt::NoPen);
		p.setBrush(st::boxBg);
		p.drawEllipse(0, 0, size, size);
		auto clipPath = QPainterPath();
		clipPath.addEllipse(0, 0, size, size);
		p.setClipPath(clipPath);
	}
	p.drawPixmap(left, top, image);
}

void ShowServerDetailsBox(
		Server server,
		Fn<void(const Server &)> join,
		Fn<void()> changed) {
	Ui::show(
		Box<ServerDetailsBox>(std::move(server), std::move(join), std::move(changed)),
		Ui::LayerOption::KeepOther);
}

void ShowServerListBox() {
	RefreshServerLogos();
	Ui::show(Box([](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::lng_polysrv_servers_title());
		box->setWidth(st::boxWideWidth);

		const auto fill = [=] {
			box->verticalLayout()->clear();
			for (const auto &server : Servers()) {
				if (!server.valid()) {
					continue;
				}
				const auto id = server.id;
				const auto row = box->addRow(
					object_ptr<ServerRow>(
						box,
						server,
						[=] {
							if (const auto found = FindServer(id)) {
								ShowServerDetailsBox(*found, nullptr);
							}
						},
						nullptr,
						false),
					st::boxRowPadding
						+ QMargins(0, st::introServerRowSpacing, 0, 0));
				row->show();
			}
		};
		fill();
		ServersChanged() | rpl::on_next(fill, box->lifetime());

		box->addButton(tr::lng_polysrv_add_server(), [] {
			ShowServerEditBox(std::nullopt);
		});
		box->addButton(tr::lng_close(), [=] {
			box->closeBox();
		});
	}), Ui::LayerOption::KeepOther);
}

void ShowServerEditBox(std::optional<Server> server, Fn<void()> changed) {
	RefreshServerLogos();
	const auto initial = server.value_or(Server());
	const auto editingId = initial.id;
	const auto editing = server.has_value() && IsCustomServer(editingId);
	Ui::show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(editing
			? tr::lng_polysrv_edit_server()
			: tr::lng_polysrv_add_server());
		box->setWidth(st::boxWideWidth);

		const auto header = box->addRow(
			object_ptr<HeaderRow>(box, initial, true),
			st::boxRowPadding);
		header->setClickedCallback([=] {
			const auto weak = QPointer<Ui::GenericBox>(box.get());
			FileDialog::GetOpenPath(
				box.get(),
				tr::lng_polysrv_server_logo_choose(tr::now),
				u"Images (*.png *.jpg *.jpeg *.webp *.bmp)"_q,
				[=](FileDialog::OpenResult &&result) {
					if (!weak || result.paths.isEmpty()) {
						return;
					}
					header->setLogoPath(result.paths.front());
				});
		});

		const auto name = AddField(
			box,
			tr::lng_polysrv_server_name(),
			initial.name);
		const auto description = AddField(
			box,
			tr::lng_polysrv_server_description_field(),
			initial.description);
		const auto address = box->addRow(
			object_ptr<PairFieldsRow>(
				box,
				tr::lng_polysrv_server_host(),
				initial.host,
				Qt::ImhUrlCharactersOnly
					| Qt::ImhNoAutoUppercase
					| Qt::ImhNoPredictiveText,
				tr::lng_polysrv_server_port(),
				QString::number(
					initial.port > 0 ? initial.port : kDefaultPort),
				Qt::ImhDigitsOnly | Qt::ImhNoPredictiveText),
			st::boxRowPadding);
		const auto host = address->left();
		const auto port = address->right();
		const auto mainDc = AddField(
			box,
			tr::lng_polysrv_server_main_dc(),
			QString::number(initial.mainDcId > 0
				? initial.mainDcId
				: kDefaultMainDcId),
			Qt::ImhDigitsOnly | Qt::ImhNoPredictiveText);
		const auto key = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::defaultInputField,
			Ui::InputField::Mode::MultiLine,
			tr::lng_polysrv_server_rsa_key(),
			initial.rsaPublicKey));
		key->setInputMethodHints(
			Qt::ImhNoAutoUppercase | Qt::ImhNoPredictiveText);

		const auto multiDc = box->addRow(
			object_ptr<Ui::Checkbox>(
				box,
				tr::lng_polysrv_server_multi_dc(tr::now),
				initial.multiDc,
				st::defaultBoxCheckbox),
			st::boxRowPadding + QMargins(0, st::introServerFieldSkip, 0, 0));

		const auto previewText = [=] {
			const auto typed = name->getLastText().trimmed();
			if (!typed.isEmpty()) {
				return typed;
			}
			return initial.name.isEmpty()
				? tr::lng_polysrv_add_server(tr::now)
				: initial.name;
		};
		const auto updatePreview = [=] {
			header->setName(previewText());
		};
		header->setName(previewText());
		name->changes() | rpl::on_next(updatePreview, box->lifetime());
		host->changes() | rpl::on_next(updatePreview, box->lifetime());

		const auto save = [=] {
			const auto serverName = name->getLastText().trimmed();
			const auto serverHost = host->getLastText().trimmed();
			const auto serverPort = port->getLastText().trimmed().toInt();
			const auto mainDcText = mainDc->getLastText().trimmed();
			if (serverName.isEmpty()) {
				name->showError();
				return;
			}
			if (serverHost.isEmpty()) {
				host->showError();
				return;
			}
			if (serverPort < 1 || serverPort > 65535) {
				port->showError();
				return;
			}
			if (!ValidMainDcId(mainDcText)) {
				mainDc->showError();
				return;
			}
			const auto isMultiDc = multiDc->checked();
			const auto mainDcId = isMultiDc
				? kDefaultMainDcId
				: mainDcText.toInt();
			const auto serverDescription = description->getLastText().trimmed();
			const auto serverKey = key->getLastText().trimmed();
			const auto serverLogo = header->logoPath().trimmed();
			const auto write = [=]() -> std::optional<Server> {
				const auto written = editing
					? UpdateCustomServer(
						editingId,
						serverName,
						serverHost,
						serverPort,
						serverDescription,
						serverKey,
						isMultiDc,
						mainDcId,
						serverLogo)
					: AddCustomServer(
						serverName,
						serverHost,
						serverPort,
						serverDescription,
						serverKey,
						isMultiDc,
						mainDcId,
						serverLogo);
				if (!written) {
					host->showError();
				}
				return written;
			};
			if (editing) {
				const auto previous = FindServer(editingId);
				const auto affected = previous
					? AccountsUsingServer(*previous)
					: std::vector<not_null<Main::Account*>>();
				if (previous
					&& !affected.empty()
					&& RebindsAccounts(
						*previous,
						serverHost,
						serverPort,
						serverKey)) {
					const auto weak = QPointer<Ui::GenericBox>(box.get());
					Ui::show(Ui::MakeConfirmBox({
						.text = tr::lng_polysrv_server_move_confirm(
							tr::now,
							lt_count,
							float64(affected.size()),
							lt_name,
							serverName),
						.confirmed = [=](Fn<void()> close) {
							if (!weak) {
								close();
								return;
							}
							if (const auto moved = write()) {
								if (changed) {
									changed();
								}
								if (const auto editBox = weak.data()) {
									editBox->closeBox();
								}
								MoveAccountsToServer(affected, *moved);
							}
							close();
						},
						.confirmText = tr::lng_polysrv_server_move(),
						.confirmStyle = &st::attentionBoxButton,
					}), Ui::LayerOption::KeepOther);
					return;
				}
			}
			if (!write()) {
				return;
			}
			if (changed) {
				changed();
			}
			box->closeBox();
		};
		box->addButton(tr::lng_settings_save(), save);
		box->addButton(tr::lng_cancel(), [=] {
			box->closeBox();
		});
		box->setFocusCallback([=] {
			name->setFocusFast();
		});
	}), Ui::LayerOption::KeepOther);
}

} // namespace PolySrv
