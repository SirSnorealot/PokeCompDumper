#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "globals.h"
#include "hex.h"
#include "romconfig.h"
#include "image.h"
#include "map.h"
#include "poketext.h"
#include "pokescript.h"
#include "lz77.h"
#include "musicdumper.h"

#include <QApplication>
#include <QFileDialog>
#include <QMessageBox>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QImage>
#include <QPixmap>
#include <QCoreApplication>
#include <QPainter>
#include <QTreeWidgetItem>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <array>
#include <vector>
#include <climits>

// ---- Constructor / UI setup -------------------------------------------------------

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    connect(ui->loadBtn, &QPushButton::clicked, this, &MainWindow::onLoadClicked);
    connect(ui->exportBtn, &QPushButton::clicked, this, &MainWindow::onExportClicked);
    connect(ui->exportAllBtn, &QPushButton::clicked, this, &MainWindow::onExportAllClicked);
    connect(ui->exportWorldBtn, &QPushButton::clicked, this, &MainWindow::onExportWorldClicked);
    connect(ui->exportMusicBtn, &QPushButton::clicked, this, &MainWindow::onExportMusicClicked);
    connect(ui->mapsAndBanks, &QTreeWidget::currentItemChanged,
            this, &MainWindow::onMapSelectionChanged);
}

MainWindow::~MainWindow()
{
    delete ui;
}

// ---- Load ROM ---------------------------------------------------------------------

void MainWindow::onLoadClicked()
{
    QString path = QFileDialog::getOpenFileName(
        this, "Select ROM to open:", QString(),
        "GBA ROM (*.gba *.GBA);;All files (*.*)");

    if (path.isEmpty())
        return;

    g_loadedROM = path;
    handleOpenedROM();
}

void MainWindow::handleOpenedROM()
{
    // Read 4-byte game code at ROM offset 0xAC
    QFile f(g_loadedROM);
    if (!f.open(QIODevice::ReadOnly))
    {
        QMessageBox::warning(this, "Error", "Could not open ROM file.");
        g_loadedROM.clear();
        return;
    }
    f.seek(0xAC);
    QByteArray hdr = f.read(4);
    f.close();

    if (hdr.size() < 4)
    {
        QMessageBox::warning(this, "Error", "File is too small.");
        g_loadedROM.clear();
        return;
    }

    g_header = QString::fromLatin1(hdr);
    g_header2 = g_header.left(3);
    g_header3 = g_header.mid(3, 1);

    static const QStringList validCodes = {"BPR", "BPG", "BPE", "AXP", "AXV"};
    if (!validCodes.contains(g_header2))
    {
        ui->romNameLabel->clear();
        g_loadedROM.clear();
        QMessageBox::information(this, "PokeCompDumper", "Not one of the Pokemon games...");
        return;
    }
    if (g_header3 == "J")
    {
        ui->romNameLabel->clear();
        g_loadedROM.clear();
        QMessageBox::information(this, "PokeCompDumper",
                                 "I haven't added Jap support out of pure lazziness. "
                                 "I will though if it get's highly Demanded.");
        return;
    }

    loadRomConfig(getConfigFileLocation());
    QString romName = romConfigString(g_header, "name");
    ui->romNameLabel->setText(g_header + " - " + romName);
    ui->exportBtn->setEnabled(false);
    ui->exportAllBtn->setEnabled(false);
    ui->exportWorldBtn->setEnabled(false);
    ui->exportMusicBtn->setEnabled(false);
    ui->mapPreview->clear();
    ui->mapPreview->resize(1, 1);
    ui->blocksPreview->clear();
    ui->blocksPreview->resize(1, 1);
    ui->tilePreview->clear();
    ui->tilePreview2->clear();

    loadMapList();
    loadBanksAndMaps();
    ui->exportBtn->setEnabled(true);
    ui->exportAllBtn->setEnabled(true);
    ui->exportWorldBtn->setEnabled(true);
    ui->exportMusicBtn->setEnabled(true);
}

// ---- Load Map List ----------------------------------------------------------------

void MainWindow::loadMapList()
{
    g_mapNames.clear();

    int count = romConfigString(g_header, "mapLabelCount", "0").toInt();

    for (int i = 0; i < count; i++)
    {
        // Get the MapLabelData offset
        QString offsetStr = romConfigString(g_header, "mapLabelsOffset");
        bool ok = false;
        int offvar = offsetStr.toInt(&ok, 16);
        if (!ok || offvar == 0)
        {
            g_mapNames.append("");
            continue;
        }

        // Dereference pointer table
        if (g_header2 == "BPR" || g_header2 == "BPG")
            offvar = readROMPointer(g_loadedROM, offvar + (4 * i));
        else
            offvar = readROMPointer(g_loadedROM, offvar + (8 * i));

        // Read up to 22 bytes of the label name from ROM
        QFile rf(g_loadedROM);
        QString name;
        if (rf.open(QIODevice::ReadOnly))
        {
            rf.seek(offvar);
            QByteArray raw = rf.read(22);
            rf.close();
            name = decodePokeText(raw, false);
        }
        g_mapNames.append(name);
    }
}

// ---- Load Banks and Maps ----------------------------------------------------------

void MainWindow::loadBanksAndMaps()
{
    ui->mapsAndBanks->clear();

    g_point2MapBankPointers = romConfigString(g_header, "mapBankTableOffset").toInt(nullptr, 16);

    g_mapBankPointers = readInt32LE(g_loadedROM, g_point2MapBankPointers) - 0x8000000;

    int bankIdx = 0;
    while (true)
    {
        QString fourBytes = readHex(g_loadedROM, g_mapBankPointers + (bankIdx * 4), 4);
        if (fourBytes == "02000000" || fourBytes == "FFFFFFFF")
            break;

        auto *bankItem = new QTreeWidgetItem(ui->mapsAndBanks);
        bankItem->setText(0, QString::number(bankIdx));

        QString origBankPtrStr = romBankPointer(g_header, bankIdx);
        int numMaps = romMapsInBank(g_header, bankIdx);
        int bankPointer = readInt32LE(g_loadedROM, g_mapBankPointers + (bankIdx * 4)) - 0x8000000;

        bool bankConfigured = !origBankPtrStr.isEmpty() && origBankPtrStr.toInt(nullptr, 16) == bankPointer;
        int maxMaps = bankConfigured ? (numMaps + 1) : 300;

        int x = 0;
        while (x < maxMaps)
        {
            QString fourB = readHex(g_loadedROM, bankPointer + (x * 4), 4);
            if (fourB == "F7F7F7F7")
                break;

            int headerPtr = readInt32LE(g_loadedROM, bankPointer + (x * 4)) - 0x8000000;

            QString mapName;
            if (fourB == "77777777")
            {
                mapName = QString("%1 - Reserved").arg(x);
            }
            else
            {
                int maplabelvar = readHex(g_loadedROM, headerPtr + 20, 1).toInt(nullptr, 16);
                int nameIdx = 0;
                if (g_header2 == "BPR" || g_header2 == "BPG")
                    nameIdx = maplabelvar - 0x58;
                else
                    nameIdx = maplabelvar;

                QString label;
                if (nameIdx >= 0 && nameIdx < g_mapNames.size())
                    label = g_mapNames[nameIdx];
                mapName = QString("%1 - %2").arg(x).arg(label);
            }

            auto *mapItem = new QTreeWidgetItem(bankItem);
            mapItem->setText(0, mapName);

            x++;
        }

        bankIdx++;
    }
}

// ---- Map selection preview --------------------------------------------------------

static void showTilePreview(int imageOffset, int palOffset, bool isCompressed,
                            int width, int height, QLabel *label)
{
    if (!label)
        return;

    QByteArray image = isCompressed
                           ? decompressFromROM(imageOffset)
                           : QByteArray::fromHex(readHex(g_loadedROM, imageOffset, width * height / 2).toLatin1());
    if (image.isEmpty())
        return;

    QByteArray palBytes = QByteArray::fromHex(readHex(g_loadedROM, palOffset, 32).toLatin1());
    auto pals = loadPalette(palBytes);
    QImage preview = loadSprite(image, pals, width, height);
    label->setPixmap(QPixmap::fromImage(preview));
    label->update();
}

