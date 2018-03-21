/*!
 * @file
 *   @brief ST16 QtQuick Interface
 *   @author Gus Grubba <mavlink@grubba.com>
 *
 */

#include "QGCApplication.h"
#include "MultiVehicleManager.h"
#include "ParameterManager.h"
#include "AppSettings.h"
#include "SettingsManager.h"
#include "MAVLinkLogManager.h"
#include "QGCMapEngine.h"
#include "QGCCameraManager.h"
#include "VideoManager.h"
#include "VideoReceiver.h"
#include "YuneecCameraControl.h"
#include "YExportFiles.h"
#if defined(__planner__)
#include "QGCSyncFilesDesktop.h"
#else
#include "QGCSyncFilesMobile.h"
#endif

#include <QDirIterator>
#include <QtAlgorithms>

#include "TyphoonHQuickInterface.h"
#if defined(__androidx86__)
#include "TyphoonHM4Interface.h"
#endif

#if defined __android__
#include <jni.h>
#include <QtAndroidExtras/QtAndroidExtras>
#include <QtAndroidExtras/QAndroidJniObject>
extern const char* jniClassName;
#else
#include <QDesktopServices>
#endif

static const char* kWifiConfig      = "WifiConfig";
static const char* kUpdateCheck     = "YuneecUpdateCheck";
static const char* kThermalOpacity  = "ThermalOpacity";
static const char* kThermalMode     = "ThermalMode";
static const char* kSecondRun       = "SecondRun";
static const char* kFirstRun        = "FirstRun";
static const char* kLedParam        = "COM_LED_MODE";

#if defined(__androidx86__)
static const char* kUpdateFile = "/storage/sdcard1/update.zip";
static const char* kUpdateDest = "/mnt/sdcard/update.zip";
#endif

#if defined(__androidx86__)
void
reset_jni()
{
    QAndroidJniEnvironment env;
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
    }
}
#endif

#define FIRMWARE_FORCE_UPDATE_MAJOR 1
#define FIRMWARE_FORCE_UPDATE_MINOR 1
#define FIRMWARE_FORCE_UPDATE_PATCH 0

//-----------------------------------------------------------------------------
TyphoonHQuickInterface::TyphoonHQuickInterface(QObject* parent)
    : QObject(parent)
#if defined(__androidx86__)
    , _pHandler(NULL)
#endif
    , _vehicle(NULL)
    , _vehicleInReadyState(false)
    , _pFileCopy(NULL)
    , _videoReceiver(NULL)
    , _exporter(NULL)
    , _thermalMode(ThermalBlend)
    , _scanEnabled(false)
    , _scanningWiFi(false)
    , _bindingWiFi(false)
    , _copyingFiles(false)
    , _copyingDone(false)
    , _wifiAlertEnabled(true)
    , _browseVideos(false)
    , _updateProgress(0)
    , _updateDone(false)
    , _selectedCount(0)
    , _distSensorMin(0)
    , _distSensorMax(0)
    , _distSensorCur(0)
    , _obsState(false)
    , _isFactoryApp(false)
    , _thermalOpacity(85.0)
    , _isUpdaterApp(false)
    , _updateShown(false)
    , _firstRun(true)
    , _passwordSet(false)
    , _newPasswordSet(false)
#if defined(__planner__)
    , _desktopSync(NULL)
#else
    , _mobileSync(NULL)
#endif
{
    qCDebug(YuneecLog) << "TyphoonHQuickInterface Created";
#if defined __android__
    reset_jni();
    _isFactoryApp = (bool)QAndroidJniObject::callStaticMethod<jboolean>(jniClassName, "isFactoryAppInstalled");
    _isUpdaterApp = (bool)QAndroidJniObject::callStaticMethod<jboolean>(jniClassName, "isUpdaterAppInstalled");
#endif
    QSettings settings;
    _thermalOpacity = settings.value(kThermalOpacity, 85.0).toDouble();
    _thermalMode =  (ThermalViewMode)settings.value(kThermalMode, (uint32_t)ThermalBlend).toUInt();
    _firstRun = settings.value(kFirstRun, true).toBool();
    qCDebug(YuneecLog) << "FirstRun:" << _firstRun;
    _loadWifiConfigurations();
    _ssid = connectedSSID();
}

//-----------------------------------------------------------------------------
TyphoonHQuickInterface::~TyphoonHQuickInterface()
{
    qCDebug(YuneecLog) << "TyphoonHQuickInterface Destroyed";
    _clearSSids();
    if(_videoReceiver) {
        delete _videoReceiver;
    }
    if(_exporter) {
        _exporter->deleteLater();
    }
#if defined(__planner__)
    if(_desktopSync) {
        _desktopSync->deleteLater();
    }
#else
    if(_mobileSync) {
        _mobileSync->deleteLater();
    }
#endif
}

//-----------------------------------------------------------------------------
static bool
created_greater_than(const QFileInfo &f1, const QFileInfo &f2)
 {
     return f1.created() > f2.created();
 }

