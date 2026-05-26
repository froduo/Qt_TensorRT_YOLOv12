#include "settingform.h"
#include "ui_settingform.h"
#include <QtSerialPort/QSerialPortInfo>
#include <QFileDialog>
#include <QDebug>
#include <QCoreApplication>
#include <QMessageBox>
#include <QFile>

settingForm::settingForm(const AppConfig &config, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::settingForm)
{
    ui->setupUi(this);
    this->setWindowTitle("系统参数配置");

    QStringList bauds = {"9600", "19200", "38400", "57600", "115200"};
    ui->cbBaud->addItems(bauds);

    scanSerialPorts();

    ui->txtWindowTitle->setText(config.windowTitle);

    // 图像保存参数
    ui->chkSaveNG->setChecked(config.saveNG);
    ui->chkSaveOK->setChecked(config.saveOK);
    ui->txtSavePath->setText(config.savePath);
    ui->cbSaveFormat->setCurrentText(config.saveFormat);
    ui->valJpegQuality->setValue(config.jpegQuality);

    // 相机1参数
    ui->txtSN->setText(config.cameraSN);
    ui->valExp->setValue(config.exposure);
    ui->valGain->setValue(config.gain);

    // 相机2参数
    ui->txtSN2->setText(config.cameraSN2);
    ui->valExp2->setValue(config.exposure2);
    ui->valGain2->setValue(config.gain2);

    ui->cbSerialPort->setCurrentText(config.serialPort);
    ui->cbBaud->setCurrentText(QString::number(config.baudRate));
    ui->txtIP->setText(config.netIp);
    ui->valNetPort->setValue(config.netPort);

    // 相机1推理
    ui->chkEnableInference->setChecked(config.enableInference);
    ui->txtEnginePath->setText(config.enginePath);
    ui->txtClassesPath->setText(config.classesPath);
    ui->valScoreThresh->setValue(config.scoreThreshold);

    // 相机2推理
    ui->chkEnableInference2->setChecked(config.enableInference2);
    ui->txtEnginePath2->setText(config.enginePath2);
    ui->txtClassesPath2->setText(config.classesPath2);
    ui->valScoreThresh2->setValue(config.scoreThreshold2);

    ensureDefaultClassesFile();

    connect(ui->btnOk, &QPushButton::clicked, this, &settingForm::accept);
    connect(ui->btnCancel, &QPushButton::clicked, this, &settingForm::reject);
    connect(ui->btnBrowsePath, &QPushButton::clicked, this, &settingForm::handleBrowsePath);
    connect(ui->btnBrowseClasses, &QPushButton::clicked, this, &settingForm::handleBrowseClasses);
    connect(ui->btnBrowseSavePath, &QPushButton::clicked, this, &settingForm::handleBrowseSavePath);
    connect(ui->btnBrowsePath2, &QPushButton::clicked, this, &settingForm::handleBrowsePath2);
    connect(ui->btnBrowseClasses2, &QPushButton::clicked, this, &settingForm::handleBrowseClasses2);

    // JPEG QUALITY 仅在 jpg 格式时可编辑
    auto updateJpegQualityEnabled = [=]() {
        ui->valJpegQuality->setEnabled(ui->cbSaveFormat->currentText() == "jpg");
    };
    connect(ui->cbSaveFormat, &QComboBox::currentTextChanged, this, updateJpegQualityEnabled);
    updateJpegQualityEnabled(); // 初始化状态
}

settingForm::~settingForm()
{
    delete ui;
}