void MainWindow::onMapSelectionChanged(QTreeWidgetItem *current, QTreeWidgetItem * /*prev*/)
{
    if (!current || !current->parent() || g_loadedROM.isEmpty())
        return;

    int mapBank = ui->mapsAndBanks->indexOfTopLevelItem(current->parent());
    int mapNum = current->parent()->indexOfChild(current);

    int p2mb = romConfigString(g_header, "mapBankTableOffset").toInt(nullptr, 16);
    int mbp = readInt32LE(g_loadedROM, p2mb) - 0x8000000;
    int bankPointer = readInt32LE(g_loadedROM, mbp + (mapBank * 4)) - 0x8000000;
    int headerPtr = readInt32LE(g_loadedROM, bankPointer + (mapNum * 4)) - 0x8000000;

    int mapFooter = readROMPointer(g_loadedROM, headerPtr);
    bool isFR = (g_header2 == "BPR" || g_header2 == "BPG");

    // ---- Primary tileset --------------------------------------------------------
    int priTsPtr = readROMPointer(g_loadedROM, mapFooter + 16);
    bool priComp = readHex(g_loadedROM, priTsPtr, 1).toInt(nullptr, 16) == 1;
    int priImage = readROMPointer(g_loadedROM, priTsPtr + 4);
    int priPal = readROMPointer(g_loadedROM, priTsPtr + 8);
    int priBlocks = readROMPointer(g_loadedROM, priTsPtr + 12);

    int priTileBytes = isFR ? 20480 : 16384;
    int priTileCount = priTileBytes / 32; // 640 or 512
    int numPriPals = isFR ? 7 : 6;
    int priMetatileMax = isFR ? 640 : 512;

    // Use actual metatile count from roms.json if available, else fall back to max
    int numPriMetatiles = romTilesetTileCount(g_header,
                                              QString::number(priTsPtr, 16).toUpper());
    if (numPriMetatiles <= 0)
        numPriMetatiles = priMetatileMax;

    QByteArray primaryTiles = priComp
                                  ? decompressFromROM(priImage)
                                  : QByteArray::fromHex(readHex(g_loadedROM, priImage, priTileBytes).toLatin1());
    QByteArray priPalRaw = QByteArray::fromHex(readHex(g_loadedROM, priPal, numPriPals * 32).toLatin1());
    QByteArray primaryBlks = QByteArray::fromHex(readHex(g_loadedROM, priBlocks, priMetatileMax * 16).toLatin1());

    // ---- Secondary tileset ------------------------------------------------------
    int secTsPtr = readROMPointer(g_loadedROM, mapFooter + 20);
    bool secComp = readHex(g_loadedROM, secTsPtr, 1).toInt(nullptr, 16) == 1;
    int secImage = readROMPointer(g_loadedROM, secTsPtr + 4);
    int secPal = readROMPointer(g_loadedROM, secTsPtr + 8);
    int secBlocks = readROMPointer(g_loadedROM, secTsPtr + 12);

    int secTileBytes = isFR ? 12288 : 16384;
    int numSecPals = 13 - numPriPals; // 6 or 7
    int secMetatileMax = isFR ? 384 : 512;

    // Use actual metatile count from roms.json if available, else fall back to max
    int numSecMetatiles = romTilesetTileCount(g_header,
                                              QString::number(secTsPtr, 16).toUpper());
    if (numSecMetatiles <= 0)
        numSecMetatiles = secMetatileMax;

    QByteArray secondaryTiles = secComp
                                    ? decompressFromROM(secImage)
                                    : QByteArray::fromHex(readHex(g_loadedROM, secImage, secTileBytes).toLatin1());
    QByteArray secPalRaw = QByteArray::fromHex(readHex(g_loadedROM, secPal + numPriPals * 32, numSecPals * 32).toLatin1());
    QByteArray secondaryBlks = QByteArray::fromHex(readHex(g_loadedROM, secBlocks, secMetatileMax * 16).toLatin1());

    // ---- Combined palettes (13 total) -------------------------------------------
    std::vector<std::array<QColor, 16>> palettes(13);
    for (int i = 0; i < numPriPals; i++)
        palettes[i] = loadPalette(priPalRaw.mid(i * 32, 32));
    for (int i = 0; i < numSecPals; i++)
        palettes[numPriPals + i] = loadPalette(secPalRaw.mid(i * 32, 32));

    // ---- Combined metatile data -------------------------------------------------
    QByteArray allMetatiles = primaryBlks + secondaryBlks;

    int numMetatiles = numPriMetatiles + numSecMetatiles;

    // ---- Tiles tab --------------------------------------------------------------
    showTilePreview(priImage, priPal, priComp, 128, isFR ? 320 : 256, ui->tilePreview);
    showTilePreview(secImage, secPal + numPriPals * 32, secComp, 128, isFR ? 192 : 256, ui->tilePreview2);

    // ---- Blocks tab -------------------------------------------------------------
    QImage blockImg = renderBlockSheet(primaryTiles, secondaryTiles, allMetatiles,
                                       numMetatiles, palettes, priTileCount, 8);
    if (!blockImg.isNull())
    {
        ui->blocksPreview->setPixmap(QPixmap::fromImage(blockImg));
        ui->blocksPreview->resize(blockImg.size());
    }

    // ---- Map tab ----------------------------------------------------------------
    int mapDataPtr = readROMPointer(g_loadedROM, mapFooter + 12);
    int mapWidth = readInt32LE(g_loadedROM, mapFooter);
    int mapHeight = readInt32LE(g_loadedROM, mapFooter + 4);
    QByteArray mapData = QByteArray::fromHex(
        readHex(g_loadedROM, mapDataPtr, mapWidth * mapHeight * 2).toLatin1());

    QImage mapImg = renderMapImage(mapData, mapWidth, mapHeight,
                                   primaryTiles, secondaryTiles, allMetatiles,
                                   palettes, priTileCount);
    if (!mapImg.isNull())
    {
        ui->mapPreview->setPixmap(QPixmap::fromImage(mapImg));
        ui->mapPreview->resize(mapImg.size());
    }
}

// ---- Render a single map to a QImage --------------------------------------------

QImage MainWindow::renderMapToImage(int bank, int mapNum)
{
    const QString &rom = g_loadedROM;
    bool isFR = (g_header2 == "BPR" || g_header2 == "BPG");

    int p2mb = romConfigString(g_header, "mapBankTableOffset").toInt(nullptr, 16);
    int mbp = readInt32LE(rom, p2mb) - 0x8000000;
    int bankPointer = readInt32LE(rom, mbp + (bank * 4)) - 0x8000000;
    int headerPtr = readInt32LE(rom, bankPointer + (mapNum * 4)) - 0x8000000;
    int mapFooter = readROMPointer(rom, headerPtr);

    int priTsPtr = readROMPointer(rom, mapFooter + 16);
    bool priComp = readHex(rom, priTsPtr, 1).toInt(nullptr, 16) == 1;
    int priImage = readROMPointer(rom, priTsPtr + 4);
    int priPal = readROMPointer(rom, priTsPtr + 8);
    int priBlocks = readROMPointer(rom, priTsPtr + 12);

    int priTileBytes = isFR ? 20480 : 16384;
    int priTileCount = priTileBytes / 32;
    int numPriPals = isFR ? 7 : 6;
    int priMetatileMax = isFR ? 640 : 512;

    QByteArray primaryTiles = priComp
                                  ? decompressFromROM(priImage)
                                  : QByteArray::fromHex(readHex(rom, priImage, priTileBytes).toLatin1());
    QByteArray priPalRaw = QByteArray::fromHex(readHex(rom, priPal, numPriPals * 32).toLatin1());
    QByteArray primaryBlks = QByteArray::fromHex(readHex(rom, priBlocks, priMetatileMax * 16).toLatin1());

    int secTsPtr = readROMPointer(rom, mapFooter + 20);
    bool secComp = readHex(rom, secTsPtr, 1).toInt(nullptr, 16) == 1;
    int secImage = readROMPointer(rom, secTsPtr + 4);
    int secPal = readROMPointer(rom, secTsPtr + 8);
    int secBlocks = readROMPointer(rom, secTsPtr + 12);

    int secTileBytes = isFR ? 12288 : 16384;
    int numSecPals = 13 - numPriPals;
    int secMetatileMax = isFR ? 384 : 512;

    QByteArray secondaryTiles = secComp
                                    ? decompressFromROM(secImage)
                                    : QByteArray::fromHex(readHex(rom, secImage, secTileBytes).toLatin1());
    QByteArray secPalRaw = QByteArray::fromHex(readHex(rom, secPal + numPriPals * 32, numSecPals * 32).toLatin1());
    QByteArray secondaryBlks = QByteArray::fromHex(readHex(rom, secBlocks, secMetatileMax * 16).toLatin1());

    std::vector<std::array<QColor, 16>> palettes(13);
    for (int i = 0; i < numPriPals; i++)
        palettes[i] = loadPalette(priPalRaw.mid(i * 32, 32));
    for (int i = 0; i < numSecPals; i++)
        palettes[numPriPals + i] = loadPalette(secPalRaw.mid(i * 32, 32));

    int mapDataPtr = readROMPointer(rom, mapFooter + 12);
    int mapWidth = readInt32LE(rom, mapFooter);
    int mapHeight = readInt32LE(rom, mapFooter + 4);
    QByteArray mapData = QByteArray::fromHex(
        readHex(rom, mapDataPtr, mapWidth * mapHeight * 2).toLatin1());

    QByteArray allMetatiles = primaryBlks + secondaryBlks;
    return renderMapImage(mapData, mapWidth, mapHeight,
                          primaryTiles, secondaryTiles, allMetatiles,
                          palettes, priTileCount);
}

// ---- Resolve export name for a given bank/map -----------------------------------

QString MainWindow::resolveExportName(int bank, int mapNum)
{
    const QString &rom = g_loadedROM;
    int p2mb = romConfigString(g_header, "mapBankTableOffset").toInt(nullptr, 16);
    int mbp = readInt32LE(rom, p2mb) - 0x8000000;
    int bankPtr = readInt32LE(rom, mbp + (bank * 4)) - 0x8000000;
    int hdrPtr = readInt32LE(rom, bankPtr + (mapNum * 4)) - 0x8000000;

    QString hexByte = readHex(rom, hdrPtr + 20, 1);
    int nameIdx = 0;
    if (g_header2 == "BPR" || g_header2 == "BPG")
        nameIdx = hexByte.toInt(nullptr, 16) - 0x58;
    else
        nameIdx = hexByte.toInt(nullptr, 16);

    if (nameIdx >= 0 && nameIdx < g_mapNames.size())
        return g_mapNames[nameIdx].replace(" ", "_");
    return "Unknown";
}

// ---- DoOutput (main data extraction) ---------------------------------------------

