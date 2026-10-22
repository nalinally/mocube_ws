#include "PcLink.h"

#include "Config.h"

void PcLink::connectWiFi(
    const char* ssid,
    const char* password,
    const std::function<void()>& onWaiting)
{
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);

        Serial.print(".");
        if (onWaiting) {
            onWaiting();
        }
    }

    Serial.println();

    Serial.println("WiFi connected!");

    Serial.print("IP: ");
    Serial.println(WiFi.localIP());

    Serial.print("MAC: ");
    Serial.println(WiFi.macAddress());
}

void PcLink::begin(uint16_t port)
{
    _port = port;
    _mac = WiFi.macAddress();

    // 送信元ポートもこのポートになるので、PC は同じポートに返信してくる
    _udp.begin(_port);

    broadcast(protocol::makeRegister(_mac));
    _lastRegister = millis();
}

bool PcLink::isWiFiConnected() const
{
    return WiFi.status() == WL_CONNECTED;
}

void PcLink::handleWiFiLost()
{
    _pcConnected = false;
}

void PcLink::onCommand(const char* command, CommandHandler handler)
{
    if (_handlerCount >= MAX_HANDLERS) {
        Serial.printf("PcLink: too many handlers, ignored %s\n", command);
        return;
    }

    _handlers[_handlerCount].command = command;
    _handlers[_handlerCount].handler = handler;
    ++_handlerCount;
}

bool PcLink::update(unsigned long now)
{
    bool changed = receive();

    if (_id < 0 &&
        now - _lastRegister >= config::REGISTER_INTERVAL_MS) {

        _lastRegister = now;
        broadcast(protocol::makeRegister(_mac));
    }

    if (now - _lastHeartbeat >= config::HEARTBEAT_INTERVAL_MS) {

        _lastHeartbeat = now;
        broadcast(protocol::makeHeartbeat(_mac));
    }

    if (now - _lastPcAck >= config::PC_TIMEOUT_MS && _pcConnected) {

        _pcConnected = false;

        Serial.println("PC connection lost");

        changed = true;
    }

    return changed;
}

IPAddress PcLink::broadcastAddress() const
{
    const IPAddress ip = WiFi.localIP();
    const IPAddress mask = WiFi.subnetMask();

    IPAddress broadcast;

    for (int i = 0; i < 4; ++i) {
        broadcast[i] =
            (ip[i] & mask[i]) |
            (~mask[i] & 0xFF);
    }

    return broadcast;
}

void PcLink::broadcast(const String& message)
{
    _udp.beginPacket(broadcastAddress(), _port);
    _udp.print(message);
    _udp.endPacket();
}

bool PcLink::receive()
{
    const int packetSize = _udp.parsePacket();

    if (packetSize <= 0) {
        return false;
    }

    char buffer[RX_BUFFER_SIZE];

    const int length =
        _udp.read(buffer, sizeof(buffer) - 1);

    if (length <= 0) {
        return false;
    }

    buffer[length] = '\0';

    protocol::Packet packet;
    protocol::parse(String(buffer), packet);

    return dispatch(packet);
}

bool PcLink::dispatch(const protocol::Packet& packet)
{
    const String& command = packet.command();

    // ID assignment: "ID,<id>"
    if (command == protocol::command::ID && packet.count >= 2) {

        _id = packet.at(1).toInt();

        _lastPcAck = millis();
        _pcConnected = true;

        Serial.print("Assigned ID: ");
        Serial.println(_id);

        return true;
    }

    // Heartbeat ACK: "ACK,<id>"
    // ACK は PC がこのモジュール宛てに返すので、ID が違えば PC の台帳に合わせる
    // (PC の台帳を消した / 別の台帳で起動した場合に食い違う)
    if (command == protocol::command::ACK && packet.count >= 2) {

        const int receivedID = packet.at(1).toInt();

        if (receivedID != _id) {
            Serial.printf("ID changed by ACK: %d -> %d\n", _id, receivedID);
            _id = receivedID;
        }

        _lastPcAck = millis();
        _pcConnected = true;

        Serial.println("PC heartbeat OK");

        return true;
    }

    for (int i = 0; i < _handlerCount; ++i) {
        if (command == _handlers[i].command) {
            _handlers[i].handler(packet);
            return false;
        }
    }

    // 他モジュールの REGISTER / HEARTBEAT / NEIGHBOR ブロードキャストも
    // 届くので、未知のコマンドは黙って無視する。
    return false;
}
