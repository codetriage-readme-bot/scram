/*
 * Copyright (C) 2017 Olzhas Rakhimov
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef EVENTDIALOG_H
#define EVENTDIALOG_H

#include <memory>

#include <QDialog>
#include <QStatusBar>

#include "ui_eventdialog.h"

#include "src/model.h"

namespace scram {
namespace gui {

class EventDialog : public QDialog, public Ui::EventDialog
{
    Q_OBJECT

public:
    explicit EventDialog(mef::Model *model, QWidget *parent = nullptr);

signals:
    void validated(bool valid);

public slots:
    void validate();

private:
    mef::Model *m_model;
    QStatusBar *m_errorBar;
};

} // namespace gui
} // namespace scram

#endif // EVENTDIALOG_H