void MainWindow::doOutput(QString &outputtext, QString &outputtextFooter,
                          QString &outputprimaryts, QString &outputsecondaryts)
{
    const QString &rom = g_loadedROM;
    const QString &en = g_exportName;
    const int mb = g_mapBank;
    const int mn = g_mapNumber;
    const int hp = g_headerPointer;
    const QString enBM = en + "_" + QString::number(mb) + "_" + QString::number(mn);
    const bool isFR = (g_header2 == "BPR" || g_header2 == "BPG");

    // ---- Read map header pointers (populate globals) ----
    g_mapFooter = readROMPointer(rom, hp);
    g_mapEvents = readROMPointer(rom, hp + 4);
    g_mapLevelScripts = readROMPointer(rom, hp + 8);
    g_mapConnectionHeader = readROMPointer(rom, hp + 12);

    // ---- Parse map header fields for map.json ----
    int musicVal = reverseHex(readHex(rom, hp + 16, 2)).toInt(nullptr, 16);
    if (isFR && musicVal >= 256)
        musicVal += 212;
    int regionSec = readHex(rom, hp + 20, 1).toInt(nullptr, 16);
    int caveVal = readHex(rom, hp + 21, 1).toInt(nullptr, 16);
    int weatherVal = readHex(rom, hp + 22, 1).toInt(nullptr, 16);
    int mapTypeVal = readHex(rom, hp + 23, 1).toInt(nullptr, 16);

    bool allowCycling, allowEscaping, allowRunning, showMapName;
    int battleVal;
    if (isFR)
    {

        int bikingAllowed = readHex(rom, hp + 24, 1).toInt(nullptr, 16);
        int flagsByte = readHex(rom, hp + 25, 1).toInt(nullptr, 16);
        allowCycling = bikingAllowed > 0;
        allowEscaping = (flagsByte >> 0) & 1;
        allowRunning = (flagsByte >> 1) & 1;
        showMapName = (flagsByte >> 2) != 0; // 6-bit showMapName field
        battleVal = readHex(rom, hp + 27, 1).toInt(nullptr, 16);
    }
    else
    {
        int flagsVal = readHex(rom, hp + 26, 1).toInt(nullptr, 16);
        allowCycling = (flagsVal >> 0) & 1;
        allowEscaping = (flagsVal >> 1) & 1;
        allowRunning = (flagsVal >> 2) & 1;
        showMapName = (flagsVal >> 3) & 1;
        battleVal = readHex(rom, hp + 27, 1).toInt(nullptr, 16);
    }

    // ---- Build map.json ----
    const QString layoutId = QString::number(static_cast<uint>(g_mapFooter), 16).toUpper();
    outputtext = "{\n";
    outputtext += "  \"id\": \"MAP_" + enBM + "\",\n";
    outputtext += "  \"name\": \"" + enBM + "\",\n";
    outputtext += "  \"layout\": \"LAYOUT_" + layoutId + "\",\n";
    outputtext += "  \"music\": \"" + enumName("music", musicVal) + "\",\n";
    outputtext += "  \"region_map_section\": \"" + enumName("mapsec", regionSec) + "\",\n";
    outputtext += "  \"requires_flash\": " + QString(caveVal ? "true" : "false") + ",\n";
    outputtext += "  \"weather\": \"" + enumName("weather", weatherVal) + "\",\n";
    outputtext += "  \"map_type\": \"" + enumName("map_type", mapTypeVal) + "\",\n";
    outputtext += "  \"allow_cycling\": " + QString(allowCycling ? "true" : "false") + ",\n";
    outputtext += "  \"allow_escaping\": " + QString(allowEscaping ? "true" : "false") + ",\n";
    outputtext += "  \"allow_running\": " + QString(allowRunning ? "true" : "false") + ",\n";
    outputtext += "  \"show_map_name\": " + QString(showMapName ? "true" : "false") + ",\n";
    outputtext += "  \"battle_scene\": \"" + enumName("battle_scene", battleVal) + "\",\n";

    // ---- Read connections from ROM ----
    {
        const QStringList dirNames = {"none", "south", "north", "west", "east", "dive", "emerge"};
        QString connectionsStr = "[]";
        if (g_mapConnectionHeader >= 0)
        {
            int connCount = readInt32LE(rom, g_mapConnectionHeader);
            int connArrRaw = readInt32LE(rom, g_mapConnectionHeader + 4);
            if (connCount > 0 && connCount <= 50 && connArrRaw != 0)
            {
                int connArr = connArrRaw - 0x8000000;
                QStringList parts;
                for (int ci = 0; ci < connCount; ci++)
                {
                    int cb = connArr + ci * 12;
                    int dir = readHex(rom, cb, 1).toInt(nullptr, 16);
                    int offset = readInt32LE(rom, cb + 4);
                    int cBank = readHex(rom, cb + 8, 1).toInt(nullptr, 16);
                    int cMap = readHex(rom, cb + 9, 1).toInt(nullptr, 16);
                    QString dirStr = (dir >= 1 && dir <= 6) ? dirNames[dir] : "none";
                    QString cName = resolveExportName(cBank, cMap);
                    QString mapId = "MAP_" + cName + "_" + QString::number(cBank) + "_" + QString::number(cMap);
                    parts += "    {\"direction\": \"" + dirStr + "\", \"offset\": " + QString::number(offset) + ", \"map\": \"" + mapId + "\"}";
                }
                connectionsStr = "[\n" + parts.join(",\n") + "\n  ]";
            }
        }
        outputtext += "  \"connections\": " + connectionsStr + ",\n";
    }

    // ---- Read object events from ROM ----
    // ObjectEventTemplate: 24 bytes each
    // +0 localId, +1 graphicsId, +4 x(s16), +6 y(s16), +8 elevation, +9 movementType,
    // +10 movRangeX(lo nibble)/movRangeY(hi nibble), +12 trainerType(u16), +14 trainerRange(u16),
    // +16 script ptr, +20 flagId(u16)
    {
        QString objectEventsStr = "[]";
        int objCount = readHex(rom, g_mapEvents + 0, 1).toInt(nullptr, 16);
        int objArrRaw = readInt32LE(rom, g_mapEvents + 4);
        if (objCount > 0 && objCount <= 255 && objArrRaw != 0)
        {
            int objArr = objArrRaw - 0x8000000;
            QStringList parts;
            for (int oi = 0; oi < objCount; oi++)
            {
                int ob        = objArr + oi * 24;
                int localId   = readHex(rom, ob + 0, 1).toInt(nullptr, 16);
                int graphicsId = readHex(rom, ob + 1, 1).toInt(nullptr, 16);
                int kind      = readHex(rom, ob + 2, 1).toInt(nullptr, 16); // 0=OBJ_KIND_NORMAL, 255=OBJ_KIND_CLONE
                int ox        = static_cast<int16_t>(reverseHex(readHex(rom, ob + 4, 2)).toInt(nullptr, 16));
                int oy        = static_cast<int16_t>(reverseHex(readHex(rom, ob + 6, 2)).toInt(nullptr, 16));

                if (kind == 255) {  // OBJ_KIND_CLONE
                    int targetLocalId  = readHex(rom, ob + 8, 1).toInt(nullptr, 16);
                    int targetMapNum   = reverseHex(readHex(rom, ob + 12, 2)).toInt(nullptr, 16);
                    int targetMapGroup = reverseHex(readHex(rom, ob + 14, 2)).toInt(nullptr, 16);
                    QString targetName = resolveExportName(targetMapGroup, targetMapNum);
                    QString targetMap  = "MAP_" + targetName + "_" + QString::number(targetMapGroup) + "_" + QString::number(targetMapNum);
                    parts += "    {\"type\": \"clone\""
                        + QString(", \"graphics_id\": \"") + enumName("graphics_id", graphicsId) + "\""
                        + ", \"x\": " + QString::number(ox)
                        + ", \"y\": " + QString::number(oy)
                        + ", \"target_local_id\": " + QString::number(targetLocalId)
                        + ", \"target_map\": \"" + targetMap + "\""
                        + "}";
                } else {
                    // Normal object event
                    int elevation    = readHex(rom, ob + 8, 1).toInt(nullptr, 16);
                    int movType      = readHex(rom, ob + 9, 1).toInt(nullptr, 16);
                    int movByte      = readHex(rom, ob + 10, 1).toInt(nullptr, 16);
                    int movRangeX    = movByte & 0xF;
                    int movRangeY    = (movByte >> 4) & 0xF;
                    int trainerType  = reverseHex(readHex(rom, ob + 12, 2)).toInt(nullptr, 16);
                    int trainerRange = reverseHex(readHex(rom, ob + 14, 2)).toInt(nullptr, 16);
                    int scriptRaw    = readInt32LE(rom, ob + 16);
                    int scriptOff    = scriptRaw - 0x8000000;
                    int flagId       = reverseHex(readHex(rom, ob + 20, 2)).toInt(nullptr, 16);
                    QString scriptStr = (scriptOff > 0 && scriptOff < 0x2000000)
                        ? enBM + "_Script_" + QString("%1").arg(scriptOff, 8, 16, QChar('0')).toUpper()
                        : "0x" + QString::number(static_cast<uint>(scriptRaw), 16).toUpper();
                    parts += "    {\"type\": \"object\""
                        + QString(", \"local_id\": ") + QString::number(localId)
                        + ", \"graphics_id\": \"" + enumName("graphics_id", graphicsId) + "\""
                        + ", \"x\": " + QString::number(ox)
                        + ", \"y\": " + QString::number(oy)
                        + ", \"elevation\": " + QString::number(elevation)
                        + ", \"movement_type\": \"" + enumName("movement_type", movType) + "\""
                        + ", \"movement_range_x\": " + QString::number(movRangeX)
                        + ", \"movement_range_y\": " + QString::number(movRangeY)
                        + ", \"trainer_type\": \"" + enumName("trainer_type", trainerType) + "\""
                        + ", \"trainer_sight_or_berry_tree_id\": \"" + QString::number(trainerRange) + "\""
                        + ", \"script\": \"" + scriptStr + "\""
                        + ", \"flag\": \"" + enumName("flag", flagId) + "\""
                        + "}";
                }
            }
            objectEventsStr = "[\n" + parts.join(",\n") + "\n  ]";
        }
        outputtext += "  \"object_events\": " + objectEventsStr + ",\n";
    }

    // ---- Read warp events from ROM ----
    {
        QString warpsStr = "[]";
        int warpCount = readHex(rom, g_mapEvents + 1, 1).toInt(nullptr, 16);
        int warpArrRaw = readInt32LE(rom, g_mapEvents + 8);
        if (warpCount > 0 && warpCount <= 255 && warpArrRaw != 0)
        {
            int warpArr = warpArrRaw - 0x8000000;
            QStringList parts;
            for (int wi = 0; wi < warpCount; wi++)
            {
                int wb = warpArr + wi * 8;
                int wx = static_cast<int16_t>(reverseHex(readHex(rom, wb, 2)).toInt(nullptr, 16));
                int wy = static_cast<int16_t>(reverseHex(readHex(rom, wb + 2, 2)).toInt(nullptr, 16));
                int welev = readHex(rom, wb + 4, 1).toInt(nullptr, 16);
                int warpId = readHex(rom, wb + 5, 1).toInt(nullptr, 16);
                int wMapNum = readHex(rom, wb + 6, 1).toInt(nullptr, 16);
                int wBank = readHex(rom, wb + 7, 1).toInt(nullptr, 16);
                QString destName = resolveExportName(wBank, wMapNum);
                QString destMap = "MAP_" + destName + "_" + QString::number(wBank) + "_" + QString::number(wMapNum);
                parts += "    {\"x\": " + QString::number(wx) + ", \"y\": " + QString::number(wy) + ", \"elevation\": " + QString::number(welev) + ", \"dest_warp_id\": " + QString::number(warpId) + ", \"dest_map\": \"" + destMap + "\"}";
            }
            warpsStr = "[\n" + parts.join(",\n") + "\n  ]";
        }
        outputtext += "  \"warp_events\": " + warpsStr + ",\n";
    }

    // ---- Read coord events from ROM ----
    // CoordEvent: 16 bytes each
    // +0 x(s16), +2 y(s16), +4 elevation, +6 trigger(u16), +8 index(u16), +12 script ptr
    {
        QString coordEventsStr = "[]";
        int coordCount = readHex(rom, g_mapEvents + 2, 1).toInt(nullptr, 16);
        int coordArrRaw = readInt32LE(rom, g_mapEvents + 12);
        if (coordCount > 0 && coordCount <= 255 && coordArrRaw != 0)
        {
            int coordArr = coordArrRaw - 0x8000000;
            QStringList parts;
            for (int ci = 0; ci < coordCount; ci++)
            {
                int cb = coordArr + ci * 16;
                int cx       = static_cast<int16_t>(reverseHex(readHex(rom, cb + 0, 2)).toInt(nullptr, 16));
                int cy       = static_cast<int16_t>(reverseHex(readHex(rom, cb + 2, 2)).toInt(nullptr, 16));
                int celev    = readHex(rom, cb + 4, 1).toInt(nullptr, 16);
                int trigger  = reverseHex(readHex(rom, cb + 6, 2)).toInt(nullptr, 16);
                int index    = reverseHex(readHex(rom, cb + 8, 2)).toInt(nullptr, 16);
                int scriptRaw = readInt32LE(rom, cb + 12);
                int scriptOff = scriptRaw - 0x8000000;
                QString scriptStr = (scriptOff > 0 && scriptOff < 0x2000000)
                    ? enBM + "_Script_" + QString("%1").arg(scriptOff, 8, 16, QChar('0')).toUpper()
                    : "0x" + QString::number(static_cast<uint>(scriptRaw), 16).toUpper();
                parts += "    {\"x\": " + QString::number(cx)
                    + ", \"y\": " + QString::number(cy)
                    + ", \"elevation\": " + QString::number(celev)
                    + ", \"trigger\": " + QString::number(trigger)
                    + ", \"index\": " + QString::number(index)
                    + ", \"script\": \"" + scriptStr + "\""
                    + "}";
            }
            coordEventsStr = "[\n" + parts.join(",\n") + "\n  ]";
        }
        outputtext += "  \"coord_events\": " + coordEventsStr + ",\n";
    }

    // ---- Read bg events from ROM ----
    // BgEvent: 12 bytes each
    // +0 x(u16), +2 y(u16), +4 elevation(u8), +5 kind(u8), +6 padding(u16), +8 union(u32)
    // kind: 0=FACING_ANY 1=NORTH 2=SOUTH 3=EAST 4=WEST  7=HIDDEN_ITEM  8=SECRET_BASE
    {
        static const char *const kFacingDir[] = {
            "BG_EVENT_PLAYER_FACING_ANY",
            "BG_EVENT_PLAYER_FACING_NORTH",
            "BG_EVENT_PLAYER_FACING_SOUTH",
            "BG_EVENT_PLAYER_FACING_EAST",
            "BG_EVENT_PLAYER_FACING_WEST",
        };

        QString bgEventsStr = "[]";
        int bgCount = readHex(rom, g_mapEvents + 3, 1).toInt(nullptr, 16);
        int bgArrRaw = readInt32LE(rom, g_mapEvents + 16);
        if (bgCount > 0 && bgCount <= 255 && bgArrRaw != 0)
        {
            int bgArr = bgArrRaw - 0x8000000;
            QStringList parts;
            for (int bi = 0; bi < bgCount; bi++)
            {
                int bb      = bgArr + bi * 12;
                int bx      = reverseHex(readHex(rom, bb + 0, 2)).toInt(nullptr, 16);
                int by      = reverseHex(readHex(rom, bb + 2, 2)).toInt(nullptr, 16);
                int belev   = readHex(rom, bb + 4, 1).toInt(nullptr, 16);
                int bkind   = readHex(rom, bb + 5, 1).toInt(nullptr, 16);
                uint32_t unionVal = static_cast<uint32_t>(readInt32LE(rom, bb + 8));

                QString common = "    {\"x\": " + QString::number(bx)
                    + ", \"y\": " + QString::number(by)
                    + ", \"elevation\": " + QString::number(belev);

                if (bkind == 7) {
                    // BG_EVENT_HIDDEN_ITEM bit layout differs between games.
                    // FR  (global.fieldmap.h GET_HIDDEN_ITEM_*):
                    //   bits  0-15: item id  (16 bits)
                    //   bits 16-23: flag id  ( 8 bits)
                    //   bits 24-30: quantity ( 7 bits)
                    //   bit     31: underfoot( 1 bit )
                    // Emerald (struct hiddenItem):
                    //   bits  0-15: item id       (u16)
                    //   bits 16-31: hiddenItemId  (u16, flag)
                    //   quantity/underfoot not stored → 0/false
                    int itemId, flagId, quantity;
                    bool underfoot;
                    if (isFR) {
                        itemId   = static_cast<int>( unionVal        & 0xFFFF);
                        flagId   = static_cast<int>((unionVal >> 16) & 0x00FF);
                        quantity = static_cast<int>((unionVal >> 24) & 0x7F);
                        underfoot = (unionVal >> 31) & 0x1;
                    } else {
                        itemId   = static_cast<int>( unionVal        & 0xFFFF);
                        flagId   = static_cast<int>((unionVal >> 16) & 0xFFFF);
                        quantity = 0;
                        underfoot = false;
                    }
                    parts += common
                        + ", \"type\": \"hidden_item\""
                        + ", \"item\": \"" + enumName("item", itemId) + "\""
                        + ", \"flag\": \"" + enumName("flag", flagId) + "\""
                        + ", \"quantity\": " + QString::number(quantity)
                        + ", \"underfoot\": " + (underfoot ? "true" : "false")
                        + "}";
                } else if (bkind == 8) {
                    // BG_EVENT_SECRET_BASE: union = secret base id
                    parts += common
                        + ", \"type\": \"secret_base\""
                        + ", \"secret_base_id\": \"0x" + QString::number(unionVal, 16).toUpper() + "\""
                        + "}";
                } else {
                    // Sign event: kind 0-4 = player facing direction
                    const char *facing = (bkind >= 0 && bkind <= 4)
                        ? kFacingDir[bkind] : "BG_EVENT_PLAYER_FACING_ANY";
                    int scriptOff = static_cast<int>(unionVal) - 0x8000000;
                    QString scriptStr = (scriptOff > 0 && scriptOff < 0x2000000)
                        ? enBM + "_Script_" + QString("%1").arg(scriptOff, 8, 16, QChar('0')).toUpper()
                        : "0x" + QString::number(unionVal, 16).toUpper();
                    parts += common
                        + ", \"type\": \"sign\""
                        + ", \"player_facing_dir\": \"" + facing + "\""
                        + ", \"script\": \"" + scriptStr + "\""
                        + "}";
                }
            }
            bgEventsStr = "[\n" + parts.join(",\n") + "\n  ]";
        }
        outputtext += "  \"bg_events\": " + bgEventsStr + "\n";
    }
    outputtext += "}\n";

    // ---- Read map footer / layout data (populate globals used by binary export) ----
    g_mapWidth = readInt32LE(rom, g_mapFooter);
    g_mapHeight = readInt32LE(rom, g_mapFooter + 4);

    g_borderPointer = readROMPointer(rom, g_mapFooter + 8);
    g_mapDataPointer = readROMPointer(rom, g_mapFooter + 12);

    g_primaryTilesetPointer = readROMPointer(rom, g_mapFooter + 16);
    g_secondaryTilesetPointer = readROMPointer(rom, g_mapFooter + 20);

    g_borderHeight = 2;
    g_borderWidth = 2;
    g_borderData = readHex(rom, g_borderPointer, (g_borderHeight * g_borderWidth) * 2);
    g_mapPermData = readHex(rom, g_mapDataPointer, (g_mapHeight * g_mapWidth) * 2);

    // the layout entry is added to layouts.json in onExportClicked.
    outputtextFooter = QString();

    // ---- Primary Tileset – C struct for src/data/tilesets/headers.h ----
    g_primaryTilesetCompression = readHex(rom, g_primaryTilesetPointer, 1).toInt(nullptr, 16);
    g_primaryImagePointer = readROMPointer(rom, g_primaryTilesetPointer + 4);
    g_primaryPalPointer = readROMPointer(rom, g_primaryTilesetPointer + 8);
    g_primaryBlockSetPointer = readROMPointer(rom, g_primaryTilesetPointer + 12);
    if (isFR)
        g_primaryBehaviourPointer = readROMPointer(rom, g_primaryTilesetPointer + 20);
    else
        g_primaryBehaviourPointer = readROMPointer(rom, g_primaryTilesetPointer + 16);

    const QString priTsId = QString::number(static_cast<uint>(g_primaryTilesetPointer), 16).toUpper();

    outputprimaryts = "const struct Tileset gTileset_" + priTsId + " =\n{\n";
    outputprimaryts += "    .isCompressed = TRUE,\n";
    outputprimaryts += "    .isSecondary = FALSE,\n";
    outputprimaryts += "    .tiles = gTilesetTiles_" + priTsId + ",\n";
    outputprimaryts += "    .palettes = gTilesetPalettes_" + priTsId + ",\n";
    outputprimaryts += "    .metatiles = gMetatiles_" + priTsId + ",\n";
    outputprimaryts += "    .metatileAttributes = gMetatileAttributes_" + priTsId + ",\n";
    outputprimaryts += "    .callback = NULL,\n};\n\n";

    // Primary tile data
    if (isFR)
        g_primaryPals = readHex(rom, g_primaryPalPointer, 7 * (16 * 2));
    else
        g_primaryPals = readHex(rom, g_primaryPalPointer, 6 * (16 * 2));

    if (isFR)
    {
        if (g_primaryTilesetCompression == 1)
            g_primaryTilesImg = mapTilesCompressedToHexStringFRPrim(g_primaryImagePointer, g_primaryPalPointer);
        else
            g_primaryTilesImg = readHex(rom, g_primaryImagePointer, 20480);
    }
    else
    {
        if (g_primaryTilesetCompression == 1)
            g_primaryTilesImg = mapTilesCompressedToHexString(g_primaryImagePointer, g_primaryPalPointer);
        else
            g_primaryTilesImg = readHex(rom, g_primaryImagePointer, 16384);
    }

    if (isFR)
    {
        g_primaryBlocks = readHex(rom, g_primaryBlockSetPointer, 16 * 640);
        g_primaryBehaviors = readHex(rom, g_primaryBehaviourPointer, 4 * 640);
    }
    else
    {
        g_primaryBlocks = readHex(rom, g_primaryBlockSetPointer, 16 * 512);
        g_primaryBehaviors = readHex(rom, g_primaryBehaviourPointer, 2 * 512);
    }

    // ---- Secondary Tileset – C struct for src/data/tilesets/headers.h ----
    g_secondaryTilesetCompression = readHex(rom, g_secondaryTilesetPointer, 1).toInt(nullptr, 16);
    g_secondaryImagePointer = readROMPointer(rom, g_secondaryTilesetPointer + 4);
    g_secondaryPalPointer = readROMPointer(rom, g_secondaryTilesetPointer + 8);
    g_secondaryBlockSetPointer = readROMPointer(rom, g_secondaryTilesetPointer + 12);
    if (isFR)
        g_secondaryBehaviourPointer = readROMPointer(rom, g_secondaryTilesetPointer + 20);
    else
        g_secondaryBehaviourPointer = readROMPointer(rom, g_secondaryTilesetPointer + 16);

    const QString secTsId = QString::number(static_cast<uint>(g_secondaryTilesetPointer), 16).toUpper();

    outputsecondaryts = "const struct Tileset gTileset_" + secTsId + " =\n{\n";
    outputsecondaryts += "    .isCompressed = TRUE,\n";
    outputsecondaryts += "    .isSecondary = TRUE,\n";
    outputsecondaryts += "    .tiles = gTilesetTiles_" + secTsId + ",\n";
    outputsecondaryts += "    .palettes = gTilesetPalettes_" + secTsId + ",\n";
    outputsecondaryts += "    .metatiles = gMetatiles_" + secTsId + ",\n";
    outputsecondaryts += "    .metatileAttributes = gMetatileAttributes_" + secTsId + ",\n";
    outputsecondaryts += "    .callback = NULL,\n};\n\n";

    g_secondaryPals = readHex(rom, g_secondaryPalPointer, 13 * (16 * 2));

    if (isFR)
    {
        if (g_secondaryTilesetCompression == 1)
            g_secondaryTilesImg = mapTilesCompressedToHexStringFRSec(g_secondaryImagePointer, g_secondaryPalPointer);
        else
            g_secondaryTilesImg = readHex(rom, g_secondaryImagePointer, 12288);
    }
    else
    {
        if (g_secondaryTilesetCompression == 1)
            g_secondaryTilesImg = mapTilesCompressedToHexString(g_secondaryImagePointer, g_secondaryPalPointer);
        else
            g_secondaryTilesImg = readHex(rom, g_secondaryImagePointer, 16384);
    }

    // Secondary block/behavior sizes
    int secMetatileMax = isFR ? 384 : 512;
    int numTiles = romTilesetTileCount(g_header,
                                       QString::number(g_secondaryTilesetPointer, 16).toUpper());
    if (numTiles <= 0)
        numTiles = secMetatileMax;

    if (isFR)
    {
        g_secondaryBlocks    = readHex(rom, g_secondaryBlockSetPointer,  secMetatileMax * 16);
        g_secondaryBehaviors = readHex(rom, g_secondaryBehaviourPointer, secMetatileMax * 4);
    }
    else
    {
        g_secondaryBlocks    = readHex(rom, g_secondaryBlockSetPointer,  numTiles * 16);
        g_secondaryBehaviors = readHex(rom, g_secondaryBehaviourPointer, numTiles * 2);
    }
}

