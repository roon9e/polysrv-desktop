/*
This file is part of PolySRV Desktop,
a Telegram Desktop fork for the PolySRV multi-server client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "intro/intro_step.h"

#include <QtCore/QString>

#include <vector>

namespace Ui {
class FlatLabel;
class LinkButton;
} // namespace Ui

namespace PolySrv {
struct Server;
} // namespace PolySrv

namespace Intro {
namespace details {

// The step that chooses the server an account signs in to.
class ServerSelectWidget : public Step {
public:
	ServerSelectWidget(
		QWidget *parent,
		not_null<Main::Account*> account,
		not_null<Data*> data);

	void submit() override;
	rpl::producer<QString> nextButtonText() const override;
	void activate() override;
	void setInnerFocus() override;

protected:
	void resizeEvent(QResizeEvent *e) override;

private:
	void fillRows();
	void updateRowsGeometry();
	void chooseServer(const QString &id);
	[[nodiscard]] PolySrv::Server currentServer() const;

	object_ptr<QWidget> _container = { nullptr };
	std::vector<Ui::LinkButton*> _rows;
	Ui::LinkButton *_addServer = nullptr;
	Ui::FlatLabel *_status = nullptr;
	QString _selectedId;

};

} // namespace details
} // namespace Intro
