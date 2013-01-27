#include "main_window.h"

#include <QtGlobal>
#include <QStringList>
#include <QIODevice>
#include <QPalette>
#include <QColor>

#include <cmath>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <linux/input.h>
#include <linux/ioctl.h>


#define MMAIO				0xA1
#define MMA7660FC_IOCTL_S_POLL_DELAY	_IOW(MMAIO, 0x03, int)



void CopterMotor::invoke_open()
{
  invoke(0);
}

void CopterMotor::invoke_close()
{
  invoke(0);
}

void CopterMotor::invoke(int _power)
{
  QString s;
  m_ctrlFile.open(QIODevice::WriteOnly|QIODevice::Truncate|QIODevice::Unbuffered|QIODevice::Text);
  s.sprintf("%d\n", _power);
  m_ctrlFile.write(s.toLatin1());
  m_ctrlFile.close();
}

CopterMotor::CopterMotor(Settings::sptr settings, const QString& _ctrlPath, QLCDNumber* _lcd)
  :m_settings(settings),
    m_lcd(_lcd),
    m_ctrlFile(_ctrlPath),
    m_factor(1.0)
{
  invoke_open();
}

CopterMotor::~CopterMotor()
{
  invoke_close();
}

void CopterMotor::factor(double _factor)
{
  m_factor = qMax(qMin(_factor, 1.0), 0.0);
}

void CopterMotor::setPower(unsigned _power)
{
  int pwr =  floor(m_factor * _power + 0.5);
  static const auto s_power_min = m_settings->getPowerMin();
  static const auto s_power_max = m_settings->getPowerMax();
  QPalette palette = m_lcd->palette();
  QColor bg = palette.color(QPalette::Disabled, m_lcd->backgroundRole());
  double pwrSat = 1.0d - static_cast<double>(_power-s_power_min)/(s_power_max-s_power_min);
  double ftrSat = m_factor;
  bg.setBlue( bg.blue() *pwrSat);
  bg.setGreen(bg.green()*pwrSat + 0xff*(1.0-pwrSat)*ftrSat);
  bg.setRed(  bg.red()  *pwrSat + 0xff*(1.0-pwrSat)*(1-ftrSat));
  palette.setColor(QPalette::Normal, m_lcd->backgroundRole(), bg);
  palette.setColor(QPalette::Active, m_lcd->backgroundRole(), bg);
  palette.setColor(QPalette::Inactive, m_lcd->backgroundRole(), bg);
  m_lcd->setPalette(palette);
  m_lcd->display(pwr);

  invoke(pwr);
}




CopterAxis::CopterAxis(const QSharedPointer<CopterMotor>& _motor1,
                       const QSharedPointer<CopterMotor>& _motor2)
  :m_motor1(_motor1),
    m_motor2(_motor2)
{
}

double CopterAxis::tilt() const
{
  return m_motor1->factor() - m_motor2->factor();
}

void CopterAxis::tilt(double _tilt) const
{
  if (_tilt == 0)
  {
    m_motor1->factor(1.0);
    m_motor2->factor(1.0);
  }
  else if (_tilt < 0)
  {
    m_motor1->factor(1.0+_tilt);
    m_motor2->factor(1.0);
  }
  else if (_tilt > 0)
  {
    m_motor1->factor(1.0);
    m_motor2->factor(1.0-_tilt);
  }
}




CopterCtrl::CopterCtrl(Settings::sptr const & settings,
                       const QSharedPointer<CopterAxis>& _axisX,
                       const QSharedPointer<CopterAxis>& _axisY,
                       QLCDNumber* _lcd)
  :m_lcd(_lcd),
    m_power(0),
    m_axisX(_axisX),
    m_axisY(_axisY),
    m_settings(settings)
{
}

void CopterCtrl::adjustTilt(double _tiltX, double _tiltY) const
{
  m_axisX->tilt(m_axisX->tilt() + _tiltX);
  m_axisY->tilt(m_axisY->tilt() + _tiltY);
  m_axisX->setPower(m_power);
  m_axisY->setPower(m_power);
}