AppConfig settingForm::getUpdatedConfig() const
{
    AppConfig cfg;
    cfg.windowTitle  = ui->txtWindowTitle->text().trimmed();

    // 图像保存参数
    cfg.saveNG       = ui->chkSaveNG->isChecked();
    cfg.saveOK       = ui->chkSaveOK->isChecked();
    cfg.savePath     = ui->txtSavePath->text().trimmed();
    cfg.saveFormat   = ui->cbSaveFormat->currentText();
    cfg.jpegQuality  = ui->valJpegQuality->value();

    // 相机1参数
    cfg.cameraSN     = ui->txtSN->text().trimmed();
    cfg.exposure     = ui->valExp->value();
    cfg.gain         = ui->valGain->value();

    // 相机2参数
    cfg.cameraSN2    = ui->txtSN2->text().trimmed();
    cfg.exposure2    = ui->valExp2->value();
    cfg.gain2        = ui->valGain2->value();

    cfg.serialPort   = ui->cbSerialPort->currentText();
    cfg.baudRate     = ui->cbBaud->currentText().toInt();
    cfg.netIp        = ui->txtIP->text().trimmed();
    cfg.netPort      = ui->valNetPort->value();

    // 相机1推理
    cfg.enableInference = ui->chkEnableInference->isChecked();
    cfg.enginePath   = ui->txtEnginePath->text().trimmed();
    cfg.classesPath  = ui->txtClassesPath->text().trimmed();
    cfg.scoreThreshold = ui->valScoreThresh->value();

    // 相机2推理
    cfg.enableInference2 = ui->chkEnableInference2->isChecked();
    cfg.enginePath2  = ui->txtEnginePath2->text().trimmed();
    cfg.classesPath2 = ui->txtClassesPath2->text().trimmed();
    cfg.scoreThreshold2 = ui->valScoreThresh2->value();

    return cfg;
}

void settingForm::scanSerialPorts()
{
    ui->cbSerialPort->clear();
    const auto ports = QSerialPortInfo::availablePorts();
    for(const QSerialPortInfo &info : ports) {
        ui->cbSerialPort->addItem(info.portName());
    }
    if(ui->cbSerialPort->count() == 0) {
        ui->cbSerialPort->addItem("No Port");
    }
}

void settingForm::handleBrowsePath()
{
    QString initDir = ui->txtEnginePath->text().trimmed();
    if (initDir.isEmpty()) {
        initDir = QCoreApplication::applicationDirPath() + "/model";
    } else {
        QFileInfo fileInfo(initDir);
        if (fileInfo.isFile()) {
            initDir = fileInfo.absolutePath();
        }
    }

    QString file = QFileDialog::getOpenFileName(
        this,
        "选择推理引擎文件",
        initDir,
        "Engine 文件 (*.engine);;所有文件 (*)",
        nullptr,
        QFileDialog::DontResolveSymlinks
    );

    if (!file.isEmpty()) {
        ui->txtEnginePath->setText(file);
    }
}

void settingForm::handleBrowseSavePath()
{
    QString initDir = ui->txtSavePath->text().trimmed();
    if (initDir.isEmpty()) {
        initDir = QCoreApplication::applicationDirPath();
    }

    QString dir = QFileDialog::getExistingDirectory(
        this,
        "选择图像保存根目录",
        initDir,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks
    );

    if (!dir.isEmpty()) {
        ui->txtSavePath->setText(dir);
    }
}

void settingForm::handleBrowseClasses()
{
    QString initDir = ui->txtClassesPath->text().trimmed();
    if (initDir.isEmpty()) {
        initDir = QCoreApplication::applicationDirPath() + "/model";
    } else {
        QFileInfo fileInfo(initDir);
        if (fileInfo.isFile()) {
            initDir = fileInfo.absolutePath();
        }
    }

    QString file = QFileDialog::getOpenFileName(
        this,
        "选择类别配置文件",
        initDir,
        "YAML 文件 (*.yaml *.yml);;所有文件 (*)",
        nullptr,
        QFileDialog::DontResolveSymlinks
    );

    if (!file.isEmpty()) {
        ui->txtClassesPath->setText(file);
    }
}

// ⭐ 相机2浏览引擎文件
void settingForm::handleBrowsePath2()
{
    QString initDir = ui->txtEnginePath2->text().trimmed();
    if (initDir.isEmpty()) {
        initDir = QCoreApplication::applicationDirPath() + "/model";
    } else {
        QFileInfo fileInfo(initDir);
        if (fileInfo.isFile()) {
            initDir = fileInfo.absolutePath();
        }
    }

    QString file = QFileDialog::getOpenFileName(
        this,
        "选择相机2推理引擎文件",
        initDir,
        "Engine 文件 (*.engine);;所有文件 (*)",
        nullptr,
        QFileDialog::DontResolveSymlinks
    );

    if (!file.isEmpty()) {
        ui->txtEnginePath2->setText(file);
    }
}