// ---- Export -----------------------------------------------------------------------

void MainWindow::onExportClicked()
{
    QTreeWidgetItem *item = ui->mapsAndBanks->currentItem();
    if (!item || !item->parent())
    {
        QMessageBox::information(this, "PokeCompDumper", "Please select a map from the tree.");
        return;
    }

    QString folder = QFileDialog::getExistingDirectory(
        this, "Select folder to export to:", QString(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (folder.isEmpty())
        return;

    setWindowTitle("Please wait...");
    setCursor(Qt::WaitCursor);
    setEnabled(false);

    int mapBank = ui->mapsAndBanks->indexOfTopLevelItem(item->parent());
    int mapNumber = item->parent()->indexOfChild(item);
    exportMap(folder, mapBank, mapNumber);

    setWindowTitle("PokeCompDumper");
    unsetCursor();
    setEnabled(true);
    activateWindow();
}

void MainWindow::exportMap(const QString &folder, int bankIdx, int mapIdx)
{
    g_mapBank = bankIdx;
    g_mapNumber = mapIdx;

    g_point2MapBankPointers = romConfigString(g_header, "mapBankTableOffset").toInt(nullptr, 16);
    g_mapBankPointers = readInt32LE(g_loadedROM, g_point2MapBankPointers) - 0x8000000;

    int bankPointer = readInt32LE(g_loadedROM, g_mapBankPointers + (g_mapBank * 4)) - 0x8000000;
    g_headerPointer = readInt32LE(g_loadedROM, bankPointer + (g_mapNumber * 4)) - 0x8000000;

    // Determine export name
    QString hexByte = readHex(g_loadedROM, g_headerPointer + 20, 1);
    int nameIdx = 0;
    if (g_header2 == "BPR" || g_header2 == "BPG")
        nameIdx = hexByte.toInt(nullptr, 16) - 0x58;
    else
        nameIdx = hexByte.toInt(nullptr, 16);

    if (nameIdx >= 0 && nameIdx < g_mapNames.size())
        g_exportName = g_mapNames[nameIdx].replace(" ", "_");
    else
        g_exportName = "Unknown";

    // Reset output strings
    QString outputtext, outputtextFooter, outputprimaryts, outputsecondaryts;
    doOutput(outputtext, outputtextFooter, outputprimaryts, outputsecondaryts);

    const QString enBM = g_exportName + "_" + QString::number(g_mapBank) + "_" + QString::number(g_mapNumber);
    const QString priTsId = QString::number(static_cast<uint>(g_primaryTilesetPointer), 16).toUpper();
    const QString secTsId = QString::number(static_cast<uint>(g_secondaryTilesetPointer), 16).toUpper();
    const QString layoutId = QString::number(static_cast<uint>(g_mapFooter), 16).toUpper();

    // ---- Write layout binary files ----
    QString layoutDir = folder + "/data/layouts/" + layoutId + "/";
    const bool layoutExists = QDir(layoutDir).exists();
    if (!layoutExists)
    {
        QDir().mkpath(layoutDir);
        writeHex(layoutDir + "border.bin", 0, g_borderData);
        writeHex(layoutDir + "map.bin", 0, g_mapPermData);
    }

    // ---- Update data/layouts/layouts.json ----
    {
        QString layoutsJsonPath = folder + "/data/layouts/layouts.json";
        QJsonObject layoutsJson;
        QJsonArray layoutsArray;
        QFile layoutsJsonFile(layoutsJsonPath);
        if (layoutsJsonFile.open(QIODevice::ReadOnly))
        {
            QJsonDocument doc = QJsonDocument::fromJson(layoutsJsonFile.readAll());
            layoutsJsonFile.close();
            if (!doc.isNull())
            {
                layoutsJson = doc.object();
                layoutsArray = layoutsJson["layouts"].toArray();
            }
        }
        if (!layoutsJson.contains("layouts_table_label"))
            layoutsJson["layouts_table_label"] = QString("gMapLayouts");

        const QString layoutEntryId = "LAYOUT_" + layoutId;
        bool layoutAlreadyInJson = false;
        for (int li = 0; li < layoutsArray.size(); li++)
        {
            if (layoutsArray[li].toObject()["id"].toString() == layoutEntryId)
            {
                layoutAlreadyInJson = true;
                break;
            }
        }
        if (!layoutAlreadyInJson)
        {
            QJsonObject layoutEntry;
            layoutEntry["id"] = layoutEntryId;
            layoutEntry["name"] = layoutId + "_Layout";
            layoutEntry["width"] = g_mapWidth;
            layoutEntry["height"] = g_mapHeight;
            layoutEntry["primary_tileset"] = "gTileset_" + priTsId;
            layoutEntry["secondary_tileset"] = "gTileset_" + secTsId;
            layoutEntry["border_filepath"] = "data/layouts/" + layoutId + "/border.bin";
            layoutEntry["blockdata_filepath"] = "data/layouts/" + layoutId + "/map.bin";
            layoutsArray.append(layoutEntry);
        }
        layoutsJson["layouts"] = layoutsArray;

        QJsonDocument outDoc(layoutsJson);
        if (layoutsJsonFile.open(QIODevice::WriteOnly))
        {
            layoutsJsonFile.write(outDoc.toJson(QJsonDocument::Indented));
            layoutsJsonFile.close();
        }
    }

    // ---- Write data/maps/<name>/map.json ----
    QString mapDir = folder + "/data/maps/" + enBM + "/";
    QDir().mkpath(mapDir);
    {
        QFile mf(mapDir + "map.json");
        (void)mf.open(QIODevice::WriteOnly | QIODevice::Text);
        mf.write(outputtext.toUtf8());
    }

    // ---- Write data/maps/<name>/scripts.inc ----
    {
        QList<int> eventScriptOffsets;
        const QString &rom = g_loadedROM;

        // Object events: events block +0 = count, +4 = array ptr, 24 bytes each, script at +16
        {
            int count  = readHex(rom, g_mapEvents + 0, 1).toInt(nullptr, 16);
            int arrRaw = readInt32LE(rom, g_mapEvents + 4);
            if (count > 0 && count <= 255 && arrRaw != 0)
            {
                int arr = arrRaw - 0x8000000;
                for (int i = 0; i < count; ++i)
                {
                    int scriptRaw = readInt32LE(rom, arr + i * 24 + 16);
                    if (scriptRaw >= 0x08000000 && scriptRaw < 0x0C000000)
                        eventScriptOffsets.append(scriptRaw - 0x8000000);
                }
            }
        }
        // Coord events: events block +2 = count, +12 = array ptr, 16 bytes each, script at +12
        {
            int count  = readHex(rom, g_mapEvents + 2, 1).toInt(nullptr, 16);
            int arrRaw = readInt32LE(rom, g_mapEvents + 12);
            if (count > 0 && count <= 255 && arrRaw != 0)
            {
                int arr = arrRaw - 0x8000000;
                for (int i = 0; i < count; ++i)
                {
                    int scriptRaw = readInt32LE(rom, arr + i * 16 + 12);
                    if (scriptRaw >= 0x08000000 && scriptRaw < 0x0C000000)
                        eventScriptOffsets.append(scriptRaw - 0x8000000);
                }
            }
        }
        // BG events: events block +3 = count, +16 = array ptr, 12 bytes each, script at +8
        {
            int count  = readHex(rom, g_mapEvents + 3, 1).toInt(nullptr, 16);
            int arrRaw = readInt32LE(rom, g_mapEvents + 16);
            if (count > 0 && count <= 255 && arrRaw != 0)
            {
                int arr = arrRaw - 0x8000000;
                for (int i = 0; i < count; ++i)
                {
                    int scriptRaw = readInt32LE(rom, arr + i * 12 + 8);
                    if (scriptRaw >= 0x08000000 && scriptRaw < 0x0C000000)
                        eventScriptOffsets.append(scriptRaw - 0x8000000);
                }
            }
        }

        bool isFR = (g_header2 == "BPR" || g_header2 == "BPG");
        QString scriptsInc = decompileMapScripts(g_loadedROM, enBM, g_mapLevelScripts, eventScriptOffsets, isFR);
        if (!scriptsInc.isEmpty())
        {
            QFile sf(mapDir + "scripts.inc");
            (void)sf.open(QIODevice::WriteOnly | QIODevice::Text);
            sf.write(scriptsInc.toUtf8());
        }
    }

    // ---- Write temp binary files ----
    QString base = folder + "/Bank" + QString::number(g_mapBank) + "_Map" + QString::number(g_mapNumber);

    writeHex(base + "_PrimaryPal.bin", 0, g_primaryPals);
    writeHex(base + "_SecondaryPal.bin", 0, g_secondaryPals);
    writeHex(base + "_PrimaryTiles.bin", 0, g_primaryTilesImg);
    writeHex(base + "_SecondaryTiles.bin", 0, g_secondaryTilesImg);
    writeHex(base + "_PrimaryBlocks.bin", 0, g_primaryBlocks);
    writeHex(base + "_SecondaryBlocks.bin", 0, g_secondaryBlocks);
    writeHex(base + "_PrimaryBehaviors.bin", 0, g_primaryBehaviors);
    writeHex(base + "_SecondaryBehaviors.bin", 0, g_secondaryBehaviors);

    // ---- FR/LG tile conversion ----
    if (g_header2 == "BPR" || g_header2 == "BPG")
    {
        // Tile conversion
        QString tiles1 = mapTilesCompressedToHexStringFRPrim2(0, 0, base + "_PrimaryTiles.bin");
        QString tiles2 = mapTilesCompressedToHexStringFRSec2(0, 0, base + "_SecondaryTiles.bin");
        QString tilecomb = tiles1 + tiles2;

        tiles1 = tilecomb.left(16384 * 2);
        tiles2 = tilecomb.mid(16384 * 2, 16384 * 2);

        tiles1 = byteArrayToHexString(lz77CompressBytes(hexStringToByteArray(tiles1)));
        tiles2 = byteArrayToHexString(lz77CompressBytes(hexStringToByteArray(tiles2)));

        QFile::remove(base + "_PrimaryTiles.bin");
        QFile::remove(base + "_SecondaryTiles.bin");
        writeHex(base + "_PrimaryTiles.bin", 0, tiles1);
        writeHex(base + "_SecondaryTiles.bin", 0, tiles2);

        // Block conversion
        QFileInfo info1(base + "_PrimaryBlocks.bin");
        QFileInfo info2(base + "_SecondaryBlocks.bin");
        QString blocks1 = readHex(base + "_PrimaryBlocks.bin", 0, (int)info1.size());
        QString blocks2 = readHex(base + "_SecondaryBlocks.bin", 0, (int)info2.size());
        QString blockscomb = blocks1 + blocks2;

        blocks1 = blockscomb.left((512 * 2) * 16);
        blocks2 = blockscomb.mid((512 * 2) * 16);

        QFile::remove(base + "_PrimaryBlocks.bin");
        QFile::remove(base + "_SecondaryBlocks.bin");
        writeHex(base + "_PrimaryBlocks.bin", 0, blocks1);
        writeHex(base + "_SecondaryBlocks.bin", 0, blocks2);

        // Palette conversion
        QString pals1 = readHex(base + "_PrimaryPal.bin", 0, (16 * 2) * 6);
        QString pals2 = readHex(base + "_PrimaryPal.bin", 0, (16 * 2) * 7) + readHex(base + "_SecondaryPal.bin", (16 * 2) * 7, (16 * 2) * 6);

        QFile::remove(base + "_PrimaryPal.bin");
        QFile::remove(base + "_SecondaryPal.bin");
        writeHex(base + "_PrimaryPal.bin", 0, pals1);
        writeHex(base + "_SecondaryPal.bin", 0, pals2);

        QFileInfo info3(base + "_PrimaryBehaviors.bin");
        QFileInfo info4(base + "_SecondaryBehaviors.bin");
        QString behaviors1, behaviors2;

        {
            QByteArray raw = QByteArray::fromHex(
                readHex(base + "_PrimaryBehaviors.bin", 0, (int)info3.size()).toLatin1());
            for (int i = 0; i + 3 < raw.size(); i += 4)
            {
                uint8_t b0 = static_cast<uint8_t>(raw[i]);
                uint8_t b1 = static_cast<uint8_t>(raw[i + 1]);
                uint8_t b3 = static_cast<uint8_t>(raw[i + 3]);
                uint8_t hi = uint8_t(((b1 >> 1) & 0x3)        // terrain  → bits 8-9
                                   | ((b3 & 0x3) << 2)         // encounter→ bits 10-11
                                   | (((b3 >> 5) & 0x3) << 4));// layer    → bits 12-13
                behaviors1 += QString("%1").arg(b0, 2, 16, QChar('0')).toUpper();
                behaviors1 += QString("%1").arg(hi, 2, 16, QChar('0')).toUpper();
            }
        }
        {
            QByteArray raw = QByteArray::fromHex(
                readHex(base + "_SecondaryBehaviors.bin", 0, (int)info4.size()).toLatin1());
            for (int i = 0; i + 3 < raw.size(); i += 4)
            {
                uint8_t b0 = static_cast<uint8_t>(raw[i]);
                uint8_t b1 = static_cast<uint8_t>(raw[i + 1]);
                uint8_t b3 = static_cast<uint8_t>(raw[i + 3]);
                uint8_t hi = uint8_t(((b1 >> 1) & 0x3)
                                   | ((b3 & 0x3) << 2)
                                   | (((b3 >> 5) & 0x3) << 4));
                behaviors2 += QString("%1").arg(b0, 2, 16, QChar('0')).toUpper();
                behaviors2 += QString("%1").arg(hi, 2, 16, QChar('0')).toUpper();
            }
        }

        QString behaviorscomb = behaviors1 + behaviors2;
        QString conbehaviors1 = behaviorscomb.left((512 * 2) * 2);
        QString conbehaviors2 = behaviorscomb.mid((512 * 2) * 2);

        QFile::remove(base + "_PrimaryBehaviors.bin");
        QFile::remove(base + "_SecondaryBehaviors.bin");
        writeHex(base + "_PrimaryBehaviors.bin", 0, conbehaviors1);
        writeHex(base + "_SecondaryBehaviors.bin", 0, conbehaviors2);
    }

    // ---- Create tileset directories (skip if already exported) ----
    QString primaryDir = folder + "/data/tilesets/primary/" + priTsId + "/";
    QString secondaryDir = folder + "/data/tilesets/secondary/" + secTsId + "/";
    const bool priTsExists = QDir(primaryDir).exists();
    const bool secTsExists = QDir(secondaryDir).exists();

    if (!priTsExists)
        QDir().mkpath(primaryDir + "palettes/");
    if (!secTsExists)
        QDir().mkpath(secondaryDir + "palettes/");

    // ---- Write src/data/tilesets/headers.h (C struct format) ----
    QDir().mkpath(folder + "/src/data/tilesets/");
    {
        QString hdrOut;
        if (!priTsExists)
            hdrOut += outputprimaryts;
        if (!secTsExists)
            hdrOut += outputsecondaryts;
        if (!hdrOut.isEmpty())
        {
            QFile hf(folder + "/src/data/tilesets/headers.h");
            (void)hf.open(QIODevice::Append | QIODevice::WriteOnly | QIODevice::Text);
            hf.write(hdrOut.toUtf8());
        }
    }

    // ---- src/data/tilesets/metatiles.h (C INCBIN_U16 format) ----
    {
        QString outputMetatiles;
        if (!priTsExists)
        {
            outputMetatiles += "const u16 gMetatiles_" + priTsId + "[] = INCBIN_U16(\"data/tilesets/primary/" + priTsId + "/metatiles.bin\");\n";
            outputMetatiles += "const u16 gMetatileAttributes_" + priTsId + "[] = INCBIN_U16(\"data/tilesets/primary/" + priTsId + "/metatile_attributes.bin\");\n\n";
        }
        if (!secTsExists)
        {
            outputMetatiles += "const u16 gMetatiles_" + secTsId + "[] = INCBIN_U16(\"data/tilesets/secondary/" + secTsId + "/metatiles.bin\");\n";
            outputMetatiles += "const u16 gMetatileAttributes_" + secTsId + "[] = INCBIN_U16(\"data/tilesets/secondary/" + secTsId + "/metatile_attributes.bin\");\n\n";
        }
        if (!outputMetatiles.isEmpty())
        {
            QFile mf(folder + "/src/data/tilesets/metatiles.h");
            (void)mf.open(QIODevice::Append | QIODevice::WriteOnly | QIODevice::Text);
            mf.write(outputMetatiles.toUtf8());
        }
    }

    // ---- src/data/tilesets/graphics.h (C INCGFX_U32 / INCGFX_U16 format) ----
    {
        // Read actual tile counts from the LZ77 headers in the tile bin files
        auto tileCountFromBin = [](const QString &binPath) -> int
        {
            QFile f(binPath);
            if (!f.open(QIODevice::ReadOnly))
                return 512;
            QByteArray hdr = f.read(4);
            f.close();
            if (hdr.size() < 4)
                return 512;
            const auto *h = reinterpret_cast<const uint8_t *>(hdr.constData());
            int uncompBytes = h[1] | (h[2] << 8) | (h[3] << 16);
            return (uncompBytes > 0) ? (uncompBytes / 32) : 512;
        };
        int priNumTiles = tileCountFromBin(base + "_PrimaryTiles.bin");
        int secNumTiles = tileCountFromBin(base + "_SecondaryTiles.bin");

        bool useFastSmol = (romConfigString(g_header, "fastSmol", "true") == "true");
        QString lzExt = useFastSmol ? ".4bpp.fastSmol" : ".4bpp.lz";

        auto palLines = [&](const QString &side, const QString &tsId)
        {
            QString out;
            for (int p = 0; p <= 15; p++)
            {
                out += QString("\tINCGFX_U16(\"data/tilesets/%1/%2/palettes/%3.pal\", \".gbapal\"),\n")
                           .arg(side)
                           .arg(tsId)
                           .arg(p, 2, 10, QChar('0'));
            }
            return out;
        };

        QString outputGraphics;
        if (!priTsExists)
        {
            outputGraphics += "const u16 gTilesetPalettes_" + priTsId + "[][16] =\n{\n";
            outputGraphics += palLines("primary", priTsId);
            outputGraphics += "};\n\n";
            outputGraphics += "const u32 gTilesetTiles_" + priTsId + "[] = INCGFX_U32(\"data/tilesets/primary/" + priTsId + "/tiles.png\", \"" + lzExt + "\", \"-num_tiles " + QString::number(priNumTiles) + " -Wnum_tiles\");\n\n";
        }
        if (!secTsExists)
        {
            outputGraphics += "const u16 gTilesetPalettes_" + secTsId + "[][16] =\n{\n";
            outputGraphics += palLines("secondary", secTsId);
            outputGraphics += "};\n\n";
            outputGraphics += "const u32 gTilesetTiles_" + secTsId + "[] = INCGFX_U32(\"data/tilesets/secondary/" + secTsId + "/tiles.png\", \"" + lzExt + "\", \"-num_tiles " + QString::number(secNumTiles) + " -Wnum_tiles\");\n\n";
        }
        if (!outputGraphics.isEmpty())
        {
            QFile gf(folder + "/src/data/tilesets/graphics.h");
            (void)gf.open(QIODevice::Append | QIODevice::WriteOnly | QIODevice::Text);
            gf.write(outputGraphics.toUtf8());
        }
    }

    // ---- Move block/behavior files ----
    auto moveFile = [&](const QString &src, const QString &dst)
    {
        QFile::remove(dst);
        QFile::rename(src, dst);
    };
    if (!priTsExists)
    {
        moveFile(base + "_PrimaryBlocks.bin", primaryDir + "metatiles.bin");
        moveFile(base + "_PrimaryBehaviors.bin", primaryDir + "metatile_attributes.bin");
    }
    else
    {
        QFile::remove(base + "_PrimaryBlocks.bin");
        QFile::remove(base + "_PrimaryBehaviors.bin");
    }
    if (!secTsExists)
    {
        moveFile(base + "_SecondaryBlocks.bin", secondaryDir + "metatiles.bin");
        moveFile(base + "_SecondaryBehaviors.bin", secondaryDir + "metatile_attributes.bin");
    }
    else
    {
        QFile::remove(base + "_SecondaryBlocks.bin");
        QFile::remove(base + "_SecondaryBehaviors.bin");
    }

    // ---- Read palettes from bin files and write .pal text ----
    std::array<std::array<QColor, 16>, 16> pals;
    for (auto &p : pals)
        p.fill(Qt::black);

    {
        QFile pf(base + "_PrimaryPal.bin");
        if (pf.open(QIODevice::ReadOnly))
        {
            pals[0] = loadPaletteFromROM(pf);
            pals[1] = loadPaletteFromROM(pf);
            pals[2] = loadPaletteFromROM(pf);
            pals[3] = loadPaletteFromROM(pf);
            pals[4] = loadPaletteFromROM(pf);
            pals[5] = loadPaletteFromROM(pf);
            pf.close();
        }
    }
    {
        QFile sf(base + "_SecondaryPal.bin");
        if (sf.open(QIODevice::ReadOnly))
        {
            sf.seek(192); // skip first 192 bytes (6×32)
            pals[6] = loadPaletteFromROM(sf);
            pals[7] = loadPaletteFromROM(sf);
            pals[8] = loadPaletteFromROM(sf);
            pals[9] = loadPaletteFromROM(sf);
            pals[10] = loadPaletteFromROM(sf);
            pals[11] = loadPaletteFromROM(sf);
            pals[12] = loadPaletteFromROM(sf);
            sf.close();
        }
    }
    pals[13] = pals[0];
    pals[14] = pals[0];
    pals[15] = pals[0];

    QFile::remove(base + "_PrimaryPal.bin");
    QFile::remove(base + "_SecondaryPal.bin");

    if (!priTsExists)
    {
        for (int p = 0; p <= 15; p++)
        {
            QString pname = QString("%1").arg(p, 2, 10, QChar('0'));
            QFile pf(primaryDir + "palettes/" + pname + ".pal");
            (void)pf.open(QIODevice::WriteOnly);
            pf.write(colorToPalText(pals[p]).toUtf8());
        }
    }
    if (!secTsExists)
    {
        for (int p = 0; p <= 15; p++)
        {
            QString pname = QString("%1").arg(p, 2, 10, QChar('0'));
            QFile pf(secondaryDir + "palettes/" + pname + ".pal");
            (void)pf.open(QIODevice::WriteOnly);
            pf.write(colorToPalText(pals[p]).toUtf8());
        }
    }

    // ---- Save tile PNG images (128px wide = 16 tiles per row, matching pokeemerald) ----
    auto saveTilePng = [&](const QString &binFile, const QString &pngPath)
    {
        QFile f(binFile);
        if (!f.open(QIODevice::ReadOnly))
            return;
        QByteArray compressed = f.readAll();
        f.close();
        if (compressed.size() < 4)
            return;

        // Read uncompressed size from LZ77 header (bytes 1-3, little-endian)
        const auto *hdr = reinterpret_cast<const uint8_t *>(compressed.constData());
        int uncompBytes = hdr[1] | (hdr[2] << 8) | (hdr[3] << 16);
        if (uncompBytes <= 0)
            return;

        // Each 4bpp tile = 32 bytes; image is 16 tiles (128px) wide
        int numTiles = uncompBytes / 32;
        int numRows = (numTiles + 15) / 16; // round up
        int imgW = 128;
        int imgH = numRows * 8;
        if (imgH <= 0)
            return;

        QByteArray pixData = loadTilesToBitsForImage(binFile, pals[0], true, imgH, imgW, imgW * imgH / 2);

        QImage img(imgW, imgH, QImage::Format_Indexed8);
        QVector<QRgb> colorTable(16);
        for (int c = 0; c < 16; c++)
            colorTable[c] = pals[0][c].rgba();
        img.setColorTable(colorTable);

        for (int row = 0; row < imgH; row++)
            for (int col = 0; col < imgW; col++)
                img.setPixel(col, row, static_cast<unsigned char>(pixData[row * imgW + col]));

        img.save(pngPath, "PNG");
    };

    if (!priTsExists)
        saveTilePng(base + "_PrimaryTiles.bin", primaryDir + "tiles.png");
    if (!secTsExists)
        saveTilePng(base + "_SecondaryTiles.bin", secondaryDir + "tiles.png");

    // Clean up temp tile files
    QFile::remove(base + "_PrimaryTiles.bin");
    QFile::remove(base + "_SecondaryTiles.bin");
}

// ---- Export All -------------------------------------------------------------------

void MainWindow::onExportAllClicked()
{
    if (g_loadedROM.isEmpty())
        return;

    QString folder = QFileDialog::getExistingDirectory(
        this, "Select folder to export all maps to:", QString(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (folder.isEmpty())
        return;

    setWindowTitle("Please wait...");
    setCursor(Qt::WaitCursor);
    setEnabled(false);
    QCoreApplication::processEvents();

    g_point2MapBankPointers = romConfigString(g_header, "mapBankTableOffset").toInt(nullptr, 16);
    g_mapBankPointers = readInt32LE(g_loadedROM, g_point2MapBankPointers) - 0x8000000;

    int bankIdx = 0;
    while (true)
    {
        QString fourBytes = readHex(g_loadedROM, g_mapBankPointers + (bankIdx * 4), 4);
        if (fourBytes == "02000000" || fourBytes == "FFFFFFFF")
            break;

        QString origBankPtrStr = romBankPointer(g_header, bankIdx);
        int numMaps = romMapsInBank(g_header, bankIdx);
        int bankPointer = readInt32LE(g_loadedROM, g_mapBankPointers + (bankIdx * 4)) - 0x8000000;

        bool bankConfigured = !origBankPtrStr.isEmpty() && origBankPtrStr.toInt(nullptr, 16) == bankPointer;
        int maxMaps = bankConfigured ? (numMaps + 1) : 300;

        int x = 0;
        while (x < maxMaps)
        {
            QString fourB = readHex(g_loadedROM, bankPointer + (x * 4), 4);
            if (fourB == "F7F7F7F7")
                break;

            if (fourB != "77777777")
                exportMap(folder, bankIdx, x);

            x++;
        }

        bankIdx++;
    }

    setWindowTitle("PokeCompDumper");
    unsetCursor();
    setEnabled(true);
    activateWindow();
}

// ---- Export World Image ----------------------------------------------------------

void MainWindow::onExportWorldClicked()
{
    QTreeWidgetItem *item = ui->mapsAndBanks->currentItem();
    if (!item || !item->parent())
    {
        QMessageBox::information(this, "PokeCompDumper", "Please select a map from the tree.");
        return;
    }

    QString savePath = QFileDialog::getSaveFileName(
        this, "Save world image:", QString(), "PNG Image (*.png)");
    if (savePath.isEmpty())
        return;
    if (!savePath.endsWith(".png", Qt::CaseInsensitive))
        savePath += ".png";

    setWindowTitle("Please wait...");
    setCursor(Qt::WaitCursor);
    setEnabled(false);
    QCoreApplication::processEvents();

    int startBank = ui->mapsAndBanks->indexOfTopLevelItem(item->parent());
    int startMap = item->parent()->indexOfChild(item);

    const QString &rom = g_loadedROM;
    int p2mb = romConfigString(g_header, "mapBankTableOffset").toInt(nullptr, 16);
    int mbp = readInt32LE(rom, p2mb) - 0x8000000;

    // BFS: track world tile position and tile dimensions for each visited map
    QMap<QPair<int, int>, QPoint> visited; // {bank,mapNum} -> world tile origin
    QMap<QPair<int, int>, QSize> mapSizes; // {bank,mapNum} -> tile dimensions
    QList<QPair<int, int>> bfsQueue;

    visited[{startBank, startMap}] = QPoint(0, 0);
    bfsQueue.append({startBank, startMap});

    while (!bfsQueue.isEmpty())
    {
        QPair<int, int> key = bfsQueue.takeFirst();
        int bank = key.first;
        int mapNum = key.second;
        QPoint origin = visited[key];

        int bankPointer = readInt32LE(rom, mbp + (bank * 4)) - 0x8000000;
        int headerPtr = readInt32LE(rom, bankPointer + (mapNum * 4)) - 0x8000000;
        int mapFooter = readROMPointer(rom, headerPtr);

        int mapW = readInt32LE(rom, mapFooter);
        int mapH = readInt32LE(rom, mapFooter + 4);
        mapSizes[key] = QSize(mapW, mapH);

        // Connection header is a ROM pointer at hp+12; raw 0 means NULL
        int connHdrRaw = readInt32LE(rom, headerPtr + 12);
        if (connHdrRaw == 0)
            continue;
        int connHdr = connHdrRaw - 0x8000000;
        if (connHdr < 0)
            continue;

        int connCount = readInt32LE(rom, connHdr);
        int connArrRaw = readInt32LE(rom, connHdr + 4);
        if (connCount <= 0 || connCount > 50 || connArrRaw == 0)
            continue;
        int connArr = connArrRaw - 0x8000000;
        if (connArr < 0)
            continue;

        for (int ci = 0; ci < connCount; ci++)
        {
            int cb = connArr + ci * 12;
            int dir = readHex(rom, cb, 1).toInt(nullptr, 16);
            int offset = readInt32LE(rom, cb + 4);
            int cBank = readHex(rom, cb + 8, 1).toInt(nullptr, 16);
            int cMap = readHex(rom, cb + 9, 1).toInt(nullptr, 16);

            QPair<int, int> nKey = {cBank, cMap};
            if (visited.contains(nKey))
                continue;

            // Need neighbor size for NORTH/WEST to compute its origin
            int bankPointer2 = readInt32LE(rom, mbp + (cBank * 4)) - 0x8000000;
            int headerPtr2 = readInt32LE(rom, bankPointer2 + (cMap * 4)) - 0x8000000;
            int mapFooter2 = readROMPointer(rom, headerPtr2);
            int nW = readInt32LE(rom, mapFooter2);
            int nH = readInt32LE(rom, mapFooter2 + 4);

            QPoint nOrigin;
            switch (dir)
            {
            case 1:
                nOrigin = {origin.x() + offset, origin.y() + mapH};
                break; // SOUTH
            case 2:
                nOrigin = {origin.x() + offset, origin.y() - nH};
                break; // NORTH
            case 3:
                nOrigin = {origin.x() - nW, origin.y() + offset};
                break; // WEST
            case 4:
                nOrigin = {origin.x() + mapW, origin.y() + offset};
                break; // EAST
            default:
                continue;
            }

            visited[nKey] = nOrigin;
            bfsQueue.append(nKey);
        }
    }

    // Compute bounding box in tiles
    int minTX = INT_MAX, minTY = INT_MAX, maxTX = INT_MIN, maxTY = INT_MIN;
    for (auto it = visited.constBegin(); it != visited.constEnd(); ++it)
    {
        QPoint o = it.value();
        QSize s = mapSizes.value(it.key(), QSize(1, 1));
        minTX = qMin(minTX, o.x());
        minTY = qMin(minTY, o.y());
        maxTX = qMax(maxTX, o.x() + s.width());
        maxTY = qMax(maxTY, o.y() + s.height());
    }

    const int TILE_PX = 16;
    QImage canvas((maxTX - minTX) * TILE_PX, (maxTY - minTY) * TILE_PX, QImage::Format_RGB32);
    canvas.fill(Qt::black);
    QPainter painter(&canvas);

    for (auto it = visited.constBegin(); it != visited.constEnd(); ++it)
    {
        QPoint o = it.value();
        QImage mapImg = renderMapToImage(it.key().first, it.key().second);
        if (!mapImg.isNull())
            painter.drawImage((o.x() - minTX) * TILE_PX, (o.y() - minTY) * TILE_PX, mapImg);
    }
    painter.end();
    canvas.save(savePath, "PNG");

    setWindowTitle("PokeCompDumper");
    unsetCursor();
    setEnabled(true);
    activateWindow();
}

// ---- Export Music ----------------------------------------------------------------

void MainWindow::onExportMusicClicked()
{
    if (g_loadedROM.isEmpty())
        return;

    QString folder = QFileDialog::getExistingDirectory(
        this, "Select folder to export music to:", QString(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (folder.isEmpty())
        return;

    setWindowTitle("Please wait...");
    setCursor(Qt::WaitCursor);
    setEnabled(false);
    QCoreApplication::processEvents();

    MusicDumpResult result = exportMusicData(g_loadedROM, g_header, folder);

    setWindowTitle("PokeCompDumper");
    unsetCursor();
    setEnabled(true);
    activateWindow();

    if (!result.ok)
    {
        QMessageBox::warning(this, "PokeCompDumper", result.error);
        return;
    }

    QMessageBox::information(
        this,
        "PokeCompDumper",
        QString("Music export complete.\n\nSongs: %1\nSong headers: %2\nTracks: %3\nVoicegroups: %4\nRelated files: %5")
            .arg(result.songs)
            .arg(result.songHeaders)
            .arg(result.tracks)
            .arg(result.voicegroups)
            .arg(result.relatedBlobs));
}
