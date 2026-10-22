/*
This file is part of PolySRV Desktop,
a Telegram Desktop fork for the PolySRV multi-server client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "intro/intro_step.h"

#include "polysrv/polysrv_servers.h"

#include <QtCore/QPointer>

namespace Ui {
class FlatLabel;
class RoundButton;
class RpWidget;
class ScrollArea;
} // namespace Ui

namespace Intro {
namespace details {

// The step that chooses the server an account signs in to.
class ServerSelectWidget final : public Step {
public:
	ServerSelectWidget(
		QWidget *parent,
		not_null<Main::Account*> account,
		not_null<Data*> data);

	void finishInit() override;
	void activate() override;
	void submit() override;
	[[nodiscard]] rpl::producer<QString> nextButtonText() const override;

	void resizeEvent(QResizeEvent *e) override;

private:
	void rebuildList();
	void showDetails(const PolySrv::Server &server);
	void joinServer(const PolySrv::Server &server);
	void proceedJoin(const PolySrv::Server &server);
	void updateListGeometry();
	void updateRowsGeometry();
	void updateStatus();
	[[nodiscard]] int listWidth() const;
	[[nodiscard]] int listInnerWidth() const;
	[[nodiscard]] int listPanelHeight(int panelTop) const;

	QPointer<Ui::RpWidget> _panel;
	QPointer<Ui::ScrollArea> _scroll;
	QPointer<Ui::RpWidget> _rowsContainer;
	QPointer<Ui::FlatLabel> _ownTitle;
	QPointer<Ui::FlatLabel> _ownDescription;
	QPointer<Ui::FlatLabel> _status;
	object_ptr<Ui::RoundButton> _addServer;
};

} // namespace details
} // namespace Intro
