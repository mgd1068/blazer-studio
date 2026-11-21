#pragma once
#include "controllersession.h"
#include "simulator.h"
#include "accessorycontract.h"
#include <QElapsedTimer>
#include <memory>
class Document;
class Machine : public Simulator {
    Q_OBJECT
    Q_PROPERTY(bool simulation READ simulation NOTIFY changed)
    Q_PROPERTY(QVariantList capabilities READ capabilities CONSTANT)
    Q_PROPERTY(bool connectionVerified READ connectionVerified NOTIFY changed)
    Q_PROPERTY(QString motionSafetyError READ motionSafetyError NOTIFY changed)
    Q_PROPERTY(QString homingSafetyError READ homingSafetyError NOTIFY changed)
    Q_PROPERTY(QString extractionSafetyError READ extractionSafetyError NOTIFY changed)
    Q_PROPERTY(QStringList processingSafetyErrors READ processingSafetyErrors NOTIFY changed)
    Q_PROPERTY(QStringList ports READ ports NOTIFY changed)
  public:
    enum class HardwareAccess { Allowed, Disabled };
    explicit Machine(Document *document, QObject *parent = nullptr,
                     HardwareAccess access = HardwareAccess::Allowed);
    bool simulation() const { return !live; }
    QStringList ports() const;
    bool connectionVerified() const { return live && session && session->status().verified; }
    QVariantList capabilities() const;
    QVariantMap controllerDiagnosticSnapshot() const;
    void setPreviewDeviceBinding(const QByteArray &binding);
    Q_INVOKABLE void refreshPreviewPosition();
    QString motionSafetyError() const {
        return live && session ? session->status().motionSafetyError : QString();
    }
    QString homingSafetyError() const {
        return live && session ? session->status().homingSafetyError : QString();
    }
    QString extractionSafetyError() const {
        return live ? accessory.preflightError(accessoryClock.elapsed()) : QString();
    }
    QStringList processingSafetyErrors() const {
        if (!live) return {};
        QStringList errors;
        if (!motionSafetyError().isEmpty()) errors << motionSafetyError();
        if (!positionReferenced()) {
            errors << "Maschinenreferenz fehlt.";
            if (!homingSafetyError().isEmpty()) errors << homingSafetyError();
        }
        if (!extractionSafetyError().isEmpty()) errors << extractionSafetyError();
        return errors;
    }
    Q_INVOKABLE bool setOverrides(int feed, int power) {
        return live && session && session->overrides(feed, power);
    }

    bool connectHardware(const QString &port);
    Q_INVOKABLE void useSimulator();
    Q_INVOKABLE void refreshPorts() { emit changed(); }
    Q_INVOKABLE void setWorkZero() {
        if (live && session)
            session->zero();
    }
    bool ready() const override { return live ? session->status().ready : Simulator::ready(); }
    bool positionReferenced() const override {
        return live ? session->status().positionReferenced : Simulator::positionReferenced();
    }
    bool connected() const override {
        return live ? session->status().connected : Simulator::connected();
    }
    QString state() const override { return live ? session->status().state : Simulator::state(); }
    double x() const override { return live ? session->status().position.x() : Simulator::x(); }
    double y() const override { return live ? session->status().position.y() : Simulator::y(); }
    double progress() const override {
        return live ? session->status().progress : Simulator::progress();
    }
    bool firing() const override { return live ? session->status().firing : Simulator::firing(); }
    bool busy() const override { return live ? session->status().busy : Simulator::busy(); }
    bool paused() const override {
        if (!live) return Simulator::paused();
        const auto status = session->status();
        return status.paused && !status.referencing && !status.resetting;
    }
    bool running() const override {
        if (!live) return Simulator::running();
        const auto status = session->status();
        return status.executing && !status.paused && !status.referencing && !status.resetting;
    }
    QStringList log() const override { return live ? session->logEntries() : Simulator::log(); }
    void toggleConnection() override;
    void jog(double dx, double dy) override;
    void home() override;
    void focus(bool on) override;
    void frame(const QVariantMap &b) override;
    void start() override;
    void pause() override;
    void stop() override;
    void disconnectFault() override;

  private:
    friend class GrblTest; // Synthetic PTY commissioning, never a public release switch.
    Document *document;
    std::unique_ptr<ControllerSession> session;
    void syncDeviceProfile();
    bool live = false;
    const HardwareAccess hardwareAccess;
    AccessoryContract accessory{true};
    QElapsedTimer accessoryClock;
    bool accessoryJobActive = false;
    bool accessoryFocusActive = false;
    QTimer accessoryWatchdog;
    QStringList lastProcessingSafetyErrors;
    bool sessionWasVerified = false, sessionWasReferenced = false;
    QByteArray previewDeviceBinding, simulatorFrameNonce, previewFrame;
    QPointF previewPoint;
    quint64 previewPositionRevision = 0;
    bool updatingPreview = false;
};