//-----------------------------------------------------------------------------
void
#if defined(__androidx86__)
TyphoonHQuickInterface::init(TyphoonHM4Interface* pHandler)
#else
TyphoonHQuickInterface::init()
#endif
{
    qmlRegisterType<TyphoonMediaItem>("TyphoonMediaItem", 1,0, "TyphoonMediaItem");
#if defined(__androidx86__)
    _pHandler = pHandler;
    if(_pHandler) {
        connect(_pHandler, &TyphoonHM4Interface::m4StateChanged,               this, &TyphoonHQuickInterface::_m4StateChanged);
        connect(_pHandler, &TyphoonHM4Interface::destroyed,                    this, &TyphoonHQuickInterface::_destroyed);
        connect(_pHandler, &TyphoonHM4Interface::controllerLocationChanged,    this, &TyphoonHQuickInterface::_controllerLocationChanged);
        connect(_pHandler, &TyphoonHM4Interface::newWifiSSID,                  this, &TyphoonHQuickInterface::_newSSID);
        connect(_pHandler, &TyphoonHM4Interface::newWifiRSSI,                  this, &TyphoonHQuickInterface::_newRSSI);
        connect(_pHandler, &TyphoonHM4Interface::scanComplete,                 this, &TyphoonHQuickInterface::_scanComplete);
        connect(_pHandler, &TyphoonHM4Interface::authenticationError,          this, &TyphoonHQuickInterface::_authenticationError);
        connect(_pHandler, &TyphoonHM4Interface::wifiConnected,                this, &TyphoonHQuickInterface::_wifiConnected);
        connect(_pHandler, &TyphoonHM4Interface::wifiDisconnected,             this, &TyphoonHQuickInterface::_wifiDisconnected);
        connect(_pHandler, &TyphoonHM4Interface::batteryUpdate,                this, &TyphoonHQuickInterface::_batteryUpdate);
        connect(_pHandler, &TyphoonHM4Interface::rawChannelsChanged,           this, &TyphoonHQuickInterface::_rawChannelsChanged);
        connect(_pHandler, &TyphoonHM4Interface::buttonStateChanged,           this, &TyphoonHQuickInterface::_buttonStateChanged);
        connect(_pHandler, &TyphoonHM4Interface::switchStateChanged,           this, &TyphoonHQuickInterface::_switchStateChanged);
        connect(_pHandler, &TyphoonHM4Interface::calibrationStateChanged,      this, &TyphoonHQuickInterface::_calibrationStateChanged);
        connect(_pHandler, &TyphoonHM4Interface::calibrationCompleteChanged,   this, &TyphoonHQuickInterface::_calibrationCompleteChanged);
        connect(_pHandler, &TyphoonHM4Interface::rcActiveChanged,              this, &TyphoonHQuickInterface::_rcActiveChanged);
#endif
        //-- RPC
#if defined(__planner__)
        _desktopSync = new QGCSyncFilesDesktop(this);
        emit desktopSyncChanged();
#else
        _mobileSync = new QGCSyncFilesMobile(this);
        emit mobileSyncChanged();
#endif
        connect(getQGCMapEngine(), &QGCMapEngine::internetUpdated,             this, &TyphoonHQuickInterface::_internetUpdated);
        connect(qgcApp()->toolbox()->multiVehicleManager(), &MultiVehicleManager::vehicleAdded,         this, &TyphoonHQuickInterface::_vehicleAdded);
        connect(qgcApp()->toolbox()->multiVehicleManager(), &MultiVehicleManager::vehicleRemoved,       this, &TyphoonHQuickInterface::_vehicleRemoved);
        connect(qgcApp()->toolbox()->multiVehicleManager(), &MultiVehicleManager::parameterReadyVehicleAvailableChanged, this, &TyphoonHQuickInterface::_vehicleReady);
        connect(qgcApp()->toolbox()->videoManager()->videoReceiver(), &VideoReceiver::imageFileChanged,   this, &TyphoonHQuickInterface::_imageFileChanged);
        connect(&_scanTimer,        &QTimer::timeout, this, &TyphoonHQuickInterface::_scanWifi);
        connect(&_flightTimer,      &QTimer::timeout, this, &TyphoonHQuickInterface::_flightUpdate);
        _flightTimer.setSingleShot(false);
#if defined(__androidx86__)
        connect(&_powerTimer,       &QTimer::timeout, this, &TyphoonHQuickInterface::_powerTrigger);
        _powerTimer.setSingleShot(true);
#endif
        //-- Make sure uLog is disabled
        qgcApp()->toolbox()->mavlinkLogManager()->setEnableAutoUpload(false);
        qgcApp()->toolbox()->mavlinkLogManager()->setEnableAutoStart(false);
        //-- See how many logs we have stored
        QString filter = "*.";
        filter += qgcApp()->toolbox()->settingsManager()->appSettings()->telemetryFileExtension;
        QDir logDir(qgcApp()->toolbox()->settingsManager()->appSettings()->telemetrySavePath(), filter);
        QFileInfoList logs = logDir.entryInfoList();
        std::sort(logs.begin(), logs.end(), created_greater_than);
        if(logs.size() > 1) {
            qint64 totalLogSize = 0;
            for(int i = 0; i < logs.size(); i++) {
               totalLogSize += logs[i].size();
            }
            //-- We want to limit at 1G
            while(totalLogSize > (1024 * 1024 * 1024) && logs.size()) {
                //qDebug() << "Removing old log file:" << logs[0].fileName();
                totalLogSize -= logs[0].size();
                QFile::remove(logs[0].filePath());
                logs.removeAt(0);
            }
        }
#if !defined(__planner__)
        //-- Thermal video surface must be created before UI
        if(!_videoReceiver) {
            _videoReceiver = new VideoReceiver(this);
            connect(_videoReceiver, &VideoReceiver::videoRunningChanged, this, &TyphoonHQuickInterface::_videoRunningChanged);
        }
#endif
#if defined(__androidx86__)
    }
#endif
}

//-----------------------------------------------------------------------------
bool
TyphoonHQuickInterface::shouldWeShowUpdate()
{
    //-- Only show once per session
    if(_firstRun || _updateShown) {
        return false;
    }
    bool res = false;
    QSettings settings;
    bool secondRun = settings.value(kSecondRun, true).toBool();
    //-- First run sets the password. Now we check for updates.
    QDate lastCheck = settings.value(kUpdateCheck, QDate::currentDate()).toDate();
    QDate now = QDate::currentDate();
    if(secondRun) {
        settings.setValue(kSecondRun, false);
        // If we run recently, it's just a password change
        if(lastCheck.year() != now.year()) {
            qWarning() << "First run after settings done. Force update dialog";
            res = true;
            //-- Reset update timer
            settings.setValue(kUpdateCheck, QDate::currentDate());
        }
    } else {
        //-- If we have Internet, reset timer
        if(getQGCMapEngine()->isInternetActive()) {
            settings.setValue(kUpdateCheck, QDate::currentDate());
        } else {
            if(lastCheck.daysTo(now) > 29) {
                //-- Reset update timer
                settings.setValue(kUpdateCheck, QDate::currentDate());
                //-- It's been too long
                qWarning() << "Too long since last Internet connection. Force update dialog";
                res = true;
            }
        }
    }
    //-- Check firmware version (if any)
    if(!res) {
        Vehicle* v = qgcApp()->toolbox()->multiVehicleManager()->activeVehicle();
        if(v) {
            uint32_t frver = FIRMWARE_FORCE_UPDATE_MAJOR << 16 | FIRMWARE_FORCE_UPDATE_MINOR << 8 | FIRMWARE_FORCE_UPDATE_PATCH;
            uint32_t fmver = v->firmwareCustomMajorVersion() << 16 | v->firmwareCustomMinorVersion() << 8 | v->firmwareCustomPatchVersion();
            //-- If fmver == 0, it's a dev firmware. Don't bother testing it.
            if(fmver && frver >= fmver) {
                //-- Reset update timer
                settings.setValue(kUpdateCheck, QDate::currentDate());
                //-- Show the dialog as this is the shipping version
                qWarning() << "Firmware version is shipping version. Force update dialog";
                res = true;
            } else {
                qCDebug(YuneecLog) << "Firmware version OK" << FIRMWARE_FORCE_UPDATE_MAJOR << FIRMWARE_FORCE_UPDATE_MINOR << FIRMWARE_FORCE_UPDATE_PATCH << " : " << v->firmwareCustomMajorVersion() << v->firmwareCustomMinorVersion() << v->firmwareCustomPatchVersion();
            }
        } else {
            qWarning() << "Vehicle not available when checking version.";
        }
    }
    _updateShown = res;
    return res;
}

//-----------------------------------------------------------------------------
bool
TyphoonHQuickInterface::isInternet()
{
    return getQGCMapEngine()->isInternetActive();
}

//-----------------------------------------------------------------------------
bool
TyphoonHQuickInterface::isDefaultPwd()
{
    if(_ssid.isEmpty()) {
        qCDebug(YuneecLog) << "isDefaultPwd() No current ssid";
        return false;
    }
    qCDebug(YuneecLog) << "isDefaultPwd()" << _configurations.contains(_ssid) << (_configurations.contains(_ssid) ? _configurations[_ssid] : "N/A");
    if(_configurations.size() && _configurations.contains(_ssid)) {
        return _configurations[_ssid] == "1234567890";
    }
    return false;
}

//-----------------------------------------------------------------------------
bool
TyphoonHQuickInterface::firstRun()
{
    return _firstRun;
}

