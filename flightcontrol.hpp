#ifndef FLIGHTCONTROL_HPP
#define FLIGHTCONTROL_HPP

#include <QObject>

#include "CopterCtrl.hpp"
#include "accelerometer.hpp"
#include "gyro.hpp"
#include "CopterAxis.hpp"

class FlightControl : public QObject
{
	Q_OBJECT
public:
	explicit FlightControl(CopterCtrl* CopterCtrl);
	
	float tiltX() const { return m_axisX->tilt(); }
	float tiltY() const { return m_axisY->tilt(); }
	void tiltX(float _tilt) const { m_axisX->tilt(_tilt); m_axisX->setPower(m_power); }
	void tiltY(float _tilt) const { m_axisY->tilt(_tilt); m_axisY->setPower(m_power); }
	void adjustTilt(float tiltX, float tiltY) const { QVector3D tilt(tiltX, tiltY, 0); adjustTilt(tilt); }
	void adjustTilt(QVector3D tilt) const;
	void adjustPower(int _incr);

	enum Motor {
		MotorX1,
		MotorX2,
		MotorY1,
		MotorY2,
		MotorAll
	};

signals:
	void accelerometerRead(QVector3D val);
	void motorPowerChanged(FlightControl::Motor motor, float power);

public slots:
	void emergencyStop();
	void setupAccelZeroAxis();

protected slots:
	void onAccelerometerRead(QVector3D val);
	void onGyroRead(QVector3D val);
	void initMotors(const QString& motorControlPath);
	void onMotorPowerChange(float power);
	void handleTilt(QVector3D tilt);

private:
	int m_power;
	CopterAxis* m_axisX;
	CopterAxis* m_axisY;

	QVector3D m_lastDerivative;
	QMap<CopterMotor*, Motor> m_motorIds;

	QVector3D m_lastTilt;
	QVector<QVector3D> m_pidIntegralVector;
	QVector3D m_pidIntegral;
	unsigned int m_pidCounter;

	Accelerometer* m_accel;
	Gyro* m_gyro;
	CopterCtrl* m_copterCtrl;
};

#endif // FLIGHTCONTROL_HPP
