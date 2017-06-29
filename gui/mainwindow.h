/*
 * Copyright (C) 2015-2017 Olzhas Rakhimov
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

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <QAction>
#include <QComboBox>
#include <QDir>
#include <QMainWindow>
#include <QRegularExpressionValidator>
#include <QTreeWidgetItem>
#include <QUndoStack>

#include "src/model.h"
#include "src/risk_analysis.h"
#include "src/settings.h"

#include "model.h"
#include "zoomableview.h"

namespace Ui {
class MainWindow;
}

namespace scram {
namespace gui {

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    void setConfig(const std::string &configPath,
                   std::vector<std::string> inputFiles = {});
    void addInputFiles(const std::vector<std::string> &inputFiles);

signals:
    void configChanged();

private slots:
    /**
     * @brief Opens a new project configuration.
     *
     * The current project and input files are reset.
     */
    void createNewModel();

    /**
     * @brief Opens model files.
     */
    void openFiles(QString directory = QDir::homePath());

    /**
     * @brief Saves the project to a file.
     *
     * If the project is new,
     * it does not have a default destination file.
     * The user is required to specify the file upon save.
     */
    void saveModel();

    /**
     * @brief Saves the project to a potentially different file.
     */
    void saveModelAs();

    /**
     * @brief Exports the current active document/diagram.
     */
    void exportAs();

    /**
     * Activates the Zoom actions
     * and updates the displayed zoom level.
     */
    void activateZoom(int level);

    /**
     * Disables the Zoom actions.
     */
    void deactivateZoom();

private:
    void setupActions(); ///< Setup all the actions with connections.

    void setupZoomableView(ZoomableView *view); ///< Connect to actions.

    /**
     * Resets the tree widget with the new model.
     */
    void resetTreeWidget();

    /**
     * @brief Resets the report view.
     *
     * @param analysis  The analysis with results.
     */
    void resetReportWidget(std::unique_ptr<core::RiskAnalysis> analysis);

    /**
     * Saves the model and sets the model file.
     *
     * @param destination  The destination file to become the main model file.
     */
    void saveToFile(std::string destination);

    /// Override to save the model before closing the application.
    void closeEvent(QCloseEvent *event) override;

    std::unique_ptr<Ui::MainWindow> ui;
    QAction *m_undoAction;
    QAction *m_redoAction;
    QUndoStack *m_undoStack;

    std::vector<std::string> m_inputFiles;  ///< The project model files.
    core::Settings m_settings; ///< The analysis settings.
    std::shared_ptr<mef::Model> m_model; ///< The analysis model.
    std::unique_ptr<model::Model> m_guiModel;  ///< The GUI Model wrapper.
    QRegularExpressionValidator m_percentValidator;  ///< Zoom percent input.
    QRegularExpressionValidator m_nameValidator; ///< The proper name schema.
    QComboBox *m_zoomBox; ///< The main zoom chooser/displayer widget.
    std::unordered_map<QTreeWidgetItem *, std::function<void()>>
        m_treeActions; ///< Actions on elements of the main tree widget.
    std::unique_ptr<core::RiskAnalysis> m_analysis; ///< Report container.
    std::unordered_map<QTreeWidgetItem *, std::function<void()>>
        m_reportActions; ///< Actions on elements of the report tree widget.
};

} // namespace gui
} // namespace scram

#endif // MAINWINDOW_H