void CopterCtrl::adjustPower(int _incr)
{
  static const auto s_power_min = m_settings->getPowerMin();
  static const auto s_power_max = m_settings->getPowerMax();

  m_power += _incr;
  m_power = qMax(qMin(m_power, s_power_max), s_power_min);

  QPalette palette = m_lcd->palette();
  QColor bg = palette.color(QPalette::Disabled, m_lcd->backgroundRole());
  double pwrSat = 1.0 - static_cast<double>(m_power-s_power_min)/(s_power_max-s_power_min);
  bg.setBlue( bg.blue() *pwrSat);
  bg.setGreen(bg.green()*pwrSat + 0xff*(1.0-pwrSat));
  bg.setRed(  bg.red()  *pwrSat);
  palette.setColor(QPalette::Normal, m_lcd->backgroundRole(), bg);
  palette.setColor(QPalette::Active, m_lcd->backgroundRole(), bg);
  palette.setColor(QPalette::Inactive, m_lcd->backgroundRole(), bg);
  m_lcd->setPalette(palette);
  m_lcd->display(m_power);

  m_axisX->setPower(m_power);
  m_axisY->setPower(m_power);
}




MainWindow::MainWindow(QWidget* _parent)
  :QMainWindow(_parent),
    m_settings(new Settings()),
    m_ui(new Ui::MainWindow()),
    m_copterCtrl(),
    m_tcpServer(),
    m_tcpConnection(),
    m_accelerometerCtrlFd(-1),
    m_accelerometerInputFd(-1),
    m_accelerometerInputNotifier(0),
    m_lastTiltX(0),
    m_lastTiltY(0)
{
  m_ui->setupUi(this);
  const auto s_ctrl_path = m_settings->getControlPath();
  QSharedPointer<CopterMotor> mx1(new CopterMotor(m_settings, s_ctrl_path+"ehrpwm.0/pwm/ehrpwm.0:0/duty_percent", m_ui->motor_x1));
  QSharedPointer<CopterMotor> mx2(new CopterMotor(m_settings, s_ctrl_path+"ehrpwm.0/pwm/ehrpwm.0:1/duty_percent", m_ui->motor_x2));
  QSharedPointer<CopterMotor> my1(new CopterMotor(m_settings, s_ctrl_path+"ehrpwm.1/pwm/ehrpwm.1:0/duty_percent", m_ui->motor_y1));
  QSharedPointer<CopterMotor> my2(new CopterMotor(m_settings, s_ctrl_path+"ehrpwm.1/pwm/ehrpwm.1:1/duty_percent", m_ui->motor_y2));
  QSharedPointer<CopterAxis>  m_axisX(new CopterAxis(mx1, mx2));
  QSharedPointer<CopterAxis>  m_axisY(new CopterAxis(my1, my2));
  m_copterCtrl = new CopterCtrl(m_settings, m_axisX, m_axisY, m_ui->motor_all);

  m_tcpServer.listen(QHostAddress::Any, m_settings->getTcpPort());
  connect(&m_tcpServer, SIGNAL(newConnection()), this, SLOT(onConnection()));

  auto const s_accel_ctrl_path = m_settings->getAccelControlPath();
  m_accelerometerCtrlFd = ::open(s_accel_ctrl_path.toLatin1().data(), O_SYNC, O_RDWR);
  if (m_accelerometerCtrlFd == -1)
    qDebug() << "Cannot open accelerometer ctrl file " << s_accel_ctrl_path << ", reason: " << errno;

  int delay_ms = 20;
  if (ioctl(m_accelerometerCtrlFd, MMA7660FC_IOCTL_S_POLL_DELAY, &delay_ms) != 0)
    qDebug() << "Cannot set poll delay to accelerometer ctrl file, reason: " << errno;

  auto const s_accel_input_path = m_settings->getAccelInputPath();
  m_accelerometerInputFd = ::open(s_accel_input_path.toLatin1().data(), O_SYNC, O_RDONLY);
  if (m_accelerometerInputFd == -1)
    qDebug() << "Cannot open accelerometer input file " << s_accel_input_path << ", reason: " << errno;

  m_accelerometerInputNotifier = new QSocketNotifier(m_accelerometerInputFd, QSocketNotifier::Read, this);
  connect(m_accelerometerInputNotifier, SIGNAL(activated(int)), this, SLOT(onAccelerometerRead()));
  m_accelerometerInputNotifier->setEnabled(true);

  m_copterCtrl->adjustPower(0);

  showFullScreen();
  showMaximized();
}

