/*
This file is part of PolySRV Desktop,
a Telegram Desktop fork for the PolySRV multi-server client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "polysrv/polysrv_server_box.h"

#include "boxes/abstract_box.h"
#include "lang/lang_keys.h"
#include "ui/layers/box_content.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "styles/style_layers.h"
#include "styles/style_widgets.h"

namespace PolySrv {
namespace {

constexpr auto kDefaultPort = 443;
constexpr auto kDefaultMainDcId = 2;
constexpr auto kMaxMainDcId = 5;

[[nodiscard]] QString ServerRowLabel(const Server &server) {
	auto result = server.name;
	if (result.isEmpty()) {
		result = server.endpoint();
	} else if (const auto endpoint = server.endpoint(); !endpoint.isEmpty()) {
		result += u" \u00b7 "_q + endpoint;
	}
	return result;
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

} // namespace

void ShowServerListBox() {
	Ui::show(Box([](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::lng_polysrv_servers_title());

		for (const auto &server : Servers()) {
			const auto label = ServerRowLabel(server);
			if (!IsCustomServer(server.id)) {
				box->addRow(object_ptr<Ui::FlatLabel>(
					box,
					label,
					st::boxLabel));
				continue;
			}
			const auto id = server.id;
			const auto row = box->addRow(object_ptr<Ui::LinkButton>(
				box,
				label,
				st::boxLinkButton));
			row->setClickedCallback([=] {
				if (const auto found = FindServer(id)) {
					ShowServerEditBox(*found);
				}
			});
		}

		box->addButton(tr::lng_polysrv_add_server(), [] {
			ShowServerEditBox(std::nullopt);
		});
		box->addButton(tr::lng_close(), [=] {
			box->closeBox();
		});
	}), Ui::LayerOption::KeepOther);
}

void ShowServerEditBox(std::optional<Server> server) {
	const auto initial = server.value_or(Server());
	const auto editingId = initial.id;
	const auto editing = server.has_value() && IsCustomServer(editingId);
	Ui::show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(editing
			? tr::lng_polysrv_edit_server()
			: tr::lng_polysrv_new_server());

		const auto name = AddField(
			box,
			tr::lng_polysrv_server_name(),
			initial.name);
		const auto description = AddField(
			box,
			tr::lng_polysrv_server_description_field(),
			initial.description);
		const auto host = AddField(
			box,
			tr::lng_polysrv_server_host(),
			initial.host,
			Qt::ImhUrlCharactersOnly
				| Qt::ImhNoAutoUppercase
				| Qt::ImhNoPredictiveText);
		const auto port = AddField(
			box,
			tr::lng_polysrv_server_port(),
			QString::number(initial.port > 0 ? initial.port : kDefaultPort),
			Qt::ImhDigitsOnly | Qt::ImhNoPredictiveText);
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

		const auto multiDc = box->addRow(object_ptr<Ui::Checkbox>(
			box,
			tr::lng_polysrv_server_multi_dc(tr::now),
			initial.multiDc,
			st::defaultBoxCheckbox));

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
			const auto written = editing
				? UpdateCustomServer(
					editingId,
					serverName,
					serverHost,
					serverPort,
					serverDescription,
					serverKey,
					isMultiDc,
					mainDcId)
				: AddCustomServer(
					serverName,
					serverHost,
					serverPort,
					serverDescription,
					serverKey,
					isMultiDc,
					mainDcId);
			if (!written) {
				host->showError();
				return;
			}
			box->closeBox();
		};
		box->addButton(tr::lng_settings_save(), save);
		if (editing) {
			box->addButton(tr::lng_box_remove(), [=] {
				if (RemoveCustomServer(editingId)) {
					box->closeBox();
				}
			}, st::attentionBoxButton);
		}
		box->addButton(tr::lng_cancel(), [=] {
			box->closeBox();
		});
		box->setFocusCallback([=] {
			name->setFocusFast();
		});
	}), Ui::LayerOption::KeepOther);
}

} // namespace PolySrv