//-----------------------------------------------------------------------------
void
TyphoonHQuickInterface::_vehicleAdded(Vehicle* vehicle)
{
    if(!_vehicle) {
        qCDebug(YuneecLog) << "_vehicleAdded()";
        _vehicle = vehicle;
        connect(_vehicle, &Vehicle::mavlinkMessageReceived, this, &TyphoonHQuickInterface::_mavlinkMessageReceived);
        connect(_vehicle, &Vehicle::armedChanged,           this, &TyphoonHQuickInterface::_armedChanged);
#if !defined (__planner__)
        connect(_vehicle, &Vehicle::dynamicCamerasChanged,  this, &TyphoonHQuickInterface::_dynamicCamerasChanged);
        _dynamicCamerasChanged();
#endif
    }
#if !defined (__planner__)
    if(!_passwordSet) {
        //-- If we dind't bind to anyting, it means this isn't really a first run. We've been here before.
        qCDebug(YuneecLog) << "Force firstRun to false";
        _resetFirstRun(true);
    }
#endif
}

//-----------------------------------------------------------------------------
void
TyphoonHQuickInterface::_vehicleRemoved(Vehicle* vehicle)
{
    if(_vehicle == vehicle) {
        qCDebug(YuneecLog) << "_vehicleRemoved()";
        disconnect(_vehicle, &Vehicle::mavlinkMessageReceived,  this, &TyphoonHQuickInterface::_mavlinkMessageReceived);
        disconnect(_vehicle, &Vehicle::armedChanged,            this, &TyphoonHQuickInterface::_armedChanged);
#if !defined (__planner__)
        disconnect(_vehicle, &Vehicle::dynamicCamerasChanged,   this, &TyphoonHQuickInterface::_dynamicCamerasChanged);
#endif
        _vehicle = NULL;
        _vehicleInReadyState = false;
    }
}

//-----------------------------------------------------------------------------
void
TyphoonHQuickInterface::_vehicleReady(bool ready)
{
    _vehicleInReadyState = ready;
    if(_vehicle && ready) {
        //-- Update LED Fact now that the parameters are loaded
        emit ledFactChanged();
    }
}

//-----------------------------------------------------------------------------
void
TyphoonHQuickInterface::_dynamicCamerasChanged()
{
#if !defined (__planner__)
    //-- Keep track of camera changes
    if(_vehicle->dynamicCameras()) {
        connect(_vehicle->dynamicCameras(), &QGCCameraManager::camerasChanged, this, &TyphoonHQuickInterface::_camerasChanged);
    }
#endif
}

//-----------------------------------------------------------------------------
void
TyphoonHQuickInterface::_camerasChanged()
{
#if !defined (__planner__)
    if(_vehicle) {
        if(_vehicle->dynamicCameras() && _vehicle->dynamicCameras()->cameras()->count()) {
            //-- A camera has just been added. Check for CGOET or E10T
            YuneecCameraControl* pCamera = qobject_cast<YuneecCameraControl*>((*_vehicle->dynamicCameras()->cameras())[0]);
            if(pCamera) {
                if(pCamera->isThermal()) {
                    connect(pCamera, &YuneecCameraControl::isVideoRecordingChanged, this, &TyphoonHQuickInterface::_isVideoRecordingChanged);
                    qCDebug(YuneecLog) << "Starting thermal image receiver";
                    if(pCamera->isCGOET()) {
                        _videoReceiver->setUri(QStringLiteral("rtsp://192.168.42.1:8554/live"));
                    } else {
                        _videoReceiver->setUri(QStringLiteral("rtsp://192.168.42.1:554/stream2"));
                    }
                    _videoReceiver->start();
                    emit thermalImagePresentChanged();
                }
            }
        }
    }
#endif
}

//-----------------------------------------------------------------------------
void
TyphoonHQuickInterface::_isVideoRecordingChanged()
{
#if !defined (__planner__)
    if(_vehicle && _videoReceiver) {
        if(_vehicle->dynamicCameras() && _vehicle->dynamicCameras()->cameras()->count()) {
            YuneecCameraControl* pCamera = qobject_cast<YuneecCameraControl*>((*_vehicle->dynamicCameras()->cameras())[0]);
            if(pCamera && pCamera->isThermal()) {
                //-- Record thermal image as well
                if(pCamera->isVideoRecording()) {
                    QString videoFile = qgcApp()->toolbox()->videoManager()->videoReceiver()->videoFile();
                    videoFile.replace(YUNEEC_VIDEO_EXTENSION, tr("-Thermal") + QString(YUNEEC_VIDEO_EXTENSION));
                   _videoReceiver->startRecording(videoFile);
                } else {
                    _videoReceiver->stopRecording();
                }
            }
        }
    }
#endif
}

//-----------------------------------------------------------------------------
void
TyphoonHQuickInterface::_mavlinkMessageReceived(const mavlink_message_t& message)
{
    if(message.msgid == MAVLINK_MSG_ID_DISTANCE_SENSOR) {
        mavlink_distance_sensor_t dist;
        mavlink_msg_distance_sensor_decode(&message, &dist);
        _distanceSensor((int)dist.min_distance, (int)dist.max_distance, (int)dist.current_distance);
    }
}

//-----------------------------------------------------------------------------
void
TyphoonHQuickInterface::_internetUpdated()
{
    emit isInternetChanged();
}

//-----------------------------------------------------------------------------
void
TyphoonHQuickInterface::launchBroswer(QString url)
{
#if defined __android__
    reset_jni();
    QAndroidJniObject javaSSID = QAndroidJniObject::fromString(url);
    QAndroidJniObject::callStaticMethod<void>(jniClassName, "launchBrowser", "(Ljava/lang/String;)V", javaSSID.object<jstring>());
#else
    QDesktopServices::openUrl(QUrl(url));
#endif
}

//-----------------------------------------------------------------------------
void
TyphoonHQuickInterface::launchUpdater()
{
#if defined __android__
    reset_jni();
    QAndroidJniObject::callStaticMethod<void>(jniClassName, "launchUpdater", "()V");
#endif
}

//-----------------------------------------------------------------------------
void
TyphoonHQuickInterface::setWiFiPassword(QString pwd, bool restart)
{
    if(_vehicle) {
        //-- Save new password (Android Configuration)
        _password = pwd;
        _configurations[_ssid] = _password;
        _saveWifiConfigurations();
#if defined __android__
        reset_jni();
        QAndroidJniObject javaSSID = QAndroidJniObject::fromString(_ssid);
        QAndroidJniObject javaPWD  = QAndroidJniObject::fromString(_password);
        QAndroidJniObject::callStaticMethod<void>(jniClassName, "setWifiPassword", "(Ljava/lang/String;Ljava/lang/String;)V", javaSSID.object<jstring>(), javaPWD.object<jstring>());
#endif
        _resetFirstRun(false);
        if(restart) {
#if defined(__androidx86__)
            QTimer::singleShot(500, this, &TyphoonHQuickInterface::_restart);
#endif
        }
    }
}

//-----------------------------------------------------------------------------
#if defined(__androidx86__)
void
TyphoonHQuickInterface::_powerTrigger()
{
    //-- If RC is not working, and if we actually have the vehicle,
    //-- otherwise this will trigger while fetching params initially.
    if(!_pHandler->rcActive() && _vehicle && _vehicleInReadyState) {
        //-- Panic button held down
        emit powerHeld();
    }
}
#endif

//-----------------------------------------------------------------------------
#if defined(__androidx86__)
void
TyphoonHQuickInterface::_switchStateChanged(M4Lib::SwitchId switchId, M4Lib::SwitchState switchState)
{
    if(switchId == M4Lib::SwitchId::OBSTACLE_AVOIDANCE) {
        if(switchState == M4Lib::SwitchState::ON && !_obsState) {
            _obsState = true;
            emit obsStateChanged();
        } else if(_obsState) {
            _obsState = false;
            emit obsStateChanged();
        }
    }
}
#endif

