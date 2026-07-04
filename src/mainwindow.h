#pragma once
#include <QMainWindow>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class QTreeWidgetItem;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

private slots:
    void onLoadClicked();
    void onExportClicked();
    void onExportAllClicked();
    void onExportWorldClicked();
    void onExportMusicClicked();
    void onExportTrainerSpritesClicked();
    void onMapSelectionChanged(QTreeWidgetItem* current, QTreeWidgetItem* previous);

private:
    Ui::MainWindow* ui;

    // Internal helpers
    void handleOpenedROM();
    void loadMapList();
    void loadBanksAndMaps();
    void exportMap(const QString& folder, int bankIdx, int mapIdx);
    QString resolveExportName(int bank, int mapNum);
    QImage renderMapToImage(int bank, int mapNum);
    void doOutput(QString& outputtext, QString& outputtextFooter,
                  QString& outputprimaryts, QString& outputsecondaryts);
};
