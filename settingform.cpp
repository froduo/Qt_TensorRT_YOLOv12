#include "settingform.h"
#include "ui_settingform.h"
#include <QtSerialPort/QSerialPortInfo>
#include <QFileDialog>
settingForm::settingForm(const AppConfig &config,QWidget *parent)
    : QDialog (parent)
    , ui(new Ui::settingForm)
{
    ui->setupUi(this);
    this->setWindowTitle("系统参数配置");

    // 1. 初始化波特率列表
    QStringList bauds = {"9600", "19200", "38400", "57600", "115200"};
    ui->cbBaud->addItems(bauds);

    // 2. 扫描串口并填入 ComboBox
    scanSerialPorts();

    // 3. 将当前配置填入界面
    ui->txtSN->setText(config.cameraSN);
    ui->valExp->setValue(config.exposure);
    ui->valGain->setValue(config.gain);
    ui->cbSerialPort->setCurrentText(config.serialPort);
    ui->cbBaud->setCurrentText(QString::number(config.baudRate));
    ui->txtIP->setText(config.netIp);
    ui->valNetPort->setValue(config.netPort);
    ui->txtEnginePath->setText(config.enginePath); // 新增路径显示
    ui->valScoreThresh->setValue(config.scoreThreshold);
    // 在 settingform.cpp 构造函数里添加
    connect(ui->btnOk, &QPushButton::clicked, this, &settingForm::accept); // 核心：调用 accept()
    connect(ui->btnCancel, &QPushButton::clicked, this, &settingForm::reject); // 调用 reject()
    connect(ui->btnBrowsePath, &QPushButton::clicked, this, &settingForm::on_btnBrowsePath_clicked);
}
void settingForm::scanSerialPorts() {
    ui->cbSerialPort->clear();
    const auto infos = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo &info : infos) {
        ui->cbSerialPort->addItem(info.portName());
    }
}
void settingForm::on_btnBrowsePath_clicked() {
    QString file = QFileDialog::getOpenFileName(this, "选择 TensorRT Engine",
                                                "./model", "Engine Files (*.engine);;All Files (*)");
    if (!file.isEmpty()) {
        ui->txtEnginePath->setText(file);
    }
}
AppConfig settingForm::getUpdatedConfig() const {
    AppConfig conf;
    conf.cameraSN = ui->txtSN->text();
    conf.exposure = ui->valExp->value();
    conf.gain = ui->valGain->value();
    conf.serialPort = ui->cbSerialPort->currentText();
    conf.baudRate = ui->cbBaud->currentText().toInt();
    conf.netIp = ui->txtIP->text();
    conf.netPort = ui->valNetPort->value();
    conf.enginePath = ui->txtEnginePath->text(); // 读取路径
    conf.scoreThreshold = ui->valScoreThresh->value();
    return conf;
}
settingForm::~settingForm()
{
    delete ui;
}