//-----------------------------------------------------------------------------
#if defined(__androidx86__)
void
TyphoonHQuickInterface::_buttonStateChanged(M4Lib::ButtonId buttonId, M4Lib::ButtonState buttonState)
{
    if(buttonId == M4Lib::ButtonId::POWER) {
        if(buttonState == M4Lib::ButtonState::PRESSED) {
            _powerTimer.start(1000);
        } else {
            _powerTimer.stop();
        }
    }
}
#endif

//-----------------------------------------------------------------------------
void
TyphoonHQuickInterface::_videoRunningChanged()
{
    qCDebug(YuneecLog) << "TyphoonHQuickInterface::_videoRunningChanged()";
    emit thermalImagePresentChanged();
}

//-----------------------------------------------------------------------------
bool
TyphoonHQuickInterface::thermalImagePresent()
{
#if defined(QGC_GST_STREAMING)
    bool res = _videoReceiver && _videoReceiver->running();
    return res;
#else
    return false;
#endif
}

//-----------------------------------------------------------------------------
void
TyphoonHQuickInterface::_m4StateChanged()
{
    emit m4StateChanged();
}

//-----------------------------------------------------------------------------
void
TyphoonHQuickInterface::_controllerLocationChanged()
{
    emit controllerLocationChanged();
}

//-----------------------------------------------------------------------------
void
TyphoonHQuickInterface::_destroyed()
{
#if defined(__androidx86__)
    disconnect(_pHandler, &TyphoonHM4Interface::m4StateChanged,       this, &TyphoonHQuickInterface::_m4StateChanged);
    disconnect(_pHandler, &TyphoonHM4Interface::destroyed,            this, &TyphoonHQuickInterface::_destroyed);
    _pHandler = NULL;
#endif
}

//-----------------------------------------------------------------------------
TyphoonHQuickInterface::M4State
TyphoonHQuickInterface::m4State()
{
#if defined(__androidx86__)
    if(_pHandler) {
        return _pHandler->m4State();
    }
#endif
    return TyphoonHQuickInterface::M4_STATE_NONE;
}

//-----------------------------------------------------------------------------
double
TyphoonHQuickInterface::latitude()
{
#if defined(__androidx86__)
    if(_pHandler) {
        return _pHandler->controllerLocation().latitude;
    }
#endif
    return 0.0;
}

//-----------------------------------------------------------------------------
double
TyphoonHQuickInterface::longitude()
{
#if defined(__androidx86__)
    if(_pHandler) {
        return _pHandler->controllerLocation().longitude;
    }
#endif
    return 0.0;
}

//-----------------------------------------------------------------------------
double
TyphoonHQuickInterface::altitude()
{
#if defined(__androidx86__)
    if(_pHandler) {
        return _pHandler->controllerLocation().altitude;
    }
#endif
    return 0.0;
}

//-----------------------------------------------------------------------------
double
TyphoonHQuickInterface::speed()
{
#if defined(__androidx86__)
    if(_pHandler) {
        return _pHandler->controllerLocation().speed;
    }
#endif
    return 0.0;
}

//-----------------------------------------------------------------------------
double
TyphoonHQuickInterface::gpsCount()
{
#if defined(__androidx86__)
    if(_pHandler) {
        return _pHandler->controllerLocation().satelliteCount;
    }
#endif
    return 0.0;
}

//-----------------------------------------------------------------------------
double
TyphoonHQuickInterface::gpsAccuracy()
{
#if defined(__androidx86__)
    if(_pHandler) {
        return _pHandler->controllerLocation().pdop;
    }
#endif
    return 0.0;
}

//-----------------------------------------------------------------------------
QString
TyphoonHQuickInterface::m4StateStr()
{
#if defined(__androidx86__)
    if(_pHandler) {
        _pHandler->m4StateStr();
    }
#endif
    return QString();
}

//-----------------------------------------------------------------------------
void
TyphoonHQuickInterface::initM4()
{
#if defined(__androidx86__)
    if(_pHandler) {
        _pHandler->softReboot();
    }
#endif
}

//-----------------------------------------------------------------------------
void
TyphoonHQuickInterface::enterBindMode()
{
#if defined(__androidx86__)
    if(_pHandler) {
        _pHandler->enterBindMode();
    }
#endif
}

//-----------------------------------------------------------------------------
void
TyphoonHQuickInterface::startScan(int delay)
{
    _clearSSids();
    _scanEnabled  = true;
    emit ssidListChanged();
#if defined(__androidx86__)
    if(delay) {
        QTimer::singleShot(delay, this, &TyphoonHQuickInterface::_scanWifi);
    } else {
        _scanWifi();
    }
#else
    Q_UNUSED(delay);
    _newSSID(QString("Some SSID"), 0);
    _newSSID(QString("Another SSID"), -10);
    _newSSID(QString("Yet Another SSID"), -20);
    _newSSID(QString("More SSID"), -30);
    _newSSID(QString("CIA Headquarters"), -40);
    _newSSID(QString("Trump Putin Direct"), -50);
    _newSSID(QString("Short"), -60);
    _newSSID(QString("A Whole Lot Longer and Useless"), -90);
#endif
}

//-----------------------------------------------------------------------------
void
TyphoonHQuickInterface::stopScan()
{
    _scanTimer.stop();
    _scanEnabled = false;
    _scanningWiFi = false;
    emit scanningWiFiChanged();
}

//-----------------------------------------------------------------------------
void
TyphoonHQuickInterface::_scanWifi()
{
#if defined(__androidx86__)
    reset_jni();
    QAndroidJniObject::callStaticMethod<void>(jniClassName, "startWifiScan", "()V");
#endif
    _scanningWiFi = true;
    emit scanningWiFiChanged();
}

//-----------------------------------------------------------------------------
bool
TyphoonHQuickInterface::connected()
{
    return rssi() < 0;
}

//-----------------------------------------------------------------------------
void
TyphoonHQuickInterface::resetWifi()
{
#if defined(__androidx86__)
    //-- Stop scanning and clear list
    stopScan();
    _clearSSids();
    emit ssidListChanged();
    //-- Reset all wifi configurations
    if(_pHandler) {
        _pHandler->resetBind();
    }
    reset_jni();
    QAndroidJniObject::callStaticMethod<void>(jniClassName, "resetWifi", "()V");
    emit connectedSSIDChanged();
    //-- Start scanning again in a bit
    startScan(1000);
#endif
}

//-----------------------------------------------------------------------------
bool
TyphoonHQuickInterface::isWifiConfigured(QString ssid)
{
    return _configurations.contains(ssid);
}

//-----------------------------------------------------------------------------
void
TyphoonHQuickInterface::bindWIFI(QString ssid, QString password)
{
    stopScan();
    _clearSSids();
    emit ssidListChanged();
    _bindingWiFi = true;
    emit bindingWiFiChanged();
    _ssid = ssid;
    _password = password;
    if(password.isEmpty()) {
        if(_configurations.contains(ssid)) {
            _password = _configurations[ssid];
        }
    } else {
        //-- This is a new binding to a new camera
        _passwordSet = true;
    }
#if defined(__androidx86__)
    reset_jni();
    QAndroidJniObject::callStaticMethod<void>(jniClassName, "disconnectWifi", "()V");
    //-- There isn't currently a way to disconnect and remove a Vehicle from here.
    //   The Wifi disconnect above will cause the vehicle to disconnect on its own
    //   after a heartbeat timeout.
    QTimer::singleShot(5000, this, &TyphoonHQuickInterface::_delayedBind);
#else
    Q_UNUSED(ssid);
    Q_UNUSED(password);
#endif
}

