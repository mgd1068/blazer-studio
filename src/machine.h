#pragma once
#include "grbl.h"
#include "simulator.h"
class Document;
class Machine : public Simulator {
    Q_OBJECT
    Q_PROPERTY(bool simulation READ simulation NOTIFY changed)
    Q_PROPERTY(QVariantList capabilities READ capabilities CONSTANT)
    Q_PROPERTY(QStringList ports READ ports NOTIFY changed)
  public:
    enum class HardwareAccess { Allowed, Disabled };
    explicit Machine(Document *document, QObject *parent = nullptr,
                     HardwareAccess access = HardwareAccess::Allowed);
    bool simulation() const { return !live; }
    QStringList ports() const;
    QVariantList capabilities() const;
    Q_INVOKABLE bool setOverrides(int feed, int power) {
        return live && grbl.overrides(feed, power);
    }

    Q_INVOKABLE bool connectHardware(const QString &port);
    Q_INVOKABLE void useSimulator();
    Q_INVOKABLE void refreshPorts() { emit changed(); }
    Q_INVOKABLE void setWorkZero() {
        if (live)
            grbl.zero();
    }
    bool ready() const override { return live ? grbl.ready() : Simulator::ready(); }
    bool connected() const override { return live ? grbl.connected() : Simulator::connected(); }
    QString state() const override { return live ? grbl.state : Simulator::state(); }
    double x() const override { return live ? grbl.position.x() : Simulator::x(); }
    double y() const override { return live ? grbl.position.y() : Simulator::y(); }
    double progress() const override { return live ? grbl.progress : Simulator::progress(); }
    bool firing() const override { return live ? grbl.firing : Simulator::firing(); }
    bool busy() const override { return live ? grbl.busy() : Simulator::busy(); }
    bool paused() const override { return live ? grbl.paused : Simulator::paused(); }
    bool running() const override {
        return live ? grbl.executing() && !grbl.paused : Simulator::running();
    }
    QStringList log() const override { return live ? grbl.log : Simulator::log(); }
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
    Document *document;
    GrblSession grbl;
    bool live = false;
    const HardwareAccess hardwareAccess;
};