// ⭐ 相机2浏览类别文件
void settingForm::handleBrowseClasses2()
{
    QString initDir = ui->txtClassesPath2->text().trimmed();
    if (initDir.isEmpty()) {
        initDir = QCoreApplication::applicationDirPath() + "/model";
    } else {
        QFileInfo fileInfo(initDir);
        if (fileInfo.isFile()) {
            initDir = fileInfo.absolutePath();
        }
    }

    QString file = QFileDialog::getOpenFileName(
        this,
        "选择相机2类别配置文件",
        initDir,
        "YAML 文件 (*.yaml *.yml);;所有文件 (*)",
        nullptr,
        QFileDialog::DontResolveSymlinks
    );

    if (!file.isEmpty()) {
        ui->txtClassesPath2->setText(file);
    }
}

void settingForm::ensureDefaultClassesFile()
{
    QString defaultPath = QCoreApplication::applicationDirPath() + "/model/coco.yaml";
    QFile file(defaultPath);
    if (!file.exists()) {
        QDir().mkpath(QCoreApplication::applicationDirPath() + "/model");
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&file);
            out << "names:\n";
            out << "  0: person\n";
            out << "  1: bicycle\n";
            out << "  2: car\n";
            out << "  3: motorcycle\n";
            out << "  4: airplane\n";
            out << "  5: bus\n";
            out << "  6: train\n";
            out << "  7: truck\n";
            out << "  8: boat\n";
            out << "  9: traffic light\n";
            out << "  10: fire hydrant\n";
            out << "  11: stop sign\n";
            out << "  12: parking meter\n";
            out << "  13: bench\n";
            out << "  14: bird\n";
            out << "  15: cat\n";
            out << "  16: dog\n";
            out << "  17: horse\n";
            out << "  18: sheep\n";
            out << "  19: cow\n";
            out << "  20: elephant\n";
            out << "  21: bear\n";
            out << "  22: zebra\n";
            out << "  23: giraffe\n";
            out << "  24: backpack\n";
            out << "  25: umbrella\n";
            out << "  26: handbag\n";
            out << "  27: tie\n";
            out << "  28: suitcase\n";
            out << "  29: frisbee\n";
            out << "  30: skis\n";
            out << "  31: snowboard\n";
            out << "  32: sports ball\n";
            out << "  33: kite\n";
            out << "  34: baseball bat\n";
            out << "  35: baseball glove\n";
            out << "  36: skateboard\n";
            out << "  37: surfboard\n";
            out << "  38: tennis racket\n";
            out << "  39: bottle\n";
            out << "  40: wine glass\n";
            out << "  41: cup\n";
            out << "  42: fork\n";
            out << "  43: knife\n";
            out << "  44: spoon\n";
            out << "  45: bowl\n";
            out << "  46: banana\n";
            out << "  47: apple\n";
            out << "  48: sandwich\n";
            out << "  49: orange\n";
            out << "  50: broccoli\n";
            out << "  51: carrot\n";
            out << "  52: hot dog\n";
            out << "  53: pizza\n";
            out << "  54: donut\n";
            out << "  55: cake\n";
            out << "  56: chair\n";
            out << "  57: couch\n";
            out << "  58: potted plant\n";
            out << "  59: bed\n";
            out << "  60: dining table\n";
            out << "  61: toilet\n";
            out << "  62: tv\n";
            out << "  63: laptop\n";
            out << "  64: mouse\n";
            out << "  65: remote\n";
            out << "  66: keyboard\n";
            out << "  67: cell phone\n";
            out << "  68: microwave\n";
            out << "  69: oven\n";
            out << "  70: toaster\n";
            out << "  71: sink\n";
            out << "  72: refrigerator\n";
            out << "  73: book\n";
            out << "  74: clock\n";
            out << "  75: vase\n";
            out << "  76: scissors\n";
            out << "  77: teddy bear\n";
            out << "  78: hair drier\n";
            out << "  79: toothbrush\n";
            out << "nc: 80\n";
            file.close();
        }
    }
}