//-----------------------------------------------------------------------------
bool
TyphoonHQuickInterface::checkForUpdate()
{
#if defined(__androidx86__)
    QFileInfo fi(kUpdateFile);
    return fi.exists();
#else
    return false;
#endif
}

//-----------------------------------------------------------------------------
void
TyphoonHQuickInterface::_imageUpdateProgress(int current)
{
    _updateProgress = current;
    emit updateProgressChanged();
}

//-----------------------------------------------------------------------------
void
TyphoonHQuickInterface::_imageUpdateError(QString errorMsg)
{
    _updateError = errorMsg;
    emit updateErrorChanged();
    _endCopyThread();
    qCDebug(YuneecLog) << "Error:" << errorMsg;
}

//-----------------------------------------------------------------------------
void
TyphoonHQuickInterface::_endCopyThread()
{
    if(_pFileCopy) {
        _pFileCopy->thread()->quit();
        _pFileCopy->thread()->wait();
        _pFileCopy = NULL;
        emit updatingChanged();
    }
}

//-----------------------------------------------------------------------------
void
TyphoonHQuickInterface::_imageUpdateDone()
{
    //-- File copy finished. Reboot and update.
#if defined(__androidx86__)
    _endCopyThread();
    qCDebug(YuneecLog) << "Copy complete. Reboot for update.";
    reset_jni();
    QAndroidJniObject::callStaticMethod<void>(jniClassName, "updateImage", "()V");
#endif
}

//-----------------------------------------------------------------------------
void
TyphoonHQuickInterface::updateSystemImage()
{
#if defined(__androidx86__)
    qCDebug(YuneecLog) << "Initializing update";
    _updateError.clear();
    _updateProgress = 0;
    _updateDone     = false;
    emit updateErrorChanged();
    emit updateProgressChanged();
    if(checkForUpdate()) {
        //-- Create file copy thread
        _pFileCopy = new TyphoonHFileCopy(kUpdateFile, kUpdateDest);
        emit updatingChanged();
        QThread* pCopyThread = new QThread(this);
        pCopyThread->setObjectName("CopyThread");
        connect(pCopyThread, &QThread::finished, _pFileCopy, &QObject::deleteLater);
        _pFileCopy->moveToThread(pCopyThread);
        pCopyThread->start();
        //-- Start copy
        connect(_pFileCopy,  &TyphoonHFileCopy::copyProgress,   this,       &TyphoonHQuickInterface::_imageUpdateProgress);
        connect(_pFileCopy,  &TyphoonHFileCopy::copyError,      this,       &TyphoonHQuickInterface::_imageUpdateError);
        connect(_pFileCopy,  &TyphoonHFileCopy::copyDone,       this,       &TyphoonHQuickInterface::_imageUpdateDone);
        QTimer::singleShot(100, _pFileCopy, &TyphoonHFileCopy::startCopy);
    } else {
        _imageUpdateError(QString(tr("Could not locate update file.")));
    }
#endif
}

//-----------------------------------------------------------------------------
void
TyphoonHQuickInterface::_restart()
{
#if defined(__androidx86__)
    qCDebug(YuneecLog) << "Restart DataPilot";
    reset_jni();
    QAndroidJniObject::callStaticMethod<void>(jniClassName, "restartApp", "()V");
#endif
}

//-----------------------------------------------------------------------------
void
TyphoonHQuickInterface::factoryTest()
{
#if defined(__androidx86__)
    qCDebug(YuneecLog) << "Exit to Factory Test";
    reset_jni();
    QAndroidJniObject::callStaticMethod<void>(jniClassName, "launchFactoryTest", "()V");
#endif
}

//-----------------------------------------------------------------------------
#define READ_CHUNK_SIZE 1024 * 1024 * 4
void
TyphoonHFileCopy::startCopy()
{
    qCDebug(YuneecLog) << "Copying update file";
    //-- File copy thread
    QFileInfo fi(_src);
    uint64_t total   = fi.size();
    uint64_t current = 0;
    QFile inFile(_src);
    if (!inFile.open(QIODevice::ReadOnly)) {
        emit copyError(QString(tr("Error opening firmware update file.")));
        return;
    }
    QFile outFile(_dst);
    if (!outFile.open(QIODevice::WriteOnly)) {
        emit copyError(QString(tr("Error opening firmware destination file.")));
        return;
    }
    QByteArray bytes;
    bytes.resize(READ_CHUNK_SIZE);
    while(!inFile.atEnd())
    {
        qint64 count = inFile.read(bytes.data(), READ_CHUNK_SIZE);
        if(count < 0) {
            emit copyError(QString(tr("Error reading firmware file.")));
            outFile.close();
            outFile.remove();
            return;
        }
        if(count && outFile.write(bytes.data(), count) != count) {
            emit copyError(QString(tr("Error writing firmware file.")));
            outFile.close();
            outFile.remove();
            return;
        }
        current += count;
        int progress = (int)((double)current / (double)total * 100.0);
        emit copyProgress(progress);
        qCDebug(YuneecLog) << "Copying" << total << current << progress;
    }
    inFile.close();
    outFile.close();
    emit copyDone();
    qCDebug(YuneecLog) << "Copy complete";
}

//-----------------------------------------------------------------------------
void
TyphoonHQuickInterface::_delayedBind()
{
#if defined(__androidx86__)
    reset_jni();
    QAndroidJniObject javaSSID = QAndroidJniObject::fromString(_ssid);
    QAndroidJniObject javaPassword = QAndroidJniObject::fromString(_password);
    jboolean jTrue = true;
    QAndroidJniObject::callStaticMethod<void>(jniClassName, "bindSSID", "(Ljava/lang/String;Ljava/lang/String;Z)V", javaSSID.object<jstring>(), javaPassword.object<jstring>(), jTrue);
    QTimer::singleShot(15000, this, &TyphoonHQuickInterface::_bindTimeout);
#endif
}

//-----------------------------------------------------------------------------
int
TyphoonHQuickInterface::rssi()
{
    int res = 0;
#if defined(__androidx86__)
    reset_jni();
    res = (int)QAndroidJniObject::callStaticMethod<jint>(jniClassName, "wifiRssi", "()I");
#endif
   return res;
}

//-----------------------------------------------------------------------------
Fact*
TyphoonHQuickInterface::ledFact()
{
    if (_vehicle && _vehicle->parameterManager()->parameterExists(_vehicle->defaultComponentId(), kLedParam)) {
        Fact* fact = _vehicle->parameterManager()->getParameter(_vehicle->defaultComponentId(), kLedParam);
        QQmlEngine::setObjectOwnership(fact, QQmlEngine::CppOwnership);
        return fact;
    }
    return NULL;
}

//-----------------------------------------------------------------------------
qreal
TyphoonHQuickInterface::rcBattery()
{
    qreal res = 0.0;
#if defined(__androidx86__)
    reset_jni();
    res = (qreal)QAndroidJniObject::callStaticMethod<jfloat>(jniClassName, "getBatteryLevel", "()F");
#endif
   return res;
}