void MainWindow::onConnection()
{
  if (!m_tcpConnection.isNull())
    qDebug() << "Replacing existing connection";
  m_tcpConnection = m_tcpServer.nextPendingConnection();
  qDebug() << "Accepted new connection";
  m_tcpConnection->setSocketOption(QAbstractSocket::LowDelayOption, 1);
  connect(m_tcpConnection, SIGNAL(disconnected()), this, SLOT(onDisconnected()));
  connect(m_tcpConnection, SIGNAL(readyRead()), this, SLOT(onNetworkRead()));
}

void MainWindow::onDisconnected()
{
  qDebug() << "Existing connection disconnected";
  m_tcpConnection = 0;
}

void MainWindow::onNetworkRead()
{
  if (m_tcpConnection.isNull())
    return;

  static const auto s_tilt_step = m_settings->getTiltStep();
  static const auto s_power_max = m_settings->getPowerMax();
  //static const auto s_power_min = m_settings->getPowerMin();
  static const auto s_power_step1 = m_settings->getPowerStep1();
  static const auto s_power_step2 = m_settings->getPowerStep2();

  while (m_tcpConnection->isReadable())
  {
    char c;
    if (!m_tcpConnection->getChar(&c))
      break;
    switch (c)
    {
      case '1': m_copterCtrl->adjustTilt(-s_tilt_step, -s_tilt_step); break;
      case '2': m_copterCtrl->adjustTilt(0,            -s_tilt_step); break;
      case '3': m_copterCtrl->adjustTilt(+s_tilt_step, -s_tilt_step); break;
      case '4': m_copterCtrl->adjustTilt(-s_tilt_step, 0); break;
      case '5': m_copterCtrl->tiltX(0); m_copterCtrl->tiltY(0); break;
      case '6': m_copterCtrl->adjustTilt(+s_tilt_step, 0); break;
      case '7': m_copterCtrl->adjustTilt(-s_tilt_step, +s_tilt_step); break;
      case '8': m_copterCtrl->adjustTilt(0,            +s_tilt_step); break;
      case '9': m_copterCtrl->adjustTilt(+s_tilt_step, +s_tilt_step); break;
      case 'Z': m_copterCtrl->adjustPower(-s_power_max); break;
      case 'z': m_copterCtrl->adjustPower(-s_power_step2); break;
      case 'x': m_copterCtrl->adjustPower(-s_power_step1); break;
      case 'c': m_copterCtrl->adjustPower(+s_power_step1); break;
      case 'v': m_copterCtrl->adjustPower(+s_power_step2); break;
      case 'V': m_copterCtrl->adjustPower(+s_power_max); break;
    }
  }
}

void MainWindow::onAccelerometerRead()
{
  struct input_event evt;

  if (read(m_accelerometerInputFd, reinterpret_cast<char*>(&evt), sizeof(evt)) != sizeof(evt))
  {
    qDebug() << "Incomplete accelerometer data read";
    return;
  }

  if (evt.type != EV_ABS)
  {
    if (evt.type != EV_SYN)
      qDebug() << "Input event type is not EV_ABS or EV_SYN: " << evt.type;
    return;
  }

  char code = 0;
  switch (evt.code)
  {
    case ABS_X: code = 'x'; handleTiltX(evt.value); break;
    case ABS_Y: code = 'y'; handleTiltY(evt.value); break;
    case ABS_Z: code = 'z'; break;
  }
  if (code == 0 || m_tcpConnection.isNull())
    return;

  QString buf;
  buf.sprintf("%c%i ", code, static_cast<int>(evt.value));
  m_tcpConnection->write(buf.toAscii());
}

void MainWindow::handleTiltX(double _tilt)
{
  static const auto s_accel_linear = m_settings->getAccelLinear();
  static const auto s_accel_derivative = m_settings->getAccelDerivative();
  double adj = s_accel_linear*_tilt + s_accel_derivative*(_tilt - m_lastTiltX);
  m_copterCtrl->adjustTilt(adj, 0);
  m_lastTiltX = _tilt;
}

void MainWindow::handleTiltY(double _tilt)
{
  static const auto s_accel_linear = m_settings->getAccelLinear();
  static const auto s_accel_derivative = m_settings->getAccelDerivative();

  double adj = s_accel_linear*_tilt + s_accel_derivative*(_tilt - m_lastTiltY);
  m_copterCtrl->adjustTilt(0, adj);
  m_lastTiltY = _tilt;
}