//-----------------------------------------------------------------------------
QString
TyphoonHQuickInterface::connectedSSID()
{
    QString ssid;
#if defined(__androidx86__)
    reset_jni();
    QAndroidJniObject str = QAndroidJniObject::callStaticObjectMethod(jniClassName, "connectedSSID", "()Ljava/lang/String;");
    ssid = str.toString();
    if(ssid.startsWith("\"")) ssid.remove(0,1);
    if(ssid.endsWith("\""))   ssid.remove(ssid.size()-1,1);
#else
    //ssid = "CIA Headquarters";
    ssid = "CGOET CIA Headquarters";
#endif
    return ssid;
}

//-----------------------------------------------------------------------------
QString
TyphoonHQuickInterface::connectedCamera()
{
    QString ssid = connectedSSID();
    if(ssid.startsWith("CGOET")) {
        return QString("CGO-ET");
    }
    if(ssid.startsWith("E10T")) {
        return QString("E10T");
    }
    if(ssid.startsWith("E90")) {
        return QString("E90");
    }
    if(ssid.startsWith("E50")) {
        return QString("E50");
    }
    return QString();
}

//-----------------------------------------------------------------------------
bool
TyphoonHQuickInterface::isTyphoon()
{
    QString ssid = connectedSSID();
    if(ssid.startsWith("CGOET") || ssid.startsWith("E10T") || ssid.startsWith("E90_") || ssid.startsWith("E50_")) {
        return true;
    }
    return false;
}

//-----------------------------------------------------------------------------
int
TyphoonHQuickInterface::rawChannel(int channel)
{
#if defined(__androidx86__)
    if(_pHandler) {
        if(channel < _pHandler->rawChannels().size()) {
            uint16_t t = _pHandler->rawChannels()[channel];
            return t;
        }
    }
#else
    Q_UNUSED(channel);
#endif
    return 0;
}

//-----------------------------------------------------------------------------
int
TyphoonHQuickInterface::calChannelState(int channel)
{
#if defined(__androidx86__)
    if(_pHandler) {
        return _pHandler->calChannel(channel);
    }
#else
    Q_UNUSED(channel);
#endif
    return 0;
}

//-----------------------------------------------------------------------------
bool
TyphoonHQuickInterface::calibrationComplete()
{
#if defined(__androidx86__)
    if(_pHandler) {
        return _pHandler->rcCalibrationComplete();
    }
#endif
    return false;
}

//-----------------------------------------------------------------------------
QQmlListProperty<TyphoonMediaItem>
TyphoonHQuickInterface::mediaList()
{
    return QQmlListProperty<TyphoonMediaItem>(
        this, this,
        &TyphoonHQuickInterface::appendMediaItem,
        &TyphoonHQuickInterface::mediaCount,
        &TyphoonHQuickInterface::mediaItem,
        &TyphoonHQuickInterface::clearMediaItems
        );
}

//-----------------------------------------------------------------------------
TyphoonMediaItem*
TyphoonHQuickInterface::mediaItem(int index)
{
    if(index >= 0 && _mediaList.size() && index < _mediaList.size()) {
        return _mediaList.at(index);
    }
    return NULL;
}

//-----------------------------------------------------------------------------
int
TyphoonHQuickInterface::mediaCount()
{
    return _mediaList.size();
}

//-----------------------------------------------------------------------------
void
TyphoonHQuickInterface::clearMediaItems()
{
    return _mediaList.clear();
}

//-----------------------------------------------------------------------------
void
TyphoonHQuickInterface::appendMediaItem(TyphoonMediaItem* p)
{
    _mediaList.append(p);
}

//-----------------------------------------------------------------------------
TyphoonMediaItem*
TyphoonHQuickInterface::mediaItem(QQmlListProperty<TyphoonMediaItem>* list, int i)
{
    return reinterpret_cast<TyphoonHQuickInterface*>(list->data)->mediaItem(i);
}

//-----------------------------------------------------------------------------
int
TyphoonHQuickInterface::mediaCount(QQmlListProperty<TyphoonMediaItem>* list)
{
    return reinterpret_cast<TyphoonHQuickInterface*>(list->data)->mediaCount();
}

void
TyphoonHQuickInterface::appendMediaItem(QQmlListProperty<TyphoonMediaItem>* list, TyphoonMediaItem* p)
{
    reinterpret_cast<TyphoonHQuickInterface*>(list->data)->appendMediaItem(p);
}

void
TyphoonHQuickInterface::clearMediaItems(QQmlListProperty<TyphoonMediaItem>* list)
{
    reinterpret_cast<TyphoonHQuickInterface*>(list->data)->clearMediaItems();
}

//-----------------------------------------------------------------------------
void
TyphoonHQuickInterface::refreshMeadiaList()
{
    clearMediaItems();
    _selectedCount = 0;
    emit selectedCountChanged();
    QString photoPath;
    QStringList nameFilters;
    if(_browseVideos) {
        photoPath = qgcApp()->toolbox()->settingsManager()->appSettings()->savePath()->rawValue().toString() + QStringLiteral("/Video");
        nameFilters << "*" YUNEEC_VIDEO_EXTENSION;
    } else {
        photoPath = qgcApp()->toolbox()->settingsManager()->appSettings()->savePath()->rawValue().toString() + QStringLiteral("/Photo");
        nameFilters << "*.jpg" << "*.JPG";
    }
    QDir photoDir = QDir(photoPath);
    photoDir.setFilter(QDir::Files | QDir::Readable | QDir::NoSymLinks);
    photoDir.setNameFilters(nameFilters);
    QStringList list = photoDir.entryList();
    foreach (QString fileName, list) {
        TyphoonMediaItem* pItem = new TyphoonMediaItem(this, fileName);
        appendMediaItem(pItem);
    }
    //-- Asking QDir above to sort by date doesn't always work because it has a low granularity (2 seconds). As the
    //   files are named based on their date/time to milliseconds, we sort it ourselves here.
    std::sort(_mediaList.begin(), _mediaList.end(), [](TyphoonMediaItem* a, TyphoonMediaItem* b) { return a->fileName() > b->fileName(); });
    emit mediaListChanged();
    /*
    for (int i = 0; i < _mediaList.size(); i++) {
        qDebug() << _mediaList.at(i)->fileName();
    }
    */
}

//-----------------------------------------------------------------------------
void
TyphoonHQuickInterface::setBrowseVideos(bool video)
{
    _browseVideos = video;
    emit browseVideosChanged();
    refreshMeadiaList();
}

//-----------------------------------------------------------------------------
void
TyphoonHQuickInterface::selectAllMedia(bool selected)
{
    for(int i = 0; i < _mediaList.size(); i++) {
        if(_mediaList[i]) {
            TyphoonMediaItem* pItem = qobject_cast<TyphoonMediaItem*>(_mediaList[i]);
            if(pItem) {
                pItem->setSelected(selected);
            }
        }
    }
}
//-----------------------------------------------------------------------------
void
TyphoonHQuickInterface::deleteSelectedMedia()
{
    QStringList toDelete;
    for(int i = 0; i < _mediaList.size(); i++) {
        if(_mediaList[i]) {
            TyphoonMediaItem* pItem = qobject_cast<TyphoonMediaItem*>(_mediaList[i]);
            if(pItem && pItem->selected()) {
                toDelete << pItem->fileName();
                pItem->setSelected(false);
            }
        }
    }
    QString photoPath;
    if(_browseVideos) {
        photoPath = qgcApp()->toolbox()->settingsManager()->appSettings()->savePath()->rawValue().toString() + QStringLiteral("/Video/");
    } else {
        photoPath = qgcApp()->toolbox()->settingsManager()->appSettings()->savePath()->rawValue().toString() + QStringLiteral("/Photo/");
    }
    foreach (QString fileName, toDelete) {
        QString filePath = photoPath + fileName;
        QFile::remove(filePath);
    }
    refreshMeadiaList();
}

//-----------------------------------------------------------------------------
void
TyphoonHQuickInterface::initExport()
{
    _copyingFiles = false;
    _updateProgress = 0;
    _copyingDone = false;
    _copyMessage.clear();
    emit updateProgressChanged();
    emit copyingFilesChanged();
    emit copyMessageChanged();
    emit copyingDoneChanged();
}

//-----------------------------------------------------------------------------
void
TyphoonHQuickInterface::exportData(bool exportUTM, bool exportSkyward)
{
    _copyingFiles = true;
    _updateProgress = 0;
    emit updateProgressChanged();
    emit copyingFilesChanged();
    _exportMessage(QString(tr("Searching files...")));
    _exporter = new YExportFiles();
    connect(_exporter, &YExportFiles::completed,        this, &TyphoonHQuickInterface::_exportCompleted);
    connect(_exporter, &YExportFiles::progress,    this, &TyphoonHQuickInterface::_copyProgress);
    connect(_exporter, &YExportFiles::message,          this, &TyphoonHQuickInterface::_exportMessage);
    _exportMessage(QString(tr("Copying files...")));
    _exporter->exportData(exportUTM, exportSkyward);
}

//-----------------------------------------------------------------------------
void
TyphoonHQuickInterface::cancelExportData()
{
    if(_exporter) {
        _exporter->cancel();
        _exportMessage(QString(tr("Canceling...")));
    }
}

//-----------------------------------------------------------------------------
void
TyphoonHQuickInterface::_exportCompleted()
{
    if(_exporter) {
        _exporter->deleteLater();
        _exporter = NULL;
    }
    _copyingFiles = false;
    _copyingDone = true;
    emit copyingFilesChanged();
    emit copyingDoneChanged();
}

//-----------------------------------------------------------------------------
void
TyphoonHQuickInterface::_copyProgress(quint32 totalCount, quint32 curCount)
{
    if(!totalCount) {
        _updateProgress = 0;
    } else {
        _updateProgress = (int)((float)curCount / (float)totalCount * 100.0f);
    }
    emit updateProgressChanged();
}

//-----------------------------------------------------------------------------
void
TyphoonHQuickInterface::_exportMessage(QString message)
{
    _copyMessage = message;
    emit copyMessageChanged();
}

//-----------------------------------------------------------------------------
void
TyphoonHQuickInterface::importMission()
{
    _copyingFiles = true;
    emit copyingFilesChanged();
    QTimer::singleShot(10, this, &TyphoonHQuickInterface::_importMissions);
}

//-----------------------------------------------------------------------------
void
TyphoonHQuickInterface::_importMissions()
{
#if defined(QT_DEBUG) && !defined (__android__)
    QString sourcePath = QStringLiteral("/tmp");
#else
    QString sourcePath = QStringLiteral("/storage/sdcard1");
#endif
    QDir destDir(sourcePath);
    if (!destDir.exists()) {
        _exportMessage(QString(tr("Source path missing. Make sure you have a (FAT32 Formatted) microSD card loaded.")));
        return;
    }
    int totalFiles = 0;
    int curCount = 0;
    //-- Collect Files
    _exportMessage(QString(tr("Importing mission files...")));
    QFileInfoList fil;
    QDirIterator it(sourcePath, QStringList() << "*.plan", QDir::Files, QDirIterator::Subdirectories);
    while(it.hasNext()) {
        QFileInfo fi(it.next());
        totalFiles++;
        fil << fi;
    }
    _copyProgress(totalFiles, 0);
    QString missionDir = qgcApp()->toolbox()->settingsManager()->appSettings()->missionSavePath();
    while(fil.size()) {
        QFileInfo fi = fil.first();
        fil.removeFirst();
        QString output = missionDir + "/" + fi.fileName();
        QFileInfo fo(output);
        if(fo.exists()) {
            QFile::remove(fo.filePath());
        }
        if(!QFile::copy(fi.filePath(), fo.filePath())) {
            _exportMessage(QString(tr("Error importing %1").arg(fi.filePath())));
            _copyingFiles = false;
            _copyingDone = true;
            emit copyingFilesChanged();
            emit copyingDoneChanged();
            return;
        }
        _copyProgress(totalFiles, ++curCount);
        qApp->processEvents();
    }
    _exportMessage(QString(tr("%1 files imported").arg(totalFiles)));
    _copyingFiles = false;
    _copyingDone = true;
    emit copyingFilesChanged();
    emit copyingDoneChanged();
}

//-----------------------------------------------------------------------------
void
TyphoonHQuickInterface::manualBind()
{
#if defined(__androidx86__)
    if(_pHandler) {
        _pHandler->enterBindMode(true);
    }
#endif
}

//-----------------------------------------------------------------------------
void
TyphoonHQuickInterface::startCalibration()
{
#if defined(__androidx86__)
    if(_pHandler) {
        _pHandler->startCalibration();
    }
#endif
}

//-----------------------------------------------------------------------------
void
TyphoonHQuickInterface::stopCalibration()
{
#if defined(__androidx86__)
    if(_pHandler) {
        _pHandler->stopCalibration();
    }
#endif
}

//-----------------------------------------------------------------------------
bool
TyphoonHQuickInterface::rcActive()
{
#if defined(__androidx86__)
    if(_pHandler) {
        return _pHandler->rcActive();
    }
#endif
    return false;
}

//-----------------------------------------------------------------------------
static bool
compareRSSI(const QVariant &v1, const QVariant &v2)
{
    TyphoonSSIDItem* s1 = qobject_cast<TyphoonSSIDItem*>(qvariant_cast<QObject*>(v1));
    TyphoonSSIDItem* s2 = qobject_cast<TyphoonSSIDItem*>(qvariant_cast<QObject*>(v2));
    //qDebug() << "Sort" << s1->rssi() << s2->rssi();
    return s1->rssi() > s2->rssi();
}

//-----------------------------------------------------------------------------
TyphoonSSIDItem*
TyphoonHQuickInterface::_findSsid(QString ssid, int rssi)
{
    for(QVariantList::const_iterator it = _ssidList.begin(); it != _ssidList.end(); ++it) {
        TyphoonSSIDItem* s = qobject_cast<TyphoonSSIDItem*>(qvariant_cast<QObject*>(*it));
        if(s->ssid() == ssid) {
            s->setRssi(rssi);
            return s;
        }
    }
    return NULL;
}

//-----------------------------------------------------------------------------
void
TyphoonHQuickInterface::_clearSSids()
{
    for(QVariantList::const_iterator it = _ssidList.begin(); it != _ssidList.end(); ++it) {
        TyphoonSSIDItem* s = qobject_cast<TyphoonSSIDItem*>(qvariant_cast<QObject*>(*it));
        if(s) {
           delete s;
        }
    }
    _ssidList.clear();
}

//-----------------------------------------------------------------------------
void
TyphoonHQuickInterface::_newSSID(QString ssid, int rssi)
{
    qCDebug(YuneecLog) << "New SSID" << ssid << rssi;
#if !defined(QT_DEBUG)
    if(ssid.startsWith("CGOET") || ssid.startsWith("E10T") || ssid.startsWith("E90_") || ssid.startsWith("E50_")) {
#endif
        if(!_findSsid(ssid, rssi)) {
            TyphoonSSIDItem* ssidInfo = new TyphoonSSIDItem(ssid, rssi);
            _ssidList.append(QVariant::fromValue((TyphoonSSIDItem*)ssidInfo));
            std::sort(_ssidList.begin(), _ssidList.end(), compareRSSI);
            emit ssidListChanged();
        }
#if !defined(QT_DEBUG)
    }
#endif
}

//-----------------------------------------------------------------------------
void
TyphoonHQuickInterface::_newRSSI()
{
    emit rssiChanged();
}

//-----------------------------------------------------------------------------
void
TyphoonHQuickInterface::_scanComplete()
{
    if(_scanEnabled) {
        _scanTimer.setSingleShot(true);
        _scanTimer.start(2000);
    }
    _scanningWiFi = false;
    emit scanningWiFiChanged();
}

//-----------------------------------------------------------------------------
void
TyphoonHQuickInterface::_authenticationError()
{
    qCDebug(YuneecLog) << "TyphoonHQuickInterface::_authenticationError()";
    //-- Remove configuration if we had it
    if(_configurations.contains(_ssid)) {
        _configurations.remove(_ssid);
        _saveWifiConfigurations();
    }
    _bindingWiFi = false;
    emit bindingWiFiChanged();
    emit connectedSSIDChanged();
    emit authenticationError();
    startScan();
}

//-----------------------------------------------------------------------------
void
TyphoonHQuickInterface::_wifiConnected()
{
    qCDebug(YuneecLog) << "TyphoonHQuickInterface::_wifiConnected()";
    //-- Save configuration
    _configurations[_ssid] = _password;
    _saveWifiConfigurations();
    _bindingWiFi = false;
    emit isDefaultPwdChanged();
    emit bindingWiFiChanged();
    emit connectedSSIDChanged();
    emit wifiConnectedChanged();
}

//-----------------------------------------------------------------------------
void
TyphoonHQuickInterface::_wifiDisconnected()
{
    qCDebug(YuneecLog) << "TyphoonHQuickInterface::_wifiDisconnected()";
    emit connectedSSIDChanged();
    emit wifiConnectedChanged();
    if(_videoReceiver) {
        _videoReceiver->stop();
        emit thermalImagePresentChanged();
    }
}

//-----------------------------------------------------------------------------
void
TyphoonHQuickInterface::_batteryUpdate()
{
    emit rcBatteryChanged();
}

//-----------------------------------------------------------------------------
void
TyphoonHQuickInterface::_bindTimeout()
{
    if(_bindingWiFi) {
        _bindingWiFi = false;
        emit bindingWiFiChanged();
        emit bindTimeout();
        startScan();
    }
}

//-----------------------------------------------------------------------------
void
TyphoonHQuickInterface::_armedChanged(bool armed)
{
    if(armed) {
        _flightTime.start();
        _flightTimer.start(500);
    } else {
        _flightTimer.stop();
    }
}

//-----------------------------------------------------------------------------
void
TyphoonHQuickInterface::_rawChannelsChanged()
{
    emit rawChannelChanged();
}

//-----------------------------------------------------------------------------
void
TyphoonHQuickInterface::_calibrationCompleteChanged()
{
    emit calibrationCompleteChanged();
}

//-----------------------------------------------------------------------------
void
TyphoonHQuickInterface::_rcActiveChanged()
{
    emit rcActiveChanged();
}

//-----------------------------------------------------------------------------
void
TyphoonHQuickInterface::_calibrationStateChanged()
{
    emit calibrationStateChanged();
}

//-----------------------------------------------------------------------------
QString
TyphoonHQuickInterface::flightTime()
{
    return QTime(0, 0).addMSecs(_flightTime.elapsed()).toString("hh:mm:ss");
}

//-----------------------------------------------------------------------------
void
TyphoonHQuickInterface::_flightUpdate()
{
    emit flightTimeChanged();
}

//-----------------------------------------------------------------------------
void
TyphoonHQuickInterface::_loadWifiConfigurations()
{
    qCDebug(YuneecLog) << "Loading WIFI Configurations";
    QSettings settings;
    settings.beginGroup(kWifiConfig);
    QStringList keys = settings.childKeys();
    foreach (QString key, keys) {
         _configurations[key] = settings.value(key).toString();
         qCDebug(YuneecLog) << key << _configurations[key];
    }
    settings.endGroup();
}

//-----------------------------------------------------------------------------
void
TyphoonHQuickInterface::_saveWifiConfigurations()
{
    QSettings settings;
    settings.beginGroup(kWifiConfig);
    settings.remove("");
    QMap<QString, QString>::const_iterator i = _configurations.constBegin();
    while (i != _configurations.constEnd()) {
        if(!i.key().isEmpty()) {
            settings.setValue(i.key(), i.value());
        }
        i++;
     }
    settings.endGroup();
}

//-----------------------------------------------------------------------------
void
TyphoonHQuickInterface::_distanceSensor(int minDist, int maxDist, int curDist)
{
    //qDebug() << minDist << maxDist << curDist;
    if(_distSensorMin != minDist) {
        _distSensorMin = minDist;
        emit distSensorMinChanged();
    }
    if(_distSensorMax != maxDist) {
        _distSensorMax = maxDist;
        emit distSensorMaxChanged();
    }
    if(_distSensorCur != curDist) {
        _distSensorCur = curDist;
        emit distSensorCurChanged();
    }
}

//-----------------------------------------------------------------------------
void
TyphoonHQuickInterface::setThermalMode(ThermalViewMode mode)
{
    QSettings settings;
    settings.setValue(kThermalMode, (uint32_t)mode);
    _thermalMode = mode;
    emit thermalModeChanged();
}

//-----------------------------------------------------------------------------
void
TyphoonHQuickInterface::_resetFirstRun(bool skipSecond)
{
    qCDebug(YuneecLog) << "Reset firstRun";
    _firstRun = false;
    QSettings settings;
    settings.setValue(kFirstRun, false);
    if(!skipSecond) {
        settings.setValue(kSecondRun, true);
    }
    emit firstRunChanged();
}

//-----------------------------------------------------------------------------
void
TyphoonHQuickInterface::setThermalOpacity(double val)
{
    if(val < 0.0) val = 0.0;
    if(val > 100.0) val = 100.0;
    if(_thermalOpacity != val) {
        _thermalOpacity = val;
        QSettings settings;
        settings.setValue(kThermalOpacity, val);
        emit thermalOpacityChanged();
    }
}

//-----------------------------------------------------------------------------
void
TyphoonHQuickInterface::_imageFileChanged()
{
    //-- Capture thermal image as well (if any)
    if(thermalImagePresent() && qgcApp()->toolbox()->videoManager()->videoReceiver()) {
        QString photoPath = qgcApp()->toolbox()->videoManager()->videoReceiver()->imageFile();
        if(!photoPath.isEmpty()) {
            photoPath.replace(".jpg", tr("-Thermal.jpg"));
            _videoReceiver->grabImage(photoPath);
        }
    }
}

//-----------------------------------------------------------------------------
void
TyphoonMediaItem::setSelected (bool sel)
{
    if(_selected != sel) {
        _selected = sel;
        emit selectedChanged();
        if(sel) {
            _parent->_selectedCount++;
        } else {
            _parent->_selectedCount--;
        }
        emit _parent->selectedCountChanged();
    }
}